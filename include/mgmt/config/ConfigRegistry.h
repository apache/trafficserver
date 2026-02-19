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

/// Declares what content sources a config handler supports.
/// @note If more sources are needed (e.g., Plugin, Env), consider
///       converting to bitwise flags instead of adding combinatorial values.
enum class ConfigSource {
  FileOnly,   ///< Handler only reloads from file on disk
  RecordOnly, ///< Handler only reacts to record changes (no file, no RPC content)
  FileAndRpc  ///< Handler can also process YAML content supplied via RPC
};

/// Handler signature for config reload - receives ConfigContext
/// Handler can check ctx.supplied_yaml() for rpc-supplied content
using ConfigReloadHandler = std::function<void(ConfigContext)>;

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
    ConfigSource             source{ConfigSource::FileOnly}; ///< What content sources this handler supports
    ConfigReloadHandler      handler;                        ///< Handler function
    std::vector<std::string> trigger_records;                ///< Records that trigger reload

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
                       ConfigReloadHandler handler, ConfigSource source, std::initializer_list<const char *> trigger_records = {});

  /// @brief Register a record-only config handler (no file).
  ///
  /// Convenience method for modules that have no config file but need their
  /// reload handler to participate in the config tracking system (tracing,
  /// status reporting, traffic_ctl config reload).
  ///
  /// This is NOT for arbitrary record-change callbacks — use RecRegisterConfigUpdateCb
  /// for that. This is for config modules like SSLTicketKeyConfig that are reloaded
  /// via record changes and need visibility in the reload infrastructure.
  ///
  /// Internally uses ConfigSource::RecordOnly.
  ///
  /// @param key              Registry key (e.g., "ssl_ticket_key")
  /// @param handler          Handler that receives ConfigContext
  /// @param trigger_records  Records that trigger reload
  ///
  void register_record_config(const std::string &key, ConfigReloadHandler handler,
                              std::initializer_list<const char *> trigger_records);

  ///
  /// @brief Attach a trigger record to an existing config
  ///
  /// Can be called from any module to add additional triggers.
  ///
  /// @note Attaches an additional record trigger to an existing config entry.
  ///
  /// @param key          The registered config key
  /// @param record_name  The record that triggers reload
  /// @return 0 on success, -1 if key not found
  ///
  int attach(const std::string &key, const char *record_name);

  ///
  /// @brief Add a file dependency to an existing config
  ///
  /// Registers an additional file with FileManager for mtime-based change detection
  /// and sets up a record callback to trigger the config's reload handler when the
  /// file changes on disk or the record value is modified.
  ///
  /// This is for auxiliary/companion files that a config module depends on but that
  /// are not the primary config file. For example, ip_allow depends on ip_categories.
  ///
  ///
  /// @param key              The registered config key (must already exist)
  /// @param filename_record  Record holding the filename (e.g., "proxy.config.cache.ip_categories.filename")
  /// @param default_filename Default filename when record value is empty (e.g., "ip_categories.yaml")
  /// @param is_required      Whether the file is required to exist
  /// @return 0 on success, -1 if key not found
  ///
  int add_file_dependency(const std::string &key, const char *filename_record, const char *default_filename, bool is_required);

  ///
  /// @brief Add a file dependency with an RPC-routable node key
  ///
  /// Like add_file_dependency(), but also registers @p dep_key as a routable key
  /// so the RPC handler can route inline YAML content to the parent entry's handler.
  ///
  /// When an RPC reload request specifies @p dep_key, resolve() maps it to the parent
  /// entry, and the content is grouped under the dep_key in the YAML node passed to
  /// the handler. The handler (or its sub-modules) can then check for their key:
  /// @code
  ///   if (auto yaml = ctx.supplied_yaml(); yaml && yaml["sni"]) {
  ///     // parse from yaml["sni"] instead of file
  ///   }
  /// @endcode
  ///
  /// @param key              The registered parent config key (must already exist)
  /// @param dep_key          Key for RPC routing (e.g., "sni"). Must be unique across all entries and dependencies.
  /// @param filename_record  Record holding the filename
  /// @param default_filename Default filename when record value is empty
  /// @param is_required      Whether the file is required to exist
  /// @return 0 on success, -1 if key not found or dep_key already exists
  ///
  int add_file_and_node_dependency(const std::string &key, const std::string &dep_key, const char *filename_record,
                                   const char *default_filename, bool is_required);

  ///
  /// @brief Resolve a key to its entry, handling both direct entries and dependency keys
  ///
  /// Looks up @p key first in the direct entry map, then in the dependency key map.
  /// For direct entries, returns {key, entry}. For dependency keys, returns {parent_key, parent_entry}.
  /// Returns {"", nullptr} if the key is not found.
  ///
  /// Used by the RPC handler to route inline content to the correct parent handler,
  /// grouping multiple dependency keys under the same parent into a single reload.
  ///
  /// @param key The key to look up (can be a direct entry key or a dependency key)
  /// @return pair of {resolved_parent_key, entry_pointer}
  ///
  std::pair<std::string, Entry const *> resolve(const std::string &key) const;

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

  /// Internal: wire a record callback to fire on_record_change for a config key.
  /// Does NOT modify trigger_records — callers decide whether to store the record.
  int wire_record_callback(const char *record_name, const std::string &config_key);

  /// Hash for lookup.
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
  /// Maps dependency keys to their parent entry's key.
  ///
  /// When a coordinator entry manages multiple configuration files, each file can
  /// be given a dependency key via add_file_and_node_dependency(). This allows
  /// resolve() to route RPC-supplied content for a dependency key back to the
  /// parent coordinator's handler, so a single reload fires for all related files.
  std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> _dep_key_to_parent;
};

} // namespace config
