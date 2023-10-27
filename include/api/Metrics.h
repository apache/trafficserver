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

#include "swoc/MemSpan.h"

#include "tscore/ink_assert.h"

namespace ts
{
class Metrics
{
private:
  using self_type = Metrics;

  class AtomicType
  {
    friend class Metrics;

  public:
    AtomicType()          = default;
    virtual ~AtomicType() = default;

    int64_t
    load()
    {
      return _value.load();
    }

    // ToDo: This is a little sketchy
    void
    store(int64_t val)
    {
      _value.store(val);
    }

  protected:
    std::atomic<int64_t> _value{0};
  };

public:
  using IdType   = int32_t; // Could be a tuple, but one way or another, they have to be combined to an int32_t.
  using SpanType = swoc::MemSpan<AtomicType>;

  static constexpr uint16_t MAX_BLOBS = 8192;
  static constexpr uint16_t MAX_SIZE  = 1024;                               // For a total of 8M metrics
  static constexpr IdType NOT_FOUND   = std::numeric_limits<IdType>::min(); // <16-bit,16-bit> = <blob-index,offset>
  static const auto MEMORY_ORDER      = std::memory_order_relaxed;

private:
  using NameAndId       = std::tuple<std::string, IdType>;
  using LookupTable     = std::unordered_map<std::string_view, IdType>;
  using NameStorage     = std::array<NameAndId, MAX_SIZE>;
  using AtomicStorage   = std::array<AtomicType, MAX_SIZE>;
  using NamesAndAtomics = std::tuple<NameStorage, AtomicStorage>;
  using BlobStorage     = std::array<NamesAndAtomics *, MAX_BLOBS>;

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
    _blobs[0] = new NamesAndAtomics();
    ink_release_assert(_blobs[0]);
    ink_release_assert(0 == _create("proxy.process.api.metrics.bad_id")); // Reserve slot 0 for errors, this should always be 0
  }

  // The singleton instance, owned by the Metrics class
  static Metrics &instance();

  // Yes, we don't return objects here, but rather ID's and atomic's directly. Treat
  // the std::atomic<int64_t> as the underlying class for a single metric, and be happy.
  IdType lookup(const std::string_view name) const;
  AtomicType *lookup(IdType id, std::string_view *name = nullptr) const;

  std::optional<AtomicType *>
  lookupPtr(const std::string_view name) const
  {
    IdType id = lookup(name);
    if (id != NOT_FOUND) {
      return lookup(id);
    }
    return std::nullopt;
  }

  bool rename(IdType id, const std::string_view name);

  AtomicType &
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

    return (metric ? metric->_value.fetch_add(val, MEMORY_ORDER) : NOT_FOUND);
  }

  int64_t
  decrement(IdType id, uint64_t val = 1)
  {
    auto metric = lookup(id);

    return (metric ? metric->_value.fetch_sub(val, MEMORY_ORDER) : NOT_FOUND);
  }

  std::string_view name(IdType id) const;

  bool
  valid(IdType id) const
  {
    auto [blob, entry] = _splitID(id);

    return (id >= 0 && ((blob < _cur_blob && entry < MAX_SIZE) || (blob == _cur_blob && entry <= _cur_off)));
  }

  // Static methods to encapsulate access to the atomic's
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

      return std::make_tuple(name, metric->_value.load());
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
    Metrics::IdType _it;
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
  // These are private, to assure that we don't use them by accident creating naked metrics
  IdType _create(const std::string_view name);
  SpanType _createSpan(size_t size, IdType *id = nullptr);

  // These are little helpers around managing the ID's
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
  BlobStorage _blobs;
  uint16_t _cur_blob = 0;
  uint16_t _cur_off  = 0;

public:
  // These are sort of factory classes, using the Metrics singleton for all storage etc.
  class Gauge
  {
  public:
    using self_type = Gauge;
    using SpanType  = Metrics::SpanType;

    class AtomicType : public Metrics::AtomicType
    {
    };

    static Metrics::IdType
    create(const std::string_view name)
    {
      auto &instance = Metrics::instance();

      return instance._create(name);
    }

    static AtomicType *
    createPtr(const std::string_view name)
    {
      auto &instance = Metrics::instance();

      return reinterpret_cast<AtomicType *>(instance.lookup(instance._create(name)));
    }

    static Metrics::Gauge::SpanType
    createSpan(size_t size, IdType *id = nullptr)
    {
      auto &instance = Metrics::instance();

      return instance._createSpan(size, id);
    }

    static void
    increment(AtomicType *metric, uint64_t val = 1)
    {
      ink_assert(metric);
      metric->_value.fetch_add(val, MEMORY_ORDER);
    }

    static void
    decrement(AtomicType *metric, uint64_t val = 1)
    {
      ink_assert(metric);
      metric->_value.fetch_sub(val, MEMORY_ORDER);
    }

    static int64_t
    load(AtomicType *metric)
    {
      ink_assert(metric);
      return metric->_value.load();
    }

    static void
    store(AtomicType *metric, int64_t val)
    {
      ink_assert(metric);
      return metric->_value.store(val);
    }

  }; // class Gauge

  class Counter
  {
  public:
    using self_type = Counter;
    using SpanType  = Metrics::SpanType;

    class AtomicType : public Metrics::AtomicType
    {
    };

    static Metrics::IdType
    create(const std::string_view name)
    {
      auto &instance = Metrics::instance();

      return instance._create(name);
    }

    static AtomicType *
    createPtr(const std::string_view name)
    {
      auto &instance = Metrics::instance();

      return reinterpret_cast<AtomicType *>(instance.lookup(instance._create(name)));
    }

    static Metrics::Counter::SpanType
    createSpan(size_t size, IdType *id = nullptr)
    {
      auto &instance = Metrics::instance();

      return instance._createSpan(size, id);
    }

    static void
    increment(AtomicType *metric, uint64_t val = 1)
    {
      ink_assert(metric);
      metric->_value.fetch_add(val, MEMORY_ORDER);
    }

    static void
    decrement(AtomicType *metric, uint64_t val = 1)
    {
      ink_assert(metric);
      metric->_value.fetch_sub(val, MEMORY_ORDER);
    }

    static int64_t
    load(AtomicType *metric)
    {
      ink_assert(metric);
      return metric->_value.load();
    }

  }; // class Counter

}; // class Metrics

} // namespace ts
