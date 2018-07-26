/** @file

  Contination.cc

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

#include "I_EventSystem.h"
#include "I_Continuation.h"
#include "I_EThread.h"

int
Continuation::handleEvent(int event, void *data)
{
  // If there is a lock, we must be holding it on entry
  ink_release_assert(!mutex || mutex->thread_holding == this_ethread());
  return (this->*handler)(event, data);
}

int
Continuation::dispatchEvent(int event, void *data)
{
  if (mutex) {
    EThread *t = this_ethread();
    MUTEX_TRY_LOCK(lock, this->mutex, t);
    if (!lock.is_locked()) {
      t->schedule_imm(this, event, data);
      return 0;
    } else {
      return (this->*handler)(event, data);
    }
  } else {
    return (this->*handler)(event, data);
  }
}
