/** @file

  Token bucket rate limiting implementation for abuse detection.

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

#include "ip_data.h"

#include <algorithm>
#include <limits>

namespace abuse_shield
{

namespace
{
  uint64_t
  pack_state(uint32_t update_ms, int32_t tokens)
  {
    // Bias the token bits so a legitimate timestamp/token pair never collides
    // with the all-zero uninitialized sentinel.
    return (static_cast<uint64_t>(update_ms) << 32) | (static_cast<uint32_t>(tokens) ^ 0x80000000U);
  }

  uint32_t
  state_update_ms(uint64_t state)
  {
    return static_cast<uint32_t>(state >> 32);
  }

  int32_t
  state_tokens(uint64_t state)
  {
    return static_cast<int32_t>(static_cast<uint32_t>(state) ^ 0x80000000U);
  }
} // namespace

int32_t
TokenBucket::consume(int rate_per_sec, int burst_limit)
{
  uint64_t old = state_.load(std::memory_order_relaxed);

  while (true) {
    // Refresh after every failed CAS so this thread never computes from a
    // timestamp older than the state returned by compare_exchange_weak.
    uint32_t now = static_cast<uint32_t>(now_ms());
    int64_t  current;

    if (old == 0) {
      current = burst_limit;
    } else {
      uint32_t elapsed_ms = now - state_update_ms(old);
      uint64_t replenish  = (static_cast<uint64_t>(elapsed_ms) * static_cast<uint64_t>(rate_per_sec)) / 1000;

      current = state_tokens(old);
      if (replenish >= static_cast<uint64_t>(std::max<int64_t>(0, static_cast<int64_t>(burst_limit) - current))) {
        current = burst_limit;
      } else {
        current += static_cast<int64_t>(replenish);
      }
    }

    current          = std::max<int64_t>(std::numeric_limits<int32_t>::min(), current - 1);
    uint64_t desired = pack_state(now, static_cast<int32_t>(current));
    if (state_.compare_exchange_weak(old, desired, std::memory_order_relaxed)) {
      return static_cast<int32_t>(current);
    }
  }
}

int32_t
TokenBucket::tokens() const
{
  return state_tokens(state_.load(std::memory_order_relaxed));
}

RuleBuckets::BucketPtr
RuleBuckets::find_or_create(const std::string &rule_name)
{
  std::lock_guard lock(mutex_);
  auto            spot = buckets_.find(rule_name);
  if (spot == buckets_.end()) {
    spot = buckets_.emplace(rule_name, std::make_shared<TokenBucket>()).first;
  }
  return spot->second;
}

RuleBuckets::BucketPtr
RuleBuckets::find(const std::string &rule_name) const
{
  std::lock_guard lock(mutex_);
  auto            spot = buckets_.find(rule_name);
  return spot == buckets_.end() ? nullptr : spot->second;
}

int32_t
RuleBuckets::consume(const std::string &rule_name, int rate_per_sec, int burst_limit)
{
  return find_or_create(rule_name)->consume(rate_per_sec, burst_limit);
}

bool
RuleBuckets::exceeded(const std::string &rule_name) const
{
  auto bucket = find(rule_name);
  return bucket && bucket->tokens() < 0;
}

int32_t
RuleBuckets::tokens(const std::string &rule_name) const
{
  auto bucket = find(rule_name);
  return bucket ? bucket->tokens() : 0;
}

bool
RuleBuckets::has_debt() const
{
  std::lock_guard lock(mutex_);
  return std::any_of(buckets_.begin(), buckets_.end(), [](auto const &item) { return item.second->tokens() < 0; });
}

} // namespace abuse_shield
