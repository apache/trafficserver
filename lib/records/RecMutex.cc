/** @file

  RecMutex definitions

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

#include "ts/ink_config.h"
#include "I_RecMutex.h"

int
rec_mutex_init(RecMutex *m, const char *name)
{
  m->nthread_holding = 0;
  m->thread_holding  = 0;
  return ink_mutex_init(&(m->the_mutex), name);
}

int
rec_mutex_destroy(RecMutex *m)
{
  ink_assert(m->nthread_holding == 0);
  ink_assert(m->thread_holding == 0);
  return ink_mutex_destroy(&(m->the_mutex));
}

int
rec_mutex_acquire(RecMutex *m)
{
  ink_thread this_thread = ink_thread_self();

  if (m->thread_holding != this_thread) {
    ink_mutex_acquire(&(m->the_mutex));
    m->thread_holding = this_thread;
  }

  m->nthread_holding++;
  return 0;
}

int
rec_mutex_release(RecMutex *m)
{
  if (m->nthread_holding != 0) {
    m->nthread_holding--;
    if (m->nthread_holding == 0) {
      m->thread_holding = 0;
      ink_mutex_release(&(m->the_mutex));
    }
  }
  return 0;
}
