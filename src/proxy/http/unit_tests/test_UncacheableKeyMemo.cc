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
using Remembered = UncacheableKeyMemo::Remembered;

CryptoHash
make_key(uint64_t a, uint64_t b)
{
  CryptoHash key;
  key.u64[0] = a;
  key.u64[1] = b;
  return key;
}

// Keys that share u64[1] share a set; u64[0] is the tag.
CryptoHash
in_set(uint64_t tag, uint64_t set)
{
  return make_key(tag << 2, set);
}
} // namespace

TEST_CASE("UncacheableKeyMemo", "[http][cache]")
{
  auto             memo  = std::make_unique<UncacheableKeyMemo>();
  CryptoHash const key   = make_key(0x1111 << 2, 0x2222);
  CryptoHash const other = make_key(0x3333 << 2, 0x4444);
  ink_hrtime const now   = HRTIME_SECONDS(1000);
  ink_hrtime const ttl   = HRTIME_SECONDS(10);

  // Two sightings within the lifetime make a key remembered.
  auto admit = [&](CryptoHash const &k, ink_hrtime at) {
    memo->remember(k, at, ttl);
    return memo->remember(k, at, ttl).what;
  };

  SECTION("an unknown key is not remembered")
  {
    CHECK_FALSE(memo->contains(key, now));
  }

  SECTION("one sighting is not enough")
  {
    CHECK(memo->remember(key, now, ttl).what == Remembered::SEEN_ONCE);
    CHECK_FALSE(memo->contains(key, now));
  }

  SECTION("a second sighting within the lifetime admits the key")
  {
    memo->remember(key, now, ttl);
    CHECK(memo->remember(key, now + ttl - 1, ttl).what == Remembered::ADMITTED);
    CHECK(memo->contains(key, now + ttl));
    CHECK_FALSE(memo->contains(other, now));
  }

  SECTION("a second sighting after the first lapsed starts over")
  {
    memo->remember(key, now, ttl);
    CHECK(memo->remember(key, now + ttl, ttl).what == Remembered::SEEN_ONCE);
    CHECK_FALSE(memo->contains(key, now + ttl));
  }

  SECTION("a remembered key is found until it expires")
  {
    REQUIRE(admit(key, now) == Remembered::ADMITTED);
    CHECK(memo->contains(key, now));
    CHECK(memo->contains(key, now + ttl - 1));
    CHECK_FALSE(memo->contains(key, now + ttl));
  }

  SECTION("a remembered key that lapses must be seen twice again")
  {
    admit(key, now);
    CHECK(memo->remember(key, now + ttl, ttl).what == Remembered::SEEN_ONCE);
    CHECK_FALSE(memo->contains(key, now + ttl));
  }

  SECTION("remembering again moves the expiry")
  {
    admit(key, now);
    CHECK(memo->remember(key, now + HRTIME_SECONDS(5), HRTIME_SECONDS(30)).what == Remembered::RENEWED);
    CHECK(memo->contains(key, now + HRTIME_SECONDS(20)));
  }

  SECTION("a forgotten key is gone, remembered or seen once")
  {
    REQUIRE(admit(key, now) == Remembered::ADMITTED);
    REQUIRE(memo->contains(key, now));
    CHECK(memo->forget(key));
    CHECK_FALSE(memo->contains(key, now));
    memo->remember(other, now, ttl);
    CHECK(memo->forget(other));
    CHECK(memo->remember(other, now, ttl).what == Remembered::SEEN_ONCE);
  }

  SECTION("forgetting a key leaves the others in its set alone")
  {
    CryptoHash const a = in_set(1, 7);
    CryptoHash const b = in_set(2, 7);
    admit(a, now);
    admit(b, now);
    CHECK(memo->forget(b));
    CHECK(memo->contains(a, now));
    CHECK_FALSE(memo->contains(b, now));
    CHECK_FALSE(memo->forget(b));
  }

  SECTION("four keys share a set without displacing each other")
  {
    for (uint64_t t = 1; t <= UncacheableKeyMemo::WAYS; ++t) {
      CHECK(admit(in_set(t, 7), now) == Remembered::ADMITTED);
    }
    for (uint64_t t = 1; t <= UncacheableKeyMemo::WAYS; ++t) {
      CHECK(memo->contains(in_set(t, 7), now));
    }
  }

  SECTION("with every entry remembered, the one renewed longest ago makes room")
  {
    for (uint64_t t = 1; t <= UncacheableKeyMemo::WAYS; ++t) {
      admit(in_set(t, 7), now + static_cast<ink_hrtime>(t));
    }
    CHECK(memo->remember(in_set(1, 7), now + 10, ttl).what == Remembered::RENEWED);
    UncacheableKeyMemo::Outcome const outcome = memo->remember(in_set(9, 7), now + 11, ttl);
    CHECK(outcome.what == Remembered::SEEN_ONCE);
    CHECK(outcome.displaced);
    CHECK_FALSE(outcome.evicted);
    CHECK(memo->contains(in_set(1, 7), now + 11));
    CHECK_FALSE(memo->contains(in_set(2, 7), now + 11));
    CHECK(memo->contains(in_set(3, 7), now + 11));
    CHECK(memo->contains(in_set(4, 7), now + 11));
    CHECK(memo->remember(in_set(9, 7), now + 12, ttl).what == Remembered::ADMITTED);
  }

  SECTION("a key renewed by every request outlasts a stream of keys asked for twice")
  {
    CryptoHash const hot = in_set(1, 7);
    admit(hot, now);
    bool stayed = true;
    for (uint64_t t = 100; t < 1100; ++t) {
      ink_hrtime const at = now + static_cast<ink_hrtime>(t) * 10;
      memo->remember(hot, at, ttl);
      admit(in_set(t, 7), at + 1);
      stayed = stayed && memo->contains(hot, at + 2);
    }
    CHECK(stayed);
  }

  SECTION("a new key takes an expired entry before a live seen-once one")
  {
    // Three remembered keys and one seen once, all live; then the first remembered one lapses.
    admit(in_set(1, 7), now);
    admit(in_set(2, 7), now + HRTIME_SECONDS(5));
    admit(in_set(3, 7), now + HRTIME_SECONDS(5));
    memo->remember(in_set(4, 7), now + HRTIME_SECONDS(5), ttl);
    UncacheableKeyMemo::Outcome const outcome = memo->remember(in_set(9, 7), now + ttl, ttl);
    CHECK(outcome.what == Remembered::SEEN_ONCE);
    CHECK_FALSE(outcome.evicted);
    CHECK_FALSE(outcome.displaced);
    CHECK(memo->remember(in_set(4, 7), now + ttl, ttl).what == Remembered::ADMITTED);
  }

  SECTION("with no expired entry, the oldest seen-once key is displaced")
  {
    admit(in_set(1, 7), now);
    admit(in_set(2, 7), now);
    memo->remember(in_set(3, 7), now + 1, ttl);
    memo->remember(in_set(4, 7), now + 2, ttl);
    UncacheableKeyMemo::Outcome const outcome = memo->remember(in_set(9, 7), now + 3, ttl);
    CHECK(outcome.what == Remembered::SEEN_ONCE);
    CHECK(outcome.evicted);
    CHECK_FALSE(outcome.displaced);
    // The younger seen-once key survived, the older one has to start over, and the remembered keys stayed.
    CHECK(memo->remember(in_set(4, 7), now + 4, ttl).what == Remembered::ADMITTED);
    CHECK(memo->remember(in_set(3, 7), now + 4, ttl).what == Remembered::SEEN_ONCE);
    CHECK_FALSE(memo->contains(in_set(3, 7), now + 4));
    CHECK(memo->contains(in_set(1, 7), now + 4));
    CHECK(memo->contains(in_set(2, 7), now + 4));
  }

  SECTION("a stream of keys seen once costs a full set at most one remembered key")
  {
    for (uint64_t t = 1; t <= UncacheableKeyMemo::WAYS; ++t) {
      admit(in_set(t, 7), now + static_cast<ink_hrtime>(t));
    }
    int displaced = 0;
    for (uint64_t t = 100; t < 1100; ++t) {
      displaced += memo->remember(in_set(t, 7), now + static_cast<ink_hrtime>(t), ttl).displaced;
    }
    CHECK(displaced == 1);
    CHECK_FALSE(memo->contains(in_set(1, 7), now + 1100));
    for (uint64_t t = 2; t <= UncacheableKeyMemo::WAYS; ++t) {
      CHECK(memo->contains(in_set(t, 7), now + 1100));
    }
  }

  SECTION("keys in other sets do not count against a set")
  {
    for (uint64_t s = 0; s < 64; ++s) {
      admit(in_set(1, s), now);
    }
    for (uint64_t s = 0; s < 64; ++s) {
      CHECK(memo->contains(in_set(1, s), now));
    }
  }

  SECTION("a key with a zero hash is still remembered")
  {
    CryptoHash const zero = make_key(0, 0);
    admit(zero, now);
    CHECK(memo->contains(zero, now));
  }

  SECTION("a forgotten entry does not match a key with a zero hash")
  {
    // Forgetting zeroes the tag but leaves the expiry, so a zero tag must never be a key's.
    CryptoHash const zero = make_key(0, 0);
    admit(zero, now);
    memo->forget(zero);
    CHECK_FALSE(memo->contains(zero, now));
  }

  SECTION("keys that differ only in the tag's flag bits stay apart")
  {
    CryptoHash const plain   = make_key(0x100, 7);
    CryptoHash const flagged = make_key(0x103, 7);
    admit(plain, now);
    CHECK(memo->contains(plain, now));
    // Both reduce to the same tag, so they are one key to the memo; that only costs a false positive.
    CHECK(memo->contains(flagged, now));
  }

  SECTION("a non-positive ttl does nothing")
  {
    CHECK(memo->remember(key, now, 0).what == Remembered::NOTHING);
    CHECK(memo->remember(key, now, -1).what == Remembered::NOTHING);
    CHECK(memo->remember(key, now, ttl).what == Remembered::SEEN_ONCE);
  }

  SECTION("a ttl past the end of time still remembers the key")
  {
    memo->remember(key, now, std::numeric_limits<ink_hrtime>::max());
    memo->remember(key, now, std::numeric_limits<ink_hrtime>::max());
    CHECK(memo->contains(key, std::numeric_limits<ink_hrtime>::max() - 1));
  }

  SECTION("a full set whose entries never expire still makes room")
  {
    ink_hrtime const forever = std::numeric_limits<ink_hrtime>::max();
    for (uint64_t t = 1; t <= 4; ++t) {
      memo->remember(in_set(t, 7), now, forever);
    }
    UncacheableKeyMemo::Outcome outcome = memo->remember(in_set(9, 7), now, forever);
    CHECK(outcome.what == Remembered::SEEN_ONCE);
    CHECK(outcome.evicted);

    for (uint64_t t = 1; t <= 4; ++t) {
      memo->remember(in_set(t, 8), now, forever);
      memo->remember(in_set(t, 8), now, forever);
    }
    outcome = memo->remember(in_set(9, 8), now, forever);
    CHECK(outcome.what == Remembered::SEEN_ONCE);
    CHECK(outcome.displaced);
    CHECK_FALSE(outcome.evicted);
    CHECK(memo->remember(in_set(9, 8), now, forever).what == Remembered::ADMITTED);
    CHECK(memo->contains(in_set(9, 8), forever - 1));
  }

  SECTION("a full set gives up the remembered key closest to expiry, whatever its ttl")
  {
    // Key 1 was renewed first but for longer, so key 2, renewed later for less, goes first.
    memo->remember(in_set(1, 7), now, HRTIME_SECONDS(60));
    memo->remember(in_set(1, 7), now, HRTIME_SECONDS(60));
    admit(in_set(2, 7), now + HRTIME_SECONDS(5));
    admit(in_set(3, 7), now + HRTIME_SECONDS(5) + 1);
    admit(in_set(4, 7), now + HRTIME_SECONDS(5) + 2);
    CHECK(memo->remember(in_set(9, 7), now + HRTIME_SECONDS(6), ttl).displaced);
    CHECK(memo->contains(in_set(1, 7), now + HRTIME_SECONDS(6)));
    CHECK_FALSE(memo->contains(in_set(2, 7), now + HRTIME_SECONDS(6)));
    CHECK(memo->contains(in_set(3, 7), now + HRTIME_SECONDS(6)));
    CHECK(memo->contains(in_set(4, 7), now + HRTIME_SECONDS(6)));
  }

  SECTION("a renewal with a shorter ttl brings the expiry in")
  {
    admit(key, now);
    CHECK(memo->remember(key, now + 1, HRTIME_SECONDS(1)).what == Remembered::RENEWED);
    CHECK(memo->contains(key, now + HRTIME_SECONDS(1)));
    CHECK_FALSE(memo->contains(key, now + 1 + HRTIME_SECONDS(1)));
  }

  SECTION("an expired seen-once entry of another key is taken without counting")
  {
    for (uint64_t t = 1; t <= 4; ++t) {
      memo->remember(in_set(t, 7), now, ttl);
    }
    UncacheableKeyMemo::Outcome const outcome = memo->remember(in_set(9, 7), now + ttl, ttl);
    CHECK(outcome.what == Remembered::SEEN_ONCE);
    CHECK_FALSE(outcome.evicted);
    CHECK_FALSE(outcome.displaced);
  }
}

TEST_CASE("UncacheableKeyMemo size", "[http][cache]")
{
  CHECK(UncacheableKeyMemo::entries_for(-1) == UncacheableKeyMemo::WAYS);
  CHECK(UncacheableKeyMemo::entries_for(0) == UncacheableKeyMemo::WAYS);
  CHECK(UncacheableKeyMemo::entries_for(4) == 4);
  CHECK(UncacheableKeyMemo::entries_for(5) == 8);
  CHECK(UncacheableKeyMemo::entries_for(65536) == 65536);
  CHECK(UncacheableKeyMemo::entries_for(65537) == 131072);
  CHECK(UncacheableKeyMemo::entries_for(UncacheableKeyMemo::MAX_ENTRIES - 1) == UncacheableKeyMemo::MAX_ENTRIES);
  CHECK(UncacheableKeyMemo::entries_for(UncacheableKeyMemo::MAX_ENTRIES) == UncacheableKeyMemo::MAX_ENTRIES);
  CHECK(UncacheableKeyMemo::entries_for(int64_t{1} << 40) == UncacheableKeyMemo::MAX_ENTRIES);
  CHECK(UncacheableKeyMemo{}.entries() == UncacheableKeyMemo::DEFAULT_ENTRIES);
  CHECK(UncacheableKeyMemo{5}.entries() == 8);

  // One set: the fifth key pushes out the remembered key renewed longest ago.
  UncacheableKeyMemo small{4};
  CHECK(small.entries() == 4);
  ink_hrtime const now = HRTIME_SECONDS(1);
  ink_hrtime const ttl = HRTIME_SECONDS(10);
  for (uint64_t t = 1; t <= 4; ++t) {
    small.remember(make_key(t << 2, t), now, ttl);
    small.remember(make_key(t << 2, t), now, ttl);
  }
  CHECK(small.remember(make_key(5 << 2, 5), now, ttl).displaced);
}
