/** @file

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "YamlLogConfigDecoders.h"

#include "LogConfig.h"
#include "LogObject.h"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <memory>

std::set<std::string> valid_log_format_keys = {"name", "format", "interval"};
std::set<std::string> valid_log_filter_keys = {"name", "action", "condition"};

namespace YAML
{
bool
convert<std::unique_ptr<LogFormat>>::decode(const Node &node, std::unique_ptr<LogFormat> &logFormat)
{
  for (auto &&item : node) {
    if (std::none_of(valid_log_format_keys.begin(), valid_log_format_keys.end(),
                     [&item](std::string s) { return s == item.first.as<std::string>(); })) {
      throw YAML::ParserException(node.Mark(), "format: unsupported key '" + item.first.as<std::string>() + "'");
    }
  }

  if (!node["format"]) {
    throw YAML::ParserException(node.Mark(), "missing 'format' argument");
  }
  std::string format = node["format"].as<std::string>();

  std::string name;
  if (node["name"]) {
    name = node["name"].as<std::string>();
  }

  // if the format_str contains any of the aggregate operators,
  // we need to ensure that an interval was specified.
  if (LogField::fieldlist_contains_aggregates(format.c_str())) {
    if (!node["interval"]) {
      Note("'interval' attribute missing for LogFormat object"
           " %s that contains aggregate operators: %s",
           name.c_str(), format.c_str());
      return false;
    }
  }

  unsigned interval = 0;
  if (node["interval"]) {
    interval = node["interval"].as<unsigned>();
  }

  logFormat.reset(new LogFormat(name.c_str(), format.c_str(), interval));

  return true;
}

bool
convert<std::unique_ptr<LogFilter>>::decode(const Node &node, std::unique_ptr<LogFilter> &logFilter)
{
  for (auto &&item : node) {
    if (std::none_of(valid_log_filter_keys.begin(), valid_log_filter_keys.end(),
                     [&item](std::string s) { return s == item.first.as<std::string>(); })) {
      throw YAML::ParserException(node.Mark(), "filter: unsupported key '" + item.first.as<std::string>() + "'");
    }
  }

  // we require all keys for LogFilter
  for (auto &&item : valid_log_filter_keys) {
    if (!node[item]) {
      throw YAML::ParserException(node.Mark(), "missing '" + item + "' argument");
    }
  }

  auto name      = node["name"].as<std::string>();
  auto action    = node["action"].as<std::string>();
  auto condition = node["condition"].as<std::string>();

  auto action_str       = action.c_str();
  LogFilter::Action act = LogFilter::REJECT; /* lv: make gcc happy */
  int i;
  for (i = 0; i < LogFilter::N_ACTIONS; i++) {
    if (strcasecmp(action_str, LogFilter::ACTION_NAME[i]) == 0) {
      act = static_cast<LogFilter::Action>(i);
      break;
    }
  }

  if (i == LogFilter::N_ACTIONS) {
    Warning("%s is not a valid filter action value; cannot create filter %s.", action_str, name.c_str());
    return false;
  }

  logFilter.reset(LogFilter::parse(name.c_str(), act, condition.c_str()));

  return true;
}

} // namespace YAML
