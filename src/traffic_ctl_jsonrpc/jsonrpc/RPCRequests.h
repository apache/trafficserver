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

#include <string>
#include <variant>
#include "tscore/ink_uuid.h"

///
/// @brief @c JSONRPC 2.0 message mapping classes.
/// This is a very thin  API to deal with encoding/decoding jsonrpc 2.0 messages.
/// More info can be found https://www.jsonrpc.org/specification
namespace specs
{
struct JSONRPCRequest {
  std::string jsonrpc{"2.0"}; //!< Always 2.0 as this is the only version that teh server supports.
  std::string method;         //!< remote method name.
  std::string id;             //!< optional, only needed for method calls.
  YAML::Node params;          //!< This is defined by each remote API.

  virtual std::string
  get_method() const
  {
    return "method";
  }
};

struct JSONRPCResponse {
  std::string id;      //!< Always 2.0 as this is the only version that teh server supports.
  std::string jsonrpc; //!< Always 2.0
  YAML::Node result; //!< Server's response, this could be decoded by using the YAML::convert mechanism. This depends solely on the
                     //!< server's data. Check docs and schemas.
  YAML::Node error;  //!<  Server's error.

  /// Handy function to check if the server sent any error
  bool
  is_error() const
  {
    return !error.IsNull();
  }

  YAML::Node fullMsg;
};

struct JSONRPCError {
  int32_t code;        //!< High level error code.
  std::string message; //!< High level message
  // the following data is defined by TS, it will be a key/value pair.
  std::vector<std::pair<int32_t, std::string>> data;
  friend std::ostream &operator<<(std::ostream &os, const JSONRPCError &err);
};
} // namespace specs

/**
 All of the following definitions have the main purpose to have a object style idiom when dealing with request and responses from/to
 the JSONRPC server. This structures will then be used by the YAML codec implementation by using the YAML::convert style.
*/

///
/// @brief Base Client JSONRPC client request.
///
/// This represents a base class that implements the basic jsonrpc 2.0 required fields. We use UUID as an id generator
/// but this was an arbitrary choice, there is no conditions that forces to use this, any random id could work too.
/// When inherit from this class the @c id  and the @c jsonrpc fields which are constant in all the request will be automatically
/// generated.
// TODO: fix this as id is optional.
struct CtrlClientRequest : specs::JSONRPCRequest {
  using super = JSONRPCRequest;
  CtrlClientRequest() { super::id = _idGen.getString(); }

private:
  struct IdGenerator {
    IdGenerator() { _uuid.initialize(TS_UUID_V4); }
    const char *
    getString()
    {
      return _uuid.valid() ? _uuid.getString() : "fix.this.is.not.an.id";
    }
    ATSUuid _uuid;
  };
  IdGenerator _idGen;
};

/// @brief Class definition just to make clear that it will be a notification and no ID will be set.
struct CtrlClientRequestNotification : specs::JSONRPCRequest {
  CtrlClientRequestNotification() {}
};
/**
 * Specific JSONRPC request implementation should be placed here. All this definitions helps for readability and in particular
 * to easily emit json(or yaml) from this definitions.
 */

//------------------------------------------------------------------------------------------------------------------------------------

// handy definitions.
static const std::vector<int> CONFIG_REC_TYPES = {1, 16};
static const std::vector<int> METRIC_REC_TYPES = {2, 4, 32};
static constexpr bool NOT_REGEX{false};
static constexpr bool REGEX{true};

///
/// @brief Record lookup API helper class.
///
/// This utility class is used to encapsulate the basic data that contains a record lookup request.
/// Requests that are meant to interact with the admin_lookup_records API should inherit from this class if a special treatment is
/// needed. Otherwise use it directly.
///
struct RecordLookupRequest : CtrlClientRequest {
  using super = CtrlClientRequest;
  struct Params {
    std::string recName;
    bool isRegex{false};
    std::vector<int> recTypes;
  };
  std::string
  get_method() const
  {
    return "admin_lookup_records";
  }
  template <typename... Args>
  void
  emplace_rec(Args &&... p)
  {
    super::params.push_back(Params{std::forward<Args>(p)...});
  }
};

struct RecordLookUpResponse {
  /// Response Records API  mapping utility classes.
  /// This utility class is used to hold the decoded response.
  struct RecordParamInfo {
    std::string name;
    int32_t type;
    int32_t version;
    int32_t rsb;
    int32_t order;
    int32_t rclass;
    bool overridable;
    std::string dataType;
    std::string currentValue;
    std::string defaultValue;

    struct ConfigMeta {
      int32_t accessType;
      int32_t updateStatus;
      int32_t updateType;
      int32_t checkType;
      int32_t source;
      std::string checkExpr;
    };
    struct StatMeta {
      int32_t persistType;
    };
    std::variant<ConfigMeta, StatMeta> meta;
  };
  /// Record request error mapping class.
  struct RecordError {
    std::string code;
    std::string recordName;
    std::string message; //!< optional.
    friend std::ostream &operator<<(std::ostream &os, const RecordError &re);
  };

  std::vector<RecordParamInfo> recordList;
  std::vector<RecordError> errorList;
};
//------------------------------------------------------------------------------------------------------------------------------------
///
/// @brief Config status request mapping class.
///
/// There is no interaction between the traffic_ctl and this class so all the variables are defined in this
/// class.
///
struct ConfigStatusRequest : RecordLookupRequest {
  using super = RecordLookupRequest;
  ConfigStatusRequest() : super()
  {
    static const std::array<std::string, 6> statusFieldsNames = {
      "proxy.process.version.server.long",        "proxy.node.restarts.proxy.start_time",
      "proxy.node.config.reconfigure_time",       "proxy.node.config.reconfigure_required",
      "proxy.node.config.restart_required.proxy", "proxy.node.config.restart_required.manager"};
    for (auto &&recordName : statusFieldsNames) {
      super::emplace_rec(recordName, NOT_REGEX, METRIC_REC_TYPES);
    }
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
///
/// @brief Models the record request message to fetch all records by type.
///
struct GetAllRecordsRequest : RecordLookupRequest {
  using super = RecordLookupRequest;
  GetAllRecordsRequest(bool const configs) : super()
  {
    super::emplace_rec(".*", REGEX, (configs ? CONFIG_REC_TYPES : METRIC_REC_TYPES));
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
///
/// @brief Models the config reload request. No params are needed.
///
struct ConfigReloadRequest : CtrlClientRequest {
  std::string
  get_method() const
  {
    return "admin_config_reload";
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
///
/// @brief Models the clear 'all' metrics request.
///
struct ClearAllMetricRequest : CtrlClientRequest {
  std::string
  get_method() const
  {
    return "admin_clear_all_metrics_records";
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
///
/// @brief Models the clear metrics request.
///
struct ClearMetricRequest : CtrlClientRequest {
  using super = CtrlClientRequest;
  struct Params {
    std::vector<std::string> names; //!< client expects a list of record names.
  };
  ClearMetricRequest(Params p) { super::params = p; }
  std::string
  get_method() const
  {
    return "admin_clear_metrics_records";
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
struct ConfigSetRecordRequest : CtrlClientRequest {
  struct Params {
    std::string recName;
    std::string recValue;
  };
  using super = CtrlClientRequest;
  ConfigSetRecordRequest(Params d) { super::params.push_back(d); }
  std::string
  get_method() const
  {
    return "admin_config_set_records";
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
struct HostSetStatusRequest : CtrlClientRequest {
  using super = CtrlClientRequest;
  struct Params {
    enum class Op : short {
      UP = 1,
      DOWN,
    };
    Op op;
    std::vector<std::string> hosts;
    std::string reason;
    std::string time{"0"};
  };

  HostSetStatusRequest(Params p) { super::params = p; }
  std::string
  get_method() const
  {
    return "admin_host_set_status";
  }
};

struct HostGetStatusRequest : RecordLookupRequest {
  static constexpr auto STATUS_PREFIX = "proxy.process.host_status";
  using super                         = RecordLookupRequest;
  HostGetStatusRequest() : super() {}
};
//------------------------------------------------------------------------------------------------------------------------------------
struct BasicPluginMessageRequest : CtrlClientRequest {
  using super = BasicPluginMessageRequest;
  struct Params {
    std::string tag;
    std::string str;
  };
  BasicPluginMessageRequest(Params p) { super::params = p; }
  std::string
  get_method() const
  {
    return "admin_plugin_send_basic_msg";
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
struct ServerStartDrainRequest : CtrlClientRequest {
  using super = CtrlClientRequest;
  struct Params {
    bool waitForNewConnections{false};
  };
  ServerStartDrainRequest(Params p)
  {
    super::method = "admin_server_start_drain";
    super::params = p;
  }
  std::string
  get_method() const
  {
    return "admin_server_start_drain";
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
struct ServerStopDrainRequest : CtrlClientRequest {
  using super = ServerStopDrainRequest;
  std::string
  get_method() const
  {
    return "admin_server_start_drain";
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
struct SetStorageDeviceOfflineRequest : CtrlClientRequest {
  using super = CtrlClientRequest;
  struct Params {
    std::vector<std::string> names;
  };
  SetStorageDeviceOfflineRequest(Params p) { super::params = p; }
  std::string
  get_method() const
  {
    return "admin_storage_set_device_offline";
  }
};

//------------------------------------------------------------------------------------------------------------------------------------
struct GetStorageDeviceStatusRequest : CtrlClientRequest {
  using super = CtrlClientRequest;
  struct Params {
    std::vector<std::string> names;
  };
  GetStorageDeviceStatusRequest(Params p) { super::params = p; }
  std::string
  get_method() const
  {
    return "admin_storage_get_device_status";
  }
};

struct DeviceStatusInfoResponse {
  struct CacheDisk {
    CacheDisk(std::string p, std::string s, int e) : path(std::move(p)), status(std::move(s)), errorCount(e) {}
    std::string path;
    std::string status;
    int errorCount;
  };
  std::vector<CacheDisk> data;
};
//------------------------------------------------------------------------------------------------------------------------------------
struct ShowRegisterHandlersRequest : CtrlClientRequest {
  using super = CtrlClientRequest;
  std::string
  get_method() const
  {
    return "show_registered_handlers";
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
// We expect the method to be passed, this request is used to create dynamic requests by using (traffic_ctl rpc invoke "func_name")
struct CustomizableRequest : CtrlClientRequest {
  using super = CtrlClientRequest;
  CustomizableRequest(std::string const &methodName) { super::method = methodName; }
  std::string
  get_method() const
  {
    return super::method;
  }
};
//------------------------------------------------------------------------------------------------------------------------------------
