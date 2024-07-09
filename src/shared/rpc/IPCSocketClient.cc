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
#include <sstream>
#include <utility>
#include <thread>

#include "tsutil/ts_bw_format.h"

#include "shared/rpc/IPCSocketClient.h"
#include <tscore/ink_assert.h>
#include <tscore/ink_sock.h>

namespace
{
/// @brief Simple buffer to store the jsonrpc server's response.
///
///        With small content it will just use the LocalBufferWritter, if the
///        content gets bigger, then it will just save the buffer into a stream
///        and reuse the already created BufferWritter.
template <size_t N> class BufferStream
{
  std::ostringstream         _os;
  swoc::LocalBufferWriter<N> _bw;
  size_t                     _written{0};

public:
  char *
  writable_data()
  {
    return _bw.aux_data();
  }

  void
  save(size_t n)
  {
    _bw.commit(n);

    if (_bw.remaining() == 0) { // no more space available, flush what's on the bw
                                // and reset it.
      flush();
    }
  }

  size_t
  available() const
  {
    return _bw.remaining();
  }

  void
  flush()
  {
    if (_bw.size() == 0) {
      return;
    }
    _os.write(_bw.view().data(), _bw.size());
    _written += _bw.size();

    _bw.clear();
  }

  std::string
  str()
  {
    if (stored() <= _bw.size()) {
      return {_bw.data(), _bw.size()};
    }

    flush();
    return _os.str();
  }

  size_t
  stored() const
  {
    return _written ? _written : _bw.size();
  }
};
} // namespace

namespace shared::rpc
{

IPCSocketClient::self_reference
IPCSocketClient::connect(std::chrono::milliseconds ms, int attempts)
{
  std::string text;
  int         err, tries{attempts};
  bool        done{false};
  _sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);

  if (this->is_closed()) {
    throw std::runtime_error(swoc::bwprint(text, "connect: error creating new socket. Reason: {}\n", std::strerror(errno)));
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
      std::this_thread::sleep_for(ms);
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

IPCSocketClient::self_reference
IPCSocketClient ::send(std::string_view data)
{
  std::string msg{data};
  if (safe_write(_sock, msg.c_str(), msg.size()) < 0) {
    this->close();
    std::string text;
    throw std::runtime_error{swoc::bwprint(text, "Error writing on stream socket {}({})", std::strerror(errno), errno)};
  }

  return *this;
}

IPCSocketClient::ReadStatus
IPCSocketClient::read_all(std::string &content)
{
  if (this->is_closed()) {
    // we had a failure.
    return {};
  }

  BufferStream<356000> bs;

  ReadStatus readStatus{ReadStatus::UNKNOWN};
  while (true) {
    auto       buf     = bs.writable_data();
    const auto to_read = bs.available();
    ssize_t    ret{-1};
    do {
      ret = ::read(_sock, buf, to_read);
    } while (ret < 0 && (errno == EAGAIN || errno == EINTR));

    if (ret > 0) {
      bs.save(ret);
      continue;
    } else {
      if (bs.stored() > 0) {
        readStatus = ReadStatus::NO_ERROR;
        break;
      }
      readStatus = ReadStatus::STREAM_ERROR;
      break;
    }
  }
  content = bs.str();
  return readStatus;
}
} // namespace shared::rpc
