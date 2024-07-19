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

#pragma once

#include "tscore/ink_apidefs.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_sock.h"

#include <cstdint>

#define NO_SOCK -1

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK O_NONBLOCK
#endif

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC O_CLOEXEC
#endif

#ifndef MSG_FASTOPEN
#if defined(__linux__)
#define MSG_FASTOPEN 0x20000000
#else
#define MSG_FASTOPEN 0
#endif
#endif

bool transient_error();

class UnixSocket
{
public:
  UnixSocket(int fd);

  /** Get a new socket.
   *
   * Call is_ok() to determine whether this call succeeded. If the call
   * failed, errno will be set to indicate the error.
   *
   * @see s_ok
   */
  UnixSocket(int domain, int ctype, int protocol);

  int get_fd() const;

  bool is_ok() const;

  int set_nonblocking();

  int bind(struct sockaddr const *name, int namelen);
  int accept4(struct sockaddr *addr, socklen_t *addrlen, int flags) const;
  int connect(struct sockaddr const *addr, socklen_t addrlen);

  std::int64_t read(void *buf, int size) const;

  int recv(void *buf, int size, int flags) const;
  int recvfrom(void *buf, int size, int flags, struct sockaddr *addr, socklen_t *addrlen) const;
  int recvmsg(struct msghdr *m, int flags) const;
#ifdef HAVE_RECVMMSG
  int recvmmsg(struct mmsghdr *msgvec, int vlen, int flags, struct timespec *timeout) const;
#endif

  std::int64_t write(void *buf, int size) const;

  int send(void *buf, int size, int flags) const;
  int sendto(void *buf, int size, int flags, struct sockaddr const *to, int tolen) const;
  int sendmsg(struct msghdr const *m, int flags) const;

  static int poll(struct pollfd *fds, unsigned long nfds, int timeout);

  int getsockname(struct sockaddr *sa, socklen_t *sz) const;

  int get_sndbuf_size() const;
  int get_rcvbuf_size() const;
  int set_sndbuf_size(int bsz);
  int set_rcvbuf_size(int bsz);

  int enable_option(int level, int optname);

  int close();
  int shutdown(int how);

  static bool client_fastopen_supported();

private:
  int fd{NO_SOCK};
};

inline UnixSocket::UnixSocket(int fd) : fd{fd} {}

inline UnixSocket::UnixSocket(int domain, int type, int protocol)
{
  this->fd = socket(domain, type, protocol);
}

inline int
UnixSocket::get_fd() const
{
  return this->fd;
}

inline bool
UnixSocket::is_ok() const
{
  return NO_SOCK != this->fd;
}

inline std::int64_t
UnixSocket::read(void *buf, int size) const
{
  std::int64_t r;
  do {
    r = ::read(this->fd, buf, size);
    if (likely(r >= 0)) {
      break;
    }
    r = -errno;
  } while (r == -EINTR);
  return r;
}

inline int
UnixSocket::recv(void *buf, int size, int flags) const
{
  int r;
  do {
    if (unlikely((r = ::recv(this->fd, static_cast<char *>(buf), size, flags)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

inline int
UnixSocket::recvfrom(void *buf, int size, int flags, struct sockaddr *addr, socklen_t *addrlen) const
{
  int r;
  do {
    r = ::recvfrom(this->fd, static_cast<char *>(buf), size, flags, addr, addrlen);
    if (unlikely(r < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

inline int
UnixSocket::recvmsg(struct msghdr *m, int flags) const
{
  int r;
  do {
    if (unlikely((r = ::recvmsg(this->fd, m, flags)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

#ifdef HAVE_RECVMMSG
inline int
UnixSocket::recvmmsg(struct mmsghdr *msgvec, int vlen, int flags, struct timespec *timeout) const
{
  int r;
  do {
    if (unlikely((r = ::recvmmsg(this->fd, msgvec, vlen, flags, timeout)) < 0)) {
      r = -errno;
      // EINVAL can ocur if timeout is invalid.
    }
  } while (r == -EINTR);
  return r;
}
#endif

inline std::int64_t
UnixSocket::write(void *buf, int size) const
{
  std::int64_t r;
  do {
    if (likely((r = ::write(this->fd, buf, size)) >= 0)) {
      break;
    }
    r = -errno;
  } while (r == -EINTR);
  return r;
}

inline int
UnixSocket::send(void *buf, int size, int flags) const
{
  int r;
  do {
    if (unlikely((r = ::send(this->fd, static_cast<char *>(buf), size, flags)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

inline int
UnixSocket::sendto(void *buf, int len, int flags, struct sockaddr const *to, int tolen) const
{
  int r;
  do {
    if (unlikely((r = ::sendto(this->fd, (char *)buf, len, flags, to, tolen)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

inline int
UnixSocket::sendmsg(struct msghdr const *m, int flags) const
{
  int r;
  do {
    if (unlikely((r = ::sendmsg(this->fd, m, flags)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

inline int
UnixSocket::poll(struct pollfd *fds, unsigned long nfds, int timeout)
{
  int r;
  do {
    if ((r = ::poll(fds, nfds, timeout)) >= 0) {
      break;
    }
    r = -errno;
  } while (transient_error());
  return r;
}

inline int
UnixSocket::getsockname(struct sockaddr *sa, socklen_t *sz) const
{
  return ::getsockname(this->fd, sa, sz);
}

inline int
UnixSocket::get_sndbuf_size() const
{
  int bsz = 0;
  int bszsz, r;

  bszsz = sizeof(bsz);
  r     = safe_getsockopt(this->fd, SOL_SOCKET, SO_SNDBUF, (char *)&bsz, &bszsz);
  return (r == 0 ? bsz : r);
}

inline int
UnixSocket::get_rcvbuf_size() const
{
  int bsz = 0;
  int bszsz, r;

  bszsz = sizeof(bsz);
  r     = safe_getsockopt(this->fd, SOL_SOCKET, SO_RCVBUF, (char *)&bsz, &bszsz);
  return (r == 0 ? bsz : r);
}

inline int
UnixSocket::set_sndbuf_size(int bsz)
{
  return safe_setsockopt(this->fd, SOL_SOCKET, SO_SNDBUF, (char *)&bsz, sizeof(bsz));
}

inline int
UnixSocket::set_rcvbuf_size(int bsz)
{
  return safe_setsockopt(this->fd, SOL_SOCKET, SO_RCVBUF, (char *)&bsz, sizeof(bsz));
}

inline int
UnixSocket::shutdown(int how)
{
  int res;
  do {
    if (unlikely((res = ::shutdown(this->fd, how)) < 0)) {
      res = -errno;
    }
  } while (res == -EINTR);
  return res;
}

inline bool
transient_error()
{
  bool transient = (errno == EINTR);
#ifdef ENOMEM
  transient = transient || (errno == ENOMEM);
#endif
#ifdef ENOBUFS
  transient = transient || (errno == ENOBUFS);
#endif
  return transient;
}
