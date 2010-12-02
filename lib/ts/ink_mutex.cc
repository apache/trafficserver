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

#include "ink_error.h"
#include <assert.h>
#include "stdio.h"
#include "ink_mutex.h"
#include "ink_unused.h"     /* MAGIC_EDITING_TAG */

x_pthread_mutexattr_t _g_mattr;

ProcessMutex __global_death = PTHREAD_MUTEX_INITIALIZER;
ProcessMutex *gobal_death_mutex = &__global_death;

void
ink_ProcessMutex_init(ProcessMutex * m, const char *name)
{
  NOWARN_UNUSED(name);
  if (pthread_mutex_init(m, &_g_mattr.attr) != 0) {
    abort();
  }
}

void
ink_ProcessMutex_destroy(ProcessMutex * m)
{
  pthread_mutex_destroy(m);
}

void
ink_ProcessMutex_acquire(ProcessMutex * m)
{
  if (pthread_mutex_lock(m) != 0) {
    abort();
  }
}

void
ink_ProcessMutex_release(ProcessMutex * m)
{
  if (pthread_mutex_unlock(m) != 0) {
    abort();
  }
}

int
ink_ProcessMutex_try_acquire(ProcessMutex * m)
{
  return pthread_mutex_trylock(m) == 0;
}

void
ink_ProcessMutex_print(FILE * out, ProcessMutex * m)
{
  (void) out;
  (void) m;
  if(m == gobal_death_mutex)
    fprintf(out, "Global ProcessMutex\n");
  else
    fprintf(out, "ProcessMutex\n");
}
