/** @file

  Generic interface which enables any event or async activity to be cancelled

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

#include "iocore/eventsystem/Thread.h"
#include "iocore/eventsystem/Continuation.h"

/**
  Handle to an in-flight asynchronous operation.

  An Action is returned by a Processor when it accepts an asynchronous
  request from a Continuation. Holding the Action lets the Continuation
  cancel the operation before it completes; once cancelled, the Continuation
  will not be called back for that operation.

  Processors that derive from Action attach additional state to the handle.
  Processors that complete a request synchronously (re-entrantly) MAY return
  a sentinel @c Action* (see @c MAKE_ACTION_RESULT) instead of a real
  pointer; callers MUST check the low bit of the returned pointer to
  distinguish a real Action from a sentinel before dereferencing.

  @par Ownership
  Allocated by the Processor that returned the Action; deallocated by that
  same Processor when the operation completes or is cancelled. Callers
  MUST NOT delete an Action* and MUST NOT access an Action after the
  operation it represents has completed or after they have called cancel().

  @par Thread Safety
  Not instance-thread-safe. The Continuation that initiated the Action is
  the only legitimate canceller, and it MUST hold its own @c ProxyMutex
  (the same mutex stored in @c Action::mutex) while calling cancel(). The
  Processor MUST guarantee that no callbacks are delivered to a cancelled
  Action.
*/
class Action
{
public:
  /**
    The Continuation that initiated this Action.

    @par Thread Safety
    The owning Processor binds this field (via @c operator= or by
    direct assignment) before the Action is made observable to a
    canceller, and the value is stable from that point until the
    Processor releases the Action. Rebinding (including to nullptr)
    is the Processor's responsibility and MUST be serialized against
    any concurrent cancel().
  */
  Continuation *continuation = nullptr;

  /**
    A retained reference to the initiating Continuation's @c ProxyMutex.

    Held independently of @c continuation so that @c cancelled remains
    accessible under a valid lock even after the initiating Continuation
    has been deallocated.

    @par Thread Safety
    The owning Processor binds this field (via @c operator= or by
    direct assignment) before the Action is made observable to a
    canceller, and the value is stable from that point until the
    Processor releases the Action. Rebinding (including to nullptr)
    is the Processor's responsibility and MUST be serialized against
    any concurrent cancel(). The retained reference keeps the
    @c ProxyMutex alive for as long as it is held.
  */
  Ptr<ProxyMutex> mutex;

  /**
    Set to true after cancel() or cancel_action() is invoked. Initially
    false.

    The owning Processor MAY clear this flag back to false when recycling
    the Action for a new operation, before the recycled Action is
    published to a canceller.

    @par Thread Safety
    Plain @c bool. Readers and writers MUST hold @c this->mutex, except
    that the owning Processor MAY clear it without the lock while the
    Action is not yet published to any canceller. The Processor MUST
    inspect this flag under @c this->mutex immediately before invoking
    @c continuation, and MUST NOT invoke @c continuation if the flag is
    set.
  */
  bool cancelled = false;

  /**
    Cancels the asynchronous operation represented by this Action.

    After a successful return, no callback for this operation will be
    delivered to @c continuation. Derived Processors may override this
    method to release additional resources before flagging the Action as
    cancelled.

    @param c The Continuation associated with this Action, or nullptr.
             If non-null, MUST equal @c this->continuation.

    @pre  @c this->cancelled is false.
    @pre  Caller MUST hold @c this->mutex.
    @pre  Caller is the Continuation referenced by @c this->continuation,
          i.e. the same Continuation that initiated the Action.
    @post @c this->cancelled is true. The Processor will not invoke
          @c continuation for this operation.

    @par Errors
    Cannot fail. Precondition violations are checked by @c ink_assert
    in debug builds and produce undefined behavior in release builds.

    @par Thread Safety
    Caller-synchronized. The caller MUST hold @c this->mutex. Concurrent
    cancellation from multiple threads is a precondition violation.
  */
  virtual void
  cancel(Continuation *c = nullptr)
  {
    ink_assert(!c || c == continuation);
    ink_assert(!cancelled);
    cancelled = true;
  }

  /**
    Flags the Action as cancelled without invoking any derived-class
    cancellation logic.

    Performs only the base cancellation: marks the Action cancelled so
    that the Processor will not invoke @c continuation for this
    operation. Any cleanup that a derived Processor performs from its
    overridden @c cancel() is skipped.

    @param c The Continuation associated with this Action, or nullptr.
             If non-null, MUST equal @c this->continuation.

    @pre  @c this->cancelled is false.
    @pre  Caller MUST hold @c this->mutex.
    @pre  Caller is the Continuation referenced by @c this->continuation,
          i.e. the same Continuation that initiated the Action.
    @post @c this->cancelled is true. The Processor will not invoke
          @c continuation for this operation.

    @par Errors
    Cannot fail. Precondition violations are checked by @c ink_assert
    in debug builds and produce undefined behavior in release builds.

    @par Thread Safety
    Caller-synchronized. The caller MUST hold @c this->mutex. Concurrent
    cancellation from multiple threads is a precondition violation.
  */
  void
  cancel_action(Continuation *c = nullptr)
  {
    ink_assert(!c || c == continuation);
    ink_assert(!cancelled);
    cancelled = true;
  }

  /**
    Binds this Action to a Continuation and retains a reference to that
    Continuation's mutex.

    @param acont The Continuation that will cancel and be called back on
                 this Action. May be nullptr to detach.
    @return @p acont, for assignment chaining.

    @pre  None.
    @post @c this->continuation == @p acont. If @p acont is non-null,
          @c this->mutex refers to the same @c ProxyMutex as
          @c acont->mutex; otherwise @c this->mutex is null. The
          @c cancelled flag is unchanged.

    @par Errors
    Cannot fail.

    @par Thread Safety
    Intended to be invoked by the Processor that owns the Action, before
    the Action is published to other threads. Not safe against concurrent
    cancel() once the Action has been published.
  */
  Continuation *
  operator=(Continuation *acont)
  {
    continuation = acont;
    if (acont) {
      mutex = acont->mutex;
    } else {
      mutex = nullptr;
    }
    return acont;
  }

  /**
    Constructs an Action with no associated Continuation and no retained
    mutex.

    @pre  None.
    @post @c continuation is nullptr; @c mutex is null;
          @c cancelled is false.

    @par Errors
    Cannot fail.

    @par Thread Safety
    Safe to call from any thread.
  */
  Action() {}

  /**
    Releases the retained reference to the Continuation's mutex.

    @pre  The Action's owning Processor has completed its lifecycle for
          this Action (the operation has finished or has been cancelled).
    @post The retained @c ProxyMutex reference is dropped; if this was
          the last reference, the @c ProxyMutex is destroyed.

    @par Errors
    Cannot fail.

    @par Thread Safety
    The owning Processor invokes destruction; it MUST guarantee no other
    thread accesses the Action concurrently.
  */
  virtual ~Action() {}
};

/**
  Sentinel return value: the Processor completed the request inline and
  has already invoked the Continuation. The caller MUST treat the returned
  pointer as a tag, not a real @c Action*. Equivalent to
  @c MAKE_ACTION_RESULT(1).

  When a caller observes this sentinel, the Continuation may have been
  deallocated during the inline callback; the caller MUST NOT touch any
  state that the Continuation owned after seeing this value.
*/
#define ACTION_RESULT_DONE MAKE_ACTION_RESULT(1)

/**
  Sentinel return value: the Processor failed the request inline with an
  I/O error and has already invoked the Continuation with a
  Processor-specific error event. The caller MUST treat the returned
  pointer as a tag, not a real @c Action*. Equivalent to
  @c MAKE_ACTION_RESULT(2).

  When a caller observes this sentinel, the Continuation may have been
  deallocated during the inline callback; the caller MUST NOT touch any
  state that the Continuation owned after seeing this value.
*/
#define ACTION_IO_ERROR MAKE_ACTION_RESULT(2)

// Processors that need additional sentinels define them with
// MAKE_ACTION_RESULT, e.g.
//   #define MY_PROCESSOR_BASE         3
//   #define ACTION_RESULT_MY_FAILURE  MAKE_ACTION_RESULT(MY_PROCESSOR_BASE + 0)

/**
  Constructs a sentinel @c Action* from a small integer.

  The encoding shifts @p _x left by one bit and sets the low bit, so
  every sentinel has bit 0 set and is therefore distinguishable from any
  validly-aligned @c Action*. A receiver of an @c Action* MUST inspect
  @c ((uintptr_t)p & 1) before dereferencing: when set, the value is a
  sentinel that MUST be compared against @c ACTION_RESULT_* constants
  rather than treated as an @c Action.

  Sentinel values MUST NOT collide; the convention is for each Processor
  that defines its own sentinels to reserve a numeric base constant and
  define sentinels relative to that base.

  @param _x A non-negative integer expression. After the @c "<<1" shift
            and @c "+1", the result MUST fit in @c uintptr_t. Behavior
            for values that overflow the shift is undefined.

  @par Errors
  Cannot fail.

  @par Thread Safety
  Safe to evaluate from any thread; the expansion is a pure expression
  with no shared state.
*/
#define MAKE_ACTION_RESULT(_x) (Action *)(((uintptr_t)((_x << 1) + 1)))
