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

#include "tscore/ink_platform.h"
#include "tscore/ink_sock.h"
#include "I_SocketManager.h"

//
// These limits are currently disabled
//
// 1024 - stdin, stderr, stdout
#define EPOLL_MAX_DESCRIPTOR_SIZE 32768

TS_INLINE bool
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
  int64_t r;
  do {
    r = ::read(fd, buf, size);
    if (likely(r >= 0)) {
      break;
    }
    r = -errno;
  } while (r == -EINTR);
  return r;
}

TS_INLINE int64_t
SocketManager::pread(int fd, void *buf, int size, off_t offset, char * /* tag ATS_UNUSED */)
{
  int64_t r;
  do {
    r = ::pread(fd, buf, size, offset);
    if (r < 0) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

TS_INLINE int64_t
SocketManager::readv(int fd, struct iovec *vector, size_t count)
{
  int64_t r;
  do {
    // coverity[tainted_data_argument]
    if (likely((r = ::readv(fd, vector, count)) >= 0)) {
      break;
    }
    r = -errno;
  } while (transient_error());
  return r;
}

TS_INLINE int64_t
SocketManager::vector_io(int fd, struct iovec *vector, size_t count, int read_request, void * /* pOLP ATS_UNUSED */)
{
  const int max_iovecs_per_request = 16;
  int n;
  int64_t r = 0;
  int n_vec;
  int64_t bytes_xfered = 0;
  int current_count;
  int64_t current_request_bytes;

  for (n_vec = 0; n_vec < (int)count; n_vec += max_iovecs_per_request) {
    current_count = std::min(max_iovecs_per_request, ((int)(count - n_vec)));
    do {
      // coverity[tainted_data_argument]
      r = read_request ? ::readv(fd, &vector[n_vec], current_count) : ::writev(fd, &vector[n_vec], current_count);
      if (likely(r >= 0)) {
        break;
      }
      r = -errno;
    } while (transient_error());

    if (r <= 0) {
      return (bytes_xfered && (r == -EAGAIN)) ? bytes_xfered : r;
    }
    bytes_xfered += r;

    if ((n_vec + max_iovecs_per_request) >= (int)count) {
      break;
    }

    // Compute bytes in current vector
    current_request_bytes = 0;
    for (n = n_vec; n < (n_vec + current_count); ++n) {
      current_request_bytes += vector[n].iov_len;
    }

    // Exit if we were unable to read all data in the current vector
    if (r != current_request_bytes) {
      break;
    }
  }
  return bytes_xfered;
}

TS_INLINE int64_t
SocketManager::read_vector(int fd, struct iovec *vector, size_t count, void *pOLP)
{
  return vector_io(fd, vector, count, 1, pOLP);
}

TS_INLINE int
SocketManager::recv(int fd, void *buf, int size, int flags)
{
  int r;
  do {
    if (unlikely((r = ::recv(fd, (char *)buf, size, flags)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

TS_INLINE int
SocketManager::recvfrom(int fd, void *buf, int size, int flags, struct sockaddr *addr, socklen_t *addrlen)
{
  int r;
  do {
    r = ::recvfrom(fd, (char *)buf, size, flags, addr, addrlen);
    if (unlikely(r < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

TS_INLINE int
SocketManager::recvmsg(int fd, struct msghdr *m, int flags, void * /* pOLP ATS_UNUSED */)
{
  int r;
  do {
    if (unlikely((r = ::recvmsg(fd, m, flags)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

TS_INLINE int64_t
SocketManager::write(int fd, void *buf, int size, void * /* pOLP ATS_UNUSED */)
{
  int64_t r;
  do {
    if (likely((r = ::write(fd, buf, size)) >= 0)) {
      break;
    }
    r = -errno;
  } while (r == -EINTR);
  return r;
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

TS_INLINE int64_t
SocketManager::writev(int fd, struct iovec *vector, size_t count)
{
  int64_t r;
  do {
    if (likely((r = ::writev(fd, vector, count)) >= 0)) {
      break;
    }
    r = -errno;
  } while (transient_error());
  return r;
}

TS_INLINE int64_t
SocketManager::write_vector(int fd, struct iovec *vector, size_t count, void *pOLP)
{
  return vector_io(fd, vector, count, 0, pOLP);
}

TS_INLINE int
SocketManager::send(int fd, void *buf, int size, int flags)
{
  int r;
  do {
    if (unlikely((r = ::send(fd, (char *)buf, size, flags)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

TS_INLINE int
SocketManager::sendto(int fd, void *buf, int len, int flags, struct sockaddr const *to, int tolen)
{
  int r;
  do {
    if (unlikely((r = ::sendto(fd, (char *)buf, len, flags, to, tolen)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

TS_INLINE int
SocketManager::sendmsg(int fd, struct msghdr *m, int flags, void * /* pOLP ATS_UNUSED */)
{
  int r;
  do {
    if (unlikely((r = ::sendmsg(fd, m, flags)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
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
SocketManager::fstat(int fd, struct stat *buf)
{
  int r;
  do {
    if ((r = ::fstat(fd, buf)) >= 0) {
      break;
    }
    r = -errno;
  } while (transient_error());
  return r;
}

TS_INLINE int
SocketManager::unlink(char *buf)
{
  int r;
  do {
    if ((r = ::unlink(buf)) < 0) {
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
SocketManager::ftruncate(int fildes, off_t length)
{
  int r;
  do {
    if ((r = ::ftruncate(fildes, length)) < 0) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}

TS_INLINE int
SocketManager::poll(struct pollfd *fds, unsigned long nfds, int timeout)
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

#if TS_USE_EPOLL
TS_INLINE int
SocketManager::epoll_create(int size)
{
  int r;
  if (size <= 0) {
    size = EPOLL_MAX_DESCRIPTOR_SIZE;
  }
  do {
    if (likely((r = ::epoll_create(size)) >= 0)) {
      break;
    }
    r = -errno;
  } while (errno == -EINTR);
  return r;
}

TS_INLINE int
SocketManager::epoll_close(int epfd)
{
  int r = 0;
  if (likely(epfd >= 0)) {
    do {
      if (likely((r = ::close(epfd)) == 0)) {
        break;
      }
      r = -errno;
    } while (errno == -EINTR);
  }
  return r;
}

TS_INLINE int
SocketManager::epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
  int r;
  do {
    if (likely((r = ::epoll_ctl(epfd, op, fd, event)) == 0)) {
      break;
    }
    r = -errno;
  } while (errno == -EINTR);
  return r;
}

TS_INLINE int
SocketManager::epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
  int r;
  do {
    if ((r = ::epoll_wait(epfd, events, maxevents, timeout)) >= 0) {
      break;
    }
    r = -errno;
  } while (errno == -EINTR);
  return r;
}

#endif /* TS_USE_EPOLL */

#if TS_USE_KQUEUE
TS_INLINE int
SocketManager::kqueue()
{
  return ::kqueue();
}

TS_INLINE int
SocketManager::kevent(int kq, const struct kevent *changelist, int nchanges, struct kevent *eventlist, int nevents,
                      const struct timespec *timeout)
{
  int r;
  do {
    r = ::kevent(kq, changelist, nchanges, eventlist, nevents, timeout);
    if (likely(r >= 0)) {
      break;
    }
    r = -errno;
  } while (errno == -EINTR);
  return r;
}
#endif /* TS_USE_KQUEUE */

#if TS_USE_PORT
TS_INLINE int
SocketManager::port_create()
{
  return ::port_create();
}

TS_INLINE int
SocketManager::port_associate(int port, int source, uintptr_t obj, int events, void *user)
{
  int r;
  r = ::port_associate(port, source, obj, events, user);
  if (r < 0)
    r = -errno;
  return r;
}

TS_INLINE int
SocketManager::port_dissociate(int port, int source, uintptr_t obj)
{
  int r;
  r = ::port_dissociate(port, source, obj);
  if (r < 0)
    r = -errno;
  return r;
}

TS_INLINE int
SocketManager::port_getn(int port, port_event_t *list, uint_t max, uint_t *nget, timespec_t *timeout)
{
  int r;
  do {
    if ((r = ::port_getn(port, list, max, nget, timeout)) >= 0)
      break;
    r = -errno;
  } while (errno == -EINTR); // TODO: possible EAGAIN(undocumented)
  return r;
}
#endif /* TS_USE_PORT */

TS_INLINE int
SocketManager::get_sndbuf_size(int s)
{
  int bsz = 0;
  int bszsz, r;

  bszsz = sizeof(bsz);
  r     = safe_getsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&bsz, &bszsz);
  return (r == 0 ? bsz : r);
}

TS_INLINE int
SocketManager::get_rcvbuf_size(int s)
{
  int bsz = 0;
  int bszsz, r;

  bszsz = sizeof(bsz);
  r     = safe_getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)&bsz, &bszsz);
  return (r == 0 ? bsz : r);
}

TS_INLINE int
SocketManager::set_sndbuf_size(int s, int bsz)
{
  return safe_setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&bsz, sizeof(bsz));
}

TS_INLINE int
SocketManager::set_rcvbuf_size(int s, int bsz)
{
  return safe_setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)&bsz, sizeof(bsz));
}

TS_INLINE int
SocketManager::getsockname(int s, struct sockaddr *sa, socklen_t *sz)
{
  return ::getsockname(s, sa, sz);
}

TS_INLINE int
SocketManager::socket(int domain, int type, int protocol)
{
  return ::socket(domain, type, protocol);
}

TS_INLINE int
SocketManager::shutdown(int s, int how)
{
  int res;
  do {
    if (unlikely((res = ::shutdown(s, how)) < 0)) {
      res = -errno;
    }
  } while (res == -EINTR);
  return res;
}

TS_INLINE int
SocketManager::lockf(int s, int f, off_t size)
{
  int res;
  do {
    if ((res = ::lockf(s, f, size)) < 0) {
      res = -errno;
    }
  } while (res == -EINTR);
  return res;
}

TS_INLINE int
SocketManager::dup(int s)
{
  int res;
  do {
    if ((res = ::dup(s)) >= 0) {
      break;
    }
    res = -errno;
  } while (res == -EINTR);
  return res;
}
