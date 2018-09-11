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

#include "tscore/ink_error.h"
#include "tscore/ink_defs.h"
#include <cassert>
#include <cstdio>
#include "tscore/ink_mutex.h"

ink_mutex __global_death = PTHREAD_MUTEX_INITIALIZER;

class x_pthread_mutexattr_t
{
public:
  x_pthread_mutexattr_t()
  {
    pthread_mutexattr_init(&attr);
#ifndef POSIX_THREAD_10031c
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
#endif

#if DEBUG && HAVE_PTHREAD_MUTEXATTR_SETTYPE
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
#endif
  }

  ~x_pthread_mutexattr_t() { pthread_mutexattr_destroy(&attr); }

  pthread_mutexattr_t attr;
};

static x_pthread_mutexattr_t attr;

void
ink_mutex_init(ink_mutex *m)
{
  int error;

  error = pthread_mutex_init(m, &attr.attr);
  if (unlikely(error != 0)) {
    ink_abort("pthread_mutex_init(%p) failed: %s (%d)", m, strerror(error), error);
  }
}

void
ink_mutex_destroy(ink_mutex *m)
{
  int error;

  error = pthread_mutex_destroy(m);
  if (unlikely(error != 0)) {
    ink_abort("pthread_mutex_destroy(%p) failed: %s (%d)", m, strerror(error), error);
  }
}
