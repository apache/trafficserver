/** @file

  Abuse Shield configuration structures and parsing.

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

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "swoc/swoc_ip.h"

namespace abuse_shield
{

// Default configuration values.
constexpr size_t DEFAULT_SLOTS              = 50000;
constexpr int    DEFAULT_BLOCK_DURATION_SEC = 300;
constexpr int    DEFAULT_LOG_INTERVAL_SEC   = 10;

/** Action types that can be taken when a rule matches. */
enum class Action : uint8_t {
  LOG   = 1 << 0,
  BLOCK = 1 << 1,
  CLOSE = 1 << 2,
};

using ActionSet          = uint8_t;
using FingerprintSet     = std::unordered_set<std::string>;
using FingerprintFilters = std::unordered_map<std::string, FingerprintSet>;

/** Check if an action set contains a specific action. */
inline bool
has_action(ActionSet set, Action action)
{
  return (set & static_cast<uint8_t>(action)) != 0;
}

/** Add an action to an action set. */
inline ActionSet
add_action(ActionSet set, Action action)
{
  return set | static_cast<uint8_t>(action);
}

/** Convert action bitmask to a human-readable comma-separated string. */
std::string actions_to_string(ActionSet set);

/** Rate-limited metric tracked by abuse_shield. */
enum class RateMetric : uint8_t {
  REQUEST,
  CONNECTION,
  H2_ERROR,
};

/** Filter criteria for rule matching.
 *
 * All filters use token bucket rate limiting (per second).
 * Multiple filters in one rule use AND logic (all must match).
 * For OR logic, use separate rules (first matching rule wins).
 *
 * Burst multipliers control how much burst capacity is allowed:
 *   - 1.0 = burst equals rate (no extra burst tolerance)
 *   - 2.0 = burst is 2x the rate (allows traffic spikes up to 2x)
 *   - Values < 1.0 are invalid
 */
struct RuleFilter {
  int    max_req_rate{0};            ///< Max requests per second (0 = disabled)
  double req_burst_multiplier{1.0};  ///< Burst multiplier for requests (must be >= 1.0)
  int    max_conn_rate{0};           ///< Max connections per second (0 = disabled)
  double conn_burst_multiplier{1.0}; ///< Burst multiplier for connections (must be >= 1.0)
  int    max_h2_error_rate{0};       ///< Max H2 errors per second (0 = disabled)
  double h2_burst_multiplier{1.0};   ///< Burst multiplier for H2 errors (must be >= 1.0)

  std::string                          rate_limited_ips_file; ///< Optional rate-limited IP list file for this rule.
  std::shared_ptr<swoc::IPSpace<bool>> rate_limited_ips;      ///< IP ranges eligible for this rate-limited rule.
  FingerprintFilters                   fingerprints;          ///< ClientHello fingerprints, ORed across methods and values.

  bool
  has_rate_limited_ips() const
  {
    return rate_limited_ips != nullptr;
  }

  bool
  has_fingerprints() const
  {
    return !fingerprints.empty();
  }
};

/** A rule defining when to take action on an IP. */
struct Rule {
  std::string name;
  RuleFilter  filter;
  ActionSet   actions{0};
};

/** Result of rule evaluation, including the matched rule for logging. */
struct RuleMatch {
  const Rule      *rule{nullptr}; ///< nullptr if no match
  ActionSet        actions{0};
  std::string_view fingerprint_method; ///< Matched fingerprint method, if any.
  std::string_view fingerprint;        ///< Matched fingerprint value, if any.
};

/** Plugin configuration loaded from YAML. */
class Config
{
public:
  Config() = default;

  /** Parse configuration from a YAML file.
   *
   * @param[in] path Path to the YAML configuration file.
   * @return The parsed configuration, or nullptr on error.
   */
  static std::shared_ptr<Config> parse(const std::string &path);

  /** Validate configuration settings.
   *
   * @param[out] error_msg Description of validation error, if any.
   * @return True if configuration is valid, false otherwise.
   */
  bool validate(std::string &error_msg) const;

  // Accessors.
  size_t
  slots() const
  {
    return slots_;
  }
  int
  block_duration_sec() const
  {
    return block_duration_sec_;
  }
  bool
  enabled() const
  {
    return enabled_;
  }
  const std::string &
  config_path() const
  {
    return config_path_;
  }
  const std::vector<Rule> &
  rules() const
  {
    return rules_;
  }
  const std::unordered_set<std::string> &
  fingerprint_methods() const
  {
    return fingerprint_methods_;
  }
  bool
  has_fingerprint_rules() const
  {
    return !fingerprint_methods_.empty();
  }
  const std::string &
  fingerprint_registry() const
  {
    return fingerprint_registry_;
  }
  const swoc::IPSpace<bool> &
  trusted_ips() const
  {
    return trusted_ips_;
  }
  const swoc::IPSpace<bool> &
  rate_limited_req_ips() const
  {
    return rate_limited_req_ips_;
  }
  const swoc::IPSpace<bool> &
  rate_limited_conn_ips() const
  {
    return rate_limited_conn_ips_;
  }
  const swoc::IPSpace<bool> &
  rate_limited_h2_ips() const
  {
    return rate_limited_h2_ips_;
  }
  int
  log_interval_sec() const
  {
    return log_interval_sec_;
  }
  const std::string &
  log_file() const
  {
    return log_file_;
  }

  // Mutators.
  void
  set_enabled(bool enabled)
  {
    enabled_ = enabled;
  }
  void
  set_config_path(const std::string &path)
  {
    config_path_ = path;
  }

  /** Check if an IP is in the global trusted list. */
  bool is_trusted(const swoc::IPAddr &ip) const;

  /** Check whether an IP belongs to the rate-limited tier for a specific metric. */
  bool is_rate_limited_for_metric(const swoc::IPAddr &ip, RateMetric metric) const;

  /** Check whether a rule's IP tier applies to this IP. */
  bool rule_applies_to_ip(const Rule &rule, const swoc::IPAddr &ip) const;

private:
  /** Load IP ranges from a separate YAML file.
   *
   * @param[in] path Path to the IP list file.
   * @param[out] ip_space Destination IP space to fill.
   * @param[in] list_description Human-readable list description for logs.
   * @param[in] primary_key Preferred YAML sequence key to read.
   * @param[in] fallback_key Optional legacy YAML sequence key, or nullptr.
   * @return True on success, false on error.
   */
  static bool load_ip_space(const std::string &path, swoc::IPSpace<bool> &ip_space, const char *list_description,
                            const char *primary_key, const char *fallback_key);

  /** Merge a rate-limited rule's IP ranges into the per-metric rate-limited IP spaces. */
  void add_rate_limited_rule_ips(const Rule &rule);

  size_t slots_{DEFAULT_SLOTS};
  int    block_duration_sec_{DEFAULT_BLOCK_DURATION_SEC};
  int    log_interval_sec_{DEFAULT_LOG_INTERVAL_SEC};

  std::vector<Rule>               rules_;
  std::unordered_set<std::string> fingerprint_methods_;
  swoc::IPSpace<bool>             trusted_ips_;
  swoc::IPSpace<bool>             rate_limited_req_ips_;
  swoc::IPSpace<bool>             rate_limited_conn_ips_;
  swoc::IPSpace<bool>             rate_limited_h2_ips_;

  bool enabled_{true};

  std::string config_path_;
  std::string log_file_;
  std::string fingerprint_registry_;
};

} // namespace abuse_shield
