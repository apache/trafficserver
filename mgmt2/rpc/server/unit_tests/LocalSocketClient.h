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

#include "helpers.h"
#include <tscore/BufferWriter.h>

static constexpr auto logTag{"rpc.test"};
static constexpr std::size_t READ_BUFFER_SIZE{32000};
// This class is used for testing purposes only. To make the usage easy and more readable this class provides
// a chained API, so you can do this like this:
//
//    ScopedLocalSocket rpc_client;
//    auto resp = rpc_client.connect().send(json).read();

// In order to prevent the misuse of the API this class implements a sort of state machine that will assert if
// you call a function which is not in the right state, so read will assert if it is not called after a send.
// Send will assert if not called after connect, and so on.
// There was no need to make this at compile time as it’s only used for unit tests.
struct LocalSocketClient {
  using self_reference = LocalSocketClient &;

  LocalSocketClient(std::string path) : _state{State::DISCONNECTED}, _path{std::move(path)} {}
  LocalSocketClient() : _state{State::DISCONNECTED}, _path{"/tmp/jsonrpc20.sock"} {}

  ~LocalSocketClient() { this->disconnect(); }
  self_reference
  connect()
  {
    if (_state == State::CONNECTED) {
      return *this;
    }
    _sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (_sock < 0) {
      Debug(logTag, "error reading stream message :%s", std ::strerror(errno));
      throw std::runtime_error{std::strerror(errno)};
    }
    _server.sun_family = AF_UNIX;
    strcpy(_server.sun_path, _path.c_str());
    if (::connect(_sock, (struct sockaddr *)&_server, sizeof(struct sockaddr_un)) < 0) {
      this->close();
      Debug(logTag, "error reading stream message :%s", std ::strerror(errno));
      throw std::runtime_error{std::strerror(errno)};
    }

    _state = State::CONNECTED;
    return *this;
  }

  bool
  is_connected()
  {
    return _state == State::CONNECTED;
  }

  template <std::size_t N>
  self_reference
  send_in_chunks_with_wait(std::string_view data, std::chrono::milliseconds wait_between_write)
  {
    assert(_state == State::CONNECTED || _state == State::SENT);

    auto chunks = chunk<N>(data);
    for (auto &&part : chunks) {
      if (::write(_sock, part.c_str(), part.size()) < 0) {
        Debug(logTag, "error sending message :%s", std ::strerror(errno));
        break;
      }
      std::this_thread::sleep_for(wait_between_write);
    }
    _state = State::SENT;
    return *this;
  }

  self_reference
  send(std::string_view data)
  {
    assert(_state == State::CONNECTED || _state == State::SENT);

    if (::write(_sock, data.data(), data.size()) < 0) {
      Debug(logTag, "Error writing on stream socket %s", std ::strerror(errno));
      this->close();
    }
    _state = State::SENT;

    return *this;
  }

  std::string
  read()
  {
    assert(_state == State::SENT);

    if (_sock <= 0) {
      // we had a failure.
      return {};
    }

    char buf[READ_BUFFER_SIZE]; // should be enough
    bzero(buf, READ_BUFFER_SIZE);
    ssize_t rval{-1};

    if (rval = ::read(_sock, buf, READ_BUFFER_SIZE); rval <= 0) {
      Debug(logTag, "error reading stream message :%s, socket: %d", std ::strerror(errno), _sock);
      this->disconnect();
      return {};
    }
    _state = State::RECEIVED;

    return {buf, static_cast<std::size_t>(rval)};
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

private:
  enum class State { CONNECTED, DISCONNECTED, SENT, RECEIVED };
  State _state;
  std::string _path;

  int _sock{-1};
  struct sockaddr_un _server;
};
