/** @file

  ConfigReloadTrace — reload progress checker and task timeout detection.

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

#include "mgmt/config/ConfigReloadTrace.h"
#include "mgmt/config/ConfigContext.h"
#include "records/RecCore.h"
#include "tsutil/Metrics.h"
#include "tsutil/ts_time_parser.h"

namespace
{
DbgCtl dbg_ctl_config{"config.reload"};

/// Helper to read a time duration from records configuration.
[[nodiscard]] std::chrono::milliseconds
read_time_record(std::string_view record_name, std::string_view default_value, std::chrono::milliseconds fallback,
                 std::chrono::milliseconds minimum = std::chrono::milliseconds{0})
{
  // record_name / default_value are compile-time string_view constants, always null-terminated.
  char str[128] = {0};

  auto             result = RecGetRecordString(record_name.data(), str, sizeof(str));
  std::string_view value  = (result.has_value() && !result->empty()) ? result.value() : default_value;

  auto [duration, errata] = ts::time_parser(value);
  if (!errata.is_ok()) {
    Dbg(dbg_ctl_config, "Failed to parse '%.*s' value '%.*s': using fallback", static_cast<int>(record_name.size()),
        record_name.data(), static_cast<int>(value.size()), value.data());
    return fallback;
  }

  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

  // Enforce minimum if specified
  if (minimum.count() > 0 && ms < minimum) {
    Dbg(dbg_ctl_config, "'%.*s' value %lldms below minimum, using %lldms", static_cast<int>(record_name.size()), record_name.data(),
        static_cast<long long>(ms.count()), static_cast<long long>(minimum.count()));
    return minimum;
  }

  return ms;
}
} // namespace

std::chrono::milliseconds
ConfigReloadProgress::get_configured_timeout()
{
  return read_time_record(RECORD_TIMEOUT, DEFAULT_TIMEOUT, std::chrono::hours{1});
}

std::chrono::milliseconds
ConfigReloadProgress::get_configured_check_interval()
{
  return read_time_record(RECORD_CHECK_INTERVAL, DEFAULT_CHECK_INTERVAL, std::chrono::seconds{2},
                          std::chrono::milliseconds{MIN_CHECK_INTERVAL_MS});
}

ConfigContext
ConfigReloadTask::add_child(std::string_view description)
{
  std::unique_lock<std::shared_mutex> lock(_mutex);
  // Read token directly - can't call get_token() as it would deadlock (tries to acquire shared_lock on same mutex)
  auto trace = std::make_shared<ConfigReloadTask>(_info.token, description, false, shared_from_this());
  _info.sub_tasks.push_back(trace);
  return ConfigContext{trace, description};
}

ConfigReloadTask &
ConfigReloadTask::log(std::string const &text)
{
  std::unique_lock<std::shared_mutex> lock(_mutex);
  _info.logs.push_back(text);
  return *this;
}

void
ConfigReloadTask::add_sub_task(ConfigReloadTaskPtr sub_task)
{
  std::unique_lock<std::shared_mutex> lock(_mutex);
  Dbg(dbg_ctl_config, "Adding subtask %.*s to task %s", static_cast<int>(sub_task->get_description().size()),
      sub_task->get_description().data(), _info.description.c_str());
  _info.sub_tasks.push_back(sub_task);
}

void
ConfigReloadTask::set_in_progress()
{
  this->set_state_and_notify(State::IN_PROGRESS);
}

void
ConfigReloadTask::set_completed()
{
  this->set_state_and_notify(State::SUCCESS);
}

void
ConfigReloadTask::set_failed()
{
  this->set_state_and_notify(State::FAIL);
}

void
ConfigReloadTask::mark_as_bad_state(std::string_view reason)
{
  std::unique_lock<std::shared_mutex> lock(_mutex);
  // Once a task reaches SUCCESS, FAIL, or TIMEOUT, reject further transitions.
  if (is_terminal(_info.state)) {
    Warning("ConfigReloadTask '%s': ignoring mark_as_bad_state from %.*s — already terminal.", _info.description.c_str(),
            static_cast<int>(state_to_string(_info.state).size()), state_to_string(_info.state).data());
    return;
  }
  _info.state = State::TIMEOUT;
  _atomic_last_updated_ms.store(now_ms(), std::memory_order_release);
  if (!reason.empty()) {
    // Push directly to avoid deadlock (log() would try to acquire same mutex)
    _info.logs.emplace_back(reason);
  }
}

void
ConfigReloadTask::notify_parent()
{
  Dbg(dbg_ctl_config, "parent null =%s , parent main task? %s", _parent ? "false" : "true",
      (_parent && _parent->is_main_task()) ? "true" : "false");

  if (_parent) {
    _parent->aggregate_status();
  }
}

void
ConfigReloadTask::set_state_and_notify(State state)
{
  {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    if (_info.state == state) {
      return;
    }
    // Once a task reaches a terminal state, reject further transitions.
    if (is_terminal(_info.state)) {
      Warning("ConfigReloadTask '%s': ignoring transition from %.*s to %.*s — already terminal.", _info.description.c_str(),
              static_cast<int>(state_to_string(_info.state).size()), state_to_string(_info.state).data(),
              static_cast<int>(state_to_string(state).size()), state_to_string(state).data());
      return;
    }
    Dbg(dbg_ctl_config, "State changed to %.*s for task %s", static_cast<int>(state_to_string(state).size()),
        state_to_string(state).data(), _info.description.c_str());
    _info.state = state;
    _atomic_last_updated_ms.store(now_ms(), std::memory_order_release);
  }

  // Now that the lock is released, we can safely notify the parent.
  this->notify_parent();
}

void
ConfigReloadTask::aggregate_status()
{
  // Use unique_lock throughout to avoid TOCTOU race and data races
  std::unique_lock<std::shared_mutex> lock(_mutex);

  if (_info.sub_tasks.empty()) {
    // No subtasks - keep current state (don't change to CREATED)
    return;
  }

  bool any_failed      = false;
  bool any_in_progress = false;
  bool all_success     = true;
  bool all_created     = true;

  for (const auto &sub_task : _info.sub_tasks) {
    State sub_state = sub_task->get_state();
    switch (sub_state) {
    case State::FAIL:
    case State::TIMEOUT: // Treat TIMEOUT as failure
      any_failed  = true;
      all_success = false;
      all_created = false;
      break;
    case State::IN_PROGRESS: // Handle IN_PROGRESS explicitly!
      any_in_progress = true;
      all_success     = false;
      all_created     = false;
      break;
    case State::SUCCESS:
      all_created = false;
      break;
    case State::CREATED:
      all_success = false;
      break;
    case State::INVALID:
    default:
      // Unknown state - treat as not success, not created
      all_success = false;
      all_created = false;
      break;
    }
  }

  // Determine new parent state based on children
  // Priority: FAIL/TIMEOUT > IN_PROGRESS > SUCCESS > CREATED
  State new_state;
  if (any_failed) {
    new_state = State::FAIL;
  } else if (any_in_progress) {
    // If any subtask is still working, parent is IN_PROGRESS
    new_state = State::IN_PROGRESS;
  } else if (all_success) {
    Dbg(dbg_ctl_config, "Setting %s task '%s' to SUCCESS (all subtasks succeeded)", _info.main_task ? "main" : "sub",
        _info.description.c_str());
    new_state = State::SUCCESS;
  } else if (all_created && !_info.main_task) {
    Dbg(dbg_ctl_config, "Setting %s task '%s' to CREATED (all subtasks created)", _info.main_task ? "main" : "sub",
        _info.description.c_str());
    new_state = State::CREATED;
  } else {
    // Mixed state or main task with created subtasks - keep as IN_PROGRESS
    Dbg(dbg_ctl_config, "Setting %s task '%s' to IN_PROGRESS (mixed state)", _info.main_task ? "main" : "sub",
        _info.description.c_str());
    new_state = State::IN_PROGRESS;
  }

  // Only update if state actually changed
  if (_info.state != new_state) {
    _info.state = new_state;
    _atomic_last_updated_ms.store(now_ms(), std::memory_order_release);
  }

  // Release lock before notifying parent to avoid potential deadlock
  lock.unlock();

  if (_parent) {
    _parent->aggregate_status();
  }
}

int64_t
ConfigReloadTask::get_last_updated_time_ms() const
{
  int64_t last_time_ms = _atomic_last_updated_ms.load(std::memory_order_acquire);

  std::shared_lock<std::shared_mutex> lock(_mutex);
  for (const auto &sub_task : _info.sub_tasks) {
    int64_t sub_time_ms = sub_task->get_own_last_updated_time_ms();
    if (sub_time_ms > last_time_ms) {
      last_time_ms = sub_time_ms;
    }
  }
  return last_time_ms;
}

std::time_t
ConfigReloadTask::get_last_updated_time() const
{
  return static_cast<std::time_t>(
    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::milliseconds{get_last_updated_time_ms()}).count());
}
void
ConfigReloadTask::start_progress_checker()
{
  std::unique_lock<std::shared_mutex> lock(_mutex);
  if (!_reload_progress_checker_started && _info.main_task && _info.state == State::IN_PROGRESS) { // can only start once
    auto *checker = new ConfigReloadProgress(shared_from_this());
    eventProcessor.schedule_in(checker, HRTIME_MSECONDS(checker->get_check_interval().count()), ET_TASK);
    _reload_progress_checker_started = true;
  }
}

// reload progress checker
int
ConfigReloadProgress::check_progress(int /* etype */, void * /* data */)
{
  Dbg(dbg_ctl_config, "Checking progress for reload task %.*s - descr: %.*s",
      _reload ? static_cast<int>(_reload->get_token().size()) : 4, _reload ? _reload->get_token().data() : "null",
      _reload ? static_cast<int>(_reload->get_description().size()) : 4, _reload ? _reload->get_description().data() : "null");
  if (_reload == nullptr) {
    return EVENT_DONE;
  }

  auto const current_state = _reload->get_state();
  if (ConfigReloadTask::is_terminal(current_state)) {
    Dbg(dbg_ctl_config, "Reload task %.*s is in %.*s state, stopping progress check.",
        static_cast<int>(_reload->get_token().size()), _reload->get_token().data(),
        static_cast<int>(ConfigReloadTask::state_to_string(current_state).size()),
        ConfigReloadTask::state_to_string(current_state).data());
    return EVENT_DONE;
  }

  // Get configured timeout (read dynamically to allow runtime changes)
  // Returns 0ms if disabled (timeout string is "0" or empty)
  auto max_running_time = get_configured_timeout();

  // Check if timeout is disabled (0ms means disabled)
  if (max_running_time.count() == 0) {
    Dbg(dbg_ctl_config, "Timeout disabled - task %.*s will run indefinitely until completion or manual cancellation",
        static_cast<int>(_reload->get_token().size()), _reload->get_token().data());
    // Still reschedule to detect completion, but don't timeout
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(_every.count()), ET_TASK);
    return EVENT_CONT;
  }

  // ok, it's running, should we keep it running?
  auto        ct  = std::chrono::system_clock::from_time_t(_reload->get_created_time());
  auto        lut = std::chrono::system_clock::from_time_t(_reload->get_last_updated_time());
  std::string buf;
  if (lut + max_running_time < std::chrono::system_clock::now()) {
    if (_reload->contains_dependents()) {
      swoc::bwprint(buf, "Task {} timed out after {}ms with no reload action (no config to reload). Last state: {}",
                    _reload->get_token(), max_running_time.count(), ConfigReloadTask::state_to_string(current_state));
    } else {
      swoc::bwprint(buf, "Reload task {} timed out after {}ms. Previous state: {}.", _reload->get_token(), max_running_time.count(),
                    ConfigReloadTask::state_to_string(current_state));
    }
    _reload->mark_as_bad_state(buf);
    Dbg(dbg_ctl_config, "%s", buf.c_str());
    return EVENT_DONE;
  }

  swoc::bwprint(buf,
                "Reload task {} ongoing with state {}, created at {} and last update at {}. Timeout in {}ms. Will check again.",
                _reload->get_token(), ConfigReloadTask::state_to_string(current_state),
                swoc::bwf::Date(std::chrono::system_clock::to_time_t(ct)),
                swoc::bwf::Date(std::chrono::system_clock::to_time_t(lut)), max_running_time.count());
  Dbg(dbg_ctl_config, "%s", buf.c_str());

  eventProcessor.schedule_in(this, HRTIME_MSECONDS(_every.count()), ET_TASK);
  return EVENT_CONT;
}

ConfigReloadProgress::ConfigReloadProgress(ConfigReloadTaskPtr reload)
  : Continuation(new_ProxyMutex()), _reload{reload}, _every{get_configured_check_interval()}
{
  SET_HANDLER(&ConfigReloadProgress::check_progress);
}
