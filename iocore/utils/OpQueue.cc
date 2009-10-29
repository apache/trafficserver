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


#include "I_OpQueue.h"
#include "inktomi++.h"
ClassAllocator<Callback> cbAlloc("Callback", 16);

#define CB_ALLOC(_a, _t) _a.alloc()
#define CB_FREE(_p, _a, _t) _a.free(_p)

Callback::Callback()
:calledback(false)
  , id(0)
  , event(0)
  , data(NULL)
{
}

Callback::~Callback()
{
}

int
Callback::tryCallback()
{
  // take out lock
  MUTEX_TRY_LOCK_FOR(lock, a.mutex, this_ethread(), a.continuation);
  if (!lock) {                  // need to retry
    return 1;
  }
  if (!a.cancelled) {           // if still not cancelled, ....
    a.continuation->handleEvent(event, data);
  }
  return 0;
}

OpQueue::OpQueue()
:in_progress(false)
{
}

OpQueue::~OpQueue()
{
  // free queues
}

Callback *
OpQueue::newCallback(Continuation * c)
{
  Callback *cb = CB_ALLOC(cbAlloc, this_ethread());
  cb->a = c;
  return cb;
}

void
OpQueue::freeCallback(Callback * cb)
{
  CB_FREE(cb, cbAlloc, this_ethread());
}

void
OpQueue::opIsDone(int id)
{
  // XXX: handle id != 0
  ink_assert(in_progress);
  in_progress = false;
  Callback *cb;
  Queue<Callback> unmatched;
  while ((cb = waitcompletionq.dequeue())) {
    if (!id || cb->id == id) {
      notifyq.enqueue(cb);
    } else {
      unmatched.enqueue(cb);
    }
  }
  waitcompletionq = unmatched;
}

void
OpQueue::toOpWaitQ(Callback * cb)
{
  opwaitq.enqueue(cb);
}

void
OpQueue::toWaitCompletionQ(Callback * cb)
{
  waitcompletionq.enqueue(cb);
}

int
OpQueue::processCallbacks()
{
  Callback *cb;
  int redo = 0;
  Queue<Callback> unnotified;
  while ((cb = notifyq.dequeue())) {
    if (cb->tryCallback()) {
      unnotified.enqueue(cb);
      redo = 1;
    } else {
      freeCallback(cb);
    }
  }
  notifyq = unnotified;
  return redo;
}
