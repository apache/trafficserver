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

#include "tscore/ink_config.h"
#include "ts/ts.h"
#include <yaml-cpp/yaml.h>
#include "utilities.h"

constexpr auto QUEUE_DELAY_TIME = std::chrono::milliseconds{300}; // Examine the queue every 300ms
using QueueTime                 = std::chrono::time_point<std::chrono::system_clock>;

enum { RATE_LIMITER_TYPE_SNI = 0, RATE_LIMITER_TYPE_REMAP, RATE_LIMITER_TYPE_MAX };

// order must align with the above
static const char *types[] = {
  "sni",
  "remap",
};

// no metric for requests we accept; accepted requests should be counted under their usual metrics
enum {
  RATE_LIMITER_METRIC_QUEUED = 0,
  RATE_LIMITER_METRIC_REJECTED,
  RATE_LIMITER_METRIC_EXPIRED,
  RATE_LIMITER_METRIC_RESUMED,

  RATE_LIMITER_METRIC_MAX
};

// order must align with the above
static const char *suffixes[] = {
  "queued",
  "rejected",
  "expired",
  "resumed",
};

static const char *RATE_LIMITER_METRIC_PREFIX = "plugin.rate_limiter";

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

  virtual ~RateLimiter() = default;

  virtual bool
  parseYaml(const YAML::Node &node)
  {
    if (node["limit"]) {
      _limit = node["limit"].as<uint32_t>();
    } else {
      // ToDo: Should we require the limit ?
    }

    const YAML::Node &queue = node["queue"];

    // If enabled, we default to UINT32_MAX, but the object default is still 0 (no queue)
    if (queue) {
      _max_queue = queue["size"] ? queue["size"].as<uint32_t>() : UINT32_MAX;

      if (queue["max_age"]) {
        _max_age = std::chrono::seconds(queue["max_age"].as<uint32_t>());
      }

      const YAML::Node &metrics = node["metrics"];

      if (metrics) {
        std::string prefix = metrics["prefix"] ? metrics["prefix"].as<std::string>() : RATE_LIMITER_METRIC_PREFIX;
        std::string tag    = metrics["tag"] ? metrics["tag"].as<std::string>() : name();

        Dbg(dbg_ctl, "Metrics for selector rule: %s(%s, %s)", name().c_str(), prefix.c_str(), tag.c_str());
        initializeMetrics(RATE_LIMITER_TYPE_SNI, prefix, tag);
      }
    }

    return true;
  }

  void
  initializeMetrics(uint type, std::string tag, std::string prefix = RATE_LIMITER_METRIC_PREFIX)
  {
    TSReleaseAssert(type < RATE_LIMITER_TYPE_MAX);
    memset(_metrics, 0, sizeof(_metrics));

    std::string metric_prefix = prefix;
    metric_prefix.push_back('.');
    metric_prefix.append(types[type]);

    if (!tag.empty()) {
      metric_prefix.push_back('.');
      metric_prefix.append(tag);
    } else if (!name().empty()) {
      metric_prefix.push_back('.');
      metric_prefix.append(name());
    }

    for (int i = 0; i < RATE_LIMITER_METRIC_MAX; i++) {
      size_t const metricsz = metric_prefix.length() + strlen(suffixes[i]) + 2; // padding for dot+terminator
      char *const metric    = static_cast<char *>(TSmalloc(metricsz));
      snprintf(metric, metricsz, "%s.%s", metric_prefix.data(), suffixes[i]);

      _metrics[i] = TS_ERROR;

      if (TSStatFindName(metric, &_metrics[i]) == TS_ERROR) {
        _metrics[i] = TSStatCreate(metric, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      }

      if (_metrics[i] != TS_ERROR) {
        Dbg(dbg_ctl, "established metric '%s' as ID %d", metric, _metrics[i]);
      } else {
        TSError("failed to create metric '%s'", metric);
      }

      TSfree(metric);
    }
  }

  // Reserve / release a slot from the active resource limits. Reserve will return
  // false if we are unable to reserve a slot.
  bool
  reserve()
  {
    std::lock_guard<std::mutex> lock(_active_lock);

    TSReleaseAssert(_active <= limit());
    if (_active < limit()) {
      ++_active;
      Dbg(dbg_ctl, "Reserving a slot, active entities == %u", _active.load());
      return true;
    }

    return false;
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
    QueueTime now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(_queue_lock);

    _queue.push_front(std::make_tuple(elem, cont, now));
    ++_size;
  }

  QueueItem
  pop()
  {
    QueueItem item;
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
  std::string _name                  = "_limiter_";                       // The name/descr (e.g. SNI name) of this limiter
  uint32_t _limit                    = UINT32_MAX;                        // No limit unless specified ...
  uint32_t _max_queue                = 0;                                 // No queue by default
  std::chrono::milliseconds _max_age = std::chrono::milliseconds::zero(); // Max age (ms) in the queue

private:
  std::atomic<uint32_t> _active = 0; // Current active number of txns. This has to always stay <= limit above
  std::atomic<uint32_t> _size   = 0; // Current size of the pending queue of txns. This should aim to be < _max_queue

  std::mutex _queue_lock, _active_lock; // Resource locks
  std::deque<QueueItem> _queue;         // Queue for the pending TXN's. ToDo: Should also move (see below)

  int _metrics[RATE_LIMITER_METRIC_MAX];
};
