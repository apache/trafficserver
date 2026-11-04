/** @file

  Catch2 unit tests for the watchdog's backpressure arithmetic.

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

#include "iocore/eventsystem/WatchdogPolicy.h"

#include <catch2/catch_test_macros.hpp>

using namespace Watchdog;
using std::chrono::milliseconds;

namespace
{
std::chrono::steady_clock::duration
peak(int ms)
{
  return std::chrono::duration_cast<std::chrono::steady_clock::duration>(milliseconds{ms});
}

// Shrink at 100 ms, refuse at 200 ms, halve on decrease, 100 permille increase, floor of 50 permille.
// The release deadband is therefore 50 ms for shrink and 100 ms for refuse.
PolicyConfig
backpressure_config()
{
  PolicyConfig config;
  config.backpressure      = true;
  config.shrink_timeout    = milliseconds{100};
  config.refuse_timeout    = milliseconds{200};
  config.decrease_permille = 500;
  config.increase_permille = 100;
  config.min_permille      = 50;
  return config;
}
} // namespace

TEST_CASE("release_threshold", "[watchdog]")
{
  SECTION("scales the threshold by the retain knob")
  {
    REQUIRE(release_threshold(milliseconds{1000}, 900) == std::chrono::nanoseconds{milliseconds{900}});
    REQUIRE(release_threshold(milliseconds{1000}, 667) == std::chrono::nanoseconds{milliseconds{667}});
  }

  SECTION("never drops below half the threshold")
  {
    REQUIRE(release_threshold(milliseconds{1000}, 500) == std::chrono::nanoseconds{milliseconds{500}});
    REQUIRE(release_threshold(milliseconds{1000}, 100) == std::chrono::nanoseconds{milliseconds{500}});
    REQUIRE(release_threshold(milliseconds{1000}, 1) == std::chrono::nanoseconds{milliseconds{500}});
  }

  SECTION("keeps a fractional millisecond boundary on a small timeout")
  {
    REQUIRE(release_threshold(milliseconds{3}, 500) == std::chrono::nanoseconds{1'500'000});
  }
}

TEST_CASE("recovery_step_permille", "[watchdog]")
{
  SECTION("is proportional to the depth in force, capped at the configured rate")
  {
    REQUIRE(recovery_step_permille({.scale = 1000}, 100) == 100);
    REQUIRE(recovery_step_permille({.scale = 500}, 100) == 50);
    REQUIRE(recovery_step_permille({.scale = 100}, 100) == 10);
  }

  SECTION("stays positive where the proportional step would truncate to zero")
  {
    REQUIRE(recovery_step_permille({.scale = 5}, 100) == 1);
    REQUIRE(recovery_step_permille({.scale = 1}, 1) == 1);
  }

  SECTION("is attenuated by the headroom left to the trouble mark")
  {
    // Step 10, nine tenths of the way still to go.
    REQUIRE(recovery_step_permille({.scale = 100, .trouble_scale = 1000}, 100) == 9);
  }

  SECTION("lands exactly on the mark once it is within one step")
  {
    REQUIRE(recovery_step_permille({.scale = 990, .trouble_scale = 1000}, 100) == 10);
    REQUIRE(recovery_step_permille({.scale = 999, .trouble_scale = 1000}, 100) == 1);
  }

  SECTION("is unattenuated past the mark")
  {
    REQUIRE(recovery_step_permille({.scale = 600, .trouble_scale = 500}, 100) == 60);
  }

  SECTION("is unattenuated with no mark")
  {
    REQUIRE(recovery_step_permille({.scale = 600, .trouble_scale = 0}, 100) == 60);
  }
}

TEST_CASE("advance with backpressure off", "[watchdog]")
{
  PolicyConfig config = backpressure_config();
  config.backpressure = false;

  SECTION("nothing engages however high the peak")
  {
    auto result = advance(config, {}, peak(10'000));
    REQUIRE(result.state == PolicyState{});
  }

  SECTION("the thresholds are still counted")
  {
    auto result = advance(config, {}, peak(10'000));
    REQUIRE(result.saturated);
    REQUIRE(result.refuse_exceeded);

    result = advance(config, {}, peak(99));
    REQUIRE_FALSE(result.saturated);
    REQUIRE_FALSE(result.refuse_exceeded);
  }

  SECTION("turning the mode off releases everything at once, without waiting out the deadband")
  {
    PolicyState engaged{.shrink_engaged = true, .refuse = true, .scale = 125, .trouble_scale = 500};

    // 75 ms is inside both deadbands, where backpressure mode would have held.
    auto result = advance(config, engaged, peak(75));
    REQUIRE(result.state == PolicyState{});
  }
}

TEST_CASE("advance shrink", "[watchdog]")
{
  PolicyConfig const config = backpressure_config();

  SECTION("engages on the first saturated scan and records the depth that caused the trouble")
  {
    auto result = advance(config, {}, peak(100));
    REQUIRE(result.saturated);
    REQUIRE(result.state.shrink_engaged);
    REQUIRE(result.state.trouble_scale == 1000);
    REQUIRE(result.state.scale == 500);
  }

  SECTION("a peak just under the threshold does not engage")
  {
    auto result = advance(config, {}, peak(99));
    REQUIRE_FALSE(result.state.shrink_engaged);
    REQUIRE(result.state.scale == 1000);
  }

  SECTION("keeps decreasing while saturated, down to the floor and no further")
  {
    PolicyState state;
    for (int expected : {500, 250, 125, 62, 50, 50}) {
      state = advance(config, state, peak(500)).state;
      REQUIRE(state.scale == expected);
    }
    REQUIRE(state.trouble_scale == 1000);
  }

  SECTION("always gives up at least one point while shrinking")
  {
    PolicyConfig slow      = config;
    slow.decrease_permille = 999;
    slow.min_permille      = 1;

    REQUIRE(advance(slow, {.scale = 3}, peak(500)).state.scale == 2);
    REQUIRE(advance(slow, {.scale = 2}, peak(500)).state.scale == 1);
  }

  SECTION("the mark is not overwritten by later saturated scans of the same episode")
  {
    auto state = advance(config, {}, peak(500)).state;
    state      = advance(config, state, peak(500)).state;
    REQUIRE(state.scale == 250);
    REQUIRE(state.trouble_scale == 1000);
  }

  SECTION("holds inside the deadband, neither shedding further nor recovering")
  {
    PolicyState held{.shrink_engaged = true, .scale = 250, .trouble_scale = 1000};

    // Under the 100 ms threshold, over the 50 ms release point.
    auto result = advance(config, held, peak(75));
    REQUIRE_FALSE(result.saturated);
    REQUIRE(result.state == held);
  }

  SECTION("releases at the deadband edge and starts climbing")
  {
    PolicyState held{.shrink_engaged = true, .scale = 250, .trouble_scale = 1000};

    auto result = advance(config, held, peak(50));
    REQUIRE_FALSE(result.state.shrink_engaged);
    REQUIRE(result.state.scale > 250);
    REQUIRE(result.state.trouble_scale == 1000);
  }

  SECTION("a peak a millisecond over the deadband edge does not release")
  {
    PolicyState held{.shrink_engaged = true, .scale = 250, .trouble_scale = 1000};
    REQUIRE(advance(config, held, peak(51)).state == held);
  }

  SECTION("a long stay in the deadband does not squeeze the queue toward the floor")
  {
    PolicyState state{.shrink_engaged = true, .scale = 250, .trouble_scale = 1000};
    for (int i = 0; i < 50; ++i) {
      state = advance(config, state, peak(75)).state;
    }
    REQUIRE(state.scale == 250);
  }
}

TEST_CASE("advance recovery", "[watchdog]")
{
  PolicyConfig const config = backpressure_config();

  SECTION("climbs back to full depth and then forgets the mark")
  {
    PolicyState state{.shrink_engaged = true, .scale = 50, .trouble_scale = 1000};
    int         scans = 0;

    int previous = state.scale;
    while (state.scale < 1000) {
      state = advance(config, state, peak(0)).state;
      REQUIRE(state.scale > previous);
      previous = state.scale;
      REQUIRE(++scans < 1000);
    }

    REQUIRE(state.scale == 1000);
    REQUIRE(state.trouble_scale == 0);
    REQUIRE_FALSE(state.shrink_engaged);
  }

  SECTION("never overshoots 1000")
  {
    auto result = advance(config, {.scale = 995}, peak(0));
    REQUIRE(result.state.scale == 1000);
  }

  SECTION("decelerates toward the mark and then lands on it")
  {
    PolicyState state{.scale = 100, .trouble_scale = 400};
    int         steps_to_mark = 0;

    while (state.scale < 400) {
      auto next = advance(config, state, peak(0)).state;
      REQUIRE(next.scale <= 400);
      state = next;
      REQUIRE(++steps_to_mark < 1000);
    }
    REQUIRE(state.scale == 400);
  }

  SECTION("a later episode is measured against its own depth, not a stale mark")
  {
    PolicyState state{.scale = 1000};
    state = advance(config, state, peak(500)).state;
    REQUIRE(state.trouble_scale == 1000);
    state = advance(config, state, peak(500)).state;
    REQUIRE(state.scale == 250);

    // Recover all the way, then shrink again from full depth.
    while (state.scale < 1000) {
      state = advance(config, state, peak(0)).state;
    }
    REQUIRE(state.trouble_scale == 0);

    state = advance(config, state, peak(500)).state;
    REQUIRE(state.trouble_scale == 1000);
  }
}

TEST_CASE("advance refuse", "[watchdog]")
{
  PolicyConfig const config = backpressure_config();

  SECTION("engages at its own threshold, above the shrink threshold")
  {
    auto result = advance(config, {}, peak(199));
    REQUIRE_FALSE(result.refuse_exceeded);
    REQUIRE_FALSE(result.state.refuse);
    REQUIRE(result.state.shrink_engaged);

    result = advance(config, {}, peak(200));
    REQUIRE(result.refuse_exceeded);
    REQUIRE(result.state.refuse);
  }

  SECTION("holds inside the deadband")
  {
    PolicyState refusing{.shrink_engaged = true, .refuse = true, .scale = 250, .trouble_scale = 1000};

    // Under the 200 ms threshold, over the 100 ms release point.
    auto result = advance(config, refusing, peak(150));
    REQUIRE_FALSE(result.refuse_exceeded);
    REQUIRE(result.state.refuse);
  }

  SECTION("releases at the deadband edge")
  {
    PolicyState refusing{.shrink_engaged = true, .refuse = true, .scale = 250, .trouble_scale = 1000};

    REQUIRE(advance(config, refusing, peak(101)).state.refuse);
    REQUIRE_FALSE(advance(config, refusing, peak(100)).state.refuse);
  }

  SECTION("is independent of shrink in both directions")
  {
    // Refuse alone, because shrink's threshold is higher.
    PolicyConfig refuse_first   = config;
    refuse_first.shrink_timeout = milliseconds{1000};
    refuse_first.refuse_timeout = milliseconds{100};

    auto result = advance(refuse_first, {}, peak(150));
    REQUIRE(result.state.refuse);
    REQUIRE_FALSE(result.state.shrink_engaged);
    REQUIRE(result.state.scale == 1000);

    // Shrink releasing does not release refuse that is still held: shrink's release point is 500 ms here and
    // refuse's is 50 ms, so a 300 ms peak releases the first and not the second.
    PolicyState both{.shrink_engaged = true, .refuse = true, .scale = 250, .trouble_scale = 1000};
    result = advance(refuse_first, both, peak(300));
    REQUIRE(result.state.refuse);
    REQUIRE_FALSE(result.state.shrink_engaged);
  }

  SECTION("a zero refuse timeout disables it and counts nothing")
  {
    PolicyConfig disabled   = config;
    disabled.refuse_timeout = milliseconds{0};

    auto result = advance(disabled, {.refuse = true}, peak(10'000));
    REQUIRE_FALSE(result.state.refuse);
    REQUIRE_FALSE(result.refuse_exceeded);
  }

  SECTION("clearing the refuse timeout while refusing releases at once")
  {
    PolicyConfig disabled   = config;
    disabled.refuse_timeout = milliseconds{0};

    PolicyState refusing{.shrink_engaged = true, .refuse = true, .scale = 250, .trouble_scale = 1000};
    REQUIRE_FALSE(advance(disabled, refusing, peak(150)).state.refuse);
  }
}
