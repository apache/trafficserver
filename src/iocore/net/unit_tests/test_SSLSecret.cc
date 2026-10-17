/** @file

  Catch based unit tests for SSLSecret

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

#include "../P_SSLSecret.h"

#include "api/LifecycleAPIHooks.h"
#include "ts/InkAPIPrivateIOCore.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
// Only count hook calls made by this test. The hook stays registered for the
// rest of the test binary, since lifecycle hooks cannot be removed.
std::atomic<bool> hook_armed{false};

std::mutex              hook_mutex;
std::condition_variable hook_cv;
int                     callers_in_hook{0};
int                     max_callers_in_hook{0};

int
count_concurrent_callers(TSCont /* cont ATS_UNUSED */, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  if (!hook_armed) {
    return 0;
  }
  std::unique_lock lock(hook_mutex);
  ++callers_in_hook;
  max_callers_in_hook = std::max(max_callers_in_hook, callers_in_hook);
  hook_cv.notify_all();
  // Linger until a second caller arrives, or give up. If the hook is not
  // serialized, the other threads are already waiting to enter it.
  hook_cv.wait_for(lock, std::chrono::milliseconds(250), [] { return callers_in_hook > 1; });
  --callers_in_hook;
  return 0;
}

// The SSLSecret that reenter_load_secret() calls back into, and how many times
// that hook has run.
std::atomic<SSLSecret *> reentry_secrets{nullptr};
std::atomic<int>         reentry_calls{0};

// Like a plugin whose hook calls TSSslSecretSet(): that can reach loadSecret()
// again on the same thread before the hook returns. Re-enter from the
// outermost call only.
int
reenter_load_secret(TSCont /* cont ATS_UNUSED */, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  SSLSecret *secrets = reentry_secrets;
  if (secrets == nullptr || reentry_calls++ > 0) {
    return 0;
  }
  std::string cert_data;
  std::string key_data;
  secrets->getOrLoadSecret("/nonexistent/reentry-inner-cert.pem", "/nonexistent/reentry-inner-key.pem", cert_data, key_data);
  return 0;
}
} // end anonymous namespace

TEST_CASE("SSL_SECRET hooks run one caller at a time")
{
  // Parallel ssl_multicert loading calls getOrLoadSecret() from several plain
  // std::threads. A plugin's secret hook must still see one call at a time.
  g_lifecycle_hooks->append(TS_LIFECYCLE_SSL_SECRET_HOOK, new INKContInternal(count_concurrent_callers, nullptr));
  hook_armed = true;

  constexpr int            num_threads = 4;
  SSLSecret                secrets;
  std::atomic<int>         waiting{num_threads};
  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&secrets, &waiting, i]() {
      --waiting;
      while (waiting > 0) {
        std::this_thread::yield();
      }
      // Each name is new to the secret map, so getOrLoadSecret() calls
      // loadSecret(), which runs the hook before it reads anything from disk.
      std::string cert_data;
      std::string key_data;
      secrets.getOrLoadSecret("/nonexistent/cert" + std::to_string(i) + ".pem", "/nonexistent/key" + std::to_string(i) + ".pem",
                              cert_data, key_data);
    });
  }
  for (auto &t : threads) {
    t.join();
  }
  hook_armed = false;

  CHECK(max_callers_in_hook == 1);
}

TEST_CASE("An SSL_SECRET hook can call back into SSLSecret on its own thread")
{
  // Static so it outlives the loader thread if that thread deadlocks.
  static SSLSecret secrets;
  g_lifecycle_hooks->append(TS_LIFECYCLE_SSL_SECRET_HOOK, new INKContInternal(reenter_load_secret, nullptr));
  reentry_secrets = &secrets;

  // Run the load on a detached thread so a deadlock fails the test instead of
  // hanging the binary. A std::async future would block in its destructor.
  auto done     = std::make_shared<std::promise<void>>();
  auto finished = done->get_future();
  std::thread([done]() {
    std::string cert_data;
    std::string key_data;
    secrets.getOrLoadSecret("/nonexistent/reentry-outer-cert.pem", "/nonexistent/reentry-outer-key.pem", cert_data, key_data);
    done->set_value();
  }).detach();
  bool const completed = finished.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  reentry_secrets      = nullptr;

  CHECK(completed);
  CHECK(reentry_calls == 2);
}
