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

#ifndef _ink_mutex_h_
#define _ink_mutex_h_

/***********************************************************************

    Fast Mutex

    Uses atomic memory operations to minimize blocking.


***********************************************************************/

#include "ts/ink_defs.h"
#include "ts/ink_error.h"

#include <pthread.h>
#include <stdlib.h>

typedef pthread_mutex_t ink_mutex;

void ink_mutex_init(ink_mutex *m);
void ink_mutex_destroy(ink_mutex *m);

static inline void
ink_mutex_acquire(ink_mutex *m)
{
  int error = pthread_mutex_lock(m);
  if (unlikely(error != 0)) {
    ink_abort("pthread_mutex_lock(%p) failed: %s (%d)", m, strerror(error), error);
  }
}

static inline void
ink_mutex_release(ink_mutex *m)
{
  int error = pthread_mutex_unlock(m);
  if (unlikely(error != 0)) {
    ink_abort("pthread_mutex_unlock(%p) failed: %s (%d)", m, strerror(error), error);
  }
}

static inline bool
ink_mutex_try_acquire(ink_mutex *m)
{
  return pthread_mutex_trylock(m) == 0;
}

/** RAII class for locking a @c ink_mutex.

    @code
    ink_mutex m;
    // ...
    {
       ink_mutex_scoped_lock lock(m);
       // code under lock.
    }
    // code not under lock
    @endcode
 */
class ink_scoped_mutex_lock
{
private:
  ink_mutex &_m;

public:
  ink_scoped_mutex_lock(ink_mutex *m) : _m(*m) { ink_mutex_acquire(&_m); }
  ink_scoped_mutex_lock(ink_mutex &m) : _m(m) { ink_mutex_acquire(&_m); }
  ~ink_scoped_mutex_lock() { ink_mutex_release(&_m); }
};

#endif /* _ink_mutex_h_ */
