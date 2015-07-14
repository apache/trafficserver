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

#ifndef _I_SocketManager_h_
#define _I_SocketManager_h_

#include "ts/ink_platform.h"
#include "I_EventSystem.h"
#include "I_Thread.h"

#define DEFAULT_OPEN_MODE 0644

class Thread;
extern int net_config_poll_timeout;

#define SOCKET int

/** Utility class for socket operations.
 */
struct SocketManager {
  SocketManager();

  // result is the socket or -errno
  SOCKET socket(int domain = AF_INET, int type = SOCK_STREAM, int protocol = 0, bool bNonBlocking = true);
  SOCKET mc_socket(int domain = AF_INET, int type = SOCK_DGRAM, int protocol = 0, bool bNonBlocking = true);

  // result is the fd or -errno
  int open(const char *path, int oflag = O_RDWR | O_NDELAY | O_CREAT, mode_t mode = DEFAULT_OPEN_MODE);

  // result is the number of bytes or -errno
  int64_t read(int fd, void *buf, int len, void *pOLP = NULL);
  int64_t vector_io(int fd, struct iovec *vector, size_t count, int read_request, void *pOLP = 0);
  int64_t readv(int fd, struct iovec *vector, size_t count);
  int64_t read_vector(int fd, struct iovec *vector, size_t count, void *pOLP = 0);
  int64_t pread(int fd, void *buf, int len, off_t offset, char *tag = NULL);

  int recv(int s, void *buf, int len, int flags);
  int recvfrom(int fd, void *buf, int size, int flags, struct sockaddr *addr, socklen_t *addrlen);

  int64_t write(int fd, void *buf, int len, void *pOLP = NULL);
  int64_t writev(int fd, struct iovec *vector, size_t count);
  int64_t write_vector(int fd, struct iovec *vector, size_t count, void *pOLP = 0);
  int64_t pwrite(int fd, void *buf, int len, off_t offset, char *tag = NULL);

  int send(int fd, void *buf, int len, int flags);
  int sendto(int fd, void *buf, int len, int flags, struct sockaddr const *to, int tolen);
  int sendmsg(int fd, struct msghdr *m, int flags, void *pOLP = 0);
  int64_t lseek(int fd, off_t offset, int whence);
  int fstat(int fd, struct stat *);
  int unlink(char *buf);
  int fsync(int fildes);
  int ftruncate(int fildes, off_t length);
  int lockf(int fildes, int function, off_t size);
  int poll(struct pollfd *fds, unsigned long nfds, int timeout);
#if TS_USE_EPOLL
  int epoll_create(int size);
  int epoll_close(int eps);
  int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
  int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout = net_config_poll_timeout);
#endif
#if TS_USE_KQUEUE
  int kqueue();
  int kevent(int kq, const struct kevent *changelist, int nchanges, struct kevent *eventlist, int nevents,
             const struct timespec *timeout);
#endif
#if TS_USE_PORT
  int port_create();
  int port_associate(int port, int fd, uintptr_t obj, int events, void *user);
  int port_dissociate(int port, int fd, uintptr_t obj);
  int port_getn(int port, port_event_t *list, uint_t max, uint_t *nget, timespec_t *timeout);
#endif
  int shutdown(int s, int how);
  int dup(int s);

  // result is the fd or -errno
  int accept(int s, struct sockaddr *addr, socklen_t *addrlen);

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

  const size_t pagesize;

  virtual ~SocketManager();

private:
  // just don't do it
  SocketManager(SocketManager &);
  SocketManager &operator=(SocketManager &);
};

extern SocketManager socketManager;

#endif /*_SocketManager_h_*/
