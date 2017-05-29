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
      if (!inserting_thread || !inserting_thread->ethreads_to_be_signalled) {
        signal();
        if (fast_signal) {
          if (e_ethread->signal_hook) {
            e_ethread->signal_hook(e_ethread);
          }
        }
      } else {
#ifdef EAGER_SIGNALLING
        // Try to signal now and avoid deferred posting.
        if (e_ethread->EventQueueExternal.try_signal())
          return;
#endif
        if (fast_signal) {
          if (e_ethread->signal_hook) {
            e_ethread->signal_hook(e_ethread);
          }
        }
        int &t          = inserting_thread->n_ethreads_to_be_signalled;
        EThread **sig_e = inserting_thread->ethreads_to_be_signalled;
        if ((t + 1) >= eventProcessor.n_ethreads) {
          // we have run out of room
          if ((t + 1) == eventProcessor.n_ethreads) {
            // convert to direct map, put each ethread (sig_e[i]) into
            // the direct map loation: sig_e[sig_e[i]->id]
            for (int i = 0; i < t; i++) {
              EThread *cur = sig_e[i]; // put this ethread
              while (cur) {
                EThread *next = sig_e[cur->id]; // into this location
                if (next == cur) {
                  break;
                }
                sig_e[cur->id] = cur;
                cur            = next;
              }
              // if not overwritten
              if (sig_e[i] && sig_e[i]->id != i) {
                sig_e[i] = nullptr;
              }
            }
            t++;
          }
          // we have a direct map, insert this EThread
          sig_e[e_ethread->id] = e_ethread;
        } else {
          // insert into vector
          sig_e[t++] = e_ethread;
        }
      }
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

// Since the lock is only there to prevent a race in ink_cond_timedwait
// the lock is taken only for a short time, thus it is unlikely that
// this code has any effect.
#ifdef EAGER_SIGNALLING
  for (i = 0; i < n; i++) {
    // Try to signal as many threads as possible without blocking.
    if (thr->ethreads_to_be_signalled[i]) {
      if (thr->ethreads_to_be_signalled[i]->EventQueueExternal.try_signal())
        thr->ethreads_to_be_signalled[i] = 0;
    }
  }
#endif
  for (i = 0; i < n; i++) {
    if (thr->ethreads_to_be_signalled[i]) {
      thr->ethreads_to_be_signalled[i]->EventQueueExternal.signal();
      if (thr->ethreads_to_be_signalled[i]->signal_hook) {
        thr->ethreads_to_be_signalled[i]->signal_hook(thr->ethreads_to_be_signalled[i]);
      }
      thr->ethreads_to_be_signalled[i] = nullptr;
    }
  }
  thr->n_ethreads_to_be_signalled = 0;
}

void
ProtectedQueue::dequeue_timed(ink_hrtime cur_time, ink_hrtime timeout, bool sleep)
{
  (void)cur_time;
  Event *e;
  if (sleep) {
    ink_mutex_acquire(&lock);
    if (INK_ATOMICLIST_EMPTY(al)) {
      timespec ts = ink_hrtime_to_timespec(timeout);
      ink_cond_timedwait(&might_have_data, &lock, &ts);
    }
    ink_mutex_release(&lock);
  }

  e = (Event *)ink_atomiclist_popall(&al);
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
