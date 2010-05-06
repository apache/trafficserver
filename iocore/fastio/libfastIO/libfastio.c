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

/*
 *
 * FastIO Userland Library 
 * 
 *
 *
 *
 *
 * Stub routines for interacting with FastIO services
 *
 */

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stropts.h>
#include <sys/conf.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <thread.h>
#include <synch.h>

#include "fastio.h"
#include "libfastio.h"

#define MAX_FASTIO_BLOCKS 512

int waitcount = 0;
uint8_t recycledblks[MAX_FASTIO_BLOCKS];
uint8_t recycledblks_temp[MAX_FASTIO_BLOCKS];

/*
 * swap the freelists, sleeping if we didn't get any memory out of it
 */
int
fastIO_swap(struct fastIO_state *fio, int noblock)
{
  int i = 0;
  time_t thetime;
  uint32_t *tblks;
  uint32_t tblks_size;
  uint32_t newlist_1st_empty;

  printf("Fastio: Calling swap\n");

  ioctl(fio->fiofd, FIO_SWAP, 0);
  fio->active = !fio->active;
  if (fio->active)
    fio->activefl = fio->flist1;
  else
    fio->activefl = fio->flist0;

  fio->nextflentry = 0;
  time(&thetime);

  {
    int j, nfree = 0;

    for (j = 0; j < fio->blockcount; j++)
      if (fio->activefl[j] != 0xffffffff) {
        nfree++;
      }
    printf("\nnfree = %d\n", nfree);

  }

  /* Don't block if we failed and noblock is set */
  if (fio->activefl[0] == 0xffffffff && noblock) {

    return 0;
  }


  while (fio->activefl[0] == 0xffffffff) {

    waitcount++;
#ifdef DEBUG
    if (waitcount % 50 == 0)
      printf("%s: waitcount:%d\n", ctime(&thetime), waitcount);
#endif

    usleep(20);

    ioctl(fio->fiofd, FIO_SWAP, 0);
    fio->active = !fio->active;
    if (fio->active)
      fio->activefl = fio->flist1;
    else
      fio->activefl = fio->flist0;
    fio->nextflentry = 0;

  }
  printf("Fastio: Swap succeeded \n");

  /*success */
  return 1;
}


/*
 *  Setup the freelists
 */
void
fastIO_init_freelists(struct fastIO_state *fio)
{

  int i;
  fio->nextflentry = 0;
  /* also initialize the block info structures */
  fio->blocks = (struct fastIO_block *) malloc(sizeof(struct fastIO_block) * fio->blockcount);

  for (i = 0; i < fio->blockcount; i++) {
    fio->flist0[i] = i;         /* the ith free block is i */
    fio->flist1[i] = 0xffffffff;        /* NOT FREE */
    fio->blocks[i].ptr = (void *) (fio->blocksize * i + fio->blockbase);
    fio->blocks[i].id = i;
  }



}


/*
 *  Allocate the kernel memory buffer
 */
int
fastIO_fio_init(struct fastIO_state *fio)
{

  /* allocate kernel memory for the buffer */

  fio->fiofd = open(FIO_DEV, O_RDWR, "rw");
  if (fio->fiofd < 0) {
    return 0;
  }


  fio->buffer = (int *) mmap((caddr_t) 0, fio->size, (PROT_READ | PROT_WRITE), MAP_SHARED, fio->fiofd, 0);


  if ((int) fio->buffer == 0) {
#ifdef DEBUG
    printf("fio->size: %d, fio->fiofd: %d\n", fio->size, fio->fiofd);
    perror("mmap");
#endif
    free(fio);
    return 0;
  }


  ioctl(fio->fiofd, FIO_INIT, fio->blockcount);

  return 1;

}



/*
 * Initialize the fastIO system for a file descriptor 
 */
struct fastIO_state *
fastIO_init(int blockcount)
{

  struct fastIO_state *fio;
  int kptr;
  int queue_ptr;


  fio = (struct fastIO_state *) malloc(sizeof(struct fastIO_state));
  if (!fio) {
#ifdef DEBUG
    printf("fastIO_init: unable to allocate state cookie.\n");
#endif
    return 0;
  }

  bzero(fio, sizeof(struct fastIO_state));

  fio->blockcount = blockcount;
  fio->blocksize = FASTIO_BLOCK_SIZE;
  fio->size = (fio->blocksize + 2 * (sizeof(uint32_t))) * fio->blockcount;


  if (!fastIO_fio_init(fio)) {
    free(fio);
    return 0;
  }

  /* initialize the memory space */
  fio->flist0 = (uint32_t *) fio->buffer;
  fio->flist1 = (uint32_t *) fio->buffer + (fio->blockcount);
  fio->activefl = fio->flist0;
  fio->active = 0;
  fio->blockbase = (int) (fio->flist1 + (fio->blockcount));

  fastIO_init_freelists(fio);


  /* you don't need to call mutex_init() on an intraprocess mutex.
   * just make sure it's zero'ed out 
   */
  bzero(&fio->mem_mutex, sizeof(mutex_t));

  return fio;
}



/*
 * Create a fastIO session 
 */
struct fastIO_session *
fastIO_udpsession_create(struct fastIO_state *fio, int fd)
{


  struct fastIO_session *fs;

  fs = (struct fastIO_session *) malloc(sizeof(struct fastIO_session));
  if (!fs) {
    printf("fastIO_session_create: Out of memory.\n");
    return 0;
  }

  /* set fastIO_state pointer for the session */
  fs->fio = fio;


  /* load the fastIO module onto the UDP stream */
  if (ioctl(fd, I_PUSH, "inkudp") < 0) {
    /* perror("ioctl I_PUSH inkio failed"); */
    free(fs);
    return 0;
  }

  /* get a pointer to the queue */
  fs->udp_queue = (int) ioctl(fd, INKUDP_GETQ);

  /* set the type */
  fs->type = FASTIO_SESSION_UDP;

  fs->fd = fd;


  return fs;

}


/*
 * Create a fastIO virtual session 
 */
struct fastIO_session *
fastIO_vsession_create(struct fastIO_state *fio)
{


  struct fastIO_session *fs;

  fs = (struct fastIO_session *) malloc(sizeof(struct fastIO_session));
  if (!fs) {
    printf("fastIO_session_create: Out of memory.\n");
    return 0;
  }

  /* set fastIO_state pointer for the session */
  fs->fio = fio;
  fs->type = FASTIO_SESSION_VIRTUAL;

  /* allocate a vsession */
  fs->vsession_id = ioctl(fio->fiofd, INKFIO_VSESSION_CREATE);
  if (fs->vsession_id == -1) {
    printf("fastIO_vsession_create: Unable to create vsession.\n");
    free(fs);
    return 0;                   /* failure */
  }

  printf("fastIO_vsession_create: Created vsession id %d.\n", fs->vsession_id);
  return fs;

}


/* 
 * Delete a fastIO Session 
 */
void
fastIO_session_destroy(struct fastIO_session *sessioncookie)
{
  struct strioctl strioctl;



  switch (sessioncookie->type) {

  case FASTIO_SESSION_UDP:
    if (ioctl(sessioncookie->fd, I_POP, "inkudp") < 0) {
      perror("ioctl I_POP inkio failed");
      break;
    }

    break;
  case FASTIO_SESSION_VIRTUAL:
    ioctl(sessioncookie->fio->fiofd, INKFIO_VSESSION_DESTROY, sessioncookie->vsession_id);
    break;
  }


  free(sessioncookie);

}





/*
 * Allocate blocks
 */
int
fastIO_balloc(struct fastIO_state *fio, int blockCount, struct fastIO_block **blocks, int flags)
{

  uint32_t blockid;
  int i, j;


  /* can't have more than one guy messing with the freelists at once! */
  mutex_lock(&fio->mem_mutex);

/*  printf("Blocks: ");*/
  for (i = 0; i < blockCount; i++) {
    while (fio->nextflentry >= fio->blockcount || fio->activefl[fio->nextflentry] == 0xffffffff) {
      if (!fastIO_swap(fio, flags & FASTIO_BALLOC_NO_BLOCK)) {
        /* fixme: return allocated blocks to freelist!. */
        mutex_unlock(&fio->mem_mutex);
        return 0;
      }
    }
    blocks[i] = &fio->blocks[fio->activefl[fio->nextflentry]];

    /* printf("Alloc'ed block: %d\n", blocks[i]->id);  */

    fio->activefl[fio->nextflentry] = 0xffffffff;
    fio->nextflentry++;

  }
/* printf("\n");*/
  mutex_unlock(&fio->mem_mutex);

  return 1;                     /* success */

}


/*
 * Send UDP data
 */
int
fastIO_sendto(struct fastIO_session *fio, uint32_t requestBlock)
{

  int ret;
  struct fastIO_request *req;
  req = (struct fastIO_request *) fio->fio->blocks[requestBlock].ptr;
  if (requestBlock >= fio->fio->blockcount) {
#ifdef DEBUG
    printf("fastio_sendto: bad requestBlock choice.\n");
#endif
    return -1;
  }

/*    printf("fastIO_send: Req block %d.\n", requestBlock);*/

  switch (fio->type) {

  case FASTIO_SESSION_UDP:
/*	printf("sending on destination queue 0x%x.\n", 
		fio->udp_queue);*/

    req->destQ = fio->udp_queue;
    ret = ioctl(fio->fio->fiofd, FIO_SENDTO, (int) requestBlock);

    if (ret == INKUDP_SUCCESS)
      return 0;
    else {
      printf("fastIO_sendto: Unknown error code %d.\n", ret);
      return 0;
    }

  case FASTIO_SESSION_VIRTUAL:
    printf("Sending on vsession...\n");
    req->destQ = fio->vsession_id;
    req->destIP = INKFIO_DEST_VSESSION;
    ret = ioctl(fio->fio->fiofd, FIO_SENDTO, (int) requestBlock);

  }



}

/*
 *  Setup a request to be included as part of a multiple-block request
 *  Must be called on a request block before that request block is
 *  setn with fastIO_metasend()
 *
 */
int
fastIO_metarequest_setup(struct fastIO_session *fio, uint32_t requestBlock)
{
  int ret;
  struct fastIO_request *req;
  req = (struct fastIO_request *) fio->fio->blocks[requestBlock].ptr;
  if (requestBlock >= fio->fio->blockcount) {
#ifdef DEBUG
    printf("fastio_sendto: bad requestBlock choice.\n");
#endif
    return -1;
  }



  switch (fio->type) {

  case FASTIO_SESSION_UDP:
    req->destQ = fio->udp_queue;
    break;
  case FASTIO_SESSION_VIRTUAL:
    req->destQ = fio->vsession_id;
    req->destIP = INKFIO_DEST_VSESSION;
    break;
  }

  return 0;                     /*success */

}

/*
 * fastIO_metarequest_send
 * Sends a metarequest
 *
 */
int
fastIO_metarequest_send(struct fastIO_state *fio, uint32_t requestBlock)
{
  int ret;
  /*printf("fastIO_metarequest_send: Sending requests in %d.\n", requestBlock); */

  ret = ioctl(fio->fiofd, FIO_METASEND, (int) requestBlock);
  return ret;

}

/*
 * Add a split rule
 */
void
fastIO_add_split_rule(struct fastIO_session *srcSession, struct fastIO_split_rule *rule)
{
  struct strioctl strioctl;

  struct ink_cmd_msg msg;


  msg.cmd = INK_CMD_SPLIT_ADD;
  rule->dst_queue = (queue_t *) rule->splitTo->udp_queue;
  memcpy(&(msg.payload.split_rule), rule, sizeof(struct fastIO_split_rule));


  switch (srcSession->type) {
  case FASTIO_SESSION_UDP:

    strioctl.ic_cmd = INK_CMD_SPLIT_ADD;
    strioctl.ic_timout = 15;
    strioctl.ic_len = sizeof(struct ink_cmd_msg);
    strioctl.ic_dp = (char *) &msg;


    if (ioctl(srcSession->fd, I_STR, &strioctl) == -1)
      perror("ioctl");
    break;
  case FASTIO_SESSION_VIRTUAL:

    msg.id = srcSession->vsession_id;
    if (ioctl(srcSession->fio->fiofd, INKFIO_VSESSION_CMD, &msg) == -1)
      perror("ioctl");
    break;
  }




}

/*
 * Remove a split rule
 */
void
fastIO_delete_split_rule(struct fastIO_session *srcSession, struct fastIO_split_rule *rule)
{
  struct strioctl strioctl;

  struct ink_cmd_msg msg;

#ifdef DEBUG
  printf("fastIO_delete_split_rule\n");
#endif

  msg.cmd = INK_CMD_SPLIT_DELETE;
  rule->dst_queue = (queue_t *) rule->splitTo->udp_queue;
  memcpy(&msg.payload.split_rule, rule, sizeof(struct fastIO_split_rule));




  switch (srcSession->type) {
  case FASTIO_SESSION_UDP:

    strioctl.ic_cmd = INK_CMD_SPLIT_DELETE;
    strioctl.ic_timout = 15;
    strioctl.ic_len = sizeof(struct ink_cmd_msg);
    strioctl.ic_dp = (char *) &msg;


    if (ioctl(srcSession->fd, I_STR, &strioctl) == -1)
      perror("ioctl");
    break;
  case FASTIO_SESSION_VIRTUAL:

    msg.id = srcSession->vsession_id;
    if (ioctl(srcSession->fio->fiofd, INKFIO_VSESSION_CMD, &msg) == -1)
      perror("ioctl");
    break;
  }



}

/*
 * Delete all redirections specified for the split rule.
 */
void
fastIO_flush_split_rules(struct fastIO_session *srcSession, struct fastIO_split_rule *rule)
{


  struct strioctl strioctl;

  struct ink_cmd_msg msg;
#ifdef DEBUG
  printf("fastIO_flush_split_rules\n");
#endif




  msg.cmd = INK_CMD_SPLIT_FLUSH;
  memcpy(&msg.payload.split_rule, rule, sizeof(struct fastIO_split_rule));


  switch (srcSession->type) {
  case FASTIO_SESSION_UDP:

    strioctl.ic_cmd = INK_CMD_SPLIT_FLUSH;
    strioctl.ic_timout = 15;
    strioctl.ic_len = sizeof(struct ink_cmd_msg);
    strioctl.ic_dp = (char *) &msg;


    if (ioctl(srcSession->fd, I_STR, &strioctl) == -1)
      perror("ioctl");
    break;
  case FASTIO_SESSION_VIRTUAL:

    msg.id = srcSession->vsession_id;
    if (ioctl(srcSession->fio->fiofd, INKFIO_VSESSION_CMD, &msg) == -1)
      perror("ioctl");
    break;
  }

}

void
fastIO_get_bytes_stats(struct fastIO_session *srcSession, uint32_t * nbytesSent)
{
  struct strioctl strioctl;
  struct ink_cmd_msg msg;
  int i;

  msg.cmd = INK_CMD_GET_BYTES_STATS;
  memset(msg.payload.nbytesSent, 0, FASTIO_MAX_FLOWS * sizeof(uint32_t));

  strioctl.ic_cmd = INK_CMD_GET_BYTES_STATS;
  strioctl.ic_timout = 15;
  strioctl.ic_len = sizeof(struct ink_cmd_msg);
  strioctl.ic_dp = (char *) &msg;

  if (ioctl(srcSession->fd, I_STR, &strioctl) == -1)
    perror("ioctl");

  memcpy(nbytesSent, msg.payload.nbytesSent, FASTIO_MAX_FLOWS * sizeof(uint32_t));
  for (i = 0; i < FASTIO_MAX_FLOWS; i++)
    if (msg.payload.nbytesSent[i])
      printf("Flow = %d, bytes = %d\n", i, msg.payload.nbytesSent[i]);
}

/*
 * Cleanup
 */
void
fastIO_fini(struct fastIO_state *cookie)
{


#ifdef DEBUG
  printf("fastIO_fini:\n");
#endif

  /* This doesn't seem to result in a devunmap handler inside inkfio, 
   * so I clean up memory in DDI close()
   */
  munmap((char *) cookie->buffer, cookie->size);
  close(cookie->fiofd);

  free(cookie);
}

/*
 * Gather and display statistics
 *
 */
void
fastIO_print_stats(struct fastIO_state *cookie)
{

  int timeout_count;

  timeout_count = ioctl(cookie->fiofd, FIO_GET_TIME_STAT);

  printf("Timeout requests: %d.\n", timeout_count);


}


/*
 *
 *  Gather statistics about FastIO Kernel Performance
 */
void
fastIO_get_stats(struct fastIO_state *cookie, struct ink_fio_stats *stats)
{
  ioctl(cookie->fiofd, FIO_GET_STATS, stats);


}

int
fastIO_add_pkt(struct fastIO_state *cookie, struct fastIO_pkt **fioPkt,
               int *fioPktCount, const char *pkt, int pktSize, uint16_t delaydelta)
{
  struct fastIO_block *curBlock[256];
  int i, nblks, size;
  struct fastIO_pkt *curPkt;

  nblks = pktSize / 1500;
  if (pktSize % 1500)
    nblks++;

  /* Is there space in the current "request" structure to hold this packet */
  if (nblks + (*fioPktCount) > 150)
    return 0;

  if (fastIO_balloc(cookie, nblks, curBlock, 0) == 0)
    return 0;

  printf("Delay delta = %d\n", delaydelta);

  curPkt = *fioPkt;
  for (i = 0; i < nblks; i++) {
    size = (pktSize <= 1500) ? pktSize : 1500;
    if (!((curBlock[i]) && (curBlock[i]->ptr))) {
      printf("Curblock ptr is bad!\n");
      exit(0);
    }

    memcpy(curBlock[i]->ptr, pkt, size);
    curPkt->blockID = curBlock[i]->id;
    curPkt->pktsize = size;
    if (i + 1 >= nblks)
      curPkt->inChain = 0;
    else
      curPkt->inChain = 1;

    curPkt->reserved = 0;

    if (i == 0)
      curPkt->delaydelta = delaydelta;
    else
      curPkt->delaydelta = 0;
    curPkt++;
    pktSize -= size;
    pkt += size;
  }
  *fioPktCount = (*fioPktCount) + nblks;
  *fioPkt = curPkt;
  return 1;
}
