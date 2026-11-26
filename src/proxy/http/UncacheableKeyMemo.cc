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

#include "proxy/http/UncacheableKeyMemo.h"

#include <bit>
#include <limits>

UncacheableKeyMemo *uncacheableKeyMemo = nullptr;

uint32_t
UncacheableKeyMemo::entries_for(int64_t requested)
{
  if (requested <= WAYS) {
    return WAYS;
  }
  if (requested >= MAX_ENTRIES) {
    return MAX_ENTRIES;
  }
  return std::bit_ceil(static_cast<uint32_t>(requested));
}

UncacheableKeyMemo::UncacheableKeyMemo(uint32_t entries)
{
  uint32_t const sets = entries_for(entries) / WAYS;
  _sets               = std::make_unique<Set[]>(sets);
  _set_mask           = sets - 1;
}

UncacheableKeyMemo::Set const &
UncacheableKeyMemo::set_of(CryptoHash const &key) const
{
  return _sets[key.slice64(1) & _set_mask];
}

UncacheableKeyMemo::Set &
UncacheableKeyMemo::set_of(CryptoHash const &key)
{
  return _sets[key.slice64(1) & _set_mask];
}

uint64_t
UncacheableKeyMemo::tag_of(CryptoHash const &key)
{
  // An empty entry has a zero tag, so a key may never produce one.
  return (key.slice64(0) & ~ADMITTED) | PRESENT;
}

bool
UncacheableKeyMemo::contains(CryptoHash const &key, ink_hrtime now) const
{
  uint64_t const want = tag_of(key) | ADMITTED;
  for (Entry const &entry : set_of(key).ways) {
    if (entry.tag.load(std::memory_order_acquire) == want) {
      return entry.until.load(std::memory_order_relaxed) > now;
    }
  }
  return false;
}

UncacheableKeyMemo::Outcome
UncacheableKeyMemo::remember(CryptoHash const &key, ink_hrtime now, ink_hrtime ttl)
{
  if (ttl <= 0) {
    return {};
  }
  ink_hrtime const until = ttl > std::numeric_limits<ink_hrtime>::max() - now ? std::numeric_limits<ink_hrtime>::max() : now + ttl;
  uint64_t const   tag   = tag_of(key);
  Set             &set   = set_of(key);

  for (Entry &entry : set.ways) {
    uint64_t const have = entry.tag.load(std::memory_order_relaxed);
    if ((have & ~ADMITTED) != tag) {
      continue;
    }
    // A sighting that lapsed starts over, so admission takes two within one lifetime.
    Remembered what = Remembered::SEEN_ONCE;
    uint64_t   next = tag;
    if (entry.until.load(std::memory_order_relaxed) > now) {
      what = (have & ADMITTED) ? Remembered::RENEWED : Remembered::ADMITTED;
      next = tag | ADMITTED;
    }
    entry.until.store(until, std::memory_order_relaxed);
    entry.tag.store(next, std::memory_order_release);
    return {what, false};
  }

  // An empty or expired entry costs nothing to take. Failing that, the oldest seen-once entry.
  Entry     *victim         = nullptr;
  ink_hrtime victim_until   = std::numeric_limits<ink_hrtime>::max();
  bool       victim_is_live = false;
  for (Entry &entry : set.ways) {
    uint64_t const   have       = entry.tag.load(std::memory_order_relaxed);
    ink_hrtime const have_until = entry.until.load(std::memory_order_relaxed);
    if (have == 0 || have_until <= now) {
      victim         = &entry;
      victim_is_live = false;
      break;
    }
    if (!(have & ADMITTED) && have_until < victim_until) {
      victim         = &entry;
      victim_until   = have_until;
      victim_is_live = true;
    }
  }
  if (victim == nullptr) {
    return {Remembered::SET_FULL, false};
  }
  victim->until.store(until, std::memory_order_relaxed);
  victim->tag.store(tag, std::memory_order_release);
  return {Remembered::SEEN_ONCE, victim_is_live};
}

void
UncacheableKeyMemo::forget(CryptoHash const &key)
{
  uint64_t const tag = tag_of(key);
  for (Entry &entry : set_of(key).ways) {
    uint64_t have = entry.tag.load(std::memory_order_relaxed);
    if ((have & ~ADMITTED) == tag) {
      entry.tag.compare_exchange_strong(have, 0, std::memory_order_relaxed);
      return;
    }
  }
}
