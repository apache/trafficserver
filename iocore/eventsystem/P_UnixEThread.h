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

  P_UnixEThread.h

  
  
*****************************************************************************/
#ifndef _P_UnixEThread_h_
#define _P_UnixEThread_h_

#include "I_EThread.h"
#include "I_ProxyAllocator.h"
#include "I_EventProcessor.h"

const int DELAY_FOR_RETRY = HRTIME_MSECONDS(10);

INK_INLINE Event *
EThread::schedule_spawn(Continuation * cont)
{
  Event *e = EVENT_ALLOC(eventAllocator, this);
  return schedule(e->init(cont, 0, 0));
}

INK_INLINE Event *
EThread::schedule_imm(Continuation * cont, int callback_event, void *cookie)
{
  NOWARN_UNUSED(callback_event);
  Event *e =::eventAllocator.alloc();
  e->cookie = cookie;
#ifdef ENABLE_TIME_TRACE
  e->start_time = ink_get_hrtime();
#endif
  return schedule(e->init(cont, 0, 0));
}

INK_INLINE Event *
EThread::schedule_at(Continuation * cont, ink_hrtime t, int callback_event, void *cookie)
{
  NOWARN_UNUSED(callback_event);
  Event *e =::eventAllocator.alloc();
  e->cookie = cookie;
  return schedule(e->init(cont, t, 0));
}

INK_INLINE Event *
EThread::schedule_in(Continuation * cont, ink_hrtime t, int callback_event, void *cookie)
{
  NOWARN_UNUSED(callback_event);
  Event *e =::eventAllocator.alloc();
  e->cookie = cookie;
  return schedule(e->init(cont, ink_get_based_hrtime() + t, 0));
}

INK_INLINE Event *
EThread::schedule_every(Continuation * cont, ink_hrtime t, int callback_event, void *cookie)
{
  NOWARN_UNUSED(callback_event);
  Event *e =::eventAllocator.alloc();
  e->cookie = cookie;
  return schedule(e->init(cont, ink_get_based_hrtime() + t, t));
}

INK_INLINE Event *
EThread::schedule(Event * e)
{
  e->ethread = this;
  ink_assert(tt == REGULAR);
  if (e->continuation->mutex)
    e->mutex = e->continuation->mutex;
  else
    e->mutex = e->continuation->mutex = e->ethread->mutex;
  ink_assert(e->mutex.m_ptr);
  EventQueueExternal.enqueue(e);
  return e;
}

INK_INLINE Event *
EThread::schedule_imm_local(Continuation * cont, int callback_event, void *cookie)
{
  NOWARN_UNUSED(callback_event);
  Event *e = EVENT_ALLOC(eventAllocator, this);
#ifdef ENABLE_TIME_TRACE
  e->start_time = ink_get_hrtime();
#endif
  e->cookie = cookie;
  return schedule_local(e->init(cont, 0, 0));
}

INK_INLINE Event *
EThread::schedule_at_local(Continuation * cont, ink_hrtime t, int callback_event, void *cookie)
{
  NOWARN_UNUSED(callback_event);
  Event *e = EVENT_ALLOC(eventAllocator, this);
  e->cookie = cookie;
  return schedule_local(e->init(cont, t, 0));
}

INK_INLINE Event *
EThread::schedule_in_local(Continuation * cont, ink_hrtime t, int callback_event, void *cookie)
{
  NOWARN_UNUSED(callback_event);
  Event *e = EVENT_ALLOC(eventAllocator, this);
  e->cookie = cookie;
  return schedule_local(e->init(cont, ink_get_based_hrtime() + t, 0));
}

INK_INLINE Event *
EThread::schedule_every_local(Continuation * cont, ink_hrtime t, int callback_event, void *cookie)
{
  NOWARN_UNUSED(callback_event);
  Event *e = EVENT_ALLOC(eventAllocator, this);
  e->cookie = cookie;
  return schedule_local(e->init(cont, ink_get_based_hrtime() + t, t));
}

INK_INLINE Event *
EThread::schedule_local(Event * e)
{
  //ink_assert(tt == REGULAR);
  if (tt != REGULAR) {
    ink_debug_assert(tt == DEDICATED);
    return eventProcessor.schedule(e, ET_CALL);
  }
  if (!e->mutex.m_ptr) {
    e->ethread = this;
    e->mutex = e->continuation->mutex;
  } else {
    ink_assert(e->ethread == this);
  }
  e->globally_allocated = false;
  EventQueueExternal.enqueue_local(e);
  return e;
}

INK_INLINE EThread *
this_ethread()
{
  return (EThread *) this_thread();
}

INK_INLINE void
EThread::free_event(Event * e)
{
  ink_assert(!e->in_the_priority_queue && !e->in_the_prot_queue);
  e->mutex = NULL;
  EVENT_FREE(e, eventAllocator, this);
}


#endif /*_EThread_h_*/
