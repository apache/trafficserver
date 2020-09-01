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

#include "tscore/ink_platform.h"
#include "I_Thread.h"
#include "I_Continuation.h"

/**
  Represents an operation initiated on a Processor.

  The Action class is an abstract representation of an operation
  being executed by some Processor. A reference to an Action object
  allows you to cancel an ongoing asynchronous operation before it
  completes. This means that the Continuation specified for the
  operation will not be called back.

  Actions or classes derived from Action are the typical return
  type of methods exposed by Processors in the Event System and
  throughout the IO Core libraries.

  The canceller of an action must be the state machine that will
  be called back by the task and that state machine's lock must be
  held while calling cancel.

  Processor implementers:

  You must ensure that no events are sent to the state machine after
  the operation has been cancelled appropriately.

  Returning an Action:

  Processor functions that are asynchronous must return actions to
  allow the calling state machine to cancel the task before completion.
  Because some processor functions are reentrant, they can call
  back the state machine before the returning from the call that
  creates the actions. To handle this case, special values are
  returned in place of an action to indicate to the state machine
  that the action is already completed.

    - @b ACTION_RESULT_DONE The processor has completed the task
      and called the state machine back inline.
    - @b ACTION_RESULT_INLINE Not currently used.
    - @b ACTION_RESULT_IO_ERROR Not currently used.

  To make matters more complicated, it's possible if the result is
  ACTION_RESULT_DONE that state machine deallocated itself on the
  reentrant callback. Thus, state machine implementers MUST either
  use a scheme to never deallocate their machines on reentrant
  callbacks OR immediately check the returned action when creating
  an asynchronous task and if it is ACTION_RESULT_DONE neither read
  nor write any state variables. With either method, it's imperative
  that the returned action always be checked for special values and
  the value handled accordingly.

  Allocation policy:

  Actions are allocated by the Processor performing the actions.
  It is the processor's responsibility to handle deallocation once
  the action is complete or cancelled. A state machine MUST NOT
  access an action once the operation that returned the Action has
  completed or it has cancelled the Action.

*/
class Action
{
public:
  /**
    Continuation that initiated this action.

    The reference to the initiating continuation is only used to
    verify that the action is being cancelled by the correct
    continuation.  This field should not be accessed or modified
    directly by the state machine.

  */
  Continuation *continuation = nullptr;

  /**
    Reference to the Continuation's lock.

    Keeps a reference to the Continuation's lock to preserve the
    access to the cancelled field valid even when the state machine
    has been deallocated. This field should not be accessed or
    modified directly by the state machine.

  */
  Ptr<ProxyMutex> mutex;

  /**
    Internal flag used to indicate whether the action has been
    cancelled.

    This flag is set after a call to cancel or cancel_action and
    it should not be accessed or modified directly by the state
    machine.

  */
  int cancelled = false;

  /**
    Cancels the asynchronous operation represented by this action.

    This method is called by state machines willing to cancel an
    ongoing asynchronous operation. Classes derived from Action may
    perform additional steps before flagging this action as cancelled.
    There are certain rules that must be followed in order to cancel
    an action (see the Remarks section).

    @param c Continuation associated with this Action.

  */
  virtual void
  cancel(Continuation *c = nullptr)
  {
    ink_assert(!c || c == continuation);
#ifdef DEBUG
    ink_assert(!cancelled);
    cancelled = true;
#else
    if (!cancelled) {
      cancelled = true;
    }
#endif
  }

  /**
    Cancels the asynchronous operation represented by this action.

    This method is called by state machines willing to cancel an
    ongoing asynchronous operation. There are certain rules that
    must be followed in order to cancel an action (see the Remarks
    section).

    @param c Continuation associated with this Action.

  */
  void
  cancel_action(Continuation *c = nullptr)
  {
    ink_assert(!c || c == continuation);
#ifdef DEBUG
    ink_assert(!cancelled);
    cancelled = true;
#else
    if (!cancelled) {
      cancelled = true;
    }
#endif
  }

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
    Constructor of the Action object. Processor implementers are
    responsible for associating this action with the proper
    Continuation.

  */
  Action() {}
  virtual ~Action() {}
};

#define ACTION_RESULT_DONE MAKE_ACTION_RESULT(1)
#define ACTION_IO_ERROR MAKE_ACTION_RESULT(2)

// Use these classes by
// #define ACTION_RESULT_HOST_DB_OFFLINE
//   MAKE_ACTION_RESULT(ACTION_RESULT_HOST_DB_BASE + 0)

#define MAKE_ACTION_RESULT(_x) (Action *)(((uintptr_t)((_x << 1) + 1)))
