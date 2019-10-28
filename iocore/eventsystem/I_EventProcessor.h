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
#include "I_Continuation.h"
#include "I_Processor.h"
#include "I_Event.h"
#include <atomic>

#ifdef TS_MAX_THREADS_IN_EACH_THREAD_TYPE
constexpr int MAX_THREADS_IN_EACH_TYPE = TS_MAX_THREADS_IN_EACH_THREAD_TYPE;
#else
constexpr int MAX_THREADS_IN_EACH_TYPE = 3072;
#endif

#ifdef TS_MAX_NUMBER_EVENT_THREADS
constexpr int MAX_EVENT_THREADS = TS_MAX_NUMBER_EVENT_THREADS;
#else
constexpr int MAX_EVENT_THREADS        = 4096;
#endif

class EThread;

/**
  Main processor for the Event System. The EventProcessor is the core
  component of the Event System. Once started, it is responsible for
  creating and managing groups of threads that execute user-defined
  tasks asynchronously at a given time or periodically.

  The EventProcessor provides a set of scheduling functions through
  which you can specify continuations to be called back by one of its
  threads. These function calls do not block. Instead they return an
  Event object and schedule the callback to the continuation passed in at
  a later or specific time, as soon as possible or at certain intervals.

  Singleton model:

  Every executable that imports and statically links against the
  EventSystem library is provided with a global instance of the
  EventProcessor called eventProcessor. Therefore, it is not necessary to
  create instances of the EventProcessor class because it was designed
  as a singleton. It is important to note that none of its functions
  are reentrant.

  Thread Groups (Event types):

  When the EventProcessor is started, the first group of threads is spawned and it is assigned the
  special id ET_CALL. Depending on the complexity of the state machine or protocol, you may be
  interested in creating additional threads and the EventProcessor gives you the ability to create a
  single thread or an entire group of threads. In the former case, you call spawn_thread and the
  thread is independent of the thread groups and it exists as long as your continuation handle
  executes and there are events to process. In the latter, you call @c registerEventType to get an
  event type and then @c spawn_event_theads which creates the threads in the group of that
  type. Such threads require events to be scheduled on a specific thread in the group or for the
  group in general using the event type. Note that between these two calls @c
  EThread::schedule_spawn can be used to set up per thread initialization.

  Callback event codes:

  @b UNIX: For all of the scheduling functions, the callback_event
  parameter is not used. On a callback, the event code passed in to
  the continuation handler is always EVENT_IMMEDIATE.

  @b NT: The value of the event code passed in to the continuation
  handler is the value provided in the callback_event parameter.

  Event allocation policy:

  Events are allocated and deallocated by the EventProcessor. A state
  machine may access the returned, non-recurring event until it is
  cancelled or the callback from the event is complete. For recurring
  events, the Event may be accessed until it is cancelled. Once the event
  is complete or cancelled, it's the eventProcessor's responsibility to
  deallocate it.

*/
class EventProcessor : public Processor
{
public:
  /** Register an event type with @a name.

      This must be called to get an event type to pass to @c spawn_event_threads
      @see spawn_event_threads
   */
  EventType register_event_type(char const *name);

  /**
    Spawn an additional thread for calling back the continuation. Spawns
    a dedicated thread (EThread) that calls back the continuation passed
    in as soon as possible.

    @param cont continuation that the spawn thread will call back
      immediately.
    @return event object representing the start of the thread.

  */
  Event *spawn_thread(Continuation *cont, const char *thr_name, size_t stacksize = 0);

  /** Spawn a group of @a n_threads event dispatching threads.

      The threads run an event loop which dispatches events scheduled for a specific thread or the event type.

      @return EventType or thread id for the new group of threads (@a ev_type)

  */
  EventType spawn_event_threads(EventType ev_type, int n_threads, size_t stacksize = DEFAULT_STACKSIZE);

  /// Convenience overload.
  /// This registers @a name as an event type using @c registerEventType and then calls the real @c spawn_event_threads
  EventType spawn_event_threads(const char *name, int n_thread, size_t stacksize = DEFAULT_STACKSIZE);

  /**
    Schedules the continuation on a specific EThread to receive an event
    at the given timeout.  Requests the EventProcessor to schedule
    the callback to the continuation 'c' at the time specified in
    'atimeout_at'. The event is assigned to the specified EThread.

    @param c Continuation to be called back at the time specified in
      'atimeout_at'.
    @param atimeout_at time value at which to callback.
    @param ethread EThread on which to schedule the event.
    @param callback_event code to be passed back to the continuation's
      handler. See the Remarks section.
    @param cookie user-defined value or pointer to be passed back in
      the Event's object cookie field.
    @return reference to an Event object representing the scheduling
      of this callback.

  */
  Event *schedule_imm(Continuation *c, EventType event_type = ET_CALL, int callback_event = EVENT_IMMEDIATE,
                      void *cookie = nullptr);

  /**
    Schedules the continuation on a specific thread group to receive an
    event at the given timeout. Requests the EventProcessor to schedule
    the callback to the continuation 'c' at the time specified in
    'atimeout_at'. The callback is handled by a thread in the specified
    thread group (event_type).

    @param c Continuation to be called back at the time specified in
      'atimeout_at'.
    @param atimeout_at Time value at which to callback.
    @param event_type thread group id (or event type) specifying the
      group of threads on which to schedule the callback.
    @param callback_event code to be passed back to the continuation's
      handler. See the Remarks section.
    @param cookie user-defined value or pointer to be passed back in
      the Event's object cookie field.
    @return reference to an Event object representing the scheduling of
      this callback.

  */
  Event *schedule_at(Continuation *c, ink_hrtime atimeout_at, EventType event_type = ET_CALL, int callback_event = EVENT_INTERVAL,
                     void *cookie = nullptr);

  /**
    Schedules the continuation on a specific thread group to receive an
    event after the specified timeout elapses. Requests the EventProcessor
    to schedule the callback to the continuation 'c' after the time
    specified in 'atimeout_in' elapses. The callback is handled by a
    thread in the specified thread group (event_type).

    @param c Continuation to call back aftert the timeout elapses.
    @param atimeout_in amount of time after which to callback.
    @param event_type Thread group id (or event type) specifying the
      group of threads on which to schedule the callback.
    @param callback_event code to be passed back to the continuation's
      handler. See the Remarks section.
    @param cookie user-defined value or pointer to be passed back in
      the Event's object cookie field.
    @return reference to an Event object representing the scheduling of
      this callback.

  */
  Event *schedule_in(Continuation *c, ink_hrtime atimeout_in, EventType event_type = ET_CALL, int callback_event = EVENT_INTERVAL,
                     void *cookie = nullptr);

  /**
    Schedules the continuation on a specific thread group to receive
    an event periodically. Requests the EventProcessor to schedule the
    callback to the continuation 'c' every time 'aperiod' elapses. The
    callback is handled by a thread in the specified thread group
    (event_type).

    @param c Continuation to call back every time 'aperiod' elapses.
    @param aperiod duration of the time period between callbacks.
    @param event_type thread group id (or event type) specifying the
      group of threads on which to schedule the callback.
    @param callback_event code to be passed back to the continuation's
      handler. See the Remarks section.
    @param cookie user-defined value or pointer to be passed back in
      the Event's object cookie field.
    @return reference to an Event object representing the scheduling of
      this callback.

  */
  Event *schedule_every(Continuation *c, ink_hrtime aperiod, EventType event_type = ET_CALL, int callback_event = EVENT_INTERVAL,
                        void *cookie = nullptr);

  ////////////////////////////////////////////
  // reschedule an already scheduled event. //
  // may be called directly or called by    //
  // schedule_xxx Event member functions.   //
  // The returned value may be different    //
  // from the argument e.                   //
  ////////////////////////////////////////////

  Event *reschedule_imm(Event *e, int callback_event = EVENT_IMMEDIATE);
  Event *reschedule_at(Event *e, ink_hrtime atimeout_at, int callback_event = EVENT_INTERVAL);
  Event *reschedule_in(Event *e, ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL);
  Event *reschedule_every(Event *e, ink_hrtime aperiod, int callback_event = EVENT_INTERVAL);

  /// Schedule an @a event on continuation @a c when a thread of type @a ev_type is spawned.
  /// The @a cookie is attached to the event instance passed to the continuation.
  /// @return The scheduled event.
  Event *schedule_spawn(Continuation *c, EventType ev_type, int event = EVENT_IMMEDIATE, void *cookie = nullptr);

  /// Schedule the function @a f to be called in a thread of type @a ev_type when it is spawned.
  Event *schedule_spawn(void (*f)(EThread *), EventType ev_type);

  /// Schedule an @a event on continuation @a c to be called when a thread is spawned by this processor.
  /// The @a cookie is attached to the event instance passed to the continuation.
  /// @return The scheduled event.
  //  Event *schedule_spawn(Continuation *c, int event, void *cookie = NULL);

  EventProcessor();
  ~EventProcessor() override;
  EventProcessor(const EventProcessor &) = delete;
  EventProcessor &operator=(const EventProcessor &) = delete;

  /**
    Initializes the EventProcessor and its associated threads. Spawns the
    specified number of threads, initializes their state information and
    sets them running. It creates the initial thread group, represented
    by the event type ET_CALL.

    @return 0 if successful, and a negative value otherwise.

  */
  int start(int n_net_threads, size_t stacksize = DEFAULT_STACKSIZE) override;

  /**
    Stop the EventProcessor. Attempts to stop the EventProcessor and
    all of the threads in each of the thread groups.

  */
  void shutdown() override;

  /**
    Allocates size bytes on the event threads. This function is thread
    safe.

    @param size bytes to be allocated.

  */
  off_t allocate(int size);

  /**
    An array of pointers to all of the EThreads handled by the
    EventProcessor. An array of pointers to all of the EThreads created
    throughout the existence of the EventProcessor instance.

  */
  EThread *all_ethreads[MAX_EVENT_THREADS];

  /**
    An array of pointers, organized by thread group, to all of the
    EThreads handled by the EventProcessor. An array of pointers to all of
    the EThreads created throughout the existence of the EventProcessor
    instance. It is a two-dimensional array whose first dimension is the
    thread group id and the second the EThread pointers for that group.

  */
  //  EThread *eventthread[MAX_EVENT_TYPES][MAX_THREADS_IN_EACH_TYPE];

  /// Data kept for each thread group.
  /// The thread group ID is the index into an array of these and so is not stored explicitly.
  struct ThreadGroupDescriptor {
    std::string _name;                               ///< Name for the thread group.
    int _count                 = 0;                  ///< # of threads of this type.
    std::atomic<int> _started  = 0;                  ///< # of started threads of this type.
    uint64_t _next_round_robin = 0;                  ///< Index of thread to use for events assigned to this group.
    Que(Event, link) _spawnQueue;                    ///< Events to dispatch when thread is spawned.
    EThread *_thread[MAX_THREADS_IN_EACH_TYPE] = {}; ///< The actual threads in this group.
    std::function<void()> _afterStartCallback  = nullptr;
  };

  /// Storage for per group data.
  ThreadGroupDescriptor thread_group[MAX_EVENT_TYPES];

  /// Number of defined thread groups.
  int n_thread_groups = 0;

  /**
    Total number of threads controlled by this EventProcessor.  This is
    the count of all the EThreads spawn by this EventProcessor, excluding
    those created by spawn_thread

  */
  int n_ethreads = 0;

  bool has_tg_started(int etype);

  /*------------------------------------------------------*\
  | Unix & non NT Interface                                |
  \*------------------------------------------------------*/

  Event *schedule(Event *e, EventType etype);
  EThread *assign_thread(EventType etype);
  EThread *assign_affinity_by_type(Continuation *cont, EventType etype);

  EThread *all_dthreads[MAX_EVENT_THREADS];
  int n_dthreads       = 0; // No. of dedicated threads
  int thread_data_used = 0;

  /// Provide container style access to just the active threads, not the entire array.
  class active_threads_type
  {
    using iterator = EThread *const *; ///< Internal iterator type, pointer to array element.
  public:
    iterator
    begin() const
    {
      return _begin;
    }

    iterator
    end() const
    {
      return _end;
    }

  private:
    iterator _begin; ///< Start of threads.
    iterator _end;   ///< End of threads.
    /// Construct from base of the array (@a start) and the current valid count (@a n).
    active_threads_type(iterator start, int n) : _begin(start), _end(start + n) {}
    friend class EventProcessor;
  };

  // These can be used in container for loops and other range operations.
  active_threads_type
  active_ethreads() const
  {
    return {all_ethreads, n_ethreads};
  }

  active_threads_type
  active_dthreads() const
  {
    return {all_dthreads, n_dthreads};
  }

  active_threads_type
  active_group_threads(int type) const
  {
    ThreadGroupDescriptor const &group{thread_group[type]};
    return {group._thread, group._count};
  }

private:
  void initThreadState(EThread *);

  /// Used to generate a callback at the start of thread execution.
  class ThreadInit : public Continuation
  {
    typedef ThreadInit self;
    EventProcessor *_evp;

  public:
    explicit ThreadInit(EventProcessor *evp) : _evp(evp) { SET_HANDLER(&self::init); }

    int
    init(int /* event ATS_UNUSED */, Event *ev)
    {
      _evp->initThreadState(ev->ethread);
      return 0;
    }
  };
  friend class ThreadInit;
  ThreadInit thread_initializer;

  // Lock write access to the dedicated thread vector.
  // @internal Not a @c ProxyMutex - that's a whole can of problems due to initialization ordering.
  ink_mutex dedicated_thread_spawn_mutex;
};

extern inkcoreapi class EventProcessor eventProcessor;

void thread_started(EThread *);
