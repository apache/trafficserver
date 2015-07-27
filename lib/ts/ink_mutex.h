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
#include <stdio.h>

#include "ts/ink_defs.h"

#if defined(POSIX_THREAD)
#include <pthread.h>
#include <stdlib.h>

typedef pthread_mutex_t ink_mutex;

// just a wrapper so that the constructor gets executed
// before the first call to ink_mutex_init();
class x_pthread_mutexattr_t
{
public:
  pthread_mutexattr_t attr;
  x_pthread_mutexattr_t();
  ~x_pthread_mutexattr_t() {}
};
inline x_pthread_mutexattr_t::x_pthread_mutexattr_t()
{
  pthread_mutexattr_init(&attr);
#ifndef POSIX_THREAD_10031c
  pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
#endif
}

extern class x_pthread_mutexattr_t _g_mattr;

static inline int
ink_mutex_init(ink_mutex *m, const char *name)
{
  (void)name;

#if defined(solaris)
  if (pthread_mutex_init(m, NULL) != 0) {
    abort();
  }
#else
  if (pthread_mutex_init(m, &_g_mattr.attr) != 0) {
    abort();
  }
#endif
  return 0;
}

static inline int
ink_mutex_destroy(ink_mutex *m)
{
  return pthread_mutex_destroy(m);
}

static inline int
ink_mutex_acquire(ink_mutex *m)
{
  if (pthread_mutex_lock(m) != 0) {
    abort();
  }
  return 0;
}

static inline int
ink_mutex_release(ink_mutex *m)
{
  if (pthread_mutex_unlock(m) != 0) {
    abort();
  }
  return 0;
}

static inline int
ink_mutex_try_acquire(ink_mutex *m)
{
  return pthread_mutex_trylock(m) == 0;
}

#endif /* #if defined(POSIX_THREAD) */
#endif /* _ink_mutex_h_ */
