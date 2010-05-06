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

	fio_request.c --
	Created On      : Fri Nov 10 14:24:06 2000
	Last Modified By: Sriram Rao
	Last Modified On: Sat Nov 11 18:34:29 2000
	Description: Handle fio requests.
****************************************************************************/



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
#include <sys/stream.h>
#include "fio_dev.h"
#include <sys/strsubr.h>
#include <sys/disp.h>
#include <sys/systm.h>

#include "fastio.h"


/* If there is an error sending a packet, how long in mS to wait
 * before trying again.
 */
#define RETRY_TIMEOUT 10

/* Maximum period between timeouts  (in msec) */
#define MAX_TIMEOUT 1000

/* Minimum period between timeouts (in msec) */
#define MIN_TIMEOUT 10

/* send stuff a bit early if it's convienent */
#define SLACK_MS 3


/*
 * A new free callback function
 *
 *
 */
void
fio_free_cb2(struct msgb *mp, struct datab *db)
{
  struct free_arg *p;

  /* cmn_err(CE_NOTE, "Calling fio_free via fio_free_cb2\n"); */

  /* call ink_free_cb as usual */
  /* XXX: This line may be problematic.  What if the kernel has trashed db_pad? */
  fio_free_cb(db->db_pad);

  /* now call the *Real* free function */
  p = (struct free_arg *) db->db_pad;

  /* put the last free function back */
  db->db_mblk = mp;

  db->db_lastfree = p->db_lastfree;
  db->db_free = p->db_lastfree;

  kmem_cache_free(db->db_cache, db);

  /* done ! */

}


/*
 * ink_esballoc
 *
 * A version of esballoc that won't cause context
 * switches out the wazoo
 */
mblk_t *
ink_esballoc(char *buf, int buflen, struct free_arg *freeinf)
{
  mblk_t *mp;

  mp = esballoc(0, 0, 0, 0);

  if (!mp) {
    cmn_err(CE_WARN, "ink_esballoc: Out of memory!.\n");
    return 0;
  }

  mp->b_datap->db_base = (unsigned char *) buf;
  mp->b_datap->db_lim = (unsigned char *) buf + buflen;
  mp->b_rptr = mp->b_wptr = (unsigned char *) buf;

  /* now do magic with the callbacks */
  freeinf->db_lastfree = mp->b_datap->db_lastfree;
  freeinf->db_free = mp->b_datap->db_free;

  mp->b_datap->db_lastfree = mp->b_datap->db_free = fio_free_cb2;

  mp->b_datap->db_pad = freeinf;


  return mp;

}


int
fio_vsession_send(fio_devstate_t * rsp, struct pending_request *req)
{
  return 1;
}


/*
 * Process a pending request. 
 *
 * Return 0 if the request should now be dequeued, otherwise
 * return the time in mS till the next packet should be sent
 *
 */
int
fio_process_request(fio_devstate_t * rsp, struct pending_request *req, hrtime_t now)
{
  int firstPkt = 1;
  int retval;
  pri_t tempPri;
  hrtime_t rec_hrtime;

  /* if things are shut down, don't process the request */
  if (!rsp->modopen) {

    return 0;
  }

  bcopy(&req->req->startTime, &rec_hrtime, sizeof(hrtime_t));

  if (now < rec_hrtime) {
    /* startTime and now are expresed in nanoseconds; return
     * the time til the request can start in milliseconds 
     */
    hrtime_t leftoverTime;

    leftoverTime = (req->req->startTime - now) / 1000000;

    return (int) (leftoverTime);

  }


  if ((req->elapsedDelay + SLACK_MS + rsp->timeout_duration) < req->nextPkt->delaydelta) {
    /* wait longer */
    req->elapsedDelay += rsp->timeout_duration;
    return (req->nextPkt->delaydelta - req->elapsedDelay);
  }

  if (!req->pktsRemaining) {
    cmn_err(CE_CONT, "fio_process_request: OOPS!  No packets left in this request <0x%x>!\n", req);
    return 0;

  }
  /* ready to send at least one packet */
  while (firstPkt || !req->nextPkt->delaydelta) {

    /* two messages, first one links to the second with b_cont */

    mblk_t *msg_dest, *msg_data, *msg_prev;
    firstPkt = 0;

    if (!rsp->modopen) {
      return 0;
    }


    /* check to make sure nextPkt has a valid block ID */
    if (!getBlockPtr(rsp, req->nextPkt->blockID)) {

      cmn_err(CE_WARN, "fio_process_request: Invalid block identifier %d in request.\n", req->nextPkt->blockID);
      return 0;
    }

    /* if the destination is a vsession, process accordingly */
    if (req->req->destIP == INKFIO_DEST_VSESSION) {
      fio_vsession_send(rsp, req);
    } else {                    /* send on a standard session */


      if (!req->destQ) {
        cmn_err(CE_WARN, "fio_process_request: ZIKES!  req->destQ is NULL!\n");

        return 0;
      }

      if (!fio_acquire_queue(req->destQIdx, req->destQ)) {
        /* It is entirely possible that the queue disappeared after the
           request was enqueued.  Such disappearance can happen if TS
           crashes */
        /* Free all the blocks */
        while (req->pktsRemaining > 0) {
          if (getBlockPtr(rsp, req->nextPkt->blockID))
            /* make sure that we are freeing a valid block */
            fio_free_cb((char *) &rsp->free_arg[req->nextPkt->blockID]);
          req->nextPkt->blockID = 0xffffffff;
          req->nextPkt++;
          req->pktsRemaining--;
        }
        return 0;
      }

      /* Setup the destination message block */

      msg_dest = (mblk_t *) dupb(req->dst_mblk);
      if (!msg_dest) {
        cmn_err(CE_WARN, "fio: Oops.  Out of memory in dupb().\n");

        fio_release_queue(req->destQIdx);
        return RETRY_TIMEOUT;
      }

      msg_prev = msg_dest;

      while (1) {
        msg_data = (mblk_t *) ink_esballoc(getBlockPtr(rsp, req->nextPkt->blockID),
                                           rsp->blocksize, &rsp->free_arg[req->nextPkt->blockID]);

        if (!msg_data) {
          cmn_err(CE_WARN, "inkio: esballoc fails.\n");
          fio_release_queue(req->destQIdx);

          return -1;
        }
        msg_data->b_datap->db_type = M_DATA;

        if (req->nextPkt->pktsize > FASTIO_BLOCK_SIZE) {
          cmn_err(CE_PANIC, "Whoops! We are getting a packet(%d) > 1500 bytes!", req->nextPkt->pktsize);
        }

        msg_data->b_wptr += req->nextPkt->pktsize;

        /* link the two messages together */
        msg_prev->b_cont = msg_data;
        msg_data->b_cont = 0;
        msg_prev = msg_data;

        if (!req->nextPkt->inChain) {
          /* There better be a packet that ends the chain */
          break;
        }
        cmn_err(CE_PANIC, "Whoops! We are getting a packet chain!");

        /* Update Statistics; in the non-chain case update happens after
           the loop. */
        rsp->stats.pkts_sent++;
        rsp->stats.bytes_sent += req->nextPkt->pktsize;

        req->nextPkt->blockID = 0xffffffff;

        req->nextPkt++;
        req->pktsRemaining--;
        if (req->pktsRemaining == 0) {
          cmn_err(CE_PANIC, "There is no sane end to a packet chain!");
        }
      }

      run_queues = 1;

      putnext((queue_t *) req->destQ, msg_dest);
      queuerun();
      fio_release_queue(req->destQIdx);


    }

    /* advance the next packet pointer */
    req->nextPkt->blockID = 0xffffffff;
    req->nextPkt++;

    /* decrement the remaining packet count pointer */
    req->pktsRemaining--;

    /* set the elapsed delay to 0 */
    req->elapsedDelay = 0;

    /* Is the request complete ? */
    if (!req->pktsRemaining)
      return 0;
  }

/* requeue the remaining packets */
  return req->nextPkt->delaydelta;
}


/*
 *  Callback function to process pending requests
 *
 */

void
fio_process_queue(void *ptr)
{
  struct pending_request *trav;
  int reprocess_time = 0;
  fio_devstate_t *rsp = (fio_devstate_t *) ptr;
  hrtime_t now;
  uint32_t blockID;

  now = gethrtime();

/*	cmn_err(CE_NOTE, "Inside process queue \n"); */


  mutex_enter(&rsp->reqmx);

  if (!rsp->modopen) {
    cmn_err(CE_CONT, "fio_process_queue: Called after shutdown.\n");
    mutex_exit(&rsp->reqmx);
    return;
  }
  if (rsp->pRequests) {
    int tempTime;
    int deleteHead = 0;

    reprocess_time = fio_process_request(rsp, rsp->pRequests, now);

    if (!reprocess_time) {

      deleteHead = 1;
    }
    trav = rsp->pRequests->next;

    while (trav != rsp->pRequests) {
      tempTime = fio_process_request(rsp, trav, now);
      if (tempTime) {
        if (tempTime < reprocess_time || !reprocess_time)
          reprocess_time = tempTime;

        trav = trav->next;
      } else {                  /*if(!tempTime) */

        struct pending_request *nextreq;

        /* remove the request from the queue */
        trav->prev->next = trav->next;
        trav->next->prev = trav->prev;

        nextreq = trav->next;
        /* free the dst_mblk message */
        freemsg(trav->dst_mblk);
        trav->dst_mblk = 0;

        /* delete the request itself: remember we copied the request into a
           kernel block; free the kernel block */
        kmem_free(trav->req, FASTIO_BLOCK_SIZE);

        /* delete the request structure */
        kmem_free(trav, sizeof(struct pending_request));

        trav = nextreq;
      }
    }

    if (deleteHead) {
      struct pending_request *pNext;
      int rset = 0;

      freemsg(rsp->pRequests->dst_mblk);
      rsp->pRequests->dst_mblk = 0;

      /* delete the request itself: remember we copied the request into a
         kernel block; free the kernel block */
      kmem_free(rsp->pRequests->req, FASTIO_BLOCK_SIZE);

      pNext = rsp->pRequests->next;

      rsp->pRequests->prev->next = rsp->pRequests->next;
      rsp->pRequests->next->prev = rsp->pRequests->prev;

      /* is the head the only thing on the queue ? */
      if (rsp->pRequests == pNext) {
        rset = 1;
      }

      kmem_free(rsp->pRequests, sizeof(struct pending_request));

      if (rset)
        rsp->pRequests = 0;
      else
        rsp->pRequests = pNext;

    }

    /* reschedule work if there is any */
    if (reprocess_time && rsp->modopen) {
      if (reprocess_time > MAX_TIMEOUT)
        reprocess_time = MAX_TIMEOUT;
      if (reprocess_time < MIN_TIMEOUT)
        reprocess_time = MIN_TIMEOUT;

      rsp->timeout_id = timeout(fio_process_queue, rsp, drv_usectohz(1000 * reprocess_time));

      rsp->timeout_duration = reprocess_time;

      /* Update Statistics */
      rsp->stats.kernel_timeout_requests++;

    } else {
      rsp->timeout_id = 0;
      rsp->timeout_duration = 0;

    }

  }

  mutex_exit(&rsp->reqmx);
}


/*
 * Queue a partially completed request 
 *
 */
void
fio_queue_request(fio_devstate_t * rsp, struct pending_request *req)
{



  if (!rsp || !req) {

    cmn_err(CE_WARN, "fio_queue_request: Called with null parameters!  Bad!\n");
    return;
  }

  mutex_enter(&rsp->reqmx);

  /* check modopen condition */

  if (!rsp->modopen) {
    cmn_err(CE_CONT, "fio_queue_request: Called after shutdown.\n");
    mutex_exit(&rsp->reqmx);
    return;
  }

  if (rsp->pRequests) {
    /* existing requests pending, append */
    rsp->pRequests->prev->next = req;
    req->prev = rsp->pRequests->prev;
    rsp->pRequests->prev = req;
    req->next = rsp->pRequests;
    /* since existing requests are pending, 
     * we don't need to schedule a callback
     *
     * Note: this may result in the next packet of the request being early
     * or late, since we don't know when the callback will come.
     */
  } else {
    /* first request */

    rsp->pRequests = req;
    req->prev = req;
    req->next = req;

    /* take out the && 0 to enable request processing immediately w/o callback.
     * this generally hurts performance by limiting parallelism
     */

    if (!req->nextPkt->delaydelta && 0) {
      fio_process_queue(rsp);
    } else {
      if (!req->nextPkt->delaydelta)
        /* introduce a 1ms delay */
        req->nextPkt->delaydelta = 1;

      /* schedule a callback */
      rsp->timeout_id = timeout(fio_process_queue, rsp, drv_usectohz(req->nextPkt->delaydelta * 1000));

      rsp->timeout_duration = req->nextPkt->delaydelta;

      /* Update Statistics */
      rsp->stats.kernel_timeout_requests++;

    }

  }
  mutex_exit(&rsp->reqmx);

}
