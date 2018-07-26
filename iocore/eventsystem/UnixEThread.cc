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

#define NO_HEARTBEAT -1
#define THREAD_MAX_HEARTBEAT_MSECONDS 60

// !! THIS MUST BE IN THE ENUM ORDER !!
char const *const EThread::STAT_NAME[] = {"proxy.process.eventloop.count",      "proxy.process.eventloop.events",
                                          "proxy.process.eventloop.events.min", "proxy.process.eventloop.events.max",
                                          "proxy.process.eventloop.wait",       "proxy.process.eventloop.time.min",
                                          "proxy.process.eventloop.time.max"};

int const EThread::SAMPLE_COUNT[N_EVENT_TIMESCALES] = {10, 100, 1000};

bool shutdown_event_system = false;

int thread_max_heartbeat_mseconds = THREAD_MAX_HEARTBEAT_MSECONDS;

EThread::EThread()
{
  memset(thread_private, 0, PER_THREAD_DATA);
}

EThread::EThread(ThreadType att, int anid) : id(anid), tt(att)
{
  ethreads_to_be_signalled = (EThread **)ats_malloc(MAX_EVENT_THREADS * sizeof(EThread *));
  memset(ethreads_to_be_signalled, 0, MAX_EVENT_THREADS * sizeof(EThread *));
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

EThread::EThread(ThreadType att, Event *e) : tt(att), start_event(e)
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
  return (event_types & (1 << static_cast<int>(et))) != 0;
}

void
EThread::set_event_type(EventType et)
{
  event_types |= (1 << static_cast<int>(et));
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
    // Make sure that the contination is locked before calling the handler
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

void
EThread::process_queue(Que(Event, link) * NegativeQueue, int *ev_count, int *nq_count)
{
  Event *e;

  // Move events from the external thread safe queues to the local queue.
  EventQueueExternal.dequeue_external();

  // execute all the available external events that have
  // already been dequeued
  while ((e = EventQueueExternal.dequeue_local())) {
    ++(*ev_count);
    if (e->cancelled) {
      free_event(e);
    } else if (!e->timeout_at) { // IMMEDIATE
      ink_assert(e->period == 0);
      process_event(e, e->callback_event);
    } else if (e->timeout_at > 0) { // INTERVAL
      EventQueue.enqueue(e, cur_time);
    } else { // NEGATIVE
      Event *p = nullptr;
      Event *a = NegativeQueue->head;
      while (a && a->timeout_at > e->timeout_at) {
        p = a;
        a = a->link.next;
      }
      if (!a) {
        NegativeQueue->enqueue(e);
      } else {
        NegativeQueue->insert(e, p);
      }
    }
    ++(*nq_count);
  }
}

void
EThread::execute_regular()
{
  Event *e;
  Que(Event, link) NegativeQueue;
  ink_hrtime next_time = 0;
  ink_hrtime delta     = 0;    // time spent in the event loop
  ink_hrtime loop_start_time;  // Time the loop started.
  ink_hrtime loop_finish_time; // Time at the end of the loop.

  // Track this so we can update on boundary crossing.
  EventMetrics *prev_metric = this->prev(metrics + (ink_get_hrtime_internal() / HRTIME_SECOND) % N_EVENT_METRICS);

  int nq_count = 0;
  int ev_count = 0;

  // A statically initialized instance we can use as a prototype for initializing other instances.
  static EventMetrics METRIC_INIT;

  // give priority to immediate events
  for (;;) {
    if (unlikely(shutdown_event_system == true)) {
      return;
    }

    loop_start_time = Thread::get_hrtime_updated();
    nq_count        = 0; // count # of elements put on negative queue.
    ev_count        = 0; // # of events handled.

    current_metric = metrics + (loop_start_time / HRTIME_SECOND) % N_EVENT_METRICS;
    if (current_metric != prev_metric) {
      // Mixed feelings - really this shouldn't be needed, but just in case more than one entry is
      // skipped, clear them all.
      do {
        memcpy((prev_metric = this->next(prev_metric)), &METRIC_INIT, sizeof(METRIC_INIT));
      } while (current_metric != prev_metric);
      current_metric->_loop_time._start = loop_start_time;
    }
    ++(current_metric->_count);

    process_queue(&NegativeQueue, &ev_count, &nq_count);

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
      process_queue(&NegativeQueue, &ev_count, &nq_count);

      // execute poll events
      while ((e = NegativeQueue.dequeue())) {
        process_event(e, EVENT_POLL);
      }
    }

    next_time             = EventQueue.earliest_timeout();
    ink_hrtime sleep_time = next_time - Thread::get_hrtime_updated();
    if (sleep_time > 0) {
      sleep_time = std::min(sleep_time, HRTIME_MSECONDS(thread_max_heartbeat_mseconds));
      ++(current_metric->_wait);
    } else {
      sleep_time = 0;
    }

    if (n_ethreads_to_be_signalled) {
      flush_signals(this);
    }

    tail_cb->waitForActivity(sleep_time);

    // loop cleanup
    loop_finish_time = this->get_hrtime_updated();
    delta            = loop_finish_time - loop_start_time;

    // This can happen due to time of day adjustments (which apparently happen quite frequently). I
    // tried using the monotonic clock to get around this but it was *very* stuttery (up to hundreds
    // of milliseconds), far too much to be actually used.
    if (delta > 0) {
      if (delta > current_metric->_loop_time._max) {
        current_metric->_loop_time._max = delta;
      }
      if (delta < current_metric->_loop_time._min) {
        current_metric->_loop_time._min = delta;
      }
    }
    if (ev_count < current_metric->_events._min) {
      current_metric->_events._min = ev_count;
    }
    if (ev_count > current_metric->_events._max) {
      current_metric->_events._max = ev_count;
    }
    current_metric->_events._total += ev_count;
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
  // Do the start event first.
  // coverity[lock]
  if (start_event) {
    MUTEX_TAKE_LOCK_FOR(start_event->mutex, this, start_event->continuation);
    start_event->continuation->handleEvent(EVENT_IMMEDIATE, start_event);
    MUTEX_UNTAKE_LOCK(start_event->mutex, this);
    free_event(start_event);
    start_event = nullptr;
  }

  switch (tt) {
  case REGULAR: {
    this->execute_regular();
    break;
  }
  case DEDICATED: {
    break;
  }
  default:
    ink_assert(!"bad case value (execute)");
    break;
  } /* End switch */
  // coverity[missing_unlock]
}

EThread::EventMetrics &
EThread::EventMetrics::operator+=(EventMetrics const &that)
{
  this->_events._max = std::max(this->_events._max, that._events._max);
  this->_events._min = std::min(this->_events._min, that._events._min);
  this->_events._total += that._events._total;
  this->_loop_time._min = std::min(this->_loop_time._min, that._loop_time._min);
  this->_loop_time._max = std::max(this->_loop_time._max, that._loop_time._max);
  this->_count += that._count;
  this->_wait += that._wait;
  return *this;
}

void
EThread::summarize_stats(EventMetrics summary[N_EVENT_TIMESCALES])
{
  // Accumulate in local first so each sample only needs to be processed once,
  // not N_EVENT_TIMESCALES times.
  EventMetrics sum;

  // To avoid race conditions, we back up one from the current metric block. It's close enough
  // and won't be updated during the time this method runs so it should be thread safe.
  EventMetrics *m = this->prev(current_metric);

  for (int t = 0; t < N_EVENT_TIMESCALES; ++t) {
    int count = SAMPLE_COUNT[t];
    if (t > 0) {
      count -= SAMPLE_COUNT[t - 1];
    }
    while (--count >= 0) {
      if (0 != m->_loop_time._start) {
        sum += *m;
      }
      m = this->prev(m);
    }
    summary[t] += sum; // push out to return vector.
  }
}
