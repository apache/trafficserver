/** @file

  Unit tests for the dedicated thread table in EventProcessor.

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

#include "inkevent_test_fixtures.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using inkevent_test::EventProcessorListener;

CATCH_REGISTER_LISTENER(EventProcessorListener)

namespace
{

/// Start continuation for a dedicated thread; counts the threads that have run it.
class StartContinuation : public Continuation
{
public:
  explicit StartContinuation(std::atomic<int> &started) : Continuation(nullptr), _started(started)
  {
    SET_HANDLER(&StartContinuation::handle_event);
  }

  int
  handle_event(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    _started.fetch_add(1, std::memory_order_release);
    return EVENT_DONE;
  }

private:
  std::atomic<int> &_started;
};

} // namespace

// The stats sync task walks the dedicated threads on its own thread while
// accept and logging threads are still being spawned during startup. On x86
// the null-entry check rarely trips even when the count is published without
// ordering; ThreadSanitizer is what reports that race.
TEST_CASE("active_dthreads can be walked while dedicated threads spawn", "[iocore]")
{
  constexpr int SPAWNS = 64;

  std::atomic<bool> is_spawning{true};
  std::atomic<int>  null_entries{0};
  std::atomic<int>  started{0};
  std::thread       walker([&]() {
    while (is_spawning.load(std::memory_order_acquire)) {
      for (EThread *et : eventProcessor.active_dthreads()) {
        if (et == nullptr) {
          null_entries.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  });

  std::vector<std::unique_ptr<StartContinuation>> conts;
  for (int i = 0; i < SPAWNS; ++i) {
    conts.push_back(std::make_unique<StartContinuation>(started));
    REQUIRE(eventProcessor.spawn_thread(conts.back().get(), "[TEST_DTHREAD]") != nullptr);
  }
  is_spawning.store(false, std::memory_order_release);
  walker.join();

  // Wait for every handler to run before conts is destroyed.
  auto const deadline = std::chrono::steady_clock::now() + inkevent_test::DEFAULT_TIMEOUT;
  while (started.load(std::memory_order_acquire) < SPAWNS && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  REQUIRE(started.load(std::memory_order_acquire) == SPAWNS);

  CHECK(null_entries.load() == 0);

  int walked = 0;
  for (EThread *et : eventProcessor.active_dthreads()) {
    CHECK(et != nullptr);
    ++walked;
  }
  CHECK(walked == SPAWNS);
}
