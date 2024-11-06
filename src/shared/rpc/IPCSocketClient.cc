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
#include <unistd.h>

#include <stdexcept>
#include <chrono>
#include <utility>
#include <thread>

#include "tsutil/ts_bw_format.h"

#include "shared/rpc/IPCSocketClient.h"
#include "shared/rpc/MessageStorage.h"
#include <tscore/ink_assert.h>
#include <tscore/ink_sock.h>

namespace shared::rpc
{

IPCSocketClient::self_reference
IPCSocketClient::connect(std::chrono::milliseconds wait_ms, int attempts)
{
  std::string text;
  int         err, tries{attempts};
  bool        done{false};
  _sock = socket(AF_UNIX, SOCK_STREAM, 0);

  if (this->is_closed()) {
    throw std::runtime_error(swoc::bwprint(text, "connect: error creating new socket. Reason: {}\n", std::strerror(errno)));
  }

  if (safe_fcntl(_sock, F_SETFL, O_NONBLOCK) < 0) {
    this->close();
    throw std::runtime_error(swoc::bwprint(text, "connect: fcntl error. Reason: {}\n", std::strerror(errno)));
  }

  _server.sun_family = AF_UNIX;
  std::strncpy(_server.sun_path, _path.c_str(), sizeof(_server.sun_path) - 1);

  // Very simple connect and retry. We will just try to connect to the Unix Domain
  // Socket and if it tell us to retry we just wait for a few ms and try again for
  // X times.
  do {
    --tries;
    if (::connect(_sock, (struct sockaddr *)&_server, sizeof(struct sockaddr_un)) >= 0) {
      done = true;
      break;
    }

    if (errno == EAGAIN || errno == EINPROGRESS) {
      // Connection cannot be completed immediately
      // EAGAIN for UDS should suffice, but just in case.
      std::this_thread::sleep_for(wait_ms);
      err = errno;
      continue;
    } else {
      // No worth it.
      err = errno;
      break;
    }
  } while (tries != 0);

  if ((tries == 0 && !done) || !done) {
    this->close();
    errno = err;
    throw std::runtime_error(swoc::bwprint(text, "connect(attempts={}/{}): Couldn't open connection with {}. Last error: {}({})\n",
                                           (attempts - tries), attempts, _path, std::strerror(errno), errno));
  }
  return *this;
}

ssize_t
IPCSocketClient::_safe_write(int fd, const char *buffer, int len)
{
  ssize_t written{0};
  while (written < len) {
    const int ret = ::write(fd, buffer + written, len - written);
    if (ret == -1) {
      if (errno == EAGAIN || errno == EINTR) {
        continue;
      }
      return -1;
    }
    written += ret;
  }

  return written;
}
IPCSocketClient::self_reference
IPCSocketClient ::send(std::string_view data)
{
  std::string msg{data};
  if (_safe_write(_sock, msg.c_str(), msg.size()) == -1) {
    this->close();
    std::string text;
    throw std::runtime_error{swoc::bwprint(text, "Error writing on stream socket {}({})", std::strerror(errno), errno)};
  }

  return *this;
}

IPCSocketClient::ReadStatus
IPCSocketClient::read_all(std::string &content, std::chrono::milliseconds timeout_ms, int attempts)
{
  if (this->is_closed()) {
    // we had a failure.
    return ReadStatus::UNKNOWN;
  }

  MessageStorage<356000> bs;
  int                    attempts_left{attempts};
  ReadStatus             readStatus{ReadStatus::NO_ERROR};
  // Try to read all the data from the socket. If a timeout happens we retry
  // 'attemps' times. On error we just stop.
  while (attempts_left > 0 || readStatus == ReadStatus::NO_ERROR) {
    auto       buf     = bs.writable_data();
    const auto to_read = bs.available(); // Available in the current memory chunk.
    ssize_t    nread{-1};

    // Try again if timed out.
    if (auto const r = read_ready(_sock, timeout_ms.count()); r == 0) {
      readStatus = ReadStatus::TIMEOUT;
      --attempts_left;
      continue;
    } else if (r < 0) {
      // No more tries.
      readStatus = ReadStatus::READ_ERROR;
      break;
    }

    nread = ::read(_sock, buf, to_read);
    if (nread > 0) {
      bs.save(nread);
      continue;
    } else if (nread == -1) {
      if (errno == EAGAIN || errno == EINTR) {
        continue;
      }
      readStatus = ReadStatus::READ_ERROR;
      break;
    }
    // EOF
    if (bs.stored() > 0) {
      readStatus = ReadStatus::NO_ERROR;
      break;
    }
    readStatus = ReadStatus::READ_ERROR;
    break;
  }
  content = bs.str();
  return readStatus;
}
} // namespace shared::rpc
