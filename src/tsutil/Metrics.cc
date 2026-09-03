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

  // Only the blob index is needed; the offset resets to zero below.
  auto const cur_blob = static_cast<uint16_t>(_next_free.load(std::memory_order_relaxed) >> 16);

  debug_assert(blob);
  // The write below is to _blobs[cur_blob + 1], so the last usable blob index is MAX_BLOBS - 1.
  release_assert(cur_blob < MAX_BLOBS - 1);

  _blobs[cur_blob + 1] = std::move(blob);

  // Publishes the blob and the offset reset as one value; the write above is sequenced before it.
  _next_free.store(_pack(cur_blob + 1, 0), std::memory_order_release);
}

Metrics::IdType
Metrics::Storage::create(std::string_view name, const MetricType type)
{
  std::lock_guard lock(_mutex);
  auto            it = _lookups.find(name);

  if (it != _lookups.end()) {
    return it->second;
  }

  // The slot is written below and the bookkeeping only then advances, calling addBlob() once the
  // offset reaches MAX_SIZE. Refusing the final slot of the final blob keeps addBlob() from ever
  // being reached in an exhausted store, at a cost of one slot out of MAX_BLOBS * MAX_SIZE.
  auto const [cur_blob, cur_off] = _splitID(static_cast<IdType>(_next_free.load(std::memory_order_relaxed)));

  if (cur_blob >= MAX_BLOBS - 1 && cur_off >= MAX_SIZE - 1) {
    return 0; // Slot 0 is the reserved bad_id. Cannot grow further.
  }

  Metrics::IdType           id    = _makeId(cur_blob, cur_off, type);
  Metrics::NamesAndAtomics *blob  = _blobs[cur_blob].get();
  Metrics::NameStorage     &names = std::get<0>(*blob);

  names[cur_off] = std::make_tuple(std::string(name), id);
  _lookups.emplace(std::get<0>(names[cur_off]), id);

  if (cur_off + 1 >= MAX_SIZE) {
    addBlob(); // Publishes the next blob with a zero offset.
  } else {
    // Publishes the slot's name.
    _next_free.store(_pack(cur_blob, cur_off + 1), std::memory_order_release);
  }

  return id;
}

Metrics::IdType
Metrics::Storage::lookup(const std::string_view name) const
{
  std::lock_guard lock(_mutex);
  auto            it = _lookups.find(name);

  if (it != _lookups.end()) {
    return it->second;
  }

  return NOT_FOUND;
}

Metrics::AtomicType *
Metrics::Storage::lookup(Metrics::IdType id, std::string_view *out_name, Metrics::MetricType *out_type) const
{
  auto [blob_ix, offset] = _splitID(id);

  // Anything not naming an allocated slot resolves to the reserved bad_id slot.
  if (!_is_allocated(id)) {
    blob_ix = 0;
    offset  = 0;
  }

  Metrics::NamesAndAtomics *blob = _blobs[blob_ix].get();

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
  auto [blob_ix, offset] = _splitID(id);

  // Anything not naming an allocated slot resolves to the reserved bad_id slot.
  if (!_is_allocated(id)) {
    blob_ix = 0;
    offset  = 0;
  }

  Metrics::NamesAndAtomics *blob = _blobs[blob_ix].get();

  const std::string &result = std::get<0>(std::get<0>(*blob)[offset]);

  return result;
}

Metrics::MetricType
Metrics::Storage::type(IdType id) const
{
  return _extractType(id);
}

bool
Metrics::Storage::rename(Metrics::IdType id, std::string_view name)
{
  // We can only rename Metrics that are already allocated
  if (!_is_allocated(id)) {
    return false;
  }

  auto [blob_ix, offset]         = _splitID(id);
  Metrics::NamesAndAtomics *blob = _blobs[blob_ix].get();

  std::string    &cur = std::get<0>(std::get<0>(*blob)[offset]);
  std::lock_guard lock(_mutex);

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

    void
    add_source(Metrics::IdType id, Metrics::AtomicType *source, Metrics::Derived::Op op)
    {
      if (!source) {
        return;
      }

      std::lock_guard l(metrics_lock);
      auto            it = std::find_if(metrics.begin(), metrics.end(), [id](DerivedMetric const &m) { return m.metric == id; });

      if (it == metrics.end()) {
        metrics.push_back(DerivedMetric{id, {source}, op});
        return;
      }
      // Already registered sources are skipped so repeated registration is harmless.
      if (std::find(it->derived_from.begin(), it->derived_from.end(), source) == it->derived_from.end()) {
        it->derived_from.push_back(source);
      }
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

void
Metrics::Derived::add_source(std::string_view derived_name, Metrics::MetricType type, Metrics::AtomicType *source, Op op)
{
  // Resolved here rather than in the helper because _create is private to Metrics.
  auto id = Metrics::instance()._create(derived_name, type);

  details::DerivativeMetrics::instance().add_source(id, source, op);
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
