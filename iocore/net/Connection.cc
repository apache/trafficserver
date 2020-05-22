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
#include "tscore/ink_platform.h"

#include "P_Net.h"

// set in the OS
// #define RECV_BUF_SIZE            (1024*64)
// #define SEND_BUF_SIZE            (1024*64)
#define FIRST_RANDOM_PORT 16000
#define LAST_RANDOM_PORT 32000

#define ROUNDUP(x, y) ((((x) + ((y)-1)) / (y)) * (y))

int
get_listen_backlog()
{
  int listen_backlog;

  REC_ReadConfigInteger(listen_backlog, "proxy.config.net.listen_backlog");
  return (0 < listen_backlog && listen_backlog <= 65535) ? listen_backlog : ats_tcp_somaxconn();
}

//
// Functions
//
char const *
NetVCOptions::toString(addr_bind_style s)
{
  return ANY_ADDR == s ? "any" : INTF_ADDR == s ? "interface" : "foreign";
}

Connection::Connection() : fd(NO_FD)
{
  memset(&addr, 0, sizeof(addr));
}

Connection::~Connection()
{
  close();
}

int
Server::accept(Connection *c)
{
  int res      = 0;
  socklen_t sz = sizeof(c->addr);

  res = socketManager.accept4(fd, &c->addr.sa, &sz, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (res < 0) {
    return res;
  }
  c->fd = res;
  if (is_debug_tag_set("iocore_net_server")) {
    ip_port_text_buffer ipb1, ipb2;
    Debug("iocore_net_server", "Connection accepted [Server]. %s -> %s", ats_ip_nptop(&c->addr, ipb2, sizeof(ipb2)),
          ats_ip_nptop(&addr, ipb1, sizeof(ipb1)));
  }

#ifdef SEND_BUF_SIZE
  socketManager.set_sndbuf_size(c->fd, SEND_BUF_SIZE);
#endif

  return 0;
}

int
Connection::close()
{
  is_connected = false;
  is_bound     = false;
  // don't close any of the standards
  if (fd >= 2) {
    int fd_save = fd;
    fd          = NO_FD;
    return socketManager.close(fd_save);
  } else {
    fd = NO_FD;
    return -EBADF;
  }
}

/**
 * Move control of the socket from the argument object orig to the current object.
 * Orig is marked as zombie, so when it is freed, the socket will not be closed
 */
void
Connection::move(Connection &orig)
{
  this->is_connected = orig.is_connected;
  this->is_bound     = orig.is_bound;
  this->fd           = orig.fd;
  // The target has taken ownership of the file descriptor
  orig.fd         = NO_FD;
  this->addr      = orig.addr;
  this->sock_type = orig.sock_type;
}

static int
add_http_filter(int fd ATS_UNUSED)
{
  int err = -1;
#if defined(SOL_FILTER) && defined(FIL_ATTACH)
  err = setsockopt(fd, SOL_FILTER, FIL_ATTACH, "httpfilt", 9);
#endif
  return err;
}

int
Server::setup_fd_for_listen(bool non_blocking, const NetProcessor::AcceptOptions &opt)
{
  int res               = 0;
  int listen_per_thread = 0;

  ink_assert(fd != NO_FD);

  if (opt.etype == ET_NET && opt.defer_accept > 0) {
    http_accept_filter = true;
    add_http_filter(fd);
  }

#ifdef SEND_BUF_SIZE
  {
    int send_buf_size = SEND_BUF_SIZE;
    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *)&send_buf_size, sizeof(int)) < 0)) {
      goto Lerror;
    }
  }
#endif

#ifdef RECV_BUF_SIZE
  {
    int recv_buf_size = RECV_BUF_SIZE;
    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&recv_buf_size, sizeof(int))) < 0) {
      goto Lerror;
    }
  }
#endif

  if (opt.recv_bufsize) {
    if (socketManager.set_rcvbuf_size(fd, opt.recv_bufsize)) {
      // Round down until success
      int rbufsz = ROUNDUP(opt.recv_bufsize, 1024);
      while (rbufsz) {
        if (socketManager.set_rcvbuf_size(fd, rbufsz)) {
          rbufsz -= 1024;
        } else {
          break;
        }
      }
    }
  }

  if (opt.send_bufsize) {
    if (socketManager.set_sndbuf_size(fd, opt.send_bufsize)) {
      // Round down until success
      int sbufsz = ROUNDUP(opt.send_bufsize, 1024);
      while (sbufsz) {
        if (socketManager.set_sndbuf_size(fd, sbufsz)) {
          sbufsz -= 1024;
        } else {
          break;
        }
      }
    }
  }

  if (safe_fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
    goto Lerror;
  }

  {
    struct linger l;
    l.l_onoff  = 0;
    l.l_linger = 0;
    if ((opt.sockopt_flags & NetVCOptions::SOCK_OPT_LINGER_ON) &&
        safe_setsockopt(fd, SOL_SOCKET, SO_LINGER, reinterpret_cast<char *>(&l), sizeof(l)) < 0) {
      goto Lerror;
    }
  }

  if (ats_is_ip6(&addr) && safe_setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, SOCKOPT_ON, sizeof(int)) < 0) {
    goto Lerror;
  }

  if (safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, SOCKOPT_ON, sizeof(int)) < 0) {
    goto Lerror;
  }
  REC_ReadConfigInteger(listen_per_thread, "proxy.config.exec_thread.listen");
  if (listen_per_thread == 1) {
    if (safe_setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, SOCKOPT_ON, sizeof(int)) < 0) {
      goto Lerror;
    }
#ifdef SO_REUSEPORT_LB
    if (safe_setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, SOCKOPT_ON, sizeof(int)) < 0) {
      goto Lerror;
    }
#endif
  }

  if ((opt.sockopt_flags & NetVCOptions::SOCK_OPT_NO_DELAY) &&
      safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, SOCKOPT_ON, sizeof(int)) < 0) {
    goto Lerror;
  }

  // enables 2 hour inactivity probes, also may fix IRIX FIN_WAIT_2 leak
  if ((opt.sockopt_flags & NetVCOptions::SOCK_OPT_KEEP_ALIVE) &&
      safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, SOCKOPT_ON, sizeof(int)) < 0) {
    goto Lerror;
  }

#ifdef TCP_FASTOPEN
  if ((opt.sockopt_flags & NetVCOptions::SOCK_OPT_TCP_FAST_OPEN) &&
      safe_setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, (char *)&opt.tfo_queue_length, sizeof(int))) {
    goto Lerror;
  }
#endif

  if (opt.f_inbound_transparent) {
#if TS_USE_TPROXY
    Debug("http_tproxy", "Listen port inbound transparency enabled.");
    if (safe_setsockopt(fd, SOL_IP, TS_IP_TRANSPARENT, SOCKOPT_ON, sizeof(int)) < 0) {
      Fatal("[Server::listen] Unable to set transparent socket option [%d] %s\n", errno, strerror(errno));
    }
#else
    Error("[Server::listen] Transparency requested but TPROXY not configured\n");
#endif
  }

  if (opt.f_proxy_protocol) {
    Debug("proxyprotocol", "Proxy Protocol enabled.");
  }

#if defined(TCP_MAXSEG)
  if (NetProcessor::accept_mss > 0) {
    if (safe_setsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, reinterpret_cast<char *>(&NetProcessor::accept_mss), sizeof(int)) < 0) {
      goto Lerror;
    }
  }
#endif

#ifdef TCP_DEFER_ACCEPT
  // set tcp defer accept timeout if it is configured, this will not trigger an accept until there is
  // data on the socket ready to be read
  if (opt.defer_accept > 0 && setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt.defer_accept, sizeof(int)) < 0) {
    // FIXME: should we go to the error
    // goto error;
    Error("[Server::listen] Defer accept is configured but set failed: %d", errno);
  }
#endif

  if (non_blocking) {
    if (safe_nonblocking(fd) < 0) {
      goto Lerror;
    }
  }

  return 0;

Lerror:
  res = -errno;

  // coverity[check_after_sink]
  if (fd != NO_FD) {
    close();
  }

  return res;
}

int
Server::listen(bool non_blocking, const NetProcessor::AcceptOptions &opt)
{
  ink_assert(fd == NO_FD);
  int res = 0;
  int namelen;

  if (!ats_is_ip(&accept_addr)) {
    ats_ip4_set(&addr, INADDR_ANY, 0);
  } else {
    ats_ip_copy(&addr, &accept_addr);
  }

  fd = res = socketManager.socket(addr.sa.sa_family, SOCK_STREAM, IPPROTO_TCP);
  if (res < 0) {
    goto Lerror;
  }

  res = setup_fd_for_listen(non_blocking, opt);
  if (res < 0) {
    goto Lerror;
  }

  if ((res = socketManager.ink_bind(fd, &addr.sa, ats_ip_size(&addr.sa), IPPROTO_TCP)) < 0) {
    goto Lerror;
  }

  if ((res = safe_listen(fd, get_listen_backlog())) < 0) {
    goto Lerror;
  }

  // Original just did this on port == 0.
  namelen = sizeof(addr);
  if ((res = safe_getsockname(fd, &addr.sa, &namelen))) {
    goto Lerror;
  }

  return 0;

Lerror:
  if (fd != NO_FD) {
    close();
    fd = NO_FD;
  }

  Error("Could not bind or listen to port %d (error: %d)", ats_ip_port_host_order(&addr), res);
  return res;
}
