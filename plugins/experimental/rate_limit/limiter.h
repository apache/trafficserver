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

#include "tscore/ink_config.h"
#include "ts/ts.h"
#include "utilities.h"

constexpr auto QUEUE_DELAY_TIME = std::chrono::milliseconds{200}; // Examine the queue every 200ms
using QueueTime                 = std::chrono::time_point<std::chrono::system_clock>;

enum {
  RATE_LIMITER_TYPE_SNI = 0,
  RATE_LIMITER_TYPE_REMAP,

  RATE_LIMITER_TYPE_MAX
};

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

public:
  RateLimiter() : _queue_lock(TSMutexCreate()), _active_lock(TSMutexCreate()) {}

  virtual ~RateLimiter()
  {
    TSMutexDestroy(_queue_lock);
    TSMutexDestroy(_active_lock);
  }

  // Reserve / release a slot from the active resource limits. Reserve will return
  // false if we are unable to reserve a slot.
  bool
  reserve()
  {
    TSReleaseAssert(_active <= limit);
    TSMutexLock(_active_lock);
    if (_active < limit) {
      ++_active;
      TSMutexUnlock(_active_lock); // Reduce the critical section, release early
      TSDebug(PLUGIN_NAME, "Reserving a slot, active entities == %u", active());
      return true;
    } else {
      TSMutexUnlock(_active_lock);
      return false;
    }
  }

  void
  release()
  {
    TSMutexLock(_active_lock);
    --_active;
    TSMutexUnlock(_active_lock);
    TSDebug(PLUGIN_NAME, "Releasing a slot, active entities == %u", active());
  }

  // Current size of the active_in connections
  unsigned
  active() const
  {
    return _active.load();
  }

  // Current size of the queue
  unsigned
  size() const
  {
    return _size.load();
  }

  // Is the queue full (at it's max size)?
  bool
  full() const
  {
    return (_size == max_queue);
  }

  void
  push(T elem, TSCont cont)
  {
    QueueTime now = std::chrono::system_clock::now();

    TSMutexLock(_queue_lock);
    _queue.push_front(std::make_tuple(elem, cont, now));
    ++_size;
    TSMutexUnlock(_queue_lock);
  }

  QueueItem
  pop()
  {
    QueueItem item;

    TSMutexLock(_queue_lock);
    if (!_queue.empty()) {
      item = std::move(_queue.back());
      _queue.pop_back();
      --_size;
    }
    TSMutexUnlock(_queue_lock);

    return item;
  }

  bool
  hasOldEntity(QueueTime now) const
  {
    TSMutexLock(_queue_lock);
    if (!_queue.empty()) {
      QueueItem item = _queue.back();
      TSMutexUnlock(_queue_lock); // A little ugly but this reduces the critical section for the lock a little bit.

      std::chrono::milliseconds age = std::chrono::duration_cast<std::chrono::milliseconds>(now - std::get<2>(item));

      return (age >= max_age);
    } else {
      TSMutexUnlock(_queue_lock);
      return false;
    }
  }

  void
  initializeMetrics(uint type)
  {
    TSReleaseAssert(type < RATE_LIMITER_TYPE_MAX);
    memset(_metrics, 0, sizeof(_metrics));

    std::string metric_prefix = prefix;
    metric_prefix.append("." + std::string(types[type]));

    if (!tag.empty()) {
      metric_prefix.append("." + tag);
    } else if (!description.empty()) {
      metric_prefix.append("." + description);
    }

    for (int i = 0; i < RATE_LIMITER_METRIC_MAX; i++) {
      size_t const metricsz = metric_prefix.length() + strlen(suffixes[i]) + 2; // padding for dot+terminator
      char *const metric    = (char *)TSmalloc(metricsz);
      snprintf(metric, metricsz, "%s.%s", metric_prefix.data(), suffixes[i]);

      _metrics[i] = TS_ERROR;

      if (TSStatFindName(metric, &_metrics[i]) == TS_ERROR) {
        _metrics[i] = TSStatCreate(metric, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      }

      if (_metrics[i] != TS_ERROR) {
        TSDebug(PLUGIN_NAME, "established metric '%s' as ID %d", metric, _metrics[i]);
      } else {
        TSError("failed to create metric '%s'", metric);
      }

      TSfree(metric);
    }
  }

  void
  incrementMetric(uint metric)
  {
    if (_metrics[metric] != TS_ERROR) {
      TSStatIntIncrement(_metrics[metric], 1);
    }
  }

  // Initialize a new instance of this rate limiter
  bool initialize(int argc, const char *argv[]);

  // These are the configurable portions of this limiter, public so sue me.
  unsigned limit                    = 100;      // Arbitrary default, probably should be a required config
  unsigned max_queue                = UINT_MAX; // No queue limit, but if sets will give an immediate error if at max
  std::chrono::milliseconds max_age = std::chrono::milliseconds::zero(); // Max age (ms) in the queue
  std::string description           = "";
  std::string prefix                = RATE_LIMITER_METRIC_PREFIX; // metric prefix, i.e.: plugin.rate_limiter
  std::string tag                   = "";                         // optional tag to append to the prefix (prefix.tag)

private:
  std::atomic<unsigned> _active = 0; // Current active number of txns. This has to always stay <= limit above
  std::atomic<unsigned> _size   = 0; // Current size of the pending queue of txns. This should aim to be < _max_queue

  TSMutex _queue_lock, _active_lock; // Resource locks
  std::deque<QueueItem> _queue;      // Queue for the pending TXN's. ToDo: Should also move (see below)

  int _metrics[RATE_LIMITER_METRIC_MAX];
};
