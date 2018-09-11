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

//-------------------------------------------------------------------------
// ink_rwlock_init
//
// Note: This should be called only once.
//-------------------------------------------------------------------------
int
ink_rwlock_init(ink_rwlock *rw)
{
  ink_mutex_init(&rw->rw_mutex);

  ink_cond_init(&rw->rw_condreaders);
  ink_cond_init(&rw->rw_condwriters);
  rw->rw_nwaitreaders = 0;
  rw->rw_nwaitwriters = 0;
  // coverity[missing_lock]
  rw->rw_refcount = 0;
  rw->rw_magic    = RW_MAGIC;

  return 0;
}

//-------------------------------------------------------------------------
// ink_rwlock_destroy
//-------------------------------------------------------------------------

int
ink_rwlock_destroy(ink_rwlock *rw)
{
  if (rw->rw_magic != RW_MAGIC) {
    return EINVAL;
  }
  if (rw->rw_refcount != 0 || rw->rw_nwaitreaders != 0 || rw->rw_nwaitwriters != 0) {
    return EBUSY;
  }

  ink_mutex_destroy(&rw->rw_mutex);
  ink_cond_destroy(&rw->rw_condreaders);
  ink_cond_destroy(&rw->rw_condwriters);
  rw->rw_magic = 0;

  return 0;
}

//-------------------------------------------------------------------------
// ink_rwlock_rdlock
//-------------------------------------------------------------------------

int
ink_rwlock_rdlock(ink_rwlock *rw)
{
  if (rw->rw_magic != RW_MAGIC) {
    return EINVAL;
  }

  ink_mutex_acquire(&rw->rw_mutex);

  /* give preference to waiting writers */
  while (rw->rw_refcount < 0 || rw->rw_nwaitwriters > 0) {
    rw->rw_nwaitreaders++;
    ink_cond_wait(&rw->rw_condreaders, &rw->rw_mutex);
    rw->rw_nwaitreaders--;
  }
  rw->rw_refcount++; /* another reader has a read lock */

  ink_mutex_release(&rw->rw_mutex);

  return 0;
}

//-------------------------------------------------------------------------
// ink_rwlock_wrlock
//-------------------------------------------------------------------------

int
ink_rwlock_wrlock(ink_rwlock *rw)
{
  if (rw->rw_magic != RW_MAGIC) {
    return EINVAL;
  }

  ink_mutex_acquire(&rw->rw_mutex);

  while (rw->rw_refcount != 0) {
    rw->rw_nwaitwriters++;
    ink_cond_wait(&rw->rw_condwriters, &rw->rw_mutex);
    rw->rw_nwaitwriters--;
  }
  rw->rw_refcount = -1;

  ink_mutex_release(&rw->rw_mutex);

  return 0;
}

//-------------------------------------------------------------------------
// ink_rwlock_unlock
//-------------------------------------------------------------------------

int
ink_rwlock_unlock(ink_rwlock *rw)
{
  if (rw->rw_magic != RW_MAGIC) {
    return EINVAL;
  }

  ink_mutex_acquire(&rw->rw_mutex);

  if (rw->rw_refcount > 0) {
    rw->rw_refcount--; /* releasing a reader */
  } else if (rw->rw_refcount == -1) {
    rw->rw_refcount = 0; /* releasing a reader */
  } else {
    ink_abort("invalid refcount %d on ink_rwlock %p", rw->rw_refcount, rw);
  }

  /* give preference to waiting writers over waiting readers */
  if (rw->rw_nwaitwriters > 0) {
    if (rw->rw_refcount == 0) {
      ink_cond_signal(&rw->rw_condwriters);
    }
  } else if (rw->rw_nwaitreaders > 0) {
    ink_cond_broadcast(&rw->rw_condreaders);
  }

  ink_mutex_release(&rw->rw_mutex);

  return 0;
}
