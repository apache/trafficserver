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

  Event.cc


*****************************************************************************/
#include "P_EventSystem.h"

#include "tscore/ink_stack_trace.h"

ClassAllocator<Event> eventAllocator("eventAllocator", 256);

void
Event::schedule_imm(int acallback_event)
{
  callback_event = acallback_event;
  ink_assert(ethread == this_ethread());
  if (in_the_priority_queue) {
    ethread->EventQueue.remove(this);
  }
  timeout_at = 0;
  period     = 0;
  immediate  = true;
  mutex      = continuation->mutex;
  if (!in_the_prot_queue) {
    ethread->EventQueueExternal.enqueue_local(this);
  }
}

void
Event::schedule_at(ink_hrtime atimeout_at, int acallback_event)
{
  callback_event = acallback_event;
  ink_assert(ethread == this_ethread());
  ink_assert(atimeout_at > 0);
  if (in_the_priority_queue) {
    ethread->EventQueue.remove(this);
  }
  timeout_at = atimeout_at;
  period     = 0;
  immediate  = false;
  mutex      = continuation->mutex;
  if (!in_the_prot_queue) {
    ethread->EventQueueExternal.enqueue_local(this);
  }
}

void
Event::schedule_in(ink_hrtime atimeout_in, int acallback_event)
{
  callback_event = acallback_event;
  ink_assert(ethread == this_ethread());
  if (in_the_priority_queue) {
    ethread->EventQueue.remove(this);
  }
  timeout_at = ink_get_hrtime() + atimeout_in;
  period     = 0;
  immediate  = false;
  mutex      = continuation->mutex;
  if (!in_the_prot_queue) {
    ethread->EventQueueExternal.enqueue_local(this);
  }
}

void
Event::schedule_every(ink_hrtime aperiod, int acallback_event)
{
  callback_event = acallback_event;
  ink_assert(ethread == this_ethread());
  ink_assert(aperiod != 0);
  if (in_the_priority_queue) {
    ethread->EventQueue.remove(this);
  }
  if (aperiod < 0) {
    timeout_at = aperiod;
  } else {
    timeout_at = ink_get_hrtime() + aperiod;
  }
  period    = aperiod;
  immediate = false;
  mutex     = continuation->mutex;
  if (!in_the_prot_queue) {
    ethread->EventQueueExternal.enqueue_local(this);
  }
}

#ifdef ENABLE_EVENT_TRACKER

void
Event::set_location()
{
  _location = ink_backtrace(3);
}

const void *
Event::get_location() const
{
  return _location;
}

#endif
