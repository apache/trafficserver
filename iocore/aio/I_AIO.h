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

#include "tscore/ink_platform.h"
#include "I_EventSystem.h"
#include "records/I_RecProcess.h"

static constexpr ts::ModuleVersion AIO_MODULE_PUBLIC_VERSION(1, 0, ts::ModuleVersion::PUBLIC);

#define AIO_EVENT_DONE (AIO_EVENT_EVENTS_START + 0)

#define AIO_MODE_THREAD 0
#define AIO_MODE_NATIVE 1

#if TS_USE_LINUX_NATIVE_AIO
#define AIO_MODE AIO_MODE_NATIVE
#else
#define AIO_MODE AIO_MODE_THREAD
#endif

#define LIO_READ 0x1
#define LIO_WRITE 0x2

#if AIO_MODE == AIO_MODE_NATIVE

#include <libaio.h>

#define MAX_AIO_EVENTS 1024

typedef struct iocb ink_aiocb;
typedef struct io_event ink_io_event_t;

// XXX hokey old-school compatibility with ink_aiocb.h ...
#define aio_nbytes u.c.nbytes
#define aio_offset u.c.offset
#define aio_buf u.c.buf

#else

struct ink_aiocb {
  int aio_fildes    = -1;      /* file descriptor or status: AIO_NOT_IN_PROGRESS */
  void *aio_buf     = nullptr; /* buffer location */
  size_t aio_nbytes = 0;       /* length of transfer */
  off_t aio_offset  = 0;       /* file offset */

  int aio_lio_opcode = 0; /* listio operation */
  int aio_state      = 0; /* state flag for List I/O */
  int aio__pad[1];        /* extension padding */
};

bool ink_aio_thread_num_set(int thread_num);

#endif

// AIOCallback::thread special values
#define AIO_CALLBACK_THREAD_ANY ((EThread *)0) // any regular event thread
#define AIO_CALLBACK_THREAD_AIO ((EThread *)-1)

struct AIOCallback : public Continuation {
  // set before calling aio_read/aio_write
  ink_aiocb aiocb;
  Action action;
  EThread *thread   = AIO_CALLBACK_THREAD_ANY;
  AIOCallback *then = nullptr;
  // set on return from aio_read/aio_write
  int64_t aio_result = 0;

  int ok();
  AIOCallback() {}
};

#if AIO_MODE == AIO_MODE_NATIVE

struct AIOVec : public Continuation {
  Action action;
  int size;
  int completed;
  AIOCallback *first;

  AIOVec(int sz, AIOCallback *c) : Continuation(new_ProxyMutex()), size(sz), completed(0), first(c)
  {
    action = c->action;
    SET_HANDLER(&AIOVec::mainEvent);
  }

  int mainEvent(int event, Event *e);
};

struct DiskHandler : public Continuation {
  Event *trigger_event;
  io_context_t ctx;
  ink_io_event_t events[MAX_AIO_EVENTS];
  Que(AIOCallback, link) ready_list;
  Que(AIOCallback, link) complete_list;
  int startAIOEvent(int event, Event *e);
  int mainAIOEvent(int event, Event *e);
  DiskHandler()
  {
    SET_HANDLER(&DiskHandler::startAIOEvent);
    memset(&ctx, 0, sizeof(ctx));
    int ret = io_setup(MAX_AIO_EVENTS, &ctx);
    if (ret < 0) {
      Debug("aio", "io_setup error: %s (%d)", strerror(-ret), -ret);
    }
  }
};
#endif

void ink_aio_init(ts::ModuleVersion version);
int ink_aio_start();
void ink_aio_set_err_callback(Continuation *error_callback);

int ink_aio_read(AIOCallback *op,
                 int fromAPI = 0); // fromAPI is a boolean to indicate if this is from a API call such as upload proxy feature
int ink_aio_write(AIOCallback *op, int fromAPI = 0);
int ink_aio_readv(AIOCallback *op,
                  int fromAPI = 0); // fromAPI is a boolean to indicate if this is from a API call such as upload proxy feature
int ink_aio_writev(AIOCallback *op, int fromAPI = 0);
AIOCallback *new_AIOCallback();
