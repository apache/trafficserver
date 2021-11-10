/** @file

  Unit Tests for Pre-Warming Pool Size Algorithm

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

#include "PreWarmAlgorithm.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

TEST_CASE("PreWarm Algorithm", "[prewarm]")
{
  SECTION("prewarm_size_v1_on_event_interval")
  {
    SECTION("{min, max} = {10, 100}")
    {
      const uint32_t min = 10;
      const uint32_t max = 100;

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 0, min, max) == min);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 0, min, max) == 20);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 0, min, max) == max);

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 5, min, max) == 5);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 5, min, max) == 15);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 5, min, max) == 95);

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 10, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 10, min, max) == 10);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 10, min, max) == 90);

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 50, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 50, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 50, min, max) == 50);
    }

    SECTION("{min, max} = {0, 0}")
    {
      const uint32_t min = 0;
      const uint32_t max = 0;

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 0, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 0, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 0, min, max) == 0);

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 5, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 5, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 5, min, max) == 0);

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 10, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 10, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 10, min, max) == 0);

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 50, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 50, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 50, min, max) == 0);
    }

    SECTION("{min, max} = {10, -1}")
    {
      const uint32_t min = 10;
      const uint32_t max = -1;

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 0, min, max) == min);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 0, min, max) == 20);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 0, min, max) == 101);

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 5, min, max) == 5);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 5, min, max) == 15);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 5, min, max) == 96);

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 10, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 10, min, max) == 10);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 10, min, max) == 91);

      CHECK(PreWarm::prewarm_size_v1_on_event_interval(0, 50, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(20, 50, min, max) == 0);
      CHECK(PreWarm::prewarm_size_v1_on_event_interval(101, 50, min, max) == 51);
    }
  }

  SECTION("prewarm_size_v4_on_event_interval")
  {
    const uint32_t min = 10;
    const uint32_t max = 100;

    // same as v3
    SECTION("rate = 1.0")
    {
      const double rate = 1.0;

      SECTION("hit + miss + current_size < min ")
      {
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 0, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 0, 1, min, max, rate) == 9);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 1, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 1, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 0, 1, min, max, rate) == 9);
      }

      SECTION("min <= miss + hit + current_size")
      {
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 10, 10, min, max, rate) == 10);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 10, 100, min, max, rate) == 0);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 9, 90, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 10, 90, min, max, rate) == 10);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 11, 90, min, max, rate) == 10);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 9, 91, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 10, 91, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 11, 91, min, max, rate) == 9);
      }
    }

    SECTION("rate = 0.0")
    {
      const double rate = 0.0;

      SECTION("hit + miss + current_size < min ")
      {
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 0, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 0, 1, min, max, rate) == 9);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 1, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 1, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 0, 1, min, max, rate) == 9);
      }

      SECTION("min <= miss + hit + current_size")
      {
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 10, 10, min, max, rate) == 0);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 10, 100, min, max, rate) == 0);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 9, 90, min, max, rate) == 0);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 10, 90, min, max, rate) == 0);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 11, 90, min, max, rate) == 0);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 9, 91, min, max, rate) == 0);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 10, 91, min, max, rate) == 0);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 11, 91, min, max, rate) == 0);
      }
    }

    SECTION("rate = 0.5")
    {
      const double rate = 0.5;

      SECTION("hit + miss + current_size < min ")
      {
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 0, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 0, 1, min, max, rate) == 9);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 1, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 1, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 0, 1, min, max, rate) == 9);
      }

      SECTION("min <= miss + hit + current_size")
      {
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 10, 10, min, max, rate) == 5);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 10, 100, min, max, rate) == 0);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 9, 90, min, max, rate) == 4);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 10, 90, min, max, rate) == 5);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 11, 90, min, max, rate) == 5);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 18, 90, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 19, 90, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 20, 90, min, max, rate) == 10);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 21, 90, min, max, rate) == 10);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 22, 90, min, max, rate) == 10);
      }
    }

    SECTION("rate = 1.5")
    {
      const double rate = 1.5;

      SECTION("hit + miss + current_size < min ")
      {
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 0, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 0, 1, min, max, rate) == 9);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 1, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 1, 1, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 0, 1, min, max, rate) == 9);
      }

      SECTION("min <= miss + hit + current_size")
      {
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(0, 10, 10, min, max, rate) == 15);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 10, 100, min, max, rate) == 0);

        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 5, 90, min, max, rate) == 7);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 6, 90, min, max, rate) == 9);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 7, 90, min, max, rate) == 10);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 8, 90, min, max, rate) == 10);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 9, 90, min, max, rate) == 10);
        CHECK(PreWarm::prewarm_size_v2_on_event_interval(1, 10, 90, min, max, rate) == 10);
      }
    }
  }
}
