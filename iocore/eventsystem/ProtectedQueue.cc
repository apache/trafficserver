/** @file

  FIFO queue

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

  @section details Details

  ProtectedQueue implements a FIFO queue with the following functionality:
    -# Multiple threads could be simultaneously trying to enqueue and
      dequeue. Hence the queue needs to be protected with mutex.
    -# In case the queue is empty, dequeue() sleeps for a specified amount
      of time, or until a new element is inserted, whichever is earlier.

*/

#include "P_EventSystem.h"

// The protected queue is designed to delay signaling of threads
// until some amount of work has been completed on the current thread
// in order to prevent excess context switches.
//
// Defining EAGER_SIGNALLING disables this behavior and causes
// threads to be made runnable immediately.
//
// #define EAGER_SIGNALLING

extern ClassAllocator<Event> eventAllocator;

void
ProtectedQueue::enqueue(Event *e, bool fast_signal)
{
  ink_assert(!e->in_the_prot_queue && !e->in_the_priority_queue);
  EThread *e_ethread   = e->ethread;
  e->in_the_prot_queue = 1;
  bool was_empty       = (ink_atomiclist_push(&al, e) == nullptr);

  if (was_empty) {
    EThread *inserting_thread = this_ethread();
    // queue e->ethread in the list of threads to be signalled
    // inserting_thread == 0 means it is not a regular EThread
    if (inserting_thread != e_ethread) {
      e_ethread->tail_cb->signalActivity();
    }
  }
}

void
flush_signals(EThread *thr)
{
  ink_assert(this_ethread() == thr);
  int n = thr->n_ethreads_to_be_signalled;
  if (n > eventProcessor.n_ethreads) {
    n = eventProcessor.n_ethreads; // MAX
  }
  int i;

  for (i = 0; i < n; i++) {
    if (thr->ethreads_to_be_signalled[i]) {
      thr->ethreads_to_be_signalled[i]->tail_cb->signalActivity();
      thr->ethreads_to_be_signalled[i] = nullptr;
    }
  }
  thr->n_ethreads_to_be_signalled = 0;
}

void
ProtectedQueue::dequeue_timed(ink_hrtime cur_time, ink_hrtime timeout, bool sleep)
{
  (void)cur_time;
  if (sleep) {
    this->wait(timeout);
  }
  this->dequeue_external();
}

void
ProtectedQueue::dequeue_external()
{
  Event *e = static_cast<Event *>(ink_atomiclist_popall(&al));
  // invert the list, to preserve order
  SLL<Event, Event::Link_link> l, t;
  t.head = e;
  while ((e = t.pop())) {
    l.push(e);
  }
  // insert into localQueue
  while ((e = l.pop())) {
    if (!e->cancelled) {
      localQueue.enqueue(e);
    } else {
      e->mutex = nullptr;
      eventAllocator.free(e);
    }
  }
}

void
ProtectedQueue::wait(ink_hrtime timeout)
{
  /* If there are no external events available, will do a cond_timedwait.
   *
   *   - The `EThread::lock` will be released,
   *   - And then the Event Thread goes to sleep and waits for the wakeup signal of `EThread::might_have_data`,
   *   - The `EThread::lock` will be locked again when the Event Thread wakes up.
   */
  if (INK_ATOMICLIST_EMPTY(al)) {
    timespec ts = ink_hrtime_to_timespec(timeout);
    ink_cond_timedwait(&might_have_data, &lock, &ts);
  }
}
