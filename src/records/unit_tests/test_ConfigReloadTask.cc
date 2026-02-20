/** @file

  Unit tests for ConfigReloadProgress timeout configuration and ReloadCoordinator::cancel_reload

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

#include "mgmt/config/ConfigReloadTrace.h"
#include "mgmt/config/ReloadCoordinator.h"

// Note: These tests verify the default values and basic logic.
// Full integration testing with records is done via autest.

TEST_CASE("ConfigReloadProgress default timeout", "[config][reload][timeout]")
{
  SECTION("Default timeout is 1 hour")
  {
    // Default should be "1h" which equals 3600000ms
    REQUIRE(std::string(ConfigReloadProgress::DEFAULT_TIMEOUT) == "1h");
  }
}

TEST_CASE("ConfigReloadProgress constants", "[config][reload][timeout]")
{
  SECTION("Record names are correct")
  {
    REQUIRE(std::string(ConfigReloadProgress::RECORD_TIMEOUT) == "proxy.config.admin.reload.timeout");
    REQUIRE(std::string(ConfigReloadProgress::RECORD_CHECK_INTERVAL) == "proxy.config.admin.reload.check_interval");
  }

  SECTION("Default values are sensible")
  {
    // Default timeout should be "1h"
    REQUIRE(std::string(ConfigReloadProgress::DEFAULT_TIMEOUT) == "1h");
    // Default check interval should be "2s"
    REQUIRE(std::string(ConfigReloadProgress::DEFAULT_CHECK_INTERVAL) == "2s");
    // Minimum check interval should be 1 second
    REQUIRE(ConfigReloadProgress::MIN_CHECK_INTERVAL_MS == 1000);
  }
}

TEST_CASE("ReloadCoordinator mark_task_as_stale with no task", "[config][reload][stale]")
{
  auto &coord = ReloadCoordinator::Get_Instance();

  SECTION("mark_task_as_stale returns false when no current task")
  {
    // Ensure no task is running (might have leftover from previous tests)
    // Note: In a real scenario, we'd wait for any existing task to complete

    // Try to mark stale with non-existent token
    bool marked = coord.mark_task_as_stale("nonexistent-token-xyz", "Test stale");
    REQUIRE(marked == false);
  }
}

TEST_CASE("ConfigReloadTask state transitions", "[config][reload][state]")
{
  SECTION("Task can be marked as timeout (bad state)")
  {
    auto task = std::make_shared<ConfigReloadTask>("test-token", "test task", false, nullptr);

    // Initial state should be CREATED
    REQUIRE(task->get_state() == ConfigReloadTask::State::CREATED);

    // Mark as in progress first
    task->set_in_progress();
    REQUIRE(task->get_state() == ConfigReloadTask::State::IN_PROGRESS);

    // Now mark as bad state (timeout)
    task->mark_as_bad_state("Test timeout");
    REQUIRE(task->get_state() == ConfigReloadTask::State::TIMEOUT);

    // Verify logs contain the reason
    auto logs = task->get_logs();
    REQUIRE(!logs.empty());
    REQUIRE(logs.back().find("Test timeout") != std::string::npos);
  }

  SECTION("Terminal states cannot be changed via mark_as_bad_state")
  {
    auto task = std::make_shared<ConfigReloadTask>("test-token-2", "test task 2", false, nullptr);

    // Set to SUCCESS (terminal state)
    task->set_completed();
    REQUIRE(task->get_state() == ConfigReloadTask::State::SUCCESS);

    // Try to mark as timeout — terminal guard rejects the transition
    task->mark_as_bad_state("Should not apply");
    REQUIRE(task->get_state() == ConfigReloadTask::State::SUCCESS);

    // Verify the rejected reason was NOT added to logs
    auto logs = task->get_logs();
    for (const auto &log : logs) {
      REQUIRE(log.find("Should not apply") == std::string::npos);
    }
  }

  SECTION("Terminal states cannot be changed via set_state_and_notify")
  {
    auto task = std::make_shared<ConfigReloadTask>("test-token-3", "test task 3", false, nullptr);

    // Set to FAIL (terminal state)
    task->set_failed();
    REQUIRE(task->get_state() == ConfigReloadTask::State::FAIL);

    // Try to transition to SUCCESS — rejected
    task->set_completed();
    REQUIRE(task->get_state() == ConfigReloadTask::State::FAIL);

    // Try to transition to IN_PROGRESS — rejected
    task->set_in_progress();
    REQUIRE(task->get_state() == ConfigReloadTask::State::FAIL);
  }
}

TEST_CASE("State to string conversion", "[config][reload][state]")
{
  // Runtime checks
  REQUIRE(ConfigReloadTask::state_to_string(ConfigReloadTask::State::INVALID) == "invalid");
  REQUIRE(ConfigReloadTask::state_to_string(ConfigReloadTask::State::CREATED) == "created");
  REQUIRE(ConfigReloadTask::state_to_string(ConfigReloadTask::State::IN_PROGRESS) == "in_progress");
  REQUIRE(ConfigReloadTask::state_to_string(ConfigReloadTask::State::SUCCESS) == "success");
  REQUIRE(ConfigReloadTask::state_to_string(ConfigReloadTask::State::FAIL) == "fail");
  REQUIRE(ConfigReloadTask::state_to_string(ConfigReloadTask::State::TIMEOUT) == "timeout");

  // Compile-time verification (constexpr)
  static_assert(ConfigReloadTask::state_to_string(ConfigReloadTask::State::SUCCESS) == "success");
  static_assert(ConfigReloadTask::state_to_string(ConfigReloadTask::State::FAIL) == "fail");
}
