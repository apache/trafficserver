#pragma once
#include <string>
#include <memory>
#include <vector>
#include <iostream>
#include <swoc/Errata.h>
#include <tscore/ink_platform.h>
#include <yaml-cpp/yaml.h>

#include "mgmt/config/ReloadCoordinator.h"

namespace YAML
{
template <> struct convert<ConfigReloadTask::Info> {
  static Node
  encode(const ConfigReloadTask::Info &info)
  {
    Node node;
    node["config_token"]         = info.token;
    node["status"]               = std::string(ConfigReloadTask::state_to_string(info.state));
    node["description"]          = info.description;
    node["filename"]             = info.filename;
    auto meta                    = YAML::Node(YAML::NodeType::Map);
    meta["created_time_ms"]      = info.created_time_ms;
    meta["last_updated_time_ms"] = info.last_updated_time_ms;
    meta["main_task"]            = info.main_task ? "true" : "false";

    node["meta"] = meta;

    node["log"] = YAML::Node(YAML::NodeType::Sequence);
    // if no logs, it will be empty sequence.
    for (const auto &log : info.logs) {
      node["logs"].push_back(log);
    }

    node["sub_tasks"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto &sub_task : info.sub_tasks) {
      node["sub_tasks"].push_back(sub_task->get_info());
    }
    return node;
  }
};

} // namespace YAML
