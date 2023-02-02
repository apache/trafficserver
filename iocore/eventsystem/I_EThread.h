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
#include "tscpp/util/Histogram.h"

// TODO: This would be much nicer to have "run-time" configurable (or something),
// perhaps based on proxy.config.stat_api.max_stats_allowed or other configs. XXX
#define PER_THREAD_DATA (1024 * 1024)

// This is not used by the cache anymore, it uses proxy.config.cache.mutex_retry_delay
// instead.
#define MUTEX_RETRY_DELAY HRTIME_MSECONDS(20)

class DiskHandler;
struct EventIO;

class ServerSessionPool;
class PreWarmQueue;

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
  static thread_local EThread *this_ethread_ptr;
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
      continuation's handler. See the EventProcessor class.
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

  void set_specific() override;

  /* private */

  Event *schedule_local(Event *e);

  InkRand generator = static_cast<uint64_t>(Thread::get_hrtime_updated() ^ reinterpret_cast<uintptr_t>(this));

  /*-------------------------------------------------------*\
  |  UNIX Interface                                         |
  \*-------------------------------------------------------*/

  EThread();
  EThread(ThreadType att, int anid);
  EThread(ThreadType att, Event *e);
  EThread(const EThread &)            = delete;
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
  PreWarmQueue *prewarm_queue            = nullptr;

  /** Default handler used until it is overridden.

      This uses the cond var wait in @a ExternalQueue.
  */
  class DefaultTailHandler : public LoopTailHandler
  {
    explicit DefaultTailHandler(ProtectedQueue &q) : _q(q) {}

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
  } DEFAULT_TAIL_HANDLER = DefaultTailHandler(EventQueueExternal);

  struct Metrics {
    using self_type = Metrics; ///< Self reference type.

    /// Information about loops within the same time slice.
    struct Slice {
      using self_type = Slice;

      /// Data for timing of the loop.
      struct Duration {
        ink_hrtime _start = 0;         ///< The time of the first loop for this sample. Used to mark valid entries.
        ink_hrtime _min   = INT64_MAX; ///< Shortest loop time.
        ink_hrtime _max   = 0;         ///< Longest loop time.
        Duration()        = default;
      } _duration;

      /// Events in the slice.
      struct Events {
        int _min   = INT_MAX; ///< Minimum # of events in a loop.
        int _max   = 0;       ///< Maximum # of events in a loop.
        int _total = 0;       ///< Total # of events.
        Events() {}
      } _events;

      int _count = 0; ///< # of times the loop executed.
      int _wait  = 0; ///< # of timed wait for events

      /** Record the loop start time.
       *
       * @param t Start time.
       * @return @a this
       */
      self_type &record_loop_start(ink_hrtime t);

      /** Record event loop duration.
       *
       * @param delta Duration of the loop.
       * @return @a this.
       */
      self_type &record_loop_duration(ink_hrtime delta);

      /** Record number of events in a loop.
       *
       * @param count Event count.
       * @return
       */
      self_type &record_event_count(int count);

      /// Add @a that to @a this data.
      /// This embodies the custom logic per member concerning whether each is a sum, min, or max.
      Slice &operator+=(Slice const &that);

      /** Slice related statistics.
          THE ORDER IS VERY SENSITIVE.
          More than one part of the code depends on this exact order. Be careful and thorough when changing.
      */
      enum class STAT_ID {
        LOOP_COUNT,      ///< # of event loops executed.
        LOOP_EVENTS,     ///< # of events
        LOOP_EVENTS_MIN, ///< min # of events dispatched in a loop
        LOOP_EVENTS_MAX, ///< max # of events dispatched in a loop
        LOOP_WAIT,       ///< # of loops that did a conditional wait.
        LOOP_TIME_MIN,   ///< Shortest time spent in loop.
        LOOP_TIME_MAX,   ///< Longest time spent in loop.
      };
      /// Number of statistics for a slice.
      static constexpr unsigned N_STAT_ID = unsigned(STAT_ID::LOOP_TIME_MAX) + 1;

      /// Statistic name stems.
      /// These will be qualified by time scale.
      static char const *const STAT_NAME[N_STAT_ID];

      Slice() = default;
    };

    /** The number of slices.
        This is a circular buffer, with one slice per second. We have a bit more than the required 1000
        to provide sufficient slop for cross thread reading of the data (as only the current slice
        is being updated).
    */
    static constexpr int N_SLICES = 1024;
    /// The slices.
    std::array<Slice, N_SLICES> _slice;

    Slice *volatile current_slice = nullptr; ///< The current slice.

    /** The number of time scales used in the event statistics.
        Currently these are 10s, 100s, 1000s.
    */
    static constexpr unsigned N_TIMESCALES = 3;

    /// # of samples for each time scale.
    static constexpr std::array<unsigned, 3> SLICE_SAMPLE_COUNT = {10, 100, 1000};

    /// Total # of stats created for slice metrics.
    static constexpr unsigned N_SLICE_STATS = Slice::N_STAT_ID * N_TIMESCALES;

    /// Back up the metric pointer, wrapping as needed.
    Slice *prev_slice(Slice *current);
    /// Advance the metric pointer, wrapping as needed.
    Slice *next_slice(Slice *current);

    /** Record a loop time sample in the histogram.
     *
     * @param delta Loop time.
     * @return @a this
     */
    self_type &record_loop_time(ink_hrtime delta);

    /** Record total api sample in the histogram.
     *
     * @param delta Duration.
     * @return @a this
     */
    self_type &record_api_time(ink_hrtime delta);

    /// Do any accumulated data decay that's required.
    self_type &decay();

    /// Base name for event loop histogram stats.
    /// The actual stats are determined by the @c Histogram properties.
    static constexpr ts::TextView LOOP_HISTOGRAM_STAT_STEM = "proxy.process.eventloop.time.";
    /// Base bucket size for @c Graph
    static constexpr ts_milliseconds LOOP_HISTOGRAM_BUCKET_SIZE{5};

    /// Histogram type. 7,2 provides a reasonable range (5-2560 ms) and accuracy.
    using Graph = ts::Histogram<7, 2>;
    Graph _loop_timing; ///< Event loop timings.
    /// Base name for event loop histogram stats.
    /// The actual stats are determined by the @c Histogram properties.
    static constexpr ts::TextView API_HISTOGRAM_STAT_STEM = "proxy.process.api.time.";
    /// Base bucket size in milliseconds for plugin API timings.
    static constexpr ts_milliseconds API_HISTOGRAM_BUCKET_SIZE{1};
    Graph _api_timing; ///< Plugin API callout timings.

    /// Data in the histogram needs to decay over time. To avoid races and locks the
    /// summarizing thread bumps this to indicate a decay is needed and doesn't update if
    /// this is non-zero. The event loop does the decay and decrements the count.
    std::atomic<unsigned> _decay_count = 0;
    /// Decay this often.
    static inline std::chrono::duration _decay_delay = std::chrono::seconds{90};
    /// Time of last decay.
    static inline ts_clock::time_point _last_decay_time;

    /// Total number of metric based statistics.
    static constexpr unsigned N_STATS = N_SLICE_STATS + 2 * Graph::N_BUCKETS;

    /// Summarize this instance into a global instance.
    void summarize(self_type &global);
  };

  Metrics metrics;
};

// --- Inline implementation

inline auto
EThread::Metrics::Slice::record_loop_start(ink_hrtime t) -> self_type &
{
  _duration._start = t;
  return *this;
}

inline auto
EThread::Metrics::Slice::record_loop_duration(ink_hrtime delta) -> self_type &
{
  if (delta > _duration._max) {
    _duration._max = delta;
  }
  if (delta < _duration._min) {
    _duration._min = delta;
  }
  return *this;
}

inline auto
EThread::Metrics::Slice::record_event_count(int count) -> self_type &
{
  if (count < _events._min) {
    _events._min = count;
  }
  if (count > _events._max) {
    _events._max = _count;
  }
  _events._total += count;
  return *this;
}

inline EThread::Metrics::Slice *
EThread::Metrics::prev_slice(EThread::Metrics::Slice *current)
{
  return --current < _slice.data() ? &_slice[N_SLICES - 1] : current;
}

inline EThread::Metrics::Slice *
EThread::Metrics::next_slice(EThread::Metrics::Slice *current)
{
  return ++current > &_slice[N_SLICES - 1] ? _slice.data() : current;
}

inline auto
EThread::Metrics::record_loop_time(ink_hrtime delta) -> self_type &
{
  static auto constexpr DIVISOR = std::chrono::duration_cast<ts_nanoseconds>(LOOP_HISTOGRAM_BUCKET_SIZE).count();
  current_slice->record_loop_duration(delta);
  _loop_timing(delta / DIVISOR);
  return *this;
}

inline auto
EThread::Metrics::record_api_time(ink_hrtime delta) -> self_type &
{
  static auto constexpr DIVISOR = std::chrono::duration_cast<ts_nanoseconds>(LOOP_HISTOGRAM_BUCKET_SIZE).count();
  _api_timing(delta / DIVISOR);
  return *this;
}

inline auto
EThread::Metrics::decay() -> self_type &
{
  while (_decay_count) {
    _loop_timing.decay();
    _api_timing.decay();
    --_decay_count;
  }
  return *this;
}

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
