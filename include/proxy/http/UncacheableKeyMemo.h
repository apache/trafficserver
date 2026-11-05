/** @file

  Remembers cache keys whose last origin response could not be stored.

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

#include "tscore/CryptoHash.h"
#include "tscore/ink_hrtime.h"

/** A fixed-size, lock-free memo of cache keys whose last origin response was uncacheable.

    A transaction that misses on a remembered key skips the eager cache write lock and goes to
    the origin straight away, taking the lock only if the response turns out to be cacheable
    after all. That keeps concurrent misses on a cacheable key collapsed onto one origin fetch
    while sparing uncacheable keys the lock and the waits behind it.

    The table is direct mapped: a key maps to the slot picked by the low bits of the second
    64-bit slice of its hash, and a newer key in that slot replaces the older one. The slot
    keeps the first 64-bit slice as a tag, with the low bit set so it is never zero, and an
    expiry time. Readers and writers use plain atomics with no lock, so two keys remembered
    into one slot at the same time can leave one key's tag next to the other's expiry until the
    slot is written again. Any such mix-up costs little: a false positive sends one miss to the
    origin uncollapsed, and a false negative makes one miss take the lock. Neither changes what
    gets stored.
 */
class UncacheableKeyMemo
{
public:
  /// @return @c true if @a key was remembered with an expiry later than @a now.
  bool contains(CryptoHash const &key, ink_hrtime now) const;

  /// Remember @a key as uncacheable for @a ttl from @a now. A non-positive @a ttl does nothing.
  void remember(CryptoHash const &key, ink_hrtime now, ink_hrtime ttl);

  /// Drop @a key if it is remembered. A different key in its slot is left alone.
  void forget(CryptoHash const &key);

private:
  static constexpr unsigned SLOT_BITS = 16;
  static constexpr unsigned SLOTS     = 1u << SLOT_BITS;

  struct Slot {
    std::atomic<uint64_t>   tag{0};
    std::atomic<ink_hrtime> until{0};
  };
  static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<ink_hrtime>::is_always_lock_free);

  static unsigned index_of(CryptoHash const &key);
  static uint64_t tag_of(CryptoHash const &key);

  Slot _slots[SLOTS];
};

extern UncacheableKeyMemo uncacheableKeyMemo;
