/** @file

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

#include "ink_config.h"
#include "P_Net.h"

typedef int (SSLNetAccept::*SSLNetAcceptHandler) (int, void *);

// Virtual function allows the correct
// etype to be used in NetAccept functions (ET_SSL
// or ET_NET).
EventType
SSLNetAccept::getEtype()
{
  return SSLNetProcessor::ET_SSL;
}

// Functions all THREAD_FREE and THREAD_ALLOC to be performed
// for both SSL and regular NetVConnection transparent to
// accept functions.
UnixNetVConnection *
SSLNetAccept::allocateThread(EThread *t)
{
  return ((UnixNetVConnection *) THREAD_ALLOC(sslNetVCAllocator, t));
}

void
SSLNetAccept::freeThread(UnixNetVConnection *vc, EThread *t)
{
  ink_assert(!vc->from_accept_thread);
  THREAD_FREE((SSLNetVConnection *) vc, sslNetVCAllocator, t);
}

// This allocates directly on the class allocator, used for accept threads.
UnixNetVConnection *
SSLNetAccept::allocateGlobal()
{
  return (UnixNetVConnection *)sslNetVCAllocator.alloc();
}

void
SSLNetAccept::init_accept_per_thread()
{
  int i, n;
  if (do_listen(NON_BLOCKING))
    return;
  if (accept_fn == net_accept)
    SET_HANDLER((SSLNetAcceptHandler) & SSLNetAccept::acceptFastEvent);
  else
    SET_HANDLER((SSLNetAcceptHandler) & SSLNetAccept::acceptEvent);
  period = ACCEPT_PERIOD;
  NetAccept *a = this;
  n = eventProcessor.n_threads_for_type[SSLNetProcessor::ET_SSL];
  for (i = 0; i < n; i++) {
    if (i < n - 1)
      a = clone();
    else
      a = this;
    EThread *t = eventProcessor.eventthread[SSLNetProcessor::ET_SSL][i];

    PollDescriptor *pd = get_PollDescriptor(t);
    if (ep.start(pd, this, EVENTIO_READ) < 0)
      Debug("iocore_net", "error starting EventIO");
    a->mutex = get_NetHandler(t)->mutex;
    t->schedule_every(a, period, etype);
  }
}

NetAccept *
SSLNetAccept::clone()
{
  NetAccept *na;
  na = NEW(new SSLNetAccept);
  *na = *this;
  return na;
}
