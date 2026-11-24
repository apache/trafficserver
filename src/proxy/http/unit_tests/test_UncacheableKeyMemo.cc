/** @file

  Unit tests for UncacheableKeyMemo.

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

#include "proxy/http/UncacheableKeyMemo.h"

#include <limits>
#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace
{
CryptoHash
make_key(uint64_t a, uint64_t b)
{
  CryptoHash key;
  key.u64[0] = a;
  key.u64[1] = b;
  return key;
}
} // namespace

TEST_CASE("UncacheableKeyMemo", "[http][cache]")
{
  // The table is a megabyte, too large for the stack.
  auto             memo  = std::make_unique<UncacheableKeyMemo>();
  CryptoHash const key   = make_key(0x1111, 0x2222);
  CryptoHash const other = make_key(0x3333, 0x4444);
  // Sharing u64[1] means sharing a slot; u64[0] is the tag.
  CryptoHash const neighbor = make_key(0x5555, key.u64[1]);
  ink_hrtime const now      = HRTIME_SECONDS(1000);
  ink_hrtime const ttl      = HRTIME_SECONDS(10);

  SECTION("an unknown key is not remembered")
  {
    CHECK_FALSE(memo->contains(key, now));
  }

  SECTION("a remembered key is found until it expires")
  {
    memo->remember(key, now, ttl);
    CHECK(memo->contains(key, now));
    CHECK(memo->contains(key, now + ttl - 1));
    CHECK_FALSE(memo->contains(key, now + ttl));
    CHECK_FALSE(memo->contains(other, now));
  }

  SECTION("remembering again moves the expiry")
  {
    memo->remember(key, now, HRTIME_SECONDS(1));
    memo->remember(key, now, HRTIME_SECONDS(30));
    CHECK(memo->contains(key, now + HRTIME_SECONDS(20)));
  }

  SECTION("a forgotten key is gone")
  {
    memo->remember(key, now, ttl);
    memo->forget(key);
    CHECK_FALSE(memo->contains(key, now));
  }

  SECTION("forgetting a key that shares the slot leaves the remembered key alone")
  {
    memo->remember(key, now, ttl);
    memo->forget(neighbor);
    CHECK(memo->contains(key, now));
  }

  SECTION("forgetting a displaced key leaves the slot's new owner alone")
  {
    memo->remember(key, now, ttl);
    memo->remember(neighbor, now, ttl);
    memo->forget(key);
    CHECK(memo->contains(neighbor, now));
  }

  SECTION("a key that shares a slot replaces the older one")
  {
    memo->remember(key, now, ttl);
    memo->remember(neighbor, now, ttl);
    CHECK(memo->contains(neighbor, now));
    CHECK_FALSE(memo->contains(key, now));
  }

  SECTION("a key with a zero hash is still remembered")
  {
    CryptoHash const zero = make_key(0, 0);
    memo->remember(zero, now, ttl);
    CHECK(memo->contains(zero, now));
  }

  SECTION("a forgotten slot does not match a key with a zero hash")
  {
    // Forgetting zeroes the tag but leaves the expiry, so a zero tag must never be a key's.
    CryptoHash const zero = make_key(0, 0);
    memo->remember(zero, now, ttl);
    memo->forget(zero);
    CHECK_FALSE(memo->contains(zero, now));
  }

  SECTION("a non-positive ttl neither remembers nor evicts")
  {
    memo->remember(key, now, ttl);
    memo->remember(neighbor, now, 0);
    memo->remember(neighbor, now, -1);
    CHECK(memo->contains(key, now));
    CHECK_FALSE(memo->contains(neighbor, now));
  }

  SECTION("a ttl past the end of time still remembers the key")
  {
    memo->remember(key, now, std::numeric_limits<ink_hrtime>::max());
    CHECK(memo->contains(key, std::numeric_limits<ink_hrtime>::max() - 1));
  }
}
