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
#ifndef _P_UnixSocketManager_h_
#define _P_UnixSocketManager_h_
#include "I_SocketManager.h"

extern int monitor_read_activity;
extern int monitor_write_activity;


//
// These limits are currently disabled
//
// 1024 - stdin, stderr, stdout
#define EPOLL_MAX_DESCRIPTOR_SIZE                32768

INK_INLINE bool
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


//
// Timing done in the connectionManager
//
INK_INLINE int
SocketManager::accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
  int r;

  do {
    r =::accept(s, addr, addrlen);
    if (likely(r >= 0))
      break;
    r = -errno;
  } while (transient_error());

  return r;
}


INK_INLINE int
SocketManager::open(char *path, int oflag, mode_t mode)
{
  int s;

  do {
    s =::open(path, oflag, mode);
    if (likely(s >= 0))
      break;
    s = -errno;
  } while (transient_error());
  return s;
}


INK_INLINE int
SocketManager::read(int fd, void *buf, int size, void *pOLP)
{
  int r = 0;

  do {
    r =::read(fd, buf, size);
    if (likely(r >= 0))
      break;
    r = -errno;
  } while (r == -EINTR);
  return r;
}


void monitor_disk_read(int fd, void *buf, int size, off_t offset, char *tag);

INK_INLINE int
SocketManager::pread(int fd, void *buf, int size, off_t offset, char *tag)
{
  if (monitor_read_activity)
    monitor_disk_read(fd, buf, size, offset, tag);
  int r = 0;
  do {
    r =::pread(fd, buf, size, offset);
    if (r < 0)
      r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::read_from_middle_of_file(int fd, void *buf, int size, off_t offset, char *tag)
{
  int r;
  if (monitor_read_activity)
    monitor_disk_read(fd, buf, size, offset, tag);
  do {
    if ((r =::read_from_middle_of_file(fd, buf, size, offset)) >= 0)
      break;
    r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::readv(int fd, struct iovec *vector, size_t count, teFDType eT)
{
  int r;

  do {
    // coverity[tainted_data_argument]
    if (likely((r =::readv(fd, vector, count)) >= 0))
      break;
    r = -errno;
  } while (transient_error());
  return r;
}


INK_INLINE int
SocketManager::vector_io(int fd, struct iovec *vector, size_t count, int read_request, void *pOLP)
{
  const int max_iovecs_per_request = 16;
  int n;
  int r = 0;
  int n_vec;
  int bytes_xfered = 0;
  int current_count;
  int current_request_bytes;

  for (n_vec = 0; n_vec < (int) count; n_vec += max_iovecs_per_request) {
    current_count = min(max_iovecs_per_request, ((int) (count - n_vec)));
    do {
      // coverity[tainted_data_argument]
      r = read_request ? ::readv(fd, &vector[n_vec], current_count) : ::writev(fd, &vector[n_vec], current_count);
      if (likely(r >= 0))
        break;
      r = -errno;
    } while (transient_error());

    if (r <= 0) {
      return (bytes_xfered && (r == -EAGAIN)) ? bytes_xfered : r;
    }
    bytes_xfered += r;

    if ((n_vec + max_iovecs_per_request) >= (int) count)
      break;

    // Compute bytes in current vector
    current_request_bytes = 0;
    for (n = n_vec; n < (n_vec + current_count); ++n)
      current_request_bytes += vector[n].iov_len;

    // Exit if we were unable to read all data in the current vector
    if (r != current_request_bytes)
      break;
  }
  return bytes_xfered;
}


INK_INLINE int
SocketManager::read_vector(int fd, struct iovec *vector, size_t count, void *pOLP)
{
  return vector_io(fd, vector, count, 1, pOLP);
}


INK_INLINE int
SocketManager::recv(int fd, void *buf, int size, int flags)
{
  int r;

  do {
    if (unlikely((r =::recv(fd, (char *) buf, size, flags)) < 0)) {
      r = -errno;
    }
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::recvfrom(int fd, void *buf, int size, int flags, struct sockaddr *addr, socklen_t *addrlen)
{
  int r;

  do {
    r =::recvfrom(fd, (char *) buf, size, flags, addr, addrlen);
    if (unlikely(r < 0))
      r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::write(int fd, void *buf, int size, void *pOLP)
{
  int r;

  do {
    if (likely((r =::write(fd, buf, size)) >= 0))
      break;
    r = -errno;
  } while (r == -EINTR);
  return r;
}


void monitor_disk_write(int fd, void *buf, int size, off_t offset, char *tag);

INK_INLINE int
SocketManager::pwrite(int fd, void *buf, int size, off_t offset, char *tag)
{
  int r;

  if (monitor_write_activity)
    monitor_disk_write(fd, buf, size, offset, tag);
  do {
    if (unlikely((r =::pwrite(fd, buf, size, offset)) < 0))
      r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::write_to_middle_of_file(int fd, void *buf, int size, off_t offset, char *tag)
{
  int r;

  if (monitor_write_activity)
    monitor_disk_write(fd, buf, size, offset, tag);

  do {
    if (likely((r =::write_to_middle_of_file(fd, buf, size, offset)) >= 0))
      break;
    r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::writev(int fd, struct iovec *vector, size_t count, teFDType eT)
{
  int r;

  do {
    if (likely((r =::writev(fd, vector, count)) >= 0))
      break;
    r = -errno;
  } while (transient_error());
  return r;
}


INK_INLINE int
SocketManager::write_vector(int fd, struct iovec *vector, size_t count, void *pOLP)
{
  return vector_io(fd, vector, count, 0, pOLP);
}


INK_INLINE int
SocketManager::send(int fd, void *buf, int size, int flags)
{
  int r;

  do {
    if (unlikely((r =::send(fd, (char *) buf, size, flags)) < 0))
      r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::sendto(int fd, void *buf, int len, int flags, struct sockaddr *to, int tolen)
{
  int r;

  do {
    if (unlikely((r =::sendto(fd, (char *) buf, len, flags, to, tolen)) < 0))
      r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::sendmsg(int fd, struct msghdr *m, int flags, void *pOLP)
{
  int r;

  do {
    if (unlikely((r =::sendmsg(fd, m, flags)) < 0))
      r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::lseek(int fd, off_t offset, int whence)
{
  int r;

  do {
    if ((r =::lseek(fd, offset, whence)) < 0)
      r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::fstat(int fd, struct stat *buf)
{
  int r;

  do {
    if ((r =::fstat(fd, buf)) >= 0)
      break;
    r = -errno;
  } while (transient_error());
  return r;
}


INK_INLINE int
SocketManager::unlink(char *buf)
{
  int r;

  do {
    if ((r =::unlink(buf)) < 0)
      r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::fsync(int fildes)
{
  int r;

  do {
    if ((r =::fsync(fildes)) < 0)
      r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::ftruncate(int fildes, off_t length)
{
  int r;

  do {
    if ((r =::ftruncate(fildes, length)) < 0)
      r = -errno;
  } while (r == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::poll(struct pollfd *fds, unsigned long nfds, int timeout)
{
  int r;

  do {
    if ((r =::poll(fds, nfds, timeout)) >= 0)
      break;
    r = -errno;
  } while (transient_error());
  return r;
}

#if defined(USE_EPOLL)
INK_INLINE int
SocketManager::epoll_create(int size)
{
  int r;

  if (size <= 0)
    size = EPOLL_MAX_DESCRIPTOR_SIZE;
  do {
    if (likely((r =::epoll_create(size)) >= 0))
      break;
    r = -errno;
  } while (errno == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::epoll_close(int epfd)
{
  int r = 0;

  if (likely(epfd >= 0)) {
    do {
      if (likely((r =::close(epfd)) == 0))
        break;
      r = -errno;
    } while (errno == -EINTR);
  }
  return r;
}


INK_INLINE int
SocketManager::epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
  int r;

  do {
    if (likely((r =::epoll_ctl(epfd, op, fd, event)) == 0))
      break;
    r = -errno;
  } while (errno == -EINTR);
  return r;
}


INK_INLINE int
SocketManager::epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
  int r;

  do {
    if ((r =::epoll_wait(epfd, events, maxevents, timeout)) >= 0)
      break;
    r = -errno;
  } while (errno == -EINTR);
  return r;
}

#elif defined(USE_KQUEUE)
INK_INLINE int
SocketManager::kqueue()
{
  return ::kqueue();
}


INK_INLINE int
SocketManager::kevent(int kq, const struct kevent *changelist, int nchanges,
                      struct kevent *eventlist, int nevents,
                      const struct timespec *timeout)
{
  int r;

  do {
    r =::kevent(kq, changelist, nchanges,
                eventlist, nevents, timeout);
    if (likely(r >= 0)) {
        break;
    }
    r = -errno;
  } while (errno == -EINTR);

  return r;
}

#endif


INK_INLINE int
SocketManager::get_sndbuf_size(int s)
{
  int bsz = 0;
  int bszsz, r;

  bszsz = sizeof(bsz);
  r = safe_getsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *) &bsz, &bszsz);
  return (r == 0 ? bsz : r);
}


INK_INLINE int
SocketManager::get_rcvbuf_size(int s)
{
  int bsz = 0;
  int bszsz, r;

  bszsz = sizeof(bsz);
  r = safe_getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *) &bsz, &bszsz);
  return (r == 0 ? bsz : r);
}


INK_INLINE int
SocketManager::set_sndbuf_size(int s, int bsz)
{
  return safe_setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *) &bsz, sizeof(bsz));
}


INK_INLINE int
SocketManager::set_rcvbuf_size(int s, int bsz)
{
  return safe_setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *) &bsz, sizeof(bsz));
}


INK_INLINE int
SocketManager::getsockname(int s, struct sockaddr *sa, socklen_t *sz)
{
  return::getsockname(s, sa, sz);
}


INK_INLINE int
SocketManager::socket(int domain, int type, int protocol, bool bNonBlocking)
{
  return::socket(domain, type, protocol);
}


INK_INLINE int
SocketManager::mc_socket(int domain, int type, int protocol, bool bNonBlocking)
{
  return SocketManager::socket(domain, type, protocol, bNonBlocking);
}


INK_INLINE int
SocketManager::shutdown(int s, int how)
{
  int res;

  do {
    if (unlikely((res =::shutdown(s, how)) < 0))
      res = -errno;
  } while (res == -EINTR);
  return res;
}


INK_INLINE int
SocketManager::lockf(int s, int f, long size)
{
  int res;

  do {
    if ((res =::lockf(s, f, size)) < 0)
      res = -errno;
  } while (res == -EINTR);
  return res;
}


INK_INLINE int
SocketManager::dup(int s)
{
  int res;

  do {
    if ((res =::dup(s)) >= 0)
      break;
    res = -errno;
  } while (res == -EINTR);
  return res;
}


INK_INLINE int
SocketManager::fast_close(int s)
{
  int res;

  do {
    if ((res =::close(s)) >= 0)
      break;
    res = -errno;
  } while (res == -EINTR);
  return res;
}

int safe_msync(caddr_t addr, size_t len, caddr_t end, int flags);

#ifndef MADV_NORMAL
#define MADV_NORMAL 0
#endif

#ifndef MADV_RANDOM
#define MADV_RANDOM 1
#endif

#ifndef MADV_SEQUENTIAL
#define MADV_SEQUENTIAL 2
#endif

#ifndef MADV_WILLNEED
#define MADV_WILLNEED 3
#endif

#ifndef MADV_DONTNEED
#define MADV_DONTNEED 4
#endif

int safe_madvise(caddr_t addr, size_t len, caddr_t end, int flags);
int safe_mlock(caddr_t addr, size_t len, caddr_t end);

#endif /*P_UnixSocketManager_h_ */
