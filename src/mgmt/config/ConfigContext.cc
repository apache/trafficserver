/** @file
 *
 *  ConfigContext implementation
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.
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
