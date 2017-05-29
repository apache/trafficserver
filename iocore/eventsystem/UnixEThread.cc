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

//////////////////////////////////////////////////////////////////////
//
// The EThread Class
//
/////////////////////////////////////////////////////////////////////
#include "P_EventSystem.h"

#if HAVE_EVENTFD
#include <sys/eventfd.h>
#endif

struct AIOCallback;

#define MAX_HEARTBEATS_MISSED 10
#define NO_HEARTBEAT -1
#define THREAD_MAX_HEARTBEAT_MSECONDS 60
#define NO_ETHREAD_ID -1

volatile bool shutdown_event_system = false;

EThread::EThread() : generator((uint64_t)Thread::get_hrtime_updated() ^ (uint64_t)(uintptr_t)this), id(NO_ETHREAD_ID)
{
  memset(thread_private, 0, PER_THREAD_DATA);
}

EThread::EThread(ThreadType att, int anid)
  : generator((uint64_t)Thread::get_hrtime_updated() ^ (uint64_t)(uintptr_t)this), id(anid), tt(att)
{
  ethreads_to_be_signalled = (EThread **)ats_malloc(MAX_EVENT_THREADS * sizeof(EThread *));
  memset((char *)ethreads_to_be_signalled, 0, MAX_EVENT_THREADS * sizeof(EThread *));
  memset(thread_private, 0, PER_THREAD_DATA);
#if HAVE_EVENTFD
  evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evfd < 0) {
    if (errno == EINVAL) { // flags invalid for kernel <= 2.6.26
      evfd = eventfd(0, 0);
      if (evfd < 0) {
        Fatal("EThread::EThread: %d=eventfd(0,0),errno(%d)", evfd, errno);
      }
    } else {
      Fatal("EThread::EThread: %d=eventfd(0,EFD_NONBLOCK | EFD_CLOEXEC),errno(%d)", evfd, errno);
    }
  }
#elif TS_USE_PORT
/* Solaris ports requires no crutches to do cross thread signaling.
 * We'll just port_send the event straight over the port.
 */
#else
  ink_release_assert(pipe(evpipe) >= 0);
  fcntl(evpipe[0], F_SETFD, FD_CLOEXEC);
  fcntl(evpipe[0], F_SETFL, O_NONBLOCK);
  fcntl(evpipe[1], F_SETFD, FD_CLOEXEC);
  fcntl(evpipe[1], F_SETFL, O_NONBLOCK);
#endif
}

EThread::EThread(ThreadType att, Event *e)
  : generator((uint32_t)((uintptr_t)time(nullptr) ^ (uintptr_t)this)),
    ethreads_to_be_signalled(nullptr),
    n_ethreads_to_be_signalled(0),
    id(NO_ETHREAD_ID),
    event_types(0),
    signal_hook(nullptr),
    tt(att),
    oneevent(e)
{
  ink_assert(att == DEDICATED);
  memset(thread_private, 0, PER_THREAD_DATA);
}

// Provide a destructor so that SDK functions which create and destroy
// threads won't have to deal with EThread memory deallocation.
EThread::~EThread()
{
  if (n_ethreads_to_be_signalled > 0) {
    flush_signals(this);
  }
  ats_free(ethreads_to_be_signalled);
  // TODO: This can't be deleted ....
  // delete[]l1_hash;
}

bool
EThread::is_event_type(EventType et)
{
  return !!(event_types & (1 << (int)et));
}

void
EThread::set_event_type(EventType et)
{
  event_types |= (1 << (int)et);
}

void
EThread::process_event(Event *e, int calling_code)
{
  ink_assert((!e->in_the_prot_queue && !e->in_the_priority_queue));
  MUTEX_TRY_LOCK_FOR(lock, e->mutex, this, e->continuation);
  if (!lock.is_locked()) {
    e->timeout_at = cur_time + DELAY_FOR_RETRY;
    EventQueueExternal.enqueue_local(e);
  } else {
    if (e->cancelled) {
      free_event(e);
      return;
    }
    Continuation *c_temp = e->continuation;
    e->continuation->handleEvent(calling_code, e);
    ink_assert(!e->in_the_priority_queue);
    ink_assert(c_temp == e->continuation);
    MUTEX_RELEASE(lock);
    if (e->period) {
      if (!e->in_the_prot_queue && !e->in_the_priority_queue) {
        if (e->period < 0) {
          e->timeout_at = e->period;
        } else {
          this->get_hrtime_updated();
          e->timeout_at = cur_time + e->period;
          if (e->timeout_at < cur_time) {
            e->timeout_at = cur_time;
          }
        }
        EventQueueExternal.enqueue_local(e);
      }
    } else if (!e->in_the_prot_queue && !e->in_the_priority_queue) {
      free_event(e);
    }
  }
}

//
// void  EThread::execute()
//
// Execute loops forever on:
// Find the earliest event.
// Sleep until the event time or until an earlier event is inserted
// When its time for the event, try to get the appropriate continuation
// lock. If successful, call the continuation, otherwise put the event back
// into the queue.
//

void
EThread::execute()
{
  switch (tt) {
  case REGULAR: {
    Event *e;
    Que(Event, link) NegativeQueue;
    ink_hrtime next_time = 0;

    // give priority to immediate events
    for (;;) {
      if (unlikely(shutdown_event_system == true)) {
        return;
      }
      // execute all the available external events that have
      // already been dequeued
      cur_time = Thread::get_hrtime_updated();
      while ((e = EventQueueExternal.dequeue_local())) {
        if (e->cancelled) {
          free_event(e);
        } else if (!e->timeout_at) { // IMMEDIATE
          ink_assert(e->period == 0);
          process_event(e, e->callback_event);
        } else if (e->timeout_at > 0) { // INTERVAL
          EventQueue.enqueue(e, cur_time);
        } else { // NEGATIVE
          Event *p = nullptr;
          Event *a = NegativeQueue.head;
          while (a && a->timeout_at > e->timeout_at) {
            p = a;
            a = a->link.next;
          }
          if (!a) {
            NegativeQueue.enqueue(e);
          } else {
            NegativeQueue.insert(e, p);
          }
        }
      }
      bool done_one;
      do {
        done_one = false;
        // execute all the eligible internal events
        EventQueue.check_ready(cur_time, this);
        while ((e = EventQueue.dequeue_ready(cur_time))) {
          ink_assert(e);
          ink_assert(e->timeout_at > 0);
          if (e->cancelled) {
            free_event(e);
          } else {
            done_one = true;
            process_event(e, e->callback_event);
          }
        }
      } while (done_one);
      // execute any negative (poll) events
      if (NegativeQueue.head) {
        if (n_ethreads_to_be_signalled) {
          flush_signals(this);
        }
        // dequeue all the external events and put them in a local
        // queue. If there are no external events available, don't
        // do a cond_timedwait.
        if (!INK_ATOMICLIST_EMPTY(EventQueueExternal.al)) {
          EventQueueExternal.dequeue_timed(cur_time, next_time, false);
        }
        while ((e = EventQueueExternal.dequeue_local())) {
          if (!e->timeout_at) {
            process_event(e, e->callback_event);
          } else {
            if (e->cancelled) {
              free_event(e);
            } else {
              // If its a negative event, it must be a result of
              // a negative event, which has been turned into a
              // timed-event (because of a missed lock), executed
              // before the poll. So, it must
              // be executed in this round (because you can't have
              // more than one poll between two executions of a
              // negative event)
              if (e->timeout_at < 0) {
                Event *p = nullptr;
                Event *a = NegativeQueue.head;
                while (a && a->timeout_at > e->timeout_at) {
                  p = a;
                  a = a->link.next;
                }
                if (!a) {
                  NegativeQueue.enqueue(e);
                } else {
                  NegativeQueue.insert(e, p);
                }
              } else {
                EventQueue.enqueue(e, cur_time);
              }
            }
          }
        }
        // execute poll events
        while ((e = NegativeQueue.dequeue())) {
          process_event(e, EVENT_POLL);
        }
        if (!INK_ATOMICLIST_EMPTY(EventQueueExternal.al)) {
          EventQueueExternal.dequeue_timed(cur_time, next_time, false);
        }
      } else { // Means there are no negative events
        next_time             = EventQueue.earliest_timeout();
        ink_hrtime sleep_time = next_time - cur_time;

        if (sleep_time > THREAD_MAX_HEARTBEAT_MSECONDS * HRTIME_MSECOND) {
          next_time = cur_time + THREAD_MAX_HEARTBEAT_MSECONDS * HRTIME_MSECOND;
        }
        // dequeue all the external events and put them in a local
        // queue. If there are no external events available, do a
        // cond_timedwait.
        if (n_ethreads_to_be_signalled) {
          flush_signals(this);
        }
        EventQueueExternal.dequeue_timed(cur_time, next_time, true);
      }
    }
  }

  case DEDICATED: {
    // coverity[lock]
    MUTEX_TAKE_LOCK_FOR(oneevent->mutex, this, oneevent->continuation);
    oneevent->continuation->handleEvent(EVENT_IMMEDIATE, oneevent);
    MUTEX_UNTAKE_LOCK(oneevent->mutex, this);
    free_event(oneevent);
    break;
  }

  default:
    ink_assert(!"bad case value (execute)");
    break;
  } /* End switch */
  // coverity[missing_unlock]
}
