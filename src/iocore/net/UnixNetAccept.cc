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

#include "P_NetAccept.h"
#include "P_Net.h"
#include "P_UnixNet.h"
#include "P_UnixNetVConnection.h"
#include "iocore/eventsystem/Watchdog.h"
#include "iocore/net/ConnectionTracker.h"
#include "iocore/net/NetHandler.h"
#include "records/RecHttp.h"
#include "tscore/TSSystemState.h"
#include "tscore/ink_inet.h"
#include "tscore/ink_defs.h"

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
 * @param[in] exempt Whether @a addr is in the client exempt list, which the per client limit does not apply to.
 * @param[out] conn_track_group The connection tracker group associated with the
 * new incoming connection if connections are being tracked.
 *
 * @return true if the connection should be accepted, false otherwise.
 */
bool
handle_max_client_connections(IpEndpoint const &addr, bool exempt, std::shared_ptr<ConnectionTracker::Group> &conn_track_group)
{
  int const client_max = NetHandler::get_per_client_max_connections_in();
  if (client_max > 0 && !exempt) {
    auto       inbound_tracker = ConnectionTracker::obtain_inbound(addr);
    auto const tracked_count   = inbound_tracker.reserve();
    if (tracked_count > client_max) {
      // close the connection as we are in per client connection throttle state
      inbound_tracker.release();
      inbound_tracker.blocked();
      inbound_tracker.Warn_Blocked(client_max, 0, tracked_count - 1, addr,
                                   dbg_ctl_iocore_net_accept.on() ? &dbg_ctl_iocore_net_accept : nullptr);
      Metrics::Counter::increment(net_rsb.per_client_connections_throttled_in);
      Dbg(dbg_ctl_iocore_net_accepts, "Blocking a client connection due to per client connection limit.");
      return false;
    }
    conn_track_group = inbound_tracker.drop();
  }
  return true;
}

/// Count an accepted client connection, and whether it is from the client exempt list.
void
count_client_connection(UnixNetVConnection *vc, bool exempt)
{
  Metrics::Gauge::increment(net_rsb.connections_currently_open);
  vc->set_client_exempt(exempt);
  if (exempt) {
    Metrics::Counter::increment(net_rsb.per_client_connections_exempt_in);
    Metrics::Gauge::increment(net_rsb.per_client_connections_exempt_currently_open);
    Dbg(dbg_ctl_iocore_net_accepts, "Accepted a client connection from an address in the exempt list.");
  }
}

/** Whether this acceptor should leave the connections queued on its listen socket for now.
 *
 * True while the peak awake span has reached
 * @c proxy.config.exec_thread.watchdog.timeout_refuse_ms, where the queued connections cost nothing at
 * all and the threads may yet catch up and serve them. Independent of the accept queue shrink, which
 * has its own threshold (see @c watchdog_apply_backlog) and lets the *kernel* shed while ATS keeps
 * accepting - a bounded queue only drains as fast as we call @c accept4, which is what keeps inflow
 * near the rate we can actually serve. Both can be in force at once, in which case the queue is
 * bounded and nothing is draining it.
 *
 * A listener marked @c f_watchdog_exempt is never subject to backpressure. Those are the loopback
 * companion listeners opened alongside each proxy port, so local connections keep getting a
 * truthful answer about the proxy however far behind the event threads are. The volume of local
 * connections is small against the inbound load that caused the saturation.
 *
 * @param[in] na The acceptor.
 *
 * @return true if the caller should not drain its listen socket on this pass.
 */
bool
watchdog_should_refuse(NetAccept *na)
{
  return !na->opt.f_watchdog_exempt && Watchdog::accept_refuse();
}

/** Resize @a na's kernel accept queue to the depth the watchdog is currently asking for.
 *
 * Calling @c listen again on a listening socket updates its backlog. Shrinking it does not evict
 * connections already queued - they drain normally - but it does stop the kernel admitting more until
 * the depth falls back under the new limit, which is the backpressure we want. The kernel then sheds on
 * our behalf at no per-connection cost to ATS, and keeps doing so even when no ATS thread is scheduled
 * to run an accept loop. On Linux that means silently dropping new SYNs rather than refusing them, so
 * shed clients retransmit with backoff instead of seeing an error.
 *
 * Called at the top of each accept pass rather than from the monitor thread: the monitor has no access
 * to the listen descriptors without taking @c naVecMutex, and it is deliberately kept clear of every
 * event system lock. One syscall per acceptor per transition, none in the steady state.
 *
 * @param[in,out] na The acceptor whose listen socket should be resized.
 */
void
watchdog_apply_backlog(NetAccept *na)
{
  int want_permille = 1000;
  if (!na->opt.f_watchdog_exempt) {
    want_permille = Watchdog::backlog_scale_permille();
  }

  if (want_permille == na->applied_backlog_permille || !na->server.sock.is_ok()) {
    return;
  }

  int const configured = get_listen_backlog();
  int const target     = std::max(1, configured * want_permille / 1000);

  if (safe_listen(na->server.sock.get_fd(), target) < 0) {
    // Report and give up on this acceptor rather than retrying every pass. Losing the resize means
    // this listener keeps its previous queue depth, which is a degradation and not a failure.
    Warning("watchdog could not resize listen backlog on port %d to %d (%d permille of %d): %s",
            ats_ip_port_host_order(&na->server.accept_addr), target, want_permille, configured, strerror(errno));
    na->applied_backlog_permille = want_permille;
    return;
  }

  Dbg(dbg_ctl_iocore_net_accept, "resized listen backlog on port %d to %d (%d permille of %d)",
      ats_ip_port_host_order(&na->server.accept_addr), target, want_permille, configured);
  na->applied_backlog_permille = want_permille;

  // The absolute depth now in force, for operators who cannot derive it from the percentage alone -
  // when proxy.config.net.listen_backlog is -1 the baseline is the resolved somaxconn, which is not
  // readable from configuration. Every non-exempt acceptor resizes to the same target, so whichever
  // writes last writes the same value.
  Metrics::Gauge::store(net_rsb.watchdog_listen_backlog, target);
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

  watchdog_apply_backlog(na);

  if (watchdog_should_refuse(na)) {
    // The queued connections are left in the kernel accept queue and cost us nothing at all. This
    // continuation is scheduled on a period, so the next tick picks them up once the threads recover.
    // Nothing here is charged to the event loop, which is what distinguishes leaving them queued from
    // proxy.config.net.connections_throttle, where a shed connection is still accepted and closed.
    Metrics::Counter::increment(net_rsb.watchdog_accepts_refused);
    return 0;
  }

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
      if (na->action_->is_listening()) {
        if (!blockable) {
          na->action_->continuation->handleEvent(EVENT_ERROR, reinterpret_cast<void *>(res));
        } else {
          SCOPED_MUTEX_LOCK(lock, na->action_->mutex, e->ethread);
          na->action_->continuation->handleEvent(EVENT_ERROR, reinterpret_cast<void *>(res));
        }
      }
      count = res;
      goto Ldone;
    }

    Metrics::Counter::increment(net_rsb.tcp_accept);

    bool const                                exempt = ConnectionTracker::is_client_exempt(con.addr);
    std::shared_ptr<ConnectionTracker::Group> conn_track_group;
    if (!handle_max_client_connections(con.addr, exempt, conn_track_group)) {
      con.close();
      continue;
    }

    vc = static_cast<UnixNetVConnection *>(na->getNetProcessor()->allocate_vc(e->ethread));
    if (!vc) {
      goto Ldone; // note: @a con will clean up the socket when it goes out of scope.
    }
    vc->enable_inbound_connection_tracking(std::move(conn_track_group));

    count++;
    count_client_connection(vc, exempt);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->set_remote_addr(con.addr);
    vc->submit_time = ink_get_hrtime();
    vc->action_     = *na->action_;
    vc->set_is_transparent(na->opt.f_inbound_transparent);
    vc->set_is_proxy_protocol(na->opt.f_proxy_protocol, na->opt.f_proxy_protocol_client_src);
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
      t = eventProcessor.assign_thread(ET_NET);
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
  // reschedule accepting to the end of the thread event queue
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
bool
NetAccept::init_accept_loop()
{
  int    i, n;
  char   thr_name[MAX_THREAD_NAME_LENGTH];
  size_t stacksize;
  if (do_blocking_listen()) {
    return false;
  }
  stacksize = RecGetRecordInt("proxy.config.thread.default.stacksize").value_or(0);
  SET_CONTINUATION_HANDLER(this, &NetAccept::acceptLoopEvent);

  n = opt.accept_threads;
  // Fill in accept thread from configuration if necessary.
  if (n < 0) {
    n = RecGetRecordInt("proxy.config.accept_threads").value_or(0);
  }

  for (i = 0; i < n; i++) {
    NetAccept *a = (i < n - 1) ? clone() : this;
    snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[ACCEPT %d:%d]", i, ats_ip_port_host_order(&server.accept_addr));
    eventProcessor.spawn_thread(a, thr_name, stacksize);
    Dbg(dbg_ctl_iocore_net_accept_start, "Created accept thread #%d for port %d", i + 1,
        ats_ip_port_host_order(&server.accept_addr));
  }

  return true;
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
    t = eventProcessor.assign_thread(ET_NET);
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
  listen_per_thread     = RecGetRecordInt("proxy.config.exec_thread.listen").value_or(0);

  // The watchdog-exempt loopback listener does not follow proxy.config.exec_thread.listen, and instead
  // always shares one socket across every thread.
  //
  // Per-thread listeners exist to remove contention on a single accept queue, which matters at
  // high connection rates. This listener sees only a low volume of local connections, so there is no
  // contention to remove - and the SO_REUSEPORT group it would join
  // selects a socket by hashing the connection's four-tuple, which is blind to what the owning thread
  // is doing. A connection could then be steered to a thread that is mid-stall while every other thread
  // sits idle, which is precisely the failure the exemption exists to prevent.
  //
  // One shared socket registered in every thread's poll set behaves the opposite way: EPOLLEXCLUSIVE
  // wakes a single thread that is currently blocked in the poll, and a thread that is behind on work
  // is not blocked in the poll, so it is not a candidate. The connection lands on a thread that is
  // demonstrably free, and is served there.
  if (listen_per_thread == 1 && !opt.f_watchdog_exempt) {
    if (ats_is_unix(server.accept_addr)) {
      auto id = this_ethread()->id;
      ats_unix_append_id(&server.accept_addr.sun, id);
    }

    if (do_listen()) {
      Fatal("[NetAccept::accept_per_thread]:error listening on ports");
      return -1;
    }
  }

  SET_HANDLER(&NetAccept::acceptFastEvent);
  PollDescriptor *pd = get_PollDescriptor(this_ethread());
  if (this->ep.start(pd, this, EVENTIO_READ) < 0) {
    Fatal("[NetAccept::accept_per_thread]:error starting EventIO");
    return -1;
  }
  return 0;
}

bool
NetAccept::init_accept_per_thread()
{
  int i, n;
  int listen_per_thread = 0;

  listen_per_thread = RecGetRecordInt("proxy.config.exec_thread.listen").value_or(0);

  // The watchdog-exempt loopback listener deliberately ignores exec_thread.listen and always uses a
  // single shared socket - see NetAccept::accept_per_thread() for why.
  if (listen_per_thread == 0 || opt.f_watchdog_exempt) {
    if (do_listen()) {
      if (opt.f_non_fatal_listen) {
        return false;
      }
      Fatal("[NetAccept::accept_per_thread]:error listenting on ports");
      return false;
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

  return true;
}

void
NetAccept::stop_accept()
{
  action_->cancel();
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

  if (res == 0 && !opt.f_watchdog_exempt) {
    // Publish the baseline so the gauge reads the depth actually in force from startup, rather than
    // zero until the watchdog first resizes something.
    Metrics::Gauge::store(net_rsb.watchdog_listen_backlog, get_listen_backlog());
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

  watchdog_apply_backlog(this);

  if (watchdog_should_refuse(this)) {
    // Dedicated accept thread - acceptLoopEvent calls straight back in, so sleep rather than spin
    // while the connections wait in the kernel accept queue.
    Metrics::Counter::increment(net_rsb.watchdog_accepts_refused);
    safe_delay(net_throttle_delay);
    return 0;
  }

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
        if (action_->is_listening()) {
          SCOPED_MUTEX_LOCK(lock, action_->mutex ? action_->mutex : t->mutex, t);
          action_->continuation->handleEvent(EVENT_ERROR, reinterpret_cast<void *>(res));
          Warning("accept thread received fatal error: errno = %d", errno);
        }
        return -1;
      }
    }
    bool const exempt = ConnectionTracker::is_client_exempt(con.addr);

    // check for throttle
    if (check_net_accept_throttle(exempt)) {
      check_throttle_warning(ACCEPT);
      // close the connection as we are in throttle state
      con.close();
      Metrics::Counter::increment(net_rsb.connections_throttled_in);
      continue;
    }
    std::shared_ptr<ConnectionTracker::Group> conn_track_group;
    if (!handle_max_client_connections(con.addr, exempt, conn_track_group)) {
      con.close();
      continue;
    }

    if (TSSystemState::is_event_system_shut_down()) {
      return -1;
    }

    Metrics::Counter::increment(net_rsb.tcp_accept);

    // Use 'nullptr' to Bypass thread allocator
    vc = static_cast<UnixNetVConnection *>(this->getNetProcessor()->allocate_vc(nullptr));
    if (unlikely(!vc)) {
      return -1;
    }
    vc->enable_inbound_connection_tracking(std::move(conn_track_group));

    count++;
    count_client_connection(vc, exempt);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->set_remote_addr(con.addr);
    vc->submit_time = ink_get_hrtime();
    vc->action_     = *action_;
    vc->set_is_transparent(opt.f_inbound_transparent);
    vc->set_is_proxy_protocol(opt.f_proxy_protocol, opt.f_proxy_protocol_client_src);
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

    EThread    *localt = eventProcessor.assign_thread(ET_NET);
    NetHandler *h      = get_NetHandler(localt);
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
      // Server was already closed by whoever called cancel().
      e->cancel();
      Metrics::Gauge::decrement(net_rsb.accepts_currently_open);
      delete this;
      return EVENT_DONE;
    }

    int res;
    if ((res = net_accept(this, e, false)) < 0) {
      action_->cancel();
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

  if (e == refuse_retry) {
    refuse_retry = nullptr;
  }

  watchdog_apply_backlog(this);

  // See the equivalent comment in net_accept(). The listen fd here is registered edge triggered, so
  // refusing must come back on a timer: returning without draining would not produce another
  // readiness event for the connections already queued. New arrivals still wake us while refusing,
  // so only arm the timer if one is not already pending.
  if (watchdog_should_refuse(this)) {
    Metrics::Counter::increment(net_rsb.watchdog_accepts_refused);
    if (refuse_retry == nullptr) {
      refuse_retry = t->schedule_in(this, HRTIME_MSECONDS(net_throttle_delay));
    }
    return EVENT_CONT;
  }

  // An arrival beat the retry timer to it, so this pass does the drain the timer was armed for.
  if (refuse_retry != nullptr) {
    refuse_retry->cancel(this);
    refuse_retry = nullptr;
  }

  do {
    socklen_t  sz = sizeof(con.addr);
    UnixSocket sock{-1};
    if (int res{server.sock.accept4(&con.addr.sa, &sz, SOCK_NONBLOCK | SOCK_CLOEXEC)}; res >= 0) {
      sock = UnixSocket{res};
    }
    con.sock = sock;
    std::shared_ptr<ConnectionTracker::Group> conn_track_group;
    bool                                      exempt = false;

    if (likely(sock.is_ok())) {
      exempt = ConnectionTracker::is_client_exempt(con.addr);

      // check for throttle
      if (check_net_accept_throttle(exempt)) {
        // close the connection as we are in throttle state
        con.close();
        Metrics::Counter::increment(net_rsb.connections_throttled_in);
        continue;
      }
      if (!handle_max_client_connections(con.addr, exempt, conn_track_group)) {
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
      if (action_->is_listening()) {
        action_->continuation->handleEvent(EVENT_ERROR, reinterpret_cast<void *>(res));
      }
      goto Lerror;
    }

    vc = static_cast<UnixNetVConnection *>(this->getNetProcessor()->allocate_vc(e->ethread));
    ink_release_assert(vc);
    vc->enable_inbound_connection_tracking(std::move(conn_track_group));

    count++;
    count_client_connection(vc, exempt);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->set_remote_addr(con.addr);
    vc->submit_time = ink_get_hrtime();
    vc->action_     = *action_;
    vc->set_is_transparent(opt.f_inbound_transparent);
    vc->set_is_proxy_protocol(opt.f_proxy_protocol, opt.f_proxy_protocol_client_src);
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
  action_->cancel();
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
  action_->cancel();
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
