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

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstring>
#include <ctime>
#include <exception>
#include <iomanip>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <sys/socket.h>

#include "ts/ts.h"
#include "swoc/IPRange.h"
#include "swoc/BufferWriter.h"
#include "swoc/bwf_ip.h"

#include "config.h"
#include "fingerprint.h"
#include "fingerprint_registry.h"
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

// Named JAx VConn user-arg selected by global.fingerprint_registry.
int g_fingerprint_registry_index = -1;

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

std::string
format_local_time(std::time_t time, const char *format)
{
  struct tm time_parts;

  if (localtime_r(&time, &time_parts) == nullptr) {
    return "-";
  }

  std::ostringstream oss;
  oss << std::put_time(&time_parts, format);
  return oss.str();
}

// Helper to get current wall clock time as string.
std::string
current_time_str()
{
  auto now   = std::chrono::system_clock::now();
  auto now_t = std::chrono::system_clock::to_time_t(now);
  return format_local_time(now_t, "%Y-%m-%dT%H:%M:%S");
}

// ============================================================================
// Global state
// ============================================================================

// Separate UDI tables for different event types, each with its own data type.
std::unique_ptr<abuse_shield::TxnTable>  g_txn_tracker;  ///< Transaction/request rate tracking
std::unique_ptr<abuse_shield::ConnTable> g_conn_tracker; ///< Connection rate tracking
std::unique_ptr<abuse_shield::H2Table>   g_h2_tracker;   ///< HTTP/2 error tracking

/** Bounded block state which never evicts an unexpired block. */
class BlockedIpTable
{
public:
  explicit BlockedIpTable(size_t capacity) : capacity_(capacity) { blocked_.reserve(capacity); }

  bool
  block(const swoc::IPAddr &ip, uint64_t until_ms)
  {
    std::lock_guard lock(mutex_);
    auto            spot = blocked_.find(ip);
    if (spot != blocked_.end()) {
      spot->second = std::max(spot->second, until_ms);
      return true;
    }

    if (blocked_.size() >= capacity_) {
      uint64_t now = abuse_shield::now_ms();
      std::erase_if(blocked_, [now](auto const &item) { return item.second <= now; });
    }
    if (blocked_.size() >= capacity_) {
      return false;
    }

    blocked_.emplace(ip, until_ms);
    return true;
  }

  bool
  is_blocked(const swoc::IPAddr &ip)
  {
    std::lock_guard lock(mutex_);
    auto            spot = blocked_.find(ip);
    if (spot == blocked_.end()) {
      return false;
    }
    if (spot->second <= abuse_shield::now_ms()) {
      blocked_.erase(spot);
      return false;
    }
    return true;
  }

private:
  size_t                                     capacity_;
  std::mutex                                 mutex_;
  std::unordered_map<swoc::IPAddr, uint64_t> blocked_;
};

std::unique_ptr<BlockedIpTable> g_blocked_ips;

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

template <typename Table>
void
reset_tracker_stats(Table *tracker, abuse_shield::TrackerStats &stats)
{
  if (tracker) {
    tracker->reset_metrics();
  }
  TSStatIntSet(stats.events, 0);
  TSStatIntSet(stats.events_untracked, 0);
  TSStatIntSet(stats.scan_exhausted, 0);
  sync_tracker_stats(tracker, stats);
}

// ============================================================================
// Rule evaluation
// ============================================================================

int
metric_rate(const abuse_shield::RuleFilter &filter, abuse_shield::RateMetric metric)
{
  switch (metric) {
  case abuse_shield::RateMetric::REQUEST:
    return filter.max_req_rate;
  case abuse_shield::RateMetric::CONNECTION:
    return filter.max_conn_rate;
  case abuse_shield::RateMetric::H2_ERROR:
    return filter.max_h2_error_rate;
  }
  return 0;
}

double
metric_burst_multiplier(const abuse_shield::RuleFilter &filter, abuse_shield::RateMetric metric)
{
  switch (metric) {
  case abuse_shield::RateMetric::REQUEST:
    return filter.req_burst_multiplier;
  case abuse_shield::RateMetric::CONNECTION:
    return filter.conn_burst_multiplier;
  case abuse_shield::RateMetric::H2_ERROR:
    return filter.h2_burst_multiplier;
  }
  return 1.0;
}

template <typename Table>
bool
rate_exceeded(Table *tracker, const abuse_shield::Rule &rule, const swoc::IPAddr &ip)
{
  auto slot = tracker ? tracker->find(ip) : nullptr;
  return slot && slot->buckets.exceeded(rule.name);
}

template <typename Table>
void
consume_rule_buckets(Table *tracker, const swoc::IPAddr &ip, const abuse_shield::Config &config, abuse_shield::RateMetric metric,
                     abuse_shield::TrackerStats &stats, uint64_t error_code = 0)
{
  typename Table::data_ptr slot;
  bool                     consumed = false;

  for (const auto &rule : config.rules()) {
    int rate = metric_rate(rule.filter, metric);
    if (rate <= 0 || !config.rule_applies_to_ip(rule, ip)) {
      continue;
    }

    if (!slot) {
      typename Table::ProcessStatus status;
      slot = tracker->process_event(ip, 1, &status);
      if (!slot) {
        TSStatIntIncrement(stats.events_untracked, 1);
        if (status == Table::ProcessStatus::NO_CANDIDATE) {
          TSStatIntIncrement(stats.scan_exhausted, 1);
        }
        return;
      }
    }

    int burst = static_cast<int>(static_cast<double>(rate) * metric_burst_multiplier(rule.filter, metric));
    if constexpr (std::is_same_v<typename Table::data_type, abuse_shield::H2Data>) {
      slot->consume(rule.name, rate, burst, error_code);
    } else {
      slot->consume(rule.name, rate, burst);
    }
    consumed = true;
  }

  if (consumed) {
    TSStatIntIncrement(stats.events, 1);
  }
}

/** Check if a rule's filter criteria match for the given IP.
 *
 * A rule matches only if ALL enabled filter criteria are satisfied (AND logic).
 * Each filter uses token bucket rate limiting - a rate is exceeded when tokens < 0.
 * Fingerprint values and methods use OR logic with one another. The resulting
 * fingerprint criterion is ANDed with any enabled rate criteria.
 *
 * @param[in] rule The rule containing filter criteria to check.
 * @param[in] ip The IP address to evaluate.
 * @return True if all enabled filter criteria are satisfied, false otherwise.
 */
bool
rule_matches(const abuse_shield::Rule &rule, const swoc::IPAddr &ip, const abuse_shield::Config &config,
             const abuse_shield::FingerprintResults *fingerprints, std::string_view &matched_method,
             std::string_view &matched_fingerprint)
{
  const auto &f = rule.filter;

  if (!config.rule_applies_to_ip(rule, ip)) {
    return false;
  }

  if (f.max_req_rate == 0 && f.max_conn_rate == 0 && f.max_h2_error_rate == 0 && !f.has_fingerprints()) {
    return false;
  }

  if (f.has_fingerprints()) {
    if (!fingerprints) {
      return false;
    }

    bool fingerprint_matched = false;
    for (const auto &[method, configured_values] : f.fingerprints) {
      auto computed = fingerprints->find(method);
      if (computed != fingerprints->end() && configured_values.contains(computed->second)) {
        matched_method      = computed->first;
        matched_fingerprint = computed->second;
        fingerprint_matched = true;
        break;
      }
    }
    if (!fingerprint_matched) {
      return false;
    }
  }

  if (f.max_req_rate > 0 && !rate_exceeded(g_txn_tracker.get(), rule, ip)) {
    return false;
  }
  if (f.max_conn_rate > 0 && !rate_exceeded(g_conn_tracker.get(), rule, ip)) {
    return false;
  }
  if (f.max_h2_error_rate > 0 && !rate_exceeded(g_h2_tracker.get(), rule, ip)) {
    return false;
  }

  return true; // All enabled filters matched
}

abuse_shield::RuleMatch
evaluate_rate_rules(const swoc::IPAddr &ip, const abuse_shield::Config &config)
{
  for (const auto &rule : config.rules()) {
    if (rule.filter.has_fingerprints()) {
      continue;
    }

    std::string_view matched_method;
    std::string_view matched_fingerprint;
    if (rule_matches(rule, ip, config, nullptr, matched_method, matched_fingerprint)) {
      Dbg(dbg_ctl, "Rule matched: %s", rule.name.c_str());
      return abuse_shield::RuleMatch{&rule, rule.actions, {}, {}};
    }
  }
  return abuse_shield::RuleMatch{};
}

abuse_shield::RuleMatch
evaluate_fingerprint_rules(const swoc::IPAddr &ip, const abuse_shield::Config &config,
                           const abuse_shield::FingerprintResults &fingerprints)
{
  for (const auto &rule : config.rules()) {
    if (!rule.filter.has_fingerprints()) {
      continue;
    }

    std::string_view matched_method;
    std::string_view matched_fingerprint;
    if (rule_matches(rule, ip, config, &fingerprints, matched_method, matched_fingerprint)) {
      Dbg(dbg_ctl, "Fingerprint rule matched: %s", rule.name.c_str());
      return abuse_shield::RuleMatch{&rule, rule.actions, matched_method, matched_fingerprint};
    }
  }
  return abuse_shield::RuleMatch{};
}

// ============================================================================
// Action execution
// ============================================================================

/** Store block state independently from the evictable rate tables. */
bool
block_ip(const swoc::IPAddr &ip, uint64_t until_ms)
{
  return g_blocked_ips && g_blocked_ips->block(ip, until_ms);
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

  // Fingerprint-only rules do not otherwise need a rate-tracking slot. Use the
  // bounded connection table to retain their per-IP log interval state.
  if (!txn_slot && !conn_slot && !h2_slot && g_conn_tracker) {
    conn_slot = g_conn_tracker->process_event(ip, 1);
  }

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
  if (!abuse_shield::log_interval_elapsed(now, most_recent_log, log_interval_ms)) {
    return;
  }

  // Try to claim the log opportunity atomically using the first available slot.
  bool claimed = false;

  if (txn_slot) {
    claimed = abuse_shield::claim_log_interval(txn_slot->last_logged, now, log_interval_ms);
  } else if (conn_slot) {
    claimed = abuse_shield::claim_log_interval(conn_slot->last_logged, now, log_interval_ms);
  } else if (h2_slot) {
    claimed = abuse_shield::claim_log_interval(h2_slot->last_logged, now, log_interval_ms);
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
  int32_t req_tokens  = txn_slot ? txn_slot->buckets.tokens(match.rule->name) : 0;
  int32_t conn_tokens = conn_slot ? conn_slot->buckets.tokens(match.rule->name) : 0;
  int32_t h2_tokens   = h2_slot ? h2_slot->buckets.tokens(match.rule->name) : 0;

  std::string fingerprint_suffix;
  if (!match.fingerprint.empty()) {
    fingerprint_suffix = " fingerprint=";
    fingerprint_suffix.append(match.fingerprint_method);
    fingerprint_suffix.push_back(':');
    fingerprint_suffix.append(match.fingerprint);
  }

  if (g_log_object) {
    TSTextLogObjectWrite(g_log_object, "Rule \"%s\" matched for IP=%s%s actions=[%s] req_tokens=%d conn_tokens=%d h2_tokens=%d",
                         match.rule->name.c_str(), ip_to_string(ip).c_str(), fingerprint_suffix.c_str(),
                         abuse_shield::actions_to_string(match.actions).c_str(), req_tokens, conn_tokens, h2_tokens);
  } else {
    TSError("[%s] Rule \"%s\" matched for IP=%s%s actions=[%s] req_tokens=%d conn_tokens=%d h2_tokens=%d", PLUGIN_NAME,
            match.rule->name.c_str(), ip_to_string(ip).c_str(), fingerprint_suffix.c_str(),
            abuse_shield::actions_to_string(match.actions).c_str(), req_tokens, conn_tokens, h2_tokens);
  }
}

enum class CloseHandling {
  SOCKET_SHUTDOWN,
  REENABLE_ERROR,
};

/** Execute actions for a matched rule.
 *
 * @param[in] match The rule match result.
 * @param[in] ip The client IP address.
 * @param[in] vconn The virtual connection (for close action).
 * @param[in] config The current configuration.
 */
bool
execute_actions(const abuse_shield::RuleMatch &match, const swoc::IPAddr &ip, TSVConn vconn, const abuse_shield::Config &config,
                CloseHandling close_handling = CloseHandling::SOCKET_SHUTDOWN)
{
  TSStatIntIncrement(g_action_stats.rules_matched, 1);

  if (abuse_shield::has_action(match.actions, abuse_shield::Action::BLOCK)) {
    uint64_t block_until = abuse_shield::now_ms() + config.block_duration_ms();
    if (block_ip(ip, block_until)) {
      TSStatIntIncrement(g_action_stats.actions_blocked, 1);
      Dbg(dbg_ctl, "Blocking IP %s for %d seconds (rule: %s)", ip_to_string(ip).c_str(), config.block_duration_sec(),
          match.rule->name.c_str());
    } else {
      TSStatIntIncrement(g_action_stats.actions_block_failed, 1);
      TSError("[%s] Block table is full; could not block %s for rule '%s'", PLUGIN_NAME, ip_to_string(ip).c_str(),
              match.rule->name.c_str());
    }
  }

  bool should_close = abuse_shield::has_action(match.actions, abuse_shield::Action::CLOSE);
  if (should_close) {
    if (close_handling == CloseHandling::SOCKET_SHUTDOWN) {
      int fd = TSVConnFdGet(vconn);
      if (fd >= 0 && shutdown(fd, SHUT_RDWR) == 0) {
        TSStatIntIncrement(g_action_stats.actions_closed, 1);
        Dbg(dbg_ctl, "Closing connection from %s (rule: %s)", ip_to_string(ip).c_str(), match.rule->name.c_str());
      } else {
        TSStatIntIncrement(g_action_stats.actions_close_failed, 1);
        TSError("[%s] Could not close the connection from %s for rule '%s'", PLUGIN_NAME, ip_to_string(ip).c_str(),
                match.rule->name.c_str());
      }
    } else {
      // The hook is rejected with TSVConnReenableEx by the caller.
      TSStatIntIncrement(g_action_stats.actions_closed, 1);
    }
  }

  // Log if logging is configured (independent of block/close actions).
  if (abuse_shield::has_action(match.actions, abuse_shield::Action::LOG)) {
    execute_log_action(match, ip, config);
  }

  return should_close;
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
  H2Errors received_error; // Error received from the client.
  H2Errors sent_error;     // Error sent to the client.
  TSHttpTxnClientReceivedErrorGet(txnp, &received_error.cls, &received_error.code);
  TSHttpTxnClientSentErrorGet(txnp, &sent_error.cls, &sent_error.code);

  auto consume_error = [&](const H2Errors &error, const char *direction) {
    if ((error.cls != 1 && error.cls != 2) || error.code == 0) {
      return false;
    }

    const char *error_class = error.cls == 1 ? "Connection" : "Stream";
    Dbg(dbg_ctl, "%s error %s %s: code=%" PRIu64, error_class, direction, ip_to_string(ip).c_str(), error.code);
    consume_rule_buckets(g_h2_tracker.get(), ip, config, abuse_shield::RateMetric::H2_ERROR, g_h2_stats, error.code);
    return true;
  };

  bool received_h2_error = consume_error(received_error, "received from");
  bool sent_h2_error     = consume_error(sent_error, "sent to");
  if (!received_h2_error && !sent_h2_error) {
    return;
  }

  abuse_shield::RuleMatch match = evaluate_rate_rules(ip, config);
  if (match.actions != 0) {
    execute_actions(match, ip, vconn, config);
  }
}

/** Evaluate ClientHello fingerprint rules before the TLS handshake continues. */
int
handle_client_hello_impl(TSCont /* contp */, TSEvent /* event */, void *edata)
{
  TSVConn vconn = static_cast<TSVConn>(edata);

  std::shared_ptr<abuse_shield::Config> config;
  {
    std::shared_lock lock(g_config_mutex);
    config = g_config;
  }

  if (!config || !config->enabled() || !config->has_fingerprint_rules()) {
    TSVConnReenable(vconn);
    return TS_SUCCESS;
  }

  sockaddr const *client_addr = TSNetVConnRemoteAddrGet(vconn);
  if (!client_addr) {
    TSError("[%s] Could not retrieve the client IP at ClientHello", PLUGIN_NAME);
    TSVConnReenable(vconn);
    return TS_SUCCESS;
  }

  swoc::IPAddr ip(client_addr);
  if (config->is_trusted(ip)) {
    TSVConnReenable(vconn);
    return TS_SUCCESS;
  }

  auto *registry = static_cast<const jax_fingerprint::RegistryV1 *>(TSUserArgGet(vconn, g_fingerprint_registry_index));
  if (!jax_fingerprint::is_valid(registry)) {
    TSStatIntIncrement(g_action_stats.fingerprint_unavailable, 1);
    Dbg(dbg_ctl, "JAx fingerprint registry '%s' is unavailable for this ClientHello", config->fingerprint_registry().c_str());
    TSVConnReenable(vconn);
    return TS_SUCCESS;
  }

  abuse_shield::FingerprintResults fingerprints;
  fingerprints.reserve(config->fingerprint_methods().size());
  for (uint32_t index = 0; index < registry->entry_count; ++index) {
    const auto *entry = jax_fingerprint::entry_at(registry, index);
    if (entry->method == nullptr || entry->value == nullptr || entry->value_length == 0) {
      continue;
    }

    std::string method(entry->method, entry->method_length);
    if (config->fingerprint_methods().contains(method)) {
      fingerprints.emplace(std::move(method), std::string(entry->value, entry->value_length));
    }
  }
  if (fingerprints.size() != config->fingerprint_methods().size()) {
    TSStatIntIncrement(g_action_stats.fingerprint_unavailable, 1);
  }

  abuse_shield::RuleMatch match = evaluate_fingerprint_rules(ip, *config, fingerprints);
  if (match.actions != 0) {
    TSStatIntIncrement(g_action_stats.fingerprint_matches, 1);
    if (execute_actions(match, ip, vconn, *config, CloseHandling::REENABLE_ERROR)) {
      TSStatIntIncrement(g_action_stats.fingerprint_connections_rejected, 1);
      TSVConnReenableEx(vconn, TS_EVENT_ERROR);
      return TS_SUCCESS;
    }
  }

  TSVConnReenable(vconn);
  return TS_SUCCESS;
}

int
handle_client_hello(TSCont contp, TSEvent event, void *edata)
{
  try {
    return handle_client_hello_impl(contp, event, edata);
  } catch (const std::exception &error) {
    TSError("[%s] ClientHello hook failed: %s", PLUGIN_NAME, error.what());
  } catch (...) {
    TSError("[%s] ClientHello hook failed with an unknown exception", PLUGIN_NAME);
  }
  TSVConnReenable(static_cast<TSVConn>(edata));
  return TS_ERROR;
}

// Called at connection start to block known abusive IPs.
int
handle_vconn_start_impl(TSCont /* contp */, TSEvent /* event */, void *edata)
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
  if (config->is_trusted(ip)) {
    Dbg(dbg_ctl, "Skipping trusted IP: %s", ip_to_string(ip).c_str());
    TSVConnReenable(vconn);
    return TS_SUCCESS;
  }

  if (g_blocked_ips && g_blocked_ips->is_blocked(ip)) {
    Dbg(dbg_ctl, "Blocking connection from %s (blocked IP)", ip_to_string(ip).c_str());
    TSStatIntIncrement(g_action_stats.connections_rejected, 1);
    TSVConnReenableEx(vconn, TS_EVENT_ERROR);
    return TS_SUCCESS;
  }

  consume_rule_buckets(g_conn_tracker.get(), ip, *config, abuse_shield::RateMetric::CONNECTION, g_conn_stats);
  abuse_shield::RuleMatch match = evaluate_rate_rules(ip, *config);
  if (match.actions != 0 && execute_actions(match, ip, vconn, *config, CloseHandling::REENABLE_ERROR)) {
    TSVConnReenableEx(vconn, TS_EVENT_ERROR);
    return TS_SUCCESS;
  }

  TSVConnReenable(vconn);
  return TS_SUCCESS;
}

int
handle_vconn_start(TSCont contp, TSEvent event, void *edata)
{
  try {
    return handle_vconn_start_impl(contp, event, edata);
  } catch (const std::exception &error) {
    TSError("[%s] TLS connection hook failed: %s", PLUGIN_NAME, error.what());
  } catch (...) {
    TSError("[%s] TLS connection hook failed with an unknown exception", PLUGIN_NAME);
  }
  TSVConnReenable(static_cast<TSVConn>(edata));
  return TS_ERROR;
}

// Plain HTTP does not pass through TS_VCONN_START_HOOK. Count and enforce its
// connection rules at the HTTP session start hook instead.
int
handle_ssn_start_impl(TSCont /* contp */, TSEvent /* event */, void *edata)
{
  TSHttpSsn ssn = static_cast<TSHttpSsn>(edata);

  std::shared_ptr<abuse_shield::Config> config;
  {
    std::shared_lock lock(g_config_mutex);
    config = g_config;
  }

  if (!config || !config->enabled() || !g_conn_tracker) {
    TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  // TLS sessions were counted at TS_VCONN_START_HOOK.
  if (TSHttpSsnClientProtocolStackContains(ssn, "tls/") != nullptr) {
    TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  TSVConn vconn       = TSHttpSsnClientVConnGet(ssn);
  auto   *client_addr = vconn ? TSNetVConnRemoteAddrGet(vconn) : nullptr;
  if (!client_addr) {
    TSError("[%s] Could not retrieve the plain HTTP session client IP", PLUGIN_NAME);
    TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  swoc::IPAddr ip(client_addr);
  if (config->is_trusted(ip)) {
    TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  if (g_blocked_ips && g_blocked_ips->is_blocked(ip)) {
    int fd = TSVConnFdGet(vconn);
    if (fd >= 0 && shutdown(fd, SHUT_RDWR) == 0) {
      TSStatIntIncrement(g_action_stats.connections_rejected, 1);
    } else {
      TSStatIntIncrement(g_action_stats.connections_reject_failed, 1);
      TSError("[%s] Could not reject blocked plain HTTP connection from %s", PLUGIN_NAME, ip_to_string(ip).c_str());
    }
    TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  consume_rule_buckets(g_conn_tracker.get(), ip, *config, abuse_shield::RateMetric::CONNECTION, g_conn_stats);
  abuse_shield::RuleMatch match = evaluate_rate_rules(ip, *config);
  if (match.actions != 0) {
    execute_actions(match, ip, vconn, *config);
  }

  TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_ssn_start(TSCont contp, TSEvent event, void *edata)
{
  try {
    return handle_ssn_start_impl(contp, event, edata);
  } catch (const std::exception &error) {
    TSError("[%s] HTTP session hook failed: %s", PLUGIN_NAME, error.what());
  } catch (...) {
    TSError("[%s] HTTP session hook failed with an unknown exception", PLUGIN_NAME);
  }
  TSHttpSsnReenable(static_cast<TSHttpSsn>(edata), TS_EVENT_HTTP_CONTINUE);
  return TS_ERROR;
}

// Unified handler for transaction start and close events.
int
handle_txn_event_impl(TSCont /* contp */, TSEvent event, void *edata)
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
  if (config->is_trusted(ip)) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    if (g_txn_tracker) {
      consume_rule_buckets(g_txn_tracker.get(), ip, *config, abuse_shield::RateMetric::REQUEST, g_txn_stats);
      abuse_shield::RuleMatch match = evaluate_rate_rules(ip, *config);
      if (match.actions != 0) {
        execute_actions(match, ip, vconn, *config);
      }
    }
    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    // Process any HTTP/2 errors.
    process_h2_response(txnp, vconn, ip, *config);

    break;

  default:
    TSError("[%s] Unknown event in handle_txn_event: %d", PLUGIN_NAME, event);
    break;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_txn_event(TSCont contp, TSEvent event, void *edata)
{
  try {
    return handle_txn_event_impl(contp, event, edata);
  } catch (const std::exception &error) {
    TSError("[%s] HTTP transaction hook failed: %s", PLUGIN_NAME, error.what());
  } catch (...) {
    TSError("[%s] HTTP transaction hook failed with an unknown exception", PLUGIN_NAME);
  }
  TSHttpTxnReenable(static_cast<TSHttpTxn>(edata), TS_EVENT_HTTP_CONTINUE);
  return TS_ERROR;
}

// Dump transaction tracker.
std::string
dump_txn_tracker()
{
  if (!g_txn_tracker) {
    return "";
  }

  auto format_entry = [](const swoc::IPAddr &ip, uint32_t score,
                         const std::shared_ptr<const abuse_shield::TxnData> &data) -> std::string {
    swoc::LocalBufferWriter<64> ip_str;
    ip_str.print("{}", ip);

    std::ostringstream oss;
    oss << std::left << std::setw(40) << std::string(ip_str.view()) << "\tcount=" << std::setw(6)
        << data->count.load(std::memory_order_relaxed) << "\tscore=" << std::setw(6) << score
        << "\trate_debt=" << (data->buckets.has_debt() ? "yes" : "no") << "\n";
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
                         const std::shared_ptr<const abuse_shield::ConnData> &data) -> std::string {
    swoc::LocalBufferWriter<64> ip_str;
    ip_str.print("{}", ip);

    std::ostringstream oss;
    oss << std::left << std::setw(40) << std::string(ip_str.view()) << "\tcount=" << std::setw(6)
        << data->count.load(std::memory_order_relaxed) << "\tscore=" << std::setw(6) << score
        << "\trate_debt=" << (data->buckets.has_debt() ? "yes" : "no") << "\n";
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

  auto format_entry = [](const swoc::IPAddr &ip, uint32_t score,
                         const std::shared_ptr<const abuse_shield::H2Data> &data) -> std::string {
    swoc::LocalBufferWriter<64> ip_str;
    ip_str.print("{}", ip);

    std::ostringstream oss;
    oss << std::left << std::setw(40) << std::string(ip_str.view()) << "\tcount=" << std::setw(6)
        << data->count.load(std::memory_order_relaxed) << "\tscore=" << std::setw(6) << score
        << "\trate_debt=" << (data->buckets.has_debt() ? "yes" : "no");

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
    header << "# Rate-Limited IPs loaded: request=" << config->rate_limited_req_ips().count()
           << " connection=" << config->rate_limited_conn_ips().count() << " h2=" << config->rate_limited_h2_ips().count()
           << " ranges\n";
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
handle_lifecycle_msg_impl(TSCont /* contp */, TSEvent /* event */, void *edata)
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
        std::unique_lock lock(g_config_mutex);
        if (new_config->slots() != g_config->slots() || new_config->log_file() != g_config->log_file() ||
            new_config->fingerprint_registry() != g_config->fingerprint_registry()) {
          TSError("[%s] Configuration reload rejected: global.ip_tracking.slots, global.log_file, and "
                  "global.fingerprint_registry are startup-only settings",
                  PLUGIN_NAME);
        } else {
          bool runtime_enabled = g_config->enabled();
          new_config->set_config_path(config_path);
          new_config->set_enabled(runtime_enabled);
          g_config = new_config;
          TSNote("[%s] Configuration reloaded successfully", PLUGIN_NAME);
        }
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
    reset_tracker_stats(g_txn_tracker.get(), g_txn_stats);
    reset_tracker_stats(g_conn_tracker.get(), g_conn_stats);
    reset_tracker_stats(g_h2_tracker.get(), g_h2_stats);
    // Reset action stats.
    TSStatIntSet(g_action_stats.rules_matched, 0);
    TSStatIntSet(g_action_stats.actions_blocked, 0);
    TSStatIntSet(g_action_stats.actions_block_failed, 0);
    TSStatIntSet(g_action_stats.actions_closed, 0);
    TSStatIntSet(g_action_stats.actions_close_failed, 0);
    TSStatIntSet(g_action_stats.actions_logged, 0);
    TSStatIntSet(g_action_stats.connections_rejected, 0);
    TSStatIntSet(g_action_stats.connections_reject_failed, 0);
    TSStatIntSet(g_action_stats.fingerprint_matches, 0);
    TSStatIntSet(g_action_stats.fingerprint_connections_rejected, 0);
    TSStatIntSet(g_action_stats.fingerprint_unavailable, 0);
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

int
handle_lifecycle_msg(TSCont contp, TSEvent event, void *edata)
{
  try {
    return handle_lifecycle_msg_impl(contp, event, edata);
  } catch (const std::exception &error) {
    TSError("[%s] Lifecycle message hook failed: %s", PLUGIN_NAME, error.what());
  } catch (...) {
    TSError("[%s] Lifecycle message hook failed with an unknown exception", PLUGIN_NAME);
  }
  return TS_ERROR;
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
    TSFatal("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  // Parse command line arguments.
  if (argc < 2) {
    TSFatal("[%s] Usage: abuse_shield.so <config_file>", PLUGIN_NAME);
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

  if (!g_config->fingerprint_registry().empty()) {
    const char *description = nullptr;
    if (TSUserArgIndexNameLookup(TS_USER_ARGS_VCONN, g_config->fingerprint_registry().c_str(), &g_fingerprint_registry_index,
                                 &description) != TS_SUCCESS) {
      TSFatal("[%s] Fingerprint registry '%s' was not exported by an earlier plugin", PLUGIN_NAME,
              g_config->fingerprint_registry().c_str());
      return;
    }
    if (description == nullptr || std::strcmp(description, jax_fingerprint::REGISTRY_DESCRIPTION) != 0) {
      TSFatal("[%s] Fingerprint registry '%s' uses an incompatible data contract", PLUGIN_NAME,
              g_config->fingerprint_registry().c_str());
      return;
    }
    TSNote("[%s] Using JAx fingerprint registry '%s'", PLUGIN_NAME, g_config->fingerprint_registry().c_str());
  }

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
  g_blocked_ips  = std::make_unique<BlockedIpTable>(g_config->slots());
  Dbg(dbg_ctl, "Created 3 IP trackers with %zu slots each (token bucket rate limiting)", g_config->slots());

  // Initialize stats - separate stats for each tracker plus global action stats.
  g_action_stats.init();
  g_txn_stats.init("txn");
  g_conn_stats.init("conn");
  g_h2_stats.init("h2");

  // Register hooks.
  // VCONN_START is the earliest TLS hook. Plain HTTP is handled at SSN_START.
  TSCont vconn_cont = TSContCreate(handle_vconn_start, nullptr);
  TSHttpHookAdd(TS_VCONN_START_HOOK, vconn_cont);

  TSCont ssn_cont = TSContCreate(handle_ssn_start, nullptr);
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, ssn_cont);

  // CLIENT_HELLO: Consume configured JAx fingerprints and reject matching TLS
  // clients before ServerHello and key-exchange work.
  TSCont client_hello_cont = TSContCreate(handle_client_hello, nullptr);
  TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, client_hello_cont);

  // TXN_START/CLOSE: Transaction-level hooks for rate limiting and rule evaluation.
  TSCont txn_cont = TSContCreate(handle_txn_event, nullptr);
  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, txn_cont);
  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, txn_cont);

  TSCont msg_cont = TSContCreate(handle_lifecycle_msg, nullptr);
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, msg_cont);

  TSNote("[%s] Plugin initialized with %zu slots per tracker, %zu rules", PLUGIN_NAME, g_config->slots(), g_config->rules().size());
}
