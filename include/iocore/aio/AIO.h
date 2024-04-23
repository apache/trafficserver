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
#include "iocore/eventsystem/EventSystem.h"
#include "records/RecProcess.h"

static constexpr ts::ModuleVersion AIO_MODULE_PUBLIC_VERSION(1, 0, ts::ModuleVersion::PUBLIC);

#define AIO_EVENT_DONE (AIO_EVENT_EVENTS_START + 0)

#define LIO_READ  0x1
#define LIO_WRITE 0x2

enum AIOBackend {
  AIO_BACKEND_AUTO     = 0,
  AIO_BACKEND_THREAD   = 1,
  AIO_BACKEND_IO_URING = 2,
};

struct ink_aiocb {
  int    aio_fildes = -1;      /* file descriptor or status: AIO_NOT_IN_PROGRESS */
  void  *aio_buf    = nullptr; /* buffer location */
  size_t aio_nbytes = 0;       /* length of transfer */
  off_t  aio_offset = 0;       /* file offset */

  int aio_lio_opcode = 0; /* listio operation */
  int aio_state      = 0; /* state flag for List I/O */
};

bool ink_aio_thread_num_set(int thread_num);

// AIOCallback::thread special values
#define AIO_CALLBACK_THREAD_ANY ((EThread *)0) // any regular event thread
#define AIO_CALLBACK_THREAD_AIO ((EThread *)-1)

struct AIOCallback : public Continuation {
  // set before calling aio_read/aio_write
  ink_aiocb    aiocb;
  Action       action;
  EThread     *thread = AIO_CALLBACK_THREAD_ANY;
  AIOCallback *then   = nullptr;
  // set on return from aio_read/aio_write
  int64_t aio_result = 0;

  int ok();
  AIOCallback() {}
};

void ink_aio_init(ts::ModuleVersion version, AIOBackend backend = AIO_BACKEND_AUTO);
void ink_aio_set_err_callback(Continuation *error_callback);

int          ink_aio_read(AIOCallback *op,
                          int fromAPI = 0); // fromAPI is a boolean to indicate if this is from an API call such as upload proxy feature
int          ink_aio_write(AIOCallback *op, int fromAPI = 0);
AIOCallback *new_AIOCallback();
