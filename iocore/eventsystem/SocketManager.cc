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

  SocketManager.cc
 ****************************************************************************/
#include "tscore/ink_platform.h"
#include "P_EventSystem.h"

#include "tscore/TextBuffer.h"
#include "tscore/TestBox.h"

SocketManager socketManager;

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
#endif

int
SocketManager::accept4(int s, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
  int fd;

  do {
    fd = ::accept4(s, addr, addrlen, flags);
    if (likely(fd >= 0)) {
      return fd;
    }
  } while (transient_error());

  return -errno;
}

SocketManager::SocketManager() : pagesize(ats_pagesize()) {}

SocketManager::~SocketManager()
{
  // free the hash table and values
}

int
SocketManager::ink_bind(int s, struct sockaddr const *name, int namelen, short Proto)
{
  (void)Proto;
  return safe_bind(s, name, namelen);
}

int
SocketManager::close(int s)
{
  int res;

  if (s == 0) {
    return -EACCES;
  } else if (s < 0) {
    return -EINVAL;
  }

  do {
    res = ::close(s);
    if (res == -1) {
      res = -errno;
    }
  } while (res == -EINTR);
  return res;
}

bool
SocketManager::fastopen_supported()
{
  static const unsigned TFO_CLIENT_ENABLE = 1;

  ats_scoped_fd fd(::open("/proc/sys/net/ipv4/tcp_fastopen", O_RDONLY));
  int value = 0;

  if (fd) {
    TextBuffer buffer(16);

    buffer.slurp(fd.get());
    value = atoi(buffer.bufPtr());
  }

  return value & TFO_CLIENT_ENABLE;
}

REGRESSION_TEST(socket_fastopen)(RegressionTest *test, int level, int *pstatus)
{
  TestBox box(test, pstatus);

  box = REGRESSION_TEST_PASSED;

  if (SocketManager::fastopen_supported()) {
    box.check(MSG_FASTOPEN != 0, "TCP Fast Open is supported, MSG_FASTOPEN must not be 0");
  }

  if (::access("/proc/sys/net/ipv4/tcp_fastopen", F_OK) == 0) {
    box.check(MSG_FASTOPEN != 0, "TCP Fast Open is available, MSG_FASTOPEN must not be 0");
  }
}
