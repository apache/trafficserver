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

#include <tscore/ink_error.h>
#include <tscore/ink_mutex.h>
#include <tscore/Diags.h>
#include <tscpp/util/Strerror.h>

// This is in a separate module from ink_mutex because some TS tools don't need this function, and also do not link in
// the Diags objects.  So putting this in ink_mutex causes a link error for these tools for the Warning() call.

void
ink_mutex_safer_destroy(ink_mutex *m)
{
  int error;

  error = pthread_mutex_trylock(m);
  if (unlikely(error != 0)) {
    if (error != EBUSY) {
      ink_abort("pthread_mutex_trylock(%p) failed: %s (%d)", m, ts::Strerror(error).c_str(), error);
    } else {
#if DEBUG
      ink_abort("destroying mutex (%p) that is still locked", m);
#else
      Warning("ink_mutex_safer_destroy: destroying mutex (%p) that is still locked", m);
#endif

      timespec timeout;
      timeout.tv_sec  = 10;
      timeout.tv_nsec = 0;
      error           = pthread_mutex_timedlock(m, &timeout);
      if (error != 0) {
        ink_abort("pthread_mutex_trylock(%p) failed: %s (%d)", m, ts::Strerror(error).c_str(), error);
      }
    }
  }
  ink_mutex_release(m);

  error = pthread_mutex_destroy(m);
  if (unlikely(error != 0)) {
    ink_abort("pthread_mutex_destroy(%p) failed: %s (%d)", m, ts::Strerror(error).c_str(), error);
  }
}
