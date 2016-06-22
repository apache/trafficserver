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

/****************************************************************************

  UnixPollDescriptor.h


*****************************************************************************/
#ifndef __P_UNIXPOLLDESCRIPTOR_H__
#define __P_UNIXPOLLDESCRIPTOR_H__

#include "ts/ink_platform.h"

#if TS_USE_KQUEUE
#include <sys/event.h>
#define INK_EVP_IN 0x001
#define INK_EVP_PRI 0x002
#define INK_EVP_OUT 0x004
#define INK_EVP_ERR 0x010
#define INK_EVP_HUP 0x020
#endif

#define POLL_DESCRIPTOR_SIZE 32768

typedef struct pollfd Pollfd;

struct PollDescriptor {
  int result; // result of poll
#if TS_USE_EPOLL
  int epoll_fd;
  int nfds; // actual number
  Pollfd pfd[POLL_DESCRIPTOR_SIZE];
  struct epoll_event ePoll_Triggered_Events[POLL_DESCRIPTOR_SIZE];
#endif
#if TS_USE_KQUEUE
  int kqueue_fd;
#endif
#if TS_USE_PORT
  int port_fd;
#endif

  PollDescriptor() { init(); }
#if TS_USE_EPOLL
#define get_ev_port(a) ((a)->epoll_fd)
#define get_ev_events(a, x) ((a)->ePoll_Triggered_Events[(x)].events)
#define get_ev_data(a, x) ((a)->ePoll_Triggered_Events[(x)].data.ptr)
#define ev_next_event(a, x)
#endif

#if TS_USE_KQUEUE
  struct kevent kq_Triggered_Events[POLL_DESCRIPTOR_SIZE];
/* we define these here as numbers, because for kqueue mapping them to a combination of
*filters / flags is hard to do. */
#define get_ev_port(a) ((a)->kqueue_fd)
#define get_ev_events(a, x) ((a)->kq_event_convert((a)->kq_Triggered_Events[(x)].filter, (a)->kq_Triggered_Events[(x)].flags))
#define get_ev_data(a, x) ((a)->kq_Triggered_Events[(x)].udata)
  int
  kq_event_convert(int16_t event, uint16_t flags)
  {
    int r = 0;

    if (event == EVFILT_READ) {
      r |= INK_EVP_IN;
    } else if (event == EVFILT_WRITE) {
      r |= INK_EVP_OUT;
    }

    if (flags & EV_EOF) {
      r |= INK_EVP_HUP;
    }
    return r;
  }
#define ev_next_event(a, x)
#endif

#if TS_USE_PORT
  port_event_t Port_Triggered_Events[POLL_DESCRIPTOR_SIZE];
#define get_ev_port(a) ((a)->port_fd)
#define get_ev_events(a, x) ((a)->Port_Triggered_Events[(x)].portev_events)
#define get_ev_data(a, x) ((a)->Port_Triggered_Events[(x)].portev_user)
#define get_ev_odata(a, x) ((a)->Port_Triggered_Events[(x)].portev_object)
#define ev_next_event(a, x)
#endif

  Pollfd *
  alloc()
  {
#if TS_USE_EPOLL
    // XXX : We need restrict max size based on definition.
    if (nfds >= POLL_DESCRIPTOR_SIZE) {
      nfds = 0;
    }
    return &pfd[nfds++];
#else
    return 0;
#endif
  }

private:
  void
  init()
  {
    result = 0;
#if TS_USE_EPOLL
    nfds     = 0;
    epoll_fd = epoll_create(POLL_DESCRIPTOR_SIZE);
    memset(ePoll_Triggered_Events, 0, sizeof(ePoll_Triggered_Events));
    memset(pfd, 0, sizeof(pfd));
#endif
#if TS_USE_KQUEUE
    kqueue_fd = kqueue();
    memset(kq_Triggered_Events, 0, sizeof(kq_Triggered_Events));
#endif
#if TS_USE_PORT
    port_fd = port_create();
    memset(Port_Triggered_Events, 0, sizeof(Port_Triggered_Events));
#endif
  }
};

#endif
