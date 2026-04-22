/** @file

  Unit tests for HostDBInfo state transitions (UP / DOWN / SUSPECT).

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

#include "iocore/hostdb/HostDBProcessor.h"
#include "tscore/ink_time.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// fail_window used throughout the tests
static const ts_seconds FAIL_WINDOW{300};

static const ts_time T0 = ts_clock::now(); ///< A fixed anchor in time;

static const ts_time T1 = T0 + ts_seconds(1);               ///< A time within FAIL_WINDOW from T0
static const ts_time T2 = T0 + FAIL_WINDOW + ts_seconds(1); ///< A time past FAIL_WINDOW from T0

static const ts_time T3 = T2 + ts_seconds(1);               ///< A time within FAIL_WINDOW from T2
static const ts_time T4 = T2 + FAIL_WINDOW + ts_seconds(1); ///< A time past FAIL_WINDOW from T2
// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("HostDBInfo state", "[hostdb]")
{
  SECTION("initial state is UP")
  {
    HostDBInfo info;
    REQUIRE(info.state(T0, FAIL_WINDOW) == HostDBInfo::State::UP);
    REQUIRE(info.is_up());
    REQUIRE(info.last_fail_time() == TS_TIME_ZERO);
    REQUIRE(info.fail_count() == 0);
  }

  SECTION("UP -> DOWN via mark_down; fail_count is reset")
  {
    HostDBInfo info;
    REQUIRE(info.mark_down(T0, FAIL_WINDOW) == true);
    REQUIRE(info.last_fail_time() == T0);
    REQUIRE(info.fail_count() == 0); // reset so the next SUSPECT window starts fresh
    REQUIRE(info.state(T1, FAIL_WINDOW) == HostDBInfo::State::DOWN);
  }

  SECTION("mark_down on DOWN server returns false and does not refresh fail window")
  {
    HostDBInfo info;
    info.mark_down(T0, FAIL_WINDOW);
    REQUIRE(info.mark_down(T1, FAIL_WINDOW) == false);
    REQUIRE(info.last_fail_time() == T0); // unchanged
  }

  SECTION("DOWN -> SUSPECT when fail window elapses (time-based, no explicit call)")
  {
    HostDBInfo info;
    info.mark_down(T0, FAIL_WINDOW);
    REQUIRE(info.state(T2, FAIL_WINDOW) == HostDBInfo::State::SUSPECT);
    REQUIRE(info.is_suspect(T2, FAIL_WINDOW));
    REQUIRE(info.last_fail_time() == T0); // _last_failure is unchanged
  }

  SECTION("SUSPECT -> DOWN via mark_down; fail_count is reset; fail window restarts")
  {
    HostDBInfo info;
    info.mark_down(T0, FAIL_WINDOW);

    REQUIRE(info.mark_down(T2, FAIL_WINDOW) == true);
    REQUIRE(info.last_fail_time() == T2); // refreshed to the new failure time
    REQUIRE(info.fail_count() == 0);      // reset so the next SUSPECT window starts fresh
    // server is DOWN again with a fresh window anchored at T2
    REQUIRE(info.state(T2 + FAIL_WINDOW / 2, FAIL_WINDOW) == HostDBInfo::State::DOWN);
  }

  SECTION("SUSPECT -> UP via mark_up; returns true (was DOWN/SUSPECT)")
  {
    HostDBInfo info;
    info.mark_down(T0, FAIL_WINDOW);
    REQUIRE(info.mark_up() == true);
    REQUIRE(info.state(T0, FAIL_WINDOW) == HostDBInfo::State::UP);
    REQUIRE(info.last_fail_time() == TS_TIME_ZERO);
    REQUIRE(info.fail_count() == 0);
  }

  SECTION("mark_up on already-UP server returns false")
  {
    HostDBInfo info;
    REQUIRE(info.mark_up() == false);
  }

  SECTION("UP -> DOWN via increment_fail_count with max_retries=1")
  {
    HostDBInfo info;
    auto [marked, count] = info.increment_fail_count(T0, 1, FAIL_WINDOW);
    REQUIRE(marked == true);
    REQUIRE(count == 1);
    REQUIRE(info.state(T1, FAIL_WINDOW) == HostDBInfo::State::DOWN);
    REQUIRE(info.fail_count() == 0); // reset by mark_down
  }

  SECTION("UP -> DOWN via increment_fail_count with max_retries=3 requires 3 failures")
  {
    HostDBInfo info;
    {
      auto [marked, count] = info.increment_fail_count(T0, 3, FAIL_WINDOW);
      REQUIRE(marked == false);
      REQUIRE(count == 1);
    }
    {
      auto [marked, count] = info.increment_fail_count(T0, 3, FAIL_WINDOW);
      REQUIRE(marked == false);
      REQUIRE(count == 2);
    }
    {
      auto [marked, count] = info.increment_fail_count(T0, 3, FAIL_WINDOW);
      REQUIRE(marked == true);
      REQUIRE(count == 3);
    }
    REQUIRE(info.state(T1, FAIL_WINDOW) == HostDBInfo::State::DOWN);
    REQUIRE(info.fail_count() == 0); // reset by mark_down
  }

  SECTION("SUSPECT -> DOWN via increment_fail_count; fail_count starts from zero each SUSPECT window")
  {
    HostDBInfo info;
    // Drive UP -> DOWN (R=1); fail_count is reset to 0 after mark_down.
    info.increment_fail_count(T0, 1, FAIL_WINDOW);
    REQUIRE(info.fail_count() == 0);

    // D=3: need exactly 3 fresh failures in the SUSPECT window.
    {
      auto [marked, count] = info.increment_fail_count(T2, 3, FAIL_WINDOW);
      REQUIRE(marked == false);
      REQUIRE(count == 1);
    }
    {
      auto [marked, count] = info.increment_fail_count(T2, 3, FAIL_WINDOW);
      REQUIRE(marked == false);
      REQUIRE(count == 2);
    }
    {
      auto [marked, count] = info.increment_fail_count(T2, 3, FAIL_WINDOW);
      REQUIRE(marked == true);
      REQUIRE(count == 3);
    }
    REQUIRE(info.last_fail_time() == T2);
    REQUIRE(info.fail_count() == 0); // reset by mark_down SUSPECT->DOWN
  }

  SECTION("full cycle: UP -> DOWN -> SUSPECT -> DOWN -> SUSPECT -> UP")
  {
    HostDBInfo info;

    // UP -> DOWN
    info.increment_fail_count(T0, 1, FAIL_WINDOW);
    REQUIRE(info.state(T1, FAIL_WINDOW) == HostDBInfo::State::DOWN);
    REQUIRE(info.fail_count() == 0);

    // DOWN -> SUSPECT
    REQUIRE(info.state(T2, FAIL_WINDOW) == HostDBInfo::State::SUSPECT);

    // SUSPECT -> DOWN (fail window restarts from T2)
    info.increment_fail_count(T2, 1, FAIL_WINDOW);
    REQUIRE(info.last_fail_time() == T2);
    REQUIRE(info.state(T3, FAIL_WINDOW) == HostDBInfo::State::DOWN);
    REQUIRE(info.fail_count() == 0);

    // DOWN -> SUSPECT again
    REQUIRE(info.state(T4, FAIL_WINDOW) == HostDBInfo::State::SUSPECT);

    // SUSPECT -> UP
    REQUIRE(info.mark_up() == true);
    REQUIRE(info.state(T4, FAIL_WINDOW) == HostDBInfo::State::UP);
    REQUIRE(info.fail_count() == 0);
  }

  SECTION("partial failures in SUSPECT then success resets to UP cleanly")
  {
    HostDBInfo info;
    info.increment_fail_count(T0, 1, FAIL_WINDOW);

    // Two failures below the D=3 threshold
    info.increment_fail_count(T2, 3, FAIL_WINDOW);
    info.increment_fail_count(T2, 3, FAIL_WINDOW);
    REQUIRE(info.state(T2, FAIL_WINDOW) == HostDBInfo::State::SUSPECT);

    // Connection succeeds
    info.mark_up();
    REQUIRE(info.state(T2, FAIL_WINDOW) == HostDBInfo::State::UP);
    REQUIRE(info.last_fail_time() == TS_TIME_ZERO);
    REQUIRE(info.fail_count() == 0);
  }
}
