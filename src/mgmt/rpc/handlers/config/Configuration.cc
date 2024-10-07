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

#include "../common/RecordsUtils.h"
#include "tsutil/Metrics.h"

namespace utils = rpc::handlers::records::utils;

namespace
{
/// key value pair from each element passed in the set command.
struct SetRecordCmdInfo {
  std::string name;
  std::string value;
};

DbgCtl dbg_ctl_RPC{"RPC"};
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
      if (RecSetRecordString(info.name.c_str(), const_cast<char *>(info.value.c_str()), REC_SOURCE_DEFAULT) != REC_ERR_OKAY) {
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

swoc::Rv<YAML::Node>
reload_config(std::string_view const & /* id ATS_UNUSED */, YAML::Node const & /* params ATS_UNUSED */)
{
  ts::Metrics         &metrics     = ts::Metrics::instance();
  static auto          reconf_time = metrics.lookup("proxy.process.proxy.reconfigure_time");
  static auto          reconf_req  = metrics.lookup("proxy.process.proxy.reconfigure_required");
  swoc::Rv<YAML::Node> resp;
  Dbg(dbg_ctl_RPC, "invoke plugin callbacks");
  // if there is any error, report it back.
  if (auto err = FileManager::instance().rereadConfig(); !err.empty()) {
    resp.note(err);
  }
  // If any callback was register(TSMgmtUpdateRegister) for config notifications, then it will be eventually notify.
  FileManager::instance().invokeConfigPluginCallbacks();

  metrics[reconf_time].store(time(nullptr));
  metrics[reconf_req].store(0);

  return resp;
}
} // namespace rpc::handlers::config
