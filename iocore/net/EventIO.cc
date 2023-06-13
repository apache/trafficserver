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

#include "EventIO.h"
#include "tscore/ink_assert.h"
#include "P_Net.h"
#include "P_UnixNetProcessor.h"
#include "P_UnixNetVConnection.h"
#include "P_NetAccept.h"
#include "P_DNSConnection.h"
#include "P_UnixUDPConnection.h"
#include "P_UnixPollDescriptor.h"

int
EventIO::start(EventLoop l, DNSConnection *vc, int events)
{
  type        = EVENTIO_DNS_CONNECTION;
  data.dnscon = vc;
  return start_common(l, vc->fd, events);
}
int
EventIO::start(EventLoop l, NetAccept *vc, int events)
{
  type    = EVENTIO_NETACCEPT;
  data.na = vc;
  return start_common(l, vc->server.fd, events);
}
int
EventIO::start(EventLoop l, NetEvent *ne, int events)
{
  type    = EVENTIO_READWRITE_VC;
  data.ne = ne;
  return start_common(l, ne->get_fd(), events);
}

int
EventIO::start(EventLoop l, UnixUDPConnection *vc, int events)
{
  type    = EVENTIO_UDP_CONNECTION;
  data.uc = vc;
  return start_common(l, vc->fd, events);
}

int
EventIO::start(EventLoop l, int afd, NetEvent *ne, int e)
{
  data.ne = ne;
  return start_common(l, afd, e);
}

int
EventIO::start_common(EventLoop l, int afd, int e)
{
  if (!this->syscall) {
    return 0;
  }

  fd         = afd;
  event_loop = l;
#if TS_USE_EPOLL
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events   = e | EPOLLEXCLUSIVE;
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
  if (e & EVENTIO_READ) {
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  }
  if (e & EVENTIO_WRITE) {
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  }
  return kevent(l->kqueue_fd, &ev[0], n, nullptr, 0, nullptr);
#endif
}

int
EventIO::modify(int e)
{
  if (!this->syscall) {
    return 0;
  }

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
    return kevent(event_loop->kqueue_fd, &ev[0], n, nullptr, 0, nullptr);
  else
    return 0;
#endif
  (void)e; // ATS_UNUSED
  return 0;
}

int
EventIO::refresh(int e)
{
  if (!this->syscall) {
    return 0;
  }

  ink_assert(event_loop);
#if TS_USE_KQUEUE && defined(USE_EDGE_TRIGGER)
  e = e & events;
  struct kevent ev[2];
  int n = 0;
  if (e & EVENTIO_READ) {
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  }
  if (e & EVENTIO_WRITE) {
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  }
  if (n) {
    return kevent(event_loop->kqueue_fd, &ev[0], n, nullptr, 0, nullptr);
  } else {
    return 0;
  }
#endif
  (void)e; // ATS_UNUSED
  return 0;
}

int
EventIO::stop()
{
  if (!this->syscall) {
    return 0;
  }
  if (event_loop) {
    int retval = 0;
#if TS_USE_EPOLL
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    retval    = epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, &ev);
#endif
    event_loop = nullptr;
    return retval;
  }
  return 0;
}

int
EventIO::close()
{
  if (!this->syscall) {
    return 0;
  }

  stop();
  switch (type) {
  default:
    ink_assert(!"case");
  // fallthrough
  case EVENTIO_DNS_CONNECTION:
    return data.dnscon->close();
    break;
  case EVENTIO_NETACCEPT:
    return data.na->server.close();
    break;
  case EVENTIO_READWRITE_VC:
    return data.ne->close();
    break;
  }
  return -1;
}
