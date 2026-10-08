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

#include <cstddef>

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
  A copyable, type-erased binding to an event-handler member function of
  @p Base or a class derived from it.

  Bind a handler of type @c int(DerivedType::*)(int event, DataType *data),
  then invoke it on a @p Base object with an untyped @c void* payload,
  which is converted back to @c DataType* before the call. This lets one
  object hold handlers whose classes and data parameter types differ.

  A default-constructed EventCallback holds no handler.

  @tparam Base The class whose objects handlers are invoked on.

  @par Thread Safety
  Not thread-safe. An assignment MUST NOT run concurrently with any other
  access to the same object.
*/
template <typename Base> class EventCallback
{
public:
  /** Assigns @p m as the member function to invoke.
   *
   * @tparam DerivedType @p Base, or a class unambiguously and non-virtually
   *                     derived from it.
   *
   * @param[in] m The member function to invoke.
   *
   * @return @c *this.
   *
   * @post Invoking @c *this on object @c b with event @c e and data @c d
   *       calls @c (static_cast<DerivedType*>(b)->*m)(e, static_cast<DataType*>(d)).
   *
   * @par Thread Safety
   *   Not thread-safe.
   */
  template <typename DerivedType, typename DataType>
  EventCallback &
  operator=(int (DerivedType::*m)(int event, DataType *data))
  {
    this->handler = make_continuation_handler(m);
    this->thunk   = &call_handler_with_type<DataType>;
    return *this;
  }

  /** Clears the callback.
   *
   * @post @c *this holds no handler. Invoking it is undefined until a
   *       handler is assigned.
   *
   * @par Thread Safety
   *   Not thread-safe.
   */
  EventCallback &
  operator=(std::nullptr_t)
  {
    this->handler = nullptr;
    this->thunk   = nullptr;
    return *this;
  }

  /**
    Invokes the bound handler on @p b with @p event and @p data.

    @param[in] b     The object to invoke the handler on.
    @param[in] event The handler's event argument.
    @param[in] data  The handler's data argument.

    @return The handler's return value.

    @pre  This EventCallback is not null.
    @pre  @p b points to a live object of the handler's class or a class
          derived from it.
    @pre  @p data is null, or was obtained by converting a pointer of the
          handler's data parameter type to @c void*.

    @note This call does not access this EventCallback after invoking the
          handler, so the handler may destroy it.
  */
  int
  operator()(Base *b, int event, void *data)
  {
    return this->thunk(b, this->handler, event, data);
  }

  /**
    Tests whether @p m is the assigned member function, e.g. to check
    which state a state machine is in.

    @param[in] m The member function to compare against.

    @return true if @p m is the assigned member function, or if @p m is
            null and nothing is assigned; false otherwise. The result is
            unspecified if @p m or the assigned member function is
            virtual.

    @note The program is ill-formed unless @c DerivedType is @p Base, or
          derives from it unambiguously, accessibly, and non-virtually.
  */
  template <typename DerivedType, typename DataType>
  bool
  operator==(int (DerivedType::*m)(int event, DataType *data)) const
  {
    // The only guarantee we have about the stored void* handler is that we
    // can safely cast it back to its original type. Therefore, we must do
    // exactly that to compare the function pointers in a well-defined way.
    if (this->thunk != &call_handler_with_type<DataType>) {
      return this->thunk == nullptr && m == nullptr;
    }

    auto const stored{reinterpret_cast<int (Base::*)(int, DataType *)>(this->handler)};

    return stored == static_cast<int (Base::*)(int, DataType *)>(m);
  }

  /** Tests whether no handler is installed.
   *
   * An EventCallback has no handler when default-constructed or after
   * @c nullptr is assigned to it.
   *
   * @return true if no handler is installed; false otherwise.
   *
   * @par Thread Safety
   *   Not thread-safe.
   */
  bool
  operator==(std::nullptr_t) const
  {
    return this->handler == nullptr;
  }

private:
  using handler_t = int (Base::*)(int, void *);
  using thunk_t   = int (*)(Base *, handler_t, int, void *);

  handler_t handler{nullptr};

  // This is the deferred invocation of the handler. By building the
  // expression when we set the handler, we can build the expression
  // with the correct data type.
  thunk_t thunk{nullptr};

  template <typename DerivedType, typename DataType>
  static handler_t
  make_continuation_handler(int (DerivedType::*m)(int event, DataType *data))
  {
    auto const base_handler{static_cast<int (Base::*)(int event, DataType *data)>(m)};
    return reinterpret_cast<handler_t>(base_handler);
  }

  template <typename DataType>
  static int
  call_handler_with_type(Base *b, handler_t handler, int event, void *data)
  {
    auto const restored_cb{reinterpret_cast<int (Base::*)(int, DataType *)>(handler)};
    return (b->*restored_cb)(event, static_cast<DataType *>(data));
  }
};

/** The type of @c Continuation::handler.
 *
 * Aliases @c EventCallback<Continuation>, which holds a member function
 * of a Continuation-derived class @c D with signature @c int(int, @c T*)
 * for any @c T, and passes it the @c void* event payload converted to
 * @c T*. Handlers can thus declare the payload type they expect instead
 * of casting from @c void* themselves.
 *
 * A default-constructed value is null. Assigning @c &D::method installs
 * a handler; assigning @c nullptr clears it. @c == compares against
 * either form.
 *
 * @pre  Invoking @c h(c, event, data) requires that @c h is non-null,
 *       that @c c points to a @c D or an object of a class derived from
 *       @c D, and that @c data is null or was converted to @c void* from
 *       a @c T* (not from a pointer to a class derived from @c T).
 *
 * @par Thread Safety
 *   Not thread-safe.
 */
using ContinuationHandler = EventCallback<Continuation>;

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

    The installed function MUST be a member of this object's class or of
    one of its bases, and its data parameter type MUST be exactly the
    pointer type that senders of events to this Continuation convert to
    @c void*. Otherwise, dispatching an event is undefined behavior.

    A handler may assign this field to move to its next state; the new
    handler receives the next event, not the current one.

    @par Thread Safety
    Not thread-safe. The Event System accesses this field only while
    dispatching an event, holding @c mutex if it is non-null.
  */
  EventCallback<Continuation> handler;

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

  Use where @c this points to the object to update, e.g. in its
  constructor or in one of its handlers. When a handler calls this, the
  current event is unaffected; @p _h receives the next one.

  Dispatching an event is undefined unless @c *this is a @c D or derives
  from @c D, and the event's data is null or was converted to @c void*
  from a @c T*.

  @param[in] _h @c nullptr, or a pointer to a member function of type
                @c int(D::*)(int, T*).

  @post @c handler holds @p _h.
  @post In DEBUG builds, @c handler_name points to the spelling of @p _h.

  @note Expands to an expression whose value is unspecified. @p _h may be
        evaluated more than once.

  @par Thread Safety
    Not thread-safe; see @c handler.
*/
#ifdef DEBUG
#define SET_HANDLER(_h) (this->handler = _h, this->handler_name = #_h, _h)
#else
#define SET_HANDLER(_h) (this->handler = _h, _h)
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
  @param[in] _h @c nullptr, or a pointer to a member function of type
                @c int(D::*)(int, T*).

  @pre  @p _c points to a live Continuation.

  @post @c _c->handler holds @p _h.
  @post In DEBUG builds, @c _c->handler_name points to the spelling of @p _h.

  @note Expands to an expression whose value is unspecified. @p _c and
        @p _h may be evaluated more than once, and @p _c may be expanded
        without enclosing parentheses, so pass a side-effect-free name or
        member access for @p _c.

  @par Thread Safety
    Not thread-safe; see @c handler.
*/
#ifdef DEBUG
#define SET_CONTINUATION_HANDLER(_c, _h) (_c->handler = _h, _c->handler_name = #_h, _h)
#else
#define SET_CONTINUATION_HANDLER(_c, _h) (_c->handler = _h, _h)
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
