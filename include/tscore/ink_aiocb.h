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

  Async Struct definition.



 ****************************************************************************/

#pragma once

#include "tscore/ink_defs.h"

/* TODO use native aiocb where possible */

#define LIO_READ 0x1
#define LIO_WRITE 0x2

struct ink_aiocb {
  int aio_fildes;
#if defined(__STDC__)
  void *aio_buf; /* buffer location */
#else
  void *aio_buf; /* buffer location */
#endif
  size_t aio_nbytes; /* length of transfer */
  off_t aio_offset;  /* file offset */

  int aio_lio_opcode; /* listio operation */
  int aio_state;      /* state flag for List I/O */
  int aio__pad[1];    /* extension padding */
};
