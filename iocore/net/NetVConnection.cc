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

  NetVConnection.cc

  This file implements an I/O Processor for network I/O.


 ****************************************************************************/

#include "P_Net.h"

Action *
NetVConnection::send_OOB(Continuation *, char *, int)
{
  return ACTION_RESULT_DONE;
}

void
NetVConnection::cancel_OOB()
{
  return;
}

int
NetVConnection::Close_callback::try_close(int, Event *) {
  // Mutex for this continuation is the original VC mutex so if we're
  // being dispatched, it should be locked enough to check.
  if (_vcptr.isValid()) {
    _vcptr->do_io_close(_lerrno);
  }
  delete this;
  return EVENT_DONE;
}

void
NetVConnection::Handle::do_locked_io_close(int lerrno) {
  EThread* t = this_ethread();
  // If the VC or the mutex isn't there, we're done.
  bool handled = ! (_vc && _mutex.m_ptr);
  /* This loop is an attempt to avoid race conditions.  We need to
     shut down the VC without colliding with another thread.

     First we try to get the lock. If that works then if our
     reference is still valid we can do the shutdown directly. If
     it's not valid we assume that someone else already did the
     shutdown (such a collision being precisely what we're trying to
     avoid).

     If we can't get the lock, we need to schedule a retry on the
     thread that owns the lock. However, we're racing at this because
     without the lock the VC or the lock itself can change. In
     particular, the thread owning the lock can drop it and leave us
     without a valid thread. So we grab what we can and check it. If
     we have a thread, we schedule there. Otherwise the lock has been
     dropped so we cycle back to trying the lock.

     We presume that there's extremely little chance of this just
     missing the lock happening and an infinitesmal chance of it
     happening more than once. So the loop shouldn't go more than once
     or twice and then only vary rarely. Further, based on experience
     in a high throughput environment we should expect to not get the
     lock in the first place only in rare circumstances. For most
     situations and in most environments we should not ever do the
     scheduling. Unfortunately it does happen under some circumstances
     so we have to deal with it.
 */
  while (!handled) {
    MUTEX_TRY_LOCK(lock, _mutex, t);
    if (lock) {
      if (this->isValid()) _vc->do_io_close(lerrno);
      handled = true;
    } else {
      // Gosh, it's probable that the VC has gone bad at this point but
      // we can't reliably detect that without the lock.
      EThread* ot = _mutex->thread_holding;
      ink_assert(ot != t); // we should have the lock in that case.
      if (ot) {
        // schedule shutdown on other thread.
        ot->schedule_imm(NEW(new UnixNetVConnection::Close_callback(*this, lerrno)));
        handled = true;
      }
    }
  }
}
