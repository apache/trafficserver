/** @file

   ConfigContext - Context for configuration loading/reloading operations

   Provides:
   - Status tracking (in_progress, complete, fail, log)
   - Inline content support for YAML configs (via -d flag or RPC API)

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

#include <memory>
#include <string>
#include <string_view>

#include "swoc/Errata.h"
#include "swoc/BufferWriter.h"
#include "yaml-cpp/node/node.h"

// Forward declarations
class ConfigReloadTask;
class ReloadCoordinator;
namespace config
{
class ConfigRegistry;
}

///
/// @brief Context passed to config handlers during load/reload operations.
///
/// This object is passed to reconfigure() methods to:
/// 1. Track progress/status of the operation (in_progress, complete, fail, log)
/// 2. Provide RPC-supplied YAML content (for -d flag (traffic_ctl) or direct JSONRPC calls)
///
/// For file-based reloads, handlers read from their own registered filename.
/// For RPC reloads, handlers use supplied_yaml() to get the content.
///
/// @note This context is also used during **startup** configuration loading.
///       At startup there is no active reload task, so all status operations
///       (in_progress, complete, fail, log) are safe **no-ops**. To keep the
///       existing code logic for loading/reloading this design aims to avoid
///       having two separate code paths for startup vs. reload — handlers
///       can use the same API in both cases.
///
/// Usage:
/// @code
///   void MyConfig::reconfigure(ConfigContext ctx) {
///     ctx.in_progress();
///
///     YAML::Node root;
///     if (auto yaml = ctx.supplied_yaml()) {
///       // RPC mode: content provided via -d flag or RPC.
///       // YAML::Node has explicit operator bool() → true when IsDefined().
///       // Copy is cheap (internally reference-counted).
///       root = yaml;
///     } else {
///       // File mode: read from registered filename.
///       root = YAML::LoadFile(my_config_filename);
///     }
///
///     // ... process config ...
///
///     ctx.complete("Loaded successfully");
///   }
/// @endcode
///
class ConfigContext
{
public:
  ConfigContext();

  explicit ConfigContext(std::shared_ptr<ConfigReloadTask> t, std::string_view description = "", std::string_view filename = "");

  ~ConfigContext();

  // Copy only — move is intentionally suppressed.
  // ConfigContext holds a weak_ptr (cheap to copy) and a YAML::Node (ref-counted).
  // Suppressing move ensures that std::move(ctx) silently copies, keeping the
  // original valid. This is critical for execute_reload()'s post-handler check:
  // if a handler defers work (e.g. LogConfig), the original ctx must remain
  // valid so is_terminal() can detect the non-terminal state and emit a warning.
  ConfigContext(ConfigContext const &);
  ConfigContext &operator=(ConfigContext const &);

  void in_progress(std::string_view text = "");
  template <typename... Args>
  void
  in_progress(swoc::TextView fmt, Args &&...args)
  {
    std::string buf;
    in_progress(swoc::bwprint(buf, fmt, std::forward<Args>(args)...));
  }

  void log(std::string_view text);
  template <typename... Args>
  void
  log(swoc::TextView fmt, Args &&...args)
  {
    std::string buf;
    log(swoc::bwprint(buf, fmt, std::forward<Args>(args)...));
  }

  /// Mark operation as successfully completed
  void complete(std::string_view text = "");
  template <typename... Args>
  void
  complete(swoc::TextView fmt, Args &&...args)
  {
    std::string buf;
    complete(swoc::bwprint(buf, fmt, std::forward<Args>(args)...));
  }

  /// Mark operation as failed.
  void fail(swoc::Errata const &errata, std::string_view summary = "");
  void fail(std::string_view reason = "");
  template <typename... Args>
  void
  fail(swoc::TextView fmt, Args &&...args)
  {
    std::string buf;
    fail(swoc::bwprint(buf, fmt, std::forward<Args>(args)...));
  }
  /// Eg: fail(errata, "Failed to load config: {}", filename);
  template <typename... Args>
  void
  fail(swoc::Errata const &errata, swoc::TextView fmt, Args &&...args)
  {
    std::string buf;
    fail(errata, swoc::bwprint(buf, fmt, std::forward<Args>(args)...));
  }

  /// Check if the task has reached a terminal state (SUCCESS, FAIL, TIMEOUT).
  [[nodiscard]] bool is_terminal() const;

  /// Get the description associated with this context's task.
  /// For registered configs this is the registration key (e.g., "sni", "ssl").
  /// For dependent contexts it is the label passed to add_dependent_ctx().
  [[nodiscard]] std::string get_description() const;

  /// Create a dependent sub-task that tracks progress independently under this parent.
  /// Each dependent reports its own status (in_progress/complete/fail) and the parent
  /// task aggregates them. The dependent context also inherits the parent's supplied YAML node.
  ///
  [[nodiscard]] ConfigContext add_dependent_ctx(std::string_view description = "");

  /// Get supplied YAML node (for RPC-based reloads).
  /// A default-constructed YAML::Node is Undefined (operator bool() == false).
  /// @code
  ///   if (auto yaml = ctx.supplied_yaml()) { /* use yaml node */ }
  /// @endcode
  /// @return copy of the supplied YAML node (cheap — YAML::Node is internally reference-counted).
  [[nodiscard]] YAML::Node supplied_yaml() const;

private:
  /// Set supplied YAML node. Only ConfigRegistry should call this during reload setup.
  void set_supplied_yaml(YAML::Node node);

  std::weak_ptr<ConfigReloadTask> _task;
  YAML::Node                      _supplied_yaml; ///< for no content, this will just be empty

  friend class ReloadCoordinator;
  friend class config::ConfigRegistry;
};

namespace config
{
/// Create a ConfigContext for use in reconfigure handlers
ConfigContext make_config_reload_context(std::string_view description, std::string_view filename = "");
} // namespace config
