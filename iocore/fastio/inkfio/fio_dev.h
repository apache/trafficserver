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

#ifndef FIO_DEV_H
#define FIO_DEV_H

#include "fastio.h"

/* Maximum simultaneous vsessions */
#define MAX_VSESSION 1024
#define MAX_SESSION 2048

struct free_arg;

/*
 * The entire state of each fio device.
 */
typedef struct
{
  void *ram;                    /*0 *//* the memory we use */
  int32_t ramsize;              /*0x4 *//* how much is there */
  ddi_umem_cookie_t cookie;     /*ox8 *//* cookie from ddi_umem_alloc */
  dev_info_t *dip;              /*oxc *//* my devinfo handle */
  queue_t *udp_queue;           /*ox10 */


  int *bufbaseptr;
  int32_t active;
  int32_t blkcount;
  int32_t blocksize;            /* FIXME: pass this through shared memory */
  uint32_t *flist0, *flist1, *activefl;
  intptr_t blockbaseptr;
  int32_t nextflentry;
  kmutex_t freemx;
  kmutex_t modopenmx;
  kmutex_t reqmx;
  int32_t modopen;

  struct free_rtn *free_struct;
  struct free_arg *free_arg;

  struct pending_request *pRequests;

  int32_t timeout_duration;
  timeout_id_t timeout_id;


  /* struct ink_redirect_list *vsession[MAX_VSESSION]; */

  int8_t vsession_alloc[MAX_VSESSION];
  int32_t vsession_count;

  int32_t stat_timeout_count;
  int32_t signal_user;
  void *signal_ref;

  /* Protect the session q's when modules get loaded/unloaded */
  kmutex_t session_mutex[MAX_SESSION];
  /* keep track of session queues */
  queue_t *session[MAX_SESSION];
  int32_t session_count;

  struct ink_fio_stats stats;
} fio_devstate_t;



struct free_arg
{
  uint32_t blockID;
  fio_devstate_t *rsp;
  void (*db_lastfree) (struct msgb *, struct datab *);
  void (*db_free) (struct msgb *, struct datab *);
};


struct pending_request
{
  uint32_t requestBlock;        /*0x0 */
  uint16_t pktsRemaining;       /* 0x2 */
  uint16_t elapsedDelay;        /* 0x4 */

  struct fastIO_request *req;   /* 0x8 */
  struct fastIO_pkt *nextPkt;   /* 0xc */

  mblk_t *dst_mblk;             /* 0x10 */

  struct pending_request *next; /* 0x14 */
  struct pending_request *prev; /* 0x18 */

  int32_t destQIdx;             /* An index into the session table for destQ */
  queue_t *destQ;               /* store the queue ptr here not in user-writable memory! */
};


/*
/*
 * STREAMS message format for sending a UDP packet
 */

struct udppkt
{
  char hdr[22];                 /* 0x08000000, 10000000, 14000000 */
  /* 0x00000000, 00000000, 0200  */
  uint16_t port;                /* set to destination UDP port # */
  int32_t ip;
  char ftr[8];                  /* 0x35410000, 0x00000000 */
};


struct inkio_blockdat
{
  struct free_rtn *freecb;      /* so it can get freed */
  uint32_t blockID;
};


int fio_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t * cred_p, int *rval_p);

int fio_valid_request(fio_devstate_t * rsp, struct fastIO_request *req);
void fio_free_request_blks(fio_devstate_t * rsp, struct fastIO_request *req);

void fio_free_cb(char *dat);
void *getBlockPtr(fio_devstate_t * rsp, uint32_t id);

int fio_vsession_ioctl(fio_devstate_t * rsp, int cmd, intptr_t arg);

queue_t *fio_lookup_queue(int qid);
int fio_acquire_queue(int qid, queue_t * q);
void fio_release_queue(int qid);

extern void *fio_state;
#endif
