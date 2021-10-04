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
#include "shared/rpc/IPCSocketClient.h"
#include <stdexcept>
#include <chrono>

#include <tscore/ink_assert.h>
#include <tscore/BufferWriter.h>

namespace shared::rpc
{
IPCSocketClient::self_reference
IPCSocketClient::connect()
{
  _sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (this->is_closed()) {
    std::string text;
    ts::bwprint(text, "connect: error creating new socket. Why?: {}\n", std::strerror(errno));
    throw std::runtime_error{text};
  }
  _server.sun_family = AF_UNIX;
  std::strncpy(_server.sun_path, _path.c_str(), sizeof(_server.sun_path) - 1);
  if (::connect(_sock, (struct sockaddr *)&_server, sizeof(struct sockaddr_un)) < 0) {
    this->close();
    std::string text;
    ts::bwprint(text, "connect: Couldn't open connection with {}. Why?: {}\n", _path, std::strerror(errno));
    throw std::runtime_error{text};
  }

  return *this;
}

IPCSocketClient::self_reference
IPCSocketClient ::send(std::string_view data)
{
  std::string msg{data};
  if (::write(_sock, msg.c_str(), msg.size()) < 0) {
    this->close();
    std::string text;
    throw std::runtime_error{ts::bwprint(text, "Error writing on stream socket {}", std ::strerror(errno))};
  }

  return *this;
}

IPCSocketClient::ReadStatus
IPCSocketClient::read_all(ts::FixedBufferWriter &bw)
{
  if (this->is_closed()) {
    // we had a failure.
    return {};
  }
  ReadStatus readStatus{ReadStatus::UNKNOWN};
  while (bw.remaining()) {
    ts::MemSpan<char> span{bw.auxBuffer(), bw.remaining()};
    const ssize_t ret = ::read(_sock, span.data(), span.size());
    if (ret > 0) {
      bw.fill(ret);
      if (bw.remaining() > 0) { // some space available.
        continue;
      } else {
        // buffer full.
        readStatus = ReadStatus::BUFFER_FULL;
        break;
      }
    } else {
      if (bw.size()) {
        // data was read.
        readStatus = ReadStatus::NO_ERROR;
        break;
      }
      readStatus = ReadStatus::STREAM_ERROR;
      break;
    }
  }
  return readStatus;
}
} // namespace shared::rpc