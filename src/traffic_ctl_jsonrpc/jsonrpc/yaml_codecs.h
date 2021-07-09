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

namespace
{
// For some fields, If we can't get the value, then just send the default/empty value. Let the
// traffic_ctl display something.
template <typename T>
auto
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
} // namespace
/**
 * YAML namespace. All json rpc request codecs can be placed here. It will read all the definitions from "requests.h"
 * It's noted that there may be some duplicated with the rpc server implementation structures but as this is very simple idiom where
 * we define the data as plain as possible and it's just used for printing purposes, No major harm on having them duplicated
 */

namespace YAML
{
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<specs::JSONRPCError> {
  static bool
  decode(Node const &node, specs::JSONRPCError &error)
  {
    error.code    = try_extract<int32_t>(node, "code");
    error.message = try_extract<std::string>(node, "message");
    if (auto data = node["data"]) {
      for (auto &&err : data) {
        error.data.emplace_back(try_extract<int32_t>(err, "code"), try_extract<std::string>(err, "message"));
      }
    }
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<RecordLookUpResponse::RecordParamInfo::ConfigMeta> {
  static bool
  decode(Node const &node, RecordLookUpResponse::RecordParamInfo::ConfigMeta &meta)
  {
    meta.accessType   = try_extract<int32_t>(node, "access_type");
    meta.updateStatus = try_extract<int32_t>(node, "update_status");
    meta.updateType   = try_extract<int32_t>(node, "update_type");
    meta.checkType    = try_extract<int32_t>(node, "checktype");
    meta.source       = try_extract<int32_t>(node, "source");
    meta.checkExpr    = try_extract<std::string>(node, "check_expr");
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<RecordLookUpResponse::RecordParamInfo::StatMeta> {
  static bool
  decode(Node const &node, RecordLookUpResponse::RecordParamInfo::StatMeta &meta)
  {
    meta.persistType = try_extract<int32_t>(node, "persist_type");
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<RecordLookUpResponse::RecordParamInfo> {
  static bool
  decode(Node const &node, RecordLookUpResponse::RecordParamInfo &info)
  {
    info.name         = try_extract<std::string>(node, "record_name");
    info.type         = try_extract<int32_t>(node, "record_type");
    info.version      = try_extract<int32_t>(node, "version");
    info.rsb          = try_extract<int32_t>(node, "raw_stat_block");
    info.order        = try_extract<int32_t>(node, "order");
    info.rclass       = try_extract<int32_t>(node, "record_class");
    info.overridable  = try_extract<bool>(node, "overridable");
    info.dataType     = try_extract<std::string>(node, "data_type");
    info.currentValue = try_extract<std::string>(node, "current_value");
    info.defaultValue = try_extract<std::string>(node, "default_value");
    try {
      if (auto n = node["config_meta"]) {
        info.meta = n.as<RecordLookUpResponse::RecordParamInfo::ConfigMeta>();
      } else if (auto n = node["stat_meta"]) {
        info.meta = n.as<RecordLookUpResponse::RecordParamInfo::StatMeta>();
      }
    } catch (Exception const &ex) {
      return false;
    }
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<RecordLookUpResponse> {
  static bool
  decode(Node const &node, RecordLookUpResponse &info)
  {
    try {
      auto records = node["recordList"];
      for (auto &&item : records) {
        if (auto record = item["record"]) {
          info.recordList.push_back(record.as<RecordLookUpResponse::RecordParamInfo>());
        }
      }

      auto errors = node["errorList"];
      for (auto &&item : errors) {
        info.errorList.push_back(item.as<RecordLookUpResponse::RecordError>());
      }
    } catch (Exception const &ex) {
      return false;
    }
    return true;
  }
};

//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<RecordLookupRequest::Params> {
  static Node
  encode(RecordLookupRequest::Params const &info)
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
template <> struct convert<RecordLookUpResponse::RecordError> {
  static bool
  decode(Node const &node, RecordLookUpResponse::RecordError &err)
  {
    err.code       = try_extract<std::string>(node, "code");
    err.recordName = try_extract<std::string>(node, "record_name");
    err.message    = try_extract<std::string>(node, "message");
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<ConfigSetRecordRequest::Params> {
  static Node
  encode(ConfigSetRecordRequest::Params const &params)
  {
    Node node;
    node["record_name"]  = params.recName;
    node["record_value"] = params.recValue;

    return node;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<HostSetStatusRequest::Params::Op> {
  static Node
  encode(HostSetStatusRequest::Params::Op const &op)
  {
    using Op = HostSetStatusRequest::Params::Op;
    switch (op) {
    case Op::UP:
      return Node("up");
    case Op::DOWN:
      return Node("down");
    }
    return Node("unknown");
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<HostSetStatusRequest::Params> {
  static Node
  encode(HostSetStatusRequest::Params const &params)
  {
    Node node;
    node["operation"] = params.op;
    node["host"]      = params.hosts; // list
    node["reason"]    = params.reason;
    node["time"]      = params.time;

    return node;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<ClearMetricRequest::Params> {
  static Node
  encode(ClearMetricRequest::Params const &params)
  {
    Node node;
    for (auto name : params.names) {
      Node n;
      n["record_name"] = name;
      node.push_back(n);
    }
    return node;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<BasicPluginMessageRequest::Params> {
  static Node
  encode(BasicPluginMessageRequest::Params const &params)
  {
    Node node;
    node["tag"]  = params.tag;
    node["data"] = params.str;
    return node;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<ServerStartDrainRequest::Params> {
  static Node
  encode(ServerStartDrainRequest::Params const &params)
  {
    Node node;
    node["no_new_connections"] = params.waitForNewConnections;
    return node;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<SetStorageDeviceOfflineRequest::Params> {
  static Node
  encode(SetStorageDeviceOfflineRequest::Params const &params)
  {
    Node node;
    for (auto &&path : params.names) {
      node.push_back(path);
    }
    return node;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<GetStorageDeviceStatusRequest::Params> {
  static Node
  encode(GetStorageDeviceStatusRequest::Params const &params)
  {
    Node node;
    for (auto &&path : params.names) {
      node.push_back(path);
    }
    return node;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<DeviceStatusInfoResponse> {
  static bool
  decode(Node const &node, DeviceStatusInfoResponse &info)
  {
    for (auto &&item : node) {
      Node disk = item["cachedisk"];
      info.data.emplace_back(try_extract<std::string>(disk, "path"),   // path
                             try_extract<std::string>(disk, "status"), // status
                             try_extract<int>(disk, "error_count")     // err count

      );
    }
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------

} // namespace YAML

/**
 * Handy classes to deal with the json emitters. If yaml needs to be emitted, then this should be changed and do not use the
 * doublequoted flow.
 */
class yamlcpp_json_emitter
{
public:
  static std::string
  encode(specs::JSONRPCRequest const &req)
  {
    YAML::Emitter json;
    json << YAML::DoubleQuoted << YAML::Flow;
    // encode(req, json);
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

  static specs::JSONRPCResponse
  decode(const std::string &response)
  {
    specs::JSONRPCResponse resp;
    try {
      resp.fullMsg = YAML::Load(response.data());
      if (resp.fullMsg.Type() != YAML::NodeType::Map) {
        // we are not expecting anything else than a structure
        std::cout << "## error parsing response, response is not a structure\n";
        return {};
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
    } catch (YAML::Exception const &e) {
      std::cout << "## error parsing response: " << e.what() << '\n';
    }
    return resp;
  }
};