/** @file
 *
 *  Verify SnowflakeID behavior.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "tscore/SnowflakeID.h"

#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>

#include "catch.hpp"

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::system_clock;

namespace
{

// These unions help us extract the fields in a platform-independent way,
// namely regardless of endianness. They obviously will need to be updated if
// the production versions of these structs change, but the tests will fail if
// that happens allerting the developer to maintain these.
union SnowflakeIDValue {
  uint64_t value;
  struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t sequence    : 10;
    uint64_t machine_id  : 12;
    uint64_t timestamp   : 41;
    uint64_t always_zero : 1;
#else  // big-endian
    uint64_t always_zero : 1;
    uint64_t timestamp   : 41;
    uint64_t machine_id  : 12;
    uint64_t sequence    : 10;
#endif // __BYTE_ORDER__
  };
};

union SnowflakeIDNoSequenceValue {
  uint64_t value;
  struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t machine_id  : 22;
    uint64_t timestamp   : 41;
    uint64_t always_zero : 1;
#else  // big-endian
    uint64_t always_zero : 1;
    uint64_t timestamp   : 41;
    uint64_t machine_id  : 22;
#endif // __BYTE_ORDER__
  };
};

} // anonymous namespace

TEST_CASE("SnowflakeIDUtils", "[libts][SnowflakeID]")
{
  REQUIRE(SnowflakeIDUtils::get_machine_id() == 0);

  uint64_t machine_id = 0xabc;
  SnowflakeIDUtils::set_machine_id(machine_id);
  REQUIRE(SnowflakeIDUtils::get_machine_id() == machine_id);

  SnowflakeIDUtils utils{0u};
  // base64 of 8 zero bytes == "AAAAAAAAAAA="
  constexpr std::string_view expected_base64{"AAAAAAAAAAA="};
  REQUIRE(utils.get_string() == expected_base64);
  // Verify caching.
  REQUIRE(utils.get_string() == expected_base64);
}

TEST_CASE("SnowflakeID", "[libts][SnowflakeID]")
{
  constexpr uint64_t machine_id          = 0x123456789abcdef;
  constexpr uint64_t expected_machine_id = machine_id & ((1u << 12) - 1); // 12 bits for machine ID
  SnowflakeIDUtils::set_machine_id(machine_id);

  // Generate two IDs back to back. The idea is that they should be generated in
  // the same millisecond, so the sequence number should increment from 0 to 1.
  uint64_t ms_since_unix_epoch_before = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  uint64_t v1                         = SnowflakeID::get_next_value();
  uint64_t v2                         = SnowflakeID::get_next_value();
  REQUIRE(v1 != 0);
  REQUIRE(v2 != 0);

  // Use the test's union to extract fields.
  SnowflakeIDValue u1{.value = v1};
  SnowflakeIDValue u2{.value = v2};

  // If by some remote happenstance the two snowflakes were generated in
  // different milliseconds, regenerate them.
  int retry_count = 0;
  while (u1.timestamp != u2.timestamp) {
    if (retry_count++ > 10) {
      // Something is seriously wrong...don't infinite loop.
      FAIL("Failed to generate two snowflake IDs in the same millisecond.");
    }
    ms_since_unix_epoch_before = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    v1                         = SnowflakeID::get_next_value();
    v2                         = SnowflakeID::get_next_value();
    u1.value                   = v1;
    u2.value                   = v2;
  }
  REQUIRE(u1.always_zero == 0);
  REQUIRE(u2.always_zero == 0);

  // This should be true per the above loop, but for the sake of clarity, test
  // explicitly using the Catch framework.
  REQUIRE(u1.timestamp == u2.timestamp);
  uint64_t const ms_since_unix_epoch_after = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

  // Make sure our snowflake IDs are offset from our designated epoch of January
  // 1, 2025.
  auto compute_epoch_ms = []() -> uint64_t {
    std::tm tm{};
    tm.tm_year = 2025 - 1900; // years since 1900
    tm.tm_mon  = 0;           // January == 0
    tm.tm_mday = 1;
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst                        = 0; // no daylight‐saving
    return static_cast<uint64_t>(timegm(&tm)) * 1000;
  };
  uint64_t const ats_epoch = compute_epoch_ms();
  // Verify that we set EPOCH to the corret hard-coded value.
  REQUIRE(ats_epoch == SnowflakeIDUtils::EPOCH);
  // Sanity check that I'm thinking about the values correctly before I subtract
  // one value from another.
  assert(ms_since_unix_epoch_before > ats_epoch);
  uint64_t const adjusted_ms_since_unix_epoch_before_for_ats_epoch = ms_since_unix_epoch_before - ats_epoch;

  // delta_ms is almost certainly 0, but I don't want the test to fail rarely
  // when we, by happenstance, grab the current time locally at a different
  // millisecond than SnowflakeID::get_next_value().
  uint64_t const delta_ms                = ms_since_unix_epoch_after - ms_since_unix_epoch_before;
  uint64_t const expected_timestamp_low  = adjusted_ms_since_unix_epoch_before_for_ats_epoch;
  uint64_t const expected_timestamp_high = expected_timestamp_low + delta_ms;
  REQUIRE(((expected_timestamp_low <= u1.timestamp) && (u1.timestamp <= expected_timestamp_high)));

  // The machine ID of both should be the expected value.
  REQUIRE(u1.machine_id == expected_machine_id);
  REQUIRE(u2.machine_id == expected_machine_id);

  // Each successive ID should be greater than the previous one.
  REQUIRE(v2 > v1);

  // Verify that the sequence number increased from 0 to 1.
  REQUIRE(u1.sequence == 0u);
  REQUIRE(u2.sequence == 1u);

  // Verify behavior when the timestamp increases.
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  uint64_t v3 = SnowflakeID::get_next_value();

  // Verify that the machine ID is still the same.
  SnowflakeIDValue u3{.value = v3};
  REQUIRE(u3.always_zero == 0);
  REQUIRE(u3.machine_id == expected_machine_id);

  // Since over a millisecond has passed.
  REQUIRE(u3.timestamp > u1.timestamp);

  // Each successive ID should be greater than the previous one, even across
  // milliseconds.
  REQUIRE(v3 > v2);

  // Verify that the sequence number is reset to 0.
  REQUIRE(u3.sequence == 0u);

  // Sanity check getting a string representation.
  SnowflakeID obj;
  auto        s1 = obj.get_string();
  REQUIRE(!s1.empty());
}

TEST_CASE("SnowflakeIdNoSequence", "[libts][SnowflakeID]")
{
  constexpr uint64_t machine_id          = 0x123456789abcdef;
  constexpr uint64_t expected_machine_id = machine_id & ((1u << 22) - 1); // 22 bits for machine ID
  SnowflakeIDUtils::set_machine_id(machine_id);

  SnowflakeIdNoSequence obj;
  uint64_t              v1 = obj.get_value();

  // Use the test's union to extract fields.
  SnowflakeIDNoSequenceValue u1{.value = v1};
  REQUIRE(u1.always_zero == 0);
  REQUIRE(u1.machine_id == expected_machine_id);

  // Sleep a bit to ensure the next ID is generated in a different millisecond.
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  uint64_t v2 = SnowflakeIdNoSequence().get_value();

  // Successive IDs should be greater than the previous one.
  REQUIRE(v2 > v1);

  // Use the test's union to extract fields for the second value.
  SnowflakeIDNoSequenceValue u2{.value = v2};
  REQUIRE(u2.always_zero == 0);
  REQUIRE(u2.machine_id == expected_machine_id);

  // Verify that the timestamp is different.
  REQUIRE(u2.timestamp > u1.timestamp);

  // Sanity check getting a string representation.
  auto s = obj.get_string();
  REQUIRE(!s.empty());
}
