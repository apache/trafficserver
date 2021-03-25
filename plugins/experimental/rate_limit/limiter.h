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
#include <atomic>
#include <cstring>
#include <cstdio>

#include <ts/ts.h>

constexpr char const PLUGIN_NAME[]  = "rate_limit";
constexpr unsigned QUEUE_DELAY_TIME = 100; // Examine the queue every 100ms.

using QueueItem = std::tuple<TSHttpTxn, TSCont>;

///////////////////////////////////////////////////////////////////////////////
// Configuration object for a rate limiting remap rule.
//
class RateLimiter
{
public:
  RateLimiter() : _queue_lock(TSMutexCreate()), _active_lock(TSMutexCreate()) {}

  ~RateLimiter()
  {
    if (_queue_cont) {
      TSContDestroy(_queue_cont);
    }
    TSMutexDestroy(_queue_lock);
    TSMutexDestroy(_active_lock);
  }

  // Reserve / release a slot from the active connect limits. Reserve will return
  // false if we are unable to reserve a slot.
  bool reserve();
  void release();

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
    TSMutexLock(_queue_lock);
    _queue.push_back(std::make_tuple(txnp, cont));
    ++_size;
    TSMutexUnlock(_queue_lock);
  }

  QueueItem
  pop()
  {
    QueueItem item{nullptr, nullptr};

    TSMutexLock(_queue_lock);
    if (!_queue.empty()) {
      item = std::move(_queue.front());
      _queue.pop_front();
      --_size;
    }
    TSMutexUnlock(_queue_lock);

    return item; // ToDo: do we see RVO here ?
  }

  // Setup the continuous queue processing continuation
  void
  setupQueueCont()
  {
    _queue_cont = TSContCreate(queue_process_cont, TSMutexCreate());
    TSReleaseAssert(_queue_cont);
    TSContDataSet(_queue_cont, this);
    TSContScheduleEveryOnPool(_queue_cont, QUEUE_DELAY_TIME, TS_THREAD_POOL_TASK);
  }

  // Create and setup a TXN continuation for a connection that needs to be delayed
  void
  setupTxnCont(void *ih, TSHttpTxn txnp, TSHttpHookID hook)
  {
    TSCont cont = TSContCreate(rate_limit_cont, nullptr);
    TSReleaseAssert(cont);

    TSContDataSet(cont, ih);
    TSHttpTxnHookAdd(txnp, hook, cont);
  }

  // These are the configurable portions of this limiter, public so sue me.
  unsigned limit     = 100; // Arbitrary default, probably should be a required config
  unsigned max_queue = 0;   // No queue limit, but if sets will give an immediate error if at max
  unsigned error     = 429; // Error code when we decide not to allow a txn to be processed (e.g. queue full)

private:
  static int queue_process_cont(TSCont cont, TSEvent event, void *edata);
  static int rate_limit_cont(TSCont cont, TSEvent event, void *edata);

  std::atomic<unsigned> _active = 0; // Current active number of txns. This has to always stay <= limit above
  std::atomic<unsigned> _size   = 0; // Current size of the pending queue of txns. This should aim to be < _max_queue.

  TSMutex _queue_lock, _active_lock; // Resource locks
  std::deque<QueueItem> _queue;      // Queue for the pending TXN's
  TSCont _queue_cont = nullptr;      // Continuation processing the queue periodically.
};
