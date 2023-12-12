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
#include <stdexcept>
#include <chrono>
#include <sstream>
#include <utility>

#include "tsutil/ts_bw_format.h"

#include "shared/rpc/IPCSocketClient.h"
#include <tscore/ink_assert.h>

namespace
{
/// @brief Simple buffer to store the jsonrpc server's response.
///
///        With small content it will just use the LocalBufferWritter, if the
///        content gets bigger, then it will just save the buffer into a stream
///        and reuse the already created BufferWritter.
template <size_t N> class BufferStream
{
  std::ostringstream _os;
  swoc::LocalBufferWriter<N> _bw;
  size_t _written{0};

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
IPCSocketClient::connect()
{
  _sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (this->is_closed()) {
    std::string text;
    swoc::bwprint(text, "connect: error creating new socket. Why?: {}\n", std::strerror(errno));
    throw std::runtime_error{text};
  }
  _server.sun_family = AF_UNIX;
  std::strncpy(_server.sun_path, _path.c_str(), sizeof(_server.sun_path) - 1);
  if (::connect(_sock, (struct sockaddr *)&_server, sizeof(struct sockaddr_un)) < 0) {
    this->close();
    std::string text;
    swoc::bwprint(text, "connect: Couldn't open connection with {}. Why?: {}\n", _path, std::strerror(errno));
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
    throw std::runtime_error{swoc::bwprint(text, "Error writing on stream socket {}", std ::strerror(errno))};
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
    auto buf           = bs.writable_data();
    const auto to_read = bs.available();
    const ssize_t ret  = ::read(_sock, buf, to_read);

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
