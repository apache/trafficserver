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

#include "tscore/ink_config.h"
#include "tscore/ink_rwlock.h"

static pthread_rwlockattr_t attr;

//-------------------------------------------------------------------------
// ink_rwlock_init
//
// Note: This should be called only once.
//-------------------------------------------------------------------------
void
ink_rwlock_init(ink_rwlock *rw)
{
  int error = pthread_rwlock_init(rw, &attr);
  if (unlikely(error != 0)) {
    ink_abort("pthread_rwlock_init(%p) failed: %s (%d)", rw, strerror(error), error);
  }
}

//-------------------------------------------------------------------------
// ink_rwlock_destroy
//-------------------------------------------------------------------------
void
ink_rwlock_destroy(ink_rwlock *rw)
{
  int error = pthread_rwlock_destroy(rw);
  if (unlikely(error != 0)) {
    ink_abort("pthread_rwlock_destroy(%p) failed: %s (%d)", rw, strerror(error), error);
  }
}
