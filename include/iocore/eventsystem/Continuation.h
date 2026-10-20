/** @file

  The @c Continuation base class for Event System state machines, and the
  handler type, macros, and codes used to install and invoke its handlers.

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

#include "tscore/ink_assert.h"
#include "tscore/ink_platform.h"
#include "tscore/List.h"
#include "iocore/eventsystem/Lock.h"
#include "tscore/ContFlags.h"

#include <type_traits>

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

class force_VFPT_to_top
{
public:
  virtual ~force_VFPT_to_top() {}
};

/**
  A copyable binding to an event-handler member function of
  @c Continuation or a class derived from it.

  Bind a handler of type @c int(D::*)(int, T*), then invoke it on a
  @c Continuation pointer with a @c void* payload, which is converted to
  @c T* before the call. One ContinuationHandler can thus hold, over
  time, handlers whose classes and payload types differ. Copy one to save
  a Continuation's current handler and assign it back to restore it.

  A default-constructed ContinuationHandler is null: it holds no handler.

  @par Thread Safety
  Concurrent invocations and other reads are safe. Binding a handler to
  a ContinuationHandler or assigning to it MUST NOT run concurrently
  with any other access to it.
*/
class ContinuationHandler
{
public:
  /**
    Binds @p M as the handler, or clears the handler if @p M is
    @c nullptr.

    @tparam M @c nullptr, or a non-null pointer to a member function of
              type @c int(D::*)(int, T*), where @c D is @c Continuation
              or a class derived from it.

    @post If @p M is not @c nullptr, invoking this ContinuationHandler
          calls @p M.

    @par Thread Safety
    Not thread-safe.
  */
  template <auto M>
  void
  set()
  {
    if constexpr (std::is_null_pointer_v<decltype(M)>) {
      this->handler = nullptr;
    } else {
      this->handler = &invoke_handler<M>;
    }
  }

  /**
    Tests whether this ContinuationHandler holds the handler @p M.

    @tparam M @c nullptr, or a pointer to a member function of type
              @c int(D::*)(int, T*), where @c D is @c Continuation or
              derives from it.

    @return If @p M is @c nullptr, whether this ContinuationHandler is
            null; otherwise, whether it holds @p M.

    @note @c &B::f and @c &D::f are distinct handlers even when
          @c D::f overrides @c B::f.

    @par Thread Safety
    Safe to call concurrently with other reads of this
    ContinuationHandler, but not with @c set or an assignment to it.
  */
  template <auto M>
  bool
  is() const
  {
    if constexpr (std::is_null_pointer_v<decltype(M)>) {
      return nullptr == this->handler;
    } else {
      return &invoke_handler<M> == this->handler;
    }
  }

  /**
    Invokes the bound handler on @p c with @p event and @p data.

    @param[in] b     The object to invoke the handler on.
    @param[in] event The handler's event argument.
    @param[in] data  The handler's data argument.

    @return The handler's return value.

    @pre  This ContinuationHandler holds a handler.
    @pre  @p c points to a live object of the handler's class or a class
          derived from it.
    @pre  @p data is null, or was converted to @c void* from exactly the
          handler's data parameter type, not from a pointer to a class
          derived from it.

    @note This call does not access this ContinuationHandler after
          invoking the handler, so the handler may rebind or destroy it.

    @par Thread Safety
    Safe to call concurrently with other reads of this
    ContinuationHandler, but not with writes to it. The handler's own
    requirements still apply.
  */
  int
  operator()(Continuation *c, int event, void *data) const
  {
    ink_release_assert(nullptr != this->handler && "invoked null handler");
    return this->handler(c, event, data);
  }

private:
  template <typename> struct handler_traits;

  template <typename D, typename T> struct handler_traits<int (D::*)(int, T *)> {
    using cls  = D;
    using data = T;
  };

  using handler_t = int (*)(Continuation *, int, void *);

  handler_t handler{nullptr};

  template <auto M>
  static int
  invoke_handler(Continuation *c, int event, void *data)
  {
    using Tr = handler_traits<decltype(M)>;
    return (static_cast<typename Tr::cls *>(c)->*M)(event, static_cast<typename Tr::data *>(data));
  }
};

/**
  Base class for event-driven state machines dispatched by the Event
  System.

  A Continuation pairs the @c handler that @c handleEvent invokes with
  the @c mutex that serializes those invocations. Derived classes add
  state and handler methods, and typically replace @c handler as they
  move between states.

  @note A copy shares the original's @c mutex, and its @c link refers
        to the original's list neighbors even though the copy is in no
        list.

  @par Lifetime
  A handler MUST be installed before the Continuation is scheduled or
  passed to an asynchronous operation. From then on, the Continuation
  MUST stay alive until no further event can be dispatched to it, i.e.,
  until each such operation has completed or been cancelled. It may then
  be destroyed through a @c Continuation pointer, including by
  @c delete @c this from its own handler, provided nothing, including
  callers further up the stack, accesses it afterward.

  @par Thread Safety
  Not thread-safe. While @c mutex is non-null, every invocation of
  @c handleEvent holds it, and other concurrent accesses MUST hold it
  too, except where a member documents otherwise. While @c mutex is
  null, handler invocations may run concurrently on different threads.
*/
class Continuation : private force_VFPT_to_top
{
public:
  /**
    The member function that @c handleEvent invokes for each event
    dispatched to this Continuation, or null.

    Dispatching an event is undefined unless the installed function is a
    member of this object's class or of one of its bases, and the event's
    data is null or was converted to @c void* from exactly the function's
    data parameter type, not from a pointer to a class derived from it.

    A handler may replace this field to move to its next state; the new
    handler receives the next event, not the current one.

    @par Thread Safety
    Not thread-safe. The Event System accesses this field only while
    dispatching an event, holding @c mutex if it is non-null.
  */
  ContinuationHandler handler;

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

    @note Treating null as "no change" lets a caller pass a thread that
          may be unknown, such as the current EThread or another
          Continuation's affinity, without erasing a preference set
          earlier. Clearing the affinity takes an explicit call to
          @c clearThreadAffinity.

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
    return this->handler(this, event, data);
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
  Assigns @p _h to @c this->handler.

  When a handler of @c *this calls this, the current event is
  unaffected; @p _h receives the next one.

  Dispatching an event is undefined unless @c *this is a @c D or derives
  from @c D, and the event's data is null or was converted to @c void*
  from a @c T*.

  @param[in] _h @c nullptr, or a constant expression of type
                @c int(D::*)(int, T*), such as @c &D::method.

  @post @c handler holds @p _h.
  @post In DEBUG builds, @c handler_name points to the spelling of @p _h.

  @note Expands to an expression whose value is unspecified.

  @par Thread Safety
    Not thread-safe; see @c handler.
*/
#ifdef DEBUG
#define SET_HANDLER(_h) (this->handler.template set<_h>(), this->handler_name = #_h)
#else
#define SET_HANDLER(_h) (this->handler.template set<_h>())
#endif

/**
  Assigns @p _h to @c _c->handler.

  Use to update a Continuation other than @c *this. When a handler of
  @c *_c calls this, the current event is unaffected; @p _h receives the
  next one.

  Dispatching an event to @c *_c is undefined unless @c *_c is a @c D or
  derives from @c D, and the event's data is null or was converted to
  @c void* from a @c T*.

  @param[in] _c The Continuation to update.
  @param[in] _h A constant expression: @c nullptr, or a pointer to a
                member function of type @c int(D::*)(int, T*), such as
                @c &D::method. A runtime variable is not accepted.

  @pre  @p _c points to a live Continuation.

  @post @c _c->handler holds @p _h.
  @post In DEBUG builds, @c _c->handler_name points to the spelling of @p _h.

  @note Expands to an expression whose value is unspecified. @p _c may be
        evaluated more than once, so pass an expression without side
        effects.

  @par Thread Safety
    Not thread-safe; see @c handler.
*/
#ifdef DEBUG
#define SET_CONTINUATION_HANDLER(_c, _h) ((_c)->handler.template set<_h>(), (_c)->handler_name = #_h)
#else
#define SET_CONTINUATION_HANDLER(_c, _h) ((_c)->handler.template set<_h>())
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
