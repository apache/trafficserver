/** @file

    Unit tests for Http2FrequencyCounter

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
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "Http2FrequencyCounter.h"

class TestHttp2FrequencyCounter : public Http2FrequencyCounter
{
public:
  void
  set_internal_state(ink_hrtime last_update_sec, uint16_t count_0, uint16_t count_1)
  {
    this->_last_update = last_update_sec;
    this->_count[0]    = count_0;
    this->_count[1]    = count_1;
  }
};

TEST_CASE("Http2FrequencyCounter_basic", "[http2][Http2FrequencyCounter]")
{
  TestHttp2FrequencyCounter counter;

  SECTION("basic")
  {
    REQUIRE(counter.get_count() == 0);
    counter.increment();
    REQUIRE(counter.get_count() == 1);
    counter.increment(2);
    REQUIRE(counter.get_count() == 3);

    counter.set_internal_state(ink_hrtime_to_sec(Thread::get_hrtime()) - 10, 1, 2);
    REQUIRE(counter.get_count() == 3);
  }

  SECTION("Update at 0")
  {
    ink_hrtime now = ink_hrtime_to_sec(Thread::get_hrtime_updated());
    while (now % 60 != 0) {
      sleep(1);
      now = ink_hrtime_to_sec(Thread::get_hrtime_updated());
    }

    counter.set_internal_state(now - 5, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 3);

    counter.set_internal_state(now - 10, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 3);

    counter.set_internal_state(now - 20, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 3);

    counter.set_internal_state(now - 30, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 3);

    counter.set_internal_state(now - 40, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);

    counter.set_internal_state(now - 50, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);

    counter.set_internal_state(now - 60, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);

    counter.set_internal_state(now - 70, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);
  }

  SECTION("Update at 10")
  {
    ink_hrtime now = ink_hrtime_to_sec(Thread::get_hrtime_updated());
    while (now % 60 != 10) {
      sleep(1);
      now = ink_hrtime_to_sec(Thread::get_hrtime_updated());
    }

    counter.set_internal_state(now - 5, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 4);

    counter.set_internal_state(now - 10, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 4);

    counter.set_internal_state(now - 20, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 3);

    counter.set_internal_state(now - 30, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 3);

    counter.set_internal_state(now - 40, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 3);

    counter.set_internal_state(now - 50, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);

    counter.set_internal_state(now - 60, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);

    counter.set_internal_state(now - 70, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);
  }

  SECTION("Update at 30")
  {
    ink_hrtime now = ink_hrtime_to_sec(Thread::get_hrtime_updated());
    while (now % 60 != 30) {
      sleep(1);
      now = ink_hrtime_to_sec(Thread::get_hrtime_updated());
    }

    counter.set_internal_state(now - 5, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 2);

    counter.set_internal_state(now - 10, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 2);

    counter.set_internal_state(now - 20, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 2);

    counter.set_internal_state(now - 30, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 2);

    counter.set_internal_state(now - 40, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);

    counter.set_internal_state(now - 50, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);

    counter.set_internal_state(now - 60, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);

    counter.set_internal_state(now - 70, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);
  }

  SECTION("Update at 40")
  {
    ink_hrtime now = ink_hrtime_to_sec(Thread::get_hrtime_updated());
    while (now % 60 != 40) {
      sleep(1);
      now = ink_hrtime_to_sec(Thread::get_hrtime_updated());
    }

    counter.set_internal_state(now - 5, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 4);

    counter.set_internal_state(now - 10, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 4);

    counter.set_internal_state(now - 20, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 2);

    counter.set_internal_state(now - 30, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 2);

    counter.set_internal_state(now - 40, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 2);

    counter.set_internal_state(now - 50, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);

    counter.set_internal_state(now - 60, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);

    counter.set_internal_state(now - 70, 1, 2);
    counter.increment();
    CHECK(counter.get_count() == 1);
  }
}
