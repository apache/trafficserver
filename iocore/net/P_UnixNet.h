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

#include <stdarg.h>
#include "List.h"

class UnixNetVConnection;
class DNSConnection;
class NetAccept;
class UnixUDPConnection;
struct epoll_data_ptr
{
  int type;
  union
  {
    UnixNetVConnection *vc;
    DNSConnection *dnscon;
    NetAccept *na;
    UnixUDPConnection *uc;
  } data;
};

#include "P_UnixNetProcessor.h"
#include "P_UnixNetVConnection.h"
#include "P_NetAccept.h"
#include "P_DNSConnection.h"

#define EPOLL_NETACCEPT			1
#define EPOLL_READWRITE_VC		2
#define EPOLL_DNS_CONNECTION		3
#define EPOLL_UDP_CONNECTION		4

struct UnixNetVConnection;
struct NetHandler;
typedef int (NetHandler::*NetContHandler) (int, void *);
typedef unsigned int inku32;

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

#define THROTTLE_FD_HEADROOM                      (128 + 64)    // CACHE_DB_FDS + 64

#define TRANSIENT_ACCEPT_ERROR_MESSAGE_EVERY      HRTIME_HOURS(24)
#define ACCEPT_THREAD_POLL_TIMEOUT                100   // msecs
#define NET_PRIORITY_MSEC                         4
#define NET_PRIORITY_PERIOD                       HRTIME_MSECONDS(NET_PRIORITY_MSEC)
// p' = p + (p / NET_PRIORITY_LOWER)
#define NET_PRIORITY_LOWER                        2
// p' = p / NET_PRIORITY_HIGHER
#define NET_PRIORITY_HIGHER                       2
#define NET_RETRY_DELAY                           HRTIME_MSECONDS(10)
#define MAX_ACCEPT_PERIOD                         HRTIME_MSECONDS(100)

// also the 'throttle connect headroom'
#define THROTTLE_AT_ONCE                          5
#define EMERGENCY_THROTTLE                        16
#define HYPER_EMERGENCY_THROTTLE                  6

#define NET_THROTTLE_ACCEPT_HEADROOM              1.1   // 10%
#define NET_THROTTLE_CONNECT_HEADROOM             1.0   // 0%
#define NET_THROTTLE_MESSAGE_EVERY                HRTIME_MINUTES(10)
#define NET_PERIOD                                -HRTIME_MSECONDS(5)
#define ACCEPT_PERIOD                             -HRTIME_MSECONDS(4)
#define NET_INITIAL_PRIORITY                      0
#define MAX_NET_BUCKETS                           256
#define MAX_EPOLL_ARRAY_SIZE                      (1024*16)
#define MAX_EPOLL_TIMEOUT                         50    /* mseconds */
#define DEFAULT_EPOLL_TIMEOUT                     10    /* mseconds */
#define REAL_DEFAULT_EPOLL_TIMEOUT                3     /* the define above is old code [ebalsa] -- this directly effects latency of the connections. */

#define NET_THROTTLE_DELAY                        50    /* mseconds */
#define INK_MIN_PRIORITY                          0

#define PRINT_IP(x) ((inku8*)&(x))[0],((inku8*)&(x))[1], ((inku8*)&(x))[2],((inku8*)&(x))[3]


// function prototype needed for SSLUnixNetVConnection
unsigned int net_next_connection_number();

struct PollCont:public Continuation
{
  NetHandler *net_handler;
  PollDescriptor *pollDescriptor;
  PollDescriptor *nextPollDescriptor;
  int poll_timeout;

  PollCont(ProxyMutex * m);
  PollCont(ProxyMutex * m, NetHandler * nh);
  ~PollCont();
  int pollEvent(int event, Event * e);
};

//
// NetHandler
//
// A NetHandler handles the Network IO operations.  It maintains
// lists of operations at multiples of it's periodicity.
//
class NetHandler:public Continuation
{
public:
  Event *trigger_event;

  Que(UnixNetVConnection, read.ready_link) read_ready_list;
  Que(UnixNetVConnection, write.ready_link) write_ready_list;
  Que(UnixNetVConnection, link) open_list;
  Que(DNSConnection, link) dnsqueue;
  ASSL(UnixNetVConnection, read.enable_link) read_enable_list;
  ASSL(UnixNetVConnection, write.enable_link) write_enable_list;

  int startNetEvent(int event, Event * data);
  int mainNetEvent(int event, Event * data);
  int mainNetEventExt(int event, Event * data);
  void process_enabled_list(NetHandler *, EThread *);
  PollDescriptor *build_poll(PollDescriptor * pd);
  PollDescriptor *build_one_read_poll(int fd, UnixNetVConnection *, PollDescriptor * pd);
  PollDescriptor *build_one_write_poll(int fd, UnixNetVConnection *, PollDescriptor * pd);

  NetHandler();
};

static inline NetHandler *
get_NetHandler(EThread * t)
{
  return (NetHandler *) ETHREAD_GET_PTR(t, unix_netProcessor.netHandler_offset);
}
static inline PollCont *
get_PollCont(EThread * t)
{
  return (PollCont *) ETHREAD_GET_PTR(t, unix_netProcessor.pollCont_offset);
}
static inline PollDescriptor *
get_PollDescriptor(EThread * t)
{
  PollCont *p = get_PollCont(t);
  return p->pollDescriptor;
}


enum ThrottleType
{ ACCEPT, CONNECT };
INK_INLINE int
net_connections_to_throttle(ThrottleType t)
{

  double headroom = t == ACCEPT ? NET_THROTTLE_ACCEPT_HEADROOM : NET_THROTTLE_CONNECT_HEADROOM;
  ink64 sval = 0, cval = 0;

#ifdef HTTP_NET_THROTTLE
  NET_READ_DYN_STAT(http_current_client_connections_stat, cval, sval);
  int http_user_agents = sval;
  // one origin server connection for each user agent
  int http_use_estimate = http_user_agents * 2;
  // be conservative, bound by number currently open
  if (http_use_estimate > currently_open)
    return (int) (http_use_estimate * headroom);
#endif
  NET_READ_DYN_STAT(net_connections_currently_open_stat, cval, sval);
  int currently_open = (int) sval;
  // deal with race if we got to multiple net threads
  if (currently_open < 0)
    currently_open = 0;
  return (int) (currently_open * headroom);
}

INK_INLINE void
check_shedding_warning()
{
  ink_hrtime t = ink_get_hrtime();
  if (t - last_shedding_warning > NET_THROTTLE_MESSAGE_EVERY) {
    last_shedding_warning = t;
    IOCORE_SignalWarning(REC_SIGNAL_SYSTEM_ERROR, "number of connections reaching shedding limit");
  }
}

INK_INLINE int
emergency_throttle(ink_hrtime now)
{
  return emergency_throttle_time > now;
}

INK_INLINE int
check_net_throttle(ThrottleType t, ink_hrtime now)
{
  int connections = net_connections_to_throttle(t);
  if (connections >= net_connections_throttle)
    return true;

  if (emergency_throttle(now))
    return true;

  return false;
}

INK_INLINE void
check_throttle_warning()
{
  ink_hrtime t = ink_get_hrtime();
  if (t - last_throttle_warning > NET_THROTTLE_MESSAGE_EVERY) {
    last_throttle_warning = t;
    IOCORE_SignalWarning(REC_SIGNAL_SYSTEM_ERROR, "too many connections, throttling");

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
INK_INLINE int
check_emergency_throttle(Connection & con)
{
  int fd = con.fd;
  int emergency = fds_limit - EMERGENCY_THROTTLE;
  if (fd > emergency) {
    int over = fd - emergency;
    emergency_throttle_time = ink_get_hrtime() + (over * over) * HRTIME_SECOND;
    IOCORE_SignalWarning(REC_SIGNAL_SYSTEM_ERROR, "too many open file descriptors, emergency throttling");
    int hyper_emergency = fds_limit - HYPER_EMERGENCY_THROTTLE;
    if (fd > hyper_emergency)
      con.close();
    return true;
  }
  return false;
}


INK_INLINE int
change_net_connections_throttle(const char *token, RecDataT data_type, RecData value, void *data)
{
  (void) token;
  (void) data_type;
  (void) value;
  (void) data;
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
INK_INLINE int
accept_error_seriousness(int res)
{
  switch (res) {
  case -EAGAIN:
  case -ECONNABORTED:
  case -ECONNRESET:            // for Linux
  case -EPIPE:                 // also for Linux
    return 1;
  case -EMFILE:
  case -ENOMEM:
#if (HOST_OS != freebsd)
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
#if (HOST_OS != freebsd)
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

INK_INLINE void
check_transient_accept_error(int res)
{
  ink_hrtime t = ink_get_hrtime();
  if (!last_transient_accept_error || t - last_transient_accept_error > TRANSIENT_ACCEPT_ERROR_MESSAGE_EVERY) {
    last_transient_accept_error = t;
    Warning("accept thread received transient error: errno = %d", -res);
#if (HOST_OS == linux)
    if (res == -ENOBUFS || res == -ENFILE)
      Warning("errno : %d consider a memory upgrade", -res);
#endif
  }
}

//
// Disable a UnixNetVConnection
//
static inline void
read_disable(NetHandler * nh, UnixNetVConnection * vc)
{
#ifdef INACTIVITY_TIMEOUT
  if (vc->inactivity_timeout) {
    if (!vc->write.enabled) {
      vc->inactivity_timeout->cancel_action();
      vc->inactivity_timeout = NULL;
    }
  }
#else
  if (vc->next_inactivity_timeout_at)
    if (!vc->write.enabled)
      vc->next_inactivity_timeout_at = 0;
#endif
  vc->read.enabled = 0;
  nh->read_ready_list.remove(vc);
}

static inline void
write_disable(NetHandler * nh, UnixNetVConnection * vc)
{
#ifdef INACTIVITY_TIMEOUT
  if (vc->inactivity_timeout) {
    if (!vc->read.enabled) {
      vc->inactivity_timeout->cancel_action();
      vc->inactivity_timeout = NULL;
    }
  }
#else
  if (vc->next_inactivity_timeout_at) 
    if (!vc->read.enabled)
      vc->next_inactivity_timeout_at = 0;
#endif
  vc->write.enabled = 0;
  nh->write_ready_list.remove(vc);
}


#ifndef INACTIVITY_TIMEOUT
// INKqa10496
// One Inactivity cop runs on each thread once every second and
// loops through the list of NetVCs and calls the timeouts
struct InactivityCop:public Continuation
{
  InactivityCop(ProxyMutex * m):Continuation(m)
  {
    SET_HANDLER(&InactivityCop::check_inactivity);
  }
  int check_inactivity(int event, Event * e)
  {
    (void) event;
    ink_hrtime now = ink_get_hrtime();
    NetHandler *nh = get_NetHandler(this_ethread());
    UnixNetVConnection * vc = nh->open_list.head, *vc_next = 0;
    while (vc) {
      vc_next = (UnixNetVConnection*)vc->link.next;
      if (vc->inactivity_timeout_in && vc->next_inactivity_timeout_at && vc->next_inactivity_timeout_at < now)
        vc->handleEvent(EVENT_IMMEDIATE, e);
      else
        if (vc->closed)
          close_UnixNetVConnection(vc, e->ethread);
      vc = vc_next;
    }
    return 0;
  }
};
#endif
#endif
