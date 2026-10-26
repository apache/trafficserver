/** @file

  Unit tests for the ConfigReloadProgress checker's ownership of its reload task, and
  for ConfigContext behavior that needs a live reload

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
#include "mgmt/config/ConfigRegistry.h"
#include "mgmt/config/ConfigReloadTrace.h"
#include "mgmt/config/ReloadCoordinator.h"
#include "records/RecCore.h"
#include "records/RecordsConfig.h"

#include <yaml-cpp/yaml.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
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
wait_for_owners_below(ConfigReloadTaskPtr const &task, long owners, std::chrono::seconds timeout)
{
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (task.use_count() >= owners) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
  }
  return true;
}

bool
wait_for_sole_owner(ConfigReloadTaskPtr const &task, std::chrono::seconds timeout)
{
  return wait_for_owners_below(task, 2, timeout);
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

// execute_reload() gives a handler a context with a task only while a reload is in
// progress, and starting one needs ET_TASK for its progress checker, so this case
// lives in this binary.
TEST_CASE("A dependent context inherits its parent's RPC content", "[config][reload][context]")
{
  static std::optional<ConfigContext> parent;
  static std::optional<ConfigContext> child;

  std::string token;
  REQUIRE(ReloadCoordinator::Get_Instance().prepare_reload(token, "test-dep-").is_ok());
  auto const main_task = ReloadCoordinator::Get_Instance().get_current_task();
  REQUIRE(main_task);
  // prepare_reload() started a progress checker, which holds one of these.
  auto const owners_with_checker = main_task.use_count();

  auto &reg = config::ConfigRegistry::Get_Instance();
  reg.register_config("test_dependent_payload", "", "",
                      [](ConfigContext ctx) {
                        parent.emplace(ctx);
                        child.emplace(ctx.add_dependent_ctx("dependent"));
                        child->complete();
                        ctx.complete();
                      },
                      config::ConfigSource::FileAndRpc, {});
  reg.set_passed_config("test_dependent_payload", YAML::Load("{_reload: {id: foo}, data: x}"));
  reg.execute_reload("test_dependent_payload");

  REQUIRE(parent.has_value());
  REQUIRE(child.has_value());
  REQUIRE(static_cast<bool>(*parent));
  REQUIRE(static_cast<bool>(*child));

  YAML::Node const content = child->supplied_yaml();
  REQUIRE(content);
  REQUIRE(content["data"].as<std::string>() == "x");
  YAML::Node const directives = child->reload_directives();
  REQUIRE(directives);
  REQUIRE(directives["id"].as<std::string>() == "foo");
  REQUIRE(content.is(parent->supplied_yaml()));
  REQUIRE(directives.is(parent->reload_directives()));

  child.reset();
  parent.reset();

  // Like the cases above, don't leave the checker running past the test. Same bound as the
  // terminal case: the checker confirms a terminal state about 7 s after it starts.
  REQUIRE(wait_for_owners_below(main_task, owners_with_checker, std::chrono::seconds{15}));
}
