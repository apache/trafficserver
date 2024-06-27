/** @file

  A brief file description

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

/****************************************************************************

  UnixSocketManager.h

  Handle the allocation of the socket descriptor (fd) resource.


****************************************************************************/
#pragma once

#include "UnixSocket.h"

#include "tscore/ink_platform.h"
#include "tscore/ink_sock.h"
#include "iocore/eventsystem/SocketManager.h"

//
// These limits are currently disabled
//
// 1024 - stdin, stderr, stdout
#define EPOLL_MAX_DESCRIPTOR_SIZE 32768

TS_INLINE int
SocketManager::open(const char *path, int oflag, mode_t mode)
{
  int s;
  do {
    s = ::open(path, oflag, mode);
    if (likely(s >= 0)) {
      break;
    }
    s = -errno;
  } while (transient_error());
  return s;
}

TS_INLINE int64_t
SocketManager::read(int fd, void *buf, int size, void * /* pOLP ATS_UNUSED */)
{
  UnixSocket sock{fd};
  return sock.read(buf, size);
}

TS_INLINE int
SocketManager::recv(int fd, void *buf, int size, int flags)
{
  UnixSocket sock{fd};
  return sock.recv(buf, size, flags);
}

TS_INLINE int
SocketManager::recvfrom(int fd, void *buf, int size, int flags, struct sockaddr *addr, socklen_t *addrlen)
{
  UnixSocket sock{fd};
  return sock.recvfrom(buf, size, flags, addr, addrlen);
}

TS_INLINE int
SocketManager::recvmsg(int fd, struct msghdr *m, int flags, void * /* pOLP ATS_UNUSED */)
{
  UnixSocket sock{fd};
  return sock.recvmsg(m, flags);
}

#ifdef HAVE_RECVMMSG
TS_INLINE int
SocketManager::recvmmsg(int fd, struct mmsghdr *msgvec, int vlen, int flags, struct timespec *timeout, void * /* pOLP ATS_UNUSED */)
{
  UnixSocket sock{fd};
  return sock.recvmmsg(msgvec, vlen, flags, timeout);
}
#endif

TS_INLINE int64_t
SocketManager::write(int fd, void *buf, int size, void * /* pOLP ATS_UNUSED */)
{
  UnixSocket sock{fd};
  return sock.write(buf, size);
}

TS_INLINE int64_t
SocketManager::pwrite(int fd, void *buf, int size, off_t offset, char * /* tag ATS_UNUSED */)
{
  int64_t r;
  do {
    if (unlikely((r = ::pwrite(fd, buf, size, offset)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

TS_INLINE int
SocketManager::send(int fd, void *buf, int size, int flags)
{
  UnixSocket sock{fd};
  return sock.send(buf, size, flags);
}

TS_INLINE int
SocketManager::sendto(int fd, void *buf, int len, int flags, struct sockaddr const *to, int tolen)
{
  UnixSocket sock{fd};
  return sock.sendto(buf, len, flags, to, tolen);
}

TS_INLINE int
SocketManager::sendmsg(int fd, struct msghdr *m, int flags, void * /* pOLP ATS_UNUSED */)
{
  UnixSocket sock{fd};
  return sock.sendmsg(m, flags);
}

TS_INLINE int64_t
SocketManager::lseek(int fd, off_t offset, int whence)
{
  int64_t r;
  do {
    if ((r = ::lseek(fd, offset, whence)) < 0) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

TS_INLINE int
SocketManager::fsync(int fildes)
{
  int r;
  do {
    if ((r = ::fsync(fildes)) < 0) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

TS_INLINE int
SocketManager::poll(struct pollfd *fds, unsigned long nfds, int timeout)
{
  return UnixSocket::poll(fds, nfds, timeout);
}

TS_INLINE int
SocketManager::get_sndbuf_size(int s)
{
  UnixSocket sock{s};
  return sock.get_sndbuf_size();
}

TS_INLINE int
SocketManager::get_rcvbuf_size(int s)
{
  UnixSocket sock{s};
  return sock.get_rcvbuf_size();
}

TS_INLINE int
SocketManager::set_sndbuf_size(int s, int bsz)
{
  UnixSocket sock{s};
  return sock.set_sndbuf_size(bsz);
}

TS_INLINE int
SocketManager::set_rcvbuf_size(int s, int bsz)
{
  UnixSocket sock{s};
  return sock.set_rcvbuf_size(bsz);
}

TS_INLINE int
SocketManager::getsockname(int s, struct sockaddr *sa, socklen_t *sz)
{
  UnixSocket sock{s};
  return sock.getsockname(sa, sz);
}

TS_INLINE int
SocketManager::socket(int domain, int type, int protocol)
{
  return ::socket(domain, type, protocol);
}

TS_INLINE int
SocketManager::shutdown(int s, int how)
{
  UnixSocket sock{s};
  return sock.shutdown(how);
}
