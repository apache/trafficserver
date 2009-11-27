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

typedef struct pollfd Pollfd;

#define POLL_DESCRIPTOR_SIZE 32768

struct PollDescriptor
{
  int result;                   // result of poll
#if defined(USE_EPOLL)
  int epoll_fd;                 // epoll interface (added by YTS Team, yamsat)
#elif defined(USE_KQUEUE)
  int kqueue_fd;
#else
#error port to your os
#endif
  int nfds;                     // actual number
  int seq_num;                  // sequence number
  Pollfd pfd[POLL_DESCRIPTOR_SIZE];

#if defined(USE_EPOLL)
  struct epoll_event ePoll_Triggered_Events[POLL_DESCRIPTOR_SIZE];      //added by YTS Team, yamsat
#define INK_EVP_IN EPOLLIN
#define INK_EVP_OUT EPOLLOUT
#define INK_EVP_ERR EPOLLERR
#define INK_EVP_PRI EPOLLPRI
#define INK_EVP_HUP EPOLLHUP
#define get_ev_events(a,x) ((a)->ePoll_Triggered_Events[(x)].events)
#define get_ev_data(a,x) ((a)->ePoll_Triggered_Events[(x)].data.ptr)

#elif defined(USE_KQUEUE)
  struct kevent kq_Triggered_Events[POLL_DESCRIPTOR_SIZE];      //added by YTS Team, yamsat
  /* we define these here as numbers, because for kqueue mapping them to a combination of 
 * filters / flags is hard to do. */
#define INK_EVP_IN    0x001
#define INK_EVP_PRI   0x002
#define INK_EVP_OUT   0x004
#define INK_EVP_ERR   0x010
#define INK_EVP_HUP   0x020
#define get_ev_events(a,x) ((a)->kq_event_convert((a)->kq_Triggered_Events[(x)].filter, (a)->kq_Triggered_Events[(x)].flags))
#define get_ev_data(a,x) ((a)->kq_Triggered_Events[(x)].udata)
  int kq_event_convert(int16_t event, uint16_t flags)
  {
    int r = 0;

    if (event == EVFILT_READ) {
      r |= INK_EVP_IN;
    }
    else if (event == EVFILT_WRITE) {
      r |= INK_EVP_OUT;
    }

    if (flags & EV_EOF) {
      r |= INK_EVP_HUP;
    }
    return r;
  }
#else
#error port to your os
#endif

  bool empty()
  {
    return nfds == 0;
  }
  void reset()
  {
    nfds = 0;
  }
  Pollfd *alloc()
  {
    return &pfd[nfds++];
  }

  PollDescriptor *init()
  {
    result = 0;
    nfds = 0;
    seq_num = 0;
#if defined(USE_EPOLL)
    epoll_fd = epoll_create(POLL_DESCRIPTOR_SIZE);      //added by YTS Team, yamsat
    memset(ePoll_Triggered_Events, 0, sizeof(ePoll_Triggered_Events));
#elif defined(USE_KQUEUE)
    kqueue_fd = kqueue();
    memset(kq_Triggered_Events, 0, sizeof(kq_Triggered_Events));
#else
#error port to your os
#endif
    memset(pfd, 0, sizeof(pfd));
    return this;
  }
  PollDescriptor() {
    init();
  }
};

#endif
