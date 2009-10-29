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

#ifndef _KCALLS_H_
#define _KCALLS_H_
#include <linux/inkaio.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <pthread.h>
#include <string.h>
#define GKM
#ifdef __cplusplus
extern "C"
{
#endif

#define inkaio_filno(kcb)	((kcb)->fd)
#define inkaio_empty(kcb)	((kcb)->outptr == (kcb)->outbuf)

  extern int libinkaio_mmap;

#define INKAIO_MIN_MMAP_SIZE	(128*1024)

  typedef struct _inkaiocb
  {
    int fd;
    int shared, serial;
    aio_mem_t *done;
    char *outbuf, *outptr, *eoob;
    int size, maxsize, readsize;
#ifdef GKM
    int aioread, aiowrite, aioread_done, aiowrite_done;
    int events_len, events_len_done;
#endif
    pthread_mutex_t mutex;
    int (*callback) (kcall_t *);
  } INKAIOCB;

/* if you pass maxbufsiz == -1, you will get shared memory with the kernel */
  extern INKAIOCB *inkaio_create(int maxbufsiz, int (*callback) (kcall_t *));
  extern int inkaio_destroy(INKAIOCB * kcb);
  extern int inkaio_execute(INKAIOCB * kcb);
  extern int inkaio_returns(INKAIOCB * kcb);
  extern int inkaio_dispatch(INKAIOCB * kcb);
  extern int inkaio_submit(INKAIOCB * kcb);     /* mmap shadow of inkaio_execute() */
  extern int inkaio_results(INKAIOCB * kcb);    /* mmap shadow of inkaio_returns() */
  extern int __inkaio_space(INKAIOCB * kcb, kcall_t ** k, void **data, int len);
  extern int inkaio_aioread(INKAIOCB * kcb, void *cookie, int fd, void *buf, size_t count, loff_t offset);
  extern int inkaio_aiowrite(INKAIOCB * kcb, void *cookie, int fd, void *buf, size_t count, loff_t offset);
#ifdef __cplusplus
};                              /* extern C */
#endif
#endif
