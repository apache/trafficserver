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

/****************************************************************************

  Async Disk IO operations.



 ****************************************************************************/
#pragma once

#include "P_EventSystem.h"
#include "I_AIO.h"

// for debugging
// #define AIO_STATS 1

static constexpr ts::ModuleVersion AIO_MODULE_INTERNAL_VERSION{AIO_MODULE_PUBLIC_VERSION, ts::ModuleVersion::PRIVATE};

TS_INLINE int
AIOCallback::ok()
{
  return (aiocb.aio_nbytes == static_cast<size_t>(aio_result)) && (aio_result >= 0);
}

extern Continuation *aio_err_callback;

#if AIO_MODE == AIO_MODE_NATIVE

struct AIOCallbackInternal : public AIOCallback {
  int io_complete(int event, void *data);
  AIOCallbackInternal()
  {
    memset((void *)&(this->aiocb), 0, sizeof(this->aiocb));
    this->aiocb.aio_fildes = -1;
    SET_HANDLER(&AIOCallbackInternal::io_complete);
  }
};

TS_INLINE int
AIOVec::mainEvent(int /* event */, Event *)
{
  ++completed;
  if (completed < size)
    return EVENT_CONT;
  else if (completed == size) {
    SCOPED_MUTEX_LOCK(lock, action.mutex, this_ethread());
    if (!action.cancelled)
      action.continuation->handleEvent(AIO_EVENT_DONE, first);
    delete this;
    return EVENT_DONE;
  }
  ink_assert(!"AIOVec mainEvent err");
  return EVENT_ERROR;
}

#else /* AIO_MODE != AIO_MODE_NATIVE */

struct AIO_Reqs;

struct AIOCallbackInternal : public AIOCallback {
  AIO_Reqs *aio_req     = nullptr;
  ink_hrtime sleep_time = 0;
  SLINK(AIOCallbackInternal, alink); /* for AIO_Reqs::aio_temp_list */

  int io_complete(int event, void *data);

  AIOCallbackInternal() { SET_HANDLER(&AIOCallbackInternal::io_complete); }
};

struct AIO_Reqs {
  Que(AIOCallback, link) aio_todo; /* queue for AIO operations */
                                   /* Atomic list to temporarily hold the request if the
                                      lock for a particular queue cannot be acquired */
  ASLL(AIOCallbackInternal, alink) aio_temp_list;
  ink_mutex aio_mutex;
  ink_cond aio_cond;
  int index           = 0;  /* position of this struct in the aio_reqs array */
  int pending         = 0;  /* number of outstanding requests on the disk */
  int queued          = 0;  /* total number of aio_todo requests */
  int filedes         = -1; /* the file descriptor for the requests or status IO_NOT_IN_PROGRESS */
  int requests_queued = 0;
};

#endif // AIO_MODE == AIO_MODE_NATIVE

TS_INLINE int
AIOCallbackInternal::io_complete(int event, void *data)
{
  (void)event;
  (void)data;
  if (aio_err_callback && !ok()) {
    AIOCallback *err_op          = new AIOCallbackInternal();
    err_op->aiocb.aio_fildes     = this->aiocb.aio_fildes;
    err_op->aiocb.aio_lio_opcode = this->aiocb.aio_lio_opcode;
    err_op->mutex                = aio_err_callback->mutex;
    err_op->action               = aio_err_callback;

    // Take this lock in-line because we want to stop other I/O operations on this disk ASAP
    SCOPED_MUTEX_LOCK(lock, aio_err_callback->mutex, this_ethread());
    err_op->action.continuation->handleEvent(EVENT_NONE, err_op);
  }
  if (!action.cancelled) {
    action.continuation->handleEvent(AIO_EVENT_DONE, this);
  }
  return EVENT_DONE;
}

#ifdef AIO_STATS
class AIOTestData : public Continuation
{
public:
  int num_req;
  int num_temp;
  int num_queue;
  ink_hrtime start;

  int ink_aio_stats(int event, void *data);

  AIOTestData() : Continuation(new_ProxyMutex()), num_req(0), num_temp(0), num_queue(0)
  {
    start = ink_get_hrtime();
    SET_HANDLER(&AIOTestData::ink_aio_stats);
  }
};
#endif

enum aio_stat_enum {
  AIO_STAT_READ_PER_SEC,
  AIO_STAT_KB_READ_PER_SEC,
  AIO_STAT_WRITE_PER_SEC,
  AIO_STAT_KB_WRITE_PER_SEC,
  AIO_STAT_COUNT
};
extern RecRawStatBlock *aio_rsb;
