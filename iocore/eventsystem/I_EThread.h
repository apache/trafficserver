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

#ifndef _EThread_h_
#define _EThread_h_

#include "ts/ink_platform.h"
#include "ts/ink_rand.h"
#include "ts/I_Version.h"
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

extern volatile bool shutdown_event_system;

/**
  Event System specific type of thread.

  The EThread class is the type of thread created and managed by
  the Event System. It is one of the available interfaces for
  schedulling events in the event system (another two are the Event
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

  There are eight schedulling functions provided by EThread and
  they are a wrapper around their counterparts in EventProcessor.

  @see EventProcessor
  @see Event

*/
class EThread : public Thread
{
public:
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
    @return Reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_imm(Continuation *c, int callback_event = EVENT_IMMEDIATE, void *cookie = nullptr);
  Event *schedule_imm_signal(Continuation *c, int callback_event = EVENT_IMMEDIATE, void *cookie = nullptr);

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
    @return A reference to an Event object representing the schedulling
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
    @return A reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_in(Continuation *c, ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    periodically.

    Schedules the callback to the continuation 'c' in the EventProcessor
    to occur every time 'aperiod' elapses. It is scheduled on this
    EThread.

    @param c Continuation to call back everytime 'aperiod' elapses.
    @param aperiod Duration of the time period between callbacks.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the Remarks section in the
      EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the schedulling
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
    @return A reference to an Event object representing the schedulling
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
    @return A reference to an Event object representing the schedulling
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
    @return A reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_in_local(Continuation *c, ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    periodically.

    Schedules the callback to the continuation 'c' to occur every
    time 'aperiod' elapses. It is scheduled on this EThread.

    @param c Continuation to call back everytime 'aperiod' elapses.
    @param aperiod Duration of the time period between callbacks.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the Remarks section in the
      EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the schedulling
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
  virtual ~EThread();

  Event *schedule(Event *e, bool fast_signal = false);

  /** Block of memory to allocate thread specific data e.g. stat system arrays. */
  char thread_private[PER_THREAD_DATA];

  /** Private Data for the Disk Processor. */
  DiskHandler *diskHandler = nullptr;

  /** Private Data for AIO. */
  Que(Continuation, link) aio_ops;

  ProtectedQueue EventQueueExternal;
  PriorityEventQueue EventQueue;

  EThread **ethreads_to_be_signalled = nullptr;
  int n_ethreads_to_be_signalled     = 0;

  static constexpr int NO_ETHREAD_ID = -1;
  int id                             = NO_ETHREAD_ID;
  unsigned int event_types           = 0;
  bool is_event_type(EventType et);
  void set_event_type(EventType et);

  // Private Interface

  void execute() override;
  void process_event(Event *e, int calling_code);
  void free_event(Event *e);
  void (*signal_hook)(EThread *) = nullptr;

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
#endif /*_EThread_h_*/
