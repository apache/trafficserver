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
#pragma once

#include "I_EThread.h"
#include "I_EventProcessor.h"
#include <execinfo.h>

const ink_hrtime DELAY_FOR_RETRY = HRTIME_MSECONDS(10);

TS_INLINE Event *
EThread::schedule_imm(Continuation *cont, int callback_event, void *cookie)
{
  Event *e = ::eventAllocator.alloc();

#ifdef ENABLE_EVENT_TRACKER
  e->set_location();
#endif

  e->callback_event = callback_event;
  e->cookie         = cookie;
  return schedule(e->init(cont, 0, 0));
}

TS_INLINE Event *
EThread::schedule_at(Continuation *cont, ink_hrtime t, int callback_event, void *cookie)
{
  Event *e = ::eventAllocator.alloc();

#ifdef ENABLE_EVENT_TRACKER
  e->set_location();
#endif

  e->callback_event = callback_event;
  e->cookie         = cookie;
  return schedule(e->init(cont, t, 0));
}

TS_INLINE Event *
EThread::schedule_in(Continuation *cont, ink_hrtime t, int callback_event, void *cookie)
{
  Event *e = ::eventAllocator.alloc();

#ifdef ENABLE_EVENT_TRACKER
  e->set_location();
#endif

  e->callback_event = callback_event;
  e->cookie         = cookie;
  return schedule(e->init(cont, get_hrtime() + t, 0));
}

TS_INLINE Event *
EThread::schedule_every(Continuation *cont, ink_hrtime t, int callback_event, void *cookie)
{
  Event *e = ::eventAllocator.alloc();

#ifdef ENABLE_EVENT_TRACKER
  e->set_location();
#endif

  e->callback_event = callback_event;
  e->cookie         = cookie;
  if (t < 0) {
    return schedule(e->init(cont, t, t));
  } else {
    return schedule(e->init(cont, get_hrtime() + t, t));
  }
}

TS_INLINE Event *
EThread::schedule(Event *e)
{
  e->ethread = this;
  if (tt != REGULAR) {
    ink_assert(tt == DEDICATED);
    return eventProcessor.schedule(e, ET_CALL);
  }
  if (e->continuation->mutex) {
    e->mutex = e->continuation->mutex;
  } else {
    e->mutex = e->continuation->mutex = e->ethread->mutex;
  }
  ink_assert(e->mutex.get());

  // Make sure client IP debugging works consistently
  // The continuation that gets scheduled later is not always the
  // client VC, it can be HttpCacheSM etc. so save the flags
  e->continuation->control_flags.set_flags(get_cont_flags().get_flags());

  if (e->ethread == this_ethread()) {
    EventQueueExternal.enqueue_local(e);
  } else {
    EventQueueExternal.enqueue(e);
  }

  return e;
}

TS_INLINE Event *
EThread::schedule_imm_local(Continuation *cont, int callback_event, void *cookie)
{
  Event *e = EVENT_ALLOC(eventAllocator, this);

#ifdef ENABLE_EVENT_TRACKER
  e->set_location();
#endif

  e->callback_event = callback_event;
  e->cookie         = cookie;
  return schedule_local(e->init(cont, 0, 0));
}

TS_INLINE Event *
EThread::schedule_at_local(Continuation *cont, ink_hrtime t, int callback_event, void *cookie)
{
  Event *e = EVENT_ALLOC(eventAllocator, this);

#ifdef ENABLE_EVENT_TRACKER
  e->set_location();
#endif

  e->callback_event = callback_event;
  e->cookie         = cookie;
  return schedule_local(e->init(cont, t, 0));
}

TS_INLINE Event *
EThread::schedule_in_local(Continuation *cont, ink_hrtime t, int callback_event, void *cookie)
{
  Event *e = EVENT_ALLOC(eventAllocator, this);

#ifdef ENABLE_EVENT_TRACKER
  e->set_location();
#endif

  e->callback_event = callback_event;
  e->cookie         = cookie;
  return schedule_local(e->init(cont, get_hrtime() + t, 0));
}

TS_INLINE Event *
EThread::schedule_every_local(Continuation *cont, ink_hrtime t, int callback_event, void *cookie)
{
  Event *e = EVENT_ALLOC(eventAllocator, this);

#ifdef ENABLE_EVENT_TRACKER
  e->set_location();
#endif

  e->callback_event = callback_event;
  e->cookie         = cookie;
  if (t < 0) {
    return schedule_local(e->init(cont, t, t));
  } else {
    return schedule_local(e->init(cont, get_hrtime() + t, t));
  }
}

TS_INLINE Event *
EThread::schedule_local(Event *e)
{
  if (tt != REGULAR) {
    ink_assert(tt == DEDICATED);
    return eventProcessor.schedule(e, ET_CALL);
  }
  if (!e->mutex) {
    e->ethread = this;
    e->mutex   = e->continuation->mutex;
  } else {
    ink_assert(e->ethread == this);
  }
  e->globally_allocated = false;

  // Make sure client IP debugging works consistently
  // The continuation that gets scheduled later is not always the
  // client VC, it can be HttpCacheSM etc. so save the flags
  e->continuation->control_flags.set_flags(get_cont_flags().get_flags());
  EventQueueExternal.enqueue_local(e);
  return e;
}

TS_INLINE Event *
EThread::schedule_spawn(Continuation *c, int ev, void *cookie)
{
  ink_assert(this != this_ethread()); // really broken to call this from the same thread.
  if (start_event) {
    free_event(start_event);
  }
  start_event          = EVENT_ALLOC(eventAllocator, this);
  start_event->ethread = this;
  start_event->mutex   = this->mutex;
  start_event->init(c);
  start_event->callback_event = ev;
  start_event->cookie         = cookie;
  return start_event;
}

TS_INLINE EThread *
this_ethread()
{
  return EThread::this_ethread_ptr;
}

TS_INLINE EThread *
this_event_thread()
{
  EThread *ethread = this_ethread();
  if (ethread != nullptr && ethread->tt == REGULAR) {
    return ethread;
  } else {
    return nullptr;
  }
}

TS_INLINE void
EThread::free_event(Event *e)
{
  ink_assert(!e->in_the_priority_queue && !e->in_the_prot_queue);
  e->mutex = nullptr;
  EVENT_FREE(e, eventAllocator, this);
}

TS_INLINE void
EThread::set_tail_handler(LoopTailHandler *handler)
{
  ink_atomic_swap(&tail_cb, handler);
}
