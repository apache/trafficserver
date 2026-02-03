/** @file

  Token bucket rate limiting data structures for abuse detection.

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

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

#include "swoc/swoc_ip.h"
#include "tsutil/UdiTable.h"

namespace abuse_shield
{

/// Get current time in milliseconds (steady clock).
inline uint64_t
now_ms()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// Consume a token from a bucket, replenishing based on elapsed time.
/// @param tokens Reference to token counter.
/// @param last_update Reference to timestamp of last update.
/// @param rate_per_sec The configured max rate per second.
/// @param burst_limit Maximum tokens that can accumulate.
/// @return Current token count after consumption (negative = rate exceeded).
int32_t consume_token(std::atomic<int32_t> &tokens, std::atomic<uint64_t> &last_update, int rate_per_sec, int burst_limit);

// ============================================================================
// Transaction/Request tracking data (for g_txn_tracker)
// ============================================================================
struct TxnData {
  std::atomic<int32_t>  tokens{0};
  std::atomic<uint64_t> last_update{0};
  std::atomic<uint64_t> blocked_until{0};
  std::atomic<uint64_t> last_logged{0}; ///< Last time we logged for this IP (steady_clock ms)

  // DEBUG ONLY - Not used for rule matching. Can be removed once stable.
  std::atomic<uint64_t> slot_created{0};
  std::atomic<uint32_t> count{0}; ///< Total requests seen

  TxnData() : slot_created(now_ms()) {}

  int32_t
  consume(int rate, int burst)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    return consume_token(tokens, last_update, rate, burst);
  }

  bool is_blocked() const;
  void block_until(uint64_t until_ms);
};

// ============================================================================
// Connection tracking data (for g_conn_tracker)
// ============================================================================
struct ConnData {
  std::atomic<int32_t>  tokens{0};
  std::atomic<uint64_t> last_update{0};
  std::atomic<uint64_t> blocked_until{0};
  std::atomic<uint64_t> last_logged{0}; ///< Last time we logged for this IP (steady_clock ms)

  // DEBUG ONLY - Not used for rule matching. Can be removed once stable.
  std::atomic<uint64_t> slot_created{0};
  std::atomic<uint32_t> count{0}; ///< Total connections seen

  ConnData() : slot_created(now_ms()) {}

  int32_t
  consume(int rate, int burst)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    return consume_token(tokens, last_update, rate, burst);
  }

  bool is_blocked() const;
  void block_until(uint64_t until_ms);
};

// ============================================================================
// H2 error tracking data (for g_h2_tracker)
// ============================================================================
constexpr size_t NUM_H2_ERROR_CODES = 16;

struct H2Data {
  std::atomic<int32_t>  tokens{0};
  std::atomic<uint64_t> last_update{0};
  std::atomic<uint64_t> blocked_until{0};
  std::atomic<uint64_t> last_logged{0}; ///< Last time we logged for this IP (steady_clock ms)

  // DEBUG ONLY - Not used for rule matching. Can be removed once stable.
  std::atomic<uint64_t> slot_created{0};
  std::atomic<uint32_t> count{0};                          ///< Total H2 errors seen
  std::atomic<uint16_t> error_codes[NUM_H2_ERROR_CODES]{}; ///< Per-code counts

  H2Data() : slot_created(now_ms()) {}

  int32_t
  consume(int rate, int burst, uint8_t error_code = 0)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    if (error_code < NUM_H2_ERROR_CODES) {
      error_codes[error_code].fetch_add(1, std::memory_order_relaxed);
    }
    return consume_token(tokens, last_update, rate, burst);
  }

  bool is_blocked() const;
  void block_until(uint64_t until_ms);
};

// Table type aliases
using TxnTable  = ts::UdiTable<swoc::IPAddr, TxnData, std::hash<swoc::IPAddr>>;
using ConnTable = ts::UdiTable<swoc::IPAddr, ConnData, std::hash<swoc::IPAddr>>;
using H2Table   = ts::UdiTable<swoc::IPAddr, H2Data, std::hash<swoc::IPAddr>>;

} // namespace abuse_shield
