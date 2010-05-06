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




#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/modctl.h>
#include <sys/open.h>
#include <sys/kmem.h>
#include <sys/poll.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/socket.h>

#include "fio_dev.h"

#include "fastio.h"


#define inline

extern int run_queues;

#define NO_AGGREGATE




void
fio_dump_mblk(mblk_t * mp)
{

  mblk_t *trav;
  unsigned char *p;

  trav = mp;



  while (trav) {
    cmn_err(CE_CONT,
            "mblk<0x%x>: b_next<0x%x> b_prev<0x%x> b_cont<0x%x> pri<0x%x> flags<0x%x> rptr<0x%x> wptr<0x%x> size<%d>\n",
            trav,
            trav->b_next,
            trav->b_prev,
            trav->b_cont, trav->b_band, trav->b_flag, trav->b_rptr, trav->b_wptr, trav->b_wptr - trav->b_rptr);

    switch (trav->b_datap->db_type) {
    case M_BREAK:
      cmn_err(CE_CONT, "M_BREAK: ");
      break;
    case M_CTL:
      cmn_err(CE_CONT, "M_CTL: ");
      break;
    case M_DATA:
      cmn_err(CE_CONT, "M_DATA: ");
      break;
    case M_DELAY:
      cmn_err(CE_CONT, "M_DELAY: ");
      break;
    case M_IOCTL:
      cmn_err(CE_CONT, "M_IOCTL: ");
      break;
    case M_PASSFP:
      cmn_err(CE_CONT, "M_PASSFP: ");
      break;
    case M_PROTO:
      cmn_err(CE_CONT, "M_PROTO: ");
      break;
    case M_SETOPTS:
      cmn_err(CE_CONT, "M_SETOPTS: ");
      break;
    case M_SIG:
      cmn_err(CE_CONT, "M_SIG: ");
      break;
    case M_COPYIN:
      cmn_err(CE_CONT, "M_COPYIN: ");
      break;
    case M_COPYOUT:
      cmn_err(CE_CONT, "M_COPYOUT: ");
      break;
    case M_ERROR:
      cmn_err(CE_CONT, "M_ERROR: ");
      break;
    case M_FLUSH:
      cmn_err(CE_CONT, "M_FLUSH: ");
      break;
    case M_HANGUP:
      cmn_err(CE_CONT, "M_HANGUP: ");
      break;
    case M_UNHANGUP:
      cmn_err(CE_CONT, "M_UNHANGUP: ");
      break;
    case M_IOCACK:
      cmn_err(CE_CONT, "M_IOCACK: ");
      break;
    case M_IOCDATA:
      cmn_err(CE_CONT, "M_IOCDATA: ");
      break;
    case M_PCPROTO:
      cmn_err(CE_CONT, "M_PCPROTO: ");
      break;
    case M_PCSIG:
      cmn_err(CE_CONT, "M_PCSIG: ");
      break;
    case M_READ:
      cmn_err(CE_CONT, "M_READ: ");
      break;
    case M_START:
      cmn_err(CE_CONT, "M_START: ");
      break;
    case M_STARTI:
      cmn_err(CE_CONT, "M_STARTI: ");
      break;
    case M_STOP:
      cmn_err(CE_CONT, "M_STOP: ");
      break;
    case M_STOPI:
      cmn_err(CE_CONT, "M_STOPI: ");
      break;

    default:
      cmn_err(CE_CONT, "Unknown type:");
    }



    cmn_err(CE_CONT, "db_base<0x%x>, db_lim<0x%x>, db_ref<%d>, db_type<0x%x> size<%d>\n",
            trav->b_datap->db_base,
            trav->b_datap->db_lim,
            trav->b_datap->db_ref, trav->b_datap->db_type, trav->b_datap->db_lim - trav->b_datap->db_base);

    cmn_err(CE_CONT, "\nBuffer: ");


    if ((trav->b_wptr - trav->b_rptr) < 100)
      for (p = trav->b_rptr; p < trav->b_wptr; p++) {
        cmn_err(CE_CONT, " 0x%x/%d/'%c' ", *p, *p, *p);

    } else
      cmn_err(CE_CONT, "**skipping data, too much **\n");
    cmn_err(CE_CONT, "\n");
    if (trav->b_cont != trav && trav->b_cont != mp)
      trav = trav->b_cont;
    else
      trav = 0;
  }


}




/*
 * Initialize a STREAMS UDP request message body 
 *
 *
 *  Magic numbers for Solaris... maybe not 
 *  relevant for other platforms
 */
inline void
fio_udppkt_init(struct udppkt *p)
{
  /* the magic numbers get hardcoded
   *
   */
#ifdef i386
  char data[22] = { 0x08, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x02, 0x00
  };
  char ftr[8] = { 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
  };
#endif
#ifdef sparc

  char data[22] = { 0x00, 0x00, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x14,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x02
  };
  char ftr[8] = { 0x00, 0x00, 0x00, 0x41,
    0x00, 0x00, 0x00, 0x00
  };
#endif

  bcopy(data, &p->hdr, 22);
  bcopy(ftr, &p->ftr, 8);
}

/*
 *  Construct a message with the proper destination block
 */
inline mblk_t *
fio_dstmsg_create(int32_t ip, int16_t port)
{

  mblk_t *mp;
  struct udppkt *buf;



/*    cmn_err(CE_NOTE, "inkio: udppkt is %d bytes.\n", sizeof(struct udppkt));*/
  mp = (mblk_t *) allocb(72, 0);

  /* cmn_err(CE_NOTE, "* * * * * *  NEWLY ALLOCATED DMSG * * * * *");
     inkio_dump_mblk(mp);  */

  if (!mp) {
    cmn_err(CE_WARN, "inkio: out of memory!\n");
    return 0;
  }


  buf = (struct udppkt *) mp->b_wptr;

  fio_udppkt_init(buf);

  buf->port = port;
  buf->ip = ip;



  /* set the message type to M_PROTO */
  mp->b_datap->db_type = M_PROTO;
  mp->b_wptr = mp->b_rptr + sizeof(struct udppkt);
  return mp;
}

/*
 * called by the callback for freeing of message blocks
 */
void
fio_free_cb(char *dat)
{
  struct free_arg *arg = (struct free_arg *) dat;

  if (!arg->rsp->modopen)
    return;

  /* cmn_err(CE_CONT, "fio_free_cb: #%d.\n", arg->blockID); */

  mutex_enter(&arg->rsp->freemx);
  if (arg->rsp->nextflentry > arg->rsp->blkcount)
    cmn_err(CE_PANIC, "# of free blks is > array size!");

  arg->rsp->activefl[arg->rsp->nextflentry] = arg->blockID;
  arg->rsp->nextflentry++;

  /* cmn_err(CE_CONT, "freeing block: %d\n", arg->blockID); */

  if (arg->rsp->signal_user && (arg->rsp->nextflentry > (arg->rsp->blkcount / 10))) {
    /*cmn_err(CE_CONT, "fio_free_cb: Signaling user...\n"); */
    proc_signal(arg->rsp->signal_ref, SIGUSR1);
    proc_unref(arg->rsp->signal_ref);
    arg->rsp->signal_user = 0;
  }
  mutex_exit(&arg->rsp->freemx);

}



/*
 * Return a pointer to the requested block
 */
inline void *
getBlockPtr(fio_devstate_t * rsp, uint32_t id)
{


  int dist = id * rsp->blocksize;

  if (id >= rsp->blkcount)
    return 0;

  dist += rsp->blockbaseptr;

/*    cmn_err(CE_CONT, "blocksize:%d blockbaseptr:0x%x block #%d @0x%x\n",
	    rsp->blocksize, rsp->blockbaseptr, id, dist);*/


  return (void *) ((uintptr_t) dist);
}

/*
 *  Initialization IOCTL
 *
 */
static int
fio_ioctl_init(fio_devstate_t * rsp, intptr_t cmd)
{

  int i;

  rsp->bufbaseptr = (int *) rsp->ram;
  rsp->blocksize = FASTIO_BLOCK_SIZE;
  rsp->blkcount = (int32_t) cmd;

  /*cmn_err(CE_CONT, "fio_ioctl_init: blockcount: %d\n", cmd); */

  rsp->flist0 = (uint32_t *) rsp->bufbaseptr;
  rsp->flist1 = (uint32_t *) rsp->bufbaseptr + rsp->blkcount;
  rsp->blockbaseptr = (intptr_t) ((uint32_t *) rsp->flist1 + rsp->blkcount);

  rsp->active = 1;
  rsp->activefl = rsp->flist1;

  rsp->signal_user = 0;

  /* initialze the free structs */
  rsp->free_struct = (struct free_rtn *) kmem_alloc(sizeof(struct free_rtn) * rsp->blkcount, 0);
  rsp->free_arg = (struct free_arg *) kmem_alloc(sizeof(struct free_arg) * rsp->blkcount, 0);

  rsp->pRequests = 0;
  rsp->timeout_id = 0;

  if (!rsp->free_struct) {
    cmn_err(CE_WARN, "fio: unable to allocate memory!\n");
    return 1;
  }

  for (i = 0; i < rsp->blkcount; i++) {

    rsp->free_struct[i].free_func = fio_free_cb;
    rsp->free_arg[i].rsp = rsp;
    rsp->free_arg[i].blockID = i;
    rsp->free_struct[i].free_arg = (char *) &rsp->free_arg[i];
  }

  rsp->modopen = 1;

  /* initialize the vsessions */
  for (i = 0; i < MAX_VSESSION; i++) {

    rsp->vsession_alloc[i] = 0;

  }
  rsp->vsession_count = 0;


  /* Initialize statistics */
  bzero(&rsp->stats, sizeof(struct ink_fio_stats));

/* initialize the free mutex */
  mutex_init(&rsp->freemx, NULL, MUTEX_DRIVER, NULL);
  mutex_init(&rsp->modopenmx, NULL, MUTEX_DRIVER, NULL);
  mutex_init(&rsp->reqmx, NULL, MUTEX_DRIVER, NULL);
  rsp->nextflentry = 0;

  return 0;                     /*success */
}


/*
 *  Sendto IOCTL
 *
 */
static int
fio_ioctl_sendto(fio_devstate_t * rsp, intptr_t cmd)
{
  int i;
  int blockid;
  struct fastIO_pkt *pkt;
  struct udppkt udpreq;
  /* contains information about the destination */
  mblk_t *dst_mblk;
  struct pending_request *pReq;
  /* for traversing the request */
  struct fastIO_request *userReq, *req;
  struct free_rtn *freecb;
  /* for debug stuff */
  char *ip;
  mblk_t *head = 0, *tail = 0;
  queue_t *dstqueue;
  int destQ;

  blockid = cmd;

  /*cmn_err(CE_CONT, "sendto: block %d.\n", blockid); */

  userReq = (struct fastIO_request *) getBlockPtr(rsp, (uint32_t) blockid);

  if (!userReq) {
    cmn_err(CE_CONT, "fio_ioctl_sendto: Bad block id %d.\n", blockid);
    return -1;
  }
  req = (struct fastIO_request *) kmem_alloc(FASTIO_BLOCK_SIZE, 0);

  if (!req) {
    /* no memory.  this baby ain't flying! */
    cmn_err(CE_NOTE, "fio_ioctl_sendto: no memory for copying request!\n");
    fio_free_cb((char *) &rsp->free_arg[blockid]);
    return -1;
  }

  /* copy the request into a kernel block */
  bcopy(userReq, req, FASTIO_BLOCK_SIZE);

  /* sanity check that the specified destination queue ID is valid */
  if ((req->destIP != INKFIO_DEST_VSESSION) && ((int) req->destQ > MAX_SESSION || !rsp->session[(int) req->destQ])) {
    /* no dice.  this baby ain't flying! */
    cmn_err(CE_NOTE, "fio_ioctl_sendto:(%d pkts) bad destination session ID %d!\n", req->pktCount, (int) req->destQ);
    /* free the alloc'e memory */
    kmem_free(req, FASTIO_BLOCK_SIZE);
    fio_free_cb((char *) &rsp->free_arg[blockid]);
    return -1;
  }

  /* Sanity check the request to make sure it is valid */
  if (!fio_valid_request(rsp, req)) {
    /* It is invalid request.  So don't bother with freeing blocks alloc'ed
       by the user.  The user deserves what they get for sending junk down */
    /* free the alloc'e memory */
    cmn_err(CE_NOTE, "Got an invalid request\n");
    kmem_free(req, FASTIO_BLOCK_SIZE);
    fio_free_cb((char *) &rsp->free_arg[blockid]);
    return -1;
  }

  /* rewrite the Q id setting it ot the real Q pointer if this is not a vsession */
  pReq = (struct pending_request *) kmem_alloc(sizeof(struct pending_request), 0);
  if (!pReq) {
    cmn_err(CE_WARN, "fio: Unable to allocate pending request structure!\n");
    /* The good blocks went nowhere; free them */
    fio_free_request_blks(rsp, req);
    /* free the alloc'e memory */
    kmem_free(req, FASTIO_BLOCK_SIZE);
    fio_free_cb((char *) &rsp->free_arg[blockid]);
    return -1;
  }

  pkt = (struct fastIO_pkt *) (req + 1);

  /* generate a template mblk with the STREAMS udp request */
  dst_mblk = (mblk_t *) fio_dstmsg_create(req->destIP, req->destPort);
  if (!dst_mblk) {
    cmn_err(CE_WARN, "inkio: out of memory (inkio_dstmsg_create failed)\n");
    /* The good blocks went nowhere; free them */
    fio_free_request_blks(rsp, req);
    kmem_free(req, FASTIO_BLOCK_SIZE);
    return -1;
  }

  /* fill out the request block */

  pReq->requestBlock = 0xffffffff;
  pReq->pktsRemaining = req->pktCount;
  pReq->elapsedDelay = 0;
  pReq->req = req;
  pReq->nextPkt = pkt;
  pReq->dst_mblk = dst_mblk;
  pReq->destQIdx = (int) req->destQ;
  pReq->destQ = rsp->session[(int) req->destQ];
  req->destQ = 0;               /* generate an error if we ever touch this memory again! */
  userReq->destQ = 0;

  /* enqueue the request */

  fio_queue_request(rsp, pReq);

  /* Mark the user's request block as free: we have already validated the
     block ptr */
  fio_free_cb((char *) &rsp->free_arg[blockid]);

  /* Update Statistics */
  rsp->stats.sendto_requests++;

  return 0;                     /*success */
}

/*
 * Free all the blocks that make up a request
 */
static void
fio_free_request_blks(fio_devstate_t * rsp, struct fastIO_request *req)
{
  struct fastIO_pkt *pkt;
  int i;

  pkt = (struct fastIO_pkt *) (req + 1);
  for (i = 0; i < req->pktCount; i++) {
    fio_free_cb((char *) &rsp->free_arg[pkt->blockID]);
    pkt++;
  }
}

/*
 * Sanity check the request to make sure that block id's are valid and the
 * block sizes are reasonable.
 * Returns: 1 if the request is valid; 0 otherwise
 */
static int
fio_valid_request(fio_devstate_t * rsp, struct fastIO_request *req)
{
  struct fastIO_pkt *pkt;
  int i;
  uint16_t isChain = 0;

  if (req->pktCount > FASTIO_MAX_REQS_PER_REQ_BLOCK) {
    cmn_err(CE_NOTE, "Too many reqs per block: %d\n", req->pktCount);
    return 0;
  }
  pkt = (struct fastIO_pkt *) (req + 1);
  for (i = 0; i < req->pktCount; i++) {
    if (!getBlockPtr(rsp, pkt->blockID)) {
      cmn_err(CE_NOTE, "Failing a request: bad block ptr\n");
      return 0;
    }
    if (pkt->pktsize > FASTIO_BLOCK_SIZE) {
      cmn_err(CE_NOTE, "Pkt size is too big (%d) \n", pkt->pktsize);
      return 0;
    }
    isChain = pkt->inChain ? 1 : 0;
    pkt++;
  }
  /* Chain isn't terminated! */
  if (isChain) {
    cmn_err(CE_NOTE, "No sane end to a packet chain!\n");
    return 0;
  }
  /* Everything is sane so far... */
  return 1;
}

/*
 *  Swap IOCTL
 *
 */
static int
fio_ioctl_swap(fio_devstate_t * rsp, intptr_t cmd)
{

  /*cmn_err(CE_CONT, "fio_ioctl_swap...\n"); */

  mutex_enter(&rsp->freemx);


  if (rsp->activefl[rsp->nextflentry] == 0xffffffff && !rsp->signal_user) {
    /* swapping won't help the user.  Signal the user to wake up when it will */
  }


  rsp->nextflentry = 0;
  rsp->active = !rsp->active;
  if (rsp->active)
    rsp->activefl = rsp->flist1;
  else
    rsp->activefl = rsp->flist0;


  mutex_exit(&rsp->freemx);

  /* Update Statistics */
  rsp->stats.swap_requests++;

  return 0;                     /*DDI_SUCCESS; */

}


/*
 * Handle a metarequest 
 *
 * Metarequests are a list of request block numbers stored in a metarequest block
 * Metarequests increase single-syscall bandwidth by a factor of 750. :)
 *
 */
static int
fio_ioctl_metasend(fio_devstate_t * rsp, intptr_t cmd)
{
  uint32_t *sendblk, *term;

  /*cmn_err(CE_CONT, "fio_ioctl_metasend:  Metasend block %d.\n", cmd); */

  sendblk = getBlockPtr(rsp, (uint32_t) cmd);
  term = sendblk + (rsp->blocksize / sizeof(uint32_t));
  if (!sendblk) {
    cmn_err(CE_CONT, "fio_ioctl_metasend: Invalid metablock.\n");
    return DDI_FAILURE;
  }

  /* Update Statistics */
  rsp->stats.metasend_requests++;

  while (sendblk < term && (*sendblk != 0xffffffff)) {

    /* don't even call sendto if the block ID is invalid */
    if (!getBlockPtr(rsp, *sendblk)) {
      sendblk++;
      continue;
    }

    fio_ioctl_sendto(rsp, *sendblk);
    sendblk++;
  }

  /* free the metarequest block */
  fio_free_cb((char *) &rsp->free_arg[cmd]);

  return DDI_SUCCESS;

}


/*
 *  Cleanup IOCTL
 *
 */
int
fio_ioctl_fini(fio_devstate_t * rsp, intptr_t cmd)
{

  return 0;                     /* SUCCESS; */

}

/*
 * Copy statistics to userspace
 *
 */
int
fio_ioctl_get_stats(fio_devstate_t * rsp, intptr_t arg)
{
  return ddi_copyout(&rsp->stats, (void *) arg, sizeof(struct ink_fio_stats), 0);
}






int
fio_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t * cred_p, int *rval_p)
{
  int retval = -1;
  fio_devstate_t *rsp;

  rsp = ddi_get_soft_state(fio_state, getminor(dev));
  if (!rsp) {
    cmn_err(CE_WARN, "fio_ioctl: unable to get soft state\n");
    return ENXIO;
  }

  /* update statistics */
  rsp->stats.ioctl_requests++;


  if (cmd & INKFIO_VSESSION_MASK) {
    retval = fio_vsession_ioctl(rsp, cmd, arg);
    *rval_p = retval;
    return DDI_SUCCESS;
  } else if (cmd & INK_CMD_SPLIT_IOCTLMASK) {

    retval = fio_vsession_ioctl(rsp, cmd, arg);
    *rval_p = retval;
    return DDI_SUCCESS;

  }


  switch (cmd) {
  case FIO_INIT:
    retval = fio_ioctl_init(rsp, arg);
    break;
  case FIO_SENDTO:
    retval = fio_ioctl_sendto(rsp, arg);
    break;
  case FIO_SWAP:
    retval = fio_ioctl_swap(rsp, arg);
    break;
  case FIO_METASEND:
    retval = fio_ioctl_metasend(rsp, arg);
    break;
  case FIO_FINI:
    retval = fio_ioctl_fini(rsp, arg);
    break;
  case FIO_GET_TIME_STAT:
    retval = rsp->stat_timeout_count;
    rsp->stat_timeout_count = 0;
    break;
  case FIO_GET_STATS:
    retval = fio_ioctl_get_stats(rsp, arg);
    break;
  case FIO_REG_SENDTO:
    /*retval=fio_regsendto(rsp, arg); *//* no longer supported */
    break;
  case FIO_DELETE_QUEUE:
    fio_unregister_queue(arg);
    retval = 0;
    break;
  default:
    cmn_err(CE_WARN, "fio: Unrecognized ioctl cmd (%d).\n", cmd);
  }

  *rval_p = retval;
  return DDI_SUCCESS;

}
