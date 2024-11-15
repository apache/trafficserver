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

#include "iocore/net/ConnectionTracker.h"
#include "P_Net.h"
#include "tscore/ink_inet.h"
#if TS_USE_NUMA
#include <numa.h>
#endif

using NetAcceptHandler = int (NetAccept::*)(int, void *);

namespace
{

DbgCtl dbg_ctl_iocore_net{"iocore_net"};
DbgCtl dbg_ctl_iocore_net_accept_start{"iocore_net_accept_start"};
DbgCtl dbg_ctl_iocore_net_accepts{"iocore_net_accepts"};
DbgCtl dbg_ctl_iocore_net_accept{"iocore_net_accept"};

/** Check and handle if the number of client connections exceeds the configured max.
 *
 * @param[in] addr The client address of the new incoming connection.
 * @param[out] conn_track_group The connection tracker group associated with the
 * new incoming connection if connections are being tracked.
 *
 * @return true if the connection should be accepted, false otherwise.
 */
bool
handle_max_client_connections(IpEndpoint const &addr, std::shared_ptr<ConnectionTracker::Group> &conn_track_group)
{
  int const client_max = NetHandler::get_per_client_max_connections_in();
  if (client_max > 0) {
    auto       inbound_tracker = ConnectionTracker::obtain_inbound(addr);
    auto const tracked_count   = inbound_tracker.reserve();
    if (tracked_count > client_max) {
      // close the connection as we are in per client connection throttle state
      inbound_tracker.release();
      inbound_tracker.blocked();
      inbound_tracker.Warn_Blocked(client_max, 0, tracked_count - 1, addr,
                                   dbg_ctl_iocore_net_accept.on() ? &dbg_ctl_iocore_net_accept : nullptr);
      Metrics::Counter::increment(net_rsb.per_client_connections_throttled_in);
      return false;
    }
    conn_track_group = inbound_tracker.drop();
  }
  return true;
}

} // end anonymous namespace

static void
safe_delay(int msec)
{
  UnixSocket::poll(nullptr, 0, msec);
}

//
// General case network connection accept code
//
int
net_accept(NetAccept *na, void *ep, bool blockable)
{
  Event              *e     = static_cast<Event *>(ep);
  int                 res   = 0;
  int                 count = 0;
  UnixNetVConnection *vc    = nullptr;
  Connection          con;

  int additional_accepts = NetHandler::get_additional_accepts();

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
      if (na->server.sock.is_ok() && !na->action_->cancelled) {
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
    Metrics::Counter::increment(net_rsb.tcp_accept);

    std::shared_ptr<ConnectionTracker::Group> conn_track_group;
    if (!handle_max_client_connections(con.addr, conn_track_group)) {
      con.close();
      continue;
    }

    vc = static_cast<UnixNetVConnection *>(na->getNetProcessor()->allocate_vc(e->ethread));
    if (!vc) {
      goto Ldone; // note: @a con will clean up the socket when it goes out of scope.
    }
    vc->enable_inbound_connection_tracking(conn_track_group);

    count++;
    Metrics::Gauge::increment(net_rsb.connections_currently_open);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->set_remote_addr(con.addr);
    vc->submit_time = ink_get_hrtime();
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

    EThread    *t;
    NetHandler *h;
    if (e->ethread->is_event_type(ET_NET)) {
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
#if TS_USE_NUMA
      int optVal;
      int optLen = sizeof(int);
      safe_getsockopt(vc->con.sock.get_fd(), SOL_SOCKET, SO_INCOMING_CPU, (char *)&optVal, &optLen);
      t = eventProcessor.assign_thread(ET_NET, numa_node_of_cpu(optVal));
#else
      t = eventProcessor.assign_thread(ET_NET, -1);
#endif
      h = get_NetHandler(t);
      // Assign NetHandler->mutex to NetVC
      vc->mutex = h->mutex;
      t->schedule_imm(vc);
    }
  } while (count < additional_accepts);

Ldone:
  if (!blockable) {
    MUTEX_UNTAKE_LOCK(na->action_->mutex, e->ethread);
  }

  // if we stop looping as a result of hitting the accept limit,
  // resechedule accepting to the end of the thread event queue
  // for the goal of fairness between accepting and other work
  Dbg(dbg_ctl_iocore_net_accepts, "exited accept loop - count: %d, limit: %d", count, additional_accepts);
  if (count >= additional_accepts) {
    this_ethread()->schedule_imm_local(na);
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
  int    i, n;
  char   thr_name[MAX_THREAD_NAME_LENGTH];
  size_t stacksize;
  if (do_blocking_listen()) {
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
    Dbg(dbg_ctl_iocore_net_accept_start, "Created accept thread #%d for port %d", i + 1,
        ats_ip_port_host_order(&server.accept_addr));
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
    unsigned int cpu = 0, node = 0;
    getcpu(&cpu, &node);
    t = eventProcessor.assign_thread(ET_NET, node);
  }

  if (!action_->continuation->mutex) {
    action_->continuation->mutex = t->mutex;
    action_->mutex               = t->mutex;
  }

  if (do_listen()) {
    return;
  }

  SET_HANDLER(&NetAccept::acceptEvent);
  period = -HRTIME_MSECONDS(net_accept_period);
  t->schedule_every(this, period);
}

int
NetAccept::accept_per_thread(int /* event ATS_UNUSED */, void * /* ep ATS_UNUSED */)
{
  int listen_per_thread = 0;
  REC_ReadConfigInteger(listen_per_thread, "proxy.config.exec_thread.listen");

  if (listen_per_thread == 1) {
    if (do_listen()) {
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

  REC_ReadConfigInteger(listen_per_thread, "proxy.config.exec_thread.listen");

  if (listen_per_thread == 0) {
    if (do_listen()) {
      Fatal("[NetAccept::accept_per_thread]:error listenting on ports");
      return;
    }
  }

  SET_HANDLER(&NetAccept::accept_per_thread);
  n = eventProcessor.thread_group[ET_NET]._count;

  for (i = 0; i < n; i++) {
    NetAccept *a = (i < n - 1) ? clone() : this;
    EThread   *t = eventProcessor.thread_group[ET_NET]._thread[i];
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
NetAccept::do_listen()
{
  // non-blocking
  return this->do_listen_impl(true);
}

int
NetAccept::do_blocking_listen()
{
  return this->do_listen_impl(false);
}

int
NetAccept::do_listen_impl(bool non_blocking)
{
  int res = 0;

  if (server.sock.is_ok()) {
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
  int                 res = 0;
  UnixNetVConnection *vc  = nullptr;
  Connection          con;
  con.sock_type = SOCK_STREAM;

  int count              = 0;
  int additional_accepts = NetHandler::get_additional_accepts();

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
      Metrics::Counter::increment(net_rsb.connections_throttled_in);
      continue;
    }
    std::shared_ptr<ConnectionTracker::Group> conn_track_group;
    if (!handle_max_client_connections(con.addr, conn_track_group)) {
      con.close();
      continue;
    }

    if (TSSystemState::is_event_system_shut_down()) {
      return -1;
    }

    Metrics::Counter::increment(net_rsb.tcp_accept);

    // Use 'nullptr' to Bypass thread allocator
    vc = (UnixNetVConnection *)this->getNetProcessor()->allocate_vc(nullptr);
    if (unlikely(!vc)) {
      return -1;
    }
    vc->enable_inbound_connection_tracking(conn_track_group);

    count++;
    Metrics::Gauge::increment(net_rsb.connections_currently_open);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->set_remote_addr(con.addr);
    vc->submit_time = ink_get_hrtime();
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

#if TS_USE_NUMA
    int optVal = 0;
    int optLen = sizeof(int);
    int err    = safe_getsockopt(vc->con.sock.get_fd(), SOL_SOCKET, SO_INCOMING_CPU, (char *)&optVal, &optLen);
    if (err < 0) {
      Warning("Unable to get SO_INCOMING_CPU, using round robin to assign thread");
    }
    EThread *localt = eventProcessor.assign_thread(ET_NET, err == 0 ? numa_node_of_cpu(optVal) : -1);
#else
    EThread *localt = eventProcessor.assign_thread(ET_NET, -1);
#endif
    NetHandler *h = get_NetHandler(localt);
    // Assign NetHandler->mutex to NetVC
    vc->mutex = h->mutex;
    localt->schedule_imm(vc);
  } while (count < additional_accepts);

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
      Metrics::Gauge::decrement(net_rsb.accepts_currently_open);
      delete this;
      return EVENT_DONE;
    }

    int res;
    if ((res = accept_fn(this, e, false)) < 0) {
      Metrics::Gauge::decrement(net_rsb.accepts_currently_open);
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
  int        bufsz, res = 0;
  Connection con;
  con.sock_type = SOCK_STREAM;

  UnixNetVConnection *vc                 = nullptr;
  int                 count              = 0;
  EThread            *t                  = e->ethread;
  NetHandler         *h                  = get_NetHandler(t);
  int                 additional_accepts = NetHandler::get_additional_accepts();

  do {
    socklen_t  sz = sizeof(con.addr);
    UnixSocket sock{-1};
    if (int res{server.sock.accept4(&con.addr.sa, &sz, SOCK_NONBLOCK | SOCK_CLOEXEC)}; res >= 0) {
      sock = UnixSocket{res};
    }
    con.sock = sock;
    std::shared_ptr<ConnectionTracker::Group> conn_track_group;

    if (likely(sock.is_ok())) {
      // check for throttle
      if (check_net_throttle(ACCEPT)) {
        // close the connection as we are in throttle state
        con.close();
        Metrics::Counter::increment(net_rsb.connections_throttled_in);
        continue;
      }
      if (!handle_max_client_connections(con.addr, conn_track_group)) {
        con.close();
        continue;
      }
      Dbg(dbg_ctl_iocore_net, "accepted a new socket: %d", sock.get_fd());
      Metrics::Counter::increment(net_rsb.tcp_accept);
      if (opt.send_bufsize > 0) {
        if (unlikely(sock.set_sndbuf_size(opt.send_bufsize))) {
          bufsz = ROUNDUP(opt.send_bufsize, 1024);
          while (bufsz > 0) {
            if (!sock.set_sndbuf_size(bufsz)) {
              break;
            }
            bufsz -= 1024;
          }
        }
      }
      if (opt.recv_bufsize > 0) {
        if (unlikely(sock.set_rcvbuf_size(opt.recv_bufsize))) {
          bufsz = ROUNDUP(opt.recv_bufsize, 1024);
          while (bufsz > 0) {
            if (!sock.set_rcvbuf_size(bufsz)) {
              break;
            }
            bufsz -= 1024;
          }
        }
      }
    } else {
      res = sock.get_fd();
    }
    // check return value from accept()
    if (res < 0) {
      Dbg(dbg_ctl_iocore_net, "received : %s", strerror(errno));
      res = -errno;
      if (res == -EAGAIN || res == -ECONNABORTED
#if defined(__linux__)
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
    vc->enable_inbound_connection_tracking(conn_track_group);

    count++;
    Metrics::Gauge::increment(net_rsb.connections_currently_open);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->set_remote_addr(con.addr);
    vc->submit_time = ink_get_hrtime();
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

    // Assign NetHandler->mutex to NetVC
    vc->mutex = h->mutex;
    // We must be holding the lock already to do later do_io_read's
    SCOPED_MUTEX_LOCK(lock, vc->mutex, e->ethread);
    vc->handleEvent(EVENT_NONE, nullptr);
    vc = nullptr;
  } while (count < additional_accepts);

Ldone:
  // if we stop looping as a result of hitting the accept limit,
  // resechedule accepting to the end of the thread event queue
  // for the goal of fairness between accepting and other work
  Dbg(dbg_ctl_iocore_net_accepts, "exited accept loop - count: %d, limit: %d", count, additional_accepts);
  if (count >= additional_accepts) {
    this_ethread()->schedule_imm_local(this);
  }
  return EVENT_CONT;

Lerror:
  server.close();
  e->cancel();
  Metrics::Gauge::decrement(net_rsb.accepts_currently_open);
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
  Metrics::Gauge::decrement(net_rsb.accepts_currently_open);
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
