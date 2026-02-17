/** @file
 *
 *  Config Registry implementation
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

#include "mgmt/config/ConfigRegistry.h"

#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EventProcessor.h"
#include "iocore/eventsystem/Tasks.h"
#include "records/RecCore.h"
#include "mgmt/config/ConfigContext.h"
#include "mgmt/config/FileManager.h"
#include "mgmt/config/ReloadCoordinator.h"
#include "tscore/Diags.h"
#include "tscore/ink_assert.h"
#include "tscore/Layout.h"
#include "tsutil/ts_errata.h"
#include "swoc/TextView.h"

#include <yaml-cpp/yaml.h>

namespace
{
DbgCtl dbg_ctl{"config.reload"};

// Resolve a config filename: read the current value from the named record,
// fallback to default_filename if the record is empty or absent.
// Returns the bare filename (no sysconfdir prefix) — suitable for FileManager::addFile().
std::string
resolve_config_filename(const char *record_name, const std::string &default_filename)
{
  if (record_name && record_name[0] != '\0') {
    if (auto val = RecGetRecordStringAlloc(record_name); val && !val->empty()) {
      return std::string{*val};
    }
  }
  return default_filename;
}

///
// Continuation that executes config reload on ET_TASK thread
// Used by ConfigRegistry::schedule_reload() for async rpc reloads (content supplied via RPC) or file reloads
//
class ScheduledReloadContinuation : public Continuation
{
public:
  ScheduledReloadContinuation(Ptr<ProxyMutex> &m, std::string key) : Continuation(m.get()), _config_key(std::move(key))
  {
    SET_HANDLER(&ScheduledReloadContinuation::execute);
  }

  int
  execute(int /* event */, Event * /* e */)
  {
    Dbg(dbg_ctl, "ScheduledReloadContinuation: executing reload for config '%s'", _config_key.c_str());
    config::ConfigRegistry::Get_Instance().execute_reload(_config_key);
    delete this;
    return EVENT_DONE;
  }

private:
  std::string _config_key;
};

///
// Continuation used by record-triggered reloads (via on_record_change callback)
// This is separate from ScheduledReloadContinuation as it always reloads from file
//
class RecordTriggeredReloadContinuation : public Continuation
{
public:
  RecordTriggeredReloadContinuation(Ptr<ProxyMutex> &m, std::string key) : Continuation(m.get()), _config_key(std::move(key))
  {
    SET_HANDLER(&RecordTriggeredReloadContinuation::execute);
  }

  int
  execute(int /* event */, Event * /* e */)
  {
    Dbg(dbg_ctl, "RecordTriggeredReloadContinuation: executing reload for config '%s'", _config_key.c_str());

    auto const *entry = config::ConfigRegistry::Get_Instance().find(_config_key);

    if (entry == nullptr) {
      Warning("Config key '%s' not found in registry", _config_key.c_str());
    } else if (!entry->handler) {
      Warning("Config '%s' has no handler", _config_key.c_str());
    } else {
      // File reload: create context, invoke handler directly
      auto ctx = ReloadCoordinator::Get_Instance().create_config_context(_config_key, entry->resolve_filename());
      ctx.in_progress();
      entry->handler(ctx);
      Dbg(dbg_ctl, "Config '%s' file reload completed", _config_key.c_str());
    }

    delete this;
    return EVENT_DONE;
  }

private:
  std::string _config_key;
};

///
// Callback invoked by the Records system when a trigger record changes.
// Only fires for records registered with ConfigRegistry (via trigger_records
// in register_config()/register_record_config(), or via add_file_dependency()).
//
int
on_record_change(const char *name, RecDataT /* data_type */, RecData /* data */, void *cookie)
{
  auto *ctx = static_cast<config::ConfigRegistry::TriggerContext *>(cookie);

  Dbg(dbg_ctl, "Record '%s' changed, scheduling reload for config '%s'", name, ctx->config_key.c_str());

  // Schedule file reload on ET_TASK thread (always file-based, no rpc-supplied content)
  eventProcessor.schedule_imm(new RecordTriggeredReloadContinuation(ctx->mutex, ctx->config_key), ET_TASK);

  return 0;
}

} // anonymous namespace

namespace config
{

ConfigRegistry &
ConfigRegistry::Get_Instance()
{
  static ConfigRegistry _instance;
  return _instance;
}

std::string
ConfigRegistry::Entry::resolve_filename() const
{
  auto fname = resolve_config_filename(filename_record.empty() ? nullptr : filename_record.c_str(), default_filename);

  // Build full path if not already absolute
  if (!fname.empty() && fname[0] != '/') {
    return Layout::get()->sysconfdir + "/" + fname;
  }
  return fname;
}

void
ConfigRegistry::do_register(Entry entry)
{
  const char *type_str = (entry.type == ConfigType::YAML) ? "YAML" : "legacy";

  Dbg(dbg_ctl, "Registering %s config '%s' (default: %s, record: %s, triggers: %zu)", type_str, entry.key.c_str(),
      entry.default_filename.c_str(), entry.filename_record.empty() ? "<none>" : entry.filename_record.c_str(),
      entry.trigger_records.size());

  std::unique_lock lock(_mutex);
  auto [it, inserted] = _entries.emplace(entry.key, std::move(entry));

  if (inserted) {
    setup_triggers(it->second);

    // Register with FileManager for mtime-based file change detection.
    // This replaces the manual registerFile() call in AddConfigFilesHere.cc.
    // When rereadConfig() detects the file changed, it calls RecSetSyncRequired()
    // on the filename_record, which eventually triggers our on_record_change callback.
    if (!it->second.default_filename.empty()) {
      auto resolved = resolve_config_filename(it->second.filename_record.empty() ? nullptr : it->second.filename_record.c_str(),
                                              it->second.default_filename);
      FileManager::instance().addFile(resolved.c_str(), it->second.filename_record.c_str(), false, false);
    }
  } else {
    Warning("Config '%s' already registered, ignoring", it->first.c_str());
  }
}

void
ConfigRegistry::register_config(const std::string &key, const std::string &default_filename, const std::string &filename_record,
                                ConfigReloadHandler handler, ConfigSource source,
                                std::initializer_list<const char *> trigger_records)
{
  Entry entry;
  entry.key              = key;
  entry.default_filename = default_filename;
  entry.filename_record  = filename_record;
  entry.handler          = std::move(handler);
  entry.source           = source;

  // Infer type from extension: .yaml/.yml = YAML (supports rpc reload), else = LEGACY
  swoc::TextView fn{default_filename};
  entry.type = (fn.ends_with(".yaml") || fn.ends_with(".yml")) ? ConfigType::YAML : ConfigType::LEGACY;

  for (auto const *record : trigger_records) {
    entry.trigger_records.emplace_back(record);
  }

  do_register(std::move(entry));
}

void
ConfigRegistry::register_record_config(const std::string &key, ConfigReloadHandler handler,
                                       std::initializer_list<const char *> trigger_records)
{
  register_config(key, "", "", std::move(handler), ConfigSource::RecordOnly, trigger_records);
}

void
ConfigRegistry::setup_triggers(Entry &entry)
{
  for (auto const &record : entry.trigger_records) {
    wire_record_callback(record.c_str(), entry.key);
  }
}

int
ConfigRegistry::wire_record_callback(const char *record_name, const std::string &config_key)
{
  // TriggerContext lives for the lifetime of the process — intentionally not deleted
  // as RecRegisterConfigUpdateCb stores the pointer and may invoke the callback at any time.
  // This is a small, bounded allocation (one per trigger record).
  auto *ctx       = new TriggerContext();
  ctx->config_key = config_key;
  ctx->mutex      = new_ProxyMutex();

  Dbg(dbg_ctl, "Wiring record callback '%s' to config '%s'", record_name, config_key.c_str());

  int result = RecRegisterConfigUpdateCb(record_name, on_record_change, ctx);
  if (result != 0) {
    Warning("Failed to wire callback for record '%s' on config '%s'", record_name, config_key.c_str());
    delete ctx;
    return -1;
  }
  return 0;
}

int
ConfigRegistry::attach(const std::string &key, const char *record_name)
{
  std::string config_key;

  // Single lock for check-and-modify.
  {
    std::unique_lock lock(_mutex);
    auto             it = _entries.find(key);
    if (it == _entries.end()) {
      Warning("Cannot attach trigger to unknown config: %s", key.c_str());
      return -1;
    }

    // Store record in entry — owned trigger
    it->second.trigger_records.emplace_back(record_name);
    config_key = it->second.key;
  }
  // Lock released before external call to RecRegisterConfigUpdateCb

  Dbg(dbg_ctl, "Attaching trigger '%s' to config '%s'", record_name, key.c_str());
  return wire_record_callback(record_name, config_key);
}

int
ConfigRegistry::add_file_dependency(const std::string &key, const char *filename_record, const char *default_filename,
                                    bool is_required)
{
  std::string config_key;

  {
    std::shared_lock lock(_mutex);
    auto             it = _entries.find(key);
    if (it == _entries.end()) {
      Warning("Cannot add file dependency to unknown config: %s", key.c_str());
      return -1;
    }
    config_key = it->second.key;
  }

  auto resolved = resolve_config_filename(filename_record, default_filename);

  Dbg(dbg_ctl, "Adding file dependency '%s' (resolved: %s) to config '%s'", filename_record, resolved.c_str(), key.c_str());

  // Register with FileManager for mtime-based change detection.
  // When rereadConfig() detects the file changed, it calls RecSetSyncRequired()
  // on the filename_record, which triggers on_record_change below.
  FileManager::instance().addFile(resolved.c_str(), filename_record, false, is_required);

  // Wire callback — dependency trigger, not stored in trigger_records.
  return wire_record_callback(filename_record, config_key);
}

int
ConfigRegistry::add_file_and_node_dependency(const std::string &key, const std::string &dep_key, const char *filename_record,
                                             const char *default_filename, bool is_required)
{
  // Do the normal file dependency work (FileManager registration + record callback wiring)
  int ret = add_file_dependency(key, filename_record, default_filename, is_required);
  if (ret != 0) {
    return ret;
  }

  // Register the dep_key -> parent mapping for RPC routing
  std::unique_lock lock(_mutex);
  if (_entries.count(dep_key)) {
    Warning("ConfigRegistry: dep_key '%s' collides with an existing entry key, ignoring", dep_key.c_str());
    return -1;
  }
  if (_dep_key_to_parent.count(dep_key)) {
    Warning("ConfigRegistry: dep_key '%s' already registered, ignoring", dep_key.c_str());
    return -1;
  }
  _dep_key_to_parent[dep_key] = key;
  Dbg(dbg_ctl, "Dependency key '%s' routes to parent '%s'", dep_key.c_str(), key.c_str());
  return 0;
}

std::pair<std::string, ConfigRegistry::Entry const *>
ConfigRegistry::resolve(const std::string &key) const
{
  std::shared_lock lock(_mutex);

  // Direct entry lookup
  auto it = _entries.find(key);
  if (it != _entries.end()) {
    return {key, &it->second};
  }

  // Dependency key lookup
  auto dep_it = _dep_key_to_parent.find(key);
  if (dep_it != _dep_key_to_parent.end()) {
    auto parent_it = _entries.find(dep_it->second);
    if (parent_it != _entries.end()) {
      return {dep_it->second, &parent_it->second};
    }
  }

  return {{}, nullptr};
}

bool
ConfigRegistry::contains(const std::string &key) const
{
  std::shared_lock lock(_mutex);
  return _entries.find(key) != _entries.end();
}

ConfigRegistry::Entry const *
ConfigRegistry::find(const std::string &key) const
{
  std::shared_lock lock(_mutex);
  auto             it = _entries.find(key);
  return it != _entries.end() ? &it->second : nullptr;
}

void
ConfigRegistry::set_passed_config(const std::string &key, YAML::Node content)
{
  std::unique_lock lock(_mutex);
  _passed_configs[key] = std::move(content);
  Dbg(dbg_ctl, "Stored passed config for '%s'", key.c_str());
}

void
ConfigRegistry::schedule_reload(const std::string &key)
{
  Dbg(dbg_ctl, "Scheduling async reload for config '%s'", key.c_str());

  Ptr<ProxyMutex> mutex(new_ProxyMutex());
  eventProcessor.schedule_imm(new ScheduledReloadContinuation(mutex, key), ET_TASK);
}

void
ConfigRegistry::execute_reload(const std::string &key)
{
  Dbg(dbg_ctl, "Executing reload for config '%s'", key.c_str());

  // Single lock for both lookups: passed config (from RPC) and registry entry
  YAML::Node passed_config;
  Entry      entry_copy;
  {
    std::shared_lock lock(_mutex);

    if (auto pc_it = _passed_configs.find(key); pc_it != _passed_configs.end()) {
      passed_config = pc_it->second;
      Dbg(dbg_ctl, "Retrieved passed config for '%s'", key.c_str());
    }

    if (auto it = _entries.find(key); it != _entries.end()) {
      entry_copy = it->second;
    } else {
      Warning("Config '%s' not found in registry during execute_reload", key.c_str());
      return;
    }
  }

  ink_release_assert(entry_copy.handler);

  // Create context with subtask tracking
  // For rpc reload: use key as description, no filename (source: rpc)
  // For file reload: use key as description, filename indicates source: file
  std::string filename = passed_config.IsDefined() ? "" : entry_copy.resolve_filename();
  auto        ctx      = ReloadCoordinator::Get_Instance().create_config_context(entry_copy.key, filename);
  ctx.in_progress();

  if (passed_config.IsDefined()) {
    // Passed config mode: store YAML node directly for handler to use via supplied_yaml()
    Dbg(dbg_ctl, "Config '%s' reloading from rpc-supplied content", entry_copy.key.c_str());
    ctx.set_supplied_yaml(passed_config);
  } else {
    Dbg(dbg_ctl, "Config '%s' reloading from file '%s'", entry_copy.key.c_str(), filename.c_str());
  }

  // Handler checks ctx.supplied_yaml() for rpc-supplied content, otherwise reads from the
  // module's known filename.
  try {
    entry_copy.handler(ctx);
    if (!ctx.is_terminal()) { // handler did not call ctx.complete() or ctx.fail(). It may have deferred work to another thread.
      Warning("Config '%s' handler returned without reaching a terminal state. "
              "If the handler deferred work to another thread, ensure ctx.complete() or ctx.fail() "
              "is called when processing finishes; otherwise the task will remain in progress "
              "until the timeout checker marks it as TIMEOUT.",
              entry_copy.key.c_str());
    }
    Dbg(dbg_ctl, "Config '%s' reload completed", entry_copy.key.c_str());
  } catch (std::exception const &ex) {
    ctx.fail(ex.what());
    Warning("Config '%s' reload failed: %s", entry_copy.key.c_str(), ex.what());
  }
}

} // namespace config
