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

#ifndef _I_EventProcessor_h_
#define _I_EventProcessor_h_

#include "ts/ink_platform.h"
#include "I_Continuation.h"
#include "I_Processor.h"
#include "I_Event.h"

#ifdef TS_MAX_THREADS_IN_EACH_THREAD_TYPE
const int MAX_THREADS_IN_EACH_TYPE = TS_MAX_THREADS_IN_EACH_THREAD_TYPE;
#else
const int MAX_THREADS_IN_EACH_TYPE = 3072;
#endif

#ifdef TS_MAX_NUMBER_EVENT_THREADS
const int MAX_EVENT_THREADS = TS_MAX_NUMBER_EVENT_THREADS;
#else
const int MAX_EVENT_THREADS = 4096;
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

  When the EventProcessor is started, the first group of threads is
  spawned and it is assigned the special id ET_CALL. Depending on the
  complexity of the state machine or protocol, you may be interested
  in creating additional threads and the EventProcessor gives you the
  ability to create a single thread or an entire group of threads. In
  the former case, you call spawn_thread and the thread is independent
  of the thread groups and it exists as long as your continuation handle
  executes and there are events to process. In the latter, you call
  spawn_event_theads which creates a new thread group and you get an id
  or event type with wich you must keep for use later on when scheduling
  continuations on that group.

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
  /**
    Spawn an additional thread for calling back the continuation. Spawns
    a dedicated thread (EThread) that calls back the continuation passed
    in as soon as possible.

    @param cont continuation that the spawn thread will call back
      immediately.
    @return event object representing the start of the thread.

  */
  Event *spawn_thread(Continuation *cont, const char *thr_name, size_t stacksize = 0);

  /**
    Spawns a group of threads for an event type. Spawns the number of
    event threads passed in (n_threads) creating a thread group and
    returns the thread group id (or EventType). See the remarks section
    for Thread Groups.

    @return EventType or thread id for the new group of threads.

  */
  EventType spawn_event_threads(int n_threads, const char *et_name, size_t stacksize);

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
  /*
    provides the same functionality as schedule_imm and also signals the thread immediately
  */
  Event *schedule_imm_signal(Continuation *c, EventType event_type = ET_CALL, int callback_event = EVENT_IMMEDIATE,
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
    callback to the continuation 'c' everytime 'aperiod' elapses. The
    callback is handled by a thread in the specified thread group
    (event_type).

    @param c Continuation to call back everytime 'aperiod' elapses.
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

  EventProcessor();

  /**
    Initializes the EventProcessor and its associated threads. Spawns the
    specified number of threads, initializes their state information and
    sets them running. It creates the initial thread group, represented
    by the event type ET_CALL.

    @return 0 if successful, and a negative value otherwise.

  */
  int start(int n_net_threads, size_t stacksize = DEFAULT_STACKSIZE);

  /**
    Stop the EventProcessor. Attempts to stop the EventProcessor and
    all of the threads in each of the thread groups.

  */
  virtual void shutdown();

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
  EThread *eventthread[MAX_EVENT_TYPES][MAX_THREADS_IN_EACH_TYPE];

  unsigned int next_thread_for_type[MAX_EVENT_TYPES];
  int n_threads_for_type[MAX_EVENT_TYPES];

  /**
    Total number of threads controlled by this EventProcessor.  This is
    the count of all the EThreads spawn by this EventProcessor, excluding
    those created by spawn_thread

  */
  int n_ethreads;

  /**
    Total number of thread groups created so far. This is the count of
    all the thread groups (event types) created for this EventProcessor.

  */
  int n_thread_groups;

private:
  // prevent unauthorized copies (Not implemented)
  EventProcessor(const EventProcessor &);
  EventProcessor &operator=(const EventProcessor &);

public:
  /*------------------------------------------------------*\
  | Unix & non NT Interface                                |
  \*------------------------------------------------------*/

  Event *schedule(Event *e, EventType etype, bool fast_signal = false);
  EThread *assign_thread(EventType etype);

  EThread *all_dthreads[MAX_EVENT_THREADS];
  volatile int n_dthreads; // No. of dedicated threads
  volatile int thread_data_used;
  ink_mutex dedicated_spawn_thread_mutex;
};

extern inkcoreapi class EventProcessor eventProcessor;

#endif /*_EventProcessor_h_*/
