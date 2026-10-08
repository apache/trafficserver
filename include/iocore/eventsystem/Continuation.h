/** @file

  Continuation base class and event handler type definitions.

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

  Continuations have a @c handleEvent method to invoke them. A
  @c ContinuationHandler (a pointer-to-member) determines the behavior
  invoked when events arrive; it is installed with the @c SET_HANDLER
  macro. Subclasses add state and additional handler methods.

 */

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/List.h"
#include "iocore/eventsystem/Lock.h"
#include "tscore/ContFlags.h"

class Continuation;
class ContinuationQueue;
class Processor;
class ProxyMutex;
class EThread;
class Event;

extern EThread *this_ethread();
extern EThread *this_event_thread();

//////////////////////////////////////////////////////////////////////////////
//
//  Constants and Type Definitions
//
//////////////////////////////////////////////////////////////////////////////

/**
  The default event code passed to @c Continuation::handleEvent when no
  Processor-specific code applies. Processors define their own non-zero
  event codes (e.g., @c EVENT_IMMEDIATE, @c VC_EVENT_READ_READY) that
  handlers dispatch on.
*/
#define CONTINUATION_EVENT_NONE 0

/**
  Handler return code signaling, by convention, that the state machine has
  finished processing this event. @c EVENT_DONE and @c VC_EVENT_DONE alias
  this value.

  The Event System dispatcher discards the handler's return value; it is
  meaningful only to callers that invoke @c handleEvent directly and define
  a convention for it.
*/
#define CONTINUATION_DONE 0

/**
  Handler return code signaling, by convention, that the state machine has
  not finished processing and expects further dispatches. @c EVENT_CONT and
  @c VC_EVENT_CONT alias this value.

  The Event System dispatcher discards the handler's return value; it is
  meaningful only to callers that invoke @c handleEvent directly and define
  a convention for it.
*/
#define CONTINUATION_CONT 1

/**
  Pointer-to-member type of an event handler, as stored in
  @c Continuation::handler.

  The handler receives the event code and payload of each event
  dispatched to the Continuation, and returns a Processor-specific
  status.

  @note A handler declared in a class derived from @c Continuation MUST be
        converted to this type with @c static_cast, which applies the
        base-class offset.

  @note Invoking a converted handler is undefined behavior unless the
        Continuation's dynamic type is, or derives from, the class that
        declared the handler.
*/
using ContinuationHandler = int (Continuation::*)(int, void *);

template <class C, typename T>
constexpr ContinuationHandler
continuation_handler_void_ptr(int (C::*fp)(int, T *))
{
  auto fp2 = reinterpret_cast<int (C::*)(int, void *)>(fp);

  // We keep this a static_cast for added static type analysis from the
  // compiler. If a compiler warning is generated for the following line of
  // code of the type "-Werror=shift-negative-value", then this may be an issue
  // with multiple inheritance of the C templated type. Make sure that for type
  // C the Continuation parent is listed first (either directly or indirectly
  // via the inheritance tree) before any other parent in the multiple class
  // hierarchy of C.
  return static_cast<ContinuationHandler>(fp2);
}

/**
  Overload for a literal @c nullptr, from which the converting overload
  cannot deduce a handler type.

  @return A null @c ContinuationHandler.

  @par Thread Safety
    Thread-safe.
*/
constexpr ContinuationHandler
continuation_handler_void_ptr(std::nullptr_t)
{
#undef X
#if !defined(__GNUC__)
#define X 1
#else
#define X (__GNUC__ > 7)
#endif
#if X
  static_assert(!static_cast<ContinuationHandler>(nullptr));
#endif
#undef X

  return static_cast<ContinuationHandler>(nullptr);
}

class force_VFPT_to_top
{
public:
  virtual ~force_VFPT_to_top() {}
};

/**
  Base class for event-driven state machines dispatched by the Event
  System.

  A Continuation pairs the @c handler that @c handleEvent invokes with
  the @c mutex that serializes those invocations. Derived classes add
  state and handler methods, and typically replace @c handler as they
  move between states.

  @note Under multiple inheritance, a derived class MUST list
        Continuation, directly or through an intermediate base, as its
        first base class. This works around a known defect: otherwise,
        installing its handlers may fail to compile with
        @c -Werror=shift-negative-value.

  @par Lifetime
  A handler MUST be installed before the Continuation is scheduled or
  passed to an asynchronous operation. From then on, the Continuation
  MUST stay alive until no further event can be dispatched to it, i.e.,
  until each such operation has completed or been cancelled. It may then
  be destroyed through a @c Continuation pointer, including by
  @c delete @c this from its own handler, provided nothing, including
  callers further up the stack, accesses it afterward.

  @par Thread Safety
  Not thread-safe. While @c mutex is non-null, concurrent accesses must
  hold it, except where a member documents otherwise; the Event System
  holds it for the duration of each dispatch.
*/
class Continuation : private force_VFPT_to_top
{
public:
  /**
    The member function that @c handleEvent invokes.

    Install a handler with @c SET_HANDLER or @c SET_CONTINUATION_HANDLER
    rather than by direct assignment; see @c ContinuationHandler for why
    a @c reinterpret_cast is unsafe. A handler may replace this field
    while it runs; the new handler receives the next dispatch.

    @par Thread Safety
    Not thread-safe.
  */
  ContinuationHandler handler = nullptr;

#ifdef DEBUG
  /**
    Name of the most recently installed handler, captured by
    @c SET_HANDLER / @c SET_CONTINUATION_HANDLER for diagnostic use.
    Present only in DEBUG builds. Initial value is null. Same
    synchronization rules as @c handler.
  */
  const char *handler_name = nullptr;
#endif

  /**
    The mutex held while events are dispatched to this Continuation, or
    null.

    The Continuation shares ownership of the referenced @c ProxyMutex.
    If the field is null when an event is scheduled, the scheduler may
    choose a mutex to hold for that dispatch and may store it here, or
    may dispatch without holding any mutex. Leave the field null only if
    the Continuation's state needs no serialization and every
    asynchronous operation it is passed to accepts a null mutex.

    Reassigning the field while an event or asynchronous operation
    targeting this Continuation is pending is undefined behavior.

    @par Thread Safety
    Not thread-safe. Holding the referenced mutex does not protect the
    field itself, and scheduling this Continuation may write it.
  */
  Ptr<ProxyMutex> mutex;

  /**
    Returns a raw pointer to the @c ProxyMutex protecting this
    Continuation, without changing the reference count.

    @return The @c ProxyMutex currently held in @c this->mutex, or
            nullptr if the field is null. The pointer is valid only
            while @c this->mutex retains a reference to it; to keep
            the mutex alive past the Continuation's destruction or a
            reassignment of @c this->mutex, the caller MUST take its
            own @c Ptr<ProxyMutex> rather than store the raw pointer.

    @par Thread Safety
    Caller-synchronized. Callers MUST order this read against any
    concurrent writers via an external happens-before edge.
  */
  ProxyMutex *
  getMutex() const
  {
    return mutex.get();
  }

  /**
    Doubly-linked list hook used to enqueue this Continuation in
    intrusive lists whose list-traits class is the nested type
    @c Continuation::Link_link (declared by the @c LINK macro). Both
    @c next and @c prev are null-initialized by @c Link<Continuation>'s
    own default constructor, leaving the hook in the unlinked state.

    @par Thread Safety
    Plain links. The owner of the list (the Processor or subsystem
    that holds the queue) is responsible for synchronizing
    insertion, removal, and traversal. Because there is a single
    @c next / @c prev pair, the Continuation MUST belong to at most
    one such list at a time.
  */
  LINK(Continuation, link);

  /**
    Control flags that may be installed as the current thread's control
    flags before an event is dispatched to this Continuation.

    Initialized from the constructing thread's current control flags.
    Scheduling an event for this Continuation may overwrite this field
    with the scheduling thread's current control flags.

    @par Thread Safety
    Not thread-safe. Scheduling and some dispatch paths access this
    field without holding @c mutex, so holding @c mutex is not enough
    to avoid a data race.
  */
  ContFlags control_flags;

  /**
    The EThread on which events for this Continuation should preferably
    be dispatched, or null for no preference.

    The preference is advisory: a Processor may dispatch on another
    thread, for example when the preferred thread does not service the
    requested event type. Scheduling an event for this Continuation may
    set a null field to the thread chosen for that event.

    A non-null value MUST point to an EThread that is alive whenever
    this Continuation is scheduled.

    @par Thread Safety
    Not thread-safe. Scheduling accesses this field without holding
    @c mutex, so holding @c mutex is not enough to avoid a data race.
  */
  EThread *thread_affinity = nullptr;

  /**
    Sets the preferred dispatch thread for this Continuation.

    @param[in] ethread The EThread to bind to. Passing nullptr is treated
                       as "no change" (use @c clearThreadAffinity to clear);
                       the call returns false in that case.
    @return true if @p ethread was non-null and the affinity was set;
            false if @p ethread was null and no change was made.

    @par Thread Safety
    Caller-synchronized; see @c thread_affinity.
  */
  bool
  setThreadAffinity(EThread *ethread)
  {
    if (ethread != nullptr) {
      thread_affinity = ethread;
      return true;
    }
    return false;
  }

  /**
    Returns the EThread previously installed as this Continuation's
    affinity, or nullptr if none has been set.

    @par Thread Safety
    Caller-synchronized read; see @c thread_affinity.
  */
  EThread *
  getThreadAffinity()
  {
    return thread_affinity;
  }

  /**
    Clears the dispatch-thread affinity, restoring the "no preference"
    state.

    @par Thread Safety
    Caller-synchronized; see @c thread_affinity.
  */
  void
  clearThreadAffinity()
  {
    thread_affinity = nullptr;
  }

  /**
    Invokes the installed @c handler with @p event and @p data.

    @param[in] event Event code. Its meaning is defined by the sender.
    @param[in] data  Event payload. Its type, lifetime, and ownership are
                     defined by the sender.

    @return The handler's return value. Its meaning is defined by the
            protocol between the caller and the handler.

    @pre  @c handler is non-null.
    @pre  If @c mutex is non-null, the calling thread holds it.

    @note The handler may destroy this Continuation. Unless the protocol
          guarantees otherwise, do not access the Continuation after
          this call returns.

    @par Thread Safety
    Not thread-safe.
  */
  TS_INLINE int
  handleEvent(int event = CONTINUATION_EVENT_NONE, void *data = nullptr)
  {
    // If there is a lock, we must be holding it on entry
    ink_release_assert(!mutex || mutex->thread_holding == this_ethread());
    return (this->*handler)(event, data);
  }

protected:
  /**
    Constructs a Continuation protected by @p amutex.

    The Continuation takes a shared reference to @p amutex. If no other
    reference exists, the mutex is destroyed when the Continuation
    releases its reference, so a raw pointer kept by the caller dangles
    after that.

    @param[in] amutex The mutex to protect this Continuation, or nullptr.
                      See @c mutex for the restrictions on dispatching a
                      Continuation whose mutex is null.

    @pre  @p amutex is null, or was allocated with @c new_ProxyMutex().

    @post @c mutex refers to @p amutex.
    @post @c control_flags equals the calling thread's current control
          flags.
    @post @c handler is null.

    @par Thread Safety
      Safe to call concurrently with code that takes or releases other
      references to @p amutex, provided @p amutex cannot be destroyed
      before this call returns.
  */
  explicit Continuation(ProxyMutex *amutex = nullptr);

  /**
    Constructs a Continuation protected by the mutex that @p amutex
    refers to.

    The Continuation shares ownership of that mutex with @p amutex; it
    does not modify @p amutex.

    @param[in] amutex The mutex to protect this Continuation. May be
                      null; see @c mutex for the restrictions on
                      dispatching a Continuation whose mutex is null.

    @post @c mutex refers to the same @c ProxyMutex as @p amutex.
    @post @c control_flags equals the calling thread's current control
          flags.
    @post @c handler is null.

    @par Thread Safety
    Safe to call concurrently with other code that takes or releases
    references to the same @c ProxyMutex. @p amutex itself MUST NOT
    be written concurrently with this call.
  */
  explicit Continuation(Ptr<ProxyMutex> &amutex);
};

/**
  Installs @p _h as the handler invoked by the enclosing Continuation's
  @c handleEvent.

  Expands to an assignment to @c handler (and @c handler_name in DEBUG
  builds) using @c continuation_handler_void_ptr to enforce that the
  handler's class derives from @c Continuation. Intended for use from
  within a member function of a Continuation-derived class, where
  @c handler refers to @c this->handler.

  @param[in] _h Pointer-to-member function with signature
               @c int(C::*)(int, T*) for some Continuation-derived @c C
               and some pointer type @c T*. May also be @c nullptr to
               detach the handler.

  @pre  Invocation context MUST refer to a Continuation instance
        (@c handler is the member of that instance).
  @post @c handler points to the type-cast form of @p _h. In DEBUG
        builds, @c handler_name holds the stringified token of @p _h.

  @par Errors
  A @c C that does not derive from @c Continuation is a compile-time
  error from @c continuation_handler_void_ptr. The data parameter type
  @c T* is @b not checked — it is reinterpret-cast, so the handler and
  the Processor delivering the event MUST agree on it by convention.

  @par Thread Safety
  Caller-synchronized via the enclosing Continuation's mutex. Concurrent
  installation racing against a dispatching handler is undefined.
*/
#ifdef DEBUG
#define SET_HANDLER(_h) (handler = continuation_handler_void_ptr(_h), handler_name = #_h)
#else
#define SET_HANDLER(_h) (handler = continuation_handler_void_ptr(_h))
#endif

/**
  Installs @p _h as the handler of the Continuation pointed to by
  @p _c.

  Same semantics as @c SET_HANDLER, but operates on an explicit
  Continuation pointer rather than the implicit @c this. Use when a
  Continuation needs to install a handler on another Continuation it
  owns (e.g., a parent state machine arming a child's handler before
  dispatch).

  @param[in] _c Non-null pointer to the target Continuation.
  @param[in] _h Pointer-to-member function as for @c SET_HANDLER.

  @pre  @p _c is non-null and refers to a live Continuation.
  @post @c _c->handler points to the type-cast form of @p _h. In DEBUG
        builds, @c _c->handler_name holds the stringified token of
        @p _h.

  @par Errors
  Same compile-time checking as @c SET_HANDLER.

  @par Thread Safety
  Caller-synchronized via @c _c->mutex.
*/
#ifdef DEBUG
#define SET_CONTINUATION_HANDLER(_c, _h) (_c->handler = continuation_handler_void_ptr(_h), _c->handler_name = #_h)
#else
#define SET_CONTINUATION_HANDLER(_c, _h) (_c->handler = continuation_handler_void_ptr(_h))
#endif

inline Continuation::Continuation(Ptr<ProxyMutex> &amutex) : mutex(amutex)
{
  // Pick up the control flags from the creating thread
  this->control_flags.set_flags(get_cont_flags().get_flags());
}

inline Continuation::Continuation(ProxyMutex *amutex) : mutex(amutex)
{
  // Pick up the control flags from the creating thread
  this->control_flags.set_flags(get_cont_flags().get_flags());
}
