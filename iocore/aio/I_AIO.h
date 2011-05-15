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

#ifndef TS_INLINE
#define TS_INLINE
#endif

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
#define AIO_MODE                 AIO_MODE_THREAD

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

void ink_aio_init(ModuleVersion version);
int ink_aio_start();
void ink_aio_set_callback(Continuation * error_callback);

int ink_aio_read(AIOCallback *op, int fromAPI = 0);   // fromAPI is a boolean to indicate if this is from a API call such as upload proxy feature
int ink_aio_write(AIOCallback *op, int fromAPI = 0);
bool ink_aio_thread_num_set(int thread_num);
AIOCallback *new_AIOCallback(void);
#endif
