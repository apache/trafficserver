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

#pragma once

#include "ts/ts.h"
#include "plugin.h"
#include "method.h"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

enum class Mode : int {
  OVERWRITE,
  KEEP,
  APPEND,
};

enum class PluginType : int {
  GLOBAL,
  REMAP,
};

// This hash function enables looking up the set by a string_view without making a temporary string object.
struct StringHash {
  // Enable heterogeneous lookup
  using is_transparent = void;

  size_t
  operator()(std::string_view sv) const
  {
    return std::hash<std::string_view>{}(sv);
  }
};

/** Settings that can be changed by reloading the configuration file at runtime. */
struct RuntimeSettings {
  Mode                                                         mode            = Mode::OVERWRITE;
  std::string                                                  header_name     = "";
  std::string                                                  via_header_name = "";
  std::unordered_set<std::string, StringHash, std::equal_to<>> servernames;
};

struct PluginConfig {
  PluginConfig() { Dbg(dbg_ctl, "New config (%p) has been created", this); }
  ~PluginConfig() { Dbg(dbg_ctl, "Config for (%p) has been deleted", this); }

  PluginType      plugin_type    = PluginType::GLOBAL;
  struct Method   method         = {"uninitialized", Method::Type::CONNECTION_BASED, nullptr, nullptr};
  std::string     log_filename   = "";
  std::string     log_symbol     = "";
  std::string     export_name    = "";
  int             user_arg_index = -1;
  TSCont          handler        = nullptr; // For remap plugin
  bool            standalone     = false;
  TSTextLogObject log_handle     = nullptr;

  /** Get a snapshot of the runtime settings that stays valid while it is held. */
  std::shared_ptr<RuntimeSettings const>
  get_settings() const
  {
    std::shared_lock lock(_settings_mutex);
    return _settings;
  }

  void
  set_settings(std::shared_ptr<RuntimeSettings const> settings)
  {
    std::unique_lock lock(_settings_mutex);
    _settings = std::move(settings);
  }

private:
  mutable std::shared_mutex              _settings_mutex;
  std::shared_ptr<RuntimeSettings const> _settings = std::make_shared<RuntimeSettings const>();
};

using PluginConfigs = std::vector<std::unique_ptr<PluginConfig>>;

/** Resolve a configuration file name, relative to the Traffic Server configuration directory if not absolute. */
std::string resolve_config_path(std::string_view filename);

/** Load the fingerprint configurations from a YAML file.
 *
 * A relative @a filename is resolved against the Traffic Server configuration directory.
 *
 * @param[in] filename The path of the YAML configuration file.
 * @param[in] plugin_type Whether the configuration is for the global or the remap plugin.
 * @param[out] configs One configuration per entry in the file's fingerprint list.
 * @return true if the file was loaded and every entry is valid, false otherwise.
 */
bool load_config_file(std::string_view filename, PluginType plugin_type, PluginConfigs &configs);

/** Check whether a reloaded configuration can replace the current one at runtime.
 *
 * Only the settings in RuntimeSettings can change on reload. The fingerprint list must otherwise match
 * the configuration loaded at startup.
 *
 * @param[in] current The configurations in use.
 * @param[in] updated The configurations loaded from the reloaded file.
 * @param[out] reason Why the reloaded configuration cannot be applied.
 * @return true if @a updated can be applied, false otherwise.
 */
bool is_reload_compatible(std::vector<PluginConfig *> const &current, PluginConfigs const &updated, std::string &reason);

/** Load the fingerprint configurations from the plugin's arguments.
 *
 * A single argument that does not start with '-' names a YAML configuration file, which is loaded
 * with load_config_file(). Otherwise the arguments are command-line options that configure a single
 * fingerprint.
 *
 * @param[in] argc The number of arguments, including @a argv[0].
 * @param[in] argv The arguments. @a argv[0] is not examined.
 * @param[in] plugin_type Whether the configuration is for the global or the remap plugin.
 * @param[out] configs The loaded configurations.
 * @param[out] config_filename The YAML configuration file, or empty if the arguments are command-line options.
 * @return true if the configuration was loaded and is valid, false otherwise.
 */
bool load_config(int argc, char const *argv[], PluginType plugin_type, PluginConfigs &configs, std::string &config_filename);
