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
namespace Metrics
{
  class Counter
  {
  private:
    using self_type = Counter;

  public:
    using AtomicType = std::atomic<int64_t>;
    using IdType     = int32_t; // Could be a tuple, but one way or another, they have to be combined to an int32_t.
    using SpanType   = swoc::MemSpan<AtomicType>;

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
    Counter(const self_type &)              = delete;
    self_type &operator=(const self_type &) = delete;
    Counter &operator=(Counter &&)          = delete;
    Counter(Counter &&)                     = delete;

    virtual ~Counter()
    {
      for (size_t i = 0; i <= _cur_blob; ++i) {
        delete _blobs[i];
      }
    }

    Counter()
    {
      _blobs[0] = new NamesAndAtomics();
      ink_release_assert(_blobs[0]);
      ink_release_assert(0 == create("proxy.process.api.metrics.bad_id")); // Reserve slot 0 for errors, this should always be 0
    }

    // Yes, we don't return objects here, but rather ID's and atomic's directly. Treat
    // the std::atomic<int64_t> as the underlying class for a single metric, and be happy.
    IdType create(const std::string_view name);
    SpanType createSpan(size_t size, IdType *id = nullptr);
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

    // A bit of a convenience, since we use the ptr to the atomic frequently in the core
    AtomicType *
    createPtr(const std::string_view name)
    {
      return lookup(create(name));
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

      return (metric ? metric->fetch_add(val, MEMORY_ORDER) : NOT_FOUND);
    }

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

      return (id >= 0 && ((blob < _cur_blob && entry < MAX_SIZE) || (blob == _cur_blob && entry <= _cur_off)));
    }

    // Static methods to encapsulate access to the atomic's
    static Counter &getInstance();

    static IdType
    Create(const std::string_view name)
    {
      auto &instance = getInstance();

      return instance.create(name);
    }

    static AtomicType *
    CreatePtr(const std::string_view name)
    {
      auto &instance = getInstance();

      return instance.lookup(instance.create(name));
    }

    static void
    increment(AtomicType *metric, uint64_t val = 1)
    {
      ink_assert(metric);
      metric->fetch_add(val, MEMORY_ORDER);
    }

    static void
    decrement(AtomicType *metric, uint64_t val = 1)
    {
      ink_assert(metric);
      metric->fetch_sub(val, MEMORY_ORDER);
    }

    static int64_t
    read(AtomicType *metric)
    {
      ink_assert(metric);
      return metric->load();
    }

    static void
    write(AtomicType *metric, int64_t val)
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

      iterator(const Counter &m, IdType pos) : _counter(m), _it(pos) {}

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
        auto metric = _counter.lookup(_it, &name);

        return std::make_tuple(name, metric->load());
      }

      bool
      operator==(const iterator &o) const
      {
        return _it == o._it && std::addressof(_counter) == std::addressof(o._counter);
      }

      bool
      operator!=(const iterator &o) const
      {
        return _it != o._it || std::addressof(_counter) != std::addressof(o._counter);
      }

    private:
      void next();

      const Counter &_counter;
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
    BlobStorage _blobs;
    uint16_t _cur_blob = 0;
    uint16_t _cur_off  = 0;

  }; // class Counter

} // namespace Metrics

} // namespace ts
