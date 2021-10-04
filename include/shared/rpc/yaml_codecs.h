/**
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
#pragma once

#include <string_view>
#include <yaml-cpp/yaml.h>

#include "RPCRequests.h"

/// JSONRPC 2.0 Client API request/response codecs only. If you need to define your own specific codecs they should then be defined
/// in a different file, unless they are strongly related to the ones defined here.

namespace helper
{
// For some fields, If we can't get the value, then just send the default/empty value. Let the
// traffic_ctl display something.
template <typename T>
inline auto
try_extract(YAML::Node const &node, const char *name, bool throwOnFail = false)
{
  try {
    if (auto n = node[name]) {
      return n.as<T>();
    }
  } catch (YAML::Exception const &ex) {
    if (throwOnFail) {
      throw ex;
    }
  }
  return T{};
}
} // namespace helper
/**
 * YAML namespace. All json rpc request codecs can be placed here. It will read all the definitions from "requests.h"
 * It's noted that there may be some duplicated with the rpc server implementation structures but as this is very simple idiom where
 * we define the data as plain as possible and it's just used for printing purposes, No major harm on having them duplicated
 */

namespace YAML
{
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<shared::rpc::JSONRPCError> {
  static bool
  decode(Node const &node, shared::rpc::JSONRPCError &error)
  {
    error.code    = helper::try_extract<int32_t>(node, "code");
    error.message = helper::try_extract<std::string>(node, "message");
    if (auto data = node["data"]) {
      for (auto &&err : data) {
        error.data.emplace_back(helper::try_extract<int32_t>(err, "code"), helper::try_extract<std::string>(err, "message"));
      }
    }
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<shared::rpc::RecordLookUpResponse::RecordParamInfo::ConfigMeta> {
  static bool
  decode(Node const &node, shared::rpc::RecordLookUpResponse::RecordParamInfo::ConfigMeta &meta)
  {
    meta.accessType   = helper::try_extract<int32_t>(node, "access_type");
    meta.updateStatus = helper::try_extract<int32_t>(node, "update_status");
    meta.updateType   = helper::try_extract<int32_t>(node, "update_type");
    meta.checkType    = helper::try_extract<int32_t>(node, "checktype");
    meta.source       = helper::try_extract<int32_t>(node, "source");
    meta.checkExpr    = helper::try_extract<std::string>(node, "check_expr");
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<shared::rpc::RecordLookUpResponse::RecordParamInfo::StatMeta> {
  static bool
  decode(Node const &node, shared::rpc::RecordLookUpResponse::RecordParamInfo::StatMeta &meta)
  {
    meta.persistType = helper::try_extract<int32_t>(node, "persist_type");
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<shared::rpc::RecordLookUpResponse::RecordParamInfo> {
  static bool
  decode(Node const &node, shared::rpc::RecordLookUpResponse::RecordParamInfo &info)
  {
    info.name         = helper::try_extract<std::string>(node, "record_name");
    info.type         = helper::try_extract<int32_t>(node, "record_type");
    info.version      = helper::try_extract<int32_t>(node, "version");
    info.rsb          = helper::try_extract<int32_t>(node, "raw_stat_block");
    info.order        = helper::try_extract<int32_t>(node, "order");
    info.rclass       = helper::try_extract<int32_t>(node, "record_class");
    info.overridable  = helper::try_extract<bool>(node, "overridable");
    info.dataType     = helper::try_extract<std::string>(node, "data_type");
    info.currentValue = helper::try_extract<std::string>(node, "current_value");
    info.defaultValue = helper::try_extract<std::string>(node, "default_value");
    try {
      if (auto n = node["config_meta"]) {
        info.meta = n.as<shared::rpc::RecordLookUpResponse::RecordParamInfo::ConfigMeta>();
      } else if (auto n = node["stat_meta"]) {
        info.meta = n.as<shared::rpc::RecordLookUpResponse::RecordParamInfo::StatMeta>();
      }
    } catch (Exception const &ex) {
      return false;
    }
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<shared::rpc::RecordLookUpResponse> {
  static bool
  decode(Node const &node, shared::rpc::RecordLookUpResponse &info)
  {
    try {
      auto records = node["recordList"];
      for (auto &&item : records) {
        if (auto record = item["record"]) {
          info.recordList.push_back(record.as<shared::rpc::RecordLookUpResponse::RecordParamInfo>());
        }
      }

      auto errors = node["errorList"];
      for (auto &&item : errors) {
        info.errorList.push_back(item.as<shared::rpc::RecordLookUpResponse::RecordError>());
      }
    } catch (Exception const &ex) {
      return false;
    }
    return true;
  }
};

//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<shared::rpc::RecordLookupRequest::Params> {
  static Node
  encode(shared::rpc::RecordLookupRequest::Params const &info)
  {
    Node record;
    if (info.isRegex) {
      record["record_name_regex"] = info.recName;
    } else {
      record["record_name"] = info.recName;
    }

    record["rec_types"] = info.recTypes;
    return record;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<shared::rpc::RecordLookUpResponse::RecordError> {
  static bool
  decode(Node const &node, shared::rpc::RecordLookUpResponse::RecordError &err)
  {
    err.code       = helper::try_extract<std::string>(node, "code");
    err.recordName = helper::try_extract<std::string>(node, "record_name");
    err.message    = helper::try_extract<std::string>(node, "message");
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
} // namespace YAML
namespace internal
{
template <typename, template <typename> class, typename = std::void_t<>> struct detect_member_function : std::false_type {
};
template <typename Codec, template <typename> class Op>
struct detect_member_function<Codec, Op, std::void_t<Op<Codec>>> : std::true_type {
};
// Help to detect if codec implements the right functions.
template <class Codec> using encode_op = decltype(std::declval<Codec>().encode({}));
template <class Codec> using decode_op = decltype(std::declval<Codec>().decode({}));

// Compile time check for encode member function
template <typename Codec> using has_encode = detect_member_function<Codec, encode_op>;
// Compile time check for decode member function
template <typename Codec> using has_decode = detect_member_function<Codec, decode_op>;
} // namespace internal
/**
 * Handy classes to deal with the json emitters. If yaml needs to be emitted, then this should be changed and do not use the
 * double quoted flow.
 */
class yamlcpp_json_emitter
{
public:
  static std::string
  encode(shared::rpc::JSONRPCRequest const &req)
  {
    YAML::Emitter json;
    json << YAML::DoubleQuoted << YAML::Flow;
    json << YAML::BeginMap;

    if (!req.id.empty()) {
      json << YAML::Key << "id" << YAML::Value << req.id;
    }
    json << YAML::Key << "jsonrpc" << YAML::Value << req.jsonrpc;
    json << YAML::Key << "method" << YAML::Value << req.get_method();
    if (!req.params.IsNull()) {
      json << YAML::Key << "params" << YAML::Value << req.params;
    }
    json << YAML::EndMap;
    return json.c_str();
  }

  static shared::rpc::JSONRPCResponse
  decode(const std::string &response)
  {
    shared::rpc::JSONRPCResponse resp;
    resp.fullMsg = YAML::Load(response.data());
    if (resp.fullMsg.Type() != YAML::NodeType::Map) {
      throw std::runtime_error{"error parsing response, response is not a structure"};
    }

    if (resp.fullMsg["result"]) {
      resp.result = resp.fullMsg["result"];
    } else if (resp.fullMsg["error"]) {
      resp.error = resp.fullMsg["error"];
    }

    if (auto id = resp.fullMsg["id"]) {
      resp.id = id.as<std::string>();
    }
    if (auto jsonrpc = resp.fullMsg["jsonrpc"]) {
      resp.jsonrpc = jsonrpc.as<std::string>();
    }

    return resp;
  }
};