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

#include "tscore/ink_config.h"
#include "tscore/ink_platform.h"

struct PollDescriptor;

using EventLoop = PollDescriptor *;

#define USE_EDGE_TRIGGER_EPOLL  1
#define USE_EDGE_TRIGGER_KQUEUE 1
#define USE_EDGE_TRIGGER_PORT   1

#if TS_USE_EPOLL
#ifndef EPOLLEXCLUSIVE
#define EPOLLEXCLUSIVE 0
#endif
#ifdef USE_EDGE_TRIGGER_EPOLL
#define USE_EDGE_TRIGGER 1
#define EVENTIO_READ     (EPOLLIN | EPOLLET)
#define EVENTIO_WRITE    (EPOLLOUT | EPOLLET)
#else
#define EVENTIO_READ  EPOLLIN
#define EVENTIO_WRITE EPOLLOUT
#endif
#define EVENTIO_ERROR (EPOLLERR | EPOLLPRI | EPOLLHUP)
#endif
#if TS_USE_KQUEUE
#ifdef USE_EDGE_TRIGGER_KQUEUE
#define USE_EDGE_TRIGGER    1
#define INK_EV_EDGE_TRIGGER EV_CLEAR
#else
#define INK_EV_EDGE_TRIGGER 0
#endif
#include <sys/event.h>
#define INK_EVP_IN    0x001
#define INK_EVP_PRI   0x002
#define INK_EVP_OUT   0x004
#define INK_EVP_ERR   0x010
#define INK_EVP_HUP   0x020
#define EVENTIO_READ  INK_EVP_IN
#define EVENTIO_WRITE INK_EVP_OUT
#define EVENTIO_ERROR (0x010 | 0x002 | 0x020) // ERR PRI HUP
#endif

/// Unified API for setting and clearing kernel and epoll events.
struct EventIO {
  int fd = -1; ///< file descriptor, often a system port
#if TS_USE_KQUEUE || TS_USE_EPOLL && !defined(USE_EDGE_TRIGGER)
  int events = 0; ///< a bit mask of enabled events
#endif
  EventLoop event_loop = nullptr; ///< the assigned event loop
  bool syscall         = true;    ///< if false, disable all functionality (for QUIC)

  /** Alter the events that will trigger the continuation, for level triggered I/O.
     @param events add with positive mask(+EVENTIO_READ), or remove with negative mask (-EVENTIO_READ)
     @return int the number of events created, -1 is error
   */
  int modify(int events);

  /** Refresh the existing events (i.e. KQUEUE EV_CLEAR), for edge triggered I/O
     @param events mask of events
     @return int the number of events created, -1 is error
   */
  int refresh(int events);

  /// Remove the kernel or epoll event. Returns 0 on success.
  int stop();

  // Process one event that has triggered.
  virtual void process_event(int flags) = 0;

  EventIO() {}
  virtual ~EventIO() {}

protected:
  /** The start methods all logically Setup a class to be called
     when a file descriptor is available for read or write.
     The type of the classes vary.  Generally the file descriptor
     is pulled from the class, but there is one option that lets
     the file descriptor be expressed directly.
     @param l the event loop
     @param events a mask of flags (for details `man epoll_ctl`)
     @return int the number of events created, -1 is error
   */
  int start_common(EventLoop l, int fd, int events);
};
