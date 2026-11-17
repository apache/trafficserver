/**
  @file Test for Diags.cc

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

#include "tscore/BaseLogFile.h"
#include "tscore/Diags.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

// traffic_server rolls diags.log from a periodic task thread and reopens it
// from the SIGUSR2 handler on a net thread, so the two must be safe to run at
// the same time on one Diags instance.
TEST_CASE("Diags rolls and reseats diags.log from two threads", "[Diags]")
{
  char tmpl[] = "/tmp/test_Diags.XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  std::filesystem::path const dir{tmpl};
  std::string const           path = (dir / "diags.log").string();

  {
    Diags diags("test_Diags", nullptr, nullptr, new BaseLogFile(path.c_str()));
    REQUIRE(diags.diags_log != nullptr);

    // A zero size threshold is met by every check, so each call rolls.
    diags.config_roll_diagslog(RollingEnabledValues::ROLL_ON_SIZE, -1, 0);

    constexpr int ITERATIONS = 500;
    std::thread   roller([&diags]() {
      for (int i = 0; i < ITERATIONS; ++i) {
        diags.should_roll_diagslog();
      }
    });
    std::thread   reseater([&diags]() {
      for (int i = 0; i < ITERATIONS; ++i) {
        diags.reseat_diagslog();
      }
    });
    roller.join();
    reseater.join();

    REQUIRE(diags.diags_log != nullptr);
    CHECK(diags.diags_log->is_init());
  }

  std::filesystem::remove_all(dir);
}
