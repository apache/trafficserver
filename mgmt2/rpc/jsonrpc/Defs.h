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

#include "yaml-cpp/yaml.h"

#include "rpc/jsonrpc/error/RpcError.h"
#include "rpc/common/JsonRpcApi.h"

// This file contains all the internal types used by the RPC engine to deal with all the messages
// While we use yamlcpp for parsing, internally we model the request/response on our wrappers (RpcRequest, RpcResponse)
namespace rpc::jsonrpc
{
static constexpr auto JSONRPC_VERSION{"2.0"};

struct RpcError {
  RpcError(int c, std::string const &m) : code(c), message(m) {}
  int code;
  std::string message;
};

class RpcHandlerResponse
{
public:
  YAML::Node result;
  ts::Errata errata;
};

struct RpcResponseInfo {
  RpcResponseInfo(std::optional<std::string> const &id_) : id(id_) {}
  RpcResponseInfo() = default;

  RpcHandlerResponse callResult;
  std::optional<RpcError> rpcError;
  std::optional<std::string> id;
};

struct RpcRequestInfo {
  std::string jsonrpc;
  std::string method;
  std::optional<std::string> id;
  YAML::Node params;

  bool
  is_notification() const
  {
    return !id.has_value();
  }
};

template <typename Message> class RPCMessage
{
  using MessageList = std::vector<Message>;

public:
  RPCMessage() {}
  RPCMessage(bool isBatch) : _is_batch(isBatch) {}
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
    return _is_batch;
  }

  void
  is_batch(bool isBatch) noexcept
  {
    _is_batch = isBatch;
  }
  void
  reserve(std::size_t size)
  {
    _elements.reserve(size);
  }

private:
  MessageList _elements;
  bool _is_batch{false};
};

using RpcRequest  = RPCMessage<std::pair<RpcRequestInfo, std::error_code>>;
using RpcResponse = RPCMessage<RpcResponseInfo>;
} // namespace rpc::jsonrpc
