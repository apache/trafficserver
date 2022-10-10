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

#include <tscore/TSSystemState.h>
#include <tscore/ink_defs.h>

#include "P_Net.h"

using NetAcceptHandler = int (NetAccept::*)(int, void *);
int accept_till_done   = 1;

// we need to protect naVec since it might be accessed
// in different threads at the same time
Ptr<ProxyMutex> naVecMutex;
std::vector<NetAccept *> naVec;
static void
safe_delay(int msec)
{
  socketManager.poll(nullptr, 0, msec);
}

//
// General case network connection accept code
//
int
net_accept(NetAccept *na, void *ep, bool blockable)
{
  Event *e               = static_cast<Event *>(ep);
  int res                = 0;
  int count              = 0;
  int loop               = accept_till_done;
  UnixNetVConnection *vc = nullptr;
  Connection con;

  if (!blockable) {
    if (!MUTEX_TAKE_TRY_LOCK(na->action_->mutex, e->ethread)) {
      return 0;
    }
  }

  // do-while for accepting all the connections
  // added by YTS Team, yamsat
  do {
    if ((res = na->server.accept(&con)) < 0) {
      if (res == -EAGAIN || res == -ECONNABORTED || res == -EPIPE) {
        goto Ldone;
      }
      if (na->server.fd != NO_FD && !na->action_->cancelled) {
        if (!blockable) {
          na->action_->continuation->handleEvent(EVENT_ERROR, (void *)static_cast<intptr_t>(res));
        } else {
          SCOPED_MUTEX_LOCK(lock, na->action_->mutex, e->ethread);
          na->action_->continuation->handleEvent(EVENT_ERROR, (void *)static_cast<intptr_t>(res));
        }
      }
      count = res;
      goto Ldone;
    }
    NET_SUM_GLOBAL_DYN_STAT(net_tcp_accept_stat, 1);

    vc = static_cast<UnixNetVConnection *>(na->getNetProcessor()->allocate_vc(e->ethread));
    if (!vc) {
      goto Ldone; // note: @a con will clean up the socket when it goes out of scope.
    }

    ++count;
    NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, 1);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->set_remote_addr(con.addr);
    vc->submit_time = Thread::get_hrtime();
    vc->action_     = *na->action_;
    vc->set_is_transparent(na->opt.f_inbound_transparent);
    vc->set_is_proxy_protocol(na->opt.f_proxy_protocol);
    vc->set_context(NET_VCONNECTION_IN);
    if (na->opt.f_mptcp) {
      vc->set_mptcp_state(); // Try to get the MPTCP state, and update accordingly
    }
#ifdef USE_EDGE_TRIGGER
    // Set the vc as triggered and place it in the read ready queue later in case there is already data on the socket.
    if (na->server.http_accept_filter) {
      vc->read.triggered = 1;
    }
#endif
    SET_CONTINUATION_HANDLER(vc, &UnixNetVConnection::acceptEvent);

    EThread *t;
    NetHandler *h;
    if (e->ethread->is_event_type(na->opt.etype)) {
      t = e->ethread;
      h = get_NetHandler(t);
      // Assign NetHandler->mutex to NetVC
      vc->mutex = h->mutex;
      MUTEX_TRY_LOCK(lock, h->mutex, t);
      if (!lock.is_locked()) {
        t->schedule_in(vc, HRTIME_MSECONDS(net_retry_delay));
      } else {
        vc->handleEvent(EVENT_NONE, e);
      }
    } else {
      t = eventProcessor.assign_thread(na->opt.etype);
      h = get_NetHandler(t);
      // Assign NetHandler->mutex to NetVC
      vc->mutex = h->mutex;
      t->schedule_imm(vc);
    }
  } while (loop);

Ldone:
  if (!blockable) {
    MUTEX_UNTAKE_LOCK(na->action_->mutex, e->ethread);
  }
  return count;
}

NetAccept *
getNetAccept(int ID)
{
  SCOPED_MUTEX_LOCK(lock, naVecMutex, this_ethread());
  return naVec.at(ID);
}

//
// Initialize the NetAccept for execution in its own thread.
// This should be done for low latency, high connection rate sockets.
//
void
NetAccept::init_accept_loop()
{
  int i, n;
  char thr_name[MAX_THREAD_NAME_LENGTH];
  size_t stacksize;
  if (do_listen(BLOCKING)) {
    return;
  }
  REC_ReadConfigInteger(stacksize, "proxy.config.thread.default.stacksize");
  SET_CONTINUATION_HANDLER(this, &NetAccept::acceptLoopEvent);

  n = opt.accept_threads;
  // Fill in accept thread from configuration if necessary.
  if (n < 0) {
    REC_ReadConfigInteger(n, "proxy.config.accept_threads");
  }

  for (i = 0; i < n; i++) {
    NetAccept *a = (i < n - 1) ? clone() : this;
    snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[ACCEPT %d:%d]", i, ats_ip_port_host_order(&server.accept_addr));
    eventProcessor.spawn_thread(a, thr_name, stacksize);
    Debug("iocore_net_accept_start", "Created accept thread #%d for port %d", i + 1, ats_ip_port_host_order(&server.accept_addr));
  }
}

//
// Initialize the NetAccept for execution in a etype thread.
// This should be done for low connection rate sockets.
// (Management, Cluster, etc.)  Also, since it adapts to the
// number of connections arriving, it should be reasonable to
// use it for high connection rates as well.
//
void
NetAccept::init_accept(EThread *t)
{
  if (!t) {
    t = eventProcessor.assign_thread(opt.etype);
  }

  if (!action_->continuation->mutex) {
    action_->continuation->mutex = t->mutex;
    action_->mutex               = t->mutex;
  }

  if (do_listen(NON_BLOCKING)) {
    return;
  }

  SET_HANDLER(&NetAccept::acceptEvent);
  period = -HRTIME_MSECONDS(net_accept_period);
  t->schedule_every(this, period);
}

int
NetAccept::accept_per_thread(int event, void *ep)
{
  int listen_per_thread = 0;
  REC_ReadConfigInteger(listen_per_thread, "proxy.config.exec_thread.listen");

  if (listen_per_thread == 1) {
    if (do_listen(NON_BLOCKING)) {
      Fatal("[NetAccept::accept_per_thread]:error listenting on ports");
      return -1;
    }
  }

  if (accept_fn == net_accept) {
    SET_HANDLER(&NetAccept::acceptFastEvent);
  } else {
    SET_HANDLER(&NetAccept::acceptEvent);
  }
  PollDescriptor *pd = get_PollDescriptor(this_ethread());
  if (this->ep.start(pd, this, EVENTIO_READ) < 0) {
    Fatal("[NetAccept::accept_per_thread]:error starting EventIO");
    return -1;
  }
  return 0;
}

void
NetAccept::init_accept_per_thread()
{
  int i, n;
  int listen_per_thread = 0;

  ink_assert(opt.etype >= 0);
  REC_ReadConfigInteger(listen_per_thread, "proxy.config.exec_thread.listen");

  if (listen_per_thread == 0) {
    if (do_listen(NON_BLOCKING)) {
      Fatal("[NetAccept::accept_per_thread]:error listenting on ports");
      return;
    }
  }

  SET_HANDLER(&NetAccept::accept_per_thread);
  n = eventProcessor.thread_group[opt.etype]._count;

  for (i = 0; i < n; i++) {
    NetAccept *a = (i < n - 1) ? clone() : this;
    EThread *t   = eventProcessor.thread_group[opt.etype]._thread[i];
    a->mutex     = get_NetHandler(t)->mutex;
    t->schedule_imm(a);
  }
}

void
NetAccept::stop_accept()
{
  if (!action_->cancelled) {
    action_->cancel();
  }
  server.close();
}

int
NetAccept::do_listen(bool non_blocking)
{
  int res = 0;

  if (server.fd != NO_FD) {
    if ((res = server.setup_fd_for_listen(non_blocking, opt))) {
      Warning("unable to listen on main accept port %d: errno = %d, %s", server.accept_addr.host_order_port(), errno,
              strerror(errno));
      goto Lretry;
    }
  } else {
  Lretry:
    if ((res = server.listen(non_blocking, opt))) {
      Warning("unable to listen on port %d: %d %d, %s", server.accept_addr.host_order_port(), res, errno, strerror(errno));
    }
  }

  return res;
}

int
NetAccept::do_blocking_accept(EThread *t)
{
  int res                = 0;
  int loop               = accept_till_done;
  UnixNetVConnection *vc = nullptr;
  Connection con;
  con.sock_type = SOCK_STREAM;

  // do-while for accepting all the connections
  // added by YTS Team, yamsat
  do {
    if ((res = server.accept(&con)) < 0) {
      int seriousness = accept_error_seriousness(res);
      switch (seriousness) {
      case 0:
        // bad enough to warn about
        check_transient_accept_error(res);
        safe_delay(net_throttle_delay);
        return 0;
      case 1:
        // not so bad but needs delay
        safe_delay(net_throttle_delay);
        return 0;
      case 2:
        // ignore
        return 0;
      case -1:
        [[fallthrough]];
      default:
        if (!action_->cancelled) {
          SCOPED_MUTEX_LOCK(lock, action_->mutex ? action_->mutex : t->mutex, t);
          action_->continuation->handleEvent(EVENT_ERROR, (void *)static_cast<intptr_t>(res));
          Warning("accept thread received fatal error: errno = %d", errno);
        }
        return -1;
      }
    }
    // check for throttle
    if (check_net_throttle(ACCEPT)) {
      check_throttle_warning(ACCEPT);
      // close the connection as we are in throttle state
      con.close();
      NET_SUM_DYN_STAT(net_connections_throttled_in_stat, 1);
      continue;
    }

    if (TSSystemState::is_event_system_shut_down()) {
      return -1;
    }

    NET_SUM_GLOBAL_DYN_STAT(net_tcp_accept_stat, 1);

    // Use 'nullptr' to Bypass thread allocator
    vc = (UnixNetVConnection *)this->getNetProcessor()->allocate_vc(nullptr);
    if (unlikely(!vc)) {
      return -1;
    }

    NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, 1);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->set_remote_addr(con.addr);
    vc->submit_time = Thread::get_hrtime();
    vc->action_     = *action_;
    vc->set_is_transparent(opt.f_inbound_transparent);
    vc->set_is_proxy_protocol(opt.f_proxy_protocol);
    vc->options.sockopt_flags        = opt.sockopt_flags;
    vc->options.packet_mark          = opt.packet_mark;
    vc->options.packet_tos           = opt.packet_tos;
    vc->options.packet_notsent_lowat = opt.packet_notsent_lowat;
    vc->options.ip_family            = opt.ip_family;
    vc->apply_options();
    vc->set_context(NET_VCONNECTION_IN);
    if (opt.f_mptcp) {
      vc->set_mptcp_state(); // Try to get the MPTCP state, and update accordingly
    }
    vc->accept_object = this;
#ifdef USE_EDGE_TRIGGER
    // Set the vc as triggered and place it in the read ready queue later in case there is already data on the socket.
    if (server.http_accept_filter) {
      vc->read.triggered = 1;
    }
#endif
    SET_CONTINUATION_HANDLER(vc, &UnixNetVConnection::acceptEvent);

    EThread *localt = eventProcessor.assign_thread(opt.etype);
    NetHandler *h   = get_NetHandler(localt);
    // Assign NetHandler->mutex to NetVC
    vc->mutex = h->mutex;
    localt->schedule_imm(vc);
  } while (loop);

  return 1;
}

int
NetAccept::acceptEvent(int event, void *ep)
{
  (void)event;
  Event *e = static_cast<Event *>(ep);
  // PollDescriptor *pd = get_PollDescriptor(e->ethread);
  Ptr<ProxyMutex> m;

  if (action_->mutex) {
    m = action_->mutex;
  } else {
    m = mutex;
  }

  MUTEX_TRY_LOCK(lock, m, e->ethread);
  if (lock.is_locked()) {
    if (action_->cancelled) {
      e->cancel();
      NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
      delete this;
      return EVENT_DONE;
    }

    int res;
    if ((res = accept_fn(this, e, false)) < 0) {
      NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
      /* INKqa11179 */
      Warning("Accept on port %d failed with error no %d", ats_ip_port_host_order(&server.addr), res);
      Warning("Traffic Server may be unable to accept more network"
              "connections on %d",
              ats_ip_port_host_order(&server.addr));
      e->cancel();
      delete this;
      return EVENT_DONE;
    }
    //}
  }
  return EVENT_CONT;
}

int
NetAccept::acceptFastEvent(int event, void *ep)
{
  Event *e = static_cast<Event *>(ep);
  (void)event;
  (void)e;
  int bufsz, res = 0;
  Connection con;
  con.sock_type = SOCK_STREAM;

  UnixNetVConnection *vc = nullptr;
  int loop               = accept_till_done;

  do {
    socklen_t sz = sizeof(con.addr);
    int fd       = socketManager.accept4(server.fd, &con.addr.sa, &sz, SOCK_NONBLOCK | SOCK_CLOEXEC);
    con.fd       = fd;

    if (likely(fd >= 0)) {
      // check for throttle
      if (check_net_throttle(ACCEPT)) {
        // close the connection as we are in throttle state
        con.close();
        NET_SUM_DYN_STAT(net_connections_throttled_in_stat, 1);
        continue;
      }
      Debug("iocore_net", "accepted a new socket: %d", fd);
      NET_SUM_GLOBAL_DYN_STAT(net_tcp_accept_stat, 1);
      if (opt.send_bufsize > 0) {
        if (unlikely(socketManager.set_sndbuf_size(fd, opt.send_bufsize))) {
          bufsz = ROUNDUP(opt.send_bufsize, 1024);
          while (bufsz > 0) {
            if (!socketManager.set_sndbuf_size(fd, bufsz)) {
              break;
            }
            bufsz -= 1024;
          }
        }
      }
      if (opt.recv_bufsize > 0) {
        if (unlikely(socketManager.set_rcvbuf_size(fd, opt.recv_bufsize))) {
          bufsz = ROUNDUP(opt.recv_bufsize, 1024);
          while (bufsz > 0) {
            if (!socketManager.set_rcvbuf_size(fd, bufsz)) {
              break;
            }
            bufsz -= 1024;
          }
        }
      }
    } else {
      res = fd;
    }
    // check return value from accept()
    if (res < 0) {
      Debug("iocore_net", "received : %s", strerror(errno));
      res = -errno;
      if (res == -EAGAIN || res == -ECONNABORTED
#if defined(linux)
          || res == -EPIPE
#endif
      ) {
        goto Ldone;
      } else if (accept_error_seriousness(res) >= 0) {
        check_transient_accept_error(res);
        goto Ldone;
      }
      if (!action_->cancelled) {
        action_->continuation->handleEvent(EVENT_ERROR, (void *)static_cast<intptr_t>(res));
      }
      goto Lerror;
    }

    vc = (UnixNetVConnection *)this->getNetProcessor()->allocate_vc(e->ethread);
    ink_release_assert(vc);

    NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, 1);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->set_remote_addr(con.addr);
    vc->submit_time = Thread::get_hrtime();
    vc->action_     = *action_;
    vc->set_is_transparent(opt.f_inbound_transparent);
    vc->set_is_proxy_protocol(opt.f_proxy_protocol);
    vc->options.sockopt_flags        = opt.sockopt_flags;
    vc->options.packet_mark          = opt.packet_mark;
    vc->options.packet_tos           = opt.packet_tos;
    vc->options.packet_notsent_lowat = opt.packet_notsent_lowat;
    vc->options.ip_family            = opt.ip_family;
    vc->apply_options();
    vc->set_context(NET_VCONNECTION_IN);
    if (opt.f_mptcp) {
      vc->set_mptcp_state(); // Try to get the MPTCP state, and update accordingly
    }

#ifdef USE_EDGE_TRIGGER
    // Set the vc as triggered and place it in the read ready queue later in case there is already data on the socket.
    if (server.http_accept_filter) {
      vc->read.triggered = 1;
    }
#endif
    SET_CONTINUATION_HANDLER(vc, &UnixNetVConnection::acceptEvent);

    EThread *t    = e->ethread;
    NetHandler *h = get_NetHandler(t);
    // Assign NetHandler->mutex to NetVC
    vc->mutex = h->mutex;
    // We must be holding the lock already to do later do_io_read's
    SCOPED_MUTEX_LOCK(lock, vc->mutex, e->ethread);
    vc->handleEvent(EVENT_NONE, nullptr);
    vc = nullptr;
  } while (loop);

Ldone:
  return EVENT_CONT;

Lerror:
  server.close();
  e->cancel();
  NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
  delete this;
  return EVENT_DONE;
}

int
NetAccept::acceptLoopEvent(int event, Event *e)
{
  (void)event;
  (void)e;
  EThread *t = this_ethread();

  while (do_blocking_accept(t) >= 0) {
    ;
  }

  // Don't think this ever happens ...
  NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
  delete this;
  return EVENT_DONE;
}

//
// Accept Event handler
//
//

NetAccept::NetAccept(const NetProcessor::AcceptOptions &_opt) : Continuation(nullptr), opt(_opt) {}

//
// Stop listening.  When the next poll takes place, an error will result.
// THIS ONLY WORKS WITH POLLING STYLE ACCEPTS!
//
void
NetAccept::cancel()
{
  action_->cancel();
  server.close();
}

NetAccept *
NetAccept::clone() const
{
  NetAccept *na;
  na  = new NetAccept(opt);
  *na = *this;
  return na;
}

NetProcessor *
NetAccept::getNetProcessor() const
{
  return &netProcessor;
}
