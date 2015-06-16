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
#ifndef _P_AIO_h_
#define _P_AIO_h_

#include "P_EventSystem.h"
#include "I_AIO.h"

// for debugging
// #define AIO_STATS 1

#undef AIO_MODULE_VERSION
#define AIO_MODULE_VERSION makeModuleVersion(AIO_MODULE_MAJOR_VERSION, AIO_MODULE_MINOR_VERSION, PRIVATE_MODULE_HEADER)

TS_INLINE int
AIOCallback::ok()
{
  return (off_t)aiocb.aio_nbytes == (off_t)aio_result;
}

#if AIO_MODE == AIO_MODE_NATIVE

extern Continuation *aio_err_callbck;

struct AIOCallbackInternal : public AIOCallback {
  int io_complete(int event, void *data);
  AIOCallbackInternal()
  {
    memset((char *)&(this->aiocb), 0, sizeof(this->aiocb));
    SET_HANDLER(&AIOCallbackInternal::io_complete);
  }
};

TS_INLINE int
AIOCallbackInternal::io_complete(int event, void *data)
{
  (void)event;
  (void)data;

  if (!ok() && aio_err_callbck)
    eventProcessor.schedule_imm(aio_err_callbck, ET_CALL, AIO_EVENT_DONE);
  mutex = action.mutex;
  SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
  if (!action.cancelled)
    action.continuation->handleEvent(AIO_EVENT_DONE, this);
  return EVENT_DONE;
}

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
  AIOCallback *first;
  AIO_Reqs *aio_req;
  ink_hrtime sleep_time;
  int io_complete(int event, void *data);
  AIOCallbackInternal()
  {
    const size_t to_zero = sizeof(AIOCallbackInternal) - (size_t) & (((AIOCallbackInternal *)0)->aiocb);
    memset((char *)&(this->aiocb), 0, to_zero);
    SET_HANDLER(&AIOCallbackInternal::io_complete);
  }
};

TS_INLINE int
AIOCallbackInternal::io_complete(int event, void *data)
{
  (void)event;
  (void)data;
  if (!action.cancelled)
    action.continuation->handleEvent(AIO_EVENT_DONE, this);
  return EVENT_DONE;
}

struct AIO_Reqs {
  Que(AIOCallback, link) aio_todo;      /* queue for holding non-http requests */
  Que(AIOCallback, link) http_aio_todo; /* queue for http requests */
                                        /* Atomic list to temporarily hold the request if the
                                           lock for a particular queue cannot be acquired */
  InkAtomicList aio_temp_list;
  ink_mutex aio_mutex;
  ink_cond aio_cond;
  int index;            /* position of this struct in the aio_reqs array */
  volatile int pending; /* number of outstanding requests on the disk */
  volatile int queued;  /* total number of aio_todo and http_todo requests */
  volatile int filedes; /* the file descriptor for the requests */
  volatile int requests_queued;
};

#endif // AIO_MODE == AIO_MODE_NATIVE
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

#endif
