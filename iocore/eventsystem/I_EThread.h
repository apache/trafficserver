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

#include "tscore/ink_platform.h"
#include "tscore/ink_rand.h"
#include "tscore/I_Version.h"
#include "I_Thread.h"
#include "I_PriorityEventQueue.h"
#include "I_ProtectedQueue.h"

// TODO: This would be much nicer to have "run-time" configurable (or something),
// perhaps based on proxy.config.stat_api.max_stats_allowed or other configs. XXX
#define PER_THREAD_DATA (1024 * 1024)

// This is not used by the cache anymore, it uses proxy.config.cache.mutex_retry_delay
// instead.
#define MUTEX_RETRY_DELAY HRTIME_MSECONDS(20)

struct DiskHandler;
struct EventIO;

class ServerSessionPool;
class Event;
class Continuation;

enum ThreadType {
  REGULAR = 0,
  DEDICATED,
};

/**
  Event System specific type of thread.

  The EThread class is the type of thread created and managed by
  the Event System. It is one of the available interfaces for
  scheduling events in the event system (another two are the Event
  and EventProcessor classes).

  In order to handle events, each EThread object has two event
  queues, one external and one internal. The external queue is
  provided for users of the EThread (clients) to append events to
  that particular thread. Since it can be accessed by other threads
  at the same time, operations using it must proceed in an atomic
  fashion.

  The internal queue, in the other hand, is used exclusively by the
  EThread to process timed events within a certain time frame. These
  events are queued internally and they may come from the external
  queue as well.

  Scheduling Interface:

  There are eight scheduling functions provided by EThread and
  they are a wrapper around their counterparts in EventProcessor.

  @see EventProcessor
  @see Event

*/
class EThread : public Thread
{
public:
  /** Handler for tail of event loop.

      The event loop should not spin. To avoid that a tail handler is called to block for a limited time.
      This is a protocol class that defines the interface to the handler.
  */
  class LoopTailHandler
  {
  public:
    /** Called at the end of the event loop to block.
        @a timeout is the maximum length of time (in ns) to block.
    */
    virtual int waitForActivity(ink_hrtime timeout) = 0;
    /** Unblock.

        This is required to unblock (wake up) the block created by calling @a cb.
    */
    virtual void signalActivity() = 0;

    virtual ~LoopTailHandler() {}
  };

  /*-------------------------------------------------------*\
  |  Common Interface                                       |
  \*-------------------------------------------------------*/

  /**
    Schedules the continuation on this EThread to receive an event
    as soon as possible.

    Forwards to the EventProcessor the schedule of the callback to
    the continuation 'c' as soon as possible. The event is assigned
    to EThread.

    @param c Continuation to be called back as soon as possible.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the the EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return Reference to an Event object representing the scheduling
      of this callback.

  */
  Event *schedule_imm(Continuation *c, int callback_event = EVENT_IMMEDIATE, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    at the given timeout.

    Forwards the request to the EventProcessor to schedule the
    callback to the continuation 'c' at the time specified in
    'atimeout_at'. The event is assigned to this EThread.

    @param c Continuation to be called back at the time specified
      in 'atimeout_at'.
    @param atimeout_at Time value at which to callback.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the scheduling
      of this callback.

  */
  Event *schedule_at(Continuation *c, ink_hrtime atimeout_at, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    after the timeout elapses.

    Instructs the EventProcessor to schedule the callback to the
    continuation 'c' after the time specified in atimeout_in elapses.
    The event is assigned to this EThread.

    @param c Continuation to be called back after the timeout elapses.
    @param atimeout_in Amount of time after which to callback.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the scheduling
      of this callback.

  */
  Event *schedule_in(Continuation *c, ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    periodically.

    Schedules the callback to the continuation 'c' in the EventProcessor
    to occur every time 'aperiod' elapses. It is scheduled on this
    EThread.

    @param c Continuation to call back every time 'aperiod' elapses.
    @param aperiod Duration of the time period between callbacks.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the Remarks section in the
      EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the scheduling
      of this callback.

  */
  Event *schedule_every(Continuation *c, ink_hrtime aperiod, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    as soon as possible.

    Schedules the callback to the continuation 'c' as soon as
    possible. The event is assigned to this EThread.

    @param c Continuation to be called back as soon as possible.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the scheduling
      of this callback.

  */
  Event *schedule_imm_local(Continuation *c, int callback_event = EVENT_IMMEDIATE, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    at the given timeout.

    Schedules the callback to the continuation 'c' at the time
    specified in 'atimeout_at'. The event is assigned to this
    EThread.

    @param c Continuation to be called back at the time specified
      in 'atimeout_at'.
    @param atimeout_at Time value at which to callback.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the scheduling
      of this callback.

  */
  Event *schedule_at_local(Continuation *c, ink_hrtime atimeout_at, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    after the timeout elapses.

    Schedules the callback to the continuation 'c' after the time
    specified in atimeout_in elapses. The event is assigned to this
    EThread.

    @param c Continuation to be called back after the timeout elapses.
    @param atimeout_in Amount of time after which to callback.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the Remarks section in the
      EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the scheduling
      of this callback.

  */
  Event *schedule_in_local(Continuation *c, ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    periodically.

    Schedules the callback to the continuation 'c' to occur every
    time 'aperiod' elapses. It is scheduled on this EThread.

    @param c Continuation to call back every time 'aperiod' elapses.
    @param aperiod Duration of the time period between callbacks.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the Remarks section in the
      EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the scheduling
      of this callback.

  */
  Event *schedule_every_local(Continuation *c, ink_hrtime aperiod, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /** Schedule an event called once when the thread is spawned.

      This is useful only for regular threads and if called before @c Thread::start. The event will be
      called first before the event loop.

      @Note This will override the event for a dedicate thread so that this is called instead of the
      event passed to the constructor.
  */
  Event *schedule_spawn(Continuation *c, int ev = EVENT_IMMEDIATE, void *cookie = nullptr);

  // Set the tail handler.
  void set_tail_handler(LoopTailHandler *handler);

  /* private */

  Event *schedule_local(Event *e);

  InkRand generator = static_cast<uint64_t>(Thread::get_hrtime_updated() ^ reinterpret_cast<uintptr_t>(this));

  /*-------------------------------------------------------*\
  |  UNIX Interface                                         |
  \*-------------------------------------------------------*/

  EThread();
  EThread(ThreadType att, int anid);
  EThread(ThreadType att, Event *e);
  EThread(const EThread &) = delete;
  EThread &operator=(const EThread &) = delete;
  ~EThread() override;

  Event *schedule(Event *e);

  /** Block of memory to allocate thread specific data e.g. stat system arrays. */
  char thread_private[PER_THREAD_DATA];

  /** Private Data for the Disk Processor. */
  DiskHandler *diskHandler = nullptr;

  /** Private Data for AIO. */
  Que(Continuation, link) aio_ops;

  ProtectedQueue EventQueueExternal;
  PriorityEventQueue EventQueue;

  static constexpr int NO_ETHREAD_ID = -1;
  int id                             = NO_ETHREAD_ID;
  unsigned int event_types           = 0;
  bool is_event_type(EventType et);
  void set_event_type(EventType et);

  // Private Interface

  void execute() override;
  void execute_regular();
  void process_queue(Que(Event, link) * NegativeQueue, int *ev_count, int *nq_count);
  void process_event(Event *e, int calling_code);
  void free_event(Event *e);
  LoopTailHandler *tail_cb = &DEFAULT_TAIL_HANDLER;

#if HAVE_EVENTFD
  int evfd = ts::NO_FD;
#else
  int evpipe[2];
#endif
  EventIO *ep = nullptr;

  ThreadType tt = REGULAR;
  /** Initial event to call, before any scheduling.

      For dedicated threads this is the only event called.
      For regular threads this is called first before the event loop starts.
      @internal For regular threads this is used by the EventProcessor to get called back after
      the thread starts but before any other events can be dispatched to provide initializations
      needed for the thread.
  */
  Event *start_event = nullptr;

  ServerSessionPool *server_session_pool = nullptr;

  /** Default handler used until it is overridden.

      This uses the cond var wait in @a ExternalQueue.
  */
  class DefaultTailHandler : public LoopTailHandler
  {
    // cppcheck-suppress noExplicitConstructor; allow implicit conversion
    DefaultTailHandler(ProtectedQueue &q) : _q(q) {}

    int
    waitForActivity(ink_hrtime timeout) override
    {
      _q.wait(Thread::get_hrtime() + timeout);
      return 0;
    }
    void
    signalActivity() override
    {
      /* Try to acquire the `EThread::lock` of the Event Thread:
       *   - Acquired, indicating that the Event Thread is sleep,
       *               must send a wakeup signal to the Event Thread.
       *   - Failed, indicating that the Event Thread is busy, do nothing.
       */
      (void)_q.try_signal();
    }

    ProtectedQueue &_q;

    friend class EThread;
  } DEFAULT_TAIL_HANDLER = EventQueueExternal;

  /// Statistics data for event dispatching.
  struct EventMetrics {
    /// Time the loop was active, not including wait time but including event dispatch time.
    struct LoopTimes {
      ink_hrtime _start = 0;         ///< The time of the first loop for this sample. Used to mark valid entries.
      ink_hrtime _min   = INT64_MAX; ///< Shortest loop time.
      ink_hrtime _max   = 0;         ///< Longest loop time.
      LoopTimes() {}
    } _loop_time;

    struct Events {
      int _min   = INT_MAX;
      int _max   = 0;
      int _total = 0;
      Events() {}
    } _events;

    int _count = 0; ///< # of times the loop executed.
    int _wait  = 0; ///< # of timed wait for events

    /// Add @a that to @a this data.
    /// This embodies the custom logic per member concerning whether each is a sum, min, or max.
    EventMetrics &operator+=(EventMetrics const &that);

    EventMetrics() {}
  };

  /** The number of metric blocks kept.
      This is a circular buffer, with one block per second. We have a bit more than the required 1000
      to provide sufficient slop for cross thread reading of the data (as only the current metric block
      is being updated).
  */
  static int const N_EVENT_METRICS = 1024;

  volatile EventMetrics *current_metric = nullptr; ///< The current element of @a metrics
  EventMetrics metrics[N_EVENT_METRICS];

  /** The various stats provided to the administrator.
      THE ORDER IS VERY SENSITIVE.
      More than one part of the code depends on this exact order. Be careful and thorough when changing.
  */
  enum STAT_ID {
    STAT_LOOP_COUNT,      ///< # of event loops executed.
    STAT_LOOP_EVENTS,     ///< # of events
    STAT_LOOP_EVENTS_MIN, ///< min # of events dispatched in a loop
    STAT_LOOP_EVENTS_MAX, ///< max # of events dispatched in a loop
    STAT_LOOP_WAIT,       ///< # of loops that did a conditional wait.
    STAT_LOOP_TIME_MIN,   ///< Shortest time spent in loop.
    STAT_LOOP_TIME_MAX,   ///< Longest time spent in loop.
    N_EVENT_STATS         ///< NOT A VALID STAT INDEX - # of different stat types.
  };

  static char const *const STAT_NAME[N_EVENT_STATS];

  /** The number of time scales used in the event statistics.
      Currently these are 10s, 100s, 1000s.
  */
  static int const N_EVENT_TIMESCALES = 3;
  /// # of samples for each time scale.
  static int const SAMPLE_COUNT[N_EVENT_TIMESCALES];

  /// Process the last 1000s of data and write out the summaries to @a summary.
  void summarize_stats(EventMetrics summary[N_EVENT_TIMESCALES]);
  /// Back up the metric pointer, wrapping as needed.
  EventMetrics *
  prev(EventMetrics volatile *current)
  {
    return const_cast<EventMetrics *>(--current < metrics ? &metrics[N_EVENT_METRICS - 1] : current); // cast to remove volatile
  }
  /// Advance the metric pointer, wrapping as needed.
  EventMetrics *
  next(EventMetrics volatile *current)
  {
    return const_cast<EventMetrics *>(++current > &metrics[N_EVENT_METRICS - 1] ? metrics : current); // cast to remove volatile
  }
};

/**
  This is used so that we dont use up operator new(size_t, void *)
  which users might want to define for themselves.

*/
class ink_dummy_for_new
{
};

inline void *
operator new(size_t, ink_dummy_for_new *p)
{
  return (void *)p;
}
#define ETHREAD_GET_PTR(thread, offset) ((void *)((char *)(thread) + (offset)))

extern EThread *this_ethread();

extern int thread_max_heartbeat_mseconds;
