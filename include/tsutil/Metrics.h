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
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <optional>

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

  using IdType = int32_t; // Could be a tuple, but one way or another, they have to be combined to an int32_t.

  static constexpr uint16_t MAX_BLOBS        = 8192;
  static constexpr uint16_t MAX_SIZE         = 1024;                               // For a total of 8M metrics
  static constexpr IdType   NOT_FOUND        = std::numeric_limits<IdType>::min(); // <16-bit,16-bit> = <blob-index,offset>
  static const auto         MEMORY_ORDER     = std::memory_order_relaxed;
  static constexpr int      METRIC_TYPE_BITS = 29;
  static constexpr int      METRIC_TYPE_MASK = 0x1FFF;

private:
  using NameAndId     = std::tuple<std::string, IdType>;
  using LookupTable   = std::unordered_map<std::string_view, IdType>;
  using NameStorage   = std::array<NameAndId, MAX_SIZE>;
  using AtomicStorage = std::array<AtomicType, MAX_SIZE>;
  /// Per slot flag bits, see @c UNLISTED. A parallel array rather than a member of @c NameAndId
  /// because an atomic member would make that tuple neither copyable nor movable, and the slot is
  /// written there with a tuple assignment.
  using FlagStorage     = std::array<std::atomic<uint8_t>, MAX_SIZE>;
  using NamesAndAtomics = std::tuple<NameStorage, AtomicStorage, FlagStorage>;
  using BlobStorage     = std::array<std::unique_ptr<NamesAndAtomics>, MAX_BLOBS>;

  /// The slot exists and is still resolvable by name or id, but is skipped by iteration.
  static constexpr uint8_t UNLISTED = 0x01;

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

  /** Take @a id out of the store's listing.
   *
   * An unlisted metric keeps its slot, its name and its atomic. It is skipped by iteration, so it
   * vanishes from everything that enumerates the store, but it still resolves through @c lookup and
   * its value may still be read and written -- an unlisted number that still rings. Creating the
   * same name again relists it and returns the same id.
   *
   * @return @c false if @a id does not name an allocated slot.
   */
  bool
  unlist(IdType id)
  {
    return _storage->set_listed(id, false);
  }

  /// Put @a id back in the listing. @see unlist
  bool
  relist(IdType id)
  {
    return _storage->set_listed(id, true);
  }

  /** Whether @a id is enumerated.
   *
   * @return @c false for an unlisted metric, and also for an id that names no allocated slot --
   *   neither appears in iteration.
   */
  bool
  listed(IdType id) const
  {
    return _storage->listed(id);
  }

  /// Convenience for callers that publish by name and do not retain the id. @see unlist
  bool
  unlist(std::string_view name)
  {
    auto id = lookup(name);

    return id != NOT_FOUND && unlist(id);
  }

  /// Convenience for callers that publish by name and do not retain the id. @see relist
  bool
  relist(std::string_view name)
  {
    auto id = lookup(name);

    return id != NOT_FOUND && relist(id);
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
    friend class Metrics;

    /// Tag for the end sentinel, which has no position and reads no storage.
    struct end_tag {
    };

    // Only Metrics hands these out, through begin(), end() and find(). A caller that could name an
    // arbitrary position could name an unlisted one, which iteration must never visit.
    explicit iterator(const Metrics &m);
    iterator(const Metrics &m, IdType pos);
    iterator(const Metrics &m, end_tag);

  public:
    using iterator_category = std::input_iterator_tag;
    using value_type        = std::tuple<std::string_view, MetricType, int64_t>;
    using difference_type   = ptrdiff_t;
    using pointer           = value_type *;
    using reference         = value_type &;

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

    /** Equality.
     *
     * Three way rather than a plain position compare: any exhausted iterator equals the end
     * sentinel, and equals any other exhausted iterator, since two of them may have skipped a
     * different number of unlisted slots. Two live iterators still compare by position.
     *
     * Two positional iterators may hold different snapshots, so exhaustion between them is judged
     * against the earlier bound. Otherwise a walk could pass its own bound while a stop iterator
     * made later was still live: they would never compare equal and @c operator++ could not make
     * progress. The sentinel keeps its own answer, since its bound is meaningless.
     *
     * @note Only iterators taken from the same snapshot are meaningfully comparable with each
     *   other. Because exhaustion is a property of an iterator's own bound, two taken at different
     *   times can compare equal to each other while disagreeing about @c end, so this is not a
     *   total equivalence relation and these are not iterators to hand to a generic algorithm.
     *   Use @c end to test for exhaustion.
     */
    bool
    operator==(const iterator &o) const
    {
      if (std::addressof(_metrics) != std::addressof(o._metrics)) {
        return false;
      }

      if (_end || o._end) {
        return at_end() == o.at_end();
      }

      auto const bound = _bound < o._bound ? _bound : o._bound;
      bool const a = _it >= bound, b = o._it >= bound;

      if (a || b) {
        return a && b;
      }
      return _it == o._it;
    }

  private:
    void next();
    void advance();
    void skip_unlisted();

    bool
    at_end() const
    {
      return _end || _it >= _bound;
    }

    const Metrics  &_metrics;
    Metrics::IdType _it{0};
    /// One past the last slot allocated when this iterator was made. Iteration is a snapshot.
    Metrics::IdType _bound{0};
    bool            _end{false};
  };

  iterator
  begin() const
  {
    return iterator(*this);
  }

  iterator
  end() const
  {
    return iterator(*this, iterator::end_tag{});
  }

  iterator
  find(const std::string_view name) const
  {
    auto id = lookup(name);

    // An unlisted slot is never visited by iteration, so handing out an iterator to one would
    // produce a bound that a skipping walk steps straight over. Reach it with lookup() instead.
    if (id == NOT_FOUND || !listed(id)) {
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

  /// As @c _makeId, without the type bits.
  static constexpr uint32_t
  _pack(uint16_t blob, uint16_t offset)
  {
    return static_cast<uint32_t>(blob) << 16 | offset;
  }

  // A packed position must not reach the type bits, and an offset must fit its field.
  static_assert(MAX_SIZE <= 0x10000);
  static_assert(MAX_BLOBS <= (1 << (METRIC_TYPE_BITS - 16)));

  class Storage
  {
    /* The next free slot, packed as @c _makeId packs one. A single value because a reader that
     * caught a new offset against an old blob index, or the reverse, would reject ids that exist
     * or accept ids that do not. Release stored last, after the blob pointer or the slot's name it
     * publishes. Only ever increases, so an id is allocated exactly when it packs below it.
     *
     * A slot's name is written once, before the store that publishes it, and never changes, which
     * is what lets @c name and @c lookup hand out a view of it without the mutex.
     */
    BlobStorage           _blobs;
    std::atomic<uint32_t> _next_free{0};
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
    bool             set_listed(IdType id, bool listed);
    bool             listed(IdType id) const;

    /// The id the next slot will get, which is also iteration's exclusive bound.
    IdType
    next_free_id() const
    {
      return static_cast<IdType>(_next_free.load(std::memory_order_acquire));
    }

    bool
    valid(IdType id) const
    {
      return _is_allocated(id);
    }

  private:
    /** Whether @a id names an allocated slot.
     *
     * The gate for every id based accessor, since ids from the @c TSStat* API are untrusted. An id
     * qualifies when it is non-negative, its offset is one @c _makeId could produce, and its slot
     * has been handed out.
     */
    bool
    _is_allocated(IdType id) const
    {
      if (id < 0) {
        return false;
      }

      auto [blob_ix, offset] = _splitID(id);

      // Not implied below: an earlier blob can name an offset past MAX_SIZE and still pack under.
      if (offset >= MAX_SIZE) {
        return false;
      }

      // Acquiring the bound acquires the blob install, so _blobs needs no check of its own.
      return _pack(blob_ix, offset) < _next_free.load(std::memory_order_acquire);
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
