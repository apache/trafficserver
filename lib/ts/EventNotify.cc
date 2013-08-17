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

#include "EventNotify.h"
#include "ink_hrtime.h"

#ifdef TS_HAS_EVENTFD
#include <sys/eventfd.h>
#include <sys/epoll.h>
#endif

EventNotify::EventNotify(const char *name): m_name(name)
{
#ifdef TS_HAS_EVENTFD
  int ret;
  struct epoll_event ev;

  // Don't use noblock here!
  m_event_fd = eventfd(0, EFD_CLOEXEC);
  if (m_event_fd < 0) {
    // EFD_CLOEXEC invalid in <= Linux 2.6.27
    m_event_fd = eventfd(0, 0);
  }
  ink_release_assert(m_event_fd != -1);

  ev.events = EPOLLIN;
  ev.data.fd = m_event_fd;

  m_epoll_fd = epoll_create(1);
  ink_release_assert(m_epoll_fd != -1);

  ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_event_fd, &ev);
  ink_release_assert(ret != -1);
#else
  ink_cond_init(&m_cond);
  ink_mutex_init(&m_mutex, m_name);
#endif
}

void
EventNotify::signal(void)
{
#ifdef TS_HAS_EVENTFD
  ssize_t nr;
  uint64_t value = 1;
  nr = write(m_event_fd, &value, sizeof(uint64_t));
  ink_release_assert(nr == sizeof(uint64_t));
#else
  ink_cond_signal(&m_cond);
#endif
}

void
EventNotify::wait(void)
{
#ifdef TS_HAS_EVENTFD
  ssize_t nr;
  uint64_t value = 0;
  nr = read(m_event_fd, &value, sizeof(uint64_t));
  ink_release_assert(nr == sizeof(uint64_t));
#else
  ink_cond_wait(&m_cond, &m_mutex);
#endif
}

int
EventNotify::timedwait(ink_timestruc *abstime)
{
#ifdef TS_HAS_EVENTFD
  int timeout;
  ssize_t nr, nr_fd = 0;
  uint64_t value = 0;
  struct timeval curtime;
  struct epoll_event ev;

  // Convert absolute time to relative time
  gettimeofday(&curtime, NULL);
  timeout = (abstime->tv_sec - curtime.tv_sec) * 1000
          + (abstime->tv_nsec / 1000  - curtime.tv_usec) / 1000;

  //
  // When timeout < 0, epoll_wait() will wait indefinitely, but
  // pthread_cond_timedwait() will return ETIMEDOUT immediately.
  // We should keep compatible with pthread_cond_timedwait() here.
  //
  if (timeout < 0)
    return ETIMEDOUT;

  do {
    nr_fd = epoll_wait(m_epoll_fd, &ev, 1, timeout);
  } while (nr_fd == -1 && errno == EINTR);

  if (nr_fd == 0)
    return ETIMEDOUT;
  else if (nr_fd == -1)
    return errno;

  nr = read(m_event_fd, &value, sizeof(uint64_t));
  ink_release_assert(nr == sizeof(uint64_t));

  return 0;
#else
  return ink_cond_timedwait(&m_cond, &m_mutex, abstime);
#endif
}

void
EventNotify::lock(void)
{
#ifdef TS_HAS_EVENTFD
  // do nothing
#else
  ink_mutex_acquire(&m_mutex);
#endif
}

bool
EventNotify::trylock(void)
{
#ifdef TS_HAS_EVENTFD
  return true;
#else
  return ink_mutex_try_acquire(&m_mutex);
#endif
}

void
EventNotify::unlock(void)
{
#ifdef TS_HAS_EVENTFD
  // do nothing
#else
  ink_mutex_release(&m_mutex);
#endif
}

EventNotify::~EventNotify()
{
#ifdef TS_HAS_EVENTFD
  close(m_event_fd);
  close(m_epoll_fd);
#else
  ink_cond_destroy(&m_cond);
  ink_mutex_destroy(&m_mutex);
#endif
}
