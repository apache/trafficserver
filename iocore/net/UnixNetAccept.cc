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

void
safe_delay(int msec)
{
  socketManager.poll(0, 0, msec);
}


//
// Send the throttling message to up to THROTTLE_AT_ONCE connections,
// delaying to let some of the current connections complete
//
int
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
      vc = na->allocateThread(e->ethread);
      ProxyMutex *mutex = e->ethread->mutex;
      NET_INCREMENT_DYN_STAT(net_connections_currently_open_stat);
      vc->id = net_next_connection_number();
      na->alloc_cache = vc;
#if TS_USE_DETAILED_LOG
      vc->loggingInit();
#endif
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
    vc->ip = vc->con.sa.sin_addr.s_addr;
    vc->port = ntohs(vc->con.sa.sin_port);
    vc->accept_port = ntohs(na->server.sa.sin_port);
    vc->mutex = new_ProxyMutex();
    vc->action_ = *na->action_;
    vc->set_is_transparent(na->server.f_inbound_transparent);
    vc->set_is_other_side_transparent(na->server.f_outbound_transparent);
    Debug("http_tproxy", "Marking accepted %sconnection on %x as%s outbound transparent.\n",
	  na->server.f_inbound_transparent ? "transparent " : "",
	  na, na->server.f_outbound_transparent ? "" : " not"
	  );
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
// Special purpose MAIN proxy accept code
// Seperate accept thread function
//
int
net_accept_main_blocking(NetAccept * na, Event * e, bool blockable)
{
  (void) blockable;
  (void) e;

  EThread *t = this_ethread();

  while (1)
    na->do_blocking_accept(na, t);
  return -1;
}


UnixNetVConnection *
NetAccept::allocateThread(EThread * t)
{
  return ((UnixNetVConnection *)THREAD_ALLOC(netVCAllocator, t));
}

void
NetAccept::freeThread(UnixNetVConnection * vc, EThread * t)
{
  ink_assert(!vc->from_accept_thread);
  THREAD_FREE(vc, netVCAllocator, t);
}


// Virtual function allows the correct
// etype to be used in NetAccept functions (ET_SSL
// or ET_NET).
EventType NetAccept::getEtype()
{
  return (ET_NET);
}


//
// Initialize the NetAccept for execution in its own thread.
// This should be done for low latency, high connection rate sockets.
//
void
NetAccept::init_accept_loop()
{
  if (!action_->continuation->mutex) {
    action_->continuation->mutex = new_ProxyMutex();
    action_->mutex = action_->continuation->mutex;
  }
  SET_CONTINUATION_HANDLER(this, &NetAccept::acceptLoopEvent);
  eventProcessor.spawn_thread(this);
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
    if (i < n - 1) {
      a = NEW(new NetAccept);
      *a = *this;
    } else
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
NetAccept::do_listen(bool non_blocking)
{
  int res = 0;

  if (server.fd != NO_FD) {
    if ((res = server.setup_fd_for_listen(non_blocking, recv_bufsize, send_bufsize))) {
      Warning("unable to listen on main accept port %d: errno = %d, %s", port, errno, strerror(errno));
      goto Lretry;
    }
  } else {
  Lretry:
    if ((res = server.listen(port, non_blocking, recv_bufsize, send_bufsize)))
      Warning("unable to listen on port %d: %d %d, %s", port, res, errno, strerror(errno));
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
NetAccept::do_blocking_accept(NetAccept * master_na, EThread * t)
{
  int res = 0;
  int loop = accept_till_done;
  UnixNetVConnection *vc = NULL;

  //do-while for accepting all the connections
  //added by YTS Team, yamsat
  do {
    vc = (UnixNetVConnection *) master_na->alloc_cache;
    if (likely(!vc)) {
      vc = (UnixNetVConnection *)netVCAllocator.alloc(); // Don't use thread / proxy allocation
      vc->from_accept_thread = true;
      vc->id = net_next_connection_number();
      master_na->alloc_cache = vc;
#if TS_USE_DETAILED_LOG
      vc->loggingInit();
#endif
    }
    ink_hrtime now = ink_get_hrtime();

    // Throttle accepts

    while (check_net_throttle(ACCEPT, now)) {
      check_throttle_warning();
      if (!unix_netProcessor.throttle_error_message) {
        safe_delay(NET_THROTTLE_DELAY);
      } else if (send_throttle_message(this) < 0)
        goto Lerror;
    }

    if ((res = server.accept(&vc->con)) < 0) {
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
    check_emergency_throttle(vc->con);
    master_na->alloc_cache = NULL;

    RecIncrGlobalRawStatSum(net_rsb, net_connections_currently_open_stat, 1);
    vc->submit_time = now;
    vc->ip = vc->con.sa.sin_addr.s_addr;
    vc->port = ntohs(vc->con.sa.sin_port);
    vc->accept_port = ntohs(server.sa.sin_port);
    vc->set_is_transparent(master_na->server.f_inbound_transparent);
    vc->set_is_other_side_transparent(master_na->server.f_outbound_transparent);
    Debug("http_tproxy", "Marking accepted %sconnect on %x as%s outbound transparent.\n",
	  master_na->server.f_inbound_transparent ? "transparent " : "",
	  master_na, master_na->server.f_outbound_transparent ? "" : " not"
	  );
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

    //ink_debug_assert(ifd < 0 || event == EVENT_INTERVAL || (pd->nfds > ifd && pd->pfd[ifd].fd == server.fd));
    //if (ifd < 0 || event == EVENT_INTERVAL || (pd->pfd[ifd].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) {
    //ink_debug_assert(!"incomplete");
      int res;
      if ((res = accept_fn(this, e, false)) < 0) {
        NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
        /* INKqa11179 */
        Warning("Accept on port %d failed with error no %d", ntohs(server.sa.sin_port), res);
        Warning("Traffic Server may be unable to accept more network" "connections on %d", ntohs(server.sa.sin_port));
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

  PollDescriptor *pd = get_PollDescriptor(e->ethread);
  UnixNetVConnection *vc = NULL;
  int loop = accept_till_done;

  do {
    if (check_net_throttle(ACCEPT, ink_get_hrtime())) {
      ifd = -1;
      return EVENT_CONT;
    }
    vc = allocateThread(e->ethread);

    socklen_t sz = sizeof(vc->con.sa);
    int fd = socketManager.accept(server.fd, (struct sockaddr *) &vc->con.sa, &sz);

#if TS_USE_DETAILED_LOG
    vc->loggingInit();
#endif

    if (likely(fd >= 0)) {
      vc->addLogMessage("accepting the connection");

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
        safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ON, sizeof(int));
        Debug("socket", "::acceptFastEvent: setsockopt() TCP_NODELAY on socket");
      }
      if (sockopt_flags & 2) {
        safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, ON, sizeof(int));
        Debug("socket", "::acceptFastEvent: setsockopt() SO_KEEPALIVE on socket");
      }
      do {
        res = safe_nonblocking(fd);
      } while (res < 0 && (errno == EAGAIN || errno == EINTR));
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
        ink_assert(vc->con.fd == NO_FD);
        ink_assert(!vc->link.next && !vc->link.prev);
        freeThread(vc, e->ethread);
        goto Ldone;
      } else if (accept_error_seriousness(res) >= 0) {
        check_transient_accept_error(res);
        freeThread(vc, e->ethread);
        goto Ldone;
      }
      if (!action_->cancelled)
        action_->continuation->handleEvent(EVENT_ERROR, (void *)(intptr_t)res);
      goto Lerror;
    }
    vc->con.fd = fd;

    NET_INCREMENT_DYN_STAT(net_connections_currently_open_stat);
    vc->id = net_next_connection_number();

    vc->submit_time = ink_get_hrtime();
    vc->ip = vc->con.sa.sin_addr.s_addr;
    vc->port = ntohs(vc->con.sa.sin_port);
    vc->accept_port = ntohs(server.sa.sin_port);
    vc->set_is_transparent(server.f_inbound_transparent);
    vc->set_is_other_side_transparent(server.f_outbound_transparent);
    Debug("http_tproxy", "Marking fast accepted %sconnection on as%s outbound transparent.\n",
	  server.f_inbound_transparent ? "transparent " : "",
	  server.f_outbound_transparent ? "" : " not"
	  );
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
  freeThread(vc, e->ethread);
  NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
  delete this;
  return EVENT_DONE;
}


int
NetAccept::acceptLoopEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  while (1)
    if (net_accept_main_blocking(this, e, true) < 0)
      break;
  NET_DECREMENT_DYN_STAT(net_accepts_currently_open_stat);
  delete this;
  return EVENT_DONE;
}


//
// Accept Event handler
//
//

NetAccept::NetAccept():
Continuation(NULL),
port(0),
period(0),
alloc_cache(0),
ifd(-1), callback_on_open(false), recv_bufsize(0), send_bufsize(0), sockopt_flags(0), etype(0)
{
}


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
