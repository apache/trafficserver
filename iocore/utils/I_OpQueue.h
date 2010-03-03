/** @file

  Operation queue handling

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

  Part of the utils library which contains classes that use multiple
  components of the IO-Core to implement some useful functionality. The
  classes also serve as good examples of how to use the IO-Core.

 */

#ifndef _OpQueue_H_
#define _OpQueue_H_

#include "I_EventSystem.h"

class Callback
{
public:
  Callback();
  virtual ~ Callback();
  /**
    Invoke callback for action.

    @return 0 if successful, not 0 if need retry.

  */
  int tryCallback();
  Action *action()
  {
    return &a;
  };
  Action a;
  bool calledback;

  /** Operation identifier. */
  int id;
  int event;
  void *data;
  LINK(Callback, link);
};

/**
  Operation queue handling multiple outstanding cancellable operations
  that processors can re-use.

  Constraints:
    - No locks are used except when trying to call back continuations
      via Callback. This operation queue therefore needs to be manipulated
      under a single lock, e.g. that of the processor's Continuation.

  Internal operation note:
    - all callbacks on opwaitq are waiting for the processor to become
      'idle' so that another 'operation' can be started.
    - all callbacks on waitcompletionq are waiting for the current
      operation to complete.  This is a queue rather than a single item
      for flexibility.
    - all callbacks on notifyq are to be called back because operation
      completed.

  Variations:
    - to handle multiple outstanding operations, make use of the
      op. identifier in Callback and opIsDone().

    - to handle multiple operations that all complete at same time,
      e.g. a flush() operation, use an op. identifier of '0', and move
      new Callbacks to the waitcompletionq if an operation is already
      in progress.

  Example usage:
  @code
  class MyProcessor {
    // ...
    Action *doOp(Continuation *caller);
    int handleOperationDone(int event, void *data);
    OpQueue opQ;
    Action *m_cbtimer;
  };
  Action *MyProcessor::doOp(Continuation *caller) {
    Callback *cb = opQ.newCallback(caller);
    // take reference to 'cb' so not removed just yet.
    if (opQ.in_progress) {
      opQ.toWaitQ(cb);
      return cb->action();
    } else {
      opQ.in_progress = true;
      opQ.toWaitCompletionQ(cb);
      // start operation
      if (!cb->calledback) {
        return cb->action();
      } else {
        // reentrancy -- called back from within operation
        return ACTION_RESULT_DONE;
      }
    }
  }

  int MyProcessor::handleOperationDone(int event, void *data) {
    if (event == EVENT_INTERVAL && data == m_cbtimer) {
      m_cbtimer = NULL;
      if (opQ.processCallbacks()) {
        // need to call back again
        m_cbtimer = eventProcessor.schedule_in(HRTIME_MSECONDS(10));
      }
      return EVENT_DONE;
    }

    // operation is complete
    opQ.in_progress = false;
    opQ.opIsDone();
    if (opQ.processCallbacks()) {
      // need to call back again
      m_cbtimer = eventProcessor.schedule_in(HRTIME_MSECONDS(10));
    }
    return EVENT_DONE;
  }
  @endcode

*/
class OpQueue
{
public:
  /** Constructor */
  OpQueue();

  /** Destructor */
  virtual ~ OpQueue();

  /**
    Try to call back all callbacks on notifyq.

    @return 0 if no further calls necessary (i.e. we got them all),
      else not 0 if we need to it needs to be called again because some
      Continuations weren't able to be called back because of missed
      locks or whatever.

  */
  int processCallbacks();

  /**
     Mark operation as being done. Moves all Callbacks in waitcompletionq
     to notifyq.

     @param id if id is 0, then all operations in waitcompletionq are
       marked as done (moved to notifyq). If not 0, then only selected
       operation is moved from waitcompletionq to notifyq.

  */
  void opIsDone(int id = 0);

  /**
    Create a new Callback for Continuation.  This starts on no queue at
    all. (Internal use)

  */
  Callback *newCallback(Continuation *);

  /** Put callback on opwaitq. */
  void toOpWaitQ(Callback * cb);

  /** Put callback on waitcompletionq. */
  void toWaitCompletionQ(Callback * cb);

  /** Is operation in progress. */
  bool in_progress;

private:

  /** Get rid of callback. (Internal use) */
  void freeCallback(Callback *);

    Queue<Callback> opwaitq;
    Queue<Callback> waitcompletionq;
    Queue<Callback> notifyq;
};
#endif
