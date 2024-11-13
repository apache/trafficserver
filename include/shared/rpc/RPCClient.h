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

/// JSONRPC 2.0 RPC network client.

#include <iostream>
#include <string_view>

#include <yaml-cpp/yaml.h>
#include <tscore/Layout.h>
#include <tscore/ink_assert.h>

#include <swoc/BufferWriter.h>

#include "shared/rpc/IPCSocketClient.h"
#include "shared/rpc/yaml_codecs.h"

namespace shared::rpc
{
///
/// @brief Wrapper class to interact with the RPC node. Do not use this internally, this is for client's applications only.
///
class RPCClient
{
public:
  RPCClient() : _client(Layout::get()->runtimedir + "/jsonrpc20.sock") {}

  /// @brief invoke the remote function using the passed jsonrpc message string.
  /// This function will connect with the remote rpc node and send the passed json string. If you don't want to deal with the
  /// endode/decode you can just call @c invoke(JSONRPCRequest const &req).
  /// @throw runtime_error
  std::string
  invoke(std::string_view req)
  {
    std::string err_text; // for error messages.
    try {
      _client.connect();
      if (!_client.is_closed()) {
        std::string resp;
        _client.send(req);
        switch (_client.read_all(resp)) {
        case IPCSocketClient::ReadStatus::NO_ERROR: {
          _client.disconnect();
          return resp;
        }
        case IPCSocketClient::ReadStatus::BUFFER_FULL:
          // we don't expect this to happen as client will not let us know about
          // this for now. Keep this in case we decide to put a limit on
          // responses.
          ink_assert(!"Buffer full, not enough space to read the response.");
          break;
        case IPCSocketClient::ReadStatus::READ_ERROR:
          err_text = swoc::bwprint(err_text, "READ_ERROR: Error while reading response. {}({})", std::strerror(errno), errno);
          break;
        case IPCSocketClient::ReadStatus::TIMEOUT:
          err_text = swoc::bwprint(err_text, "TIMEOUT: Couldn't get the response. {}({})", std::strerror(errno), errno);
          break;
        default:
          err_text = "Something happened, we can't read the response. Unknown error.";
          break;
        }
      } else {
        swoc::bwprint(err_text, "Node seems not available: {}", std ::strerror(errno));
      }
    } catch (std::exception const &ex) {
      swoc::bwprint(err_text, "RPC Node Error: {}", ex.what());
    }
    _client.disconnect();
    // If we've got this far, then there must be an error to report back.
    throw std::runtime_error(err_text);
  }

  /// @brief Invoke the rpc node passing the JSONRPC objects.
  /// This function will connect with the remote rpc node and send the passed objects which will be encoded and decoded using the
  /// yamlcpp_json_emitter impl.
  /// @note If you inherit from @c JSONRPCRequest make sure the base members are properly filled before calling this function, the
  /// encode/decode will only deal with the @c JSONRPCRequest members, unless you pass your own codec class. By default @c
  /// yamlcpp_json_emitter is used. If you pass your own Codecs, make sure you follow the yamlcpp_json_emitter API.
  /// @throw runtime_error
  /// @throw YAML::Exception
  template <typename Codec = yamlcpp_json_emitter>
  JSONRPCResponse
  invoke(JSONRPCRequest const &req)
  {
    static_assert(internal::has_decode<Codec>::value || internal::has_encode<Codec>::value,
                  "You need to implement encode/decode in your own codec impl.");
    // We should add a static_assert and make sure encode/decode are part of Codec type.
    auto const &reqStr = Codec::encode(req);
    return Codec::decode(invoke(reqStr));
  }

private:
  IPCSocketClient _client;
};
} // namespace shared::rpc
