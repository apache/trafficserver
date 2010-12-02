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

#include "ink_unused.h"   /* MAGIC_EDITING_TAG */
#include "libts.h"

#include "P_Net.h"

#define SET_TCP_NO_DELAY
#define SET_NO_LINGER
// set in the OS
// #define RECV_BUF_SIZE            (1024*64)
// #define SEND_BUF_SIZE            (1024*64)
#define FIRST_RANDOM_PORT        16000
#define LAST_RANDOM_PORT         32000

#define ROUNDUP(x, y) ((((x)+((y)-1))/(y))*(y))

int
get_listen_backlog(void)
{
  int listen_backlog = 1024;

  IOCORE_ReadConfigInteger(listen_backlog, "proxy.config.net.listen_backlog");
  return listen_backlog;
}


//
// Functions
//
char const*
NetVCOptions::toString(addr_bind_style s) {
  return ANY_ADDR == s ? "any"
    : INTF_ADDR == s ? "interface"
    : "foreign"
    ;
}

Connection::Connection()
  : fd(NO_FD)
  , is_bound(false)
  , is_connected(false)
{
  memset(&sa, 0, sizeof(struct sockaddr_storage));
}


Connection::~Connection()
{
  close();
}


int
Server::accept(Connection * c)
{
  int res = 0;
  socklen_t sz = sizeof(c->sa);

  res = socketManager.accept(fd, (struct sockaddr *)&c->sa, &sz);
  if (res < 0)
    return res;
  c->fd = res;

#ifdef SET_CLOSE_ON_EXEC
  if ((res = safe_fcntl(fd, F_SETFD, 1)) < 0)
    goto Lerror;
#endif
  if ((res = safe_nonblocking(c->fd)) < 0)
    goto Lerror;
#ifdef SEND_BUF_SIZE
  socketManager.set_sndbuf_size(c->fd, SEND_BUF_SIZE);
#endif
#ifdef SET_SO_KEEPALIVE
  // enables 2 hour inactivity probes, also may fix IRIX FIN_WAIT_2 leak
  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, ON, sizeof(int))) < 0)
    goto Lerror;
#endif

  return 0;
Lerror:
  c->close();
  return res;
}


int
Connection::close()
{
  is_connected = false;
  is_bound = false;
  // don't close any of the standards
  if (fd >= 2) {
    int fd_save = fd;
    fd = NO_FD;
    return socketManager.close(fd_save);
  } else {
    fd = NO_FD;
    return -EBADF;
  }
}

int
Server::setup_fd_for_listen(bool non_blocking, int recv_bufsize, int send_bufsize)
{
  int res = 0;
#ifdef SEND_BUF_SIZE
  {
    int send_buf_size = SEND_BUF_SIZE;
    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &send_buf_size, sizeof(int)) < 0))
      goto Lerror;
  }
#endif
#ifdef RECV_BUF_SIZE
  {
    int recv_buf_size = RECV_BUF_SIZE;
    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *) &recv_buf_size, sizeof(int))) < 0)
      goto Lerror;
  }
#endif

  if (recv_bufsize) {
    // coverity[negative_sink_in_call]
    if (socketManager.set_rcvbuf_size(fd, recv_bufsize)) {
      // Round down until success
      int rbufsz = ROUNDUP(recv_bufsize, 1024);
      while (rbufsz) {
        if (socketManager.set_rcvbuf_size(fd, rbufsz)) {
          rbufsz -= 1024;
        } else {
          break;
        }
      }
    }
  }
  if (send_bufsize) {
    if (socketManager.set_sndbuf_size(fd, send_bufsize)) {
      // Round down until success
      int sbufsz = ROUNDUP(send_bufsize, 1024);
      while (sbufsz) {
        if (socketManager.set_sndbuf_size(fd, sbufsz)) {
          sbufsz -= 1024;
        } else {
          break;
        }
      }
    }
  }
#ifdef SET_NO_LINGER
  {
    struct linger l;
    l.l_onoff = 0;
    l.l_linger = 0;
    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &l, sizeof(l))) < 0)
      goto Lerror;
  }
#endif
#ifdef SET_TCP_NO_DELAY
  if ((res = safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ON, sizeof(int))) < 0)
    goto Lerror;
#endif
#ifdef SET_SO_KEEPALIVE
  // enables 2 hour inactivity probes, also may fix IRIX FIN_WAIT_2 leak
  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, ON, sizeof(int))) < 0)
    goto Lerror;
#endif

#if defined(linux)
  if (NetProcessor::accept_mss > 0)
    if ((res = safe_setsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, (char *) &NetProcessor::accept_mss, sizeof(int)) < 0))
      goto Lerror;
#endif

  /*
   * dg: this has been removed since the ISS patch under solaris seems
   * to not like the socket being listened on twice. This is first done
   * in the manager when the socket is created.
   */
  if (non_blocking)
    if ((res = safe_nonblocking(fd)) < 0)
      goto Lerror;
  {
    int namelen = sizeof(sa);
    if ((res = safe_getsockname(fd, (struct sockaddr *) &sa, &namelen)))
      goto Lerror;
  }
  return 0;
Lerror:
  res = -errno;
  // coverity[check_after_sink]
  if (fd != NO_FD)
    close();
  return res;
}


int
Server::listen(int port_number, int domain, bool non_blocking, int recv_bufsize, int send_bufsize)
{
  ink_assert(fd == NO_FD);
  int res = 0;
  int gai_errno = 0;

  char port[6] = {'\0'};
  struct addrinfo hints;
  struct addrinfo *ai_res = NULL;
  struct addrinfo *ai = NULL;
  socklen_t addrlen = 0;  // keep track of length of socket address info
  snprintf(port, sizeof(port), "%d", port_number);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = domain;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE|AI_NUMERICHOST|AI_ADDRCONFIG;
  gai_errno = getaddrinfo(accept_ip_str, port, &hints, &ai_res);
  if(0 != gai_errno) {
    Error("getaddrinfo error %i: %s", gai_errno, gai_strerror(gai_errno));
    return -1;
  }

  ai = ai_res;

  res = socketManager.socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);

  memset(&sa, 0, sizeof(sa));
  addrlen = ai->ai_addrlen;  // save value for later since ai will be freed asap
  memcpy(&sa, ai->ai_addr, ai->ai_addrlen);

  freeaddrinfo(ai_res);

  if (res < 0)
    return res;
  fd = res;

#ifdef SEND_BUF_SIZE
  {
    int send_buf_size = SEND_BUF_SIZE;
    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &send_buf_size, sizeof(int)) < 0))
      goto Lerror;
  }
#endif
#ifdef RECV_BUF_SIZE
  {
    int recv_buf_size = RECV_BUF_SIZE;
    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *) &recv_buf_size, sizeof(int))) < 0)
      goto Lerror;
  }
#endif

  if (recv_bufsize) {
    if (socketManager.set_rcvbuf_size(fd, recv_bufsize)) {
      // Round down until success
      int rbufsz = ROUNDUP(recv_bufsize, 1024);
      while (rbufsz) {
        if (socketManager.set_rcvbuf_size(fd, rbufsz)) {
          rbufsz -= 1024;
        } else {
          break;
        }
      }
    }
  }
  if (send_bufsize) {
    if (socketManager.set_sndbuf_size(fd, send_bufsize)) {
      // Round down until success
      int sbufsz = ROUNDUP(send_bufsize, 1024);
      while (sbufsz) {
        if (socketManager.set_sndbuf_size(fd, sbufsz)) {
          sbufsz -= 1024;
        } else {
          break;
        }
      }
    }
  }
#ifdef SET_CLOSE_ON_EXEC
  if ((res = safe_fcntl(fd, F_SETFD, 1)) < 0)
    goto Lerror;
#endif

#ifdef SET_NO_LINGER
  {
    struct linger l;
    l.l_onoff = 0;
    l.l_linger = 0;
    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &l, sizeof(l))) < 0)
      goto Lerror;
  }
#endif

  if (domain == AF_INET6 && (res = safe_setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, ON, sizeof(int))) < 0)
    goto Lerror;

  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, ON, sizeof(int))) < 0)
    goto Lerror;

  if ((res = socketManager.ink_bind(fd, (struct sockaddr *) &sa, addrlen, IPPROTO_TCP)) < 0) {
    goto Lerror;
  }
#ifdef SET_TCP_NO_DELAY
  if ((res = safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ON, sizeof(int))) < 0)
    goto Lerror;
#endif
#ifdef SET_SO_KEEPALIVE
  // enables 2 hour inactivity probes, also may fix IRIX FIN_WAIT_2 leak
  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, ON, sizeof(int))) < 0)
    goto Lerror;
#endif

#if defined(linux)
  if (NetProcessor::accept_mss > 0)
    if ((res = safe_setsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, (char *) &NetProcessor::accept_mss, sizeof(int))) < 0)
      goto Lerror;
#endif

  if ((res = safe_listen(fd, get_listen_backlog())) < 0)
    goto Lerror;
  if (non_blocking)
    if ((res = safe_nonblocking(fd)) < 0)
      goto Lerror;
  if (!port_number) {
    int namelen = sizeof(sa);
    if ((res = safe_getsockname(fd, (struct sockaddr *) &sa, &namelen)))
      goto Lerror;
  }
  return 0;

Lerror:
  if (fd != NO_FD)
    close();
  Error("Could not bind or listen to port %d (error: %d)", port_number, res);
  return res;
}
