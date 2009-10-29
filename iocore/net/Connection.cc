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
#include "ink_platform.h"

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
};


//
// Functions
//
Connection::Connection()
{
  memset(&sa, 0, sizeof(struct sockaddr_in));
  fd = NO_FD;
}


Connection::~Connection()
{
  close();
}


int
Server::accept(Connection * c)
{
  int res = 0;
  int sz = sizeof(c->sa);

  res = socketManager.accept(fd, (struct sockaddr *) &c->sa, &sz);
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
Connection::fast_connect(const unsigned int ip, const int port, NetVCOptions * opt, const int socketFd
#ifdef __INKIO
                         , bool use_inkio
#endif
  )
{
  inku32 *z;
  ink_assert(fd == NO_FD);
  int res = 0;

  if (socketFd == -1) {
    if ((res = socketManager.socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      goto Lerror;
    }
  } else {
    res = socketFd;
  }

  fd = res;

  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = ip;
  z = (inku32 *) & sa.sin_zero;
  z[0] = 0;
  z[1] = 0;

  do {
    res = safe_nonblocking(fd);
  } while (res < 0 && (errno == EAGAIN || errno == EINTR));

  // cannot do this after connection on non-blocking connect
#ifdef SET_TCP_NO_DELAY
  Debug("socket", "setting TCP_NODELAY in fast_connect");
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

  if (opt) {
    if (opt->socket_recv_bufsize > 0) {
      if (socketManager.set_rcvbuf_size(fd, opt->socket_recv_bufsize)) {
        int rbufsz = ROUNDUP(opt->socket_recv_bufsize, 1024);   // Round down until success
        while (rbufsz) {
          if (!socketManager.set_rcvbuf_size(fd, rbufsz))
            break;
          rbufsz -= 1024;
        }
      }
    }
    if (opt->socket_send_bufsize > 0) {
      if (socketManager.set_sndbuf_size(fd, opt->socket_send_bufsize)) {
        int sbufsz = ROUNDUP(opt->socket_send_bufsize, 1024);   // Round down until success
        while (sbufsz) {
          if (!socketManager.set_sndbuf_size(fd, sbufsz))
            break;
          sbufsz -= 1024;
        }
      }
    }
    if (opt->sockopt_flags & 1) {
      safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ON, sizeof(int));
      Debug("socket", "::fast_connect: setsockopt() TCP_NODELAY on socket");
    }
    if (opt->sockopt_flags & 2) {
      safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, ON, sizeof(int));
      Debug("socket", "::fast_connect: setsockopt() SO_KEEPALIVE on socket");
    }
  }
#ifdef __INKIO
  if (use_inkio) {
    inkio_queue qd = this_ethread()->kernel_q;
    res = inkio_connect(qd, fd, 0, 0, (struct sockaddr *) &sa, sizeof(struct sockaddr_in));
  } else {
    res =::connect(fd, (struct sockaddr *) &sa, sizeof(struct sockaddr_in));
  }
#else
  res =::connect(fd, (struct sockaddr *) &sa, sizeof(struct sockaddr_in));
#endif

  if (res < 0 && errno != EINPROGRESS) {
    goto Lerror;
  }

  return 0;

Lerror:
  if (fd != NO_FD)
    close();
  return res;
}


int
Connection::connect(unsigned int ip, int port,
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
  res =::connect(fd, (struct sockaddr *) &sa, sizeof(struct sockaddr_in));

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

#if (HOST_OS == linux)
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
Server::listen(int port_number, bool non_blocking, int recv_bufsize, int send_bufsize)
{
  ink_assert(fd == NO_FD);
  int res = 0;

  res = socketManager.socket(AF_INET, SOCK_STREAM, 0);
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

  memset(&sa, 0, sizeof(sa));
  //
  // Accept_ip should already be in network byte order..
  //
  sa.sin_addr.s_addr = accept_ip;
  sa.sin_port = htons(port_number);
  sa.sin_family = AF_INET;

#ifdef SET_NO_LINGER
  {
    struct linger l;
    l.l_onoff = 0;
    l.l_linger = 0;
    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &l, sizeof(l))) < 0)
      goto Lerror;
  }
#endif

  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, ON, sizeof(int))) < 0)
    goto Lerror;

  if ((res = socketManager.ink_bind(fd, (struct sockaddr *) &sa, sizeof(sa), IPPROTO_TCP)) < 0) {
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

#if (HOST_OS == linux)
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
  return res;
}
