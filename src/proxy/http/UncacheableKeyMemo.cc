/** @file

  Remembers cache keys whose origin responses could not be stored.

  @par License

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
#include "tscore/ink_assert.h"

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
    // A sighting that lapsed starts over, so admission takes two within one ttl.
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

  // An empty or expired entry costs nothing to take. Failing that, the seen-once entry closest
  // to expiry, and only when every entry is remembered, the remembered one closest to expiry.
  // Each qualifying uncacheable response renews its key, so with one ttl for every key the
  // earliest expiry marks the key asked for least recently, without a write on the lookup path.
  Entry     *empty         = nullptr;
  Entry     *seen_once     = nullptr;
  Entry     *remembered    = nullptr;
  ink_hrtime seen_until    = std::numeric_limits<ink_hrtime>::max();
  ink_hrtime remembered_at = std::numeric_limits<ink_hrtime>::max();
  for (Entry &entry : set.ways) {
    uint64_t const   have       = entry.tag.load(std::memory_order_relaxed);
    ink_hrtime const have_until = entry.until.load(std::memory_order_relaxed);
    if (have == 0 || have_until <= now) {
      empty = &entry;
      break;
    }
    if (!(have & ADMITTED)) {
      if (seen_once == nullptr || have_until < seen_until) {
        seen_once  = &entry;
        seen_until = have_until;
      }
    } else if (remembered == nullptr || have_until < remembered_at) {
      remembered    = &entry;
      remembered_at = have_until;
    }
  }
  Outcome outcome{Remembered::SEEN_ONCE};
  Entry  *victim = empty;
  if (victim == nullptr && seen_once != nullptr) {
    victim          = seen_once;
    outcome.evicted = true;
  } else if (victim == nullptr) {
    victim            = remembered;
    outcome.displaced = true;
  }
  // Every live entry is seen once or remembered, so with at least one way a victim exists.
  ink_assert(victim != nullptr);
  victim->until.store(until, std::memory_order_relaxed);
  victim->tag.store(tag, std::memory_order_release);
  return outcome;
}

bool
UncacheableKeyMemo::forget(CryptoHash const &key)
{
  uint64_t const tag = tag_of(key);
  // Every matching entry goes, since racing first answers can leave a key in two of them. A
  // failed exchange, whether another writer changed the entry or it failed spuriously, reloads
  // the tag, so the entry is cleared only while it still holds this key.
  bool dropped = false;
  for (Entry &entry : set_of(key).ways) {
    uint64_t have = entry.tag.load(std::memory_order_relaxed);
    while ((have & ~ADMITTED) == tag) {
      if (entry.tag.compare_exchange_weak(have, 0, std::memory_order_relaxed)) {
        dropped = true;
        break;
      }
    }
  }
  return dropped;
}
