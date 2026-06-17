/** @file

  Shared test fixtures for inkevent Catch2 unit tests.

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
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <catch2/interfaces/catch_interfaces_config.hpp>

#include "iocore/eventsystem/EventSystem.h"
#include "tscore/Layout.h"

#include "iocore/utils/diags.i"

namespace inkevent_test
{

inline constexpr int    DEFAULT_TEST_THREADS   = 2;
inline constexpr size_t DEFAULT_TEST_STACKSIZE = 1048576;
inline constexpr auto   DEFAULT_TIMEOUT        = std::chrono::seconds{5};

/**
  Catch2 event listener that boots the inkevent eventProcessor once per
  test executable. Mirrors the in-file listener used by test_EventSystem.cc
  / test_IOBuffer.cc; lifted here so each new inkevent test file registers
  the listener with a single CATCH_REGISTER_LISTENER call rather than
  duplicating the boot logic.
*/
struct EventProcessorListener : Catch::EventListenerBase {
  using EventListenerBase::EventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const & /* testRunInfo ATS_UNUSED */) override
  {
    Layout::create();
    init_diags("", nullptr);
    RecProcessInit();

    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    eventProcessor.start(DEFAULT_TEST_THREADS, DEFAULT_TEST_STACKSIZE);

    EThread *main_thread = new EThread;
    main_thread->set_specific();
  }
};

/**
  Atomic boolean flag with a bounded acquire-load wait. Used by
  multi-threaded tests to observe a one-shot signal from a Continuation
  handler without sleeping in the assertion path.
*/
class AtomicFlag
{
public:
  void
  set()
  {
    flag.store(true, std::memory_order_release);
  }

  bool
  is_set() const
  {
    return flag.load(std::memory_order_acquire);
  }

  bool
  wait_until_set(std::chrono::milliseconds timeout = std::chrono::duration_cast<std::chrono::milliseconds>(DEFAULT_TIMEOUT))
  {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag.load(std::memory_order_acquire)) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      std::this_thread::yield();
    }
    return true;
  }

private:
  std::atomic<bool> flag{false};
};

/**
  Continuation whose handler counts every dispatch into a public atomic
  counter. The first consumer is the Continuation tests in
  test_Continuation.cc; later inkevent tests reuse it whenever they need
  to observe handler invocations as an externally-visible side effect.

  Usage:
    CountingContinuation cont{new_ProxyMutex()};
    eventProcessor.schedule_imm(&cont, ET_CALL);
    REQUIRE(cont.wait_until_at_least(1));
*/
class CountingContinuation : public Continuation
{
public:
  explicit CountingContinuation(ProxyMutex *amutex) : Continuation(amutex) { SET_HANDLER(&CountingContinuation::handle_event); }

  int
  count() const
  {
    return counter.load(std::memory_order_acquire);
  }

  bool
  wait_until_at_least(int                       n,
                      std::chrono::milliseconds timeout = std::chrono::duration_cast<std::chrono::milliseconds>(DEFAULT_TIMEOUT))
  {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (counter.load(std::memory_order_acquire) < n) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      std::this_thread::yield();
    }
    return true;
  }

private:
  int
  handle_event(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    counter.fetch_add(1, std::memory_order_release);
    return 0;
  }

  std::atomic<int> counter{0};
};

} // namespace inkevent_test
