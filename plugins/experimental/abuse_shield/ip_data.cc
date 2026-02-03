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

namespace abuse_shield
{

int32_t
consume_token(std::atomic<int32_t> &tokens, std::atomic<uint64_t> &last_update, int rate_per_sec, int burst_limit)
{
  uint64_t now  = now_ms();
  uint64_t last = last_update.load(std::memory_order_relaxed);
  int32_t  current;

  if (last == 0) {
    // First event - initialize with full burst allowance
    current = burst_limit;
  } else {
    // Replenish based on elapsed time
    uint64_t elapsed_ms = now - last;
    int32_t  replenish  = static_cast<int32_t>((elapsed_ms * rate_per_sec) / 1000);

    current  = tokens.load(std::memory_order_relaxed);
    current += replenish;
    if (current > burst_limit) {
      current = burst_limit;
    }
  }

  // Update timestamp and consume one token
  last_update.store(now, std::memory_order_relaxed);
  current -= 1;
  tokens.store(current, std::memory_order_relaxed);

  return current;
}

// TxnData methods
bool
TxnData::is_blocked() const
{
  uint64_t until = blocked_until.load(std::memory_order_relaxed);
  return until > 0 && now_ms() < until;
}

void
TxnData::block_until(uint64_t until_ms)
{
  blocked_until.store(until_ms, std::memory_order_relaxed);
}

// ConnData methods
bool
ConnData::is_blocked() const
{
  uint64_t until = blocked_until.load(std::memory_order_relaxed);
  return until > 0 && now_ms() < until;
}

void
ConnData::block_until(uint64_t until_ms)
{
  blocked_until.store(until_ms, std::memory_order_relaxed);
}

// H2Data methods
bool
H2Data::is_blocked() const
{
  uint64_t until = blocked_until.load(std::memory_order_relaxed);
  return until > 0 && now_ms() < until;
}

void
H2Data::block_until(uint64_t until_ms)
{
  blocked_until.store(until_ms, std::memory_order_relaxed);
}

} // namespace abuse_shield
