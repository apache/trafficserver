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

#pragma once

#include <bitset>

#include "tscore/ink_platform.h"

#include "iocore/net/PollCont.h"
#include "iocore/net/EventIO.h"
#include "iocore/net/NetHandler.h"
#include "tscore/ink_platform.h"

#if TS_USE_LINUX_IO_URING
#include "iocore/io_uring/IOUringEventIO.h"
#endif

#include "../dns/P_DNSConnection.h"
#include "P_Net.h"
#include "P_NetAccept.h"
#include "P_UnixNetProcessor.h"
#include "P_UnixNetVConnection.h"
#include "P_UnixPollDescriptor.h"
#include <limits>
#include "tscore/ink_sys_control.h"

NetHandler *get_NetHandler(EThread *t);
PollCont *get_PollCont(EThread *t);
PollDescriptor *get_PollDescriptor(EThread *t);

using NetContHandler = int (NetHandler::*)(int, void *);
using uint32         = unsigned int;

extern ink_hrtime last_throttle_warning;
extern ink_hrtime last_shedding_warning;
extern ink_hrtime emergency_throttle_time;
extern int net_connections_throttle;
extern bool net_memory_throttle;
extern int fds_throttle;
extern ink_hrtime last_transient_accept_error;

//
// Configuration Parameter had to move here to share
// between UnixNet and UnixUDPNet or SSLNet modules.
// Design notes are in Memo.NetDesign
//

#define THROTTLE_FD_HEADROOM (128 + 64) // CACHE_DB_FDS + 64

#define TRANSIENT_ACCEPT_ERROR_MESSAGE_EVERY HRTIME_HOURS(24)

// also the 'throttle connect headroom'
#define EMERGENCY_THROTTLE       16
#define THROTTLE_AT_ONCE         5
#define HYPER_EMERGENCY_THROTTLE 6

#define NET_THROTTLE_ACCEPT_HEADROOM  1.1 // 10%
#define NET_THROTTLE_CONNECT_HEADROOM 1.0 // 0%
#define NET_THROTTLE_MESSAGE_EVERY    HRTIME_MINUTES(10)

#define PRINT_IP(x) ((uint8_t *)&(x))[0], ((uint8_t *)&(x))[1], ((uint8_t *)&(x))[2], ((uint8_t *)&(x))[3]

// function prototype needed for SSLUnixNetVConnection
unsigned int net_next_connection_number();

enum ThrottleType {
  ACCEPT,
  CONNECT,
};

TS_INLINE int
net_connections_to_throttle(ThrottleType t)
{
  double headroom    = t == ACCEPT ? NET_THROTTLE_ACCEPT_HEADROOM : NET_THROTTLE_CONNECT_HEADROOM;
  int currently_open = static_cast<int>(Counter::read(net_rsb.connections_currently_open));

  // deal with race if we got to multiple net threads
  if (currently_open < 0) {
    currently_open = 0;
  }
  return static_cast<int>(currently_open * headroom);
}

TS_INLINE void
check_shedding_warning()
{
  ink_hrtime t = ink_get_hrtime();
  if (t - last_shedding_warning > NET_THROTTLE_MESSAGE_EVERY) {
    last_shedding_warning = t;
    Warning("number of connections reaching shedding limit");
  }
}

TS_INLINE bool
check_net_throttle(ThrottleType t)
{
  int connections = net_connections_to_throttle(t);

  if (net_connections_throttle != 0 && connections >= net_connections_throttle) {
    return true;
  }

  return false;
}

TS_INLINE void
check_throttle_warning(ThrottleType type)
{
  ink_hrtime t = ink_get_hrtime();
  if (t - last_throttle_warning > NET_THROTTLE_MESSAGE_EVERY) {
    last_throttle_warning = t;
    int connections       = net_connections_to_throttle(type);
    Warning("too many connections, throttling.  connection_type=%s, current_connections=%d, net_connections_throttle=%d",
            type == ACCEPT ? "ACCEPT" : "CONNECT", connections, net_connections_throttle);
  }
}

TS_INLINE int
change_net_connections_throttle(const char *token, RecDataT data_type, RecData value, void *data)
{
  (void)token;
  (void)data_type;
  (void)value;
  (void)data;
  int throttle = ink_get_fds_limit() - THROTTLE_FD_HEADROOM;
  if (fds_throttle == 0) {
    net_connections_throttle = fds_throttle;
  } else if (fds_throttle < 0) {
    net_connections_throttle = throttle;
  } else {
    net_connections_throttle = fds_throttle;
    if (net_connections_throttle > throttle) {
      net_connections_throttle = throttle;
    }
  }
  return 0;
}

// 2  - ignore
// 1  - transient
// 0  - report as warning
// -1 - fatal
TS_INLINE int
accept_error_seriousness(int res)
{
  switch (res) {
  case -ECONNABORTED:
    return 2;
  case -EAGAIN:
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
  // fallthrough
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
  ink_hrtime t = ink_get_hrtime();
  if (!last_transient_accept_error || t - last_transient_accept_error > TRANSIENT_ACCEPT_ERROR_MESSAGE_EVERY) {
    last_transient_accept_error = t;
    Warning("accept thread received transient error: errno = %d", -res);
#if defined(__linux__)
    if (res == -ENOBUFS || res == -ENFILE)
      Warning("errno : %d consider a memory upgrade", -res);
#endif
  }
}

/** Disable reading on the NetEvent @a ne.
     @param nh Nethandler that owns @a ne.
     @param ne The @c NetEvent to modify.

     - If write is already disable, also disable the inactivity timeout.
     - clear read enabled flag.
     - Remove the @c epoll READ flag.
     - Take @a ne out of the read ready list.
*/
static inline void
read_disable(NetHandler *nh, NetEvent *ne)
{
  if (!ne->write.enabled) {
    // Clear the next scheduled inactivity time, but don't clear inactivity_timeout_in,
    // so the current timeout is used when the NetEvent is reenabled and not the default inactivity timeout
    ne->next_inactivity_timeout_at = 0;
    Dbg(NetHandler::dbg_ctl_socket, "read_disable updating inactivity_at %" PRId64 ", NetEvent=%p", ne->next_inactivity_timeout_at,
        ne);
  }
  ne->read.enabled = 0;
  nh->read_ready_list.remove(ne);
  ne->ep.modify(-EVENTIO_READ);
}

/** Disable writing on the NetEvent @a ne.
     @param nh Nethandler that owns @a ne.
     @param ne The @c NetEvent to modify.

     - If read is already disable, also disable the inactivity timeout.
     - clear write enabled flag.
     - Remove the @c epoll WRITE flag.
     - Take @a ne out of the write ready list.
*/
static inline void
write_disable(NetHandler *nh, NetEvent *ne)
{
  if (!ne->read.enabled) {
    // Clear the next scheduled inactivity time, but don't clear inactivity_timeout_in,
    // so the current timeout is used when the NetEvent is reenabled and not the default inactivity timeout
    ne->next_inactivity_timeout_at = 0;
    Dbg(NetHandler::dbg_ctl_socket, "write_disable updating inactivity_at %" PRId64 ", NetEvent=%p", ne->next_inactivity_timeout_at,
        ne);
  }
  ne->write.enabled = 0;
  nh->write_ready_list.remove(ne);
  ne->ep.modify(-EVENTIO_WRITE);
}
