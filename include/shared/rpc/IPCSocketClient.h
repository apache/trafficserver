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

#include <stdexcept>
#include <chrono>

#include <sys/types.h>
#include <sys/unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <tscore/BufferWriter.h>

namespace shared::rpc
{
/// The goal of this class is abstract the Unix Socket implementation and provide a JSONRPC Node client for Tests and client's
/// applications like traffic_ctl and traffic_top.
/// To make the usage easy and more readable this class provides a chained API, so you can do this like this:
///
///    IPCSocketClient client;
///    auto resp = client.connect().send(json).read();
///
/// There is also a @c RPCClient class which should be used unless you need some extra control of the socket client.
///
/// Error handling: Enclose this inside a try/catch because if any error is detected functions will throw.
struct IPCSocketClient {
  enum class ReadStatus { NO_ERROR = 0, BUFFER_FULL, STREAM_ERROR, UNKNOWN };
  using self_reference = IPCSocketClient &;

  IPCSocketClient(std::string path) : _path{std::move(path)} {}
  IPCSocketClient() : _path{"/tmp/jsonrpc20.sock"} {}

  ~IPCSocketClient() { this->disconnect(); }

  /// Connect to the configured socket path.
  self_reference connect();

  /// Send all the passed string to the socket.
  self_reference send(std::string_view data);

  /// Read all the content from the socket till the passed buffer is full.
  ReadStatus read_all(ts::FixedBufferWriter &bw);

  /// Closes the socket.
  void
  disconnect()
  {
    this->close();
  }

  /// Close the socket.
  void
  close()
  {
    if (_sock > 0) {
      ::close(_sock);
      _sock = -1;
    }
  }

  /// Test if the socket was closed or it wasn't initialized.
  bool
  is_closed() const
  {
    return (_sock < 0);
  }

protected:
  std::string _path;
  struct sockaddr_un _server;

  int _sock{-1};
};
} // namespace shared::rpc