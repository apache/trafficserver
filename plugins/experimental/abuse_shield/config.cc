/** @file

  Abuse Shield configuration parsing implementation.

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

#include "config.h"
#include "fingerprint.h"
#include "logging.h"

#include <string>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ts/ts.h"

#include <yaml-cpp/yaml.h>

namespace
{
using abuse_shield::dbg_ctl;
using abuse_shield::PLUGIN_NAME;

using RateLimitedIpCache_t = std::unordered_map<std::string, std::shared_ptr<swoc::IPSpace<bool>>>;

std::optional<abuse_shield::ActionSet>
parse_actions(const YAML::Node &node, std::string_view rule_name)
{
  if (!node || !node.IsSequence() || node.size() == 0) {
    TSError("[%s] Rule '%.*s' action must be a non-empty sequence", PLUGIN_NAME, static_cast<int>(rule_name.size()),
            rule_name.data());
    return std::nullopt;
  }

  abuse_shield::ActionSet set = 0;
  for (const auto &item : node) {
    auto s = item.as<std::string>();
    if (s == "log") {
      set = abuse_shield::add_action(set, abuse_shield::Action::LOG);
    } else if (s == "block") {
      set = abuse_shield::add_action(set, abuse_shield::Action::BLOCK);
    } else if (s == "close") {
      set = abuse_shield::add_action(set, abuse_shield::Action::CLOSE);
    } else {
      TSError("[%s] Rule '%.*s' has unknown action '%s'", PLUGIN_NAME, static_cast<int>(rule_name.size()), rule_name.data(),
              s.c_str());
      return std::nullopt;
    }
  }
  return set;
}

template <typename T>
T
read_optional(const YAML::Node &parent, const char *key, T default_value)
{
  YAML::Node value = parent[key];
  return value ? value.as<T>() : default_value;
}

bool
ip_space_contains(const swoc::IPSpace<bool> &ip_space, const swoc::IPAddr &ip)
{
  return ip_space.find(ip) != ip_space.end();
}

std::string
ip_list_key_description(const char *primary_key, const char *fallback_key)
{
  std::string description  = "'";
  description             += primary_key;
  description             += "' key";
  if (fallback_key != nullptr) {
    description += " (or legacy '";
    description += fallback_key;
    description += "' key)";
  }
  return description;
}

bool
filter_uses_metric(const abuse_shield::RuleFilter &filter, abuse_shield::RateMetric metric)
{
  switch (metric) {
  case abuse_shield::RateMetric::REQUEST:
    return filter.max_req_rate > 0;
  case abuse_shield::RateMetric::CONNECTION:
    return filter.max_conn_rate > 0;
  case abuse_shield::RateMetric::H2_ERROR:
    return filter.max_h2_error_rate > 0;
  }
  return false;
}

} // namespace

namespace abuse_shield
{

std::string
actions_to_string(ActionSet set)
{
  std::string result;
  if (has_action(set, Action::LOG)) {
    result += "log";
  }
  if (has_action(set, Action::BLOCK)) {
    if (!result.empty()) {
      result += ",";
    }
    result += "block";
  }
  if (has_action(set, Action::CLOSE)) {
    if (!result.empty()) {
      result += ",";
    }
    result += "close";
  }
  return result;
}

bool
Config::load_ip_space(const std::string &path, swoc::IPSpace<bool> &ip_space, const char *list_description, const char *primary_key,
                      const char *fallback_key)
{
  try {
    YAML::Node root = YAML::LoadFile(path);

    const char *ip_list_key = primary_key;
    YAML::Node  ip_list     = root[primary_key];
    if (!ip_list && fallback_key != nullptr) {
      ip_list     = root[fallback_key];
      ip_list_key = fallback_key;
    }

    if (!ip_list) {
      std::string key_description = ip_list_key_description(primary_key, fallback_key);
      TSError("[%s] Missing %s in %s file %s", PLUGIN_NAME, key_description.c_str(), list_description, path.c_str());
      return false;
    }

    if (!ip_list.IsSequence()) {
      TSError("[%s] '%s' must be a sequence in %s file %s", PLUGIN_NAME, ip_list_key, list_description, path.c_str());
      return false;
    }

    for (const auto &item : ip_list) {
      std::string   ip_str = item.as<std::string>();
      swoc::IPRange range;
      if (range.load(ip_str)) {
        ip_space.fill(range, true);
        Dbg(dbg_ctl, "Added %s IP: %s", list_description, ip_str.c_str());
      } else {
        TSError("[%s] Invalid IP in %s file %s: %s", PLUGIN_NAME, list_description, path.c_str(), ip_str.c_str());
        return false;
      }
    }

  } catch (const YAML::Exception &e) {
    TSError("[%s] YAML parse error in %s file %s: %s", PLUGIN_NAME, list_description, path.c_str(), e.what());
    return false;
  }

  return true;
}

void
Config::add_rate_limited_rule_ips(const Rule &rule)
{
  if (!rule.filter.has_rate_limited_ips()) {
    return;
  }

  bool applies_to_req  = rule.filter.max_req_rate > 0;
  bool applies_to_conn = rule.filter.max_conn_rate > 0;
  bool applies_to_h2   = rule.filter.max_h2_error_rate > 0;
  if (!applies_to_req && !applies_to_conn && !applies_to_h2) {
    return;
  }

  for (auto const &[range, flag] : *rule.filter.rate_limited_ips) {
    if (!flag) {
      continue;
    }
    if (applies_to_req) {
      rate_limited_req_ips_.fill(range, true);
    }
    if (applies_to_conn) {
      rate_limited_conn_ips_.fill(range, true);
    }
    if (applies_to_h2) {
      rate_limited_h2_ips_.fill(range, true);
    }
  }
}

std::shared_ptr<Config>
Config::parse(const std::string &path)
{
  auto config = std::make_shared<Config>();

  try {
    YAML::Node           root = YAML::LoadFile(path);
    RateLimitedIpCache_t rate_limited_ip_cache;

    // Global settings.
    if (root["global"]) {
      auto global = root["global"];

      // IP tracking table settings.
      if (global["ip_tracking"]) {
        auto ip_tracking = global["ip_tracking"];
        config->slots_   = read_optional<size_t>(ip_tracking, "slots", DEFAULT_SLOTS);
      }

      // Blocking settings.
      if (global["blocking"]) {
        auto blocking               = global["blocking"];
        config->block_duration_sec_ = read_optional<int>(blocking, "duration_seconds", DEFAULT_BLOCK_DURATION_SEC);
      }

      // Trusted IPs file.
      if (global["trusted_ips_file"]) {
        std::string trusted_path = global["trusted_ips_file"].as<std::string>();
        if (!load_ip_space(trusted_path, config->trusted_ips_, "trusted", "trusted_ips", nullptr)) {
          return nullptr;
        }
      }

      // Log rate limiting.
      if (global["log_interval_sec"]) {
        config->log_interval_sec_ = global["log_interval_sec"].as<int>();
      }

      // Optional log file for LOG action output.
      if (global["log_file"]) {
        config->log_file_ = global["log_file"].as<std::string>();
        Dbg(dbg_ctl, "Log file configured: %s", config->log_file_.c_str());
      }
    }

    // Rules.
    if (root["rules"]) {
      if (!root["rules"].IsSequence()) {
        TSError("[%s] 'rules' must be a sequence", PLUGIN_NAME);
        return nullptr;
      }
      for (const auto &rule_node : root["rules"]) {
        Rule rule;
        rule.name = read_optional<std::string>(rule_node, "name", "");

        if (rule_node["filter"]) {
          auto filter_node                  = rule_node["filter"];
          rule.filter.max_req_rate          = read_optional<int>(filter_node, "max_req_rate", 0);
          rule.filter.req_burst_multiplier  = read_optional<double>(filter_node, "req_burst_multiplier", 1.0);
          rule.filter.max_conn_rate         = read_optional<int>(filter_node, "max_conn_rate", 0);
          rule.filter.conn_burst_multiplier = read_optional<double>(filter_node, "conn_burst_multiplier", 1.0);
          rule.filter.max_h2_error_rate     = read_optional<int>(filter_node, "max_h2_error_rate", 0);
          rule.filter.h2_burst_multiplier   = read_optional<double>(filter_node, "h2_burst_multiplier", 1.0);
          if (auto fingerprints_node = filter_node["fingerprints"]) {
            if (!fingerprints_node.IsMap()) {
              TSError("[%s] Rule '%s' fingerprints must be a map", PLUGIN_NAME, rule.name.c_str());
              return nullptr;
            }

            for (const auto &fingerprint_entry : fingerprints_node) {
              std::string method_name = fingerprint_entry.first.as<std::string>();
              const auto *method      = find_fingerprint_method(method_name);
              if (!method) {
                TSError("[%s] Rule '%s' references unavailable fingerprint method '%s'", PLUGIN_NAME, rule.name.c_str(),
                        method_name.c_str());
                return nullptr;
              }
              if (!fingerprint_entry.second.IsSequence() || fingerprint_entry.second.size() == 0) {
                TSError("[%s] Rule '%s' fingerprint method '%s' must contain at least one value", PLUGIN_NAME, rule.name.c_str(),
                        method_name.c_str());
                return nullptr;
              }

              auto &configured_values = rule.filter.fingerprints[std::string(method->name)];
              for (const auto &value_node : fingerprint_entry.second) {
                std::string value     = value_node.as<std::string>();
                auto        canonical = method->canonicalize(value);
                if (!canonical) {
                  TSError("[%s] Rule '%s' contains an invalid %s fingerprint '%s'", PLUGIN_NAME, rule.name.c_str(),
                          method->name.data(), value.c_str());
                  return nullptr;
                }
                configured_values.insert(std::move(*canonical));
              }
              config->fingerprint_methods_.emplace(method->name);
            }
          }
          if (filter_node["rate_limited_ips_file"]) {
            rule.filter.rate_limited_ips_file = filter_node["rate_limited_ips_file"].as<std::string>();
            auto [cached_ip_space, inserted]  = rate_limited_ip_cache.try_emplace(rule.filter.rate_limited_ips_file);
            if (inserted) {
              cached_ip_space->second = std::make_shared<swoc::IPSpace<bool>>();
              if (!load_ip_space(rule.filter.rate_limited_ips_file, *cached_ip_space->second, "rate-limited", "rate_limited_ips",
                                 "trusted_ips")) {
                return nullptr;
              }
            }
            rule.filter.rate_limited_ips = cached_ip_space->second;
          }
        }

        auto actions = parse_actions(rule_node["action"], rule.name);
        if (!actions) {
          return nullptr;
        }
        rule.actions = *actions;

        config->add_rate_limited_rule_ips(rule);
        Dbg(dbg_ctl, "Loaded rule: %s", rule.name.c_str());
        config->rules_.push_back(std::move(rule));
      }
    }

    config->enabled_ = root["enabled"].as<bool>(true);

  } catch (const YAML::Exception &e) {
    TSError("[%s] YAML parse error in %s at line %d, column %d: %s", PLUGIN_NAME, path.c_str(), e.mark.line + 1, e.mark.column + 1,
            e.what());
    return nullptr;
  }

  return config;
}

bool
Config::validate(std::string &error_msg) const
{
  if (slots_ == 0) {
    error_msg = "global.ip_tracking.slots must be greater than zero";
    return false;
  }
  if (block_duration_sec_ <= 0) {
    error_msg = "global.blocking.duration_seconds must be greater than zero";
    return false;
  }
  if (log_interval_sec_ < 0) {
    error_msg = "global.log_interval_sec must not be negative";
    return false;
  }

  std::unordered_set<std::string> rule_names;
  if (rules_.empty()) {
    error_msg = "At least one rule must be configured";
    return false;
  }
  for (const auto &rule : rules_) {
    if (rule.name.empty()) {
      error_msg = "Every rule must have a non-empty name";
      return false;
    }
    if (!rule_names.insert(rule.name).second) {
      error_msg = "Duplicate rule name '" + rule.name + "'";
      return false;
    }
    if (rule.actions == 0) {
      error_msg = "Rule '" + rule.name + "' must configure at least one action";
      return false;
    }
    if (rule.filter.max_req_rate < 0 || rule.filter.max_conn_rate < 0 || rule.filter.max_h2_error_rate < 0) {
      error_msg = "Rule '" + rule.name + "' has a negative rate limit";
      return false;
    }
    if (rule.filter.max_req_rate == 0 && rule.filter.max_conn_rate == 0 && rule.filter.max_h2_error_rate == 0 &&
        !rule.filter.has_fingerprints()) {
      error_msg = "Rule '" + rule.name + "' has no filter criteria";
      return false;
    }
    if (!std::isfinite(rule.filter.req_burst_multiplier) || rule.filter.req_burst_multiplier < 1.0) {
      error_msg =
        "Rule '" + rule.name + "' has req_burst_multiplier < 1.0 (" + std::to_string(rule.filter.req_burst_multiplier) + ")";
      return false;
    }
    if (!std::isfinite(rule.filter.conn_burst_multiplier) || rule.filter.conn_burst_multiplier < 1.0) {
      error_msg =
        "Rule '" + rule.name + "' has conn_burst_multiplier < 1.0 (" + std::to_string(rule.filter.conn_burst_multiplier) + ")";
      return false;
    }
    if (!std::isfinite(rule.filter.h2_burst_multiplier) || rule.filter.h2_burst_multiplier < 1.0) {
      error_msg =
        "Rule '" + rule.name + "' has h2_burst_multiplier < 1.0 (" + std::to_string(rule.filter.h2_burst_multiplier) + ")";
      return false;
    }
    auto burst_fits = [](int rate, double multiplier) {
      return static_cast<double>(rate) * multiplier <= static_cast<double>(std::numeric_limits<int32_t>::max());
    };
    if (!burst_fits(rule.filter.max_req_rate, rule.filter.req_burst_multiplier) ||
        !burst_fits(rule.filter.max_conn_rate, rule.filter.conn_burst_multiplier) ||
        !burst_fits(rule.filter.max_h2_error_rate, rule.filter.h2_burst_multiplier)) {
      error_msg = "Rule '" + rule.name + "' has a rate burst larger than INT32_MAX";
      return false;
    }
  }
  return true;
}

bool
Config::is_trusted(const swoc::IPAddr &ip) const
{
  return ip_space_contains(trusted_ips_, ip);
}

bool
Config::is_rate_limited_for_metric(const swoc::IPAddr &ip, RateMetric metric) const
{
  switch (metric) {
  case RateMetric::REQUEST:
    return ip_space_contains(rate_limited_req_ips_, ip);
  case RateMetric::CONNECTION:
    return ip_space_contains(rate_limited_conn_ips_, ip);
  case RateMetric::H2_ERROR:
    return ip_space_contains(rate_limited_h2_ips_, ip);
  }
  return false;
}

bool
Config::rule_applies_to_ip(const Rule &rule, const swoc::IPAddr &ip) const
{
  if (rule.filter.has_rate_limited_ips()) {
    return ip_space_contains(*rule.filter.rate_limited_ips, ip);
  }

  if (filter_uses_metric(rule.filter, RateMetric::REQUEST) && is_rate_limited_for_metric(ip, RateMetric::REQUEST)) {
    return false;
  }
  if (filter_uses_metric(rule.filter, RateMetric::CONNECTION) && is_rate_limited_for_metric(ip, RateMetric::CONNECTION)) {
    return false;
  }
  if (filter_uses_metric(rule.filter, RateMetric::H2_ERROR) && is_rate_limited_for_metric(ip, RateMetric::H2_ERROR)) {
    return false;
  }

  return true;
}

} // namespace abuse_shield
