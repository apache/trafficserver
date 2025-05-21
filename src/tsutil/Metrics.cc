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
#include <memory>
#include <mutex>
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

void
Metrics::Storage::addBlob() // The mutex must be held before calling this!
{
  auto blob = std::make_unique<Metrics::NamesAndAtomics>();

  debug_assert(blob);
  debug_assert(_cur_blob < MAX_BLOBS);

  _blobs[++_cur_blob] = std::move(blob);
  _cur_off            = 0;
}

Metrics::IdType
Metrics::Storage::create(std::string_view name)
{
  std::lock_guard lock(_mutex);
  auto            it = _lookups.find(name);

  if (it != _lookups.end()) {
    return it->second;
  }

  Metrics::IdType           id    = _makeId(_cur_blob, _cur_off);
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
  std::lock_guard lock(_mutex);
  auto            it = _lookups.find(name);

  if (it != _lookups.end()) {
    return it->second;
  }

  return NOT_FOUND;
}

Metrics::AtomicType *
Metrics::Storage::lookup(Metrics::IdType id, std::string_view *out_name) const
{
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

  return &((std::get<1>(*blob)[offset]));
}

Metrics::AtomicType *
Metrics::Storage::lookup(const std::string_view name, Metrics::IdType *out_id) const
{
  Metrics::IdType      id     = lookup(name);
  Metrics::AtomicType *result = nullptr;

  if (id != NOT_FOUND) {
    result = lookup(id);
  }

  if (nullptr != out_id) {
    *out_id = id;
  }

  return result;
}

std::string_view
Metrics::Storage::name(Metrics::IdType id) const
{
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

Metrics::SpanType
Metrics::Storage::createSpan(size_t size, Metrics::IdType *id)
{
  release_assert(size <= MAX_SIZE);
  std::lock_guard lock(_mutex);

  if (_cur_off + size > MAX_SIZE) {
    addBlob();
  }

  Metrics::IdType           span_start = _makeId(_cur_blob, _cur_off);
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
  auto [blob_ix, offset]         = _splitID(id);
  Metrics::NamesAndAtomics *blob = _blobs[blob_ix].get();

  // We can only rename Metrics that are already allocated
  if (!blob || (blob_ix == _cur_blob && offset > _cur_off)) {
    return false;
  }

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

  _it = _makeId(blob, offset);
}

namespace details
{
  struct DerivedMetric {
    Metrics::IdType                    metric;
    std::vector<Metrics::AtomicType *> derived_from;
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
        int64_t sum = 0;

        for (auto d : m.derived_from) {
          sum += d->load();
        }
        instance[m.metric].store(sum);
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
    dm.metric = instance._create(m.derived_name);

    for (auto &d : m.derived_from) {
      if (std::holds_alternative<Metrics::AtomicType *>(d)) {
        dm.derived_from.push_back(std::get<Metrics::AtomicType *>(d));
      } else if (std::holds_alternative<Metrics::IdType>(d)) {
        dm.derived_from.push_back(instance.lookup(std::get<Metrics::IdType>(d)));
      } else if (std::holds_alternative<std::string_view>(d)) {
        dm.derived_from.push_back(instance.lookup(instance.lookup(std::get<std::string_view>(d))));
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

} // namespace ts
