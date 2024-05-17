/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <deque>
#include <tuple>
#include <atomic>
#include <chrono>
#include <string>
#include <climits>
#include <mutex>
#include <thread>

#include "tscore/ink_config.h"
#include "ts/ts.h"
#include <yaml-cpp/yaml.h>
#include "utilities.h"

constexpr auto BUCKET_REFILL_INTERVAL = std::chrono::milliseconds{25};  // Increase rate limit buckets every 25ms
constexpr auto QUEUE_DELAY_TIME       = std::chrono::milliseconds{300}; // Examine the queue every 300ms
using QueueTime                       = std::chrono::time_point<std::chrono::system_clock>;

// No metric for requests we accept; accepted requests should be counted under their usual metrics
enum {
  RATE_LIMITER_METRIC_QUEUED = 0,
  RATE_LIMITER_METRIC_REJECTED,
  RATE_LIMITER_METRIC_EXPIRED,
  RATE_LIMITER_METRIC_RESUMED,

  RATE_LIMITER_METRIC_MAX
};

int bucket_refill_cont(TSCont cont, TSEvent event, void *edata);
class BucketManager
{
  using self_type = BucketManager;

public:
  class RateBucket
  {
    using self_type = RateBucket;

  public:
    RateBucket(uint32_t max) : _count(0), _max(max) {}
    ~RateBucket() = default;

    RateBucket(self_type &&)                = delete;
    self_type &operator=(const self_type &) = delete;
    self_type &operator=(self_type &&)      = delete;

    uint32_t
    count() const
    {
      return _count.load(std::memory_order_acquire);
    }

    bool
    consume()
    {
      uint32_t val = _count.load(std::memory_order_acquire);

      while (val > 0) {
        if (_count.compare_exchange_weak(val, val - 1, std::memory_order_release, std::memory_order_acquire)) {
          break;
        }
      }
      TSReleaseAssert(val <= _max);

      return val > 0;
    }

    // This should only be called from the manager, as such no locking is needed
  private:
    friend class BucketManager;

    void
    refill()
    {
      static const uint32_t amount = _max / (1000 / BUCKET_REFILL_INTERVAL.count());
      uint32_t              old    = _count.load(std::memory_order_acquire);
      uint32_t              nval;

      do {
        nval = old + amount;
      } while (!_count.compare_exchange_weak(old, std::min(nval, _max), std::memory_order_release, std::memory_order_acquire));
    }

    std::atomic<uint32_t> _count;
    uint32_t              _max;

  }; // End class RateBucket

  BucketManager() = default;
  ~BucketManager()
  {
    if (_running) {
      _running = false;
      _thread.join(); // Wait for the thread to finish
    }
  }

  BucketManager(self_type &)              = delete;
  BucketManager(self_type &&)             = delete;
  self_type &operator=(const self_type &) = delete;
  self_type &operator=(self_type &&)      = delete;

  static BucketManager &
  getInstance()
  {
    static self_type instance;
    return instance;
  }

  void refill_thread();

  std::shared_ptr<RateBucket>
  add(uint32_t max)
  {
    auto                        bucket = std::make_shared<RateBucket>(max);
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_running) {
      _running = true;
      _thread  = std::thread(&BucketManager::refill_thread, this);
    }

    _buckets.push_back(bucket);

    return bucket;
  }

  void
  remove(std::shared_ptr<RateBucket> bucket)
  {
    std::lock_guard<std::mutex> lock(_mutex);
    auto                        it = std::find(_buckets.begin(), _buckets.end(), bucket);

    if (it != _buckets.end()) {
      _buckets.erase(it);
    }
  }

private:
  std::vector<std::shared_ptr<RateBucket>> _buckets;
  std::mutex                               _mutex;           // Protect the bucket list
  bool                                     _running = false; // Is the Bucket manager thread running already ?
  std::thread                              _thread;          // The thread refilling the buckets

}; // End class BucketManager

enum { RATE_LIMITER_TYPE_SNI = 0, RATE_LIMITER_TYPE_REMAP, RATE_LIMITER_TYPE_MAX };
enum class ReserveStatus { UNLIMITED = 0, RESERVED, FULL, HIGH_RATE };

static const char *RATE_LIMITER_METRIC_PREFIX = "plugin.rate_limiter";

void metric_helper(std::array<int, RATE_LIMITER_METRIC_MAX> &metrics, uint type, const std::string &tag, const std::string &name,
                   const std::string &prefix = RATE_LIMITER_METRIC_PREFIX);

///////////////////////////////////////////////////////////////////////////////
// Base class for all limiters
//
template <class T> class RateLimiter
{
  using QueueItem = std::tuple<T, TSCont, QueueTime>;
  using self_type = RateLimiter;

public:
  RateLimiter()                           = default;
  RateLimiter(self_type &&)               = delete;
  self_type &operator=(const self_type &) = delete;
  self_type &operator=(self_type &&)      = delete;

  virtual ~RateLimiter() { BucketManager::getInstance().remove(_bucket); }

  void
  initializeMetrics(uint type, std::string tag, std::string prefix = RATE_LIMITER_METRIC_PREFIX)
  {
    TSReleaseAssert(type < RATE_LIMITER_TYPE_MAX);
    metric_helper(_metrics, type, tag, name(), prefix);
  }

  virtual bool
  parseYaml(const YAML::Node &node)
  {
    if (node["limit"]) {
      _limit = node["limit"].as<uint32_t>();
    }

    if (node["rate"]) {
      _limit = node["rate"].as<uint32_t>();
    }

    // ToDo: One or both of these should be required

    const YAML::Node &queue = node["queue"];

    // If enabled, we default to UINT32_MAX, but the object default is still 0 (no queue)
    if (queue) {
      _max_queue = queue["size"] ? queue["size"].as<uint32_t>() : UINT32_MAX;

      if (queue["max_age"]) {
        _max_age = std::chrono::seconds(queue["max_age"].as<uint32_t>());
      }
    }

    const YAML::Node &metrics = node["metrics"];

    if (metrics) {
      std::string prefix = metrics["prefix"] ? metrics["prefix"].as<std::string>() : RATE_LIMITER_METRIC_PREFIX;
      std::string tag    = metrics["tag"] ? metrics["tag"].as<std::string>() : name();

      Dbg(dbg_ctl, "Metrics for selector rule: %s(%s, %s)", name().c_str(), prefix.c_str(), tag.c_str());
      initializeMetrics(RATE_LIMITER_TYPE_SNI, prefix, tag);
    }

    return true;
  }

  // Add a rate bucket for this limiter
  void
  addBucket()
  {
    TSAssert(_rate > 0);
    _bucket = BucketManager::getInstance().add(_rate);
  }

  // Reserve / release a slot from the active resource limits.

  ReserveStatus
  reserve()
  {
    if (_rate > 0) {
      if (!_bucket->consume()) {
        Dbg(dbg_ctl, "Rate limit exceeded");
        return ReserveStatus::HIGH_RATE;
      } else {
        Dbg(dbg_ctl, "Rate limit OK, count() == %u", _bucket->count());
      }
    }

    if (!has_limit()) { // If we have no limits and not at rate
      return ReserveStatus::UNLIMITED;
    }

    std::lock_guard<std::mutex> lock(_active_lock);

    TSReleaseAssert(_active <= _limit);
    if (_active < _limit) {
      ++_active;
      Dbg(dbg_ctl, "Reserving a slot, active entities == %u", _active.load());

      return ReserveStatus::RESERVED;
    }

    return ReserveStatus::FULL;
  }

  void
  free()
  {
    {
      std::lock_guard<std::mutex> lock(_active_lock);
      --_active;
    }

    Dbg(dbg_ctl, "Releasing a slot, active entities == %u", _active.load());
  }

  // Current size of the active_in connections
  uint32_t
  active() const
  {
    return _active.load();
  }

  // Current size of the queue
  uint32_t
  size() const
  {
    return _size.load();
  }

  // Is the queue full (at it's max size)?
  bool
  full() const
  {
    return (_size >= max_queue());
  }

  void
  push(T elem, TSCont cont)
  {
    QueueTime                   now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(_queue_lock);

    _queue.push_front(std::make_tuple(elem, cont, now));
    ++_size;
  }

  QueueItem
  pop()
  {
    QueueItem                   item;
    std::lock_guard<std::mutex> lock(_queue_lock);

    if (!_queue.empty()) {
      item = std::move(_queue.back());
      _queue.pop_back();
      --_size;
    }

    return item;
  }

  void
  incrementMetric(uint metric)
  {
    if (_metrics[metric] != TS_ERROR) {
      TSStatIntIncrement(_metrics[metric], 1);
    }
  }

  bool
  hasOldEntity(QueueTime now)
  {
    std::lock_guard<std::mutex> lock(_queue_lock);

    if (!_queue.empty()) {
      QueueItem item = _queue.back();

      std::chrono::milliseconds age = std::chrono::duration_cast<std::chrono::milliseconds>(now - std::get<2>(item));

      return (age >= max_age());
    }

    return false;
  }

  const std::string &
  name() const
  {
    return _name;
  }

  uint32_t
  limit() const
  {
    return _limit;
  }

  bool
  has_limit() const
  {
    return _limit != UINT32_MAX && _limit != 0;
  }

  uint32_t
  rate() const
  {
    return _rate;
  }

  uint32_t
  max_queue() const
  {
    return _max_queue;
  }

  std::chrono::milliseconds
  max_age() const
  {
    return _max_age;
  }

  void
  setName(const std::string &name)
  {
    _name = name;
  }

protected:
  std::string               _name      = "_limiter_";                       // The name/descr (e.g. SNI name) of this limiter
  uint32_t                  _limit     = UINT32_MAX;                        // No limit unless specified ...
  uint32_t                  _rate      = 0;                                 // Rate limit (if any)
  uint32_t                  _max_queue = 0;                                 // No queue by default
  std::chrono::milliseconds _max_age   = std::chrono::milliseconds::zero(); // Max age (ms) in the queue

private:
  std::atomic<uint32_t> _active = 0; // Current active number of txns. This has to always stay <= limit above
  std::atomic<uint32_t> _size   = 0; // Current size of the pending queue of txns. This should aim to be < _max_queue

  std::mutex            _queue_lock, _active_lock; // Resource locks
  std::deque<QueueItem> _queue;                    // Queue for the pending TXN's. ToDo: Should also move (see below)

  std::array<int, RATE_LIMITER_METRIC_MAX>   _metrics{};
  std::shared_ptr<BucketManager::RateBucket> _bucket; // The rate bucket (optional)
};
