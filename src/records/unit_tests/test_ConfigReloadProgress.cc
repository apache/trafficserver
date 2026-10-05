/** @file

  Unit tests for the ConfigReloadProgress checker's ownership of its reload task

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

#include "iocore/eventsystem/Tasks.h"
#include "mgmt/config/ConfigReloadTrace.h"
#include "records/RecCore.h"
#include "records/RecordsConfig.h"

#include <chrono>
#include <memory>
#include <thread>

namespace
{
// The checker runs on ET_TASK, so the test run needs task threads and the reload records.
struct ReloadCheckerListener : inkevent_test::EventProcessorListener {
  using EventProcessorListener::EventProcessorListener;

  void
  testRunStarting(Catch::TestRunInfo const &info) override
  {
    EventProcessorListener::testRunStarting(info);
    LibRecordsConfigInit();
    tasksProcessor.register_event_type();
    tasksProcessor.start(1);
  }
};

bool
wait_for_sole_owner(ConfigReloadTaskPtr const &task, std::chrono::seconds timeout)
{
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (task.use_count() > 1) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
  }
  return true;
}
} // namespace

CATCH_REGISTER_LISTENER(ReloadCheckerListener)

TEST_CASE("Progress checker releases a task confirmed terminal", "[config][reload][lifetime]")
{
  auto task = std::make_shared<ConfigReloadTask>("test-token-terminal", "main task", true, nullptr);
  task->start_progress_checker();
  REQUIRE(task.use_count() == 2);

  task->mark_as_bad_state("forced");
  // The first check runs after check_interval (2 s) and confirms the terminal state 5 s later.
  REQUIRE(wait_for_sole_owner(task, std::chrono::seconds{15}));
}

TEST_CASE("Progress checker releases a task that timed out", "[config][reload][lifetime]")
{
  REQUIRE(RecSetRecordString(ConfigReloadProgress::RECORD_TIMEOUT.data(), "1s", REC_SOURCE_EXPLICIT) == REC_ERR_OKAY);

  auto task = std::make_shared<ConfigReloadTask>("test-token-timeout", "main task", true, nullptr);
  task->start_progress_checker();
  REQUIRE(task.use_count() == 2);

  REQUIRE(wait_for_sole_owner(task, std::chrono::seconds{10}));
  REQUIRE(task->get_state() == ConfigReloadTask::State::TIMEOUT);
}
