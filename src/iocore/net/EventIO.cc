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

#include "iocore/net/EventIO.h"
#include "tscore/ink_assert.h"
#include "P_UnixPollDescriptor.h"

#include <cerrno>

#if TS_USE_KQUEUE
namespace
{
int
delete_kqueue_filters(EventLoop loop, int fd, int events)
{
  int result = 0;

  if (events & EVENTIO_READ) {
    struct kevent ev;

    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    if (kevent(loop->kqueue_fd, &ev, 1, nullptr, 0, nullptr) < 0 && errno != ENOENT) {
      result = -1;
    }
  }
  if (events & EVENTIO_WRITE) {
    struct kevent ev;

    EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    if (kevent(loop->kqueue_fd, &ev, 1, nullptr, 0, nullptr) < 0 && errno != ENOENT) {
      result = -1;
    }
  }
  return result;
}
} // namespace
#endif

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
  return epoll_ctl(l->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
#endif
#if TS_USE_KQUEUE
  events = e;
  struct kevent ev[2];
  int           n = 0;
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

  // Another thread can stop this EventIO to migrate the connection while the owning thread is
  // still processing it, see UnixNetVConnection::detachForMigration. Read the event loop once
  // so a concurrent stop cannot clear it between the check and the use.
  EventLoop loop = event_loop;

  if (!loop) {
    return 0;
  }
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
    return epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, &ev);
  else if (!old_events)
    return epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  else
    return epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
#endif
#if TS_USE_KQUEUE && !defined(USE_EDGE_TRIGGER)
  int           n = 0;
  struct kevent ev[2];
  int           ee = events;
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
  if (n) {
    return kevent(loop->kqueue_fd, &ev[0], n, nullptr, 0, nullptr);
  } else {
    return 0;
  }
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

  // Another thread can stop this EventIO to migrate the connection while the owning thread is
  // still processing it, see UnixNetVConnection::detachForMigration. Read the event loop once
  // so a concurrent stop cannot clear it between the check and the use.
  EventLoop loop = event_loop;

  if (!loop) {
    return 0;
  }
#if TS_USE_KQUEUE && defined(USE_EDGE_TRIGGER)
  e = e & events;
  struct kevent ev[2];
  int           n = 0;
  if (e & EVENTIO_READ) {
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  }
  if (e & EVENTIO_WRITE) {
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  }
  if (n) {
    int result = kevent(loop->kqueue_fd, &ev[0], n, nullptr, 0, nullptr);

    // A concurrent migration may have removed the old registration during the add.
    if (event_loop.load() != loop && delete_kqueue_filters(loop, fd, e) < 0) {
      return -1;
    }
    return result;
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
  if (EventLoop loop = event_loop.exchange(nullptr)) {
    int retval = 0;
#if TS_USE_EPOLL
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    retval    = epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, &ev);
#else
    (void)loop;
#endif
    return retval;
  }
  return 0;
}

int
EventIO::stop_for_migration()
{
#if TS_USE_KQUEUE
  if (!this->syscall) {
    return 0;
  }
  if (EventLoop loop = event_loop.exchange(nullptr)) {
    return delete_kqueue_filters(loop, fd, events);
  }
  return 0;
#else
  return stop();
#endif
}
