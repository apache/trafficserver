/** @file

  A watchdog for event loops

  Each event thread advertises its current state through a lightweight
  "heartbeat" struct: the thread publishes the timestamps for the most recent
  sleep/wake pair along with a monotonically increasing sequence number.
  `Watchdog::Monitor`, started from `traffic_server.cc`, runs in its own
  `std::thread` and periodically scans those heartbeats; if a thread has been
  awake longer than the configured timeout it emits a warning (timeout values
  come from `proxy.config.exec_thread.watchdog.timeout_ms`, where 0 disables
  the monitor).  The monitor never touches event-system locks, keeping the
  runtime overhead in the hot loop confined to a handful of atomic updates.

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

#include <atomic>
#include <chrono>
#include <vector>
#include <thread>

class EThread;

namespace Watchdog
{
struct Heartbeat {
  std::atomic<std::chrono::time_point<std::chrono::steady_clock>> last_sleep{
    std::chrono::steady_clock::time_point::min()}; // set right before sleeping (e.g. before epoll_wait)
  std::atomic<std::chrono::time_point<std::chrono::steady_clock>> last_wake{
    std::chrono::steady_clock::time_point::min()}; // set right after waking from sleep (e.g. epoll_wait returns)
  std::atomic<uint64_t> seq{0};                    // increment on each loop - used to deduplicate warnings
  std::atomic<uint64_t> warned_seq{0};             // last seq we logged a warning about
};

class Monitor
{
public:
  explicit Monitor(EThread *threads[], size_t n_threads, std::chrono::milliseconds timeout_ms);
  ~Monitor();
  Monitor() = delete;

private:
  const std::vector<EThread *>    _threads;
  std::thread                     _watchdog_thread;
  const std::chrono::milliseconds _timeout;
  std::atomic<bool>               _shutdown = false;
  void                            monitor_loop() const;
};

} // namespace Watchdog
