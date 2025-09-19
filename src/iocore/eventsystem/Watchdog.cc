/** @file

  A watchdog for event loops

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

#include "iocore/eventsystem/Watchdog.h"
#include "iocore/eventsystem/EThread.h"
#include "tscore/Diags.h"
#include "tscore/ink_assert.h"
#include "tsutil/DbgCtl.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <functional>

namespace Watchdog
{

DbgCtl dbg_ctl_watchdog("watchdog");

Monitor::Monitor(EThread *threads[], size_t n_threads, std::chrono::milliseconds timeout_ms)
  : _threads(threads, threads + n_threads), _watchdog_thread{std::bind_front(&Monitor::monitor_loop, this)}, _timeout{timeout_ms}
{
  ink_assert(timeout_ms.count() > 0);
}

void
Monitor::monitor_loop(const std::stop_token &stoken) const
{
  // Divide by a floating point 2 to avoid truncation to zero.
  auto sleep_time = _timeout / 2.0;
  ink_release_assert(sleep_time.count() > 0);
  Dbg(dbg_ctl_watchdog, "Starting watchdog with timeout %" PRIu64 " ms on %zu threads.  sleep_time = %" PRIu64 " us",
      _timeout.count(), _threads.size(), std::chrono::duration_cast<std::chrono::microseconds>(sleep_time).count());

  while (!stoken.stop_requested()) {
    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < _threads.size(); ++i) {
      EThread                                           *t          = _threads[i];
      std::chrono::time_point<std::chrono::steady_clock> last_sleep = t->heartbeat_state.last_sleep.load(std::memory_order_relaxed);
      if (last_sleep == std::chrono::steady_clock::time_point::min()) {
        // initial value sentinel - event loop hasn't started
        continue;
      }
      std::chrono::time_point<std::chrono::steady_clock> last_wake = t->heartbeat_state.last_wake.load(std::memory_order_relaxed);

      if (last_wake == std::chrono::steady_clock::time_point::min() || last_wake < last_sleep) {
        // not yet woken from last sleep
        continue;
      }

      auto awake_duration = now - last_wake;
      if (awake_duration > _timeout) {
        uint64_t seq        = t->heartbeat_state.seq.load(std::memory_order_relaxed);
        uint64_t warned_seq = t->heartbeat_state.warned_seq.load(std::memory_order_relaxed);
        if (warned_seq < seq) {
          // Warn once per loop iteration
          Error("Watchdog: [ET_NET %zu] has been awake for %" PRIu64 " ms", i,
                std::chrono::duration_cast<std::chrono::milliseconds>(awake_duration).count());
          t->heartbeat_state.warned_seq.store(seq, std::memory_order_relaxed);
        }
      }
    }

    std::this_thread::sleep_for(sleep_time);
  }
  Dbg(dbg_ctl_watchdog, "Stopping watchdog");
}
} // namespace Watchdog
