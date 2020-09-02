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
#include "rpc/handlers/common/ErrorId.h"

namespace rpc::handlers::config
{
using NameValuePair = std::pair<std::string, std::string>;
} // namespace rpc::handlers::config
namespace rpc::handlers::config::field_names
{
static constexpr auto RECORD_NAME{"record_name"};
static constexpr auto RECORD_VALUE{"record_value"};
} // namespace rpc::handlers::config::field_names

namespace YAML
{
namespace config = rpc::handlers::config;
template <> struct convert<config::NameValuePair> {
  static bool
  decode(Node const &node, config::NameValuePair &info)
  {
    using namespace config::field_names;
    try {
      if (node[RECORD_NAME] && node[RECORD_VALUE]) {
        info.first  = node[RECORD_NAME].as<std::string>();
        info.second = node[RECORD_VALUE].as<std::string>();
        return true;
      }
    } catch (YAML::Exception const &ex) {
      // ignore, just let it fail(return false)
      // TODO: we may need to find a way to let this error travel higher up
    }
    return false;
  }
};
} // namespace YAML

namespace rpc::handlers::config
{
static unsigned configRecType  = RECT_CONFIG | RECT_LOCAL;
static constexpr auto ERROR_ID = rpc::handlers::errors::ID::Configuration;

namespace err   = rpc::handlers::errors;
namespace utils = rpc::handlers::records::utils;

ts::Rv<YAML::Node>
get_config_records(std::string_view const &id, YAML::Node const &params)
{
  using namespace rpc::handlers::records::utils;
  ts::Rv<YAML::Node> resp;
  std::error_code ec;

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
      ec = error;
      break;
    }

    resp.result().push_back(std::move(node));
  }

  if (ec) {
    push_error(ERROR_ID, ec, resp.errata());
  }

  return resp;
}

ts::Rv<YAML::Node>
get_config_records_regex(std::string_view const &id, YAML::Node const &params)
{
  using namespace rpc::handlers::records::utils;
  ts::Rv<YAML::Node> resp;
  std::error_code ec;
  for (auto &&element : params) {
    auto const &recordName = element.as<std::string>();
    auto &&[node, error]   = get_yaml_record_regex(recordName, configRecType);

    if (error) {
      ec = error;
      break;
    }
    // node may have more than one.
    for (auto &&n : node) {
      resp.result().push_back(std::move(n));
    }
  }

  if (ec) {
    push_error(ERROR_ID, ec, resp.errata());
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
    push_error(ERROR_ID, error, resp.errata());
    return resp;
  }

  resp.result() = std::move(node);
  return resp;
}

namespace
{
  template <typename T>
  bool
  set_data_type(std::string const &name, std::string const &value)
  {
    if constexpr (std::is_same_v<T, float>) {
      T val;
      try {
        val = std::stof(value.data());
      } catch (std::exception const &ex) {
        return false;
      }
      // set the value
      if (RecSetRecordFloat(name.c_str(), val, REC_SOURCE_DEFAULT) != REC_ERR_OKAY) {
        return false;
      }
    } else if constexpr (std::is_same_v<T, int>) {
      T val;
      try {
        val = std::stoi(value.data());
      } catch (std::exception const &ex) {
        return false;
      }
      // set the value
      if (RecSetRecordInt(name.c_str(), val, REC_SOURCE_DEFAULT) != REC_ERR_OKAY) {
        return false;
      }
    } else if constexpr (std::is_same_v<T, std::string>) {
      if (RecSetRecordString(name.c_str(), const_cast<char *>(value.c_str()), REC_SOURCE_DEFAULT) != REC_ERR_OKAY) {
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
  std::error_code ec;

  // we need the type and the udpate type for now.
  using LookupContext = std::tuple<RecDataT, RecUpdateT, RecCheckT, const char *>;

  for (auto const &kv : params) {
    std::string name, value;
    try {
      auto nvPair = kv.as<NameValuePair>();
      name        = nvPair.first;
      value       = nvPair.second;
    } catch (YAML::Exception const &ex) {
      ec = err::RecordError::GENERAL_ERROR;
      break;
    }

    LookupContext recordCtx;

    // Get record info first. We will respond with the update status, so we
    // save it.
    const auto ret = RecLookupRecord(
      name.c_str(),
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
      ec = err::RecordError::RECORD_NOT_FOUND;
      break;
    }

    // now set the value.
    auto const &[dataType, updateType, checkType, pattern] = recordCtx;

    // run the check only if we have something to check against it.
    if (pattern != nullptr && utils::recordValidityCheck(value, checkType, pattern) == false) {
      ec = err::RecordError::VALIDITY_CHECK_ERROR;
      break;
    }

    bool set_ok{false};
    switch (dataType) {
    case RECD_INT:
    case RECD_COUNTER:
      set_ok = set_data_type<int>(name, value);
      break;
    case RECD_FLOAT:
      set_ok = set_data_type<float>(name, value);
      break;
    case RECD_STRING:
      set_ok = set_data_type<std::string>(name, value);
      break;
    default:
      // null?
      ;
    }

    if (!set_ok) {
      ec = err::RecordError::GENERAL_ERROR;
    }

    // Set the response.
    auto &result = resp.result();

    result["record_name"]   = name;
    result["new_value"]     = value;
    result["update_status"] = updateType;
  }

  if (ec) {
    push_error(ERROR_ID, ec, resp.errata());
  }

  return resp;
}

ts::Rv<YAML::Node>
reload_config(std::string_view const &id, YAML::Node const &params)
{
  ts::Rv<YAML::Node> resp;
  auto err = FileManager::instance().rereadConfig();
  // if there is any error, report it back.
  if (err.size() > 0) {
    resp.errata() = std::move(err);
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
