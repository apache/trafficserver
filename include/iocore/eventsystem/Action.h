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

/** Handle for cancelling a pending asynchronous operation.

  A function that starts an asynchronous operation on behalf of a
  Continuation returns an @c Action* for that operation. Calling cancel()
  before the operation completes guarantees that @c continuation receives
  no further callbacks for it.

  A function returning @c Action* may instead return nullptr or a sentinel
  value it documents; neither may be dereferenced.

  @par Lifetime
  The object performing the operation owns the Action and controls its
  lifetime; callers must not delete it. Unless the owner documents
  otherwise, callers must not access an Action after calling cancel() or
  after the operation completes.

  @par Thread Safety
  Not thread-safe. Callers of cancel() must hold @c mutex.
*/
class Action
{
public:
  /**
    The Continuation called back for this operation, or nullptr if none.

    The Action does not own this Continuation; it must remain valid until
    the operation completes or is cancelled. Only the object performing
    the operation may modify this field.

    @par Thread Safety
    Not thread-safe.
  */
  Continuation *continuation = nullptr;

  /**
    The mutex that serializes cancel() against callbacks for this
    operation, or null if nothing serializes them.

    Only the object performing the operation may modify this field.

    @par Thread Safety
    Concurrent reads are thread-safe. The object performing the
    operation does not modify this field concurrently with a caller's
    access to the Action.
  */
  Ptr<ProxyMutex> mutex;

  /**
    Whether the operation has been cancelled.

    cancel() and cancel_action() set it to true. Once it is true, the
    owning Processor MUST NOT call back @c continuation for this
    operation.

    Only the owning Processor may assign this field directly, and it may
    reset it to false only when reusing the Action for a new operation.

    @par Thread Safety
    Not thread-safe. Concurrent accessors must hold @c mutex.
  */
  bool cancelled = false;

  /**
    Cancels the asynchronous operation represented by this Action.

    Derived classes may override this to release resources held by the
    operation.

    @param[in] c The cancelling Continuation, or nullptr.

    @pre  This Action has not already been cancelled.
    @pre  @p c is nullptr or equal to @c continuation.

    @post @c continuation receives no further callbacks for this
          operation. Unless the owning object documents otherwise, this
          Action may be deallocated at any time and must not be accessed
          again.

    @par Thread Safety
    Not thread-safe. The caller must hold @c mutex.
  */
  virtual void
  cancel(Continuation *c = nullptr)
  {
    ink_assert(!c || c == continuation);
    ink_assert(!cancelled);
    cancelled = true;
  }

  /**
    Cancels the operation without invoking any override of cancel().

    Use it only when the operation does not depend on the work an override
    of cancel() performs.

    @param[in] c nullptr, or the Continuation that initiated this Action.

    @pre  This Action has not already been cancelled.
    @pre  @p c is nullptr or equal to @c continuation.

    @post The Processor does not call back @c continuation for this
          operation.

    @par Thread Safety
    Not thread-safe. The caller must hold @c mutex.
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

    @param[in] acont The Continuation that will cancel and be called back
                     on this Action, or nullptr to detach.

    @return Returns @p acont.

    @pre  If @p acont is non-null, it points to a live Continuation.

    @post @c this->continuation == @p acont. If @p acont is non-null,
          @c this->mutex refers to the same @c ProxyMutex as
          @c acont->mutex; otherwise @c this->mutex is null.
          @c this->cancelled is unchanged.

    @par Thread Safety
    Not thread-safe. The call MUST NOT overlap any other access to this
    Action.
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
    Constructs an uncancelled Action bound to no Continuation and holding
    no mutex.

    @par Thread Safety
    Thread-safe.
  */
  Action() {}

  /**
    Releases this Action's reference to @c mutex.

    @par Thread Safety
    Not thread-safe. Destruction MUST NOT overlap any other access to this
    Action.
  */
  virtual ~Action() {}
};

/**
  Sentinel @c Action* meaning the request left nothing to cancel.

  A Processor returns this instead of a real Action when it settled the
  request, successfully or not, before returning. Whether and how the
  Continuation was notified is Processor-specific; typically its handler
  has already run inline, during the call that returned this value.

  Compare against this value with @c ==. It MUST NOT be dereferenced or
  cancelled.

  @note If the Continuation's handler ran inline, it may have destroyed
        the Continuation before this value was returned.
*/
#define ACTION_RESULT_DONE MAKE_ACTION_RESULT(1)

/**
  Sentinel @c Action* returned when the Processor fails a request with an
  I/O error before returning, leaving no operation pending.

  It is not a real Action and MUST NOT be dereferenced. Whether the
  Processor invoked the Continuation with an error event before returning
  is Processor-specific; if it did, the Continuation may have been
  deallocated during that call.
*/
#define ACTION_IO_ERROR MAKE_ACTION_RESULT(2)

// Processors that need additional sentinels define them with
// MAKE_ACTION_RESULT, e.g.
//   #define MY_PROCESSOR_BASE         3
//   #define ACTION_RESULT_MY_FAILURE  MAKE_ACTION_RESULT(MY_PROCESSOR_BASE + 0)

/**
  Encodes a small integer as a sentinel @c Action* that a Processor may
  return in place of a real Action.

  Distinct values of @p _x yield distinct sentinels. Every sentinel has
  bit 0 of its integer representation set, so it never equals the
  address of a real Action. A sentinel MUST NOT be dereferenced.

  @param[in] _x A non-negative integer expression identifying the sentinel.

  @return The sentinel @c Action* for @p _x.

  @pre  2 * @p _x + 1 is representable in the type of @p _x.

  @note Values 1 and 2 are already used by this header for inline
        completion and inline I/O error. A Processor that defines its own
        sentinels MUST use other values.

  @note @p _x is substituted without parentheses. Parenthesize an argument
        that contains an operator binding more loosely than @c <<, for
        example @c MAKE_ACTION_RESULT((c ? 3 : 4)).

  @par Thread Safety
  Thread-safe.
*/
#define MAKE_ACTION_RESULT(_x) (Action *)(((uintptr_t)((_x << 1) + 1)))
