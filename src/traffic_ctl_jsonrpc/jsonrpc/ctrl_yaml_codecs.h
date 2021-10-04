/**
  @section license License

  traffic_ctl yaml codecs.

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

// base yaml codecs.
#include "shared/rpc/yaml_codecs.h"

#include "CtrlRPCRequests.h"

// traffic_ctl jsonrpc request/response YAML codec implementation.

namespace YAML
{
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
      info.data.emplace_back(helper::try_extract<std::string>(disk, "path"),   // path
                             helper::try_extract<std::string>(disk, "status"), // status
                             helper::try_extract<int>(disk, "error_count")     // err count

      );
    }
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
template <> struct convert<ConfigSetRecordResponse> {
  static bool
  decode(Node const &node, ConfigSetRecordResponse &info)
  {
    for (auto &&item : node) {
      info.data.push_back(
        {helper::try_extract<std::string>(item, "record_name"), helper::try_extract<std::string>(item, "update_type")});
    }
    return true;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
} // namespace YAML