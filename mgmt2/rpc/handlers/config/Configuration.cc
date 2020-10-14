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

namespace YAML
{
template <> struct convert<SetRecordCmdInfo> {
  static bool
  decode(Node const &node, SetRecordCmdInfo &info)
  {
    if (!node["record_name"] || !node["record_value"]) {
      return false;
    }

    info.name  = node["record_name"].as<std::string>();
    info.value = node["record_value"].as<std::string>();
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
  using LookupContext = std::tuple<RecDataT, RecCheckT, const char *>;

  for (auto const &kv : params) {
    SetRecordCmdInfo info;
    try {
      info = kv.as<SetRecordCmdInfo>();
    } catch (YAML::Exception const &ex) {
      resp.errata().push({err::RecordError::RECORD_NOT_FOUND});
      continue;
    }

    LookupContext recordCtx;

    // Get record info first. TODO: we may just want to get the full record and  then send it back  as a response.
    const auto ret = RecLookupRecord(
      info.name.c_str(),
      [](const RecRecord *record, void *data) {
        auto &[dataType, checkType, pattern] = *static_cast<LookupContext *>(data);
        if (REC_TYPE_IS_CONFIG(record->rec_type)) {
          dataType  = record->data_type;
          checkType = record->config_meta.check_type;
          if (record->config_meta.check_expr) {
            pattern = record->config_meta.check_expr;
          }
        }
      },
      &recordCtx);

    // make sure if exist. If not, we stop it and do not keep forward.
    if (ret != REC_ERR_OKAY) {
      resp.errata().push({err::RecordError::RECORD_NOT_FOUND});
      continue;
    }

    // now set the value.
    auto const &[dataType, checkType, pattern] = recordCtx;

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
    default:;
    }

    if (set_ok) {
      YAML::Node updatedRecord;
      updatedRecord["record_name"] = info.name;
      resp.result().push_back(updatedRecord);
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
