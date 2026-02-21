/** @file
 *
 *  ConfigReloadTrace - Reload task tracking and progress reporting
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

#include <atomic>
#include <chrono>
#include <string_view>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <iostream>

#include <swoc/Errata.h>
#include <tscore/ink_platform.h>
#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EventSystem.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/Tasks.h"
#include "tscore/Diags.h"

class ConfigReloadTask;
class ConfigContext;

namespace YAML
{
template <typename T> struct convert;
} // namespace YAML

using ConfigReloadTaskPtr = std::shared_ptr<ConfigReloadTask>;

///
/// @brief Progress checker for reload tasks — detects stuck/hanging tasks.
///
/// Created per-reload by ConfigReloadTask::start_progress_checker(), which is called
/// only for main tasks in the IN_PROGRESS state. Each instance is bound to a single
/// ConfigReloadTask and holds a shared_ptr to it.
///
/// Lifecycle:
///   - Scheduled on ET_TASK when a reload starts.
///   - check_progress() runs periodically (every check_interval).
///   - Self-terminates (returns EVENT_DONE) when:
///       * The task reaches a terminal state (SUCCESS, FAIL, TIMEOUT).
///       * The task exceeds the configured timeout (marked as TIMEOUT, then stops).
///       * The _reload pointer is null (defensive).
///   - Reschedules itself (EVENT_CONT) only while the task is still non-terminal.
///   - No idle polling — when no reload is in progress, no checker exists.
///
/// Configurable via records:
///   - proxy.config.admin.reload.timeout: Duration string (default: "1h")
///     Supports: "30s", "5min", "1h", "1 hour 30min", "0" (disabled)
///   - proxy.config.admin.reload.check_interval: Duration string (default: "2s")
///     Minimum: 1s (enforced). How often to check task progress.
///
/// If timeout is 0 or empty, timeout is disabled. Tasks can hang forever (BAD).
/// Use --force (traffic_ctl / RPC API) flag to bypass the in-progress guard and start a new reload.
/// Note: --force only marks the existing tracking task as stale; it does not cancel the actual work.
///
struct ConfigReloadProgress : public Continuation {
  /// Record names for configuration
  static constexpr std::string_view RECORD_TIMEOUT        = "proxy.config.admin.reload.timeout";
  static constexpr std::string_view RECORD_CHECK_INTERVAL = "proxy.config.admin.reload.check_interval";

  /// Defaults
  static constexpr std::string_view DEFAULT_TIMEOUT        = "1h"; ///< 1 hour
  static constexpr std::string_view DEFAULT_CHECK_INTERVAL = "2s"; ///< 2 seconds
  static constexpr int64_t          MIN_CHECK_INTERVAL_MS  = 1000; ///< 1 second minimum

  ConfigReloadProgress(ConfigReloadTaskPtr reload);
  int check_progress(int /* etype */, void * /* data */);

  /// Read timeout value from records, returns 0ms if disabled or invalid
  [[nodiscard]] static std::chrono::milliseconds get_configured_timeout();

  /// Read check interval from records, enforces minimum of 1s
  [[nodiscard]] static std::chrono::milliseconds get_configured_check_interval();

  /// Get the check interval for this instance
  [[nodiscard]] std::chrono::milliseconds
  get_check_interval() const
  {
    return _every;
  }

private:
  ConfigReloadTaskPtr       _reload{nullptr};
  std::chrono::milliseconds _every{std::chrono::seconds{2}}; ///< Set from config in constructor
};

///
/// @brief Tracks the status and progress of a single config reload operation.
///
/// Represents either a top-level (main) reload task or a sub-task for an individual
/// config module. Tasks form a tree: the main task has sub-tasks for each config,
/// and sub-tasks can themselves have children (e.g., SSLClientCoordinator → SSLConfig, SNIConfig).
///
/// State flows: CREATED → IN_PROGRESS → SUCCESS / FAIL / TIMEOUT
/// Parent tasks aggregate state from their children automatically.
///
/// Serialized to YAML via YAML::convert<ConfigReloadTask::Info> for RPC responses.
///
class ConfigReloadTask : public std::enable_shared_from_this<ConfigReloadTask>
{
public:
  enum class State {
    INVALID = -1,
    CREATED,     ///< Initial state — task exists but not started
    IN_PROGRESS, ///< Work is actively happening
    SUCCESS,     ///< Terminal: completed successfully
    FAIL,        ///< Terminal: error occurred
    TIMEOUT      ///< Terminal: task exceeded time limit
  };

  /// Check if a state represents a terminal (final) state
  [[nodiscard]] static constexpr bool
  is_terminal(State s) noexcept
  {
    return s == State::SUCCESS || s == State::FAIL || s == State::TIMEOUT;
  }

  /// Convert State enum to string
  [[nodiscard]] static constexpr std::string_view
  state_to_string(State s) noexcept
  {
    switch (s) {
    case State::INVALID:
      return "invalid";
    case State::CREATED:
      return "created";
    case State::IN_PROGRESS:
      return "in_progress";
    case State::SUCCESS:
      return "success";
    case State::FAIL:
      return "fail";
    case State::TIMEOUT:
      return "timeout";
    }
    return "unknown";
  }

  /// Helper to get current time in milliseconds since epoch
  [[nodiscard]] static int64_t
  now_ms()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  }

  struct Info {
    friend class ConfigReloadTask;
    /// Grant friendship to the specific YAML::convert specialization.
    friend struct YAML::convert<ConfigReloadTask::Info>;
    Info() = default;
    Info(State p_state, std::string_view p_token, std::string_view p_description, bool p_main_task)
      : state(p_state), token(p_token), description(p_description), main_task(p_main_task)
    {
    }

  protected:
    int64_t                          created_time_ms{now_ms()};      ///< milliseconds since epoch
    int64_t                          last_updated_time_ms{now_ms()}; ///< last time this task was updated (ms)
    std::vector<std::string>         logs;                           ///< log messages from handler
    State                            state{State::CREATED};
    std::string                      token;
    std::string                      description;
    std::string                      filename;         ///< source file, if applicable
    std::vector<ConfigReloadTaskPtr> sub_tasks;        ///< child tasks (if any)
    bool                             main_task{false}; ///< true for the top-level reload task
  };

  using self_type    = ConfigReloadTask;
  ConfigReloadTask() = default;
  ConfigReloadTask(std::string_view token, std::string_view description, bool main_task, ConfigReloadTaskPtr parent)
    : _info(State::CREATED, token, description, main_task), _parent{parent}
  {
    if (_info.main_task) {
      _info.state = State::IN_PROGRESS;
    }
  }

  /// Start the periodic progress checker (ConfigReloadProgress).
  /// Only runs once, and only for main tasks. The checker detects stuck tasks
  /// and marks them as TIMEOUT if they exceed the configured time limit.
  void start_progress_checker();

  /// Create a child sub-task and return a ConfigContext wrapping it.
  /// The child inherits the parent's token and if passed, the supplied YAML content.
  [[nodiscard]] ConfigContext add_child(std::string_view description = "");

  self_type &log(std::string const &text);
  void       set_completed();
  void       set_failed();
  void       set_in_progress();

  void
  set_description(std::string_view description)
  {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _info.description = description;
  }

  [[nodiscard]] std::string
  get_description() const
  {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _info.description;
  }

  void
  set_filename(std::string_view filename)
  {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _info.filename = filename;
  }

  [[nodiscard]] std::string
  get_filename() const
  {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _info.filename;
  }

  /// Debug utility: dump task tree to an output stream.
  /// Recursively prints this task and all sub-tasks with indentation.

  [[nodiscard]] bool
  contains_dependents() const
  {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return !_info.sub_tasks.empty();
  }

  /// Get created time in seconds (for Date formatting and metrics)
  [[nodiscard]] std::time_t
  get_created_time() const
  {
    return static_cast<std::time_t>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::milliseconds{_info.created_time_ms}).count());
  }

  /// Get created time in milliseconds since epoch
  [[nodiscard]] int64_t
  get_created_time_ms() const
  {
    return _info.created_time_ms;
  }

  [[nodiscard]] State
  get_state() const
  {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _info.state;
  }

  /// Mark task as TIMEOUT with an optional reason logged
  void mark_as_bad_state(std::string_view reason = "");

  [[nodiscard]] std::vector<std::string>
  get_logs() const
  {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _info.logs;
  }

  [[nodiscard]] std::string_view
  get_token() const
  {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _info.token;
  }

  [[nodiscard]] bool
  is_main_task() const
  {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _info.main_task;
  }

  /// Create a copy of the current task info
  [[nodiscard]] Info
  get_info() const
  {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    Info                                copy = _info;
    copy.last_updated_time_ms                = _atomic_last_updated_ms.load(std::memory_order_acquire);
    return copy;
  }

  /// Get last updated time in seconds (considers subtasks)
  [[nodiscard]] std::time_t get_last_updated_time() const;

  /// Get last updated time in milliseconds (considers subtasks)
  [[nodiscard]] int64_t get_last_updated_time_ms() const;

  void
  update_last_updated_time()
  {
    _atomic_last_updated_ms.store(now_ms(), std::memory_order_release);
  }

  /// Read the last updated time for this task only (no subtask traversal, lock-free)
  [[nodiscard]] int64_t
  get_own_last_updated_time_ms() const
  {
    return _atomic_last_updated_ms.load(std::memory_order_acquire);
  }

private:
  /// Add a pre-created sub-task to this task's children list.
  /// Called by ReloadCoordinator::create_config_context().
  void add_sub_task(ConfigReloadTaskPtr sub_task);
  void on_sub_task_update(State state);
  void aggregate_status();
  void notify_parent();
  void set_state_and_notify(State state);

  mutable std::shared_mutex _mutex;
  bool                      _reload_progress_checker_started{false};
  Info                      _info;
  ConfigReloadTaskPtr       _parent; ///< parent task, if any

  std::atomic<int64_t> _atomic_last_updated_ms{now_ms()};

  friend class ReloadCoordinator;
};
