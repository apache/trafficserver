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
#include <vector>

#include "swoc/IPRange.h"

namespace abuse_shield
{

// Default configuration values.
constexpr size_t DEFAULT_SLOTS              = 50000;
constexpr size_t DEFAULT_PARTITIONS         = 64;
constexpr int    DEFAULT_BLOCK_DURATION_SEC = 300;
constexpr int    DEFAULT_LOG_INTERVAL_SEC   = 10;

/** Action types that can be taken when a rule matches. */
enum class Action : uint8_t {
  LOG       = 1 << 0,
  BLOCK     = 1 << 1,
  CLOSE     = 1 << 2,
  DOWNGRADE = 1 << 3,
};

using ActionSet = uint8_t;

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
};

/** A rule defining when to take action on an IP. */
struct Rule {
  std::string name;
  RuleFilter  filter;
  ActionSet   actions{0};
};

/** Result of rule evaluation, including the matched rule for logging. */
struct RuleMatch {
  const Rule *rule{nullptr}; ///< nullptr if no match
  ActionSet   actions{0};
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
  size_t
  partitions() const
  {
    return partitions_;
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
  const swoc::IPSpace<bool> &
  trusted_ips() const
  {
    return trusted_ips_;
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

private:
  /** Load trusted IPs from a separate YAML file.
   *
   * @param[in] path Path to the trusted IPs file.
   * @return True on success, false on error.
   */
  bool load_trusted_ips(const std::string &path);

  size_t slots_{DEFAULT_SLOTS};
  size_t partitions_{DEFAULT_PARTITIONS};
  int    block_duration_sec_{DEFAULT_BLOCK_DURATION_SEC};
  int    log_interval_sec_{DEFAULT_LOG_INTERVAL_SEC};

  std::vector<Rule>   rules_;
  swoc::IPSpace<bool> trusted_ips_;

  bool enabled_{true};

  std::string config_path_;
  std::string log_file_;
};

} // namespace abuse_shield
