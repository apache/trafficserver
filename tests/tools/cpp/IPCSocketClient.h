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
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>

#include "tscore/Diags.h"
#include <tscore/ink_assert.h>
#include <tscore/BufferWriter.h>

static constexpr auto logTag{"rpc.test"};

/// This class is used for testing purposes only(and traffic_ctl). To make the usage easy and more readable this class provides
/// a chained API, so you can do this like this:
///
///    IPCSocketClient client;
///    auto resp = client.connect().send(json).read();
///
/// In order to prevent the misuse of the API this class implements a sort of state machine that will assert if
/// you call a function which is not in the right state, so read will assert if it is not called after a send.
/// Send will assert if not called after connect, and so on.
struct IPCSocketClient {
  enum class ReadStatus { OK = 0, BUFFER_FULL, STREAM_ERROR };
  using self_reference = IPCSocketClient &;

  IPCSocketClient(std::string path) : _path{std::move(path)}, _state{State::DISCONNECTED} {}
  IPCSocketClient() : _path{"/tmp/jsonrpc20.sock"}, _state{State::DISCONNECTED} {}

  ~IPCSocketClient() { this->disconnect(); }
  self_reference
  connect()
  {
    if (_state == State::CONNECTED) {
      return *this;
    }
    _sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (_sock < 0) {
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

    _state = State::CONNECTED;
    return *this;
  }

  bool
  is_connected()
  {
    return _state == State::CONNECTED;
  }

  self_reference
  send(std::string_view data)
  {
    ink_assert(_state == State::CONNECTED || _state == State::SENT);

    if (::write(_sock, data.data(), data.size()) < 0) {
      // TODO: work on the return error so the client can do something about it. EWOULDBLOCK, EAGAIN, etc.
      Debug(logTag, "Error writing on stream socket %s", std ::strerror(errno));
      this->close();
    }
    _state = State::SENT;

    return *this;
  }

  ReadStatus
  read(ts::FixedBufferWriter &bw)
  {
    ink_assert(_state == State::SENT);

    if (_sock <= 0) {
      // we had a failure.
      return {};
    }

    while (bw.remaining()) {
      ts::MemSpan<char> span{bw.auxBuffer(), bw.remaining()};
      const ssize_t ret = ::read(_sock, span.data(), span.size());
      if (ret > 0) {
        bw.fill(ret);
        if (bw.remaining() > 0) { // some space available.
          continue;
        } else {
          // buffer full.
          //   break;
          return ReadStatus::BUFFER_FULL;
        }
      } else {
        if (bw.size()) {
          // data was read.
          return ReadStatus::OK;
        }
        Debug(logTag, "error reading stream message :%s, socket: %d", std ::strerror(errno), _sock);
        this->disconnect();
        return ReadStatus::STREAM_ERROR;
      }
    }
    _state = State::RECEIVED;
    return ReadStatus::OK;
  }

  void
  disconnect()
  {
    this->close();
    _state = State::DISCONNECTED;
  }

  void
  close()
  {
    if (_sock > 0) {
      ::close(_sock);
      _sock = -1;
    }
  }

protected:
  enum class State { CONNECTED, DISCONNECTED, SENT, RECEIVED };

  std::string _path;
  State _state;
  struct sockaddr_un _server;

  int _sock{-1};
};
