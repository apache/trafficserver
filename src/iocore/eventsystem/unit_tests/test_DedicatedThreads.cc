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

// Static so a handler that runs after a timed-out case has failed still has a counter to update.
std::atomic<int> started{0};

int
count_active_dthreads()
{
  int n = 0;
  for ([[maybe_unused]] EThread *et : eventProcessor.active_dthreads()) {
    ++n;
  }
  return n;
}

} // namespace

// The stats sync task walks the dedicated threads on its own thread while
// accept and logging threads are still being spawned during startup. On x86
// the null-entry check rarely trips, so there this case mostly checks the walk;
// ThreadSanitizer reports a missing ordering on any CPU.
TEST_CASE("active_dthreads can be walked while dedicated threads spawn", "[iocore]")
{
  constexpr int SPAWNS = 64;

  // eventProcessor outlives this case and may already hold dedicated threads.
  int const already = count_active_dthreads();

  std::atomic<bool> is_spawning{true};
  std::atomic<bool> is_walking{false};
  std::atomic<int>  null_entries{0};
  std::thread       walker([&]() {
    is_walking.store(true, std::memory_order_release);
    while (is_spawning.load(std::memory_order_acquire)) {
      for (EThread *et : eventProcessor.active_dthreads()) {
        if (et == nullptr) {
          null_entries.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  });

  // Spawn only once the walker runs, so the walks overlap the spawns.
  while (!is_walking.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

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
  int const ran = started.load(std::memory_order_acquire);
  if (ran < SPAWNS) {
    // The start event of a handler that has not run yet still points at its continuation; keep them all alive.
    for (auto &cont : conts) {
      static_cast<void>(cont.release());
    }
  }
  REQUIRE(ran == SPAWNS);

  CHECK(null_entries.load() == 0);

  int walked = 0;
  for (EThread *et : eventProcessor.active_dthreads()) {
    CHECK(et != nullptr);
    ++walked;
  }
  CHECK(walked == already + SPAWNS);
}
