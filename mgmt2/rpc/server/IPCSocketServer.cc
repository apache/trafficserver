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
    return std::make_error_code(static_cast<std::errc>(EINVAL));
  }

  std::error_code ec;

  create_socket(ec);
  if (ec) {
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
IPCSocketServer::wait_for_new_client(int timeout) const
{
  auto keep_polling = [&](int pfd) -> bool {
    if (!_running.load()) {
      return false;
    }
    // A value of 0 indicates that the call timed out and no file descriptors were ready
    if (pfd < 0) {
      switch (errno) {
      case EINTR:
      case EAGAIN:
        return true;
      default:
        Warning("Error while waiting %s", std::strerror(errno));
        return false;
      }
    }
    if (pfd > 0) {
      // ready.
      return false;
    }

    // timeout, try again
    return true;
  };

  struct pollfd poll_fd;
  poll_fd.fd     = _socket;
  poll_fd.events = POLLIN; // when data is ready.
  int poll_ret;
  do {
    poll_ret = poll(&poll_fd, 1, timeout);
  } while (keep_polling(poll_ret));

  if (!(poll_fd.revents & POLLIN)) {
    return false;
  }

  return true;
}

void
IPCSocketServer::run()
{
  _running.store(true);

  ts::LocalBufferWriter<32000> bw;
  while (_running) {
    // poll till socket it's ready.

    if (!this->wait_for_new_client()) {
      if (_running.load()) {
        Warning("ups, we've got an issue.");
      }
      break;
    }

    std::error_code ec;
    if (auto fd = this->accept(ec); !ec) {
      Client client{fd};

      if (auto ret = client.read_all(bw); ret) {
        // we will load the yaml node twice using the YAMLParserChecker, ok for now.
        const auto json = std::string{bw.data(), bw.size()};
        if (auto response = rpc::JsonRPCManager::instance().handle_call(json); response) {
          // seems a valid response.
          const bool success = client.write(*response);
          if (!success) {
            Debug(logTag, "Error sending the response: %s", std ::strerror(errno));
          }
        } // it was a notification.
      } else {
        Debug(logTag, "We couldn't read it all");
      }
    } else {
      Debug(logTag, "Something happened %s", ec.message().c_str());
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
  for (int retries = 0; retries < _conf.maxRetriesOnTransientErrors; retries++) {
    _socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (_socket >= 0) {
      break;
    }
    if (!check_for_transient_errors()) {
      ec = std::make_error_code(static_cast<std::errc>(errno));
      break;
    }
  }

  if (_socket < 0) {
    // seems that we have reched the max retries.
    ec = InternalError::MAX_TRANSIENT_ERRORS_HANDLED;
  }
}

bool
IPCSocketServer::check_for_transient_errors() const
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
  if (ret != 0) {
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
  if (::listen(_socket, _conf.backlog) != 0) {
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

namespace detail
{
  template <typename T> struct MessageParser {
    static bool
    is_complete(std::string const &str)
    {
      return false;
    }
  };

  template <> struct MessageParser<rpc::comm::IPCSocketServer::Client::yamlparser> {
    static bool
    is_complete(std::string const &data)
    {
      try {
        [[maybe_unused]] auto const &node = YAML::Load(data);
        // TODO: if we follow this approach, keep in mind we can re-use the already parsed node. Using a lightweigh json SM is
        // another option.
      } catch (std::exception const &ex) {
        return false;
      }
      return true;
    }
  };
} // namespace detail

IPCSocketServer::Client::Client(int fd) : _fd{fd} {}
IPCSocketServer::Client::~Client()
{
  this->close();
}

bool
IPCSocketServer::Client::wait_for_data(int timeout) const
{
  auto keep_polling = [&](int pfd) -> bool {
    if (pfd < 0) {
      switch (errno) {
      case EINTR:
      case EAGAIN:
        return true;
      default:
        return false;
      }
    } else if (pfd > 0) {
      // TODO : merge this together
      // done waiting, data is ready.
      return false;
    } else {
      return false /*timeout*/;
    }
    // return timeout > 0 ? true : false /*timeout*/;
  };

  struct pollfd poll_fd;
  poll_fd.fd     = this->_fd;
  poll_fd.events = POLLIN; // when data is ready.
  int poll_ret;
  do {
    poll_ret = poll(&poll_fd, 1, timeout);
  } while (keep_polling(poll_ret));

  if (!(poll_fd.revents & POLLIN)) {
    return false;
  }

  return true;
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

bool
IPCSocketServer::Client::read_all(ts::FixedBufferWriter &bw) const
{
  if (!this->wait_for_data()) {
    return false;
  }

  using MsgParser = detail::MessageParser<MessageParserLogic>;
  while (bw.remaining() > 0) {
    const ssize_t ret = this->read({bw.auxBuffer(), bw.remaining()});

    if (ret > 0) {
      // already read.
      bw.fill(ret);
      std::string msg{bw.data(), bw.size()}; // TODO: improve. make the parser to work with a bw.
      if (!MsgParser::is_complete(msg)) {
        // we need more data, we check the buffer if we have space.
        if (bw.remaining() > 0) {
          // if so, then we will read again, there is a false positive scenario where it could be that the
          // json is invalid and the message is done, so, we will try again.
          if (!this->wait_for_data()) {
            Debug(logTag, "Timeout when reading again.");
            // no more data in the wire.
            // We will let the parser to respond with an invalid json if it's the case.
            return true;
          }
          // it could be legit, keep reading.
          continue;
        } else {
          // message buffer is full and we are flag to keep reading, throw this request away.
          Debug(logTag, "Buffer is full: %ld", bw.size());
          break;
        }
      } else {
        // all seems ok, we have a valid message, so we return true;
        return true;
      }
    } else {
      if (bw.size()) {
        // seems that we read some data, but there is no more available.
        // let the rpc json parser fail?
        // TODO: think on this scenario for a bit.
        Note("Data was read, but seems not good.");
      }
      break;
    }
  }

  // we fail.
  return false;
}

bool
IPCSocketServer::Client::write(std::string const &data) const
{
  // TODO: broken pipe if client closes the socket. Add a test
  if (::write(_fd, data.c_str(), data.size()) < 0) {
    Debug(logTag, "Error sending the response: %s", std ::strerror(errno));
    return false;
  }

  return true;
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
