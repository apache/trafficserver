/** @file

  ConfigContext implementation

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

#include "mgmt/config/ConfigContext.h"
#include "mgmt/config/ConfigReloadTrace.h"
#include "mgmt/config/ReloadCoordinator.h"

#include <yaml-cpp/yaml.h>

ConfigContext::ConfigContext(std::shared_ptr<ConfigReloadTask> t, std::string_view description, std::string_view filename)
  : _task(t)
{
  if (auto p = _task.lock()) {
    if (!description.empty()) {
      p->set_description(description);
    }
    if (!filename.empty()) {
      p->set_filename(filename);
    }
  }
}

bool
ConfigContext::is_terminal() const
{
  if (auto p = _task.lock()) {
    return ConfigReloadTask::is_terminal(p->get_state());
  }
  return true; // expired task is supposed to be terminal
}

void
ConfigContext::in_progress(std::string_view text)
{
  if (auto p = _task.lock()) {
    p->set_in_progress();
    if (!text.empty()) {
      p->log(std::string{text});
    }
  }
}

void
ConfigContext::log(std::string_view text)
{
  if (auto p = _task.lock()) {
    p->log(std::string{text});
  }
}

void
ConfigContext::complete(std::string_view text)
{
  if (auto p = _task.lock()) {
    p->set_completed();
    if (!text.empty()) {
      p->log(std::string{text});
    }
  }
}

void
ConfigContext::fail(std::string_view reason)
{
  if (auto p = _task.lock()) {
    p->set_failed();
    if (!reason.empty()) {
      p->log(std::string{reason});
    }
  }
}

void
ConfigContext::fail(swoc::Errata const &errata, std::string_view summary)
{
  if (auto p = _task.lock()) {
    p->set_failed();
    // Log the summary first
    if (!summary.empty()) {
      p->log(std::string{summary});
    }
    // Log each error from the errata
    for (auto const &err : errata) {
      p->log(std::string{err.text()});
    }
  }
}

std::string_view
ConfigContext::get_description() const
{
  if (auto p = _task.lock()) {
    return p->get_description();
  }
  return "";
}

ConfigContext
ConfigContext::add_dependent_ctx(std::string_view description)
{
  if (auto p = _task.lock()) {
    auto child = p->add_child(description);
    // child task will get the full content of the parent task
    // TODO: eventyually we can have a "key" passed so child module
    // only gets their node of interest.
    child._supplied_yaml = _supplied_yaml;
    return child;
  }
  return {};
}

void
ConfigContext::set_supplied_yaml(YAML::Node node)
{
  _supplied_yaml = node; // YAML::Node has no move semantics; copy is cheap (ref-counted).
}

YAML::Node
ConfigContext::supplied_yaml() const
{
  return _supplied_yaml;
}

namespace config
{
ConfigContext
make_config_reload_context(std::string_view description, std::string_view filename)
{
  return ReloadCoordinator::Get_Instance().create_config_context(description, filename);
}
} // namespace config
