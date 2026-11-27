/** @file

  Remembers cache keys whose origin responses could not be stored.

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

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "tscore/CryptoHash.h"
#include "tscore/ink_hrtime.h"

/** A fixed-size, lock-free memo of cache keys whose origin responses were uncacheable.

    A transaction that misses on a remembered key skips the eager cache write lock and goes to
    the origin straight away, taking the lock only if the response turns out to be cacheable
    after all. That keeps concurrent misses on a cacheable key collapsed onto one origin fetch
    while sparing uncacheable keys the lock and the waits behind it.

    A key is remembered only once it answers uncacheable twice within one ttl of the first
    answer. Repeated keys are the ones that pile up behind the lock when they are not
    remembered, and the first answer only takes a seen-once entry, so URLs asked for once cost a
    set at most one remembered key at a time (see the victim order below). A repeated key that
    has answered only once can still be pushed out before its second answer, which remember()
    reports as Outcome::evicted.

    The table is split into sets of four entries, one cache line each, picked by the low bits of
    the second 64-bit slice of the key's hash. An entry keeps the first slice as a tag, with the
    low bit set so it is never zero and the next bit marking a remembered key, plus an expiry
    time. A new key takes an empty or expired entry first, then the seen-once entry closest to
    expiry, and only when all four hold live remembered keys, the remembered one closest to
    expiry. With one ttl for every key, that is the one renewed longest ago. So once a set is
    full of remembered keys, keys asked for once displace one of them and then churn through
    that entry, while the keys renewed most recently stay put.

    Readers and writers use plain atomics with no lock, so two keys written into one entry at
    the same time can leave one key's tag next to the other's expiry until the entry is written
    again, and two first answers for one key racing into a set can leave it in two entries.
    Any such mix-up costs little: a false positive sends one miss to the origin uncollapsed,
    and a false negative makes one miss take the lock. Neither changes what gets stored.
 */
class UncacheableKeyMemo
{
public:
  static constexpr unsigned WAYS            = 4;
  static constexpr uint32_t DEFAULT_ENTRIES = 1u << 16;
  static constexpr uint32_t MAX_ENTRIES     = 1u << 24;
  static_assert(WAYS > 0, "victim selection needs at least one entry per set");

  /// What a call to remember() did.
  enum class Remembered {
    NOTHING,   ///< A non-positive ttl.
    SEEN_ONCE, ///< The key now has a seen-once entry.
    ADMITTED,  ///< A seen-once key answered uncacheable again and is now remembered.
    RENEWED,   ///< An already remembered key had its expiry reset.
  };

  struct Outcome {
    Remembered what = Remembered::NOTHING;
    /// A live seen-once entry of another key was overwritten to make room.
    bool evicted = false;
    /// Every entry held a live remembered key, and the one closest to expiry made room.
    bool displaced = false;
  };

  /// @return @a requested rounded up to a power of two in [WAYS, MAX_ENTRIES].
  static uint32_t entries_for(int64_t requested);

  /// A memo of @a entries keys, rounded as entries_for() rounds.
  explicit UncacheableKeyMemo(uint32_t entries = DEFAULT_ENTRIES);

  uint32_t
  entries() const
  {
    return (_set_mask + 1) * WAYS;
  }

  /// @return @c true if @a key is remembered with an expiry later than @a now.
  bool contains(CryptoHash const &key, ink_hrtime now) const;

  /// Note that @a key answered uncacheable at @a now, for @a ttl. A non-positive @a ttl does nothing.
  Outcome remember(CryptoHash const &key, ink_hrtime now, ink_hrtime ttl);

  /// Drop every entry for @a key, remembered or seen once. Other keys in its set are left alone.
  /// @return @c true if an entry for @a key, live or expired, was dropped.
  bool forget(CryptoHash const &key);

private:
  static constexpr uint64_t PRESENT  = 1;
  static constexpr uint64_t ADMITTED = 2;

  struct Entry {
    std::atomic<uint64_t>   tag{0};
    std::atomic<ink_hrtime> until{0};
  };
  static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<ink_hrtime>::is_always_lock_free);

  struct alignas(64) Set {
    Entry ways[WAYS];
  };
  static_assert(sizeof(Set) == 64);

  Set const      &set_of(CryptoHash const &key) const;
  Set            &set_of(CryptoHash const &key);
  static uint64_t tag_of(CryptoHash const &key);

  std::unique_ptr<Set[]> _sets;
  uint32_t               _set_mask;
};

/// The process's memo, sized from records.yaml at startup.
extern UncacheableKeyMemo *uncacheableKeyMemo;
