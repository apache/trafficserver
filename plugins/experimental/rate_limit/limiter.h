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
#include <deque>
#include <tuple>
#include <climits>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <string>

#include <ts/ts.h>

constexpr char const PLUGIN_NAME[] = "rate_limit";
constexpr auto QUEUE_DELAY_TIME    = std::chrono::milliseconds{100}; // Examine the queue every 100ms

using QueueTime = std::chrono::time_point<std::chrono::system_clock>;
using QueueItem = std::tuple<TSHttpTxn, TSCont, QueueTime>;

///////////////////////////////////////////////////////////////////////////////
// Configuration object for a rate limiting remap rule.
//
class RateLimiter
{
public:
  RateLimiter() : _queue_lock(TSMutexCreate()), _active_lock(TSMutexCreate()) {}

  ~RateLimiter()
  {
    if (_action) {
      TSActionCancel(_action);
    }
    if (_queue_cont) {
      TSContDestroy(_queue_cont);
    }
    TSMutexDestroy(_queue_lock);
    TSMutexDestroy(_active_lock);
  }

  // Reserve / release a slot from the active connect limits. Reserve will return
  // false if we are unable to reserve a slot.
  bool
  reserve()
  {
    TSReleaseAssert(_active <= limit);
    TSMutexLock(_active_lock);
    if (_active < limit) {
      ++_active;
      TSMutexUnlock(_active_lock); // Reduce the critical section, release early
      TSDebug(PLUGIN_NAME, "Reserving a slot, active txns == %u", active());
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
    TSDebug(PLUGIN_NAME, "Releasing a slot, active txns == %u", active());
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
  push(TSHttpTxn txnp, TSCont cont)
  {
    QueueTime now = std::chrono::system_clock::now();

    TSMutexLock(_queue_lock);
    _queue.push_front(std::make_tuple(txnp, cont, now));
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
  hasOldTxn(QueueTime now) const
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

  void delayHeader(TSHttpTxn txpn, std::chrono::microseconds delay) const;
  void retryAfter(TSHttpTxn txpn, unsigned after) const;

  // Continuation creation and scheduling
  void setupQueueCont();

  void
  setupTxnCont(void *ih, TSHttpTxn txnp, TSHttpHookID hook)
  {
    TSCont cont = TSContCreate(rate_limit_cont, nullptr);
    TSReleaseAssert(cont);

    TSContDataSet(cont, ih);
    TSHttpTxnHookAdd(txnp, hook, cont);
  }

  // These are the configurable portions of this limiter, public so sue me.
  unsigned limit                    = 100;      // Arbitrary default, probably should be a required config
  unsigned max_queue                = UINT_MAX; // No queue limit, but if sets will give an immediate error if at max
  unsigned error                    = 429;      // Error code when we decide not to allow a txn to be processed (e.g. queue full)
  unsigned retry                    = 0;        // If > 0, we will also send a Retry-After: header with this retry value
  std::chrono::milliseconds max_age = std::chrono::milliseconds::zero(); // Max age (ms) in the queue
  std::string header; // Header to put the latency metrics in, e.g. @RateLimit-Delay

private:
  static int queue_process_cont(TSCont cont, TSEvent event, void *edata);
  static int rate_limit_cont(TSCont cont, TSEvent event, void *edata);

  std::atomic<unsigned> _active = 0; // Current active number of txns. This has to always stay <= limit above
  std::atomic<unsigned> _size   = 0; // Current size of the pending queue of txns. This should aim to be < _max_queue

  TSMutex _queue_lock, _active_lock; // Resource locks
  std::deque<QueueItem> _queue;      // Queue for the pending TXN's
  TSCont _queue_cont = nullptr;      // Continuation processing the queue periodically
  TSAction _action   = nullptr;      // The action associated with the queue continuation, needed to shut it down
};
