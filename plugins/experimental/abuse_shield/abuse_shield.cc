/** @file

  Abuse Shield Plugin - HTTP/2 error tracking and IP-based abuse detection.

  Uses the Udi "King of the Hill" algorithm for efficient, bounded-memory IP tracking.

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

#include <chrono>
#include <cinttypes>
#include <ctime>
#include <iomanip>
#include <memory>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unistd.h>

#include "ts/ts.h"
#include "swoc/IPRange.h"
#include "swoc/BufferWriter.h"
#include "swoc/bwf_ip.h"

#include "config.h"
#include "ip_data.h"
#include "stats.h"
#include "logging.h"

namespace
{
using abuse_shield::dbg_ctl;
using abuse_shield::PLUGIN_NAME;

// Optional log file for LOG action output.
TSTextLogObject g_log_object = nullptr;

// Global action stats.
abuse_shield::ActionStats g_action_stats;

// Per-tracker stats.
abuse_shield::TrackerStats g_txn_stats;
abuse_shield::TrackerStats g_conn_stats;
abuse_shield::TrackerStats g_h2_stats;

// Helper to convert IPAddr to string.
std::string
ip_to_string(const swoc::IPAddr &ip)
{
  swoc::LocalBufferWriter<64> writer;
  writer.print("{}", ip);
  return std::string(writer.view());
}

// Helper to format blocked_until as human-readable string.
// Returns "-" if not blocked, or "HH:MM:SS (Xs left)" if blocked.
std::string
format_blocked_time(uint64_t blocked_until)
{
  if (blocked_until == 0) {
    return "-";
  }

  uint64_t now = abuse_shield::now_ms();
  if (now >= blocked_until) {
    return "-"; // Block has expired
  }

  int64_t remaining_ms  = static_cast<int64_t>(blocked_until - now);
  int64_t remaining_sec = remaining_ms / 1000;

  // Convert steady_clock blocked_until to wall clock time for display.
  // Note: This is approximate since steady_clock and system_clock can drift.
  auto steady_now   = std::chrono::steady_clock::now();
  auto system_now   = std::chrono::system_clock::now();
  auto steady_ms    = std::chrono::duration_cast<std::chrono::milliseconds>(steady_now.time_since_epoch()).count();
  auto delta_ms     = static_cast<int64_t>(blocked_until) - steady_ms;
  auto block_time   = system_now + std::chrono::milliseconds(delta_ms);
  auto block_time_t = std::chrono::system_clock::to_time_t(block_time);

  std::ostringstream oss;
  oss << std::put_time(std::localtime(&block_time_t), "%H:%M:%S");
  oss << " (" << remaining_sec << "s left)";
  return oss.str();
}

// Helper to get current wall clock time as string.
std::string
current_time_str()
{
  auto               now   = std::chrono::system_clock::now();
  auto               now_t = std::chrono::system_clock::to_time_t(now);
  std::ostringstream oss;
  oss << std::put_time(std::localtime(&now_t), "%Y-%m-%dT%H:%M:%S");
  return oss.str();
}

// ============================================================================
// Global state
// ============================================================================

// Separate UDI tables for different event types, each with its own data type.
std::unique_ptr<abuse_shield::TxnTable>  g_txn_tracker;  ///< Transaction/request rate tracking
std::unique_ptr<abuse_shield::ConnTable> g_conn_tracker; ///< Connection rate tracking
std::unique_ptr<abuse_shield::H2Table>   g_h2_tracker;   ///< HTTP/2 error tracking

std::shared_ptr<abuse_shield::Config> g_config;
std::shared_mutex                     g_config_mutex; // Protects g_config pointer swaps

// Sync the metrics from a single Udi table to its stats.
void
sync_tracker_stats(abuse_shield::TxnTable *tracker, abuse_shield::TrackerStats &stats)
{
  if (tracker) {
    TSStatIntSet(stats.slots_used, static_cast<int64_t>(tracker->slots_used()));
    TSStatIntSet(stats.contests, static_cast<int64_t>(tracker->contests()));
    TSStatIntSet(stats.contests_won, static_cast<int64_t>(tracker->contests_won()));
    TSStatIntSet(stats.evictions, static_cast<int64_t>(tracker->evictions()));
  }
}

void
sync_tracker_stats(abuse_shield::ConnTable *tracker, abuse_shield::TrackerStats &stats)
{
  if (tracker) {
    TSStatIntSet(stats.slots_used, static_cast<int64_t>(tracker->slots_used()));
    TSStatIntSet(stats.contests, static_cast<int64_t>(tracker->contests()));
    TSStatIntSet(stats.contests_won, static_cast<int64_t>(tracker->contests_won()));
    TSStatIntSet(stats.evictions, static_cast<int64_t>(tracker->evictions()));
  }
}

void
sync_tracker_stats(abuse_shield::H2Table *tracker, abuse_shield::TrackerStats &stats)
{
  if (tracker) {
    TSStatIntSet(stats.slots_used, static_cast<int64_t>(tracker->slots_used()));
    TSStatIntSet(stats.contests, static_cast<int64_t>(tracker->contests()));
    TSStatIntSet(stats.contests_won, static_cast<int64_t>(tracker->contests_won()));
    TSStatIntSet(stats.evictions, static_cast<int64_t>(tracker->evictions()));
  }
}

// Sync the metrics from all Udi tables.
void
sync_all_tracker_stats()
{
  sync_tracker_stats(g_txn_tracker.get(), g_txn_stats);
  sync_tracker_stats(g_conn_tracker.get(), g_conn_stats);
  sync_tracker_stats(g_h2_tracker.get(), g_h2_stats);
}

// ============================================================================
// Rule evaluation
// ============================================================================

/** Check if request rate limit is exceeded (token bucket).
 * @return True if rate exceeded (tokens < 0), false otherwise.
 */
bool
check_req_rate(const abuse_shield::RuleFilter &filter, const swoc::IPAddr &ip)
{
  if (filter.max_req_rate > 0 && g_txn_tracker) {
    auto slot = g_txn_tracker->find(ip);
    if (slot && slot->tokens.load(std::memory_order_relaxed) < 0) {
      return true; // Rate exceeded
    }
  }
  return false;
}

/** Check if connection rate limit is exceeded (token bucket).
 * @return True if rate exceeded (tokens < 0), false otherwise.
 */
bool
check_conn_rate(const abuse_shield::RuleFilter &filter, const swoc::IPAddr &ip)
{
  if (filter.max_conn_rate > 0 && g_conn_tracker) {
    auto slot = g_conn_tracker->find(ip);
    if (slot && slot->tokens.load(std::memory_order_relaxed) < 0) {
      return true; // Rate exceeded
    }
  }
  return false;
}

/** Check if H2 error rate limit is exceeded (token bucket).
 * @return True if rate exceeded (tokens < 0), false otherwise.
 */
bool
check_h2_rate(const abuse_shield::RuleFilter &filter, const swoc::IPAddr &ip)
{
  if (filter.max_h2_error_rate > 0 && g_h2_tracker) {
    auto slot = g_h2_tracker->find(ip);
    if (slot && slot->tokens.load(std::memory_order_relaxed) < 0) {
      return true; // Rate exceeded
    }
  }
  return false;
}

/** Check if a rule's filter criteria match for the given IP.
 *
 * A rule matches only if ALL enabled filter criteria are satisfied (AND logic).
 * Each filter uses token bucket rate limiting - a rate is exceeded when tokens < 0.
 * If no filter criteria are enabled, the rule does NOT match (no action needed).
 *
 * @param[in] rule The rule containing filter criteria to check.
 * @param[in] ip The IP address to evaluate.
 * @return True if all enabled filter criteria are satisfied, false otherwise.
 */
bool
rule_matches(const abuse_shield::Rule &rule, const swoc::IPAddr &ip)
{
  const auto &f = rule.filter;

  // If no filters are enabled, rule doesn't match
  if (f.max_req_rate == 0 && f.max_conn_rate == 0 && f.max_h2_error_rate == 0) {
    return false;
  }

  // All enabled filters must match (rate exceeded) - AND logic
  if (f.max_req_rate > 0 && !check_req_rate(f, ip)) {
    return false;
  }
  if (f.max_conn_rate > 0 && !check_conn_rate(f, ip)) {
    return false;
  }
  if (f.max_h2_error_rate > 0 && !check_h2_rate(f, ip)) {
    return false;
  }

  return true; // All enabled filters matched
}

abuse_shield::RuleMatch
evaluate_rules(const swoc::IPAddr &ip, const abuse_shield::Config &config)
{
  for (const auto &rule : config.rules()) {
    if (rule_matches(rule, ip)) {
      Dbg(dbg_ctl, "Rule matched: %s", rule.name.c_str());
      return abuse_shield::RuleMatch{&rule, rule.actions};
    }
  }
  return abuse_shield::RuleMatch{};
}

// ============================================================================
// Action execution
// ============================================================================

/** Block an IP across all trackers. */
void
block_ip(const swoc::IPAddr &ip, uint64_t until_ms)
{
  if (g_txn_tracker) {
    auto slot = g_txn_tracker->find(ip);
    if (slot) {
      slot->block_until(until_ms);
    }
  }
  if (g_conn_tracker) {
    auto slot = g_conn_tracker->find(ip);
    if (slot) {
      slot->block_until(until_ms);
    }
  }
  if (g_h2_tracker) {
    auto slot = g_h2_tracker->find(ip);
    if (slot) {
      slot->block_until(until_ms);
    }
  }
}

/** Execute the log action while respecting the log rate limit.
 *
 * @param[in] match The rule match result.
 * @param[in] ip The client IP address.
 * @param[in] config The current configuration.
 */
void
execute_log_action(const abuse_shield::RuleMatch &match, const swoc::IPAddr &ip, const abuse_shield::Config &config)
{
  uint64_t log_interval_ms = static_cast<uint64_t>(config.log_interval_sec()) * 1000;
  uint64_t now             = abuse_shield::now_ms();

  // Find the most recent log time across all trackers for this IP.
  uint64_t most_recent_log = 0;
  auto     txn_slot        = g_txn_tracker ? g_txn_tracker->find(ip) : nullptr;
  auto     conn_slot       = g_conn_tracker ? g_conn_tracker->find(ip) : nullptr;
  auto     h2_slot         = g_h2_tracker ? g_h2_tracker->find(ip) : nullptr;

  if (txn_slot) {
    most_recent_log = std::max(most_recent_log, txn_slot->last_logged.load(std::memory_order_relaxed));
  }
  if (conn_slot) {
    most_recent_log = std::max(most_recent_log, conn_slot->last_logged.load(std::memory_order_relaxed));
  }
  if (h2_slot) {
    most_recent_log = std::max(most_recent_log, h2_slot->last_logged.load(std::memory_order_relaxed));
  }

  // Only log if enough time has passed since the last log for this IP.
  if (now - most_recent_log < log_interval_ms) {
    return;
  }

  // Try to claim the log opportunity atomically using the first available slot.
  bool     claimed  = false;
  uint64_t expected = 0;

  if (txn_slot) {
    expected = txn_slot->last_logged.load(std::memory_order_relaxed);
    if (now - expected >= log_interval_ms) {
      claimed = txn_slot->last_logged.compare_exchange_weak(expected, now, std::memory_order_relaxed);
    }
  } else if (conn_slot) {
    expected = conn_slot->last_logged.load(std::memory_order_relaxed);
    if (now - expected >= log_interval_ms) {
      claimed = conn_slot->last_logged.compare_exchange_weak(expected, now, std::memory_order_relaxed);
    }
  } else if (h2_slot) {
    expected = h2_slot->last_logged.load(std::memory_order_relaxed);
    if (now - expected >= log_interval_ms) {
      claimed = h2_slot->last_logged.compare_exchange_weak(expected, now, std::memory_order_relaxed);
    }
  }

  if (!claimed) {
    return;
  }

  // Update last_logged in all slots.
  if (txn_slot) {
    txn_slot->last_logged.store(now, std::memory_order_relaxed);
  }
  if (conn_slot) {
    conn_slot->last_logged.store(now, std::memory_order_relaxed);
  }
  if (h2_slot) {
    h2_slot->last_logged.store(now, std::memory_order_relaxed);
  }

  TSStatIntIncrement(g_action_stats.actions_logged, 1);

  // Get token state for logging.
  int32_t req_tokens  = txn_slot ? txn_slot->tokens.load(std::memory_order_relaxed) : 0;
  int32_t conn_tokens = conn_slot ? conn_slot->tokens.load(std::memory_order_relaxed) : 0;
  int32_t h2_tokens   = h2_slot ? h2_slot->tokens.load(std::memory_order_relaxed) : 0;

  // Write to log file if configured, otherwise use TSError.
  if (g_log_object) {
    TSTextLogObjectWrite(g_log_object, "Rule \"%s\" matched for IP=%s: actions=[%s] req_tokens=%d conn_tokens=%d h2_tokens=%d",
                         match.rule->name.c_str(), ip_to_string(ip).c_str(), abuse_shield::actions_to_string(match.actions).c_str(),
                         req_tokens, conn_tokens, h2_tokens);
  } else {
    TSError("[%s] Rule \"%s\" matched for IP=%s: actions=[%s] req_tokens=%d conn_tokens=%d h2_tokens=%d", PLUGIN_NAME,
            match.rule->name.c_str(), ip_to_string(ip).c_str(), abuse_shield::actions_to_string(match.actions).c_str(), req_tokens,
            conn_tokens, h2_tokens);
  }
}

/** Execute actions for a matched rule.
 *
 * @param[in] match The rule match result.
 * @param[in] ip The client IP address.
 * @param[in] vconn The virtual connection (for close action).
 * @param[in] config The current configuration.
 */
void
execute_actions(const abuse_shield::RuleMatch &match, const swoc::IPAddr &ip, TSVConn vconn, const abuse_shield::Config &config)
{
  TSStatIntIncrement(g_action_stats.rules_matched, 1);

  if (abuse_shield::has_action(match.actions, abuse_shield::Action::BLOCK)) {
    TSStatIntIncrement(g_action_stats.actions_blocked, 1);
    uint64_t block_until = abuse_shield::now_ms() + (config.block_duration_sec() * 1000);
    block_ip(ip, block_until);
    Dbg(dbg_ctl, "Blocking IP %s for %d seconds (rule: %s)", ip_to_string(ip).c_str(), config.block_duration_sec(),
        match.rule->name.c_str());
  }

  if (abuse_shield::has_action(match.actions, abuse_shield::Action::CLOSE)) {
    TSStatIntIncrement(g_action_stats.actions_closed, 1);
    int fd = TSVConnFdGet(vconn);
    if (fd >= 0) {
      shutdown(fd, SHUT_RDWR);
      // Drain any pending data to ensure clean shutdown.
      char buffer[4096];
      while (read(fd, buffer, sizeof(buffer)) > 0) {
        // Drain pending data.
      }
      Dbg(dbg_ctl, "Closing connection from %s (rule: %s)", ip_to_string(ip).c_str(), match.rule->name.c_str());
    }
  }

  // Log if logging is configured (independent of block/close actions).
  if (abuse_shield::has_action(match.actions, abuse_shield::Action::LOG)) {
    execute_log_action(match, ip, config);
  }
}

// ============================================================================
// Hook handlers
// ============================================================================

// Helper struct for error info.
struct H2Errors {
  uint32_t cls{0};  ///< Error class (1 = connection, 2 = stream)
  uint64_t code{0}; ///< HTTP/2 error code
};

/** Process HTTP/2 response errors.
 *
 * Tracks HTTP/2 stream and connection errors using token bucket rate limiting.
 *
 * @param[in] txnp The transaction being closed.
 * @param[in] vconn The virtual connection.
 * @param[in] ip The client IP address.
 * @param[in] config The current configuration.
 */
void
process_h2_response(TSHttpTxn txnp, TSVConn vconn, const swoc::IPAddr &ip, const abuse_shield::Config &config)
{
  if (!g_h2_tracker) {
    return;
  }

  // Get HTTP/2 errors.
  H2Errors received_error; // Client received (stream error)
  H2Errors sent_error;     // Client sent (connection error)
  TSHttpTxnClientReceivedErrorGet(txnp, &received_error.cls, &received_error.code);
  TSHttpTxnClientSentErrorGet(txnp, &sent_error.cls, &sent_error.code);

  // Check for HTTP/2 errors.
  bool     has_h2_error = false;
  uint64_t error_code   = 0;

  // Stream-level error (class 2).
  if (received_error.cls == 2 && received_error.code != 0) {
    has_h2_error = true;
    error_code   = received_error.code;
    Dbg(dbg_ctl, "Stream error from %s: code=%" PRIu64, ip_to_string(ip).c_str(), error_code);
  }

  // Connection-level error (class 1).
  if (sent_error.cls == 1 && sent_error.code != 0) {
    has_h2_error = true;
    error_code   = sent_error.code;
    Dbg(dbg_ctl, "Connection error from %s: code=%" PRIu64, ip_to_string(ip).c_str(), error_code);
  }

  if (!has_h2_error) {
    return;
  }

  // Find the minimum H2 error rate (strictest rule) so that all rules can trigger.
  int    min_h2_rate       = 0;
  double min_h2_multiplier = 1.0;
  for (const auto &rule : config.rules()) {
    if (rule.filter.max_h2_error_rate > 0) {
      if (min_h2_rate == 0 || rule.filter.max_h2_error_rate < min_h2_rate) {
        min_h2_rate       = rule.filter.max_h2_error_rate;
        min_h2_multiplier = rule.filter.h2_burst_multiplier;
      }
    }
  }
  if (min_h2_rate == 0) {
    // No rules configured for H2 error rate limiting, so just return. There's no tracking needed.
    return;
  }

  // Calculate burst capacity from rate * multiplier.
  int min_h2_burst = static_cast<int>(min_h2_rate * min_h2_multiplier);

  // Record HTTP/2 error via token bucket.
  auto h2_slot = g_h2_tracker->process_event(ip, 1);
  if (h2_slot) {
    TSStatIntIncrement(g_h2_stats.events, 1);
    h2_slot->consume(min_h2_rate, min_h2_burst, static_cast<uint8_t>(error_code));

    // Evaluate rules
    abuse_shield::RuleMatch match = evaluate_rules(ip, config);
    if (match.actions != 0) {
      execute_actions(match, ip, vconn, config);
    }
  }
}

// Called at connection start to block known abusive IPs.
int
handle_vconn_start(TSCont /* contp */, TSEvent /* event */, void *edata)
{
  TSVConn vconn = static_cast<TSVConn>(edata);

  // Get config with shared lock.
  std::shared_ptr<abuse_shield::Config> config;
  {
    std::shared_lock lock(g_config_mutex);
    config = g_config;
  }

  if (!config || !config->enabled() || !g_conn_tracker) {
    TSVConnReenable(vconn);
    return TS_SUCCESS;
  }

  // Get client IP.
  sockaddr const *client_addr = TSNetVConnRemoteAddrGet(vconn);
  if (!client_addr) {
    TSError("[%s] TSNetVConnRemoteAddrGet failed to retrieve client IP", PLUGIN_NAME);
    TSVConnReenable(vconn);
    return TS_SUCCESS;
  }

  swoc::IPAddr ip(client_addr);

  // Check if trusted - skip all abuse checking for trusted IPs.
  if (config->trusted_ips().find(ip) != config->trusted_ips().end()) {
    Dbg(dbg_ctl, "Skipping trusted IP: %s", ip_to_string(ip).c_str());
    TSVConnReenable(vconn);
    return TS_SUCCESS;
  }

  // Find the minimum connection rate (strictest rule) so that all rules can trigger.
  int    min_conn_rate       = 0;
  double min_conn_multiplier = 1.0;
  for (const auto &rule : config->rules()) {
    if (rule.filter.max_conn_rate > 0) {
      if (min_conn_rate == 0 || rule.filter.max_conn_rate < min_conn_rate) {
        min_conn_rate       = rule.filter.max_conn_rate;
        min_conn_multiplier = rule.filter.conn_burst_multiplier;
      }
    }
  }

  // Calculate burst capacity from rate * multiplier.
  int min_conn_burst = static_cast<int>(min_conn_rate * min_conn_multiplier);

  // Check if IP is currently blocked (from any tracker).
  auto h2_slot   = g_h2_tracker ? g_h2_tracker->find(ip) : nullptr;
  auto conn_slot = g_conn_tracker->find(ip);
  auto txn_slot  = g_txn_tracker ? g_txn_tracker->find(ip) : nullptr;

  bool is_blocked =
    (h2_slot && h2_slot->is_blocked()) || (conn_slot && conn_slot->is_blocked()) || (txn_slot && txn_slot->is_blocked());

  if (is_blocked) {
    Dbg(dbg_ctl, "Blocking connection from %s (blocked IP)", ip_to_string(ip).c_str());
    TSStatIntIncrement(g_action_stats.connections_rejected, 1);

    int fd = TSVConnFdGet(vconn);
    if (fd >= 0) {
      shutdown(fd, SHUT_RDWR);
      char buffer[4096];
      while (read(fd, buffer, sizeof(buffer)) > 0) {
        // Drain pending data.
      }
    }
  }

  // Record connection event via token bucket.
  if (min_conn_rate > 0) {
    auto slot = g_conn_tracker->process_event(ip, 1);
    if (slot) {
      slot->consume(min_conn_rate, min_conn_burst);
      TSStatIntIncrement(g_conn_stats.events, 1);
    }
  }

  // Keep stats current for external queries.
  sync_all_tracker_stats();

  TSVConnReenable(vconn);
  return TS_SUCCESS;
}

// Unified handler for transaction start and close events.
int
handle_txn_event(TSCont /* contp */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  // Get config with shared lock.
  std::shared_ptr<abuse_shield::Config> config;
  {
    std::shared_lock lock(g_config_mutex);
    config = g_config;
  }

  if (!config || !config->enabled()) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  // Get client IP from session.
  TSHttpSsn ssn = TSHttpTxnSsnGet(txnp);
  if (!ssn) {
    Dbg(dbg_ctl, "TSHttpTxnSsnGet returned NULL");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }
  TSVConn vconn = TSHttpSsnClientVConnGet(ssn);
  if (!vconn) {
    Dbg(dbg_ctl, "TSHttpSsnClientVConnGet returned NULL");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }
  sockaddr const *client_addr = TSNetVConnRemoteAddrGet(vconn);
  if (!client_addr) {
    Dbg(dbg_ctl, "TSNetVConnRemoteAddrGet returned NULL");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  swoc::IPAddr ip(client_addr);

  // Check if trusted.
  if (config->trusted_ips().find(ip) != config->trusted_ips().end()) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    // Transaction start: consume request token.
    if (g_txn_tracker) {
      // Find the minimum request rate (strictest rule) so that all rules can trigger.
      // Using the minimum ensures tokens go negative when the strictest rule is violated.
      int    min_req_rate       = 0;
      double min_req_multiplier = 1.0;
      for (const auto &rule : config->rules()) {
        if (rule.filter.max_req_rate > 0) {
          if (min_req_rate == 0 || rule.filter.max_req_rate < min_req_rate) {
            min_req_rate       = rule.filter.max_req_rate;
            min_req_multiplier = rule.filter.req_burst_multiplier;
          }
        }
      }

      // Calculate burst capacity from rate * multiplier.
      int min_req_burst = static_cast<int>(min_req_rate * min_req_multiplier);

      if (min_req_rate > 0) {
        auto slot = g_txn_tracker->process_event(ip, 1);
        if (slot) {
          slot->consume(min_req_rate, min_req_burst);
          TSStatIntIncrement(g_txn_stats.events, 1);
        }
      }
      sync_all_tracker_stats();
    }
    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    // Process any HTTP/2 errors.
    process_h2_response(txnp, vconn, ip, *config);

    // Evaluate rules on this IP
    {
      abuse_shield::RuleMatch match = evaluate_rules(ip, *config);
      if (match.actions != 0) {
        execute_actions(match, ip, vconn, *config);
      }
    }

    sync_all_tracker_stats();
    break;

  default:
    TSError("[%s] Unknown event in handle_txn_event: %d", PLUGIN_NAME, event);
    break;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

// Dump transaction tracker.
std::string
dump_txn_tracker()
{
  if (!g_txn_tracker) {
    return "";
  }

  auto format_entry = [](const swoc::IPAddr &ip, uint32_t score,
                         const std::shared_ptr<abuse_shield::TxnData> &data) -> std::string {
    swoc::LocalBufferWriter<64> ip_str;
    ip_str.print("{}", ip);

    uint64_t blocked_until = data->blocked_until.load(std::memory_order_relaxed);

    std::ostringstream oss;
    oss << std::left << std::setw(40) << std::string(ip_str.view()) << "\ttokens=" << std::setw(6)
        << data->tokens.load(std::memory_order_relaxed) << "\tcount=" << std::setw(6) << data->count.load(std::memory_order_relaxed)
        << "\tscore=" << std::setw(6) << score << "\tblocked=" << format_blocked_time(blocked_until) << "\n";
    return oss.str();
  };

  std::ostringstream oss;
  oss << "# Transaction (Request) tracker\n";
  oss << "# slots_used: " << g_txn_tracker->slots_used() << " / " << g_txn_tracker->num_slots() << "\n";
  oss << "# contests: " << g_txn_tracker->contests() << " (won: " << g_txn_tracker->contests_won() << ")\n";
  oss << "# evictions: " << g_txn_tracker->evictions() << "\n";
  oss << g_txn_tracker->dump(format_entry);
  oss << "\n";

  return oss.str();
}

// Dump connection tracker.
std::string
dump_conn_tracker()
{
  if (!g_conn_tracker) {
    return "";
  }

  auto format_entry = [](const swoc::IPAddr &ip, uint32_t score,
                         const std::shared_ptr<abuse_shield::ConnData> &data) -> std::string {
    swoc::LocalBufferWriter<64> ip_str;
    ip_str.print("{}", ip);

    uint64_t blocked_until = data->blocked_until.load(std::memory_order_relaxed);

    std::ostringstream oss;
    oss << std::left << std::setw(40) << std::string(ip_str.view()) << "\ttokens=" << std::setw(6)
        << data->tokens.load(std::memory_order_relaxed) << "\tcount=" << std::setw(6) << data->count.load(std::memory_order_relaxed)
        << "\tscore=" << std::setw(6) << score << "\tblocked=" << format_blocked_time(blocked_until) << "\n";
    return oss.str();
  };

  std::ostringstream oss;
  oss << "# Connection tracker\n";
  oss << "# slots_used: " << g_conn_tracker->slots_used() << " / " << g_conn_tracker->num_slots() << "\n";
  oss << "# contests: " << g_conn_tracker->contests() << " (won: " << g_conn_tracker->contests_won() << ")\n";
  oss << "# evictions: " << g_conn_tracker->evictions() << "\n";
  oss << g_conn_tracker->dump(format_entry);
  oss << "\n";

  return oss.str();
}

// Dump H2 error tracker.
std::string
dump_h2_tracker()
{
  if (!g_h2_tracker) {
    return "";
  }

  auto format_entry = [](const swoc::IPAddr &ip, uint32_t score, const std::shared_ptr<abuse_shield::H2Data> &data) -> std::string {
    swoc::LocalBufferWriter<64> ip_str;
    ip_str.print("{}", ip);

    uint64_t blocked_until = data->blocked_until.load(std::memory_order_relaxed);

    std::ostringstream oss;
    oss << std::left << std::setw(40) << std::string(ip_str.view()) << "\ttokens=" << std::setw(6)
        << data->tokens.load(std::memory_order_relaxed) << "\tcount=" << std::setw(6) << data->count.load(std::memory_order_relaxed)
        << "\tscore=" << std::setw(6) << score << "\tblocked=" << std::setw(24) << format_blocked_time(blocked_until);

    // Show per-error-code counts if any
    bool has_errors = false;
    for (size_t i = 0; i < abuse_shield::NUM_H2_ERROR_CODES; ++i) {
      uint16_t cnt = data->error_codes[i].load(std::memory_order_relaxed);
      if (cnt > 0) {
        if (!has_errors) {
          oss << "\terrors=[";
          has_errors = true;
        } else {
          oss << ",";
        }
        oss << i << ":" << cnt;
      }
    }
    if (has_errors) {
      oss << "]";
    }
    oss << "\n";
    return oss.str();
  };

  std::ostringstream oss;
  oss << "# H2 Error tracker\n";
  oss << "# slots_used: " << g_h2_tracker->slots_used() << " / " << g_h2_tracker->num_slots() << "\n";
  oss << "# contests: " << g_h2_tracker->contests() << " (won: " << g_h2_tracker->contests_won() << ")\n";
  oss << "# evictions: " << g_h2_tracker->evictions() << "\n";
  oss << g_h2_tracker->dump(format_entry);
  oss << "\n";

  return oss.str();
}

// Dump all tracked IPs to a string for debugging.
std::string
dump_tracker()
{
  std::shared_ptr<abuse_shield::Config> config;
  {
    std::shared_lock lock(g_config_mutex);
    config = g_config;
  }

  std::ostringstream header;
  header << "# abuse_shield dump (token bucket rate limiting)\n";
  header << "# Current time: " << current_time_str() << " (now_ms=" << abuse_shield::now_ms() << ")\n";
  if (config) {
    header << "# Block duration: " << config->block_duration_sec() << "s\n";
    header << "# Trusted IPs loaded: " << config->trusted_ips().count() << " ranges\n";
  }
  header << "# Negative tokens indicate rate exceeded\n\n";

  std::string result  = header.str();
  result             += dump_txn_tracker();
  result             += dump_conn_tracker();
  result             += dump_h2_tracker();

  return result;
}

// Handle plugin messages for dynamic config reload and data dump.
int
handle_lifecycle_msg(TSCont /* contp */, TSEvent /* event */, void *edata)
{
  TSPluginMsg *msg = static_cast<TSPluginMsg *>(edata);

  std::string_view tag(msg->tag, strlen(msg->tag));

  if (tag == "abuse_shield.reload") {
    std::string config_path;
    {
      std::shared_lock lock(g_config_mutex);
      if (g_config) {
        config_path = g_config->config_path();
      }
    }
    Dbg(dbg_ctl, "Reloading configuration from %s", config_path.c_str());

    auto new_config = abuse_shield::Config::parse(config_path);
    if (new_config) {
      // Validate the new configuration before applying.
      std::string validation_error;
      if (!new_config->validate(validation_error)) {
        TSError("[%s] Configuration reload rejected: %s. Keeping current configuration.", PLUGIN_NAME, validation_error.c_str());
      } else {
        new_config->set_config_path(config_path);
        std::unique_lock lock(g_config_mutex);
        g_config = new_config;
        TSNote("[%s] Configuration reloaded successfully", PLUGIN_NAME);
      }
    } else {
      TSError("[%s] Configuration reload failed", PLUGIN_NAME);
    }
  } else if (tag == "abuse_shield.dump") {
    sync_all_tracker_stats();
    std::string dump = dump_tracker();
    TSNote("[%s] Dump:\n%s", PLUGIN_NAME, dump.c_str());
  } else if (tag == "abuse_shield.stats") {
    sync_all_tracker_stats();
    TSNote("[%s] Stats synced", PLUGIN_NAME);
  } else if (tag == "abuse_shield.reset") {
    if (g_txn_tracker) {
      g_txn_tracker->reset_metrics();
    }
    if (g_conn_tracker) {
      g_conn_tracker->reset_metrics();
    }
    if (g_h2_tracker) {
      g_h2_tracker->reset_metrics();
    }
    // Reset action stats.
    TSStatIntSet(g_action_stats.rules_matched, 0);
    TSStatIntSet(g_action_stats.actions_blocked, 0);
    TSStatIntSet(g_action_stats.actions_closed, 0);
    TSStatIntSet(g_action_stats.actions_logged, 0);
    TSStatIntSet(g_action_stats.connections_rejected, 0);
    TSNote("[%s] Metrics reset", PLUGIN_NAME);
  } else if (tag == "abuse_shield.enabled") {
    if (msg->data_size > 0) {
      bool             enabled = (static_cast<const char *>(msg->data)[0] == '1');
      std::unique_lock lock(g_config_mutex);
      if (g_config) {
        g_config->set_enabled(enabled);
        TSNote("[%s] Plugin %s", PLUGIN_NAME, enabled ? "enabled" : "disabled");
      }
    }
  } else if (tag == "abuse_shield.trusted") {
    std::shared_lock lock(g_config_mutex);
    if (g_config) {
      std::ostringstream oss;
      oss << "Trusted IP ranges (" << g_config->trusted_ips().count() << " total):\n";
      for (auto const &[range, flag] : g_config->trusted_ips()) {
        swoc::LocalBufferWriter<64> w;
        w.print("{}", range);
        oss << "  " << w.view() << "\n";
      }
      TSNote("[%s] %s", PLUGIN_NAME, oss.str().c_str());
    }
  }

  return TS_SUCCESS;
}

} // anonymous namespace

// ============================================================================
// Plugin initialization
// ============================================================================

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  // Parse command line arguments.
  if (argc < 2) {
    TSError("[%s] Usage: abuse_shield.so <config_file>", PLUGIN_NAME);
    return;
  }

  std::string config_path = argv[1];

  // If path is relative, make it relative to config dir.
  if (config_path[0] != '/') {
    config_path = std::string(TSConfigDirGet()) + "/" + config_path;
  }

  // Load configuration.
  g_config = abuse_shield::Config::parse(config_path);
  if (!g_config) {
    TSFatal("[%s] Failed to load configuration from %s", PLUGIN_NAME, config_path.c_str());
    return;
  }

  // Validate configuration - fatal error if invalid at startup.
  std::string validation_error;
  if (!g_config->validate(validation_error)) {
    TSFatal("[%s] Invalid configuration: %s", PLUGIN_NAME, validation_error.c_str());
    return;
  }

  g_config->set_config_path(config_path);

  // Create optional log file for LOG action output.
  if (!g_config->log_file().empty()) {
    if (TSTextLogObjectCreate(g_config->log_file().c_str(), TS_LOG_MODE_ADD_TIMESTAMP, &g_log_object) != TS_SUCCESS) {
      TSError("[%s] Failed to create log file: %s", PLUGIN_NAME, g_config->log_file().c_str());
      g_log_object = nullptr;
    } else {
      Dbg(dbg_ctl, "Created log file: %s", g_config->log_file().c_str());
    }
  }

  // Create the IP tracker tables - one for each event type with its own data type.
  g_txn_tracker  = std::make_unique<abuse_shield::TxnTable>(g_config->slots());
  g_conn_tracker = std::make_unique<abuse_shield::ConnTable>(g_config->slots());
  g_h2_tracker   = std::make_unique<abuse_shield::H2Table>(g_config->slots());
  Dbg(dbg_ctl, "Created 3 IP trackers with %zu slots each (token bucket rate limiting)", g_config->slots());

  // Initialize stats - separate stats for each tracker plus global action stats.
  g_action_stats.init();
  g_txn_stats.init("txn");
  g_conn_stats.init("conn");
  g_h2_stats.init("h2");

  // Register hooks.
  // VCONN_START: Earliest hook for all TCP connections (both HTTP and HTTPS).
  // Blocked IPs are rejected here before any protocol negotiation.
  TSCont vconn_cont = TSContCreate(handle_vconn_start, nullptr);
  TSHttpHookAdd(TS_VCONN_START_HOOK, vconn_cont);

  // TXN_START/CLOSE: Transaction-level hooks for rate limiting and rule evaluation.
  TSCont txn_cont = TSContCreate(handle_txn_event, nullptr);
  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, txn_cont);
  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, txn_cont);

  TSCont msg_cont = TSContCreate(handle_lifecycle_msg, nullptr);
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, msg_cont);

  TSNote("[%s] Plugin initialized with %zu slots per tracker, %zu rules", PLUGIN_NAME, g_config->slots(), g_config->rules().size());
}
