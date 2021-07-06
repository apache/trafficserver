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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/file.h>
#include <poll.h>

#include <atomic>
#include <thread>
#include <memory>
#include <optional>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <system_error>
#include <iostream>

#include "tscore/Diags.h"
#include "tscore/bwf_std_format.h"
#include "records/I_RecProcess.h"

#include <ts/ts.h>

#include "rpc/jsonrpc/JsonRPCManager.h"
#include "rpc/server/IPCSocketServer.h"

namespace
{
constexpr size_t MAX_REQUEST_BUFFER_SIZE{32001};

// Quick check for errors(base on the errno);
bool check_for_transient_errors();

// Check poll's return and validate against the passed function.
template <typename Func>
bool
poll_on_socket(Func &&check_poll_return, std::chrono::milliseconds timeout, int fd)
{
  struct pollfd poll_fd;
  poll_fd.fd     = fd;
  poll_fd.events = POLLIN; // when data is ready.
  int poll_ret;
  do {
    poll_ret = poll(&poll_fd, 1, timeout.count());
  } while (check_poll_return(poll_ret));

  if (!(poll_fd.revents & POLLIN)) {
    return false;
  }
  return true;
}
} // namespace

namespace rpc::comm
{
static constexpr auto logTag = "rpc.net";

IPCSocketServer::~IPCSocketServer()
{
  unlink(_conf.sockPathName.c_str());
}

bool
IPCSocketServer::configure(YAML::Node const &params)
{
  try {
    _conf = params.as<Config>();
  } catch (YAML::Exception const &ex) {
    return false;
  }

  return true;
}

std::error_code
IPCSocketServer::init()
{
  // Need to run some validations on the pathname to avoid issue. Normally this would not be an issue, but some tests may fail on
  // this.
  if (_conf.sockPathName.empty() || _conf.sockPathName.size() > sizeof _serverAddr.sun_path) {
    Debug(logTag, "Invalid unix path name, check the size.");
    return std::make_error_code(static_cast<std::errc>(ENAMETOOLONG));
  }

  std::error_code ec; // Flag possible errors.

  if (this->create_socket(ec); ec) {
    return ec;
  }

  Debug(logTag, "Using %s as socket path.", _conf.sockPathName.c_str());
  _serverAddr.sun_family = AF_UNIX;
  std::strncpy(_serverAddr.sun_path, _conf.sockPathName.c_str(), sizeof(_serverAddr.sun_path) - 1);

  if (this->bind(ec); ec) {
    this->close();
    return ec;
  }

  if (this->listen(ec); ec) {
    this->close();
    return ec;
  }

  return ec;
}

bool
IPCSocketServer::poll_for_new_client(std::chrono::milliseconds timeout) const
{
  auto check_poll_return = [&](int pfd) -> bool {
    if (!_running.load()) {
      return false;
    }
    if (pfd < 0) {
      switch (errno) {
      case EINTR:
      case EAGAIN:
        return true;
      default:
        return false;
      }
    } else if (pfd > 0) {
      // ready.
      return false;
    } else {
      // time out, try again
      return true;
    }
  };

  return poll_on_socket(check_poll_return, timeout, this->_socket);
}

void
IPCSocketServer::run()
{
  _running.store(true);

  ts::LocalBufferWriter<MAX_REQUEST_BUFFER_SIZE> bw;
  while (_running) {
    // poll till socket it's ready.
    if (!this->poll_for_new_client()) {
      if (_running.load()) {
        Warning("ups, we've got an issue.");
      }
      break;
    }

    std::error_code ec;
    if (auto fd = this->accept(ec); !ec) {
      Client client{fd};

      if (auto [ok, errStr] = client.read_all(bw); ok) {
        const auto json = std::string{bw.data(), bw.size()};
        if (auto response = rpc::JsonRPCManager::instance().handle_call(json); response) {
          // seems a valid response.
          if (client.write(*response, ec); ec) {
            Debug(logTag, "Error sending the response: %s", ec.message().c_str());
          }
        } // it was a notification.
      } else {
        Debug(logTag, "Error detected while reading: %s", errStr.c_str());
      }
    } else {
      Debug(logTag, "Error while accepting a new connection on the socket: %s", ec.message().c_str());
    }

    bw.reset();
  }

  this->close();
}

bool
IPCSocketServer::stop()
{
  _running.store(false);

  this->close();

  return true;
}

void
IPCSocketServer::create_socket(std::error_code &ec)
{
  _socket = socket(AF_UNIX, SOCK_STREAM, 0);

  if (_socket < 0) {
    ec = std::make_error_code(static_cast<std::errc>(errno));
  }
}

int
IPCSocketServer::accept(std::error_code &ec) const
{
  int ret = {-1};

  for (int retries = 0; retries < _conf.maxRetriesOnTransientErrors; retries++) {
    ret = ::accept(_socket, 0, 0);
    if (ret >= 0) {
      return ret;
    }
    if (!check_for_transient_errors()) {
      ec = std::make_error_code(static_cast<std::errc>(errno));
      return ret;
    }
  }

  if (ret < 0) {
    // seems that we have reched the max retries.
    ec = InternalError::MAX_TRANSIENT_ERRORS_HANDLED;
  }

  return ret;
}

void
IPCSocketServer::bind(std::error_code &ec)
{
  int lock_fd = open(_conf.lockPathName.c_str(), O_RDONLY | O_CREAT, 0600);
  if (lock_fd == -1) {
    ec = std::make_error_code(static_cast<std::errc>(errno));
    return;
  }

  int ret = flock(lock_fd, LOCK_EX | LOCK_NB);
  if (ret != 0) {
    ec = std::make_error_code(static_cast<std::errc>(errno));
    return;
  }
  // TODO: we may be able to use SO_REUSEADDR

  // remove socket file
  unlink(_conf.sockPathName.c_str());

  ret = ::bind(_socket, (struct sockaddr *)&_serverAddr, sizeof(struct sockaddr_un));
  if (ret < 0) {
    ec = std::make_error_code(static_cast<std::errc>(errno));
    return;
  }

  mode_t mode = _conf.restrictedAccessApi ? 00700 : 00777;
  if (chmod(_conf.sockPathName.c_str(), mode) < 0) {
    ec = std::make_error_code(static_cast<std::errc>(errno));
    return;
  }
}

void
IPCSocketServer::listen(std::error_code &ec)
{
  if (::listen(_socket, _conf.backlog) < 0) {
    ec = std::make_error_code(static_cast<std::errc>(errno));
    return;
  }
}

void
IPCSocketServer::close()
{
  if (_socket > 0) {
    ::close(_socket);
    _socket = -1;
  }
}
//// client

IPCSocketServer::Client::Client(int fd) : _fd{fd} {}
IPCSocketServer::Client::~Client()
{
  this->close();
}

// ---------------- client --------------
bool
IPCSocketServer::Client::poll_for_data(std::chrono::milliseconds timeout) const
{
  auto check_poll_return = [&](int pfd) -> bool {
    if (pfd > 0) {
      // something is ready.
      return false;
    } else if (pfd < 0) {
      switch (errno) {
      case EINTR:
      case EAGAIN:
        return true;
      default:
        return false;
      }
    } else { // timeout
      return false;
    }
  };

  return poll_on_socket(check_poll_return, timeout, this->_fd);
}

void
IPCSocketServer::Client::close()
{
  if (_fd > 0) {
    ::close(_fd);
    _fd = -1;
  }
}

ssize_t
IPCSocketServer::Client::read(ts::MemSpan<char> span) const
{
  return ::read(_fd, span.data(), span.size());
}

std::tuple<bool, std::string>
IPCSocketServer::Client::read_all(ts::FixedBufferWriter &bw) const
{
  std::string buff;
  while (bw.remaining() > 0) {
    auto ret = read({bw.auxBuffer(), bw.remaining()});
    if (ret < 0) {
      if (check_for_transient_errors()) {
        continue;
      } else {
        return {false, ts::bwprint(buff, "Error reading the socket: {}", ts::bwf::Errno{})};
      }
    }

    if (ret == 0) {
      if (bw.size()) {
        return {false, ts::bwprint(buff, "Peer disconnected after reading {} bytes.", bw.size())};
      }
      return {false, ts::bwprint(buff, "Peer disconnected. EOF")};
    }
    bw.fill(ret);
    if (bw.remaining() > 0) {
      using namespace std::chrono_literals;
      if (!this->poll_for_data(1ms)) {
        return {true, buff};
      }
      continue;
    } else {
      ts::bwprint(buff, "Buffer is full, we hit the limit: {}", bw.capacity());
      break;
    }
  }

  return {false, buff};
}

void
IPCSocketServer::Client::write(std::string const &data, std::error_code &ec) const
{
  if (::write(_fd, data.c_str(), data.size()) < 0) {
    ec = std::make_error_code(static_cast<std::errc>(errno));
  }
}
IPCSocketServer::Config::Config()
{
  // Set default values.
  std::string rundir{RecConfigReadRuntimeDir()};
  lockPathName = Layout::relative_to(rundir, "jsonrpc20.lock");
  sockPathName = Layout::relative_to(rundir, "jsonrpc20.sock");
}
} // namespace rpc::comm

namespace YAML
{
template <> struct convert<rpc::comm::IPCSocketServer::Config> {
  using config = rpc::comm::IPCSocketServer::Config;

  static bool
  decode(const Node &node, config &rhs)
  {
    // ++ If we configure this, traffic_ctl will not be able to connect.
    // ++ This is meant to be used by unit test as you need to set up  a
    // ++ server.
    if (auto n = node[config::LOCK_PATH_NAME_KEY_STR]) {
      rhs.lockPathName = n.as<std::string>();
    }
    if (auto n = node[config::SOCK_PATH_NAME_KEY_STR]) {
      rhs.sockPathName = n.as<std::string>();
    }

    if (auto n = node[config::BACKLOG_KEY_STR]) {
      rhs.backlog = n.as<int>();
    }
    if (auto n = node[config::MAX_RETRY_ON_TR_ERROR_KEY_STR]) {
      rhs.maxRetriesOnTransientErrors = n.as<int>();
    }
    if (auto n = node[config::RESTRICTED_API]) {
      rhs.restrictedAccessApi = n.as<bool>();
    }
    return true;
  }
};
} // namespace YAML

namespace
{
bool
check_for_transient_errors()
{
  switch (errno) {
  case EINTR:
  case EAGAIN:

#ifdef ENOMEM
  case ENOMEM:
#endif

#ifdef ENOBUF
  case ENOBUF:
#endif

#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
  case EWOULDBLOCK:
#endif
    return true;
  default:
    return false;
  }
}
} // namespace