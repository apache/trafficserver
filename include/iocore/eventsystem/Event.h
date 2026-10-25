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

#include "iocore/eventsystem/Action.h"

#define MAX_EVENTS_PER_THREAD 100000

// Events

/** Event code indicating that no specific event occurred.

  A handler receives this code when @c handleEvent is called without an
  event code. The Event System dispatches an Event with this code only if
  the code that scheduled the Event passed it as the callback event.
*/
#define EVENT_NONE CONTINUATION_EVENT_NONE // 0

/**
  The default event code that the Event System passes to a
  Continuation's handler when it dispatches an Event that was scheduled
  to run immediately.

  Receiving this code does not prove the Event was scheduled to run
  immediately. The scheduling code can choose another code for an
  immediate Event, or choose this code for a timed one.

  @note Some callers invoke a handler directly with this code. The data
        they pass is often @c nullptr rather than an Event, so a handler
        must not assume the data is an Event.
*/
#define EVENT_IMMEDIATE 1

/** Event code for a delayed, timed, or recurring dispatch.

  The scheduling functions that take a delay, an absolute time, or a
  period pass this code to the Continuation's handler unless the
  caller supplies a different event code.

  @note Some callers outside the Event System pass this code to a
        handler directly, with a null data pointer. A handler must not
        assume that this code means the data pointer is an Event.
*/
#define EVENT_INTERVAL 2

/** Event code reporting that an operation failed.

  The Event System never generates this code on its own. The component
  that delivers it to a handler decides what the data pointer means, so
  check that component's documentation before you use the pointer.

  @note The data pointer might not point to an object. For example, a
        failed accept passes a negated @c errno value cast to a pointer.
*/
#define EVENT_ERROR 3

/** Event code telling a handler that its caller invoked it directly.

  The Event System never generates this code. A caller passes it when it
  calls a handler as an ordinary function instead of scheduling an Event.
  The handler may then return the code meaning the work finished
  synchronously, and the caller continues inline. The data pointer is
  unspecified.

  @note Do not schedule an Event with this code. The Event System
        discards handler return values, so a handler that reports
        synchronous completion would have its result lost.
*/
#define EVENT_CALL 4

/** Event code for an Event that polls.

  An Event polls when it is scheduled with a negative period. The Event
  System usually passes this code, not the Event's callback event, when
  it dispatches such an Event. Sometimes it passes the callback event
  instead, so the handler must accept both codes. In either case the
  data pointer is the Event.

  @note This code has the same value as a handler return code. Do not
        return it from a handler.
*/
#define EVENT_POLL 5

// Event callback return functions

/** Handler return code meaning the handler has finished with the event.

  The Event System does not act on a handler's return value, so returning
  this code neither cancels a periodic Event nor releases any resource. It
  matters only to code that calls a handler directly and checks the
  result. Some such callers treat this code as meaning the handler's
  object may have been destroyed, so a handler that destroys its own
  object should return this code.

  @note This code has the same value as the "no event" event code. Do not
        pass it as an event code.
*/
#define EVENT_DONE CONTINUATION_DONE // 0

/** Handler return code meaning the Continuation has not finished its work.

  The Event System ignores handler return values. This code matters only
  to code that calls a handler directly and checks the result, and that
  code decides what it means. By convention, it means the Continuation is
  still alive and expects more events, so the caller may keep using it.

  @note Do not test an incoming event code against this value. It equals
        an event code, so a handler cannot tell the two apart.
*/
#define EVENT_CONT CONTINUATION_CONT // 1

/** Handler return code meaning the work finished synchronously.

  A handler that is called directly, rather than dispatched by the Event
  System, returns this code to tell its caller that work it would
  normally complete later, through a scheduled event, is already done.
  No later dispatch delivers that result, so the caller must continue
  inline. The Event System ignores handler return values, so this code
  has meaning only to a direct caller that checks for it.

  @note Its value is also an event code that the Event System passes
        to handlers, so do not pass this code as an event code.
*/
#define EVENT_RETURN 5

/** Handler return code asking the caller to restart the operation from the beginning.

  The Event System ignores handler return values. Returning this code
  has an effect only if the code that called the handler directly
  checks for it.
*/
#define EVENT_RESTART 6

/** Handler return code asking the caller to restart the operation after a delay.

  The Event System ignores handler return values. Returning this code
  has an effect only if the code that called the handler directly
  checks for it.
*/
#define EVENT_RESTART_DELAYED 7

// Event numbers block allocation
// ** ALL NEW EVENT TYPES SHOULD BE ALLOCATED FROM BLOCKS LISTED HERE! **

#define VC_EVENT_EVENTS_START                   100
#define NET_EVENT_EVENTS_START                  200
#define DISK_EVENT_EVENTS_START                 300
#define HOSTDB_EVENT_EVENTS_START               500
#define DNS_EVENT_EVENTS_START                  600
#define CONFIG_EVENT_EVENTS_START               800
#define LOG_EVENT_EVENTS_START                  900
#define REFCOUNT_CACHE_EVENT_EVENTS_START       1000
#define CACHE_EVENT_EVENTS_START                1100
#define CACHE_DIRECTORY_EVENT_EVENTS_START      1200
#define CACHE_DB_EVENT_EVENTS_START             1300
#define HTTP_NET_CONNECTION_EVENT_EVENTS_START  1400
#define HTTP_NET_VCONNECTION_EVENT_EVENTS_START 1500
#define GC_EVENT_EVENTS_START                   1600
#define TRANSFORM_EVENTS_START                  2000
#define STAT_PAGES_EVENTS_START                 2100
#define HTTP_SESSION_EVENTS_START               2200
#define HTTP2_SESSION_EVENTS_START              2250
#define HTTP_TUNNEL_EVENTS_START                2300
#define HTTP_SCH_UPDATE_EVENTS_START            2400
#define QUIC_EVENT_EVENTS_START                 2500
#define HTTP3_SESSION_EVENTS_START              2600
#define QPACK_EVENT_EVENTS_START                2700
#define NT_ASYNC_CONNECT_EVENT_EVENTS_START     3000
#define NT_ASYNC_IO_EVENT_EVENTS_START          3100
// #define RAFT_EVENT_EVENTS_START                 3200 Not currently used
#define SIMPLE_EVENT_EVENTS_START      3300
#define UPDATE_EVENT_EVENTS_START      3500
#define AIO_EVENT_EVENTS_START         3900
#define BLOCK_CACHE_EVENT_EVENTS_START 4000
#define UTILS_EVENT_EVENTS_START       5000
#define INK_API_EVENT_EVENTS_START     60000
#define SRV_EVENT_EVENTS_START         62000
#define REMAP_EVENT_EVENTS_START       63000

// define misc events here

/**
  Event code that a @c OneWayTunnel of a two-way pair passes to its
  peer's @c OneWayTunnel::startEvent when it finishes.

  The call is made directly on the finishing tunnel's thread, not
  scheduled. The receiving tunnel shuts down without closing the
  virtual connections. After the call returns, the finishing tunnel
  closes each connection only if it was set up to close that
  connection. The event data is unspecified.

  @note No continuation other than @c OneWayTunnel receives this code.
*/
#define ONE_WAY_TUNNEL_EVENT_PEER_CLOSE (SIMPLE_EVENT_EVENTS_START + 1)

/** Identifies a group of event threads.

  Scheduling functions take an @c EventType to select the group of
  threads that dispatches an Event. Only values produced by the Event
  System name a group; passing any other value is undefined.

  @see ET_TASK, ET_DNS, ET_UDP
*/
using EventType = int;

/**
  The @c EventType of the default event-thread group, whose threads
  also handle network I/O. The group is named "ET_NET".

  @c eventProcessor registers this group itself; callers never
  register it. It is the default group for @c eventProcessor's
  scheduling functions.

  @note The group has no threads until @c eventProcessor is started.
        Scheduling an Event on it before then is undefined.
*/
const int ET_CALL = 0;

/**
  The maximum number of event-thread groups that @c eventProcessor can
  hold, counting the default group. Every valid event type lies in the
  range [0, @c MAX_EVENT_TYPES).

  Registering more groups than this is undefined.
*/
const int MAX_EVENT_TYPES = 8;

class EThread;

/** A scheduled call to a Continuation's handler on an event thread.

  The Event System creates an Event when work is scheduled, then calls
  @c continuation->handleEvent on @c ethread, passing an event code and
  this Event. A one-shot Event is dispatched once; a recurring Event is
  dispatched repeatedly until it is cancelled or rescheduled to fire once.

  @par Lifetime
  The Event System owns every Event. Callers must not create or destroy
  one. An Event must not be accessed after it is cancelled, or after the
  handler of a one-shot Event returns without rescheduling it.

  @par Thread Safety
  Not thread-safe. Concurrent accessors must hold @c mutex. The Event
  System holds @c mutex while it runs the handler; if @c mutex is null,
  the handler runs without a lock.
*/
class Event : public Action
{
public:
  ///////////////////////////////////////////////////////////
  // Common Interface                                      //
  ///////////////////////////////////////////////////////////

  /** Reschedules this Event for immediate dispatch on its owning @c EThread.

    Replaces any pending dispatch of this Event, so a recurring Event
    becomes one-shot. Typically called from the Event's own handler to
    run it again.

    @param[in] callback_event Event code passed to the handler when the
                              Event fires.

    @pre  The calling thread is @c ethread.
    @pre  The Event is not cancelled, and is either awaiting dispatch or
          being dispatched to the handler making this call.
    @pre  If @c period < 0, this call is made from the Event's own handler.

    @post @c mutex equals @c continuation->mutex.
    @post Unless cancelled or rescheduled, the Event fires once, as soon
          as its @c EThread can dispatch it.

    @note A handler that keeps rescheduling itself with
          @c schedule_imm may keep its @c EThread from processing
          other work.

    @par Thread Safety
    Not thread-safe. The caller must hold @c mutex.
  */
  void schedule_imm(int callback_event = EVENT_IMMEDIATE);

  /** Reschedules this Event to fire once at an absolute time.

    Replaces any pending dispatch of this Event, so a recurring Event
    becomes one-shot.

    @param[in] atimeout_at    Time at which the Event should fire, in
                              nanoseconds on the clock selected by
                              @c gSystemClock.
    @param[in] callback_event Event code passed to the handler when the
                              Event fires.

    @pre  The calling thread is @c ethread.
    @pre  @p atimeout_at > 0.
    @pre  The Event is not cancelled, and is either awaiting dispatch or
          being dispatched to the handler making this call.
    @pre  If @c period < 0, this call is made from the Event's own handler.

    @post @c mutex equals @c continuation->mutex.
    @post Unless cancelled or rescheduled, the Event fires once, at about
          @p atimeout_at. If @p atimeout_at has already passed, the Event
          is due immediately.

    @note Timing is approximate. The Event may fire a few milliseconds
          early, or later if its @c EThread is busy. The clock can be a
          wall clock, so a change to the system time can also move the
          dispatch earlier or later.

    @par Thread Safety
    Not thread-safe. The caller must hold @c mutex.
  */
  void schedule_at(ink_hrtime atimeout_at, int callback_event = EVENT_INTERVAL);

  /** Reschedules this Event to fire once, after a delay.

    Replaces any pending dispatch of this Event, so a recurring Event
    becomes one-shot. Calling this from the Event's own handler is the
    usual way to retry a one-shot Event; the Event then stays valid
    until the new dispatch completes.

    @param[in] atimeout_in    Delay from now, in nanoseconds.
    @param[in] callback_event Event code passed to the handler when the
                              Event fires.

    @pre  The calling thread is @c ethread.
    @pre  @p atimeout_in >= 0.
    @pre  The Event is not cancelled, and is either awaiting dispatch or
          being dispatched to the handler making this call.
    @pre  If @c period < 0, this call is made from the Event's own handler.

    @post @c mutex equals @c continuation->mutex.
    @post Unless cancelled or rescheduled, the Event fires once, about
          @p atimeout_in after this call.

    @note Timing is approximate. The Event may fire a few milliseconds
          early, or later if its @c EThread is busy. The clock can be a
          wall clock, so a change to the system time can also move the
          dispatch earlier or later.

    @par Thread Safety
    Not thread-safe. The caller must hold @c mutex.
  */
  void schedule_in(ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL);

  /** Reschedules this Event to fire repeatedly.

    Replaces any pending dispatch of this Event, so a one-shot Event
    becomes recurring.

    @param[in] aperiod        Positive: the interval between dispatches, in
                              nanoseconds. Negative: the Event polls, firing
                              about once per iteration of @c ethread's event
                              loop; among polling Events dispatched in the
                              same iteration, those with smaller magnitudes
                              fire first.
    @param[in] callback_event Event code passed to the handler when the
                              Event fires.

    @pre  The calling thread is @c ethread.
    @pre  @p aperiod != 0.
    @pre  The Event is not cancelled, and is either awaiting dispatch or
          being dispatched to the handler making this call.
    @pre  If @c period < 0, this call is made from the Event's own handler.

    @post @c mutex equals @c continuation->mutex.
    @post Unless cancelled or rescheduled, the Event fires repeatedly.
          If @p aperiod is positive, the first dispatch comes about
          @p aperiod after this call, and later dispatches follow at
          intervals of about @p aperiod.

    @note Timing is approximate. The Event may fire a few milliseconds
          early, or later if its @c EThread is busy. The clock can be a
          wall clock, so a change to the system time can also move the
          dispatch earlier or later.
    @note When @p aperiod is negative, each dispatch passes either
          @c EVENT_POLL or @p callback_event, so the handler must accept
          both.

    @par Thread Safety
    Not thread-safe. The caller must hold @c mutex.
  */
  void schedule_every(ink_hrtime aperiod, int callback_event = EVENT_INTERVAL);

  // inherited from Action::cancel
  // virtual void cancel(Continuation * c = nullptr);

#ifdef ENABLE_EVENT_TRACKER
  void        set_location();
  const void *get_location() const;
#endif

  void free();

  /** The @c EThread that dispatches this Event.

    The Event System sets this when the Event is scheduled. In the
    Event's handler it is the thread running the handler, so handlers
    use it to reschedule work on the same thread or to reach that
    thread's per-thread state.

    @note Read-only for callers.

    @par Thread Safety
    Read it only on the @c EThread it names.
  */
  EThread *ethread = nullptr;

  unsigned int in_the_prot_queue     : 1;
  unsigned int in_the_priority_queue : 1;
  unsigned int immediate             : 1;
  unsigned int globally_allocated    : 1;
  unsigned int in_heap               : 4;
  int          callback_event = 0;

  ink_hrtime timeout_at = 0;
  ink_hrtime period     = 0;

  /** Opaque value passed from the code that schedules this Event to its handler.

    Holds the @c cookie argument of the scheduling call that created
    this Event, or @c nullptr if none was given. Rescheduling this
    Event keeps the value. The Event System neither interprets nor
    owns it: the scheduling code and the handler agree on its meaning
    and on the lifetime of anything it points to.

    @par Thread Safety
    Not thread-safe. Once the Event is scheduled, read or write it
    only while holding @c mutex.
  */
  void *cookie = nullptr;

  // Private

  Event() : in_the_prot_queue(false), in_the_priority_queue(false), immediate(false), globally_allocated(true), in_heap(false) {}

  Event *
  init(Continuation *c, ink_hrtime atimeout_at = 0, ink_hrtime aperiod = 0)
  {
    continuation = c;
    timeout_at   = atimeout_at;
    period       = aperiod;
    immediate    = !period && !atimeout_at;
    cancelled    = false;
    return this;
  }

#ifdef ENABLE_TIME_TRACE
  ink_hrtime start_time;
#endif

  // noncopyable: prevent unauthorized copies (Not implemented)
  Event(const Event &)            = delete;
  Event &operator=(const Event &) = delete;

private:
  void *operator new(size_t size); // use the fast allocators

#ifdef ENABLE_EVENT_TRACKER
  /**
    Address of who scheduled this event
    To get symbols, use backtrace_symbols(3) or external tools like `addr2line(1)` (Linux) or `atos(1)`(BSD).
   */
  const void *_location = nullptr;
#endif

public:
  LINK(Event, link);

  /*-------------------------------------------------------*\
  | UNIX/non-NT Interface                                   |
  \*-------------------------------------------------------*/

#if defined(__GNUC__)
  ~Event() override {}
#endif
};

//
// Event Allocator
//
extern ClassAllocator<Event, false> eventAllocator;

inline void
Event::free()
{
  mutex = nullptr;
  eventAllocator.free(this);
}

#define EVENT_ALLOC(_a, _t) THREAD_ALLOC(_a, _t)
#define EVENT_FREE(_p, _a, _t) \
  _p->mutex = nullptr;         \
  if (_p->globally_allocated)  \
    ::_a.free(_p);             \
  else                         \
    THREAD_FREE(_p, _a, _t)
