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
#include "Configuration.h"

#include <system_error>
#include <string>
#include <string_view>

#include "tscore/BufferWriter.h"
#include "records/I_RecCore.h"
#include "records/P_RecCore.h"
#include "tscore/Diags.h"

#include "config/FileManager.h"

#include "rpc/handlers/common/RecordsUtils.h"

namespace
{
/// key value pair from each element passed in the set command.
struct SetRecordCmdInfo {
  std::string name;
  std::string value;
};
} // namespace
namespace rpc::handlers::config::field_names
{
static constexpr auto RECORD_NAME{"record_name"};
static constexpr auto RECORD_VALUE{"record_value"};
} // namespace rpc::handlers::config::field_names

namespace YAML
{
namespace config = rpc::handlers::config;
template <> struct convert<SetRecordCmdInfo> {
  static bool
  decode(Node const &node, SetRecordCmdInfo &info)
  {
    using namespace config::field_names;
    if (!node[RECORD_NAME] || !node[RECORD_VALUE]) {
      return false;
    }

    info.name  = node[RECORD_NAME].as<std::string>();
    info.value = node[RECORD_VALUE].as<std::string>();
    return true;
  }
};
} // namespace YAML

namespace rpc::handlers::config
{
static unsigned configRecType = RECT_CONFIG | RECT_LOCAL;

namespace err   = rpc::handlers::errors;
namespace utils = rpc::handlers::records::utils;

ts::Rv<YAML::Node>
get_config_records(std::string_view const &id, YAML::Node const &params)
{
  using namespace rpc::handlers::records::utils;
  ts::Rv<YAML::Node> resp;

  auto check = [](RecT rec_type, std::error_code &ec) {
    if (!REC_TYPE_IS_CONFIG(rec_type)) {
      // we have an issue
      ec = err::RecordError::RECORD_NOT_CONFIG;
      return false;
    }
    return true;
  };

  for (auto &&element : params) {
    auto const &recordName = element.as<std::string>();
    auto &&[node, error]   = get_yaml_record(recordName, check);

    if (error) {
      resp.errata().push(error);
      continue;
    }

    resp.result().push_back(std::move(node));
  }

  return resp;
}

ts::Rv<YAML::Node>
get_config_records_regex(std::string_view const &id, YAML::Node const &params)
{
  using namespace rpc::handlers::records::utils;
  ts::Rv<YAML::Node> resp;

  for (auto &&element : params) {
    auto const &recordName = element.as<std::string>();
    auto &&[node, error]   = get_yaml_record_regex(recordName, configRecType);

    if (error) {
      resp.errata().push(error);
      continue;
    }
    // node may have more than one.
    for (auto &&n : node) {
      resp.result().push_back(std::move(n));
    }
  }

  return resp;
}

ts::Rv<YAML::Node>
get_all_config_records(std::string_view const &id, [[maybe_unused]] YAML::Node const &params)
{
  ts::Rv<YAML::Node> resp;
  static constexpr std::string_view all{".*"};
  using namespace rpc::handlers::records::utils;

  auto &&[node, error] = get_yaml_record_regex(all, configRecType);

  if (error) {
    return {error};
  }

  resp.result() = std::move(node);
  return resp;
}

namespace
{
  template <typename T>
  bool
  set_data_type(SetRecordCmdInfo const &info)
  {
    if constexpr (std::is_same_v<T, float>) {
      T val;
      try {
        val = std::stof(info.value.data());
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
        val = std::stoi(info.value.data());
      } catch (std::exception const &ex) {
        return false;
      }
      // set the value
      if (RecSetRecordInt(info.name.c_str(), val, REC_SOURCE_DEFAULT) != REC_ERR_OKAY) {
        return false;
      }
    } else if constexpr (std::is_same_v<T, std::string>) {
      if (RecSetRecordString(info.name.c_str(), const_cast<char *>(info.value.c_str()), REC_SOURCE_DEFAULT) != REC_ERR_OKAY) {
        return false;
      }
    }

    // all set.
    return true;
  }
} // namespace

ts::Rv<YAML::Node>
set_config_records(std::string_view const &id, YAML::Node const &params)
{
  ts::Rv<YAML::Node> resp;

  // we need the type and the udpate type for now.
  using LookupContext = std::tuple<RecDataT, RecUpdateT, RecCheckT, const char *>;

  for (auto const &kv : params) {
    SetRecordCmdInfo info;
    try {
      info = kv.as<SetRecordCmdInfo>();
    } catch (YAML::Exception const &ex) {
      resp.errata().push({err::RecordError::RECORD_NOT_FOUND});
      continue;
    }

    LookupContext recordCtx;

    // Get record info first. We will respond with the update status, so we
    // save it.
    const auto ret = RecLookupRecord(
      info.name.c_str(),
      [](const RecRecord *record, void *data) {
        auto &[dataType, updateType, checkType, pattern] = *static_cast<LookupContext *>(data);
        if (REC_TYPE_IS_CONFIG(record->rec_type)) {
          dataType   = record->data_type;
          updateType = record->config_meta.update_type;
          checkType  = record->config_meta.check_type;
          if (record->config_meta.check_expr) {
            pattern = record->config_meta.check_expr;
          }
        } // if not??
      },
      &recordCtx);

    // make sure if exist. If not, we stop it and do not keep forward.
    if (ret != REC_ERR_OKAY) {
      resp.errata().push({err::RecordError::RECORD_NOT_FOUND});
      continue;
    }

    // now set the value.
    auto const &[dataType, updateType, checkType, pattern] = recordCtx;

    // run the check only if we have something to check against it.
    if (pattern != nullptr && utils::recordValidityCheck(info.value, checkType, pattern) == false) {
      resp.errata().push({err::RecordError::VALIDITY_CHECK_ERROR});
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
    default:
      // null?
      ;
    }

    if (set_ok) {
      // Set the response.
      auto &result = resp.result();

      result["record_name"]   = info.name;
      result["new_value"]     = info.value;
      result["update_status"] = updateType;
    } else {
      resp.errata().push({err::RecordError::GENERAL_ERROR});
      continue;
    }
  }

  return resp;
}

ts::Rv<YAML::Node>
reload_config(std::string_view const &id, YAML::Node const &params)
{
  ts::Rv<YAML::Node> resp;

  // if there is any error, report it back.
  if (auto err = FileManager::instance().rereadConfig(); err) {
    return err;
  }

  // If any callback was register(TSMgmtUpdateRegister) for config notifications, then it will be eventually notify.
  FileManager::instance().invokeConfigPluginCallbacks();

  // save config time.
  RecSetRecordInt("proxy.node.config.reconfigure_time", time(nullptr), REC_SOURCE_DEFAULT);
  // TODO: we may not need this any more
  RecSetRecordInt("proxy.node.config.reconfigure_required", 0, REC_SOURCE_DEFAULT);

  return resp;
}
} // namespace rpc::handlers::config
