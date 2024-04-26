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

  SocketManager.h

  Handle the allocation of the socket descriptor (fd) resource.


 ****************************************************************************/

#pragma once

#include "tscore/ink_platform.h"

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

#define SOCKET int

/** Utility namespace for socket operations.
 */
namespace SocketManager
{
// Test whether TCP Fast Open is supported on this host.
bool fastopen_supported();

// result is the socket or -errno
SOCKET socket(int domain = AF_INET, int type = SOCK_STREAM, int protocol = 0);

const mode_t DEFAULT_OPEN_MODE{0644};

// result is the fd or -errno
int open(const char *path, int oflag = O_RDWR | O_NDELAY | O_CREAT, mode_t mode = DEFAULT_OPEN_MODE);

// result is the number of bytes or -errno
int64_t read(int fd, void *buf, int len, void *pOLP = nullptr);

int recv(int s, void *buf, int len, int flags);
int recvfrom(int fd, void *buf, int size, int flags, struct sockaddr *addr, socklen_t *addrlen);
int recvmsg(int fd, struct msghdr *m, int flags, void *pOLP = nullptr);

int64_t write(int fd, void *buf, int len, void *pOLP = nullptr);
int64_t pwrite(int fd, void *buf, int len, off_t offset, char *tag = nullptr);

int send(int fd, void *buf, int len, int flags);
int sendto(int fd, void *buf, int len, int flags, struct sockaddr const *to, int tolen);
int sendmsg(int fd, struct msghdr *m, int flags, void *pOLP = nullptr);
#ifdef HAVE_RECVMMSG
int recvmmsg(int fd, struct mmsghdr *msgvec, int vlen, int flags, struct timespec *timeout, void *pOLP = nullptr);
#endif
int64_t lseek(int fd, off_t offset, int whence);
int     fsync(int fildes);
int     poll(struct pollfd *fds, unsigned long nfds, int timeout);

int shutdown(int s, int how);

// result is the fd or -errno
int accept4(int s, struct sockaddr *addr, socklen_t *addrlen, int flags);

// manipulate socket buffers
int get_sndbuf_size(int s);
int get_rcvbuf_size(int s);
int set_sndbuf_size(int s, int size);
int set_rcvbuf_size(int s, int size);

int getsockname(int s, struct sockaddr *, socklen_t *);

/** Close the socket.
    @return 0 if successful, -errno on error.
 */
int close(int sock);
int ink_bind(int s, struct sockaddr const *name, int namelen, short protocol = 0);
} // namespace SocketManager
