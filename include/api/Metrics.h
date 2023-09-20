/** @file

  A brief file description

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

#pragma once

#include <array>
#include <optional>
#include <unordered_map>
#include <tuple>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include "tscore/ink_assert.h"

namespace ts
{
class Metrics
{
private:
  using self_type = Metrics;

public:
  using IntType = std::atomic<int64_t>;
  using IdType  = int32_t; // Could be a tuple, but one way or another, they have to be combined to an int32_t.

  static constexpr uint16_t METRICS_MAX_BLOBS = 8192;
  static constexpr uint16_t METRICS_MAX_SIZE  = 2048;                               // For a total of 16M metrics
  static constexpr IdType NOT_FOUND           = std::numeric_limits<IdType>::min(); // <16-bit,16-bit> = <blob-index,offset>
  static const auto MEMORY_ORDER              = std::memory_order_relaxed;

private:
  using NameAndId       = std::tuple<std::string, IdType>;
  using NameContainer   = std::array<NameAndId, METRICS_MAX_SIZE>;
  using AtomicContainer = std::array<IntType, METRICS_MAX_SIZE>;
  using MetricStorage   = std::tuple<NameContainer, AtomicContainer>;
  using MetricBlobs     = std::array<MetricStorage *, METRICS_MAX_BLOBS>;
  using LookupTable     = std::unordered_map<std::string_view, IdType>;

public:
  Metrics(const self_type &)              = delete;
  self_type &operator=(const self_type &) = delete;
  Metrics &operator=(Metrics &&)          = delete;
  Metrics(Metrics &&)                     = delete;

  virtual ~Metrics()
  {
    for (size_t i = 0; i <= _cur_blob; ++i) {
      delete _blobs[i];
    }
  }

  Metrics()
  {
    _blobs[0] = new MetricStorage();
    ink_release_assert(_blobs[0]);
    ink_release_assert(0 == newMetric("proxy.node.api.metrics.bad_id")); // Reserve slot 0 for errors, this should always be 0
  }

  // Singleton
  static Metrics &getInstance();

  // Yes, we don't return objects here, but rather ID's and atomic's directly. Treat
  // the std::atomic<int64_t> as the underlying class for a single metric, and be happy.
  IdType newMetric(const std::string_view name);
  IdType lookup(const std::string_view name) const;
  IntType *lookup(IdType id, std::string_view *name = nullptr) const;

  std::optional<IntType *>
  lookupPtr(const std::string_view name) const
  {
    IdType id = lookup(name);
    if (id != NOT_FOUND) {
      return lookup(id);
    }
    return std::nullopt;
  }

  // A bit of a convenience, since we use the ptr to the atomic frequently in the core
  IntType *
  newMetricPtr(const std::string_view name)
  {
    return lookup(newMetric(name));
  }

  IntType &
  operator[](IdType id)
  {
    return *lookup(id);
  }

  IdType
  operator[](const std::string_view name) const
  {
    return lookup(name);
  }

  int64_t
  increment(IdType id, uint64_t val = 1)
  {
    auto metric = lookup(id);

    return (metric ? metric->fetch_add(val, MEMORY_ORDER) : NOT_FOUND);
  }

  // ToDo: Do we even need these inc/dec functions?
  int64_t
  decrement(IdType id, uint64_t val = 1)
  {
    auto metric = lookup(id);

    return (metric ? metric->fetch_sub(val, MEMORY_ORDER) : NOT_FOUND);
  }

  std::string_view name(IdType id) const;

  bool
  valid(IdType id) const
  {
    auto [blob, entry] = _splitID(id);

    return (id >= 0 && ((blob < _cur_blob && entry < METRICS_MAX_SIZE) || (blob == _cur_blob && entry <= _cur_off)));
  }

  // Static methods to encapsulate access to the atomic's
  static void
  increment(IntType *metric, uint64_t val = 1)
  {
    ink_assert(metric);
    metric->fetch_add(val, MEMORY_ORDER);
  }

  static void
  decrement(IntType *metric, uint64_t val = 1)
  {
    ink_assert(metric);
    metric->fetch_sub(val, MEMORY_ORDER);
  }

  static int64_t
  read(IntType *metric)
  {
    ink_assert(metric);
    return metric->load();
  }

  static void
  write(IntType *metric, int64_t val)
  {
    ink_assert(metric);
    return metric->store(val);
  }

  class iterator
  {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type        = std::tuple<std::string_view, int64_t>;
    using difference_type   = ptrdiff_t;
    using pointer           = value_type *;
    using reference         = value_type &;

    iterator(const Metrics &m, IdType pos) : _metrics(m), _it(pos) {}

    iterator &
    operator++()
    {
      next();

      return *this;
    }

    iterator
    operator++(int)
    {
      iterator result = *this;

      next();

      return result;
    }

    value_type
    operator*() const
    {
      std::string_view name;
      auto metric = _metrics.lookup(_it, &name);

      return std::make_tuple(name, metric->load());
    }

    bool
    operator==(const iterator &o) const
    {
      return _it == o._it && std::addressof(_metrics) == std::addressof(o._metrics);
    }

    bool
    operator!=(const iterator &o) const
    {
      return _it != o._it || std::addressof(_metrics) != std::addressof(o._metrics);
    }

  private:
    void next();

    const Metrics &_metrics;
    IdType _it;
  };

  iterator
  begin() const
  {
    return iterator(*this, 0);
  }

  iterator
  end() const
  {
    _mutex.lock();
    int16_t blob   = _cur_blob;
    int16_t offset = _cur_off;
    _mutex.unlock();

    return iterator(*this, _makeId(blob, offset));
  }

  iterator
  find(const std::string_view name) const
  {
    auto id = lookup(name);

    if (id == NOT_FOUND) {
      return end();
    } else {
      return iterator(*this, id);
    }
  }

private:
  static constexpr std::tuple<uint16_t, uint16_t>
  _splitID(IdType value)
  {
    return std::make_tuple(static_cast<uint16_t>(value >> 16), static_cast<uint16_t>(value & 0xFFFF));
  }

  static constexpr IdType
  _makeId(uint16_t blob, uint16_t offset)
  {
    return (blob << 16 | offset);
  }

  static constexpr IdType
  _makeId(std::tuple<uint16_t, uint16_t> id)
  {
    return _makeId(std::get<0>(id), std::get<1>(id));
  }

  void _addBlob();

  mutable std::mutex _mutex;
  LookupTable _lookups;
  MetricBlobs _blobs;
  uint16_t _cur_blob = 0;
  uint16_t _cur_off  = 0;

}; // class Metrics

} // namespace ts
