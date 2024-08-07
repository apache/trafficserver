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

#include "P_Net.h"

#include "tscore/ink_defs.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_sock.h"

#if TS_USE_HWLOC
#include <hwloc.h>
#endif

#ifdef SO_ACCEPTFILTER
#include <sys/param.h>
#include <sys/linker.h>
#endif

namespace
{
DbgCtl dbg_ctl_http_tproxy{"http_tproxy"};
DbgCtl dbg_ctl_proxyprotocol{"proxyprotocol"};
DbgCtl dbg_ctl_iocore_net_server{"iocore_net_server"};
DbgCtl dbg_ctl_connection{"connection"};
DbgCtl dbg_ctl_iocore_thread{"iocore_thread"};

} // end anonymous namespace

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

Connection::Connection()
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
  int       res = 0;
  socklen_t sz  = sizeof(c->addr);

  res = sock.accept4(&c->addr.sa, &sz, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (res < 0) {
    return res;
  }
  c->sock = UnixSocket{res};
  if (dbg_ctl_iocore_net_server.on()) {
    ip_port_text_buffer ipb1, ipb2;
    DbgPrint(dbg_ctl_iocore_net_server, "Connection accepted [Server]. %s -> %s", ats_ip_nptop(&c->addr, ipb2, sizeof(ipb2)),
             ats_ip_nptop(&addr, ipb1, sizeof(ipb1)));
  }

  return 0;
}

int
Connection::close()
{
  is_connected = false;
  is_bound     = false;
  // don't close any of the standards
  if (sock.get_fd() >= 2) {
    return sock.close();
  } else {
    sock = UnixSocket{NO_FD};
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
  this->sock         = orig.sock;
  // The target has taken ownership of the file descriptor
  orig.sock       = UnixSocket{NO_FD};
  this->addr      = orig.addr;
  this->sock_type = orig.sock_type;
}

static int
add_http_filter([[maybe_unused]] int fd)
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

  ink_assert(sock.is_ok());

  if (opt.defer_accept > 0) {
    http_accept_filter = true;
    add_http_filter(sock.get_fd());
  }

  if (opt.recv_bufsize) {
    if (sock.set_rcvbuf_size(opt.recv_bufsize)) {
      // Round down until success
      int rbufsz = ROUNDUP(opt.recv_bufsize, 1024);
      while (rbufsz) {
        if (sock.set_rcvbuf_size(rbufsz)) {
          rbufsz -= 1024;
        } else {
          break;
        }
      }
    }
  }

  if (opt.send_bufsize) {
    if (sock.set_sndbuf_size(opt.send_bufsize)) {
      // Round down until success
      int sbufsz = ROUNDUP(opt.send_bufsize, 1024);
      while (sbufsz) {
        if (sock.set_sndbuf_size(sbufsz)) {
          sbufsz -= 1024;
        } else {
          break;
        }
      }
    }
  }

  if (safe_fcntl(sock.get_fd(), F_SETFD, FD_CLOEXEC) < 0) {
    goto Lerror;
  }

  {
    struct linger l;
    l.l_onoff  = 0;
    l.l_linger = 0;
    if ((opt.sockopt_flags & NetVCOptions::SOCK_OPT_LINGER_ON) &&
        safe_setsockopt(sock.get_fd(), SOL_SOCKET, SO_LINGER, reinterpret_cast<char *>(&l), sizeof(l)) < 0) {
      goto Lerror;
    }
  }

  if (ats_is_ip6(&addr) && sock.enable_option(IPPROTO_IPV6, IPV6_V6ONLY) < 0) {
    goto Lerror;
  }

  if (sock.enable_option(SOL_SOCKET, SO_REUSEADDR) < 0) {
    goto Lerror;
  }
  REC_ReadConfigInteger(listen_per_thread, "proxy.config.exec_thread.listen");
  if (listen_per_thread == 1) {
    if (sock.enable_option(SOL_SOCKET, SO_REUSEPORT) < 0) {
      goto Lerror;
    }
#ifdef SO_REUSEPORT_LB
    if (sock.enable_option(SOL_SOCKET, SO_REUSEPORT_LB) < 0) {
      goto Lerror;
    }
#endif
  }

#ifdef SO_INCOMING_CPU
  if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_INCOMING_CPU) {
    int      cpu     = 0;
    EThread *ethread = this_ethread();

#if TS_USE_HWLOC
    cpu = ethread->hwloc_obj->os_index;
#else
    cpu = ethread->id;
#endif
    if (safe_setsockopt(sock.get_fd(), SOL_SOCKET, SO_INCOMING_CPU, &cpu, sizeof(cpu)) < 0) {
      goto Lerror;
    }
    Dbg(dbg_ctl_iocore_thread, "SO_INCOMING_CPU - fd=%d cpu=%d", sock.get_fd(), cpu);
  }
#endif
  if ((opt.sockopt_flags & NetVCOptions::SOCK_OPT_NO_DELAY) && sock.enable_option(IPPROTO_TCP, TCP_NODELAY) < 0) {
    goto Lerror;
  }

  // enables 2 hour inactivity probes, also may fix IRIX FIN_WAIT_2 leak
  if ((opt.sockopt_flags & NetVCOptions::SOCK_OPT_KEEP_ALIVE) && sock.enable_option(SOL_SOCKET, SO_KEEPALIVE) < 0) {
    goto Lerror;
  }

#ifdef TCP_FASTOPEN
  if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_TCP_FAST_OPEN) {
    if (safe_setsockopt(sock.get_fd(), IPPROTO_TCP, TCP_FASTOPEN, (char *)&opt.tfo_queue_length, sizeof(int))) {
      // EOPNOTSUPP also checked for general safeguarding of unsupported operations of socket functions
      if (opt.f_mptcp && (errno == ENOPROTOOPT || errno == EOPNOTSUPP)) {
        Warning("[Server::listen] TCP_FASTOPEN socket option not valid on MPTCP socket level");
      } else {
        goto Lerror;
      }
    }
  }
#endif

  if (opt.f_inbound_transparent) {
#if TS_USE_TPROXY
    Dbg(dbg_ctl_http_tproxy, "Listen port inbound transparency enabled.");
    if (sock.enable_option(SOL_IP, TS_IP_TRANSPARENT) < 0) {
      Fatal("[Server::listen] Unable to set transparent socket option [%d] %s\n", errno, strerror(errno));
    }
#else
    Error("[Server::listen] Transparency requested but TPROXY not configured\n");
#endif
  }

  if (opt.f_proxy_protocol) {
    Dbg(dbg_ctl_proxyprotocol, "Proxy Protocol enabled.");
  }

#if defined(TCP_MAXSEG)
  if (NetProcessor::accept_mss > 0) {
    if (opt.f_mptcp) {
      Warning("[Server::listen] TCP_MAXSEG socket option not valid on MPTCP socket level");
    } else if (safe_setsockopt(sock.get_fd(), IPPROTO_TCP, TCP_MAXSEG, reinterpret_cast<char *>(&NetProcessor::accept_mss),
                               sizeof(int)) < 0) {
      goto Lerror;
    }
  }
#endif

#ifdef TCP_DEFER_ACCEPT
  // set tcp defer accept timeout if it is configured, this will not trigger an accept until there is
  // data on the socket ready to be read
  if (opt.defer_accept > 0 && setsockopt(sock.get_fd(), IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt.defer_accept, sizeof(int)) < 0) {
    // FIXME: should we go to the error
    // goto error;
    Error("[Server::listen] Defer accept is configured but set failed: %d", errno);
  }
#endif

  if (non_blocking) {
    if (sock.set_nonblocking() < 0) {
      goto Lerror;
    }
  }

  return 0;

Lerror:
  res = -errno;

  // coverity[check_after_sink]
  if (sock.is_ok()) {
    close();
  }

  return res;
}

int
Server::setup_fd_after_listen([[maybe_unused]] const NetProcessor::AcceptOptions &opt)
{
#ifdef SO_ACCEPTFILTER
  // SO_ACCEPTFILTER needs to be set **after** listen
  if (opt.defer_accept > 0) {
    int file_id = kldfind("accf_data");

    struct kld_file_stat stat;
    stat.version = sizeof(stat);

    if (kldstat(file_id, &stat) < 0) {
      Error("[Server::listen] Ignored defer_accept config. Because accf_data module is not loaded errno=%d", errno);
    } else {
      struct accept_filter_arg afa;

      bzero(&afa, sizeof(afa));
      strcpy(afa.af_name, "dataready");

      if (setsockopt(this->sock.get_fd(), SOL_SOCKET, SO_ACCEPTFILTER, &afa, sizeof(afa)) < 0) {
        Error("[Server::listen] Defer accept is configured but set failed: %d", errno);
        return -errno;
      }
    }
  }
#endif

  return 0;
}

int
Server::listen(bool non_blocking, const NetProcessor::AcceptOptions &opt)
{
  ink_assert(!sock.is_ok());
  int       res = 0;
  socklen_t namelen;
  int       prot = IPPROTO_TCP;

  if (!ats_is_ip(&accept_addr)) {
    ats_ip4_set(&addr, INADDR_ANY, 0);
  } else {
    ats_ip_copy(&addr, &accept_addr);
  }

  if (opt.f_mptcp) {
    Dbg(dbg_ctl_connection, "Define socket with MPTCP");
    prot = IPPROTO_MPTCP;
  }

  sock = UnixSocket{addr.sa.sa_family, SOCK_STREAM, prot};
  if (!sock.is_ok()) {
    goto Lerror;
  }

  res = setup_fd_for_listen(non_blocking, opt);
  if (res < 0) {
    goto Lerror;
  }

  if ((res = sock.bind(&addr.sa, ats_ip_size(&addr.sa))) < 0) {
    goto Lerror;
  }

  if ((res = safe_listen(sock.get_fd(), get_listen_backlog())) < 0) {
    goto Lerror;
  }

  res = setup_fd_after_listen(opt);
  if (res < 0) {
    goto Lerror;
  }

  // Original just did this on port == 0.
  namelen = sizeof(addr);
  if ((res = sock.getsockname(&addr.sa, &namelen))) {
    goto Lerror;
  }

  return 0;

Lerror:
  if (sock.is_ok()) {
    close();
  }

  Fatal("Could not bind or listen to port %d, mptcp enabled: %d (error: %d) %s %d", ats_ip_port_host_order(&addr),
        prot == IPPROTO_MPTCP, errno, strerror(errno), res);
  return res;
}
