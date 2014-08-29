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

#include "P_Net.h"

#ifdef ROUNDUP
#undef ROUNDUP
#endif
#define ROUNDUP(x, y) ((((x)+((y)-1))/(y))*(y))

typedef int (NetAccept::*NetAcceptHandler) (int, void *);
volatile int dummy_volatile = 0;
int accept_till_done = 1;

static void
safe_delay(int msec)
{
  socketManager.poll(0, 0, msec);
}


//
// Send the throttling message to up to THROTTLE_AT_ONCE connections,
// delaying to let some of the current connections complete
//
static int
send_throttle_message(NetAccept * na)
{
  struct pollfd afd;
  Connection con[100];
  char dummy_read_request[4096];

  afd.fd = na->server.fd;
  afd.events = POLLIN;

  int n = 0;
  while (check_net_throttle(ACCEPT, ink_get_hrtime()) && n < THROTTLE_AT_ONCE - 1
         && (socketManager.poll(&afd, 1, 0) > 0)) {
    int res = 0;
    if ((res = na->server.accept(&con[n])) < 0)
      return res;
    n++;
  }
  safe_delay(NET_THROTTLE_DELAY / 2);
  int i = 0;
  for (i = 0; i < n; i++) {
    socketManager.read(con[i].fd, dummy_read_request, 4096);
    socketManager.write(con[i].fd, unix_netProcessor.throttle_error_message,
                        strlen(unix_netProcessor.throttle_error_message));
  }
  safe_delay(NET_THROTTLE_DELAY / 2);
  for (i = 0; i < n; i++)
    con[i].close();
  return 0;
}


//
// General case network connection accept code
//
int
net_accept(NetAccept * na, void *ep, bool blockable)
{
  Event *e = (Event *) ep;
  int res = 0;
  int count = 0;
  int loop = accept_till_done;
  UnixNetVConnection *vc = NULL;

  if (!blockable)
    if (!MUTEX_TAKE_TRY_LOCK_FOR(na->action_->mutex, e->ethread, na->action_->continuation))
      return 0;
  //do-while for accepting all the connections
  //added by YTS Team, yamsat
  do {
    vc = (UnixNetVConnection *) na->alloc_cache;
    if (!vc) {
      vc = (UnixNetVConnection *)na->getNetProcessor()->allocate_vc(e->ethread);
      NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, 1);
      vc->id = net_next_connection_number();
      na->alloc_cache = vc;
    }
    if ((res = na->server.accept(&vc->con)) < 0) {
      if (res == -EAGAIN || res == -ECONNABORTED || res == -EPIPE)
        goto Ldone;
      if (na->server.fd != NO_FD && !na->action_->cancelled) {
        if (!blockable)
          na->action_->continuation->handleEvent(EVENT_ERROR, (void *)(intptr_t)res);
        else {
          MUTEX_LOCK(lock, na->action_->mutex, e->ethread);
          na->action_->continuation->handleEvent(EVENT_ERROR, (void *)(intptr_t)res);
        }
      }
      count = res;
      goto Ldone;
    }
    count++;
    na->alloc_cache = NULL;

    vc->submit_time = ink_get_hrtime();
    ats_ip_copy(&vc->server_addr, &vc->con.addr);
    vc->mutex = new_ProxyMutex();
    vc->action_ = *na->action_;
    vc->set_is_transparent(na->server.f_inbound_transparent);
    vc->closed  = 0;
    SET_CONTINUATION_HANDLER(vc, (NetVConnHandler) & UnixNetVConnection::acceptEvent);

    if (e->ethread->is_event_type(na->etype))
      vc->handleEvent(EVENT_NONE, e);
    else
      eventProcessor.schedule_imm(vc, na->etype);
  } while (loop);

Ldone:
  if (!blockable)
    MUTEX_UNTAKE_LOCK(na->action_->mutex, e->ethread);
  return count;
}

//
// Initialize the NetAccept for execution in its own thread.
// This should be done for low latency, high connection rate sockets.
//
void
NetAccept::init_accept_loop(const char *thr_name)
{
  size_t stacksize;

  REC_ReadConfigInteger(stacksize, "proxy.config.thread.default.stacksize");
  SET_CONTINUATION_HANDLER(this, &NetAccept::acceptLoopEvent);
  eventProcessor.spawn_thread(this, thr_name, stacksize);
}


//
// Initialize the NetAccept for execution in a etype thread.
// This should be done for low connection rate sockets.
// (Management, Cluster, etc.)  Also, since it adapts to the
// number of connections arriving, it should be reasonable to
// use it for high connection rates as well.
//
void
NetAccept::init_accept(EThread * t)
{
  if (!t)
    t = eventProcessor.assign_thread(etype);

  if (!action_->continuation->mutex) {
    action_->continuation->mutex = t->mutex;
    action_->mutex = t->mutex;
  }
  if (do_listen(NON_BLOCKING))
    return;
  SET_HANDLER((NetAcceptHandler) & NetAccept::acceptEvent);
  period = ACCEPT_PERIOD;
  t->schedule_every(this, period, etype);
}


void
NetAccept::init_accept_per_thread()
{
  int i, n;

  if (do_listen(NON_BLOCKING))
    return;
  if (accept_fn == net_accept)
    SET_HANDLER((NetAcceptHandler) & NetAccept::acceptFastEvent);
  else
    SET_HANDLER((NetAcceptHandler) & NetAccept::acceptEvent);
  period = ACCEPT_PERIOD;

  NetAccept *a;
  n = eventProcessor.n_threads_for_type[ET_NET];
  for (i = 0; i < n; i++) {
    if (i < n - 1)
      a = clone();
    else
      a = this;
    EThread *t = eventProcessor.eventthread[ET_NET][i];
    PollDescriptor *pd = get_PollDescriptor(t);
    if (a->ep.start(pd, a, EVENTIO_READ) < 0)
      Warning("[NetAccept::init_accept_per_thread]:error starting EventIO");
    a->mutex = get_NetHandler(t)->mutex;
    t->schedule_every(a, period, etype);
  }
}

int
NetAccept::do_listen(bool non_blocking, bool transparent)
{
  int res = 0;

  if (server.fd != NO_FD) {
    if ((res = server.setup_fd_for_listen(non_blocking, recv_bufsize, send_bufsize, transparent))) {

      Warning("unable to listen on main accept port %d: errno = %d, %s", ntohs(server.accept_addr.port()), errno, strerror(errno));
      goto Lretry;
    }
  } else {
  Lretry:
    if ((res = server.listen(non_blocking, recv_bufsize, send_bufsize, transparent)))
      Warning("unable to listen on port %d: %d %d, %s", ntohs(server.accept_addr.port()), res, errno, strerror(errno));
  }
  if (callback_on_open && !action_->cancelled) {
    if (res)
      action_->continuation->handleEvent(NET_EVENT_ACCEPT_FAILED, this);
    else
      action_->continuation->handleEvent(NET_EVENT_ACCEPT_SUCCEED, this);
    mutex = NULL;
  }
  return res;
}

int
NetAccept::do_blocking_accept(EThread * t)
{
  int res = 0;
  int loop = accept_till_done;
  UnixNetVConnection *vc = NULL;
  Connection con;

  //do-while for accepting all the connections
  //added by YTS Team, yamsat
  do {
    ink_hrtime now = ink_get_hrtime();

    // Throttle accepts

    while (!backdoor && check_net_throttle(ACCEPT, now)) {
      check_throttle_warning();
      if (!unix_netProcessor.throttle_error_message) {
        safe_delay(NET_THROTTLE_DELAY);
      } else if (send_throttle_message(this) < 0) {
        goto Lerror;
      }
      now = ink_get_hrtime();
    }

    if ((res = server.accept(&con)) < 0) {
    Lerror:
      int seriousness = accept_error_seriousness(res);
      if (seriousness >= 0) {   // not so bad
        if (!seriousness)       // bad enough to warn about
          check_transient_accept_error(res);
        safe_delay(NET_THROTTLE_DELAY);
        return 0;
      }
      if (!action_->cancelled) {
        MUTEX_LOCK(lock, action_->mutex, t);
        action_->continuation->handleEvent(EVENT_ERROR, (void *)(intptr_t)res);
        MUTEX_UNTAKE_LOCK(action_->mutex, t);
        Warning("accept thread received fatal error: errno = %d", errno);
      }
      return -1;
    }

#if TS_HAS_SO_MARK
      if (packet_mark != 0) {
        safe_setsockopt(con.fd, SOL_SOCKET, SO_MARK, reinterpret_cast<char *>(&packet_mark), sizeof(uint32_t));
      }
#endif

#if TS_HAS_IP_TOS
      if (packet_tos != 0) {
        safe_setsockopt(con.fd, IPPROTO_IP, IP_TOS, reinterpret_cast<char *>(&packet_tos), sizeof(uint32_t));
      }
#endif

    // Use 'NULL' to Bypass thread allocator
    vc = (UnixNetVConnection *)this->getNetProcessor()->allocate_vc(NULL);
    if (!vc) {
      con.close();
      return -1;
    }
    vc->con = con;
    vc->from_accept_thread = true;
    vc->id = net_next_connection_number();
    alloc_cache = NULL;

    check_emergency_throttle(con);

    NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, 1);
    vc->submit_time = now;
    ats_ip_copy(&vc->server_addr, &vc->con.addr);
    vc->set_is_transparent(server.f_inbound_transparent);
    vc->mutex = new_ProxyMutex();
    vc->action_ = *action_;
    SET_CONTINUATION_HANDLER(vc, (NetVConnHandler) & UnixNetVConnection::acceptEvent);
    //eventProcessor.schedule_imm(vc, getEtype());
    eventProcessor.schedule_imm_signal(vc, getEtype());
  } while (loop);

  return 1;
}


int
NetAccept::acceptEvent(int event, void *ep)
{
  (void) event;
  Event *e = (Event *) ep;
  //PollDescriptor *pd = get_PollDescriptor(e->ethread);
  ProxyMutex *m = 0;

  if (action_->mutex)
    m = action_->mutex;
  else
    m = mutex;
  MUTEX_TRY_LOCK(lock, m, e->ethread);
  if (lock) {
    if (action_->cancelled) {
      e->cancel();
      NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
      delete this;
      return EVENT_DONE;
    }

    //ink_assert(ifd < 0 || event == EVENT_INTERVAL || (pd->nfds > ifd && pd->pfd[ifd].fd == server.fd));
    //if (ifd < 0 || event == EVENT_INTERVAL || (pd->pfd[ifd].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) {
    //ink_assert(!"incomplete");
      int res;
      if ((res = accept_fn(this, e, false)) < 0) {
        NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
        /* INKqa11179 */
        Warning("Accept on port %d failed with error no %d",
          ats_ip_port_host_order(&server.addr), res
        );
        Warning("Traffic Server may be unable to accept more network" "connections on %d",
          ats_ip_port_host_order(&server.addr)
        );
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
  Event *e = (Event *) ep;
  (void) event;
  (void) e;
  int bufsz, res;
  Connection con;

  PollDescriptor *pd = get_PollDescriptor(e->ethread);
  UnixNetVConnection *vc = NULL;
  int loop = accept_till_done;

  do {
    if (!backdoor && check_net_throttle(ACCEPT, ink_get_hrtime())) {
      ifd = -1;
      return EVENT_CONT;
    }

    socklen_t sz = sizeof(con.addr);
    int fd = socketManager.accept(server.fd, &con.addr.sa, &sz);
    con.fd = fd;

    if (likely(fd >= 0)) {
      Debug("iocore_net", "accepted a new socket: %d", fd);
      if (send_bufsize > 0) {
        if (unlikely(socketManager.set_sndbuf_size(fd, send_bufsize))) {
          bufsz = ROUNDUP(send_bufsize, 1024);
          while (bufsz > 0) {
            if (!socketManager.set_sndbuf_size(fd, bufsz))
              break;
            bufsz -= 1024;
          }
        }
      }
      if (recv_bufsize > 0) {
        if (unlikely(socketManager.set_rcvbuf_size(fd, recv_bufsize))) {
          bufsz = ROUNDUP(recv_bufsize, 1024);
          while (bufsz > 0) {
            if (!socketManager.set_rcvbuf_size(fd, bufsz))
              break;
            bufsz -= 1024;
          }
        }
      }
      if (sockopt_flags & 1) {  // we have to disable Nagle
        safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, SOCKOPT_ON, sizeof(int));
        Debug("socket", "::acceptFastEvent: setsockopt() TCP_NODELAY on socket");
      }
      if (sockopt_flags & 2) {
        safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, SOCKOPT_ON, sizeof(int));
        Debug("socket", "::acceptFastEvent: setsockopt() SO_KEEPALIVE on socket");
      }
#if TS_HAS_SO_MARK
      if (packet_mark != 0) {
        safe_setsockopt(fd, SOL_SOCKET, SO_MARK, reinterpret_cast<char *>(&packet_mark), sizeof(uint32_t));
      }
#endif

#if TS_HAS_IP_TOS
      if (packet_tos != 0) {
        safe_setsockopt(fd, IPPROTO_IP, IP_TOS, reinterpret_cast<char *>(&packet_tos), sizeof(uint32_t));
      }
#endif
      do {
        res = safe_nonblocking(fd);
      } while (res < 0 && (errno == EAGAIN || errno == EINTR));

      vc = (UnixNetVConnection *)this->getNetProcessor()->allocate_vc(e->ethread);
      if (!vc) {
        con.close();
        goto Ldone;
      }

      vc->con = con;

    } else {
      res = fd;
    }
    if (res < 0) {
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
      if (!action_->cancelled)
        action_->continuation->handleEvent(EVENT_ERROR, (void *)(intptr_t)res);
      goto Lerror;
    }

    NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, 1);
    vc->id = net_next_connection_number();

    vc->submit_time = ink_get_hrtime();
    ats_ip_copy(&vc->server_addr, &vc->con.addr);
    vc->set_is_transparent(server.f_inbound_transparent);
    vc->mutex = new_ProxyMutex();
    vc->thread = e->ethread;

    vc->nh = get_NetHandler(e->ethread);

    SET_CONTINUATION_HANDLER(vc, (NetVConnHandler) & UnixNetVConnection::mainEvent);

    if (vc->ep.start(pd, vc, EVENTIO_READ|EVENTIO_WRITE) < 0) {
      Warning("[NetAccept::acceptFastEvent]: Error in inserting fd[%d] in kevent\n", vc->con.fd);
      close_UnixNetVConnection(vc, e->ethread);
      return EVENT_DONE;
    }

    vc->nh->open_list.enqueue(vc);

#ifdef USE_EDGE_TRIGGER
    // Set the vc as triggered and place it in the read ready queue in case there is already data on the socket.
    Debug("iocore_net", "acceptEvent : Setting triggered and adding to the read ready queue");
    vc->read.triggered = 1;
    vc->nh->read_ready_list.enqueue(vc);
#endif

    if (!action_->cancelled)
      action_->continuation->handleEvent(NET_EVENT_ACCEPT, vc);
    else
      close_UnixNetVConnection(vc, e->ethread);
  } while (loop);

Ldone:
  return EVENT_CONT;

Lerror:
  server.close();
  e->cancel();
  if (vc)
    vc->free(e->ethread);
  NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
  delete this;
  return EVENT_DONE;
}


int
NetAccept::acceptLoopEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  EThread *t = this_ethread();

  while (1)
    do_blocking_accept(t);

  // Don't think this ever happens ...
  NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
  delete this;
  return EVENT_DONE;
}


//
// Accept Event handler
//
//

NetAccept::NetAccept()
  : Continuation(NULL),
    period(0),
    alloc_cache(0),
    ifd(-1),
    callback_on_open(false),
    backdoor(false),
    recv_bufsize(0),
    send_bufsize(0),
    sockopt_flags(0),
    packet_mark(0),
    packet_tos(0),
    etype(0)
{ }


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
  na = new NetAccept;
  *na = *this;
  return na;
}

// Virtual function allows the correct
// etype to be used in NetAccept functions (ET_SSL
// or ET_NET).
EventType NetAccept::getEtype() const
{
  return etype;
}

NetProcessor *
NetAccept::getNetProcessor() const
{
  return &netProcessor;
}
