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

/**************************************************************************

  ink_thread.cc

  Generic threads interface.
**************************************************************************/

#include "ts/ink_platform.h"
#include "ts/ink_thread.h"
#include "ts/ink_atomic.h"

// // ignore the compiler warning... so that this can be used
// // in the face of changes to the Solaris header files (see "man thread")
//
// ink_mutex ink_mutex_initializer = INK_MUTEX_INIT;
//
// Put a bracketed initializer in ink_thread.h --- 9/19/96, bri
ink_mutex ink_mutex_initializer = INK_MUTEX_INIT;

#if TS_EMULATE_ANON_SEMAPHORES
static int64_t ink_semaphore_count = 0;
#endif

void
ink_sem_init(ink_semaphore *sp, unsigned int count)
{
// Darwin has sem_open, but not sem_init. We emulate sem_init with sem_open.
#if TS_EMULATE_ANON_SEMAPHORES
  char sname[NAME_MAX];

  sp->semid = ink_atomic_increment(&ink_semaphore_count, 1);
  snprintf(sname, sizeof(sname), "/trafficserver/anon/%" PRId64, sp->semid);

  ink_assert((sp->sema = sem_open(sname, O_CREAT | O_EXCL, 0770, count)) != SEM_FAILED);

  // Since we are emulating anonymous semaphores, unlink the name
  // so no other process can accidentally get it.
  ink_assert(sem_unlink(sname) != -1);
#else
  ink_assert(sem_init(sp->get(), 0 /* pshared */, count) != -1);
#endif
}

void
ink_sem_destroy(ink_semaphore *sp)
{
#if TS_EMULATE_ANON_SEMAPHORES
  ink_assert(sem_close(sp->get()) != -1);
#else
  ink_assert(sem_destroy(sp->get()) != -1);
#endif
}

void
ink_sem_wait(ink_semaphore *sp)
{
  int r;
  while (EINTR == (r = sem_wait(sp->get()))) {
    ;
  }
  ink_assert(!r);
}

bool
ink_sem_trywait(ink_semaphore *sp)
{
  int r;
  while (EINTR == (r = sem_trywait(sp->get()))) {
    ;
  }
  ink_assert(r == 0 || (errno == EAGAIN));
  return r == 0;
}

void
ink_sem_post(ink_semaphore *sp)
{
  ink_assert(sem_post(sp->get()) != -1);
}
