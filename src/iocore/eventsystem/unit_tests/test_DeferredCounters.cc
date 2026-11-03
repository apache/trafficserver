/** @file

  Event threads defer counter increments and publish them without further traffic.

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

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <catch2/interfaces/catch_interfaces_config.hpp>

#include "iocore/eventsystem/EventSystem.h"
#include "iocore/utils/diags.i"
#include "tscore/Layout.h"
#include "tsutil/Metrics.h"

#include <atomic>
#include <chrono>
#include <thread>

using ts::Metrics;

namespace
{
Metrics::Counter::AtomicType *counter = nullptr;
// The counter's value as the incrementing thread saw it right after its own increment.
std::atomic<int64_t> seen_after_increment{-1};

struct Incrementer : public Continuation {
  explicit Incrementer(ProxyMutex *m) : Continuation(m) { SET_HANDLER(&Incrementer::handle); }

  int
  handle(int, Event *)
  {
    Metrics::Counter::increment(counter, 7);
    seen_after_increment = Metrics::Counter::load(counter);
    return 0;
  }
};

/// Polls until @a metric reaches @a expected or the deadline passes; returns its last value.
int64_t
wait_for(Metrics::Counter::AtomicType *metric, int64_t expected, std::chrono::milliseconds deadline)
{
  auto const until = std::chrono::steady_clock::now() + deadline;
  int64_t    value = Metrics::Counter::load(metric);
  while (value != expected && std::chrono::steady_clock::now() < until) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    value = Metrics::Counter::load(metric);
  }
  return value;
}
} // namespace

TEST_CASE("event threads publish deferred counters without more traffic", "[iocore][metrics]")
{
  counter = Metrics::Counter::createPtr("test.deferred_counters.increments");
  Incrementer inc{new_ProxyMutex()};

  eventProcessor.schedule_imm(&inc);

  // The thread goes idle after the event, so it flushes well before its 60ms heartbeat would.
  CHECK(wait_for(counter, 7, std::chrono::milliseconds(1000)) == 7);
  // The increment was held in the thread's table rather than applied directly.
  CHECK(seen_after_increment.load() == 0);
}

struct DeferredCountersListener : Catch::EventListenerBase {
  using EventListenerBase::EventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const &) override
  {
    Layout::create();
    init_diags("", nullptr);
    RecProcessInit();

    thread_deferred_counter_interval_ms = 5;
    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    eventProcessor.start(2, 1048576);

    EThread *main_thread = new EThread;
    main_thread->set_specific();
  }
};

CATCH_REGISTER_LISTENER(DeferredCountersListener);
