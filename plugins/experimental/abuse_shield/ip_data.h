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
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "swoc/swoc_ip.h"
#include "UdiTable.h"

namespace abuse_shield
{

/// Get current time in milliseconds (steady clock).
inline uint64_t
now_ms()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/** A token bucket whose tokens and update time change in one atomic operation. */
class TokenBucket
{
public:
  int32_t consume(int rate_per_sec, int burst_limit);
  int32_t tokens() const;

private:
  std::atomic<uint64_t> state_{0};
};

/** Per-rule token buckets for one IP and one metric. */
class RuleBuckets
{
public:
  int32_t consume(std::string_view rule_name, int rate_per_sec, int burst_limit);
  bool    exceeded(std::string_view rule_name) const;
  int32_t tokens(std::string_view rule_name) const;
  bool    has_debt() const;

private:
  using BucketPtr = std::shared_ptr<TokenBucket>;

  struct TransparentStringHash {
    using is_transparent = void;

    size_t
    operator()(std::string_view value) const noexcept
    {
      return std::hash<std::string_view>{}(value);
    }
  };

  BucketPtr find_or_create(std::string_view rule_name);
  BucketPtr find(std::string_view rule_name) const;

  mutable std::mutex                                                                 mutex_;
  std::unordered_map<std::string, BucketPtr, TransparentStringHash, std::equal_to<>> buckets_;
};

// ============================================================================
// Transaction/Request tracking data (for g_txn_tracker)
// ============================================================================
struct TxnData {
  RuleBuckets           buckets;
  std::atomic<uint64_t> last_logged{0}; ///< Last time we logged for this IP (steady_clock ms)

  // DEBUG ONLY - Not used for rule matching. Can be removed once stable.
  std::atomic<uint64_t> slot_created{0};
  std::atomic<uint32_t> count{0}; ///< Total requests seen

  TxnData() : slot_created(now_ms()) {}

  int32_t
  consume(std::string_view rule_name, int rate, int burst)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    return buckets.consume(rule_name, rate, burst);
  }

  bool
  is_evictable() const
  {
    return !buckets.has_debt();
  }
};

// ============================================================================
// Connection tracking data (for g_conn_tracker)
// ============================================================================
struct ConnData {
  RuleBuckets           buckets;
  std::atomic<uint64_t> last_logged{0}; ///< Last time we logged for this IP (steady_clock ms)

  // DEBUG ONLY - Not used for rule matching. Can be removed once stable.
  std::atomic<uint64_t> slot_created{0};
  std::atomic<uint32_t> count{0}; ///< Total connections seen

  ConnData() : slot_created(now_ms()) {}

  int32_t
  consume(std::string_view rule_name, int rate, int burst)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    return buckets.consume(rule_name, rate, burst);
  }

  bool
  is_evictable() const
  {
    return !buckets.has_debt();
  }
};

// ============================================================================
// H2 error tracking data (for g_h2_tracker)
// ============================================================================
constexpr size_t NUM_H2_ERROR_CODES = 16;

struct H2Data {
  RuleBuckets           buckets;
  std::atomic<uint64_t> last_logged{0}; ///< Last time we logged for this IP (steady_clock ms)

  // DEBUG ONLY - Not used for rule matching. Can be removed once stable.
  std::atomic<uint64_t> slot_created{0};
  std::atomic<uint32_t> count{0};                          ///< Total H2 errors seen
  std::atomic<uint16_t> error_codes[NUM_H2_ERROR_CODES]{}; ///< Per-code counts

  H2Data() : slot_created(now_ms()) {}

  int32_t
  consume(std::string_view rule_name, int rate, int burst, uint64_t error_code = 0)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    if (error_code < NUM_H2_ERROR_CODES) {
      error_codes[error_code].fetch_add(1, std::memory_order_relaxed);
    }
    return buckets.consume(rule_name, rate, burst);
  }

  bool
  is_evictable() const
  {
    return !buckets.has_debt();
  }
};

// Table type aliases
using TxnTable  = UdiTable<swoc::IPAddr, TxnData, std::hash<swoc::IPAddr>>;
using ConnTable = UdiTable<swoc::IPAddr, ConnData, std::hash<swoc::IPAddr>>;
using H2Table   = UdiTable<swoc::IPAddr, H2Data, std::hash<swoc::IPAddr>>;

} // namespace abuse_shield
