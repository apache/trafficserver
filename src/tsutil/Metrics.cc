/** @file

  The implementations of the Metrics::Counter API class.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "tsutil/Assert.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>
#include "tsutil/Metrics.h"

namespace ts
{

Metrics &
Metrics::instance()
{
  // This is the singleton instance of the metrics storage class.
  static std::shared_ptr<Storage> _metrics_store = std::make_shared<Storage>();
  thread_local Metrics            _instance(_metrics_store);

  return _instance;
}

Metrics &
Metrics::hidden_instance()
{
  // Separate storage from instance(). Hidden metrics are never published.
  static std::shared_ptr<Storage> _hidden_store = std::make_shared<Storage>();
  thread_local Metrics            _instance(_hidden_store);

  return _instance;
}

void
Metrics::Storage::addBlob() // The mutex must be held before calling this!
{
  auto blob = std::make_unique<Metrics::NamesAndAtomics>();

  debug_assert(blob);
  // The write below is to _blobs[_cur_blob + 1], so the last usable blob index is MAX_BLOBS - 1.
  release_assert(_cur_blob < MAX_BLOBS - 1);

  _blobs[++_cur_blob] = std::move(blob);
  _cur_off            = 0;
}

Metrics::IdType
Metrics::Storage::create(std::string_view name, const MetricType type)
{
  ts::lock_guard lock(_mutex);
  auto           it = _lookups.find(name);

  if (it != _lookups.end()) {
    return it->second;
  }

  // The slot is written below and the bookkeeping only then advances, calling addBlob() once
  // _cur_off reaches MAX_SIZE. Refusing the final slot of the final blob keeps addBlob() from
  // ever being reached in an exhausted store, at a cost of one slot out of MAX_BLOBS * MAX_SIZE.
  if (_cur_blob >= MAX_BLOBS - 1 && _cur_off >= MAX_SIZE - 1) {
    return 0; // Slot 0 is the reserved bad_id. Cannot grow further.
  }

  Metrics::IdType           id    = _makeId(_cur_blob, _cur_off, type);
  Metrics::NamesAndAtomics *blob  = _blobs[_cur_blob].get();
  Metrics::NameStorage     &names = std::get<0>(*blob);

  names[_cur_off] = std::make_tuple(std::string(name), id);
  _lookups.emplace(std::get<0>(names[_cur_off]), id);

  if (++_cur_off >= MAX_SIZE) {
    addBlob(); // This resets _cur_off to 0 as well
  }

  return id;
}

Metrics::IdType
Metrics::Storage::lookup(const std::string_view name) const
{
  ts::lock_guard lock(_mutex);
  auto           it = _lookups.find(name);

  if (it != _lookups.end()) {
    return it->second;
  }

  return NOT_FOUND;
}

Metrics::AtomicType *
Metrics::Storage::lookup(Metrics::IdType id, std::string_view *out_name, Metrics::MetricType *out_type) const
{
  ts::lock_guard lock(_mutex);
  auto [blob_ix, offset]         = _splitID(id);
  Metrics::NamesAndAtomics *blob = _blobs[blob_ix].get();

  // Do a sanity check on the ID, to make sure we don't index outside of the realm of possibility.
  if (!blob || (blob_ix == _cur_blob && offset > _cur_off)) {
    blob   = _blobs[0].get();
    offset = 0;
  }

  if (out_name) {
    *out_name = std::get<0>(std::get<0>(*blob)[offset]);
  }

  if (out_type) {
    // don't trust the passed in id to get the type as it might have been manufactured (i.e. from iterators)
    // so get the type from the storage tuple.
    *out_type = _extractType(std::get<1>(std::get<0>(*blob)[offset]));
  }

  return &((std::get<1>(*blob)[offset]));
}

Metrics::AtomicType *
Metrics::Storage::lookup(const std::string_view name, Metrics::IdType *out_id, Metrics::MetricType *out_type) const
{
  Metrics::IdType      id     = lookup(name);
  Metrics::AtomicType *result = nullptr;

  if (id != NOT_FOUND) {
    result = lookup(id);
  }

  if (nullptr != out_id) {
    *out_id = id;
  }

  if (out_type && id != NOT_FOUND) {
    *out_type = _extractType(id);
  }

  return result;
}

std::string_view
Metrics::Storage::name(Metrics::IdType id) const
{
  ts::lock_guard lock(_mutex);
  auto [blob_ix, offset]         = _splitID(id);
  Metrics::NamesAndAtomics *blob = _blobs[blob_ix].get();

  // Do a sanity check on the ID, to make sure we don't index outside of the realm of possibility.
  if (!blob || (blob_ix == _cur_blob && offset > _cur_off)) {
    blob   = _blobs[0].get();
    offset = 0;
  }

  const std::string &result = std::get<0>(std::get<0>(*blob)[offset]);

  return result;
}

Metrics::MetricType
Metrics::Storage::type(IdType id) const
{
  return _extractType(id);
}

Metrics::SpanType
Metrics::Storage::createSpan(size_t size, Metrics::MetricType type, Metrics::IdType *id)
{
  release_assert(size <= MAX_SIZE);
  ts::lock_guard lock(_mutex);

  if (_cur_off + size > MAX_SIZE) {
    addBlob();
  }

  Metrics::IdType           span_start = _makeId(_cur_blob, _cur_off, type);
  Metrics::NamesAndAtomics *blob       = _blobs[_cur_blob].get();
  Metrics::AtomicStorage   &atomics    = std::get<1>(*blob);
  Metrics::SpanType         span       = Metrics::SpanType(&atomics[_cur_off], size);

  if (id) {
    *id = span_start;
  }

  _cur_off += size;

  return span;
}

bool
Metrics::Storage::rename(Metrics::IdType id, std::string_view name)
{
  ts::lock_guard lock(_mutex);
  auto [blob_ix, offset]         = _splitID(id);
  Metrics::NamesAndAtomics *blob = _blobs[blob_ix].get();

  // We can only rename Metrics that are already allocated
  if (!blob || (blob_ix == _cur_blob && offset > _cur_off)) {
    return false;
  }

  std::string &cur = std::get<0>(std::get<0>(*blob)[offset]);

  if (cur.length() > 0) {
    _lookups.erase(cur);
  }
  cur = name;
  _lookups.emplace(cur, id);

  return true;
}

// Iterator implementation
void
Metrics::iterator::next()
{
  auto [blob, offset] = _metrics._splitID(_it);

  if (++offset == MAX_SIZE) {
    ++blob;
    offset = 0;
  }

  _it = _makeId(blob, offset, MetricType::COUNTER);
}

namespace details
{
  struct DerivedMetric {
    Metrics::IdType                    metric;
    std::vector<Metrics::AtomicType *> derived_from;
    Metrics::Derived::Op               op{Metrics::Derived::Op::SUM};
  };

  struct DerivativeMetrics {
    std::vector<DerivedMetric> metrics;
    std::mutex                 metrics_lock;

    void
    update()
    {
      auto           &instance = Metrics::instance();
      std::lock_guard l(metrics_lock);

      for (auto &m : metrics) {
        if (m.derived_from.empty()) {
          continue;
        }

        // Seeded from the first source rather than from zero: a zero seed is correct only for
        // SUM, and would clamp every MIN result to <= 0.
        int64_t value = m.derived_from.front()->load();

        for (auto it = m.derived_from.begin() + 1; it != m.derived_from.end(); ++it) {
          int64_t const v = (*it)->load();

          switch (m.op) {
          case Metrics::Derived::Op::SUM:
            value += v;
            break;
          case Metrics::Derived::Op::MAX:
            value = std::max(value, v);
            break;
          case Metrics::Derived::Op::MIN:
            value = std::min(value, v);
            break;
          }
        }
        instance[m.metric].store(value);
      }
    }

    void
    push_back(const DerivedMetric &m)
    {
      std::lock_guard l(metrics_lock);
      metrics.push_back(std::move(m));
    }

    static DerivativeMetrics &
    instance()
    {
      static DerivativeMetrics theDerivedMetrics;
      return theDerivedMetrics;
    }
  };

} // namespace details

void
Metrics::Derived::derive(const std::initializer_list<Metrics::Derived::DerivedMetricSpec> &metrics)
{
  auto &instance = Metrics::instance();

  for (auto &m : metrics) {
    details::DerivedMetric dm{};
    dm.metric = instance._create(m.derived_name, m.derived_type);
    dm.op     = m.op;

    for (auto &d : m.derived_from) {
      Metrics::AtomicType *ptr = nullptr;

      if (std::holds_alternative<Metrics::AtomicType *>(d)) {
        ptr = std::get<Metrics::AtomicType *>(d);
      } else if (std::holds_alternative<Metrics::IdType>(d)) {
        auto id = std::get<Metrics::IdType>(d);
        ptr     = instance.valid(id) ? instance.lookup(id) : nullptr;
      } else {
        auto id = instance.lookup(std::get<std::string_view>(d));
        ptr     = (id != Metrics::NOT_FOUND) ? instance.lookup(id) : nullptr;
      }

      // A source that does not resolve is skipped. Passing an unresolved id to lookup() would
      // silently land on the reserved bad_id slot and contribute its value to the aggregate.
      if (ptr) {
        dm.derived_from.push_back(ptr);
      }
    }
    details::DerivativeMetrics::instance().push_back(dm);
  }
}

void
Metrics::Derived::update_derived()
{
  details::DerivativeMetrics::instance().update();
}

Metrics::StaticString &
Metrics::StaticString::instance()
{
  static Metrics::StaticString i{};
  return i;
}

void
Metrics::StaticString::_createString(const std::string &name, const std::string_view value)
{
  std::lock_guard lock(_mutex);
  _strings[name] = value;
}

std::optional<std::string_view>
Metrics::StaticString::lookup(const std::string &name) const
{
  std::lock_guard                 lock(_mutex);
  auto                            it = _strings.find(name);
  std::optional<std::string_view> result{};

  if (it != _strings.end()) {
    result = it->second;
  }

  return result;
}

} // namespace ts
