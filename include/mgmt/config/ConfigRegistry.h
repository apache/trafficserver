/** @file
 *
 *  Config Registry - Centralized configuration management
 *
 *  Provides:
 *  - Registration of config handlers by key
 *  - Flexible trigger attachment (at registration or later)
 *  - RPC reload support (YAML content supplied via RPC)
 *  - Runtime lookup for RPC handlers
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <functional>
#include <initializer_list>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "iocore/eventsystem/Lock.h" // For Ptr<ProxyMutex>
#include "mgmt/config/ConfigContext.h"
#include "swoc/Errata.h"

namespace config
{

/// Type of configuration file
enum class ConfigType {
  YAML,  ///< Modern YAML config (ip_allow.yaml, sni.yaml, etc.)
  LEGACY ///< Legacy .config files (remap.config, etc.)
};

/// Handler signature for config reload - receives ConfigContext
/// Handler can check ctx.supplied_yaml() for rpc-supplied content
using ConfigReloadHandler = std::function<void(ConfigContext &)>;

///
/// @brief Central registry for configuration files
///
/// Singleton that maps config keys to their handlers, supporting:
/// - YAML and legacy .config file types
/// - Multiple trigger records per config
/// - RPC reload with supplied YAML content (not for legacy .config)
/// - Runtime lookup by string key
///
/// Usage:
/// @code
///   // Register with filename record (allows runtime filename changes)
///   ConfigRegistry::Get_Instance().register_yaml(
///     "ip_allow",                                    // key
///     "ip_allow.yaml",                              // default filename
///     "proxy.config.cache.ip_allow.filename",      // record holding filename
///     [](ConfigContext s) { IpAllow::reconfigure(s); },
///     {"proxy.config.cache.ip_allow.filename"}     // triggers
///   );
///
///   // Later, if needed, add another trigger from a different module
///   ConfigRegistry::Get_Instance().attach("ip_allow", "proxy.config.plugin.extra");
///
///   // RPC reload with supplied content:
///   // 1. Store content: registry.set_passed_config("ip_allow", yaml_node);
///   // 2. Schedule:      registry.schedule_reload("ip_allow");
/// @endcode
///
class ConfigRegistry
{
public:
  ///
  /// @brief Configuration entry
  ///
  struct Entry {
    std::string              key;              ///< Registry key (e.g., "ip_allow")
    std::string              default_filename; ///< Default filename if record not set (e.g., "ip_allow.yaml")
    std::string              filename_record;  ///< Record containing filename (e.g., "proxy.config.cache.ip_allow.filename")
    ConfigType               type;             ///< YAML or LEGACY - we set that based on the filename extension.
    ConfigReloadHandler      handler;          ///< Handler function
    std::vector<std::string> trigger_records;  ///< Records that trigger reload

    /// Resolve the actual filename (reads from record, falls back to default)
    std::string resolve_filename() const;
  };

  ///
  /// @brief Get singleton instance
  ///
  static ConfigRegistry &Get_Instance();

  ///
  /// @brief Register a config file
  ///
  /// Type (YAML/LEGACY) is inferred from filename extension:
  /// - .yaml, .yml → YAML (supports rpc reload via ctx.supplied_yaml())
  /// - others → LEGACY (file-based only)
  ///
  /// @param key              Registry key (e.g., "ip_allow")
  /// @param default_filename Default filename (e.g., "ip_allow.yaml")
  /// @param filename_record  Record that holds the filename (e.g., "proxy.config.cache.ip_allow.filename")
  ///                         If empty, default_filename is always used.
  /// @param handler          Handler that receives ConfigContext
  /// @param trigger_records  Records that trigger reload (optional)
  ///
  void register_config(const std::string &key, const std::string &default_filename, const std::string &filename_record,
                       ConfigReloadHandler handler, std::initializer_list<const char *> trigger_records = {});

  ///
  /// @brief Attach a trigger record to an existing config
  ///
  /// Can be called from any module to add additional triggers.
  ///
  /// @param key          The registered config key
  /// @param record_name  The record that triggers reload
  /// @return 0 on success, -1 if key not found
  ///
  int attach(const std::string &key, const char *record_name);

  ///
  /// @brief Store passed config content for a key (internal RPC use only)
  ///
  /// Stores YAML content passed via RPC for rpc reload.
  /// Called by RPC handlers in Configuration.cc before schedule_reload().
  /// Content is kept for future reference/debugging.
  /// Thread-safe.
  ///
  /// @note Not intended for external use. Will be moved to a restricted interface
  ///       when the plugin config registration API is introduced.
  ///
  /// @param key     The registered config key
  /// @param content YAML::Node content to store
  ///
  void set_passed_config(const std::string &key, YAML::Node content);

  ///
  /// @brief Schedule async reload for a config key
  ///
  /// Creates a ScheduledReloadContinuation on ET_TASK.
  /// If content was set via set_passed_config(), it will be used (rpc reload).
  /// Otherwise, handler reads from file.
  ///
  /// @param key The registered config key
  ///
  void schedule_reload(const std::string &key);

  ///
  /// @brief Execute reload for a key (called by ScheduledReloadContinuation)
  ///
  /// Checks for rpc-supplied content, creates context, calls handler.
  /// Internal use - called from continuation on ET_TASK thread.
  ///
  /// @param key The config key to reload
  ///
  void execute_reload(const std::string &key);

  /// look up.
  bool         contains(const std::string &key) const;
  Entry const *find(const std::string &key) const;

  /// Callback context for RecRegisterConfigUpdateCb (public for callback access)
  struct TriggerContext {
    std::string     config_key;
    Ptr<ProxyMutex> mutex;
  };

private:
  ConfigRegistry()  = default;
  ~ConfigRegistry() = default;

  // Non-copyable
  ConfigRegistry(ConfigRegistry const &)            = delete;
  ConfigRegistry &operator=(ConfigRegistry const &) = delete;

  /// Internal: common registration logic
  void do_register(Entry entry);

  /// Internal: setup trigger callbacks for an entry
  void setup_triggers(Entry &entry);

  /// Hash for heterogeneous lookup (string_view → string key)
  struct StringHash {
    using is_transparent = void;
    size_t
    operator()(std::string_view sv) const
    {
      return std::hash<std::string_view>{}(sv);
    }
    size_t
    operator()(std::string const &s) const
    {
      return std::hash<std::string>{}(s);
    }
  };

  mutable std::shared_mutex                                                _mutex;
  std::unordered_map<std::string, Entry, StringHash, std::equal_to<>>      _entries;
  std::unordered_map<std::string, YAML::Node, StringHash, std::equal_to<>> _passed_configs;
};

} // namespace config
