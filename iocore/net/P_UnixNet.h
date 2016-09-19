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

#ifndef __P_UNIXNET_H__
#define __P_UNIXNET_H__

#include "ts/ink_platform.h"

#define USE_EDGE_TRIGGER_EPOLL 1
#define USE_EDGE_TRIGGER_KQUEUE 1
#define USE_EDGE_TRIGGER_PORT 1

#define EVENTIO_NETACCEPT 1
#define EVENTIO_READWRITE_VC 2
#define EVENTIO_DNS_CONNECTION 3
#define EVENTIO_UDP_CONNECTION 4
#define EVENTIO_ASYNC_SIGNAL 5

#if TS_USE_EPOLL
#ifdef USE_EDGE_TRIGGER_EPOLL
#define USE_EDGE_TRIGGER 1
#define EVENTIO_READ (EPOLLIN | EPOLLET)
#define EVENTIO_WRITE (EPOLLOUT | EPOLLET)
#else
#define EVENTIO_READ EPOLLIN
#define EVENTIO_WRITE EPOLLOUT
#endif
#define EVENTIO_ERROR (EPOLLERR | EPOLLPRI | EPOLLHUP)
#endif

#if TS_USE_KQUEUE
#ifdef USE_EDGE_TRIGGER_KQUEUE
#define USE_EDGE_TRIGGER 1
#define INK_EV_EDGE_TRIGGER EV_CLEAR
#else
#define INK_EV_EDGE_TRIGGER 0
#endif
#define EVENTIO_READ INK_EVP_IN
#define EVENTIO_WRITE INK_EVP_OUT
#define EVENTIO_ERROR (0x010 | 0x002 | 0x020) // ERR PRI HUP
#endif
#if TS_USE_PORT
#ifdef USE_EDGE_TRIGGER_PORT
#define USE_EDGE_TRIGGER 1
#endif
#define EVENTIO_READ POLLIN
#define EVENTIO_WRITE POLLOUT
#define EVENTIO_ERROR (POLLERR | POLLPRI | POLLHUP)
#endif

struct PollDescriptor;
typedef PollDescriptor *EventLoop;

class UnixNetVConnection;
class UnixUDPConnection;
struct DNSConnection;
struct NetAccept;
struct EventIO {
  int fd;
#if TS_USE_KQUEUE || TS_USE_EPOLL && !defined(USE_EDGE_TRIGGER) || TS_USE_PORT
  int events;
#endif
  EventLoop event_loop;
  int type;
  union {
    Continuation *c;
    UnixNetVConnection *vc;
    DNSConnection *dnscon;
    NetAccept *na;
    UnixUDPConnection *uc;
  } data;
  int start(EventLoop l, DNSConnection *vc, int events);
  int start(EventLoop l, NetAccept *vc, int events);
  int start(EventLoop l, UnixNetVConnection *vc, int events);
  int start(EventLoop l, UnixUDPConnection *vc, int events);
  int start(EventLoop l, int fd, Continuation *c, int events);
  // Change the existing events by adding modify(EVENTIO_READ)
  // or removing modify(-EVENTIO_READ), for level triggered I/O
  int modify(int events);
  // Refresh the existing events (i.e. KQUEUE EV_CLEAR), for edge triggered I/O
  int refresh(int events);
  int stop();
  int close();
  EventIO()
  {
    type   = 0;
    data.c = 0;
  }
};

#include "P_UnixNetProcessor.h"
#include "P_UnixNetVConnection.h"
#include "P_NetAccept.h"
#include "P_DNSConnection.h"
#include "P_UnixUDPConnection.h"
#include "P_UnixPollDescriptor.h"

class UnixNetVConnection;
class NetHandler;
typedef int (NetHandler::*NetContHandler)(int, void *);
typedef unsigned int uint32;

extern ink_hrtime last_throttle_warning;
extern ink_hrtime last_shedding_warning;
extern ink_hrtime emergency_throttle_time;
extern int net_connections_throttle;
extern int fds_throttle;
extern int fds_limit;
extern ink_hrtime last_transient_accept_error;
extern int http_accept_port_number;

//#define INACTIVITY_TIMEOUT
//
// Configuration Parameter had to move here to share
// between UnixNet and UnixUDPNet or SSLNet modules.
// Design notes are in Memo.NetDesign
//

#define THROTTLE_FD_HEADROOM (128 + 64) // CACHE_DB_FDS + 64

#define TRANSIENT_ACCEPT_ERROR_MESSAGE_EVERY HRTIME_HOURS(24)

// also the 'throttle connect headroom'
#define THROTTLE_AT_ONCE 5
#define EMERGENCY_THROTTLE 16
#define HYPER_EMERGENCY_THROTTLE 6

#define NET_THROTTLE_ACCEPT_HEADROOM 1.1  // 10%
#define NET_THROTTLE_CONNECT_HEADROOM 1.0 // 0%
#define NET_THROTTLE_MESSAGE_EVERY HRTIME_MINUTES(10)

#define PRINT_IP(x) ((uint8_t *)&(x))[0], ((uint8_t *)&(x))[1], ((uint8_t *)&(x))[2], ((uint8_t *)&(x))[3]

// function prototype needed for SSLUnixNetVConnection
unsigned int net_next_connection_number();

struct PollCont : public Continuation {
  NetHandler *net_handler;
  PollDescriptor *pollDescriptor;
  PollDescriptor *nextPollDescriptor;
  int poll_timeout;

  PollCont(ProxyMutex *m, int pt = net_config_poll_timeout);
  PollCont(ProxyMutex *m, NetHandler *nh, int pt = net_config_poll_timeout);
  ~PollCont();
  int pollEvent(int event, Event *e);
};

//
// NetHandler
//
// A NetHandler handles the Network IO operations.  It maintains
// lists of operations at multiples of it's periodicity.
//
class NetHandler : public Continuation
{
public:
  Event *trigger_event;
  QueM(UnixNetVConnection, NetState, read, ready_link) read_ready_list;
  QueM(UnixNetVConnection, NetState, write, ready_link) write_ready_list;
  Que(UnixNetVConnection, link) open_list;
  DList(UnixNetVConnection, cop_link) cop_list;
  ASLLM(UnixNetVConnection, NetState, read, enable_link) read_enable_list;
  ASLLM(UnixNetVConnection, NetState, write, enable_link) write_enable_list;
  Que(UnixNetVConnection, keep_alive_queue_link) keep_alive_queue;
  uint32_t keep_alive_queue_size;
  Que(UnixNetVConnection, active_queue_link) active_queue;
  uint32_t active_queue_size;
  uint32_t max_connections_per_thread_in;
  uint32_t max_connections_active_per_thread_in;

  // configuration settings for managing the active and keep-alive queues
  uint32_t max_connections_in;
  uint32_t max_connections_active_in;
  uint32_t inactive_threashold_in;
  uint32_t transaction_no_activity_timeout_in;
  uint32_t keep_alive_no_activity_timeout_in;

  time_t sec;
  int cycles;

  int startNetEvent(int event, Event *data);
  int mainNetEvent(int event, Event *data);
  int mainNetEventExt(int event, Event *data);
  void process_enabled_list(NetHandler *);
  void manage_keep_alive_queue();
  bool manage_active_queue(bool ignore_queue_size);
  void add_to_keep_alive_queue(UnixNetVConnection *vc);
  void remove_from_keep_alive_queue(UnixNetVConnection *vc);
  bool add_to_active_queue(UnixNetVConnection *vc);
  void remove_from_active_queue(UnixNetVConnection *vc);
  void configure_per_thread();

  NetHandler();

private:
  void _close_vc(UnixNetVConnection *vc, ink_hrtime now, int &handle_event, int &closed, int &total_idle_time,
                 int &total_idle_count);
};

static inline NetHandler *
get_NetHandler(EThread *t)
{
  return (NetHandler *)ETHREAD_GET_PTR(t, unix_netProcessor.netHandler_offset);
}
static inline PollCont *
get_PollCont(EThread *t)
{
  return (PollCont *)ETHREAD_GET_PTR(t, unix_netProcessor.pollCont_offset);
}
static inline PollDescriptor *
get_PollDescriptor(EThread *t)
{
  PollCont *p = get_PollCont(t);
  return p->pollDescriptor;
}

enum ThrottleType {
  ACCEPT,
  CONNECT,
};

TS_INLINE int
net_connections_to_throttle(ThrottleType t)
{
  double headroom = t == ACCEPT ? NET_THROTTLE_ACCEPT_HEADROOM : NET_THROTTLE_CONNECT_HEADROOM;
  int64_t sval    = 0;

  NET_READ_GLOBAL_DYN_SUM(net_connections_currently_open_stat, sval);
  int currently_open = (int)sval;
  // deal with race if we got to multiple net threads
  if (currently_open < 0)
    currently_open = 0;
  return (int)(currently_open * headroom);
}

TS_INLINE void
check_shedding_warning()
{
  ink_hrtime t = Thread::get_hrtime();
  if (t - last_shedding_warning > NET_THROTTLE_MESSAGE_EVERY) {
    last_shedding_warning = t;
    RecSignalWarning(REC_SIGNAL_SYSTEM_ERROR, "number of connections reaching shedding limit");
  }
}

TS_INLINE bool
emergency_throttle(ink_hrtime now)
{
  return (bool)(emergency_throttle_time > now);
}

TS_INLINE bool
check_net_throttle(ThrottleType t, ink_hrtime now)
{
  int connections = net_connections_to_throttle(t);

  if (connections >= net_connections_throttle)
    return true;

  if (emergency_throttle(now))
    return true;

  return false;
}

TS_INLINE void
check_throttle_warning()
{
  ink_hrtime t = Thread::get_hrtime();
  if (t - last_throttle_warning > NET_THROTTLE_MESSAGE_EVERY) {
    last_throttle_warning = t;
    RecSignalWarning(REC_SIGNAL_SYSTEM_ERROR, "too many connections, throttling");
  }
}

//
// Emergency throttle when we are close to exhausting file descriptors.
// Block all accepts or connects for N seconds where N
// is the amount into the emergency fd stack squared
// (e.g. on the last file descriptor we have 14 * 14 = 196 seconds
// of emergency throttle).
//
// Hyper Emergency throttle when we are very close to exhausting file
// descriptors.  Close the connection immediately, the upper levels
// will recover.
//
TS_INLINE bool
check_emergency_throttle(Connection &con)
{
  int fd        = con.fd;
  int emergency = fds_limit - EMERGENCY_THROTTLE;
  if (fd > emergency) {
    int over                = fd - emergency;
    emergency_throttle_time = Thread::get_hrtime() + (over * over) * HRTIME_SECOND;
    RecSignalWarning(REC_SIGNAL_SYSTEM_ERROR, "too many open file descriptors, emergency throttling");
    int hyper_emergency = fds_limit - HYPER_EMERGENCY_THROTTLE;
    if (fd > hyper_emergency)
      con.close();
    return true;
  }
  return false;
}

TS_INLINE int
change_net_connections_throttle(const char *token, RecDataT data_type, RecData value, void *data)
{
  (void)token;
  (void)data_type;
  (void)value;
  (void)data;
  int throttle = fds_limit - THROTTLE_FD_HEADROOM;
  if (fds_throttle < 0)
    net_connections_throttle = throttle;
  else {
    net_connections_throttle = fds_throttle;
    if (net_connections_throttle > throttle)
      net_connections_throttle = throttle;
  }
  return 0;
}

// 1  - transient
// 0  - report as warning
// -1 - fatal
TS_INLINE int
accept_error_seriousness(int res)
{
  switch (res) {
  case -EAGAIN:
  case -ECONNABORTED:
  case -ECONNRESET: // for Linux
  case -EPIPE:      // also for Linux
    return 1;
  case -EMFILE:
  case -ENOMEM:
#if defined(ENOSR) && !defined(freebsd)
  case -ENOSR:
#endif
    ink_assert(!"throttling misconfigured: set too high");
#ifdef ENOBUFS
  case -ENOBUFS:
#endif
#ifdef ENFILE
  case -ENFILE:
#endif
    return 0;
  case -EINTR:
    ink_assert(!"should be handled at a lower level");
    return 0;
#if defined(EPROTO) && !defined(freebsd)
  case -EPROTO:
#endif
  case -EOPNOTSUPP:
  case -ENOTSOCK:
  case -ENODEV:
  case -EBADF:
  default:
    return -1;
  }
}

TS_INLINE void
check_transient_accept_error(int res)
{
  ink_hrtime t = Thread::get_hrtime();
  if (!last_transient_accept_error || t - last_transient_accept_error > TRANSIENT_ACCEPT_ERROR_MESSAGE_EVERY) {
    last_transient_accept_error = t;
    Warning("accept thread received transient error: errno = %d", -res);
#if defined(linux)
    if (res == -ENOBUFS || res == -ENFILE)
      Warning("errno : %d consider a memory upgrade", -res);
#endif
  }
}

//
// Disable a UnixNetVConnection
//
static inline void
read_disable(NetHandler *nh, UnixNetVConnection *vc)
{
#ifdef INACTIVITY_TIMEOUT
  if (vc->inactivity_timeout) {
    if (!vc->write.enabled) {
      vc->inactivity_timeout->cancel_action();
      vc->inactivity_timeout = NULL;
    }
  }
#else
  if (!vc->write.enabled) {
    vc->next_inactivity_timeout_at = 0;
    Debug("socket", "read_disable updating inactivity_at %" PRId64 ", NetVC=%p", vc->next_inactivity_timeout_at, vc);
  }
#endif
  vc->read.enabled = 0;
  nh->read_ready_list.remove(vc);
  vc->ep.modify(-EVENTIO_READ);
}

static inline void
write_disable(NetHandler *nh, UnixNetVConnection *vc)
{
#ifdef INACTIVITY_TIMEOUT
  if (vc->inactivity_timeout) {
    if (!vc->read.enabled) {
      vc->inactivity_timeout->cancel_action();
      vc->inactivity_timeout = NULL;
    }
  }
#else
  if (!vc->read.enabled) {
    vc->next_inactivity_timeout_at = 0;
    Debug("socket", "write_disable updating inactivity_at %" PRId64 ", NetVC=%p", vc->next_inactivity_timeout_at, vc);
  }
#endif
  vc->write.enabled = 0;
  nh->write_ready_list.remove(vc);
  vc->ep.modify(-EVENTIO_WRITE);
}

TS_INLINE int
EventIO::start(EventLoop l, DNSConnection *vc, int events)
{
  type = EVENTIO_DNS_CONNECTION;
  return start(l, vc->fd, (Continuation *)vc, events);
}
TS_INLINE int
EventIO::start(EventLoop l, NetAccept *vc, int events)
{
  type = EVENTIO_NETACCEPT;
  return start(l, vc->server.fd, (Continuation *)vc, events);
}
TS_INLINE int
EventIO::start(EventLoop l, UnixNetVConnection *vc, int events)
{
  type = EVENTIO_READWRITE_VC;
  return start(l, vc->con.fd, (Continuation *)vc, events);
}
TS_INLINE int
EventIO::start(EventLoop l, UnixUDPConnection *vc, int events)
{
  type = EVENTIO_UDP_CONNECTION;
  return start(l, vc->fd, (Continuation *)vc, events);
}
TS_INLINE int
EventIO::close()
{
  stop();
  switch (type) {
  default:
    ink_assert(!"case");
  case EVENTIO_DNS_CONNECTION:
    return data.dnscon->close();
    break;
  case EVENTIO_NETACCEPT:
    return data.na->server.close();
    break;
  case EVENTIO_READWRITE_VC:
    return data.vc->con.close();
    break;
  }
  return -1;
}

TS_INLINE int
EventIO::start(EventLoop l, int afd, Continuation *c, int e)
{
  data.c     = c;
  fd         = afd;
  event_loop = l;
#if TS_USE_EPOLL
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events   = e;
  ev.data.ptr = this;
#ifndef USE_EDGE_TRIGGER
  events = e;
#endif
  return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
#endif
#if TS_USE_KQUEUE
  events = e;
  struct kevent ev[2];
  int n = 0;
  if (e & EVENTIO_READ)
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  if (e & EVENTIO_WRITE)
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  return kevent(l->kqueue_fd, &ev[0], n, NULL, 0, NULL);
#endif
#if TS_USE_PORT
  events     = e;
  int retval = port_associate(event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
  Debug("iocore_eventio", "[EventIO::start] e(%d), events(%d), %d[%s]=port_associate(%d,%d,%d,%d,%p)", e, events, retval,
        retval < 0 ? strerror(errno) : "ok", event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
  return retval;
#endif
}

TS_INLINE int
EventIO::modify(int e)
{
  ink_assert(event_loop);
#if TS_USE_EPOLL && !defined(USE_EDGE_TRIGGER)
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  int new_events = events, old_events = events;
  if (e < 0)
    new_events &= ~(-e);
  else
    new_events |= e;
  events      = new_events;
  ev.events   = new_events;
  ev.data.ptr = this;
  if (!new_events)
    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, &ev);
  else if (!old_events)
    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  else
    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
#endif
#if TS_USE_KQUEUE && !defined(USE_EDGE_TRIGGER)
  int n = 0;
  struct kevent ev[2];
  int ee = events;
  if (e < 0) {
    ee &= ~(-e);
    if ((-e) & EVENTIO_READ)
      EV_SET(&ev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, this);
    if ((-e) & EVENTIO_WRITE)
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, this);
  } else {
    ee |= e;
    if (e & EVENTIO_READ)
      EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
    if (e & EVENTIO_WRITE)
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  }
  events = ee;
  if (n)
    return kevent(event_loop->kqueue_fd, &ev[0], n, NULL, 0, NULL);
  else
    return 0;
#endif
#if TS_USE_PORT
  int n  = 0;
  int ne = e;
  if (e < 0) {
    if (((-e) & events)) {
      ne = ~(-e) & events;
      if ((-e) & EVENTIO_READ)
        n++;
      if ((-e) & EVENTIO_WRITE)
        n++;
    }
  } else {
    if (!(e & events)) {
      ne = events | e;
      if (e & EVENTIO_READ)
        n++;
      if (e & EVENTIO_WRITE)
        n++;
    }
  }
  if (n && ne && event_loop) {
    events     = ne;
    int retval = port_associate(event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
    Debug("iocore_eventio", "[EventIO::modify] e(%d), ne(%d), events(%d), %d[%s]=port_associate(%d,%d,%d,%d,%p)", e, ne, events,
          retval, retval < 0 ? strerror(errno) : "ok", event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
    return retval;
  }
  return 0;
#else
  (void)e; // ATS_UNUSED
  return 0;
#endif
}

TS_INLINE int
EventIO::refresh(int e)
{
  ink_assert(event_loop);
#if TS_USE_KQUEUE && defined(USE_EDGE_TRIGGER)
  e = e & events;
  struct kevent ev[2];
  int n = 0;
  if (e & EVENTIO_READ)
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  if (e & EVENTIO_WRITE)
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  if (n)
    return kevent(event_loop->kqueue_fd, &ev[0], n, NULL, 0, NULL);
  else
    return 0;
#endif
#if TS_USE_PORT
  int n  = 0;
  int ne = e;
  if ((e & events)) {
    ne = events | e;
    if (e & EVENTIO_READ)
      n++;
    if (e & EVENTIO_WRITE)
      n++;
    if (n && ne && event_loop) {
      events     = ne;
      int retval = port_associate(event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
      Debug("iocore_eventio", "[EventIO::refresh] e(%d), ne(%d), events(%d), %d[%s]=port_associate(%d,%d,%d,%d,%p)", e, ne, events,
            retval, retval < 0 ? strerror(errno) : "ok", event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
      return retval;
    }
  }
  return 0;
#else
  (void)e; // ATS_UNUSED
  return 0;
#endif
}

TS_INLINE int
EventIO::stop()
{
  if (event_loop) {
    int retval = 0;
#if TS_USE_EPOLL
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    retval    = epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, &ev);
#endif
#if TS_USE_PORT
    retval = port_dissociate(event_loop->port_fd, PORT_SOURCE_FD, fd);
    Debug("iocore_eventio", "[EventIO::stop] %d[%s]=port_dissociate(%d,%d,%d)", retval, retval < 0 ? strerror(errno) : "ok",
          event_loop->port_fd, PORT_SOURCE_FD, fd);
#endif
    event_loop = NULL;
    return retval;
  }
  return 0;
}

#endif
