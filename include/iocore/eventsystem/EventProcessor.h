/** @file

  EventProcessor

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

  @c EventProcessor is the singleton @c Processor that owns the Event
  System's @c REGULAR EThread pool and a parallel set of @c DEDICATED
  EThreads. EThreads are partitioned into named @c EventType groups
  (the default group is @c ET_CALL); callers schedule work by group,
  which the processor dispatches round-robin across that group's
  threads. The library exposes a single global instance,
  @c eventProcessor.

 */

#pragma once

#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/Processor.h"
#include "iocore/eventsystem/Event.h"
#include <atomic>

#ifdef TS_MAX_THREADS_IN_EACH_THREAD_TYPE
constexpr int MAX_THREADS_IN_EACH_TYPE = TS_MAX_THREADS_IN_EACH_THREAD_TYPE;
#else
constexpr int MAX_THREADS_IN_EACH_TYPE = 3071;
#endif

/**
  Compile-time upper bound on the number of @c EThreads in a single
  @c EventProcessor pool. Applied independently to the @c REGULAR
  pool (across all @c EventType groups combined) and to the
  @c DEDICATED pool.

  Set from @c TS_MAX_NUMBER_EVENT_THREADS at configure time, or to
  4096 if that macro is not defined. Spawning threads that would
  push either pool past this limit aborts the process.

  @par Thread Safety
  Compile-time constant; safe to use from any thread.
*/
#ifdef TS_MAX_NUMBER_EVENT_THREADS
constexpr int MAX_EVENT_THREADS = TS_MAX_NUMBER_EVENT_THREADS;
#else
constexpr int MAX_EVENT_THREADS = 4096;
#endif

class EThread;

/**
  Singleton @c Processor that owns the Event System's thread pools and
  dispatches work to them.

  @c EventProcessor::start spawns an initial @c REGULAR EThread group
  with @c EventType @c ET_CALL; additional groups are added via
  @c register_event_type / @c spawn_event_threads. @c DEDICATED
  EThreads are spawned individually by @c spawn_thread. Callers
  schedule a Continuation onto a group via @c schedule_imm /
  @c schedule_at / @c schedule_in / @c schedule_every (each takes an
  @c EventType, defaulting to @c ET_CALL); the processor selects a
  thread within that group on each call.

  Allocation: Events handed to a Continuation by the @c schedule_*
  family are owned by the framework. A non-recurring Event remains
  valid until its single dispatch completes or @c Event::cancel is
  called; a recurring Event remains valid until @c Event::cancel is
  called. The framework deallocates the Event after that.

  @par Ownership
  Singleton; the global @c eventProcessor instance lives for the
  entire process lifetime. Direct instantiation is supported but
  not the intended usage.

  @par Thread Safety
  None of the @c EventProcessor service methods are reentrant on the
  same internal state. The @c schedule_* family is safe to call from
  any thread; the lifecycle methods (@c start, @c shutdown) are
  designed to be called once from the main thread.
*/
class EventProcessor : public Processor
{
public:
  /**
    Reserves a fresh @c EventType slot and labels it @p name.

    Subsystems that want a private @c EThread group call this to
    obtain an @c EventType, then pass that value to
    @c spawn_event_threads to create the actual threads. The
    reservation is immediate; the threads are not yet spawned.

    @param name Null-terminated name for the new group; copied.
                Stored in the per-group descriptor for
                administrative reporting.

    @pre  @c n_thread_groups @c < @c MAX_EVENT_TYPES. Calling when
          this is not satisfied aborts the process via
          @c ink_release_assert.
    @post @c n_thread_groups is incremented; the new
          @c thread_group[returned] entry has the supplied name and
          a zero @c _count until @c spawn_event_threads runs.

    @return The new @c EventType (zero-based group index).

    @par Errors
    Aborts the process if the precondition is violated.

    @par Thread Safety
    Caller-restricted by convention: invoked from the main thread
    during process startup. Concurrent calls are not safe.
  */
  EventType register_event_type(char const *name);

  /**
    Spawns a single @c DEDICATED @c EThread that dispatches @p cont
    as its sole task.

    The new thread is created with @c ThreadType @c DEDICATED, given
    a single @c start_event whose Continuation is @p cont, and added
    to @c all_dthreads. The thread invokes @p cont's handler once
    with @c EVENT_IMMEDIATE and exits when the handler returns; it
    does not participate in the event-loop dispatch the @c REGULAR
    pool runs. As a side effect @p cont 's mutex is overwritten with
    the new EThread's mutex.

    @param cont      Continuation to dispatch on the new thread. Must
                     be non-null and remain valid until its handler
                     returns. The framework allocates and owns the
                     @c Event passed to the handler.
    @param thr_name  Null-terminated thread name (truncated to
                     @c MAX_THREAD_NAME_LENGTH-1 bytes).
    @param stacksize Stack size in bytes; zero selects the platform
                     default (@c DEFAULT_STACKSIZE).

    @pre  @p cont is non-null. @c n_dthreads @c <
          @c MAX_EVENT_THREADS.
    @post A new @c DEDICATED EThread is running. @c all_dthreads
          contains the new thread; @c n_dthreads is incremented.
          @p cont->mutex points at the new EThread's mutex.

    @return Pointer to the @c Event that will dispatch @p cont on the
            new thread.

    @par Errors
    Aborts the process via @c ink_release_assert if @c n_dthreads is
    already at @c MAX_EVENT_THREADS.

    @par Thread Safety
    Safe to call from any thread; the dedicated-thread vector is
    serialized by an internal mutex.
  */
  Event *spawn_thread(Continuation *cont, const char *thr_name, size_t stacksize = 0);

  /**
    Spawns @p n_threads @c REGULAR EThreads bound to the @p ev_type
    group.

    Each new thread runs the standard event loop and dispatches
    events scheduled with @p ev_type. Each Continuation registered
    on the group via @c schedule_spawn runs once on every newly
    spawned thread before that thread enters its event loop.

    @param ev_type   @c EventType obtained from
                     @c register_event_type.
    @param n_threads Number of threads to spawn. MUST be positive
                     and combined with the existing total MUST NOT
                     exceed @c MAX_EVENT_THREADS.
    @param stacksize Per-thread stack size in bytes. Values below
                     @c INK_THREAD_STACK_MIN are clamped up to it,
                     and the result is rounded up to a multiple of
                     the page size (or huge-page size when huge
                     pages are enabled).

    @pre  @p ev_type was returned by a prior
          @c register_event_type and @c spawn_event_threads has not
          yet been called for it. @p n_threads is positive and
          @p n_threads @c + @c n_ethreads @c <=
          @c MAX_EVENT_THREADS.
    @post @p n_threads new EThreads have been spawned and bound to
          @p ev_type. @c n_ethreads is incremented by @p n_threads;
          @c thread_group[ev_type]._count equals @p n_threads and
          @c thread_group[ev_type]._thread[0, n_threads) point at
          the new threads.

    @return @p ev_type unchanged, for call chaining.

    @par Errors
    Aborts the process via @c ink_release_assert if any precondition
    is violated.

    @par Thread Safety
    Caller-restricted by convention: invoked from the main thread
    during process startup. Concurrent calls are not safe.
  */
  EventType spawn_event_threads(EventType ev_type, int n_threads, size_t stacksize = DEFAULT_STACKSIZE);

  /**
    Convenience overload combining @c register_event_type and
    @c spawn_event_threads. Registers @p name as a new event type and
    immediately spawns @p n_thread threads for it.

    @param name      Null-terminated name for the new group; copied.
    @param n_thread  Number of threads to spawn. MUST be positive.
    @param stacksize Per-thread stack size in bytes. Values below
                     @c INK_THREAD_STACK_MIN are clamped up to it, and
                     the result is rounded up to a multiple of the
                     page size (or huge-page size when huge pages are
                     enabled).

    @pre  @c n_thread_groups @c < @c MAX_EVENT_TYPES. @p n_thread is
          positive and @p n_thread @c + @c n_ethreads @c <=
          @c MAX_EVENT_THREADS.
    @post A fresh @c EventType is reserved with @p name and
          @p n_thread @c REGULAR EThreads are spawned and bound to it.
          @c n_thread_groups is incremented; @c n_ethreads is
          incremented by @p n_thread.

    @return The newly registered @c EventType.

    @par Errors
    Aborts the process via @c ink_release_assert if any precondition
    is violated.

    @par Thread Safety
    Caller-restricted by convention: invoked from the main thread
    during process startup. Concurrent calls are not safe.
  */
  EventType spawn_event_threads(const char *name, int n_thread, size_t stacksize = DEFAULT_STACKSIZE);

  /**
    Schedules @p c on a thread of group @p event_type for immediate
    dispatch.

    Allocates an @c Event and enqueues it on a thread in group
    @p event_type. The thread is selected as follows: if @p c has a
    thread affinity that belongs to @p event_type 's group, that
    thread is used; otherwise if the calling thread is itself in
    @p event_type 's group it is used; otherwise a thread is chosen
    by the group's round-robin cursor. When @p c had no prior
    affinity the chosen thread is recorded as @p c 's affinity. The
    Event fires as soon as the dispatch loop reaches it.

    @param c              Continuation to dispatch. MUST be non-null
                          and live until the resulting Event is
                          delivered or cancelled.
    @param event_type     @c EventType (group id) on which to
                          dispatch. Defaults to @c ET_CALL.
    @param callback_event @c event_id passed to @c handleEvent on
                          dispatch. Defaults to @c EVENT_IMMEDIATE.
    @param cookie         Stored verbatim in @c Event::cookie.

    @pre  @p c is non-null. The threads for @p event_type are
          spawned.
    @post On success, an Event is enqueued on a thread in
          @p event_type 's group. If the Event System is in shutdown,
          no Event is enqueued.

    @return Pointer to the scheduled Event, or @c nullptr if the
            Event System is in shutdown. Use @c Event::cancel to
            detach. Framework-owned; do not delete.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    Caller-synchronized with respect to @p c: safe to call from any
    thread provided no other thread is concurrently scheduling @p c
    or otherwise reading or writing @c c->thread_affinity. The
    external-queue enqueue itself is thread-safe.
  */
  Event *schedule_imm(Continuation *c, EventType event_type = ET_CALL, int callback_event = EVENT_IMMEDIATE,
                      void *cookie = nullptr);

  /**
    Schedules @p c on a thread of group @p event_type to be
    dispatched at absolute time @p atimeout_at.

    The selected thread is chosen using the same rule as
    @c schedule_imm.

    @param c              Continuation to dispatch. MUST be non-null
                          and live until the resulting Event is
                          delivered or cancelled.
    @param atimeout_at    Absolute @c ink_hrtime at which to fire.
                          MUST be strictly positive; a time already
                          past is legal and fires at the dispatch
                          loop's next opportunity.
    @param event_type     @c EventType (group id). Defaults to
                          @c ET_CALL.
    @param callback_event @c event_id passed on dispatch. Defaults
                          to @c EVENT_INTERVAL.
    @param cookie         Stored verbatim in @c Event::cookie.

    @pre  @p c is non-null. @p atimeout_at @c > 0. The threads for
          @p event_type are spawned.
    @post On success, an Event is enqueued for delivery at
          @p atimeout_at on a thread in @p event_type 's group. If
          the Event System is in shutdown, no Event is enqueued.

    @return Pointer to the scheduled Event, or @c nullptr if the
            Event System is in shutdown. Use @c Event::cancel to
            detach. Framework-owned; do not delete.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    Caller-synchronized with respect to @p c: safe to call from any
    thread provided no other thread is concurrently scheduling @p c
    or otherwise reading or writing @c c->thread_affinity. The
    external-queue enqueue itself is thread-safe.
  */
  Event *schedule_at(Continuation *c, ink_hrtime atimeout_at, EventType event_type = ET_CALL, int callback_event = EVENT_INTERVAL,
                     void *cookie = nullptr);

  /**
    Schedules @p c on a thread of group @p event_type to be
    dispatched after @p atimeout_in elapses.

    Computes an absolute deadline of @c ink_get_hrtime() @c +
    @p atimeout_in and enqueues a one-shot Event for that time. The
    selected thread is chosen using the same rule as
    @c schedule_imm.

    @param c              Continuation to dispatch. MUST be non-null
                          and live until the resulting Event is
                          delivered or cancelled.
    @param atimeout_in    Relative delay in @c ink_hrtime units. Zero
                          or negative values are legal; they yield a
                          deadline at or before the current time and
                          fire at the dispatch loop's next
                          opportunity.
    @param event_type     @c EventType (group id). Defaults to
                          @c ET_CALL.
    @param callback_event @c event_id passed on dispatch. Defaults
                          to @c EVENT_INTERVAL.
    @param cookie         Stored verbatim in @c Event::cookie.

    @pre  @p c is non-null. The threads for @p event_type are
          spawned.
    @post On success, an Event is enqueued for delivery at the
          computed absolute time. If the Event System is in shutdown,
          no Event is enqueued.

    @return Pointer to the scheduled Event, or @c nullptr if the
            Event System is in shutdown. Use @c Event::cancel to
            detach. Framework-owned; do not delete.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    Caller-synchronized with respect to @p c: safe to call from any
    thread provided no other thread is concurrently scheduling @p c
    or otherwise reading or writing @c c->thread_affinity. The
    external-queue enqueue itself is thread-safe.
  */
  Event *schedule_in(Continuation *c, ink_hrtime atimeout_in, EventType event_type = ET_CALL, int callback_event = EVENT_INTERVAL,
                     void *cookie = nullptr);

  /**
    Schedules @p c on a thread of group @p event_type to be
    dispatched repeatedly every @p aperiod.

    For positive @p aperiod the first dispatch occurs after
    @p aperiod elapses and thereafter the Event fires every
    @p aperiod until @c Event::cancel is called. For negative
    @p aperiod the Event joins the negative-event (poll) rotation
    and the handler is dispatched once per event-loop iteration with
    @c EVENT_POLL regardless of @p callback_event. The selected
    thread is chosen using the same rule as @c schedule_imm.

    @param c              Continuation to dispatch. MUST be non-null
                          and live until the resulting Event is
                          cancelled.
    @param aperiod        Period between successive dispatches in
                          @c ink_hrtime units. MUST be non-zero.
                          Negative values switch to negative-event
                          semantics.
    @param event_type     @c EventType (group id). Defaults to
                          @c ET_CALL.
    @param callback_event @c event_id passed on each positive-period
                          dispatch. Ignored when @p aperiod is
                          negative. Defaults to @c EVENT_INTERVAL.
    @param cookie         Stored verbatim in @c Event::cookie.

    @pre  @p c is non-null. @p aperiod is non-zero. The threads for
          @p event_type are spawned.
    @post On success, a recurring Event is enqueued. If the Event
          System is in shutdown, no Event is enqueued.

    @return Pointer to the recurring Event, or @c nullptr if the
            Event System is in shutdown. The caller MUST eventually
            call @c Event::cancel.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    Caller-synchronized with respect to @p c: safe to call from any
    thread provided no other thread is concurrently scheduling @p c
    or otherwise reading or writing @c c->thread_affinity. The
    external-queue enqueue itself is thread-safe.
  */
  Event *schedule_every(Continuation *c, ink_hrtime aperiod, EventType event_type = ET_CALL, int callback_event = EVENT_INTERVAL,
                        void *cookie = nullptr);

  /**
    Schedules @p c on every thread in group @p event_type.

    For each thread in the group an independent Event is allocated
    and enqueued on that thread. Each Event is given a fresh
    @c ProxyMutex (rather than sharing @p c 's mutex), so the
    per-thread invocations may run concurrently. Used by subsystems
    that want a per-thread copy of the same Continuation (e.g.,
    per-thread initialization or shutdown).

    The timing parameters select one of the following modes; at
    least one of @p atimeout and @p aperiod MUST be zero:
    - @p atimeout @c == @c 0 and @p aperiod @c == @c 0: each Event
      is a one-shot dispatched on the next event-loop iteration.
    - @p atimeout @c > @c 0 and @p aperiod @c == @c 0: each Event
      is a one-shot dispatched at @c ink_get_hrtime() @c +
      @p atimeout.
    - @p atimeout @c == @c 0 and @p aperiod @c > @c 0: each Event is
      recurring with period @p aperiod; the first dispatch occurs at
      @c ink_get_hrtime() @c + @p aperiod.
    - @p atimeout @c == @c 0 and @p aperiod @c < @c 0: each Event
      joins the negative-event (poll) rotation and the handler is
      dispatched once per event-loop iteration.

    @param c              Continuation to dispatch. Must remain
                          valid for the lifetime of every produced
                          Event.
    @param atimeout       Relative delay before the (single) one-shot
                          fire when @p aperiod is zero. Otherwise
                          MUST be zero.
    @param aperiod        Period between recurring dispatches; zero
                          for one-shot. Negative values switch to
                          negative-event semantics.
    @param event_type     @c EventType (group id). Defaults to
                          @c ET_CALL.
    @param callback_event @c event_id passed on each positive-period
                          or one-shot dispatch. Ignored when
                          @p aperiod is negative. Defaults to
                          @c EVENT_IMMEDIATE.
    @param cookie         Stored verbatim in each Event's
                          @c cookie.

    @pre  @p c is non-null. The threads for @p event_type are
          spawned. At least one of @p atimeout and @p aperiod is
          zero; behavior is undefined when both are non-zero.
    @post One Event per thread in the group is enqueued, regardless
          of Event-System shutdown state.

    @return Vector of @c TSAction handles, one per thread in the
            group, in thread-index order. Each entry is a wrapper
            around the per-thread Event; cancel via the wrapper.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    Safe to call from any thread.
  */
  std::vector<TSAction> schedule_entire(Continuation *c, ink_hrtime atimeout, ink_hrtime aperiod, EventType event_type = ET_CALL,
                                        int callback_event = EVENT_IMMEDIATE, void *cookie = nullptr);

  // Defect: declared but never defined and never called. Linking against any of
  // these will fail; treat as dead declarations pending removal.
  Event *reschedule_imm(Event *e, int callback_event = EVENT_IMMEDIATE);
  Event *reschedule_at(Event *e, ink_hrtime atimeout_at, int callback_event = EVENT_INTERVAL);
  Event *reschedule_in(Event *e, ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL);
  Event *reschedule_every(Event *e, ink_hrtime aperiod, int callback_event = EVENT_INTERVAL);

  /**
    Registers a Continuation to be dispatched once on every thread of
    group @p ev_type at thread-spawn time.

    Adds an Event template to @p ev_type 's spawn queue. When each
    thread in @p ev_type starts, it walks the queue and invokes
    every registered Continuation's handler with a fresh per-thread
    Event whose @c ethread points at the spawning thread. Threads
    in @p ev_type that have already been spawned do NOT receive the
    Event.

    @param c      Continuation to dispatch on each newly spawned
                  thread of @p ev_type. Must remain valid through
                  every per-thread dispatch.
    @param ev_type @c EventType to install on. Use
                  @c register_event_type to obtain a fresh value.
    @param event  @c event_id passed to @c handleEvent on each
                  dispatch. Defaults to @c EVENT_IMMEDIATE.
    @param cookie Stored verbatim in each per-thread Event's
                  @c cookie.

    @pre  @p c is non-null. @p ev_type was returned by a prior
          @c register_event_type. The threads for @p ev_type have
          NOT yet been spawned; for the implicit @c ET_CALL group
          this means @c EventProcessor::start has not been called
          yet.
    @post The Continuation is registered for delivery on each
          thread of @p ev_type that the processor subsequently
          spawns.

    @return Pointer to the registered template Event.
            Framework-owned.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    Caller-restricted by convention: invoked from the main thread
    during process startup.
  */
  Event *schedule_spawn(Continuation *c, EventType ev_type, int event = EVENT_IMMEDIATE, void *cookie = nullptr);

  /**
    Convenience overload: invokes @p f once on every thread of
    @p ev_type at thread-spawn time.

    Wraps @p f in a framework-owned stub Continuation; semantics are
    otherwise the same as the Continuation overload above. The
    callback event is fixed to @c EVENT_IMMEDIATE.

    @param f       Free function called on each newly spawned
                   thread of @p ev_type. Receives a pointer to the
                   @c EThread on which it runs.
    @param ev_type @c EventType to install on. Use
                   @c register_event_type to obtain a fresh value.

    @pre  @p f is non-null. @p ev_type was returned by a prior
          @c register_event_type. The threads for @p ev_type have
          NOT yet been spawned.
    @post @p f is registered for delivery on each thread of
          @p ev_type that the processor subsequently spawns.

    @return Pointer to the registered template Event.
            Framework-owned.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    Caller-restricted by convention: invoked from the main thread
    during process startup.
  */
  Event *schedule_spawn(void (*f)(EThread *), EventType ev_type);

  //  Event *schedule_spawn(Continuation *c, int event, void *cookie = NULL);

  /**
    Constructs a fresh @c EventProcessor with the @c ET_CALL group
    reserved but no threads spawned.

    The global @c eventProcessor singleton is constructed at process
    startup; user code does not normally instantiate this class.

    @post @c n_thread_groups @c == @c 1 (the @c ET_CALL group is
          reserved with its registered name); @c n_ethreads and
          @c n_dthreads are zero; no EThreads exist. The internal
          @c thread_initializer Continuation is constructed and the
          dedicated-thread mutex is initialized.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    Safe to call from any thread. The constructed instance is not
    yet observable to others.
  */
  EventProcessor();
  /**
    Destroys an @c EventProcessor.

    Tears down the internal dedicated-thread spawn mutex. Does not
    stop, join, or otherwise reclaim any EThread previously spawned
    by this @c EventProcessor; those must already be quiesced.

    @pre  No EThread spawned by this @c EventProcessor is still
          running, and no thread is currently inside
          @c spawn_event_threads on this @c EventProcessor.

    @par Errors
    Aborts the process if the underlying mutex teardown reports an
    error (for example, if the mutex is still locked).

    @par Thread Safety
    Safe to call from any thread once the precondition holds.
  */
  ~EventProcessor() override;
  EventProcessor(const EventProcessor &)            = delete;
  EventProcessor &operator=(const EventProcessor &) = delete;

  /**
    Initializes the @c EventProcessor and spawns the @c ET_CALL
    thread group.

    Initializes thread-affinity bookkeeping, registers the
    Event-System metric stats, prepends a thread-affinity
    initializer Continuation to the @c ET_CALL spawn queue (so it
    runs first on every @c ET_CALL thread), and spawns
    @p n_net_threads @c REGULAR EThreads in @c ET_CALL. Each
    Continuation registered for @c ET_CALL via @c schedule_spawn
    runs once on every newly spawned thread before that thread
    enters its event loop. After this call returns, additional
    groups may be created via @c register_event_type /
    @c spawn_event_threads.

    @param n_net_threads Number of threads to spawn for the initial
                         @c ET_CALL group. MUST be positive and not
                         exceed @c MAX_EVENT_THREADS.
    @param stacksize     Per-thread stack size in bytes. Values
                         below @c INK_THREAD_STACK_MIN are clamped
                         up to it, and the result is rounded up to
                         a multiple of the page size (or huge-page
                         size when huge pages are enabled). The
                         default argument @c DEFAULT_STACKSIZE
                         selects the platform default.

    @pre  Has not been called before on any @c EventProcessor in
          this process. @p n_net_threads is positive and does not
          exceed @c MAX_EVENT_THREADS.
    @post @c ET_CALL group has @p n_net_threads spawned threads;
          @c n_ethreads is incremented by @p n_net_threads.
          @c n_thread_groups is unchanged.

    @return Zero. The contract retains the negative-on-failure
            convention from @c Processor::start, but the current
            implementation has no failure path that returns a
            value (resource exhaustion aborts).

    @par Errors
    Aborts the process via @c ink_release_assert if the
    precondition is violated or if a resource-exhaustion failure
    occurs during thread spawn.

    @par Thread Safety
    Caller-restricted: invoked once from the main thread during
    process startup.
  */
  int start(int n_net_threads, size_t stacksize = DEFAULT_STACKSIZE) override;

  /**
    Hook for shutting down the @c EventProcessor subsystem.

    @pre  No preconditions.
    @post No observable side effects.

    @par Errors
    Cannot fail.

    @par Thread Safety
    Safe to call from any thread.
  */
  // The current implementation is an empty body: it exists so the
  // override resolution from Processor::shutdown works, but there is
  // no per-subsystem work to do here. Process shutdown is handled
  // elsewhere via TSSystemState.
  void shutdown() override;

  /**
    Reserves @p size bytes inside every @c EThread's
    @c thread_private region and returns the byte offset, measured
    from the start of the @c EThread, at which those bytes begin.

    Used by subsystems that want a per-EThread chunk of state. The
    returned offset is suitable for use with @c ETHREAD_GET_PTR.
    The reserved region's starting address is 16-byte aligned, and
    @p size is rounded up to a multiple of 16.

    @param size Number of bytes to reserve. MUST be non-negative.

    @pre  @p size is non-negative.
    @post On success, the running reservation counter is advanced
          past the reserved region. On failure, no state is changed.

    @return Byte offset, measured from the start of an @c EThread,
            at which the reserved region begins; or @c -1 if the
            requested allocation would not fit within
            @c PER_THREAD_DATA.

    @par Errors
    Returns @c -1 if the requested size cannot be satisfied.

    @par Thread Safety
    Safe to call from any thread; concurrent calls are serialized
    via a compare-and-swap retry loop.
  */
  off_t allocate(int size);

  /**
    Storage for every @c REGULAR EThread spawned by this processor.

    Indices [0, @c n_ethreads) are valid pointers; remaining slots
    are @c nullptr. Consumers iterate via @c active_ethreads (or one
    of the per-group accessors) rather than indexing directly.

    @par Thread Safety
    Written only during thread-pool spawn (single-threaded process
    startup). Safe to read concurrently after startup.
  */
  EThread *all_ethreads[MAX_EVENT_THREADS];

  /**
    Per-group state for a single @c EventType.

    The thread-group id is the index into @c thread_group; it is not
    stored on the descriptor itself. Accessed by name from
    consumers that want raw access to the per-group thread vector
    or count; most consumers prefer @c active_group_threads.

    @par Ownership
    Owned by @c EventProcessor; lives for the processor's lifetime.

    @par Thread Safety
    Members are written during thread-pool spawn (single-threaded
    process startup) except for @c _started (atomic) and
    @c _next_round_robin (per-call increment by the dispatch path).
  */
  struct ThreadGroupDescriptor {
    /**
      Name registered with @c register_event_type. Stable for the
      lifetime of the descriptor.

      @par Thread Safety
      Written once at registration; safe to read concurrently.
    */
    std::string _name;
    /**
      Number of threads in this group. Set by @c spawn_event_threads
      before any of those threads start running.

      @par Thread Safety
      Plain @c int. Written at thread-pool spawn; safe to read
      concurrently after startup.
    */
    int _count = 0;
    /**
      Atomic counter incremented each time a thread in this group
      finishes its per-thread initialization and signals readiness.
      When @c _started == @c _count, the group is fully running.

      @par Thread Safety
      @c std::atomic<int>. Each thread in the group performs one
      increment with @c std::memory_order_seq_cst (the default for
      @c operator++); readers use @c load with the same default.
      Other modules may observe a value bounded by the number of
      threads that have completed their per-thread init.
    */
    std::atomic<int> _started = 0;
    /**
      Round-robin cursor used by @c assign_thread to pick the next
      thread in the group for a fresh @c schedule_imm /
      @c schedule_at / @c schedule_in / @c schedule_every call.

      @par Thread Safety
      Plain @c uint64_t. Read and incremented without
      synchronization by @c assign_thread.
    */
    // Defect: see the data-race note on EventProcessor::assign_thread.
    uint64_t _next_round_robin = 0;
    /**
      Template Events whose continuations are dispatched on each
      thread in this group at spawn time. Populated by
      @c schedule_spawn before @c spawn_event_threads runs. Each
      newly spawned thread iterates this queue read-only, building
      a per-thread Event from each entry to invoke its continuation;
      the queue itself is not drained.

      @par Thread Safety
      Written only from the main thread before the group's threads
      spawn; read-only thereafter from each spawning thread.
    */
    Que(Event, link) _spawnQueue;
    /**
      Pointers to the EThreads in this group. Indices [0,
      @c _count) are valid; remaining slots are @c nullptr.

      @par Thread Safety
      Written during thread-pool spawn; safe to read concurrently
      after startup.
    */
    EThread *_thread[MAX_THREADS_IN_EACH_TYPE] = {};
    /**
      Optional callback invoked once when every thread in the group
      has finished its per-thread initialization (i.e., when
      @c _started reaches @c _count). May be @c nullptr.

      @par Thread Safety
      Written by the main thread before the group's threads spawn;
      called once from the last spawning thread to reach the
      threshold.
    */
    std::function<void()> _afterStartCallback = nullptr;
  };

  /**
    Per-group descriptors indexed by @c EventType.

    Indices [0, @c n_thread_groups) are populated; the rest are
    default-constructed.

    @par Thread Safety
    Same as the per-descriptor contracts above.
  */
  ThreadGroupDescriptor thread_group[MAX_EVENT_TYPES];

  /**
    Number of registered thread groups.

    Bumped by @c register_event_type each time a fresh group is
    reserved.

    @par Thread Safety
    Plain @c int. Written by @c register_event_type during process
    startup; safe to read concurrently after that.
  */
  int n_thread_groups = 0;

  /**
    Total number of @c REGULAR EThreads spawned by this processor.

    Equals the sum of @c thread_group[i]._count for valid @c i.
    Excludes @c DEDICATED threads created via @c spawn_thread.

    @par Thread Safety
    Plain @c int. Written by @c spawn_event_threads during process
    startup; safe to read concurrently after that.
  */
  int n_ethreads = 0;

  /**
    Returns whether every thread in group @p etype has finished its
    per-thread initialization.

    @param etype @c EventType (group id) to query.

    @pre  @p etype is in the range [0, @c n_thread_groups).
    @post No observable side effects.
    @return @c true iff every thread in the group has signaled
            readiness; @c false otherwise.

    @par Errors
    Cannot fail.

    @par Thread Safety
    Safe to call from any thread.
  */
  bool has_tg_started(int etype);

  /*------------------------------------------------------*\
  | Unix & non NT Interface                                |
  \*------------------------------------------------------*/

  /**
    Schedules an already-initialized Event @p e onto group @p etype.

    Selects a thread within @p etype 's group as follows: if
    @p e 's continuation has a thread affinity that belongs to
    @p etype 's group, that thread is used; otherwise if the
    calling thread is itself in @p etype 's group it is used;
    otherwise the group's round-robin cursor is consulted. When the
    continuation had no prior affinity the chosen thread is
    recorded as its affinity. If the continuation has a mutex, that
    mutex is also installed on @p e. The Event is then enqueued on
    the chosen thread's external queue, using the local-enqueue
    fast path if the chosen thread is the caller's own thread.

    @param e     Event whose @c init has been called. Ownership of
                 @p e passes to the framework on success.
    @param etype @c EventType (group id) to dispatch on.

    @pre  @p e is a freshly initialized Event whose @c continuation
          is non-null. Threads for @p etype have been spawned.
    @post On success, @p e->ethread points at a thread in
          @p etype 's group, @p e->mutex equals the continuation's
          mutex when the continuation has one, and @p e is enqueued
          for delivery on the chosen thread. If the Event System is
          in shutdown, @p e is not enqueued and ownership stays
          with the caller.

    @return @p e on success, or @c nullptr if the Event System is
            in shutdown.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    Safe to call from any thread.
  */
  // Lower-level entry point used by schedule_imm / schedule_at /
  // schedule_in / schedule_every after they allocate and initialize
  // the Event.
  Event *schedule(Event *e, EventType etype);
  /**
    Returns a pointer to one of the EThreads in group @p etype,
    chosen by the round-robin cursor.

    @param etype @c EventType (group id).

    @pre  @p etype is in the range [0, @c MAX_EVENT_TYPES).
          Threads for @p etype have been spawned.
    @post When the group has more than one thread, the group's
          round-robin cursor advances by one and the returned
          thread is the one at that cursor position modulo the
          group size. Single-thread groups always return that
          one thread and leave the cursor unchanged.

    @return Pointer to a thread in the group; never @c nullptr
            given the precondition.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    Safe to call from any single thread for groups whose
    @c _count is at most one. For larger groups, concurrent
    calls update the round-robin cursor non-atomically.
  */
  // Defect: when a group has more than one thread, the cursor
  // increment `++tg->_next_round_robin` is a non-atomic
  // read-modify-write on a value concurrently read and written
  // by other threads via this routine. That is a C++ data race
  // (undefined behavior). Fix is to make `_next_round_robin`
  // `std::atomic<uint64_t>` and use a relaxed fetch_add.
  EThread *assign_thread(EventType etype);
  /**
    Returns an EThread chosen for @p cont in group @p etype using
    the affinity rule.

    Selects a thread by the following priority:
    1. If @p cont 's mutex's holding thread is in @p etype 's
       group, that thread.
    2. Otherwise if @p cont already has a registered thread
       affinity in @p etype 's group, that thread.
    3. Otherwise the group's round-robin cursor via
       @c assign_thread.

    If @p cont has no prior thread affinity, the chosen thread is
    recorded as its affinity. A prior affinity is never overwritten,
    even when it is not in @p etype 's group.

    @param cont  Continuation to schedule. Its affinity may be
                 updated as a side effect when no prior affinity
                 was set.
    @param etype @c EventType (group id) in the range
                 [0, @c MAX_EVENT_TYPES).

    @pre  @p cont is non-null and its @c mutex holds a non-null
          @c thread_holding. Threads for @p etype have been
          spawned. No other thread concurrently reads or writes
          @p cont 's @c thread_affinity.
    @post If @p cont had no prior affinity, its affinity is set to
          the returned thread. The returned thread is in
          @p etype 's group.

    @return Pointer to the chosen thread; never @c nullptr given
            the precondition.

    @par Errors
    Cannot fail at the contract level.

    @par Thread Safety
    The read and write of @p cont 's @c thread_affinity are
    unsynchronized; the caller must serialize them against any
    other accessor. Round-robin selection inherits the race
    characteristics of @c assign_thread.
  */
  EThread *assign_affinity_by_type(Continuation *cont, EventType etype);

  /**
    Storage for every @c DEDICATED EThread spawned by
    @c spawn_thread.

    Indices [0, @c n_dthreads) are valid pointers; the rest are
    @c nullptr.

    @par Thread Safety
    Written by @c spawn_thread under
    @c dedicated_thread_spawn_mutex. Safe to read concurrently
    after the spawn completes; readers MUST first read
    @c n_dthreads to know the valid range.
  */
  EThread *all_dthreads[MAX_EVENT_THREADS];
  /**
    Number of @c DEDICATED EThreads spawned by this processor.

    @par Thread Safety
    Plain @c int. Written by @c spawn_thread under
    @c dedicated_thread_spawn_mutex; safe to read concurrently
    after the spawn completes.
  */
  int n_dthreads = 0;
  /**
    Running tally of bytes reserved out of every EThread's
    @c thread_private region by @c allocate. Updated atomically by
    @c allocate so multiple subsystems can register from different
    threads without corruption.

    @par Thread Safety
    Plain @c int updated via @c ink_atomic_cas in @c allocate;
    safe for concurrent updates.
  */
  int thread_data_used = 0;

  /**
    Range view over a contiguous, valid prefix of an EThread
    pointer array.

    Provides @c begin and @c end iterators for use in range-for
    loops and STL algorithms. Constructed by @c active_ethreads,
    @c active_dthreads, and @c active_group_threads; cannot be
    constructed directly by consumers.

    @par Ownership
    Stateless reference into one of the @c EventProcessor's
    pointer arrays; outlives the underlying array only as long as
    the @c EventProcessor itself does.

    @par Thread Safety
    The view is safe to use after thread-pool startup is complete;
    iterating concurrently with thread spawn is not safe.
  */
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

  /**
    Returns a range over every @c REGULAR EThread spawned by this
    processor.

    @pre  No preconditions.
    @post No observable side effects.
    @return Range whose @c begin / @c end span @c all_ethreads[0,
            n_ethreads).

    @par Errors
    Cannot fail.

    @par Thread Safety
    Safe to call from any thread once @c spawn_event_threads has
    finished spawning every group; iterating the returned range
    concurrently with thread spawn is not safe.
  */
  active_threads_type
  active_ethreads() const
  {
    return {all_ethreads, n_ethreads};
  }

  /**
    Returns a range over every @c DEDICATED EThread spawned by
    this processor.

    @pre  No preconditions.
    @post No observable side effects.
    @return Range whose @c begin / @c end span @c all_dthreads[0,
            n_dthreads).

    @par Errors
    Cannot fail.

    @par Thread Safety
    Safe to call from any thread once dedicated-thread spawning has
    quiesced; iterating the returned range concurrently with
    @c spawn_thread is not safe.
  */
  active_threads_type
  active_dthreads() const
  {
    return {all_dthreads, n_dthreads};
  }

  /**
    Returns a range over every EThread in group @p type.

    @param type @c EventType (group id) to iterate.

    @pre  @p type is in the range [0, @c n_thread_groups).
    @post No observable side effects.
    @return Range whose @c begin / @c end span the group's
            @c _thread[0, _count) slice.

    @par Errors
    Cannot fail.

    @par Thread Safety
    Safe to call from any thread once the group's threads have
    been spawned.
  */
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
    using self = ThreadInit;
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

/**
  Global @c EventProcessor singleton.

  Defined in the inkevent library. Every executable that links
  inkevent observes this single instance; consumers schedule work
  through it (e.g., @c eventProcessor.schedule_imm(cont, ET_TASK)).

  @par Ownership
  Static-storage singleton; lives for the entire process lifetime.

  @par Thread Safety
  See @c EventProcessor type-level contract.
*/
extern class EventProcessor eventProcessor;

void thread_started(EThread *);
