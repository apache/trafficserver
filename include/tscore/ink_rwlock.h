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

//-------------------------------------------------------------------------
// Read-Write Lock -- Code from Stevens' Unix Network Programming -
// Interprocess Communications.  This is the simple implementation and
// will not work if used in conjunction with ink_thread_cancel().
//-------------------------------------------------------------------------

#pragma once

#include "tscore/ink_mutex.h"
#include "tscore/ink_thread.h"

#define RW_MAGIC 0x19283746

struct ink_rwlock {
  ink_mutex rw_mutex;      /* basic lock on this struct */
  ink_cond rw_condreaders; /* for reader threads waiting */
  ink_cond rw_condwriters; /* for writer threads waiting */
  int rw_magic;            /* for error checking */
  int rw_nwaitreaders;     /* the number waiting */
  int rw_nwaitwriters;     /* the number waiting */
  int rw_refcount;
};

int ink_rwlock_init(ink_rwlock *rw);
int ink_rwlock_destroy(ink_rwlock *rw);
int ink_rwlock_rdlock(ink_rwlock *rw);
int ink_rwlock_wrlock(ink_rwlock *rw);
int ink_rwlock_unlock(ink_rwlock *rw);
