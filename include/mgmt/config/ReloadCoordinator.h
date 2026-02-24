/** @file
 *
 *  ReloadCoordinator - Manages config reload lifecycle and concurrency
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

#include <string_view>
#include <string>

#include <mutex>
#include <shared_mutex>
#include <swoc/Errata.h>
#include <tscore/ink_platform.h>
#include "tscore/Diags.h"
#include "mgmt/config/ConfigReloadTrace.h"
#include "mgmt/config/ConfigContext.h"

class ReloadCoordinator
{
public:
  /// Initialize a new reload session. Generates a token, checks for in-progress reloads,
  /// and creates the main tracking task. Use --force to bypass the in-progress guard.
  [[nodiscard]] swoc::Errata prepare_reload(std::string &token_name, const char *token_prefix = "rldtk-", bool force = false);

  /// Return snapshots of the last N reload tasks (0 = all). Most recent first.
  [[nodiscard]] std::vector<ConfigReloadTask::Info> get_all(std::size_t N = 0) const;

  /// Look up a reload task by its token. Returns {found, info_snapshot}.
  [[nodiscard]] std::pair<bool, ConfigReloadTask::Info> find_by_token(std::string_view token) const;

  /// Singleton access
  static ReloadCoordinator &
  Get_Instance()
  {
    static ReloadCoordinator instance;
    return instance;
  }

  [[nodiscard]] std::shared_ptr<ConfigReloadTask>
  get_current_task() const
  {
    std::shared_lock lock(_mutex);
    return _current_task;
  }

  /// Create a sub-task context for the given config key.
  ///
  /// Deduplication: if a subtask with the same @a config_key already exists on the
  /// current main task, returns an empty ConfigContext (operator bool() == false).
  /// This prevents the same handler from running multiple times in a single reload
  /// cycle when multiple trigger records map to the same config key.
  ///
  /// @see ConfigRegistry::setup_triggers() — N trigger records produce N callbacks.
  [[nodiscard]] ConfigContext create_config_context(std::string_view config_key, std::string_view description = "",
                                                    std::string_view filename = "");

  [[nodiscard]] bool is_reload_in_progress() const;

  [[nodiscard]] bool
  has_token(std::string_view token_name) const
  {
    std::shared_lock lock(_mutex);
    return std::any_of(_history.begin(), _history.end(),
                       [&token_name](const std::shared_ptr<ConfigReloadTask> &task) { return task->get_token() == token_name; });
  }

  ///
  /// @brief Mark a reload task as stale/superseded
  ///
  /// Used internally when --force flag (jsonrpc or traffic_ctl) is specified to mark the current
  /// in-progress task as stale before starting a new one.
  ///
  /// Note: This does NOT stop running handlers. Any handlers actively
  /// processing will continue. This only updates the task tracking status
  /// so we can spawn another reload task. Use with caution.
  ///
  /// @param token The token of the task to mark stale (empty = current task)
  /// @param reason Reason for marking stale (will be logged)
  /// @return true if task was found and marked, false otherwise
  ///
  bool mark_task_as_stale(std::string_view token = "", std::string_view reason = "Superseded by new reload");

private:
  static constexpr size_t MAX_HISTORY_SIZE = 100; ///< Maximum number of reload tasks to keep in history. TODO: maybe configurable?

  /// Create main task (caller must hold unique lock)
  void create_main_config_task(std::string_view token, std::string description);

  std::string generate_token_name(const char *prefix) const;

  std::vector<std::shared_ptr<ConfigReloadTask>> _history;
  std::shared_ptr<ConfigReloadTask>              _current_task;
  mutable std::shared_mutex                      _mutex;

  // Prevent construction/copying from outside
  ReloadCoordinator()                                     = default;
  ReloadCoordinator(const ReloadCoordinator &)            = delete;
  ReloadCoordinator &operator=(const ReloadCoordinator &) = delete;
};
