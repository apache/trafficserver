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
#if !defined (_I_AIO_h_)
#define _I_AIO_h_

#include "libts.h"
#include "I_EventSystem.h"
#include "I_RecProcess.h"

#define AIO_MODULE_MAJOR_VERSION 1
#define AIO_MODULE_MINOR_VERSION 0
#define AIO_MODULE_VERSION       makeModuleVersion(AIO_MODULE_MAJOR_VERSION,\
						   AIO_MODULE_MINOR_VERSION,\
						   PUBLIC_MODULE_HEADER)

#define AIO_EVENT_DONE           (AIO_EVENT_EVENTS_START+0)

#define AIO_MODE_AIO             0
#define AIO_MODE_SYNC            1
#define AIO_MODE_THREAD          2
#define AIO_MODE_NATIVE          3
#if use_linux_native_aio
#define AIO_MODE                 AIO_MODE_NATIVE
#else
#define AIO_MODE                 AIO_MODE_THREAD
#endif

#if AIO_MODE == AIO_MODE_NATIVE

#include <sys/syscall.h>  /* for __NR_* definitions */
#include <linux/aio_abi.h>  /* for AIO types and constants */
#define MAX_AIO_EVENTS 1024

#if defined(__LITTLE_ENDIAN)
#if (SIZEOF_VOID_POINTER == 4)
#define PADDEDPtr(x, y) x; unsigned y
#define PADDEDul(x, y) unsigned long x; unsigned y
#elif (SIZEOF_VOID_POINTER == 8)
#define PADDEDPtr(x, y) x
#define PADDEDul(x, y) unsigned long x
#endif
#elif defined(__BIG_ENDIAN)
#if (SIZEOF_VOID_POINTER == 4)
#define PADDEDPtr(x, y) unsigned y; x
#define PADDEDul(x, y) unsigned y; unsigned long y
#elif (SIZEOF_VOID_POINTER == 8)
#define PADDEDPtr(x, y) x
#define PADDEDul(x, y) unsigned long x
#endif
#else
#error edit for your odd byteorder.
#endif

typedef struct ink_iocb {
  /* these are internal to the kernel/libc. */
  PADDEDPtr(void *aio_data, _pad1); /* data to be returned in event's data */
  unsigned PADDED(aio_key, aio_reserved1);
        /* the kernel sets aio_key to the req # */

  /* common fields */
  short aio_lio_opcode; /* see IOCB_CMD_ above */
  short aio_reqprio;
  int aio_fildes;

  PADDEDPtr(void *aio_buf, _pad2);
  PADDEDul(aio_nbytes, _pad3);
  int64_t aio_offset;

  /* extra parameters */
  uint64_t aio_reserved2;  /* TODO: use this for a (struct sigevent *) */

  /* flags for the "struct iocb" */
  int aio_flags;

  /*
   * if the IOCB_FLAG_RESFD flag of "aio_flags" is set, this is an
   * eventfd to signal AIO readiness to
   */
  int aio_resfd;

} ink_aiocb_t;

typedef struct ink_io_event {
  PADDEDPtr(void *data, _pad1);   /* the data field from the iocb */
  PADDEDPtr(ink_aiocb_t *obj, _pad2);    /* what iocb this event came from */
  PADDEDul(res, _pad3);    /* result code for this event */
  PADDEDul(res2, _pad4);   /* secondary result */
} ink_io_event_t;

TS_INLINE int io_setup(unsigned nr, aio_context_t *ctxp)
{
  return syscall(__NR_io_setup, nr, ctxp);
}

TS_INLINE int io_destroy(aio_context_t ctx)
{
  return syscall(__NR_io_destroy, ctx);
}

TS_INLINE int io_submit(aio_context_t ctx, long nr,  ink_aiocb_t **iocbpp)
{
  return syscall(__NR_io_submit, ctx, nr, iocbpp);
}

TS_INLINE int io_getevents(aio_context_t ctx, long min_nr, long max_nr,
    ink_io_event_t *events, struct timespec *timeout)
{
  return syscall(__NR_io_getevents, ctx, min_nr, max_nr, events, timeout);
}

struct AIOVec: public Continuation
{
  Action action;
  int size;
  int completed;

  AIOVec(int sz, Continuation *c): Continuation(new_ProxyMutex()), size(sz), completed(0)
  {
    action = c;
    SET_HANDLER(&AIOVec::mainEvent);
  }

  int mainEvent(int event, Event *e);
};
#else
typedef ink_aiocb ink_aiocb_t;
bool ink_aio_thread_num_set(int thread_num);
#endif
// AIOCallback::thread special values
#define AIO_CALLBACK_THREAD_ANY ((EThread*)0) // any regular event thread
#define AIO_CALLBACK_THREAD_AIO ((EThread*)-1)

#define AIO_LOWEST_PRIORITY      0
#define AIO_DEFAULT_PRIORITY     AIO_LOWEST_PRIORITY

struct AIOCallback: public Continuation
{
  // set before calling aio_read/aio_write
  ink_aiocb_t aiocb;
  Action action;
  EThread *thread;
  AIOCallback *then;
  // set on return from aio_read/aio_write
  int64_t aio_result;

  int ok();
  AIOCallback() : thread(AIO_CALLBACK_THREAD_ANY), then(0) {
    aiocb.aio_reqprio = AIO_DEFAULT_PRIORITY;
  }
};

#if AIO_MODE == AIO_MODE_NATIVE
struct DiskHandler: public Continuation
{
  Event *trigger_event;
  aio_context_t ctx;
  ink_io_event_t events[MAX_AIO_EVENTS];
  Que(AIOCallback, link) ready_list;
  Que(AIOCallback, link) complete_list;
  int startAIOEvent(int event, Event *e);
  int mainAIOEvent(int event, Event *e);
  DiskHandler() {
    SET_HANDLER(&DiskHandler::startAIOEvent);
    memset(&ctx, 0, sizeof(aio_context_t));
    int ret = io_setup(MAX_AIO_EVENTS, &ctx);
    if (ret < 0) {
      perror("io_setup error");
    }
  }
};
#endif
void ink_aio_init(ModuleVersion version);
int ink_aio_start();
void ink_aio_set_callback(Continuation * error_callback);

int ink_aio_read(AIOCallback *op, int fromAPI = 0);   // fromAPI is a boolean to indicate if this is from a API call such as upload proxy feature
int ink_aio_write(AIOCallback *op, int fromAPI = 0);
int ink_aio_readv(AIOCallback *op, int fromAPI = 0);   // fromAPI is a boolean to indicate if this is from a API call such as upload proxy feature
int ink_aio_writev(AIOCallback *op, int fromAPI = 0);
AIOCallback *new_AIOCallback(void);
#endif
