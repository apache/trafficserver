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

#include <limits>

constinit UncacheableKeyMemo uncacheableKeyMemo;

unsigned
UncacheableKeyMemo::index_of(CryptoHash const &key)
{
  return static_cast<unsigned>(key.slice64(1) & (SLOTS - 1));
}

uint64_t
UncacheableKeyMemo::tag_of(CryptoHash const &key)
{
  // An empty slot has a zero tag, so a key may never produce one.
  return key.slice64(0) | 1;
}

bool
UncacheableKeyMemo::contains(CryptoHash const &key, ink_hrtime now) const
{
  Slot const &slot = _slots[index_of(key)];
  return slot.tag.load(std::memory_order_acquire) == tag_of(key) && slot.until.load(std::memory_order_relaxed) > now;
}

void
UncacheableKeyMemo::remember(CryptoHash const &key, ink_hrtime now, ink_hrtime ttl)
{
  if (ttl <= 0) {
    return;
  }
  ink_hrtime const until = ttl > std::numeric_limits<ink_hrtime>::max() - now ? std::numeric_limits<ink_hrtime>::max() : now + ttl;
  Slot            &slot  = _slots[index_of(key)];
  slot.until.store(until, std::memory_order_relaxed);
  slot.tag.store(tag_of(key), std::memory_order_release);
}

void
UncacheableKeyMemo::forget(CryptoHash const &key)
{
  Slot    &slot = _slots[index_of(key)];
  uint64_t tag  = tag_of(key);
  if (slot.tag.load(std::memory_order_relaxed) == tag) {
    slot.tag.compare_exchange_strong(tag, 0, std::memory_order_relaxed);
  }
}
