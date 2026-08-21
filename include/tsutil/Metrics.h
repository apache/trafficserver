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
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <optional>

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

  enum class MetricType : int { COUNTER = 0, GAUGE };

  using IdType   = int32_t; // Could be a tuple, but one way or another, they have to be combined to an int32_t.
  using SpanType = swoc::MemSpan<AtomicType>;

  static constexpr uint16_t MAX_BLOBS        = 8192;
  static constexpr uint16_t MAX_SIZE         = 1024;                               // For a total of 8M metrics
  static constexpr IdType   NOT_FOUND        = std::numeric_limits<IdType>::min(); // <16-bit,16-bit> = <blob-index,offset>
  static const auto         MEMORY_ORDER     = std::memory_order_relaxed;
  static constexpr int      METRIC_TYPE_BITS = 29;
  static constexpr int      METRIC_TYPE_MASK = 0x1FFF;

private:
  using NameAndId       = std::tuple<std::string, IdType>;
  using LookupTable     = std::unordered_map<std::string_view, IdType>;
  using NameStorage     = std::array<NameAndId, MAX_SIZE>;
  using AtomicStorage   = std::array<AtomicType, MAX_SIZE>;
  using NamesAndAtomics = std::tuple<NameStorage, AtomicStorage>;
  using BlobStorage     = std::array<std::unique_ptr<NamesAndAtomics>, MAX_BLOBS>;

public:
  Metrics(const self_type &)              = delete;
  self_type &operator=(const self_type &) = delete;
  Metrics   &operator=(Metrics &&)        = delete;
  Metrics(Metrics &&)                     = delete;

  virtual ~Metrics() {}

  // The singleton instance, owned by the Metrics class
  static Metrics &instance();

  /** The hidden metrics instance.
   *
   * A completely separate storage from @c instance(). Metrics here are stored but never
   * published - they are structurally unreachable from the published store, so no consumer
   * (traffic_ctl, JSONRPC, stats_over_http) can expose them by omission.
   *
   * Intended for high cardinality intermediate values which feed @c Derived aggregates.
   *
   * @note An @c IdType from this instance is NOT interchangeable with one from @c instance().
   *   Ids are meaningful only relative to their store: passing a hidden id to the published
   *   store yields a silently wrong metric, with no error and no crash, since @c valid() will
   *   accept it. Prefer @c Gauge::createHiddenPtr / @c Counter::createHiddenPtr, which return
   *   correctly typed pointers and never hand out an id.
   */
  static Metrics &hidden_instance();

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
  lookup(IdType id, std::string_view *out_name = nullptr, Metrics::MetricType *type = nullptr) const
  {
    return _storage->lookup(id, out_name, type);
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

  MetricType
  type(IdType id) const
  {
    return _storage->type(id);
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
    using value_type        = std::tuple<std::string_view, MetricType, int64_t>;
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
      MetricType       type;
      auto             metric = _metrics.lookup(_it, &name, &type);

      return std::make_tuple(name, type, metric->_value.load());
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

    return iterator(*this, _makeId(blob, offset, MetricType::COUNTER));
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
  _create(const std::string_view name, MetricType type)
  {
    return _storage->create(name, type);
  }

  SpanType
  _createSpan(size_t size, MetricType type, IdType *id = nullptr)
  {
    return _storage->createSpan(size, type, id);
  }

  // These are little helpers around managing the ID's
  static constexpr std::tuple<uint16_t, uint16_t>
  _splitID(IdType value)
  {
    return std::make_tuple(static_cast<uint16_t>(value >> 16) & METRIC_TYPE_MASK, static_cast<uint16_t>(value & 0xFFFF));
  }

  static constexpr MetricType
  _extractType(IdType value)
  {
    return MetricType{static_cast<int>((static_cast<uint32_t>(value) >> METRIC_TYPE_BITS) & 0x1)};
  }

  static constexpr IdType
  _makeId(uint16_t blob, uint16_t offset, const MetricType type)
  {
    int t = static_cast<int>(type);
    return (t << METRIC_TYPE_BITS | blob << 16 | offset);
  }

  class Storage
  {
    /* _cur_blob and _cur_off are the two publication points. Each is written last, with a release
     * store, after whatever it makes visible: the blob pointer and the reset of _cur_off for
     * _cur_blob, the slot's name for _cur_off. A reader loads them with acquire, _cur_blob first --
     * see _is_allocated(). That is what makes the pair consistent without reading it as one word,
     * and it is why _blobs itself needs no atomic: it is only ever read at an index no greater than
     * _cur_blob, and that write is sequenced before the release store the reader acquired.
     *
     * Writers all hold _mutex, so they load these relaxed; there is no other writer to race with.
     */
    BlobStorage           _blobs;
    std::atomic<uint16_t> _cur_blob{0};
    std::atomic<uint16_t> _cur_off{0};
    LookupTable           _lookups;
    mutable std::mutex    _mutex;

  public:
    Storage(const Storage &)            = delete;
    Storage &operator=(const Storage &) = delete;

    Storage()
    {
      _blobs[0] = std::make_unique<NamesAndAtomics>();
      release_assert(_blobs[0]);
      // Reserve slot 0 for errors, this should always be 0
      release_assert(0 == create("proxy.process.api.metrics.bad_id", MetricType::COUNTER));
    }

    ~Storage() {}

    IdType           create(const std::string_view name, const MetricType type = MetricType::COUNTER);
    void             addBlob();
    IdType           lookup(const std::string_view name) const;
    AtomicType      *lookup(const std::string_view name, IdType *out_id, MetricType *out_type = nullptr) const;
    AtomicType      *lookup(Metrics::IdType id, std::string_view *out_name = nullptr, MetricType *out_type = nullptr) const;
    std::string_view name(IdType id) const;
    MetricType       type(IdType id) const;
    SpanType         createSpan(size_t size, const MetricType type = MetricType::COUNTER, IdType *id = nullptr);
    bool             rename(IdType id, const std::string_view name);

    std::pair<int16_t, int16_t>
    current() const
    {
      std::lock_guard lock(_mutex);
      return {_cur_blob.load(std::memory_order_relaxed), _cur_off.load(std::memory_order_relaxed)};
    }

    /** Whether @a id names a slot that has actually been allocated.
     *
     * The single gate for every id based accessor. Ids arriving through the @c TSStat* API are
     * plugin supplied and untrusted, so three things have to hold: the id is not negative, the
     * offset is one @c _makeId could have produced -- the offset field is 16 bits wide but a real
     * offset is always below @c MAX_SIZE, so a larger one is malformed rather than merely stale --
     * and the slot has been handed out. Blobs are filled in increasing order and never freed, so
     * that last part means either an earlier blob, or below the allocation point in the current one.
     */
    bool
    _is_allocated(IdType id) const
    {
      if (id < 0) {
        return false;
      }

      auto [blob_ix, offset] = _splitID(id);

      // Load the outer publication point first: seeing a value for _cur_blob means everything
      // addBlob() wrote before releasing it -- the blob pointer, and the reset of _cur_off -- is
      // visible here too. Reading _cur_off first would defeat that.
      auto const cur_blob = _cur_blob.load(std::memory_order_acquire);
      auto const cur_off  = _cur_off.load(std::memory_order_acquire);

      // The blob comparison is against <= / <, not a test for "not the current blob", because
      // addBlob() stores a new blob before advancing _cur_blob: until it does, _blobs[cur_blob + 1]
      // is non-null while still holding nothing.
      return offset < MAX_SIZE && blob_ix <= cur_blob && _blobs[blob_ix] != nullptr && (blob_ix < cur_blob || offset < cur_off);
    }

    bool
    valid(IdType id) const
    {
      return _is_allocated(id);
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

      return instance._create(name, MetricType::GAUGE);
    }

    static AtomicType *
    createPtr(const std::string_view name)
    {
      auto &instance = Metrics::instance();

      return reinterpret_cast<AtomicType *>(instance.lookup(instance._create(name, MetricType::GAUGE)));
    }

    static AtomicType *
    createPtr(const std::string_view prefix, const std::string_view name)
    {
      auto       &instance = Metrics::instance();
      std::string tmpname  = std::string(prefix) + std::string(name);

      return reinterpret_cast<AtomicType *>(instance.lookup(instance._create(tmpname, MetricType::GAUGE)));
    }

    /** Create a metric which is stored but never published.
     *
     * @see Metrics::hidden_instance()
     */
    static AtomicType *
    createHiddenPtr(const std::string_view name)
    {
      auto &instance = Metrics::hidden_instance();

      return reinterpret_cast<AtomicType *>(instance.lookup(instance._create(name, MetricType::GAUGE)));
    }

    static AtomicType *
    createHiddenPtr(const std::string_view prefix, const std::string_view name)
    {
      auto       &instance = Metrics::hidden_instance();
      std::string tmpname  = std::string(prefix) + std::string(name);

      return reinterpret_cast<AtomicType *>(instance.lookup(instance._create(tmpname, MetricType::GAUGE)));
    }

    static Metrics::Gauge::SpanType
    createSpan(size_t size, IdType *id = nullptr)
    {
      auto &instance = Metrics::instance();

      return instance._createSpan(size, MetricType::GAUGE, id);
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

      return instance._create(name, MetricType::COUNTER);
    }

    static AtomicType *
    createPtr(const std::string_view name)
    {
      auto &instance = Metrics::instance();

      return reinterpret_cast<AtomicType *>(instance.lookup(instance._create(name, MetricType::COUNTER)));
    }

    static AtomicType *
    createPtr(const std::string_view prefix, const std::string_view name)
    {
      auto       &instance = Metrics::instance();
      std::string tmpname  = std::string(prefix) + std::string(name);

      return reinterpret_cast<AtomicType *>(instance.lookup(instance._create(tmpname, MetricType::COUNTER)));
    }

    /** Create a metric which is stored but never published.
     *
     * @see Metrics::hidden_instance()
     */
    static AtomicType *
    createHiddenPtr(const std::string_view name)
    {
      auto &instance = Metrics::hidden_instance();

      return reinterpret_cast<AtomicType *>(instance.lookup(instance._create(name, MetricType::COUNTER)));
    }

    static AtomicType *
    createHiddenPtr(const std::string_view prefix, const std::string_view name)
    {
      auto       &instance = Metrics::hidden_instance();
      std::string tmpname  = std::string(prefix) + std::string(name);

      return reinterpret_cast<AtomicType *>(instance.lookup(instance._create(tmpname, MetricType::COUNTER)));
    }

    static Metrics::Counter::SpanType
    createSpan(size_t size, IdType *id = nullptr)
    {
      auto &instance = Metrics::instance();

      return instance._createSpan(size, MetricType::COUNTER, id);
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

  /**
   * Static string metrics storage.
   *
   * All methods are thread-safe.
   */
  class StaticString
  {
  public:
    using StringStorage = std::unordered_map<std::string, std::string>;

    static void
    createString(const std::string &name, const std::string_view value)
    {
      auto &instance = Metrics::StaticString::instance();
      return instance._createString(name, value);
    }

    static StaticString &instance();

    /**
     * Thread-safe iteration over all string metrics.
     * The callback is invoked for each metric while holding the mutex.
     */
    template <typename Func>
    void
    for_each(Func &&func) const
    {
      std::lock_guard lock(_mutex);
      for (const auto &[name, value] : _strings) {
        func(name, value);
      }
    }

    std::optional<std::string_view> lookup(const std::string &name) const;

  private:
    void _createString(const std::string &name, const std::string_view value);

    StringStorage      _strings;
    mutable std::mutex _mutex;
  };

  /**
   * Derive metrics by summing a set of other metrics.
   *
   */
  class Derived
  {
  public:
    /// How the sources of a derived metric are combined into its value.
    enum class Op { SUM, MAX, MIN };

    struct DerivedMetricSpec {
      using MetricSpec = std::variant<Metrics::AtomicType *, Metrics::IdType, std::string_view>;
      std::string_view                  derived_name;
      Metrics::MetricType               derived_type;
      std::initializer_list<MetricSpec> derived_from;
      Op                                op{Op::SUM};
    };

    /**
     * Create new metrics derived from existing metrics.
     *
     * This function will create new metrics from a list of existing metrics.  The existing metric can
     * be specified by name, id or a pointer to the metric.
     */
    static void derive(const std::initializer_list<DerivedMetricSpec> &metrics);

    /** Add a source to a derived metric, creating the derived metric if needed.
     *
     * Unlike @c derive this may be called at any time, so aggregates can be built up as their
     * sources are discovered at runtime.
     *
     * @param derived_name Name of the derived metric, in the published store.
     * @param type Type of the derived metric. Ignored if the derived metric already exists.
     * @param source The source metric. May come from either the published or the hidden store.
     * @param op How to combine the sources. Ignored if the derived metric already exists.
     *
     * Adding a source which is already registered for @a derived_name is a no-op, so callers
     * which may re-register (e.g. an object recreated for the same key) need not track this.
     */
    static void add_source(std::string_view derived_name, Metrics::MetricType type, Metrics::AtomicType *source, Op op = Op::SUM);

    /**
     * Update derived metrics.
     *
     * This static function should be called periodically to update derived metrics.
     */
    static void update_derived();
  };

}; // class Metrics

} // namespace ts
