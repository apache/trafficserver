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

#include <variant>
#include <string>
#include <optional>

#include <yaml-cpp/yaml.h>

#include "rpc/jsonrpc/error/RPCError.h"

// This file contains all the internal types used by the RPC engine to deal with all the messages
// While we use yamlcpp for parsing, internally we model the request/response on our wrappers (RPCRequest, RPCResponse)
namespace rpc::specs
{
static constexpr auto JSONRPC_VERSION{"2.0"};

// /// @brief Class that contains a translated error from an std::error_code that can be understand by the YAMLCodec when building a
// /// response.
// struct RPCError {
//   RPCError(int c, std::string const &m) : code(c), message(m) {}
//   int code;
//   std::string message;
// };

/// @brief  This class encapsulate the registered handler call data.
/// It contains the YAML::Node that will contain the response of a call and  if any error, will also encapsulate the  error from the
/// call.
/// @see MethodHandler
class RPCHandlerResponse
{
public:
  YAML::Node result; //!< The response from the registered handler.
  ts::Errata errata; //!< The  error response from the registered handler.
};

struct RPCResponseInfo {
  RPCResponseInfo(std::optional<std::string> const &id_) : id(id_) {}
  RPCResponseInfo() = default;

  RPCHandlerResponse callResult;
  std::error_code rpcError;
  std::optional<std::string> id;
};

///
/// @brief Class that contains all the request information.
/// This class maps the jsonrpc protocol for a request. It can be used for Methods and Notifications.
/// Notifications will not use the id, this is the main reason why is a std::optional<>.
///
struct RPCRequestInfo {
  RPCRequestInfo() = default;
  RPCRequestInfo(std::string const &version, std::string const &mid) : jsonrpc(version), id(mid) {}
  std::string jsonrpc;           //!<  JsonRPC version ( we only allow 2.0 ). @see yamlcpp_json_decoder
  std::string method;            //!< incoming method name.
  std::optional<std::string> id; //!< incoming request if (only used for method calls.)
  YAML::Node params;             //!< incoming parameter structure.

  /// Convenience function that checks for the type of request. If contains id then it should be threated as method call, otherwise
  /// will be a notification.
  bool
  is_notification() const
  {
    return !id.has_value();
  }
};

template <class M> class RPCMessage;
using RPCRequest  = RPCMessage<std::pair<RPCRequestInfo, std::error_code>>;
using RPCResponse = RPCMessage<RPCResponseInfo>;

///
/// @brief Class that reprecent a RPC message, it could be either the request or the response.
/// Requests @see RPCRequest are represented by a vector of pairs, which contains, the request @see RPCRequestInfo and an associated
/// error_code in case that the request fails. The main reason of this to be a pair is that as per the protocol specs we need to
/// respond every message(if it's a method), so for cases like a batch request where you may have some of the request fail, then the
/// error will be attached to the response. The order is not important
///
/// Responses @see RPCResponse models pretty much the same structure except that there is no error associated with it. The @see
/// RPCResponseInfo could be the error.
///
/// @tparam Message The type of the RPCMessage, either a pair of @see RPCRequestInfo, std::error_code. Or @see RPCResponseInfo
///
template <typename Message> class RPCMessage
{
  static_assert(!std::is_same_v<Message, RPCRequest> || !std::is_same_v<Message, RPCResponse>,
                "Ups, only RPCRequest or RPCResponse");

  using MessageList = std::vector<Message>;
  /// @brief to keep track of internal message data.
  struct Metadata {
    Metadata() = default;
    Metadata(bool batch) : isBatch(batch) {}
    /// @brief Used to mark the incoming request and response base on the former's format. If we want to respond with the same
    /// format as the incoming request, then this should be used. base on the request's format.
    enum class MsgFormat {
      UNKNOWN = 0, //!< Default value.
      JSON,        //!< If messages arrives as JSON
      YAML         //!< If messages arrives as YAML
    };
    MsgFormat msgFormat{MsgFormat::UNKNOWN};
    bool isBatch{false};
  };

public:
  RPCMessage() {}
  RPCMessage(bool isBatch) : _metadata{isBatch} {}
  ///
  /// @brief
  ///
  /// @tparam Msg
  /// @param msg
  ///
  template <typename Msg>
  void
  add_message(Msg &&msg)
  {
    _elements.push_back(std::forward<Msg>(msg));
  }

  const MessageList &
  get_messages() const
  {
    return _elements;
  }

  bool
  is_notification() const noexcept
  {
    return _elements.size() == 0;
  }

  bool
  is_batch() const noexcept
  {
    return _metadata.isBatch;
  }

  void
  is_batch(bool isBatch) noexcept
  {
    _metadata.isBatch = isBatch;
  }
  void
  reserve(std::size_t size)
  {
    _elements.reserve(size);
  }

  bool
  is_json_format() const noexcept
  {
    return _metadata.msgFormat == Metadata::MsgFormat::JSON;
  }

  bool
  is_yaml_format() const noexcept
  {
    return _metadata.msgFormat == Metadata::MsgFormat::YAML;
  }

private:
  MessageList _elements;
  Metadata _metadata;
};

} // namespace rpc::specs
