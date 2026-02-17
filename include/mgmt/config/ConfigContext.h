/** @file
 *
 *  ConfigContext - Context for configuration loading/reloading operations
 *
 *  Provides:
 *  - Status tracking (in_progress, complete, fail, log)
 *  - Inline content support for YAML configs (via -d flag or RPC API)
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.
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
  ConfigContext() = default;

  explicit ConfigContext(std::shared_ptr<ConfigReloadTask> t, std::string_view description = "", std::string_view filename = "");

  ~ConfigContext();

  // Allow copy/move (weak_ptr is safe to copy)
  ConfigContext(ConfigContext const &)            = default;
  ConfigContext &operator=(ConfigContext const &) = default;
  ConfigContext(ConfigContext &&)                 = default;
  ConfigContext &operator=(ConfigContext &&)      = default;

  void in_progress(std::string_view text = "");
  void log(std::string_view text);
  /// Mark operation as successfully completed
  void complete(std::string_view text = "");
  /// Mark operation as failed.
  void fail(swoc::Errata const &errata, std::string_view summary = "");
  void fail(std::string_view reason = "");
  /// Eg: fail(errata, "Failed to load config: %s", filename);
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
  /// For child contexts it is the label passed to child_context().
  [[nodiscard]] std::string_view get_description() const;

  /// Create a child sub-task that tracks progress independently under this parent.
  /// Each child reports its own status (in_progress/complete/fail) and the parent
  /// task aggregates them. The child also inherits the parent's supplied YAML node.
  ///
  /// @code
  ///   // SSLClientCoordinator delegates to multiple sub-configs:
  ///   void SSLClientCoordinator::reconfigure(ConfigContext ctx) {
  ///     SSLConfig::reconfigure(ctx.child_context("SSLConfig"));
  ///     SNIConfig::reconfigure(ctx.child_context("SNIConfig"));
  ///     SSLCertificateConfig::reconfigure(ctx.child_context("SSLCertificateConfig"));
  ///   }
  /// @endcode
  [[nodiscard]] ConfigContext child_context(std::string_view description = "");

  /// Get supplied YAML node (for RPC-based reloads).
  /// A default-constructed YAML::Node is Undefined (operator bool() == false).
  /// @code
  ///   if (auto yaml = ctx.supplied_yaml()) { /* use yaml node */ }
  /// @endcode
  /// @return const reference to the supplied YAML node.
  [[nodiscard]] const YAML::Node &supplied_yaml() const;

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
