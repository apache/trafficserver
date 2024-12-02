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
#include "tsutil/ts_bw_format.h"
#include "records/RecProcess.h"
#include "tscore/ink_sock.h"

#include <ts/ts.h>

#include "mgmt/rpc/jsonrpc/JsonRPCManager.h"
#include "mgmt/rpc/server/IPCSocketServer.h"

namespace
{
DbgCtl dbg_ctl{"rpc.net"};

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

static bool
has_peereid()
{
#if HAVE_GETPEEREID
  return true;
#elif HAVE_GETPEERUCRED
  return true;
#elif TS_HAS_SO_PEERCRED
  return true;
#else
  return false;
#endif
}

static int
get_peereid(int fd, uid_t *euid, gid_t *egid)
{
  *euid = -1;
  *egid = -1;

#if HAVE_GETPEEREID
  return getpeereid(fd, euid, egid);
#elif HAVE_GETPEERUCRED
  ucred_t *ucred;

  if (getpeerucred(fd, &ucred) == -1) {
    return -1;
  }

  *euid = ucred_geteuid(ucred);
  *egid = ucred_getegid(ucred);
  ucred_free(ucred);
  return 0;
#elif TS_HAS_SO_PEERCRED
  struct ucred cred;
  socklen_t    credsz = sizeof(cred);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &credsz) == -1) {
    return -1;
  }

  *euid = cred.uid;
  *egid = cred.gid;
  return 0;
#else
  (void)fd;
  errno = ENOTSUP;
  return -1;
#endif
}

} // namespace

namespace rpc::comm
{
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
    Dbg(dbg_ctl, "Invalid unix path name, check the size.");
    return std::make_error_code(static_cast<std::errc>(ENAMETOOLONG));
  }

  std::error_code ec; // Flag possible errors.

  if (this->create_socket(ec); ec) {
    return ec;
  }

  Dbg(dbg_ctl, "Using %s as socket path.", _conf.sockPathName.c_str());
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

  while (_running) {
    // poll till socket it's ready.
    if (!this->poll_for_new_client()) {
      if (_running.load()) {
        Warning("ups, we've got an issue.");
      }
      break;
    }

    std::error_code ec;
    if (int fd = this->accept(ec); !ec) {
      Client client{fd, _conf.incomingRequestMaxBufferSize};
      Buffer bw;

      if (auto [ok, errStr] = client.read_all(bw); ok) {
        const std::string &json = bw.str();
        rpc::Context       ctx;
        // we want to make sure the peer's credentials are ok.
        ctx.get_auth().add_checker(
          [&](TSRPCHandlerOptions const &opt, swoc::Errata &errata) -> void { late_check_peer_credentials(fd, opt, errata); });

        if (auto response = rpc::JsonRPCManager::instance().handle_call(ctx, json); response) {
          // seems a valid response.
          if (client.write(*response, ec); ec) {
            Dbg(dbg_ctl, "Error sending the response: %s", ec.message().c_str());
          }
        } // it was a notification.
      } else {
        Dbg(dbg_ctl, "Error detected while reading: %s", errStr.c_str());
      }
    } else {
      Dbg(dbg_ctl, "Error while accepting a new connection on the socket: %s", ec.message().c_str());
    }
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
  int ret{-1};

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
    // seems that we have reached the max retries.
    ec = InternalError::MAX_TRANSIENT_ERRORS_HANDLED;
  }

  return ret;
}

void
IPCSocketServer::bind(std::error_code &ec)
{
  _lock_fd = open(_conf.lockPathName.c_str(), O_RDONLY | O_CREAT, 0600);
  if (_lock_fd == -1) {
    ec = std::make_error_code(static_cast<std::errc>(errno));
    return;
  }

  int ret = flock(_lock_fd, LOCK_EX | LOCK_NB);

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

  // If the socket is not administratively restricted, check whether we have platform
  // support. Otherwise, default to making it restricted.
  bool restricted{true};
  if (!_conf.restrictedAccessApi) {
    restricted = !has_peereid();
  }

  mode_t mode = restricted ? 00700 : 00777;
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

  if (_lock_fd > 0) {
    ::close(_lock_fd);
    _lock_fd = -1;
  }
}
//// client

IPCSocketServer::Client::Client(int fd, size_t max_req_size) : _fd{fd}, _max_req_size{max_req_size} {}
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
IPCSocketServer::Client::read(swoc::MemSpan<char> span) const
{
  return ::read(_fd, span.data(), span.size());
}

std::tuple<bool, std::string>
IPCSocketServer::Client::read_all(Buffer &bw) const
{
  std::string buff;
  while (true) {
    auto ret = read({bw.writable_data(), bw.available()});
    if (ret < 0) {
      if (check_for_transient_errors()) {
        continue;
      } else {
        return {false, swoc::bwprint(buff, "Error reading the socket: {}", swoc::bwf::Errno{})};
      }
    }

    if (ret == 0) {
      if (bw.stored()) {
        return {false, swoc::bwprint(buff, "Peer disconnected after reading {} bytes.", bw.stored())};
      }
      return {false, swoc::bwprint(buff, "Peer disconnected. EOF")};
    }
    bw.save(ret);
    if (_max_req_size - bw.stored() > 0) { // we can still read more.
      using namespace std::chrono_literals;
      if (!this->poll_for_data(1ms)) {
        return {true, buff};
      }
      continue;
    } else {
      swoc::bwprint(buff, "Buffer is full, we hit the limit: {}", _max_req_size);
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

void
IPCSocketServer::late_check_peer_credentials(int peedFd, TSRPCHandlerOptions const &options, swoc::Errata &errata) const
{
  // For privileged calls, ensure we have caller credentials and that the caller is privileged.
  auto ecode = [](UnauthorizedErrorCode c) -> std::error_code {
    return std::error_code(static_cast<unsigned>(c), std::generic_category());
  };

  if (has_peereid() && options.auth.restricted) {
    uid_t euid = -1;
    gid_t egid = -1;
    if (get_peereid(peedFd, &euid, &egid) == -1) {
      errata.assign(ecode(UnauthorizedErrorCode::PEER_CREDENTIALS_ERROR))
        .note("Error getting peer credentials: {}", swoc::bwf::Errno{});
    } else if (euid != 0 && euid != geteuid()) {
      errata.assign(ecode(UnauthorizedErrorCode::PERMISSION_DENIED))
        .note("Denied privileged API access for uid={} gid={}", euid, egid);
    }
  }
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
    if (auto n = node[config::MAX_BUFFER_SIZE]) {
      rhs.incomingRequestMaxBufferSize = n.as<size_t>();
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
