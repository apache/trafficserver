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

/**************************************************************************
  Connections

  Commonality across all platforms -- move out as required.

**************************************************************************/

#include "ink_unused.h" /* MAGIC_EDITING_TAG */
#include "ink_platform.h"

#include "P_DNS.h"
#include "P_DNSConnection.h"

#define SET_TCP_NO_DELAY
#define SET_NO_LINGER
// set in the OS
// #define RECV_BUF_SIZE            (1024*64)
// #define SEND_BUF_SIZE            (1024*64)
#define FIRST_RANDOM_PORT        16000
#define LAST_RANDOM_PORT         32000

#define ROUNDUP(x, y) ((((x)+((y)-1))/(y))*(y))

//
// Functions
//

DNSConnection::DNSConnection():
fd(NO_FD), num(0), epoll_ptr(NULL)
{
  memset(&sa, 0, sizeof(struct sockaddr_in));
}

DNSConnection::~DNSConnection()
{
  close();
}

int
DNSConnection::close()
{
  // don't close any of the standards
  if (fd >= 2) {
    int fd_save = fd;
    fd = NO_FD;
    return socketManager.close(fd_save, keSocket);
  } else {
    fd = NO_FD;
    return -EBADF;
  }
}

int
DNSConnection::connect(unsigned int ip, int port,
                       bool non_blocking_connect, bool use_tcp, bool non_blocking, bool bind_random_port)
{
  ink_assert(fd == NO_FD);

  int res = 0;
  ink_hrtime t;
  short Proto;

  if (use_tcp) {
    Proto = IPPROTO_TCP;
    if ((res = socketManager.socket(AF_INET, SOCK_STREAM, 0)) < 0)
      goto Lerror;
  } else {
    Proto = IPPROTO_UDP;
    if ((res = socketManager.socket(AF_INET, SOCK_DGRAM, 0)) < 0)
      goto Lerror;
  }

  fd = res;

  if (bind_random_port) {
    int retries = 0;
    int offset = 0;
    while (retries++ < 10000) {
      struct sockaddr_in bind_sa;
      memset(&sa, 0, sizeof(bind_sa));
      bind_sa.sin_family = AF_INET;
      bind_sa.sin_addr.s_addr = INADDR_ANY;
      int p = time(NULL) + offset;
      p = (p % (LAST_RANDOM_PORT - FIRST_RANDOM_PORT)) + FIRST_RANDOM_PORT;
      bind_sa.sin_port = htons(p);
      Debug("dns", "random port = %d\n", p);
      if ((res = socketManager.ink_bind(fd, (struct sockaddr *) &bind_sa, sizeof(bind_sa), Proto)) < 0) {
        offset += 101;
        continue;
      }
      goto Lok;
    }
    IOCORE_MachineFatal("unable to bind random DNS port");
  Lok:;
  }

  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = ip;
  memset(&sa.sin_zero, 0, 8);

  if (non_blocking_connect)
    if ((res = safe_nonblocking(fd)) < 0)
      goto Lerror;

  // cannot do this after connection on non-blocking connect
#ifdef SET_TCP_NO_DELAY
  if (use_tcp)
    if ((res = safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ON, sizeof(int))) < 0)
      goto Lerror;
#endif
#ifdef RECV_BUF_SIZE
  socketManager.set_rcvbuf_size(fd, RECV_BUF_SIZE);
#endif
#ifdef SET_SO_KEEPALIVE
  // enables 2 hour inactivity probes, also may fix IRIX FIN_WAIT_2 leak
  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, ON, sizeof(int))) < 0)
    goto Lerror;
#endif

  t = ink_get_hrtime();
#ifdef BSD_TCP
  if (IS_BSD_FD(fd)) {
    res = inkio_connect(BSD_FD(fd), (struct sockaddr *) &sa, sizeof(struct sockaddr_in));
  } else {
    res =::connect(fd, (struct sockaddr *) &sa, sizeof(struct sockaddr_in));
  }
#else
  res =::connect(fd, (struct sockaddr *) &sa, sizeof(struct sockaddr_in));
#endif

  if (!res || ((res < 0) && (errno == EINPROGRESS || errno == EWOULDBLOCK))) {
    if (!non_blocking_connect && non_blocking)
      if ((res = safe_nonblocking(fd)) < 0)
        goto Lerror;
  } else
    goto Lerror;

  return 0;

Lerror:
  if (fd != NO_FD)
    close();
  return res;
}
