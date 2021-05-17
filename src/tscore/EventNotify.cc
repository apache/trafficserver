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

/**************************************************************************
  EventNotify.cc

  Generic event notify mechanism among threads.
**************************************************************************/

#include "tscore/EventNotify.h"
#include "tscore/ink_hrtime.h"
#include "tscore/ink_defs.h"

#if defined(HAVE_EVENTFD) && TS_USE_EPOLL == 1
#include <sys/eventfd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#endif

EventNotify::EventNotify()
{
#if defined(HAVE_EVENTFD) && TS_USE_EPOLL == 1
  int ret;
  struct epoll_event ev;

  m_event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  ink_release_assert(m_event_fd != -1);

  ev.events  = EPOLLIN;
  ev.data.fd = m_event_fd;

  m_epoll_fd = epoll_create(1);
  ink_release_assert(m_epoll_fd != -1);

  ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_event_fd, &ev);
  ink_release_assert(ret != -1);
#else
  ink_cond_init(&m_cond);
  ink_mutex_init(&m_mutex);
#endif
}

void
EventNotify::signal()
{
#if defined(HAVE_EVENTFD) && TS_USE_EPOLL == 1
  uint64_t value = 1;
  //
  // If the addition would cause the counter's value of eventfd
  // to exceed the maximum, write() will fail with the errno EAGAIN,
  // which is acceptable as the receiver will be notified eventually.
  //
  ATS_UNUSED_RETURN(write(m_event_fd, &value, sizeof(uint64_t)));
#else
  ink_cond_signal(&m_cond);
#endif
}

int
EventNotify::wait()
{
#if defined(HAVE_EVENTFD) && TS_USE_EPOLL == 1
  ssize_t nr, nr_fd;
  uint64_t value = 0;
  struct epoll_event ev;

  do {
    nr_fd = epoll_wait(m_epoll_fd, &ev, 1, 500000);
  } while (nr_fd == -1 && errno == EINTR);

  if (nr_fd == -1) {
    return errno;
  }

  nr = read(m_event_fd, &value, sizeof(uint64_t));
  if (nr == sizeof(uint64_t)) {
    return 0;
  } else {
    return errno;
  }
#else
  ink_cond_wait(&m_cond, &m_mutex);
  return 0;
#endif
}

int
EventNotify::timedwait(int timeout) // milliseconds
{
#if defined(HAVE_EVENTFD) && TS_USE_EPOLL == 1
  ssize_t nr, nr_fd = 0;
  uint64_t value = 0;
  struct epoll_event ev;

  //
  // When timeout < 0, epoll_wait() will wait indefinitely, but
  // pthread_cond_timedwait() will return ETIMEDOUT immediately.
  // We should keep compatible with pthread_cond_timedwait() here.
  //
  if (timeout < 0) {
    return ETIMEDOUT;
  }

  do {
    nr_fd = epoll_wait(m_epoll_fd, &ev, 1, timeout);
  } while (nr_fd == -1 && errno == EINTR);

  if (nr_fd == 0) {
    return ETIMEDOUT;
  } else if (nr_fd == -1) {
    return errno;
  }

  nr = read(m_event_fd, &value, sizeof(uint64_t));
  if (nr == sizeof(uint64_t)) {
    return 0;
  } else {
    return errno;
  }
#else
  ink_timestruc abstime;

  abstime = ink_hrtime_to_timespec(ink_get_hrtime_internal() + HRTIME_SECONDS(timeout));
  return ink_cond_timedwait(&m_cond, &m_mutex, &abstime);
#endif
}

void
EventNotify::lock()
{
#if defined(HAVE_EVENTFD) && TS_USE_EPOLL == 1
// do nothing
#else
  ink_mutex_acquire(&m_mutex);
#endif
}

bool
EventNotify::trylock()
{
#if defined(HAVE_EVENTFD) && TS_USE_EPOLL == 1
  return true;
#else
  return ink_mutex_try_acquire(&m_mutex);
#endif
}

void
EventNotify::unlock()
{
#if defined(HAVE_EVENTFD) && TS_USE_EPOLL == 1
// do nothing
#else
  ink_mutex_release(&m_mutex);
#endif
}

EventNotify::~EventNotify()
{
#if defined(HAVE_EVENTFD) && TS_USE_EPOLL == 1
  close(m_event_fd);
  close(m_epoll_fd);
#else
  ink_cond_destroy(&m_cond);
  ink_mutex_destroy(&m_mutex);
#endif
}
