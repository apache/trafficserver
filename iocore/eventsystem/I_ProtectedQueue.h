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

/****************************************************************************

  Protected Queue, a FIFO queue with the following functionality:
  (1). Multiple threads could be simultaneously trying to enqueue
       and dequeue. Hence the queue needs to be protected with mutex.
  (2). In case the queue is empty, dequeue() sleeps for a specified
       amount of time, or until a new element is inserted, whichever
       is earlier


 ****************************************************************************/
#pragma once

#include "ts/ink_platform.h"
#include "I_Event.h"
struct ProtectedQueue {
  void enqueue(Event *e, bool fast_signal = false);
  void signal();
  int try_signal(); // Use non blocking lock and if acquired, signal
  void remove(Event *e);

  /// Add an event to the thread local queue.
  /// @note Must be called from the owner thread.
  void enqueue_local(Event *e);

  /// Get an event from the thread local queue.
  /// @note Must be called from the owner thread.
  Event *dequeue_local();

  /// Attempt to dequeue, waiting for @a timeout if there's no data.
  void dequeue_timed(ink_hrtime cur_time, ink_hrtime timeout, bool sleep);

  /// Dequeue any external events.
  void dequeue_external();

  /// Wait for @a timeout nanoseconds on a condition variable if there are no events.
  void wait(ink_hrtime timeout);
  /// Events added from other threads.
  InkAtomicList al;

  /// Lock for condition variable
  /// pthread_cond_wait requires we lock this mutex before calling and it's used for
  /// the signal logic to avoid race conditions.
  ink_mutex lock;

  /// Condition variable for timed wait.
  ink_cond might_have_data;

  /** This is a queue for events scheduled from the same thread. The @c _local methods use this
      queue and should never be called from another thread.
  */
  Que(Event, link) localQueue;

  ProtectedQueue();
};

void flush_signals(EThread *t);
