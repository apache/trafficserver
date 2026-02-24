/** @file

  ReloadCoordinator - tracks config reload sessions and status.

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

#include "mgmt/config/ReloadCoordinator.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "tscore/Diags.h"
#include "tsutil/Metrics.h"
#include "swoc/Errata.h"

namespace
{
DbgCtl dbg_ctl{"config.reload"};
} // namespace

swoc::Errata
ReloadCoordinator::prepare_reload(std::string &token_name, const char *token_prefix, bool force)
{
  std::unique_lock lock(_mutex);

  if (token_name.empty()) {
    token_name = generate_token_name(token_prefix);
  }

  Dbg(dbg_ctl, "Preparing reload task for token: %s (force=%s)", token_name.c_str(), force ? "true" : "false");

  // Check if a reload is already in progress.
  if (_current_task != nullptr) {
    auto state = _current_task->get_state();
    if (!ConfigReloadTask::is_terminal(state) && state != ConfigReloadTask::State::INVALID) {
      if (force) {
        Dbg(dbg_ctl, "Force mode: marking existing reload as stale to start new one");
        _current_task->mark_as_bad_state("Superseded by forced reload");
      } else {
        return swoc::Errata("Reload already in progress for token: {}", token_name);
      }
    }
  }

  Dbg(dbg_ctl, "No reload in progress detected");

  // Create the main task for tracking (status tracking only)
  // The actual reload work is scheduled separately via config::schedule_reload_work()
  create_main_config_task(token_name, "Main reload task");

  Dbg(dbg_ctl, "Reload task created with token: %s", token_name.c_str());
  return {}; // Success — caller will schedule the actual work
}

void
ReloadCoordinator::create_main_config_task(std::string_view token, std::string description)
{
  _current_task = std::make_shared<ConfigReloadTask>(token, description, true /*root*/, nullptr);
  _current_task->start_progress_checker();

  std::string txt;
  swoc::bwprint(txt, "{} - {}", description, swoc::bwf::Date(_current_task->get_created_time()));
  _current_task->set_description(txt);

  // Enforce history size limit — remove oldest when full
  if (_history.size() >= MAX_HISTORY_SIZE) {
    _history.erase(_history.begin());
  }
  _history.push_back(_current_task);

  ts::Metrics &metrics     = ts::Metrics::instance();
  static auto  reconf_time = metrics.lookup("proxy.process.proxy.reconfigure_time");
  metrics[reconf_time].store(
    _current_task->get_created_time()); // This may be different from the actual task reload time. Ok for now.
}

std::string
ReloadCoordinator::generate_token_name(const char *prefix) const
{
  auto now  = std::chrono::system_clock::now();
  auto time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  return std::string(prefix) + std::to_string(time);
}

ConfigContext
ReloadCoordinator::create_config_context(std::string_view config_key, std::string_view description, std::string_view filename)
{
  std::unique_lock lock(_mutex);
  if (!_current_task) {
    // No active reload — return empty context (safe no-op for startup path)
    return ConfigContext{};
  }

  // Dedup: reject if a subtask for this config key already exists.
  //
  // This handles the N-to-1(handler) issue of duplicate subtasks from trigger records. When a config key has N triggers,
  // setup_triggers() registers N independent on_record_change callbacks. The Records system's
  // fires all dirty-record callbacks in one pass, producing N continuations
  // that all carry the same config_key. Only the first one should create a subtask and run
  // the handler; the rest are duplicates.
  if (!config_key.empty() && !ConfigReloadTask::is_terminal(_current_task->get_state()) &&
      _current_task->has_subtask_for_key(config_key)) {
    Dbg(dbg_ctl, "Duplicate reload for config '%.*s' — subtask already exists, skipping", static_cast<int>(config_key.size()),
        config_key.data());
    return ConfigContext{};
  }

  auto task =
    std::make_shared<ConfigReloadTask>(_current_task->get_token(), description, false /*not a main reload job*/, _current_task);
  task->set_config_key(config_key);
  _current_task->add_sub_task(task);

  ConfigContext ctx{task, description, filename};
  return ctx;
}

bool
ReloadCoordinator::is_reload_in_progress() const
{
  std::shared_lock lock(_mutex);

  if (_current_task == nullptr) {
    Dbg(dbg_ctl, "No current task found, reload not in progress.");
    return false;
  }

  auto state = _current_task->get_state();
  if (!ConfigReloadTask::is_terminal(state) && state != ConfigReloadTask::State::INVALID) {
    Dbg(dbg_ctl, "Found reload in progress for task: %s", _current_task->get_token().c_str());
    return true;
  } else {
    auto state_str = ConfigReloadTask::state_to_string(state);
    Dbg(dbg_ctl, "Current task is not running, state: %s", state_str.data());
  }

  return false;
}

std::pair<bool, ConfigReloadTask::Info>
ReloadCoordinator::find_by_token(std::string_view token_name) const
{
  std::shared_lock lock(_mutex);
  Dbg(dbg_ctl, "Search %s, history size=%d", token_name.data(), static_cast<int>(_history.size()));

  auto it =
    std::find_if(_history.begin(), _history.end(), [&token_name](auto const &task) { return task->get_token() == token_name; });

  if (it != _history.end()) {
    return {true, (*it)->get_info()};
  }
  return {false, ConfigReloadTask::Info{}};
}

std::vector<ConfigReloadTask::Info>
ReloadCoordinator::get_all(std::size_t N) const
{
  std::shared_lock                    lock(_mutex);
  std::vector<ConfigReloadTask::Info> result;
  if (N == 0) {
    N = _history.size();
  }

  result.reserve(std::min(N, _history.size()));

  auto start_it = _history.begin();
  if (_history.size() > N) {
    start_it = _history.end() - N;
  }

  std::transform(start_it, _history.end(), std::back_inserter(result),
                 [](const std::shared_ptr<ConfigReloadTask> &task) { return task->get_info(); });

  return result;
}

bool
ReloadCoordinator::mark_task_as_stale(std::string_view token, std::string_view reason)
{
  std::unique_lock lock(_mutex);

  std::shared_ptr<ConfigReloadTask> task_to_mark;

  if (token.empty()) {
    task_to_mark = _current_task;
  } else {
    auto it = std::find_if(_history.begin(), _history.end(), [&token](auto const &task) { return task->get_token() == token; });
    if (it != _history.end()) {
      task_to_mark = *it;
    }
  }

  if (!task_to_mark) {
    Dbg(dbg_ctl, "No task found to mark stale (token: %.*s)", static_cast<int>(token.size()),
        token.empty() ? "<current>" : token.data());
    return false;
  }

  auto state = task_to_mark->get_state();
  if (ConfigReloadTask::is_terminal(state)) {
    Dbg(dbg_ctl, "Task %s already in terminal state (%.*s), cannot mark stale", task_to_mark->get_token().c_str(),
        static_cast<int>(ConfigReloadTask::state_to_string(state).size()), ConfigReloadTask::state_to_string(state).data());
    return false;
  }

  auto state_str = ConfigReloadTask::state_to_string(state);
  Dbg(dbg_ctl, "Marking task %s as stale (state: %s) - reason: %s", task_to_mark->get_token().c_str(), state_str.data(),
      reason.data());

  task_to_mark->mark_as_bad_state(reason);
  return true;
}
