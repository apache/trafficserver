/*
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
#include "mgmt/rpc/handlers/config/Configuration.h"

#include <system_error>
#include <string>
#include <string_view>

#include "records/RecCore.h"
#include "../../../../records/P_RecCore.h"
#include "../../../../records/P_RecUtils.h"
#include "tscore/Diags.h"

#include "mgmt/config/FileManager.h"
#include "mgmt/config/ConfigReloadExecutor.h"
#include "mgmt/config/ConfigRegistry.h"

#include "../common/RecordsUtils.h"
#include "tsutil/Metrics.h"

#include "mgmt/config/ReloadCoordinator.h"
#include "mgmt/config/ConfigReloadErrors.h"
#include "records/YAMLConfigReloadTaskEncoder.h"

namespace utils     = rpc::handlers::records::utils;
using ConfigError   = config::reload::errors::ConfigReloadError;
constexpr auto errc = config::reload::errors::to_int;

namespace
{
/// key value pair from each element passed in the set command.
struct SetRecordCmdInfo {
  std::string name;
  std::string value;
};

DbgCtl dbg_ctl_RPC{"rpc"};
} // namespace

namespace YAML
{

template <> struct convert<SetRecordCmdInfo> {
  static bool
  decode(Node const &node, SetRecordCmdInfo &info)
  {
    if (!node[utils::RECORD_NAME_KEY] || !node[utils::RECORD_VALUE_KEY]) {
      return false;
    }

    info.name  = node[utils::RECORD_NAME_KEY].as<std::string>();
    info.value = node[utils::RECORD_VALUE_KEY].as<std::string>();
    return true;
  }
};
} // namespace YAML

namespace rpc::handlers::config
{
namespace err   = rpc::handlers::errors;
namespace utils = rpc::handlers::records::utils;

namespace
{
  template <typename T>
  bool
  set_data_type(SetRecordCmdInfo const &info)
  {
    if constexpr (std::is_same_v<T, float>) {
      T val;
      try {
        val = std::stof(info.value);
      } catch (std::exception const &ex) {
        return false;
      }
      // set the value
      if (RecSetRecordFloat(info.name.c_str(), val, REC_SOURCE_DEFAULT) != REC_ERR_OKAY) {
        return false;
      }
    } else if constexpr (std::is_same_v<T, int>) {
      T val;
      try {
        val = std::stoi(info.value);
      } catch (std::exception const &ex) {
        return false;
      }
      // set the value
      if (RecSetRecordInt(info.name.c_str(), val, REC_SOURCE_DEFAULT) != REC_ERR_OKAY) {
        return false;
      }
    } else if constexpr (std::is_same_v<T, std::string>) {
      if (RecSetRecordString(info.name.c_str(), info.value.c_str(), REC_SOURCE_DEFAULT) != REC_ERR_OKAY) {
        return false;
      }
    }

    // all set.
    return true;
  }
} // namespace

swoc::Rv<YAML::Node>
set_config_records(std::string_view const & /* id ATS_UNUSED */, YAML::Node const &params)
{
  swoc::Rv<YAML::Node> resp;

  // we need the type and the update type for now.
  using LookupContext = std::tuple<RecDataT, RecCheckT, const char *, RecUpdateT>;

  for (auto const &kv : params) {
    SetRecordCmdInfo info;
    try {
      info = kv.as<SetRecordCmdInfo>();
    } catch (YAML::Exception const &ex) {
      resp.errata().assign(std::error_code{errors::Codes::RECORD}).note("{}", std::error_code{err::RecordError::RECORD_NOT_FOUND});
      continue;
    }

    LookupContext recordCtx;

    // Get record info first. TODO: we may just want to get the full record and  then send it back  as a response.
    const auto ret = RecLookupRecord(
      info.name.c_str(),
      [](const RecRecord *record, void *data) {
        auto &[dataType, checkType, pattern, updateType] = *static_cast<LookupContext *>(data);
        if (REC_TYPE_IS_CONFIG(record->rec_type)) {
          dataType  = record->data_type;
          checkType = record->config_meta.check_type;
          if (record->config_meta.check_expr) {
            pattern = record->config_meta.check_expr;
          }
          updateType = record->config_meta.update_type;
        }
      },
      &recordCtx);

    // make sure if exist. If not, we stop it and do not keep forward.
    if (ret != REC_ERR_OKAY) {
      resp.errata().assign(std::error_code{errors::Codes::RECORD}).note("{}", std::error_code{err::RecordError::RECORD_NOT_FOUND});
      continue;
    }

    // now set the value.
    auto const &[dataType, checkType, pattern, updateType] = recordCtx;

    // run the check only if we have something to check against it.
    if (pattern != nullptr && RecordValidityCheck(info.value.c_str(), checkType, pattern) == false) {
      resp.errata()
        .assign(std::error_code{errors::Codes::RECORD})
        .note("{}", std::error_code{err::RecordError::VALIDITY_CHECK_ERROR});
      continue;
    }

    bool set_ok{false};
    switch (dataType) {
    case RECD_INT:
    case RECD_COUNTER:
      set_ok = set_data_type<int>(info);
      break;
    case RECD_FLOAT:
      set_ok = set_data_type<float>(info);
      break;
    case RECD_STRING:
      set_ok = set_data_type<std::string>(info);
      break;
    default:;
    }

    if (set_ok) {
      YAML::Node updatedRecord;
      updatedRecord[utils::RECORD_NAME_KEY]        = info.name;
      updatedRecord[utils::RECORD_UPDATE_TYPE_KEY] = std::to_string(updateType);
      resp.result().push_back(updatedRecord);
    } else {
      resp.errata().assign(std::error_code{errors::Codes::RECORD}).note("{}", std::error_code{err::RecordError::GENERAL_ERROR});
      continue;
    }
  }

  return resp;
}

///
// Unified config reload handler - supports both file-based and inline modes
// Inline mode is detected by presence of "configs" parameter
//
swoc::Rv<YAML::Node>
reload_config(std::string_view const & /* id ATS_UNUSED */, YAML::Node const &params)
{
  std::string          token = params["token"] ? params["token"].as<std::string>() : std::string{};
  bool const           force = params["force"] ? params["force"].as<bool>() : false;
  std::string          buf;
  swoc::Rv<YAML::Node> resp;

  auto make_error = [&](std::string_view msg, int code) -> YAML::Node {
    YAML::Node err;
    err["message"] = msg;
    err["code"]    = code;
    return err;
  };

  // Check if reload is already in progress
  if (!force && ReloadCoordinator::Get_Instance().is_reload_in_progress()) {
    resp.result()["errors"].push_back(make_error(
      swoc::bwprint(buf, "Reload ongoing with token '{}'", ReloadCoordinator::Get_Instance().get_current_task()->get_token()),
      errc(ConfigError::RELOAD_IN_PROGRESS)));
    resp.result()["tasks"].push_back(ReloadCoordinator::Get_Instance().get_current_task()->get_info());
    return resp;
  }

  // Validate token doesn't already exist
  if (!token.empty() && ReloadCoordinator::Get_Instance().has_token(token)) {
    resp.result()["errors"].push_back(
      make_error(swoc::bwprint(buf, "Token '{}' already exists.", token), errc(ConfigError::TOKEN_ALREADY_EXISTS)));
    return resp;
  }

  ///
  // Inline mode: detected by presence of "configs" parameter
  // Expected format:
  //   configs:
  //     ip_allow:
  //       ip_allow:
  //         - apply: in
  //           ...
  //     sni:
  //       sni:
  //         - fqdn: '*.example.com'
  //           ...
  //
  if (params["configs"] && params["configs"].IsMap()) {
    auto const &configs  = params["configs"];
    auto       &registry = ::config::ConfigRegistry::Get_Instance();

    // Phase 1: Validate all configs and collect valid ones (before creating task)
    std::vector<std::pair<std::string, YAML::Node>> valid_configs;
    for (auto it = configs.begin(); it != configs.end(); ++it) {
      std::string key = it->first.as<std::string>();

      auto const *entry = registry.find(key);
      if (!entry) {
        resp.result()["errors"].push_back(
          make_error(swoc::bwprint(buf, "Config '{}' not registered", key), errc(ConfigError::CONFIG_NOT_REGISTERED)));
        continue;
      }

      if (entry->type == ::config::ConfigType::LEGACY) {
        resp.result()["errors"].push_back(
          make_error(swoc::bwprint(buf, "Config '{}' is a legacy .config file - inline reload not supported", key),
                     errc(ConfigError::LEGACY_NO_INLINE)));
        continue;
      }

      if (!entry->handler) {
        resp.result()["errors"].push_back(
          make_error(swoc::bwprint(buf, "Config '{}' has no handler", key), errc(ConfigError::CONFIG_NO_HANDLER)));
        continue;
      }

      valid_configs.emplace_back(key, it->second);
    }

    // If no valid configs, return early without creating a task
    if (valid_configs.empty()) {
      resp.result()["message"].push_back("No configs were scheduled for reload");
      return resp;
    }

    // Phase 2: Create reload task only if we have valid configs
    std::string token_prefix = token.empty() ? "rpc-" : "";
    if (auto ret = ReloadCoordinator::Get_Instance().prepare_reload(token, token_prefix.c_str(), force); !ret.is_ok()) {
      resp.result()["errors"].push_back(
        make_error(swoc::bwprint(buf, "Failed to create reload task: {}", ret), errc(ConfigError::RELOAD_TASK_FAILED)));
      return resp;
    }

    // Phase 3: Schedule all valid configs
    for (auto const &[key, yaml_content] : valid_configs) {
      Dbg(dbg_ctl_RPC, "Storing passed config for '%s' and scheduling reload", key.c_str());
      registry.set_passed_config(key, yaml_content);
      registry.schedule_reload(key);
    }

    // Build response
    resp.result()["token"] = token;
    resp.result()["created_time"] =
      swoc::bwprint(buf, "{}", swoc::bwf::Date(ReloadCoordinator::Get_Instance().get_current_task()->get_created_time()));
    resp.result()["message"].push_back("Inline reload scheduled");

    return resp;
  }

  ///
  // File-based mode: default when no "configs" param
  //
  if (auto ret = ReloadCoordinator::Get_Instance().prepare_reload(token, "rldtk-", force); !ret.is_ok()) {
    resp.result()["errors"].push_back(make_error(swoc::bwprint(buf, "Failed to prepare reload for token '{}': {}", token, ret),
                                                 errc(ConfigError::RELOAD_TASK_FAILED)));
    return resp;
  }

  // Schedule the actual reload work asynchronously on ET_TASK
  ::config::schedule_reload_work();

  resp.result()["created_time"] =
    swoc::bwprint(buf, "{}", swoc::bwf::Date(ReloadCoordinator::Get_Instance().get_current_task()->get_created_time()));
  resp.result()["message"].push_back("Reload task scheduled");
  resp.result()["token"] = token;

  return resp;
}

swoc::Rv<YAML::Node>
get_reload_config_status(std::string_view const & /* id ATS_UNUSED */, YAML::Node const &params)
{
  swoc::Rv<YAML::Node> resp;

  auto make_error = [&](std::string const &msg, int code) -> YAML::Node {
    YAML::Node err;
    err["message"] = msg;
    err["code"]    = code;
    return err;
  };

  const std::string token = params["token"] ? params["token"].as<std::string>() : "";

  if (!token.empty()) {
    if (auto [found, info] = ReloadCoordinator::Get_Instance().find_by_token(token); !found) {
      std::string text;
      Dbg(dbg_ctl_RPC, "No reload task found with token: %s", token.c_str());
      resp.result()["errors"].push_back(
        make_error(swoc::bwprint(text, "Token '{}' not found", token), errc(ConfigError::TOKEN_NOT_FOUND)));
      resp.result()["token"] = token;
    } else {
      resp.result()["tasks"].push_back(info);
    }
  } else {
    const int count = params["count"] ? params["count"].as<int>() : 1;
    Dbg(dbg_ctl_RPC, "No token provided, count=%d", count);
    // no token provided and no count, get last one.
    auto infos = ReloadCoordinator::Get_Instance().get_all(count);
    if (infos.empty()) {
      resp.result()["errors"].push_back(make_error("No reload tasks found", errc(ConfigError::NO_RELOAD_TASKS)));
    } else {
      for (const auto &info : infos) {
        resp.result()["tasks"].push_back(info);
      }
    }
  }

  return resp;
}
} // namespace rpc::handlers::config
