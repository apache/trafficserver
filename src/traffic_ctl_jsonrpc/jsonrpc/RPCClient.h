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

#include <iostream>
#include <string_view>

#include <yaml-cpp/yaml.h>

#include "tools/cpp/IPCSocketClient.h"
#include "tscore/I_Layout.h"
#include <tscore/BufferWriter.h>

///
/// @brief Wrapper class to interact with the RPC Server
///
class RPCClient
{
  // Large buffer, as we may query a full list of records.
  // TODO: should we add a parameter to increase the buffer? or maybe a record limit on the server's side?
  static constexpr int BUFFER_SIZE{3560000};

public:
  RPCClient() : _client(Layout::get()->runtimedir + "/jsonrpc20.sock") {}
  std::string
  call(std::string_view req)
  {
    std::string text; // for error messages.
    ts::LocalBufferWriter<BUFFER_SIZE> bw;
    try {
      auto resp = _client.connect();
      if (_client.is_connected()) {
        _client.send(req);
        auto ret = _client.read(bw);
        switch (ret) {
        case IPCSocketClient::ReadStatus::OK: {
          _client.disconnect();
          return {bw.data(), bw.size()};
          ;
        }
        case IPCSocketClient::ReadStatus::BUFFER_FULL: {
          throw std::runtime_error(
            ts::bwprint(text, "Buffer full, not enough space to read the response. Buffer size: {}", BUFFER_SIZE));
        } break;
        default:
          throw std::runtime_error("Something happened, we can't read the response");
          break;
        }
      }
    } catch (std::exception const &ex) {
      _client.disconnect();
      throw std::runtime_error(ts::bwprint(text, "Server Error: {}", ex.what()));
    }

    return {};
  }

private:
  IPCSocketClient _client;
};