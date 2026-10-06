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

#include "config.h"

#ifdef ENABLE_JAX_METHOD_JA4
#include "ja4/method.h"
#endif
#ifdef ENABLE_JAX_METHOD_JA4H
#include "ja4h/method.h"
#endif
#ifdef ENABLE_JAX_METHOD_JA3
#include "ja3/method.h"
#endif

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_set>

namespace
{
constexpr Method const *METHODS[] = {
#ifdef ENABLE_JAX_METHOD_JA4
  &ja4::method,
#endif
#ifdef ENABLE_JAX_METHOD_JA4H
  &ja4h::method,
#endif
#ifdef ENABLE_JAX_METHOD_JA3
  &ja3::method,
#endif
};

constexpr std::string_view ROOT_KEY         = "jax_fingerprint";
constexpr std::string_view FINGERPRINTS_KEY = "fingerprints";

bool
check_keys(const YAML::Node &node, std::initializer_list<std::string_view> allowed, std::string_view scope)
{
  std::unordered_set<std::string> seen;
  for (auto const &entry : node) {
    auto key = entry.first.as<std::string>();
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
      TSError("[%s] Unknown key '%s' in %.*s", PLUGIN_NAME, key.c_str(), static_cast<int>(scope.size()), scope.data());
      return false;
    }
    if (!seen.insert(key).second) {
      TSError("[%s] Duplicate key '%s' in %.*s", PLUGIN_NAME, key.c_str(), static_cast<int>(scope.size()), scope.data());
      return false;
    }
  }
  return true;
}

bool
read_string(const YAML::Node &node, const char *key, std::string &value)
{
  if (YAML::Node field = node[key]; field) {
    if (!field.IsScalar() || field.Scalar().empty()) {
      TSError("[%s] '%s' must be a non-empty string", PLUGIN_NAME, key);
      return false;
    }
    value = field.Scalar();
  }
  return true;
}

bool
parse_fingerprint(const YAML::Node &node, PluginType plugin_type, PluginConfig &config)
{
  if (!node.IsMap()) {
    TSError("[%s] Each '%.*s' entry must be a map", PLUGIN_NAME, static_cast<int>(FINGERPRINTS_KEY.size()),
            FINGERPRINTS_KEY.data());
    return false;
  }

  if (!check_keys(node,
                  {"method", "standalone", "mode", "header", "via_header", "log_filename", "log_field", "servernames", "export"},
                  "fingerprint entry")) {
    return false;
  }

  config.plugin_type = plugin_type;

  std::string method_name;
  if (!read_string(node, "method", method_name) || method_name.empty()) {
    TSError("[%s] Each fingerprint entry requires a 'method'", PLUGIN_NAME);
    return false;
  }
  auto method = std::find_if(std::begin(METHODS), std::end(METHODS), [&](Method const *m) { return m->name == method_name; });
  if (method == std::end(METHODS)) {
    TSError("[%s] Unknown method: %s", PLUGIN_NAME, method_name.c_str());
    return false;
  }
  config.method = **method;

  if (YAML::Node standalone = node["standalone"]; standalone) {
    config.standalone = standalone.as<bool>();
  }

  auto settings = std::make_shared<RuntimeSettings>();

  std::string mode;
  if (!read_string(node, "mode", mode)) {
    return false;
  }
  if (mode.empty() || mode == "overwrite") {
    settings->mode = Mode::OVERWRITE;
  } else if (mode == "keep") {
    settings->mode = Mode::KEEP;
  } else if (mode == "append") {
    settings->mode = Mode::APPEND;
  } else {
    TSError("[%s] Unknown mode: %s", PLUGIN_NAME, mode.c_str());
    return false;
  }

  if (!read_string(node, "header", settings->header_name) || !read_string(node, "via_header", settings->via_header_name) ||
      !read_string(node, "log_filename", config.log_filename) || !read_string(node, "log_field", config.log_symbol) ||
      !read_string(node, "export", config.export_name)) {
    return false;
  }

  if (!config.log_symbol.empty() && plugin_type == PluginType::REMAP) {
    TSError("[%s] 'log_field' is only supported for the plugin loaded via plugin.config", PLUGIN_NAME);
    return false;
  }

  if (YAML::Node servernames = node["servernames"]; servernames) {
    if (!servernames.IsSequence()) {
      TSError("[%s] 'servernames' must be a list of server names", PLUGIN_NAME);
      return false;
    }
    for (auto const &servername : servernames) {
      if (!servername.IsScalar() || servername.Scalar().empty()) {
        TSError("[%s] Each 'servernames' entry must be a non-empty server name", PLUGIN_NAME);
        return false;
      }
      settings->servernames.emplace(servername.Scalar());
    }
  }

  Dbg(dbg_ctl, "JAx method is %s", config.method.name.data());
  Dbg(dbg_ctl, "JAx mode is %d", static_cast<int>(settings->mode));
  Dbg(dbg_ctl, "JAx header is %s", !settings->header_name.empty() ? settings->header_name.c_str() : "DISABLED");
  Dbg(dbg_ctl, "JAx via-header is %s", !settings->via_header_name.empty() ? settings->via_header_name.c_str() : "DISABLED");
  Dbg(dbg_ctl, "JAx log file is %s", !config.log_filename.empty() ? config.log_filename.c_str() : "DISABLED");
  Dbg(dbg_ctl, "JAx export registry is %s", !config.export_name.empty() ? config.export_name.c_str() : PLUGIN_NAME);
  Dbg(dbg_ctl, "JAx standalone mode is %s", config.standalone ? "ENABLED" : "DISABLED");
  for (auto const &servername : settings->servernames) {
    Dbg(dbg_ctl, "JAx servername: %s", servername.c_str());
  }

  config.set_settings(std::move(settings));
  return true;
}
} // namespace

bool
is_reload_compatible(std::vector<PluginConfig *> const &current, PluginConfigs const &updated, std::string &reason)
{
  if (current.size() != updated.size()) {
    reason =
      "the number of fingerprint entries changed from " + std::to_string(current.size()) + " to " + std::to_string(updated.size());
    return false;
  }

  for (size_t i = 0; i < current.size(); ++i) {
    PluginConfig const &before = *current[i];
    PluginConfig const &after  = *updated[i];
    std::string_view    changed;
    if (before.method.name != after.method.name) {
      changed = "method";
    } else if (before.standalone != after.standalone) {
      changed = "standalone";
    } else if (before.export_name != after.export_name) {
      changed = "export";
    } else if (before.log_filename != after.log_filename) {
      changed = "log_filename";
    } else if (before.log_symbol != after.log_symbol) {
      changed = "log_field";
    }
    if (!changed.empty()) {
      reason  = "'";
      reason += changed;
      reason += "' of fingerprint entry " + std::to_string(i + 1) + " changed, but it can only be set at startup";
      return false;
    }
  }

  return true;
}

bool
load_config_file(std::string_view filename, PluginType plugin_type, PluginConfigs &configs)
{
  std::string path{filename};
  if (!path.empty() && path.front() != '/') {
    path = std::string{TSConfigDirGet()} + "/" + path;
  }
  Dbg(dbg_ctl, "Loading configuration from %s", path.c_str());

  try {
    YAML::Node root = YAML::LoadFile(path);
    if (!root.IsMap() || !check_keys(root, {ROOT_KEY}, "the top level")) {
      TSError("[%s] %s: the top level must be a map with a single '%.*s' key", PLUGIN_NAME, path.c_str(),
              static_cast<int>(ROOT_KEY.size()), ROOT_KEY.data());
      return false;
    }

    YAML::Node plugin_node = root[std::string{ROOT_KEY}];
    if (!plugin_node.IsMap() || !check_keys(plugin_node, {FINGERPRINTS_KEY}, ROOT_KEY)) {
      TSError("[%s] %s: '%.*s' must be a map containing '%.*s'", PLUGIN_NAME, path.c_str(), static_cast<int>(ROOT_KEY.size()),
              ROOT_KEY.data(), static_cast<int>(FINGERPRINTS_KEY.size()), FINGERPRINTS_KEY.data());
      return false;
    }

    YAML::Node fingerprints = plugin_node[std::string{FINGERPRINTS_KEY}];
    if (!fingerprints.IsSequence() || fingerprints.size() == 0) {
      TSError("[%s] %s: '%.*s' must be a non-empty list", PLUGIN_NAME, path.c_str(), static_cast<int>(FINGERPRINTS_KEY.size()),
              FINGERPRINTS_KEY.data());
      return false;
    }

    for (auto const &fingerprint : fingerprints) {
      auto config = std::make_unique<PluginConfig>();
      if (!parse_fingerprint(fingerprint, plugin_type, *config)) {
        TSError("[%s] %s: invalid fingerprint entry at line %d", PLUGIN_NAME, path.c_str(), fingerprint.Mark().line + 1);
        return false;
      }
      configs.push_back(std::move(config));
    }
  } catch (const YAML::Exception &e) {
    TSError("[%s] Failed to load %s: %s", PLUGIN_NAME, path.c_str(), e.what());
    return false;
  }

  return true;
}
