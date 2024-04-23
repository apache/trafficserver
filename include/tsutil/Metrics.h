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
#include <unordered_map>
#include <tuple>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include "swoc/MemSpan.h"

#include "tsutil/Assert.h"

namespace ts
{
class Metrics
{
private:
  using self_type = Metrics;

public:
  class AtomicType
  {
    friend class Metrics;

  public:
    AtomicType() = default;

    int64_t
    load() const
    {
      return _value.load();
    }

    void
    increment(int64_t val)
    {
      _value.fetch_add(val, MEMORY_ORDER);
    }

    // Use with care ...
    void
    store(int64_t val)
    {
      _value.store(val);
    }

    void
    decrement(int64_t val)
    {
      _value.fetch_sub(val, MEMORY_ORDER);
    }

  protected:
    std::atomic<int64_t> _value{0};
  };

  using IdType   = int32_t; // Could be a tuple, but one way or another, they have to be combined to an int32_t.
  using SpanType = swoc::MemSpan<AtomicType>;

  static constexpr uint16_t MAX_BLOBS    = 8192;
  static constexpr uint16_t MAX_SIZE     = 1024;                               // For a total of 8M metrics
  static constexpr IdType   NOT_FOUND    = std::numeric_limits<IdType>::min(); // <16-bit,16-bit> = <blob-index,offset>
  static const auto         MEMORY_ORDER = std::memory_order_relaxed;

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
  Metrics   &operator=(Metrics &&)        = delete;
  Metrics(Metrics &&)                     = delete;

  virtual ~Metrics() {}

  // The singleton instance, owned by the Metrics class
  static Metrics &instance();

  // Yes, we don't return objects here, but rather ID's and atomic's directly. Treat
  // the std::atomic<int64_t> as the underlying class for a single metric, and be happy.
  IdType
  lookup(const std::string_view name) const
  {
    return _storage->lookup(name);
  }
  AtomicType *
  lookup(const std::string_view name, IdType *out_id) const
  {
    return _storage->lookup(name, out_id);
  }
  AtomicType *
  lookup(IdType id, std::string_view *out_name = nullptr) const
  {
    return _storage->lookup(id, out_name);
  }
  bool
  rename(IdType id, const std::string_view name)
  {
    return _storage->rename(id, name);
  }

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

  std::string_view
  name(IdType id) const
  {
    return _storage->name(id);
  }

  bool
  valid(IdType id) const
  {
    return _storage->valid(id);
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
      auto             metric = _metrics.lookup(_it, &name);

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

    const Metrics  &_metrics;
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
    auto [blob, offset] = _storage->current();

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
  IdType
  _create(const std::string_view name)
  {
    return _storage->create(name);
  }

  SpanType
  _createSpan(size_t size, IdType *id = nullptr)
  {
    return _storage->createSpan(size, id);
  }

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

  class Storage
  {
    BlobStorage        _blobs;
    uint16_t           _cur_blob = 0;
    uint16_t           _cur_off  = 0;
    LookupTable        _lookups;
    mutable std::mutex _mutex;

  public:
    Storage(const Storage &)            = delete;
    Storage &operator=(const Storage &) = delete;

    Storage()
    {
      _blobs[0] = new NamesAndAtomics();
      release_assert(_blobs[0]);
      release_assert(0 == create("proxy.process.api.metrics.bad_id")); // Reserve slot 0 for errors, this should always be 0
    }

    ~Storage()
    {
      for (size_t i = 0; i <= _cur_blob; ++i) {
        delete _blobs[i];
      }
    }

    IdType           create(const std::string_view name);
    void             addBlob();
    IdType           lookup(const std::string_view name) const;
    AtomicType      *lookup(const std::string_view name, IdType *out_id) const;
    AtomicType      *lookup(Metrics::IdType id, std::string_view *out_name = nullptr) const;
    std::string_view name(IdType id) const;
    SpanType         createSpan(size_t size, IdType *id = nullptr);
    bool             rename(IdType id, const std::string_view name);

    std::pair<int16_t, int16_t>
    current() const
    {
      std::lock_guard lock(_mutex);
      return {_cur_blob, _cur_off};
    }

    bool
    valid(IdType id) const
    {
      auto [blob, entry] = _splitID(id);

      return (id >= 0 && ((blob < _cur_blob && entry < MAX_SIZE) || (blob == _cur_blob && entry <= _cur_off)));
    }
  };

  Metrics(std::shared_ptr<Storage> &str) : _storage(str) {}

  std::shared_ptr<Storage> _storage;

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

    static IdType
    lookup(const std::string_view name)
    {
      auto &instance = Metrics::instance();

      return instance.lookup(name);
    }

    static AtomicType *
    lookup(const IdType id, std::string_view *out_name = nullptr)
    {
      auto &instance = Metrics::instance();

      return reinterpret_cast<AtomicType *>(instance.lookup(id, out_name));
    }

    static AtomicType *
    lookup(const std::string_view name, IdType *id)
    {
      auto &instance = Metrics::instance();

      return reinterpret_cast<AtomicType *>(instance.lookup(name, id));
    }

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
      debug_assert(metric);
      metric->_value.fetch_add(val, MEMORY_ORDER);
    }

    static void
    decrement(AtomicType *metric, uint64_t val = 1)
    {
      debug_assert(metric);
      metric->_value.fetch_sub(val, MEMORY_ORDER);
    }

    static int64_t
    load(const AtomicType *metric)
    {
      debug_assert(metric);
      return metric->_value.load();
    }

    static void
    store(AtomicType *metric, int64_t val)
    {
      debug_assert(metric);
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

    static IdType
    lookup(const std::string_view name)
    {
      auto &instance = Metrics::instance();

      return instance.lookup(name);
    }

    static AtomicType *
    lookup(const IdType id, std::string_view *out_name = nullptr)
    {
      auto &instance = Metrics::instance();

      return reinterpret_cast<AtomicType *>(instance.lookup(id, out_name));
    }

    static AtomicType *
    lookup(const std::string_view name, IdType *id)
    {
      auto &instance = Metrics::instance();

      return reinterpret_cast<AtomicType *>(instance.lookup(name, id));
    }

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
      debug_assert(metric);
      metric->_value.fetch_add(val, MEMORY_ORDER);
    }

    static int64_t
    load(const AtomicType *metric)
    {
      debug_assert(metric);
      return metric->_value.load();
    }

  }; // class Counter

}; // class Metrics

} // namespace ts
