/** @file

  ConfigProcessor per-thread pins and leases.

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
#include "iocore/eventsystem/ConfigProcessor.h"
#include "iocore/utils/diags.i"
#include "tscore/Layout.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
std::atomic<int> destroyed{0};

struct TestConfig : public ConfigInfo {
  explicit TestConfig(int v) : value(v) {}
  ~TestConfig() override { destroyed.fetch_add(1); }
  int value;
};

// A config class shaped like the ones that use scoped_config.
struct TestConfigClass {
  static inline unsigned id = 0;

  static unsigned
  config_id()
  {
    return id;
  }
  static TestConfig *
  acquire()
  {
    return static_cast<TestConfig *>(configProcessor.get(id));
  }
  static void
  release(TestConfig *config)
  {
    configProcessor.release(id, config);
  }
  using scoped_config = ConfigProcessor::scoped_config<TestConfigClass, TestConfig>;
};

int
refcount(ConfigInfo *config)
{
  return config->refcount();
}

/// Polls until @a count objects have been destroyed or the deadline passes.
int
wait_for_destroyed(int count, std::chrono::milliseconds deadline)
{
  auto const until = std::chrono::steady_clock::now() + deadline;
  while (destroyed.load() < count && std::chrono::steady_clock::now() < until) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return destroyed.load();
}
} // namespace

TEST_CASE("ConfigProcessor pins and leases", "[iocore][config]")
{
  auto *first             = new TestConfig(1);
  TestConfigClass::id     = configProcessor.set(0, first, 1);
  unsigned const id       = TestConfigClass::id;
  int const      baseline = refcount(first); // the processor's own reference

  ConfigProcessor::enable_thread_pins();

  SECTION("repeated pinned and scoped reads share one reference per thread")
  {
    CHECK(configProcessor.get_pinned(id) == first);
    CHECK(refcount(first) == baseline + 1);
    for (int i = 0; i < 10; ++i) {
      TestConfigClass::scoped_config scoped;
      CHECK(scoped->value == 1);
    }
    CHECK(configProcessor.get_pinned(id) == first);
    CHECK(refcount(first) == baseline + 1);
  }

  SECTION("leases count against the thread's pin")
  {
    auto lease_a = configProcessor.get_leased(id);
    auto lease_b = configProcessor.get_leased(id);
    CHECK(lease_a.get() == first);
    CHECK(refcount(first) == baseline + 1);
  }

  SECTION("a replaced configuration is released by the sweep but kept alive by a lease")
  {
    auto lease = configProcessor.get_leased(id);

    auto *second = new TestConfig(2);
    configProcessor.set(id, second, 1);
    CHECK(static_cast<TestConfig *>(configProcessor.get_pinned(id))->value == 2);

    ConfigProcessor::release_stale_pins();
    // The processor still holds its reference for the release timeout, and the lease holds the thread's.
    CHECK(refcount(first) == baseline + 1);
    CHECK(lease.get() == first);

    // A lease released on another thread still releases the thread's reference.
    std::thread([&lease] { lease.reset(); }).join();
    CHECK(refcount(first) == baseline);

    // Once the processor drops its reference, nothing holds the first configuration.
    int const before = destroyed.load();
    CHECK(wait_for_destroyed(before + 1, std::chrono::seconds(5)) == before + 1);
  }

  SECTION("a pinned read stays valid for the event after the processor's delayed reference expires")
  {
    auto *held = static_cast<TestConfig *>(configProcessor.get_pinned(id));

    configProcessor.set(id, new TestConfig(2), 1);
    // Simulate an event that outlasts the release timeout: only this thread's pin is left.
    auto const until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (refcount(held) > 1 && std::chrono::steady_clock::now() < until) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(refcount(held) == 1);

    int const before = destroyed.load();
    CHECK(static_cast<TestConfig *>(configProcessor.get_pinned(id))->value == 2);
    // A dropped last reference is destroyed on another thread, so give it time to run.
    CHECK(wait_for_destroyed(before + 1, std::chrono::milliseconds(200)) == before);
    CHECK(held->value == 1);

    ConfigProcessor::release_stale_pins();
    CHECK(wait_for_destroyed(before + 1, std::chrono::seconds(5)) == before + 1);
  }

  SECTION("threads without pins use the shared refcount")
  {
    ConfigInfo *current = configProcessor.get_pinned(id);
    int const   pinned  = refcount(current);
    std::thread([id, current, pinned] {
      CHECK_FALSE(ConfigProcessor::thread_pins_enabled());
      {
        TestConfigClass::scoped_config scoped;
        CHECK(refcount(current) == pinned + 1);
      }
      auto lease = configProcessor.get_leased(id);
      CHECK(refcount(current) == pinned + 1);
      lease.reset();
      CHECK(refcount(current) == pinned);
    }).join();
  }

  ConfigProcessor::disable_thread_pins();
}

struct ConfigPinsListener : Catch::EventListenerBase {
  using EventListenerBase::EventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const &) override
  {
    Layout::create();
    init_diags("", nullptr);
    RecProcessInit();

    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    eventProcessor.start(2, 1048576);

    EThread *main_thread = new EThread;
    main_thread->set_specific();
  }
};

CATCH_REGISTER_LISTENER(ConfigPinsListener);
