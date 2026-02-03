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
#include "logging.h"

#include <string>
#include <vector>

#include "ts/ts.h"

#include <yaml-cpp/yaml.h>

namespace
{
using abuse_shield::dbg_ctl;
using abuse_shield::PLUGIN_NAME;

std::vector<std::string>
parse_action_list(const YAML::Node &node)
{
  std::vector<std::string> actions;
  if (node.IsSequence()) {
    for (const auto &item : node) {
      actions.push_back(item.as<std::string>());
    }
  }
  return actions;
}

abuse_shield::ActionSet
actions_from_strings(const std::vector<std::string> &strings)
{
  abuse_shield::ActionSet set = 0;
  for (const auto &s : strings) {
    if (s == "log") {
      set = abuse_shield::add_action(set, abuse_shield::Action::LOG);
    } else if (s == "block") {
      set = abuse_shield::add_action(set, abuse_shield::Action::BLOCK);
    } else if (s == "close") {
      set = abuse_shield::add_action(set, abuse_shield::Action::CLOSE);
    } else if (s == "downgrade") {
      set = abuse_shield::add_action(set, abuse_shield::Action::DOWNGRADE);
    } else {
      TSError("[%s] Unknown action '%s' - ignoring", PLUGIN_NAME, s.c_str());
    }
  }
  return set;
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
  if (has_action(set, Action::DOWNGRADE)) {
    if (!result.empty()) {
      result += ",";
    }
    result += "downgrade";
  }
  return result;
}

bool
Config::load_trusted_ips(const std::string &path)
{
  try {
    YAML::Node root = YAML::LoadFile(path);

    if (!root["trusted_ips"]) {
      TSError("[%s] Missing 'trusted_ips' key in %s", PLUGIN_NAME, path.c_str());
      return false;
    }

    YAML::Node trusted_list = root["trusted_ips"];
    if (!trusted_list.IsSequence()) {
      TSError("[%s] 'trusted_ips' must be a sequence in %s", PLUGIN_NAME, path.c_str());
      return false;
    }

    for (const auto &item : trusted_list) {
      std::string   ip_str = item.as<std::string>();
      swoc::IPRange range;
      if (range.load(ip_str)) {
        trusted_ips_.fill(range, true);
        Dbg(dbg_ctl, "Added trusted IP: %s", ip_str.c_str());
      } else {
        TSError("[%s] Invalid IP in trusted file: %s", PLUGIN_NAME, ip_str.c_str());
      }
    }

  } catch (const YAML::Exception &e) {
    TSError("[%s] YAML parse error in %s: %s", PLUGIN_NAME, path.c_str(), e.what());
    return false;
  }

  return true;
}

std::shared_ptr<Config>
Config::parse(const std::string &path)
{
  auto config = std::make_shared<Config>();

  try {
    YAML::Node root = YAML::LoadFile(path);

    // Global settings.
    if (root["global"]) {
      auto global = root["global"];

      // IP tracking table settings.
      if (global["ip_tracking"]) {
        auto ip_tracking = global["ip_tracking"];
        config->slots_   = ip_tracking["slots"].as<size_t>(DEFAULT_SLOTS);
      }

      // Blocking settings.
      if (global["blocking"]) {
        auto blocking               = global["blocking"];
        config->block_duration_sec_ = blocking["duration_seconds"].as<int>(DEFAULT_BLOCK_DURATION_SEC);
      }

      // Trusted IPs file.
      if (global["trusted_ips_file"]) {
        std::string trusted_path = global["trusted_ips_file"].as<std::string>();
        config->load_trusted_ips(trusted_path);
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
      for (const auto &rule_node : root["rules"]) {
        Rule rule;
        rule.name = rule_node["name"].as<std::string>("");

        if (rule_node["filter"]) {
          auto filter_node                  = rule_node["filter"];
          rule.filter.max_req_rate          = filter_node["max_req_rate"].as<int>(0);
          rule.filter.req_burst_multiplier  = filter_node["req_burst_multiplier"].as<double>(1.0);
          rule.filter.max_conn_rate         = filter_node["max_conn_rate"].as<int>(0);
          rule.filter.conn_burst_multiplier = filter_node["conn_burst_multiplier"].as<double>(1.0);
          rule.filter.max_h2_error_rate     = filter_node["max_h2_error_rate"].as<int>(0);
          rule.filter.h2_burst_multiplier   = filter_node["h2_burst_multiplier"].as<double>(1.0);
        }

        if (rule_node["action"]) {
          auto action_strings = parse_action_list(rule_node["action"]);
          rule.actions        = actions_from_strings(action_strings);
        }

        config->rules_.push_back(std::move(rule));
        Dbg(dbg_ctl, "Loaded rule: %s", rule.name.c_str());
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
  for (const auto &rule : rules_) {
    if (rule.filter.req_burst_multiplier < 1.0) {
      error_msg =
        "Rule '" + rule.name + "' has req_burst_multiplier < 1.0 (" + std::to_string(rule.filter.req_burst_multiplier) + ")";
      return false;
    }
    if (rule.filter.conn_burst_multiplier < 1.0) {
      error_msg =
        "Rule '" + rule.name + "' has conn_burst_multiplier < 1.0 (" + std::to_string(rule.filter.conn_burst_multiplier) + ")";
      return false;
    }
    if (rule.filter.h2_burst_multiplier < 1.0) {
      error_msg =
        "Rule '" + rule.name + "' has h2_burst_multiplier < 1.0 (" + std::to_string(rule.filter.h2_burst_multiplier) + ")";
      return false;
    }
  }
  return true;
}

} // namespace abuse_shield
