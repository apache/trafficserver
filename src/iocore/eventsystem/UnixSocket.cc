/** @file

  Provides a wrapper for a Unix socket.

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

#include "iocore/eventsystem/UnixSocket.h"

#include "tscore/ink_apidefs.h"
#include "tscore/ink_config.h"
#include "tscore/TextBuffer.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_sock.h"
#include "tscore/Diags.h"
#if TS_USE_NUMA
#include <numa.h>
#endif

#include <cstdlib>

namespace
{
enum class TCPFastopenMask {
  CLIENT_ENABLE = 1,
  SERVER_ENABLE,
  CLIENT_NOCOOKIE,
  SERVER_NOCOOKIE,
  SERVER_IMPLICIT_ENABLE,

  MAX_VALUE,
};
} // end anonymous namespace

#if !HAVE_ACCEPT4
static int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
#endif
static unsigned int read_uint_from_fd(int fd);

int
UnixSocket::set_nonblocking()
{
  return safe_set_fl(this->fd, O_NONBLOCK);
}

int
UnixSocket::bind(struct sockaddr const *name, int namelen)
{
  return safe_bind(this->fd, name, namelen);
}

int
UnixSocket::accept4(struct sockaddr *addr, socklen_t *addrlen, int flags) const
{
  do {
    int fd = ::accept4(this->fd, addr, addrlen, flags);
    if (likely(fd >= 0)) {
#if TS_ENABLE_NUMA_DEBUG
      unsigned thread_cpu, thread_numa;
      getcpu(&thread_cpu, &thread_numa);

      unsigned socket_cpu     = UINT_MAX;
      int      socket_cpu_len = sizeof(socket_cpu);

      int ret = safe_getsockopt(fd, SOL_SOCKET, SO_INCOMING_CPU, &socket_cpu, &socket_cpu_len);
      if (ret != 0) {
        Debug("numa", "Unable to get SO_INCOMING_CPU");
      } else {
        std::string status;
        if (thread_numa == numa_node_of_cpu(socket_cpu)) {
          status = "OK";
        } else {
          status = "NOT OK";
        }
        Debug("numa", "NUMA thread affinity for Thread id: %d, Thread cpu: %d, Thread numa: %d, \
                Socket cpu: %d, Socket numa: %d, %s",
              gettid(), thread_cpu, thread_numa, socket_cpu, numa_node_of_cpu(socket_cpu), status.c_str());
      }
#endif
      return fd;
    }
  } while (transient_error());

  return -errno;
}

#if !HAVE_ACCEPT4
static int
accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
  int fd, err;

  do {
    fd = accept(sockfd, addr, addrlen);
    if (likely(fd >= 0))
      break;
  } while (transient_error());

  if ((fd >= 0) && (flags & SOCK_CLOEXEC) && (safe_fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)) {
    err = errno;
    close(fd);
    errno = err;
    return -1;
  }

  if ((fd >= 0) && (flags & SOCK_NONBLOCK) && (safe_nonblocking(fd) < 0)) {
    err = errno;
    close(fd);
    errno = err;
    return -1;
  }

  return fd;
}
#endif // !HAVE_ACCEPT4

int
UnixSocket::connect(struct sockaddr const *addr, socklen_t addrlen)
{
  return ::connect(this->fd, addr, addrlen);
}

int
UnixSocket::enable_option(int level, int optname)
{
  int on = 1;
  return safe_setsockopt(this->fd, level, optname, &on, sizeof(on));
}

int
UnixSocket::close()
{
  int res{};

  if (this->fd == 0) {
    return -EACCES;
  } else if (this->fd < 0) {
    return -EINVAL;
  }

  do {
    res = ::close(this->fd);
    if (res == -1) {
      res = -errno;
    } else {
      this->fd = NO_SOCK;
    }
  } while (res == -EINTR);

  return res;
}

bool
UnixSocket::client_fastopen_supported()
{
  ats_scoped_fd fd{::open("/proc/sys/net/ipv4/tcp_fastopen", O_RDONLY)};
  unsigned int  bitfield{read_uint_from_fd(fd.get())};
  return bitfield & static_cast<unsigned int>(TCPFastopenMask::CLIENT_ENABLE);
}

static unsigned int
read_uint_from_fd(int fd)
{
  int result{};
  if (fd) {
    TextBuffer buffer(16);
    buffer.slurp(fd);
    result = std::atoi(buffer.bufPtr());
  }
  return static_cast<unsigned int>(result);
}
