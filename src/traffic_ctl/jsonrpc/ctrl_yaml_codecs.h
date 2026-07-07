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

template <> struct convert<ConfigReloadRequest::Params> {
  static Node
  encode(ConfigReloadRequest::Params const &params)
  {
    Node node;
    if (!params.token.empty()) {
      node["token"] = params.token;
    }

    if (params.force) {
      node["force"] = params.force;
    }

    // Include configs if present (triggers inline mode on server)
    if (params.configs && params.configs.IsMap() && params.configs.size() > 0) {
      node["configs"] = params.configs;
    }

    return node;
  }
};

template <> struct convert<FetchConfigReloadStatusRequest::Params> {
  static Node
  encode(FetchConfigReloadStatusRequest::Params const &params)
  {
    Node node;
    auto is_number = [](const std::string &s) {
      return !s.empty() && std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isdigit(c); }) == s.end();
    };
    // either passed values or defaults.
    node["token"] = params.token;

    if (!params.count.empty() && (!is_number(params.count) && params.count != "all")) {
      throw std::invalid_argument("Invalid 'count' value, must be numeric or 'all'");
    }

    if (!params.count.empty()) {
      if (params.count == "all") {
        node["count"] = 0; // 0 means all.
      } else {
        node["count"] = std::stoi(params.count);
      }
    }

    return node;
  }
};

template <> struct convert<ConfigReloadResponse> {
  static bool
  decode(Node const &node, ConfigReloadResponse &out)
  {
    auto get_info = [](auto &&self, YAML::Node const &from) -> ConfigReloadResponse::ReloadInfo {
      ConfigReloadResponse::ReloadInfo info;
      info.config_token = helper::try_extract<std::string>(from, "config_token");
      info.status       = helper::try_extract<std::string>(from, "status");
      info.description  = helper::try_extract<std::string>(from, "description", false, std::string{"<none>"});
      info.filename     = helper::try_extract<std::string>(from, "filename", false, std::string{"<none>"});
      for (auto &&log : from["logs"]) {
        info.logs.push_back(log.as<std::string>());
      }

      for (auto &&sub : from["sub_tasks"]) {
        info.sub_tasks.push_back(self(self, sub));
      }

      if (auto meta = from["meta"]) {
        info.meta.created_time_ms      = helper::try_extract<int64_t>(meta, "created_time_ms");
        info.meta.last_updated_time_ms = helper::try_extract<int64_t>(meta, "last_updated_time_ms");
        info.meta.is_main_task         = helper::try_extract<bool>(meta, "main_task");
      }
      return info;
    };

    // Server sends "errors" (plural)
    if (node["errors"]) {
      for (auto &&err : node["errors"]) {
        ConfigReloadResponse::Error e;
        e.code    = helper::try_extract<int>(err, "code");
        e.message = helper::try_extract<std::string>(err, "message");
        out.error.push_back(std::move(e));
      }
    }
    out.created_time = helper::try_extract<std::string>(node, "created_time");
    for (auto &&msg : node["message"]) {
      out.messages.push_back(msg.as<std::string>());
    }
    out.config_token = helper::try_extract<std::string>(node, "token");

    for (auto &&element : node["tasks"]) {
      ConfigReloadResponse::ReloadInfo task = get_info(get_info, element);
      out.tasks.push_back(std::move(task));
    }

    return true;
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
template <> struct convert<HostDBGetStatusRequest::Params> {
  static Node
  encode(HostDBGetStatusRequest::Params const &params)
  {
    Node node;
    node["hostname"] = params.hostname;
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

template <> struct convert<HostStatusLookUpResponse> {
  static bool
  decode(Node const &node, HostStatusLookUpResponse &info)
  {
    YAML::Node statusList = node["statusList"];
    YAML::Node errorList  = node["errorList"];
    for (auto &&item : statusList) {
      HostStatusLookUpResponse::HostStatusInfo hi;
      hi.hostName = item["hostname"].Scalar();
      hi.status   = item["status"].Scalar();
      info.statusList.push_back(hi);
    }
    for (auto &&item : errorList) {
      info.errorList.push_back(item.Scalar());
    }
    return true;
  }
};
} // namespace YAML
