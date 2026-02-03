/** @file

  Unit tests for abuse_shield plugin ip_data token bucket and blocking logic.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements.  See the NOTICE file distributed with this work for additional information regarding
  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
  (the "License"); you may not use this file except in compliance with the License.  You may obtain
  a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
*/

#include "../ip_data.h"

#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace abuse_shield;

// ============================================================================
// Token bucket consume tests
// ============================================================================

TEST_CASE("Token bucket consume_token", "[abuse_shield][token_bucket]")
{
  SECTION("first event initializes to burst_limit minus one")
  {
    std::atomic<int32_t>  tokens{0};
    std::atomic<uint64_t> last_update{0};

    int result = consume_token(tokens, last_update, 100, 100);

    // First event: starts at burst_limit (100), then consumes 1 = 99
    REQUIRE(result == 99);
    REQUIRE(tokens.load() == 99);
    REQUIRE(last_update.load() > 0);
  }

  SECTION("subsequent consume decrements tokens")
  {
    std::atomic<int32_t>  tokens{50};
    std::atomic<uint64_t> last_update{now_ms()};

    int result = consume_token(tokens, last_update, 100, 100);

    // With no time elapsed, replenish is 0, so 50 - 1 = 49
    REQUIRE(result == 49);
    REQUIRE(tokens.load() == 49);
  }

  SECTION("tokens can go negative when rate exceeded")
  {
    std::atomic<int32_t>  tokens{0};
    std::atomic<uint64_t> last_update{now_ms()};

    int result = consume_token(tokens, last_update, 100, 100);

    // 0 - 1 = -1 (no time for replenishment)
    REQUIRE(result == -1);
    REQUIRE(tokens.load() == -1);
  }

  SECTION("tokens replenish over time")
  {
    std::atomic<int32_t>  tokens{0};
    std::atomic<uint64_t> last_update{now_ms() - 100}; // 100ms ago

    // With rate=100/sec, 100ms should replenish 10 tokens
    int result = consume_token(tokens, last_update, 100, 100);

    // 0 + 10 (replenish) - 1 (consume) = 9
    REQUIRE(result == 9);
    REQUIRE(tokens.load() == 9);
  }

  SECTION("tokens cap at burst_limit")
  {
    std::atomic<int32_t>  tokens{50};
    std::atomic<uint64_t> last_update{now_ms() - 10000}; // 10 seconds ago

    // Even with lots of time elapsed, tokens cap at burst_limit
    int result = consume_token(tokens, last_update, 100, 100);

    // Would be 50 + 1000, but capped at 100, then -1 = 99
    REQUIRE(result == 99);
    REQUIRE(tokens.load() == 99);
  }

  SECTION("negative tokens can recover with time")
  {
    std::atomic<int32_t>  tokens{-50};
    std::atomic<uint64_t> last_update{now_ms() - 1000}; // 1 second ago

    // With rate=100/sec, 1s should replenish 100 tokens
    int result = consume_token(tokens, last_update, 100, 100);

    // -50 + 100 (replenish) - 1 (consume) = 49
    REQUIRE(result == 49);
    REQUIRE(tokens.load() == 49);
  }
}

// ============================================================================
// TxnData tests
// ============================================================================

TEST_CASE("TxnData operations", "[abuse_shield][TxnData]")
{
  TxnData data;

  SECTION("initial state")
  {
    REQUIRE(data.tokens.load() == 0);
    REQUIRE(data.count.load() == 0);
    REQUIRE(data.blocked_until.load() == 0);
    REQUIRE(data.is_blocked() == false);
  }

  SECTION("consume increments count and returns token count")
  {
    int result = data.consume(100, 100);

    REQUIRE(result == 99); // First event: burst_limit - 1
    REQUIRE(data.count.load() == 1);
  }

  SECTION("multiple consumes decrement tokens")
  {
    data.consume(10, 10); // First: 10 - 1 = 9
    data.consume(10, 10); // 9 - 1 = 8
    data.consume(10, 10); // 8 - 1 = 7

    REQUIRE(data.count.load() == 3);
    REQUIRE(data.tokens.load() <= 9); // Some decrement happened
  }

  SECTION("is_blocked returns false when blocked_until is 0")
  {
    REQUIRE(data.is_blocked() == false);
  }

  SECTION("is_blocked returns true when blocked_until > now")
  {
    data.block_until(now_ms() + 10000); // Block for 10 seconds
    REQUIRE(data.is_blocked() == true);
  }

  SECTION("is_blocked returns false after block expires")
  {
    data.block_until(now_ms() - 1); // Block expired 1ms ago
    REQUIRE(data.is_blocked() == false);
  }

  SECTION("block_until sets blocked_until timestamp")
  {
    uint64_t until = now_ms() + 5000;
    data.block_until(until);
    REQUIRE(data.blocked_until.load() == until);
  }
}

// ============================================================================
// ConnData tests
// ============================================================================

TEST_CASE("ConnData operations", "[abuse_shield][ConnData]")
{
  ConnData data;

  SECTION("initial state")
  {
    REQUIRE(data.tokens.load() == 0);
    REQUIRE(data.count.load() == 0);
    REQUIRE(data.blocked_until.load() == 0);
    REQUIRE(data.is_blocked() == false);
  }

  SECTION("consume increments count")
  {
    data.consume(10, 10);
    REQUIRE(data.count.load() == 1);
  }

  SECTION("blocking works")
  {
    REQUIRE(data.is_blocked() == false);
    data.block_until(now_ms() + 10000);
    REQUIRE(data.is_blocked() == true);
  }
}

// ============================================================================
// H2Data tests
// ============================================================================

TEST_CASE("H2Data operations", "[abuse_shield][H2Data]")
{
  H2Data data;

  SECTION("initial state")
  {
    REQUIRE(data.tokens.load() == 0);
    REQUIRE(data.count.load() == 0);
    REQUIRE(data.blocked_until.load() == 0);
    REQUIRE(data.is_blocked() == false);

    // All error codes should be 0
    for (size_t i = 0; i < NUM_H2_ERROR_CODES; ++i) {
      REQUIRE(data.error_codes[i].load() == 0);
    }
  }

  SECTION("consume tracks per-error-code counts")
  {
    data.consume(10, 10, 1); // PROTOCOL_ERROR
    data.consume(10, 10, 1); // PROTOCOL_ERROR
    data.consume(10, 10, 8); // CANCEL (Rapid Reset)
    data.consume(10, 10, 9); // COMPRESSION_ERROR

    REQUIRE(data.count.load() == 4);
    REQUIRE(data.error_codes[1].load() == 2); // PROTOCOL_ERROR
    REQUIRE(data.error_codes[8].load() == 1); // CANCEL
    REQUIRE(data.error_codes[9].load() == 1); // COMPRESSION_ERROR
    REQUIRE(data.error_codes[0].load() == 0); // NO_ERROR - not incremented
  }

  SECTION("error_codes array bounds checking")
  {
    // Error code >= NUM_H2_ERROR_CODES should not crash
    data.consume(10, 10, 255); // Out of bounds - should be ignored
    data.consume(10, 10, 16);  // Just out of bounds

    REQUIRE(data.count.load() == 2);
    // Should not have modified any error_codes entry unsafely
  }

  SECTION("blocking works")
  {
    REQUIRE(data.is_blocked() == false);
    data.block_until(now_ms() + 10000);
    REQUIRE(data.is_blocked() == true);
  }
}

// ============================================================================
// now_ms tests
// ============================================================================

TEST_CASE("now_ms function", "[abuse_shield][time]")
{
  SECTION("returns non-zero value")
  {
    uint64_t now = now_ms();
    REQUIRE(now > 0);
  }

  SECTION("is monotonically increasing")
  {
    uint64_t t1 = now_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint64_t t2 = now_ms();

    REQUIRE(t2 > t1);
  }
}
