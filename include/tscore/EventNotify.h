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
  EventNotify.h

  Generic event notify mechanism among threads.

**************************************************************************/

#pragma once

#include "tscore/ink_thread.h"

class EventNotify
{
public:
  EventNotify();
  void signal();
  int wait();
  int timedwait(int timeout); // milliseconds
  void lock();
  bool trylock();
  void unlock();
  ~EventNotify();

private:
#if defined(HAVE_EVENTFD) && TS_USE_EPOLL == 1
  int m_event_fd;
  int m_epoll_fd;
#else
  ink_cond m_cond;
  ink_mutex m_mutex;
#endif
};
/* vim: set sw=4 ts=4 tw=79 et : */
