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
#include <yaml-cpp/yaml.h>
#include <tscore/BufferWriter.h>
#include <tscore/ink_uuid.h>

/// JSONRPC 2.0 Client API utility definitions. Only client applications should use these definitions. Internal handlers should not
/// use these definitions. Check @c mgmg2/rpc/jsonrpc/Defs.h instead

/// @brief @c JSONRPC 2.0 message mapping classes.
/// This is a very thin  API to deal with encoding/decoding jsonrpc 2.0 messages.
/// More info can be found https://www.jsonrpc.org/specification
namespace shared::rpc
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
struct ClientRequest : JSONRPCRequest {
  using super = JSONRPCRequest;
  ClientRequest() { super::id = _idGen.getString(); }

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
struct ClientRequestNotification : JSONRPCRequest {
  ClientRequestNotification() {}
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
struct RecordLookupRequest : ClientRequest {
  using super = ClientRequest;
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
    bool registered;
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

inline std::ostream &
operator<<(std::ostream &os, const RecordLookUpResponse::RecordError &re)
{
  std::string text;
  os << ts::bwprint(text, "{:16s}: {}\n", "Record Name ", re.recordName);
  os << ts::bwprint(text, "{:16s}: {}\n", "Code", re.code);
  if (!re.message.empty()) {
    os << ts::bwprint(text, "{:16s}: {}\n", "Message", re.message);
  }
  return os;
}

inline std::ostream &
operator<<(std::ostream &os, const JSONRPCError &err)
{
  os << "Error found.\n";
  os << "code: " << err.code << '\n';
  os << "message: " << err.message << '\n';
  if (err.data.size() > 0) {
    os << "---\nAdditional error information found:\n";
    auto my_print = [&](auto const &e) {
      os << "+ code: " << e.first << '\n';
      os << "+ message: " << e.second << '\n';
    };

    auto iter = std::begin(err.data);

    my_print(*iter);
    ++iter;
    for (; iter != std::end(err.data); ++iter) {
      os << "---\n";
      my_print(*iter);
    }
  }

  return os;
}
} // namespace shared::rpc