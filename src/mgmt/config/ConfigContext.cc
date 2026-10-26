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

#include <optional>

// Shared by every copy of a context, so a built Payload is never changed: the
// setters build a new one instead, because assigning into a YAML::Node writes
// through to every other handle on the same node. The YAML it holds is still
// shared, so a write through a returned node reaches every copy.
struct ConfigContext::Payload {
  std::optional<YAML::Node> supplied_yaml;
  std::optional<YAML::Node> reload_directives;
};

// ConfigContext is passed by value to every reload handler, and Coverity flags by-value
// parameters over 128 bytes. A bound of 64 keeps it well under that with libstdc++ and
// libc++ (it is 32 with both).
static_assert(sizeof(ConfigContext) <= 64, "ConfigContext is passed by value; add new state to ConfigContext::Payload");

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
      p->log(DL_Note, std::string{text});
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
ConfigContext::log(DiagsLevel level, std::string_view text)
{
  if (auto p = _task.lock()) {
    p->log(level, std::string{text});
  }
}

void
ConfigContext::log(swoc::Errata const &errata)
{
  if (auto p = _task.lock()) {
    for (auto const &annotation : errata) {
      p->log(static_cast<DiagsLevel>(static_cast<int>(annotation.severity())), std::string{annotation.text()});
    }
  }
}

void
ConfigContext::complete(std::string_view text)
{
  if (auto p = _task.lock()) {
    p->set_completed();
    if (!text.empty()) {
      p->log(DL_Note, std::string{text});
    }
  }
}

void
ConfigContext::fail(std::string_view reason)
{
  if (auto p = _task.lock()) {
    p->set_failed();
    if (!reason.empty()) {
      p->log(DL_Error, std::string{reason});
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
      p->log(DL_Error, std::string{summary});
    }
    // Log each error from the errata
    for (auto const &err : errata) {
      p->log(static_cast<DiagsLevel>(static_cast<int>(err.severity())), std::string{err.text()});
    }
  }
}

std::string
ConfigContext::get_description() const
{
  if (auto p = _task.lock()) {
    return p->get_description();
  }
  return {};
}

std::string
ConfigContext::get_reload_token() const
{
  if (auto p = _task.lock()) {
    return p->get_token();
  }
  return {};
}

ConfigContext
ConfigContext::add_dependent_ctx(std::string_view description, std::string_view filename)
{
  if (auto p = _task.lock()) {
    auto child = p->add_child(description, filename);
    // child task will get the full content of the parent task
    // TODO: eventually we can have a "key" passed so child module
    // only gets their node of interest.
    child._payload = _payload;
    return child;
  }
  return {};
}

void
ConfigContext::set_supplied_yaml(YAML::Node node)
{
  auto next = _payload ? std::make_shared<Payload>(*_payload) : std::make_shared<Payload>();
  // emplace() copy-constructs the node; assigning into an engaged optional would write through.
  next->supplied_yaml.emplace(node); // YAML::Node has no move semantics; copy is cheap (ref-counted).
  _payload = std::move(next);
}

void
ConfigContext::set_plugin_name(std::string_view name)
{
  if (auto p = _task.lock()) {
    p->set_plugin_name(name);
  }
}

YAML::Node
ConfigContext::supplied_yaml() const
{
  if (_payload && _payload->supplied_yaml) {
    return *_payload->supplied_yaml;
  }
  // A fresh node each time: a default YAML::Node is a (truthy) Null, and a shared one
  // would turn defined for every caller once one of them wrote into it.
  return YAML::Node{YAML::NodeType::Undefined};
}

void
ConfigContext::set_reload_directives(YAML::Node node)
{
  auto next = _payload ? std::make_shared<Payload>(*_payload) : std::make_shared<Payload>();
  next->reload_directives.emplace(node);
  _payload = std::move(next);
}

YAML::Node
ConfigContext::reload_directives() const
{
  if (_payload && _payload->reload_directives) {
    return *_payload->reload_directives;
  }
  return YAML::Node{YAML::NodeType::Undefined};
}

namespace config
{
ConfigContext
make_config_reload_context(std::string_view description, std::string_view filename)
{
  return ReloadCoordinator::Get_Instance().create_config_context({}, description, filename);
}
} // namespace config
