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
#include <unordered_map>

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

/** Check whether a per-IP log interval has elapsed.
 *
 * A zero timestamp means that the IP has never been logged.
 */
inline bool
log_interval_elapsed(uint64_t now, uint64_t last_logged, uint64_t interval)
{
  return last_logged == 0 || (now >= last_logged && now - last_logged >= interval);
}

/** Atomically claim the current per-IP log opportunity. */
inline bool
claim_log_interval(std::atomic<uint64_t> &last_logged, uint64_t now, uint64_t interval)
{
  uint64_t expected = last_logged.load(std::memory_order_relaxed);

  return log_interval_elapsed(now, expected, interval) &&
         last_logged.compare_exchange_strong(expected, now, std::memory_order_relaxed);
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
  int32_t consume(const std::string &rule_name, int rate_per_sec, int burst_limit);
  bool    exceeded(const std::string &rule_name) const;
  int32_t tokens(const std::string &rule_name) const;
  bool    has_debt() const;

private:
  using BucketPtr = std::shared_ptr<TokenBucket>;

  BucketPtr find_or_create(const std::string &rule_name);
  BucketPtr find(const std::string &rule_name) const;

  mutable std::mutex                         mutex_;
  std::unordered_map<std::string, BucketPtr> buckets_;
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
  consume(const std::string &rule_name, int rate, int burst)
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
  consume(const std::string &rule_name, int rate, int burst)
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
  consume(const std::string &rule_name, int rate, int burst, uint64_t error_code = 0)
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

struct DebtAwareEviction {
  template <typename Data>
  bool
  operator()(Data const &data) const
  {
    return data.is_evictable();
  }
};

// Table type aliases
using TxnTable  = ts::UdiTable<swoc::IPAddr, TxnData, std::hash<swoc::IPAddr>, DebtAwareEviction>;
using ConnTable = ts::UdiTable<swoc::IPAddr, ConnData, std::hash<swoc::IPAddr>, DebtAwareEviction>;
using H2Table   = ts::UdiTable<swoc::IPAddr, H2Data, std::hash<swoc::IPAddr>, DebtAwareEviction>;

} // namespace abuse_shield
