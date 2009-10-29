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
#include "fio_dev.h"

#include "fastio.h"


#define inline

#if 1
int
fio_add_split_rule(fio_devstate_t * rsp, int id, struct fastIO_split_rule *rule)
{
  return 1;

}

int
fio_delete_split_rule(fio_devstate_t * rsp, int id, struct fastIO_split_rule *rule)
{
  return 1;
}

int
fio_flush_split_rules(fio_devstate_t * rsp, int id)
{
  return 1;

}

int
fio_vsession_cmd(fio_devstate_t * rsp, struct ink_cmd_msg *msg)
{
  return 1;
}

int
fio_vsession_create(fio_devstate_t * rsp)
{
  return -1;
}

int
fio_vsession_destroy(fio_devstate_t * rsp, int id)
{
  return 0;
}

/*
 * Handle vsession-related ioctls
 *
 */
int
fio_vsession_ioctl(fio_devstate_t * rsp, int cmd, intptr_t arg)
{
  struct ink_cmd_msg msg;

  switch (cmd) {
  case INKFIO_VSESSION_CREATE:
    return fio_vsession_create(rsp);
  case INKFIO_VSESSION_DESTROY:
    return fio_vsession_destroy(rsp, (int) arg);
  case INKFIO_VSESSION_CMD:
    if (ddi_copyin((char *) arg, &msg, sizeof(struct ink_cmd_msg), 0)) {
      cmn_err(CE_WARN, "fio_vsession_ioctl: Invalid userspace pointer 0x%x.\n", (int) arg);
      return -1;
    }
    return fio_vsession_cmd(rsp, &msg);


  }

  cmn_err(CE_WARN, "fio: Unrecognized vsession ioctl 0x%x\n", cmd);
  return -1;
}

#else

/*
 *  Add the specified splitting rule on the indicated vsession
 *
 *  Return 0 on error.
 *
 */
int
fio_add_split_rule(fio_devstate_t * rsp, int id, struct fastIO_split_rule *rule)
{

  /*cmn_err(CE_CONT, "fio_add_split_rule: Adding on vsession %d.\n", id); */

  /* verify that destination session ID makes sense */
  if (((int) rule->dst_queue >= MAX_SESSION) || !rsp->session[(int) rule->dst_queue])
    return -1;

  if (!rsp->vsession[id]) {
    /* first redirect entry */

    /*cmn_err(CE_CONT, "inkudp_handle_cmsg: First redirection entry.\n"); */


    rsp->vsession[id] = kmem_alloc(sizeof(struct ink_redirect_list), 0);
    if (!rsp->vsession[id]) {
      cmn_err(CE_WARN, "fio_add_split_rule: Out of memory.\n");
      return 0;
    }
    rsp->vsession[id]->dst_mblk = 0;
    rsp->vsession[id]->destIP = rule->dstIP;
    rsp->vsession[id]->destPort = rule->dstPort;
    rsp->vsession[id]->destSession = rsp->session[(int) rule->dst_queue];       /* transform Qid to queue ptr */
    rsp->vsession[id]->next = rsp->vsession[id];
    rsp->vsession[id]->prev = rsp->vsession[id];

  } else {
    struct ink_redirect_list *node;
    /* add an entry */
/*	    cmn_err(CE_CONT, "inkudp_handle_cmsg: Adding a redirection entry.\n");*/


    node = kmem_alloc(sizeof(struct ink_redirect_list), 0);
    if (!node) {
      cmn_err(CE_WARN, "inkudp_handle_cmsg: Out of memory.\n");
      return 0;
    }
    node->dst_mblk = 0;
    node->destIP = rule->dstIP;
    node->destPort = rule->dstPort;
    node->destSession = rsp->session[(int) rule->dst_queue];    /* transform Qid to queue ptr */

    node->next = rsp->vsession[id];
    node->prev = rsp->vsession[id]->prev;

    rsp->vsession[id]->prev->next = node;
    rsp->vsession[id]->prev = node;
  }
  return 1;                     /* success */
}



/*
 *
 * fio_delete_split_rule()
 *
 * Remove the specified splitting rule
 * Return 0 on failure, nonzero on success
 */
int
fio_delete_split_rule(fio_devstate_t * rsp, int id, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *trav;
  int first = 1;
  int axehead = 0;

  trav = rsp->vsession[id];

  /*cmn_err(CE_CONT, "fio_delete_split_rule: ...\n"); */
  while (trav && (first || trav != rsp->vsession[id])) {
    /* match */
    if (trav->destIP == rule->dstIP && trav->destPort == rule->dstPort) {
      trav->prev->next = trav->next;
      trav->next->prev = trav->prev;

      /* Deleting the first entry? */
      if (trav == rsp->vsession[id]) {
        /* Deleting the only entry ? */
        if (trav->next == trav) {
          rsp->vsession[id] = 0;
        } else {
          rsp->vsession[id] = trav->next;
        }



      }

      /* if we have a template destination mblk, free it */
      if (trav->dst_mblk)
        freeb(trav->dst_mblk);

      /* dealocate the splitting rule */
      kmem_free(trav, sizeof(struct ink_redirect_list));
      return 1;                 /* success */

    }
    first = 0;
    trav = trav->next;

  }
  cmn_err(CE_NOTE, "inkudp_delete_split_rule: Unable to find requested split rule in database.\n");
  return 0;                     /* failure */

}



/*
 *
 * fio_flush_split_rule()
 *
 * Remove all splitting rules
 * Return 0 on failure, nonzero on success
 */
int
fio_flush_split_rules(fio_devstate_t * rsp, int id)
{
  struct ink_redirect_list *trav;
  int first = 1;
  int axehead = 0;

  trav = rsp->vsession[id];
  cmn_err(CE_CONT, "inkudp_flush_split_rules...\n");
  while (trav && (first || trav != rsp->vsession[id])) {

    /* if we have a template destination mblk, free it */
    if (trav->dst_mblk)
      freeb(trav->dst_mblk);
    kmem_free(trav, sizeof(struct ink_redirect_list));
    trav = trav->next;
    first = 0;
  }

  rsp->vsession[id] = 0;

  return 1;                     /*success */
}




/*
 *
 *  Add or remove splitting rules for a vsession
 *
 */
int
fio_vsession_cmd(fio_devstate_t * rsp, struct ink_cmd_msg *msg)
{

  /* verify that the command references a valid vsession */
  if (msg->id >= MAX_VSESSION || !rsp->vsession_alloc[msg->id]) {
    cmn_err(CE_WARN, "inkfio: Invalid vsession identifier 0x%x.\n", msg->id);
    return 0;                   /*failure */
  }

  switch (msg->cmd) {

  case INK_CMD_SPLIT_ADD:
    return fio_add_split_rule(rsp, msg->id, &msg->payload.split_rule);
    break;
  case INK_CMD_SPLIT_DELETE:
    return fio_delete_split_rule(rsp, msg->id, &msg->payload.split_rule);
    break;

  case INK_CMD_SPLIT_FLUSH:
    return fio_flush_split_rules(rsp, msg->id);
    break;
  case INK_CMD_NOSE_PICK:
    cmn_err(CE_CONT, "inkudp_handle_cmsg: Ewww.  That's disgusting.\n");
    break;


  }

  return 0;                     /* error */

}

/*
 * Create a vsession
 *
 * Return the ID of the vsession, or -1 if the creation failed
 */
int
fio_vsession_create(fio_devstate_t * rsp)
{
  int i;
  if (rsp->vsession_count == MAX_VSESSION)
    goto oops;

  for (i = (rsp->vsession_count + 1) % MAX_VSESSION; i != rsp->vsession_count; i = (i + 1) % MAX_VSESSION) {
    if (!rsp->vsession_alloc[i]) {
      rsp->vsession_count++;
      rsp->vsession_alloc[i] = 1;
      rsp->vsession[i] = 0;     /* no redirect list yet */

      /* update statistics */
      rsp->stats.vsessions_open = rsp->vsession_count;

      return i;
    }

  }



oops:

  cmn_err(CE_WARN, "fio_vsession_create: Ooops.  Too many vsessions already.\n");
  return -1;

}

/*
 * Destroy a vsession
 *
 */
int
fio_vsession_destroy(fio_devstate_t * rsp, int id)
{
  if (id >= MAX_VSESSION || !rsp->vsession_alloc[id]) {
    cmn_err(CE_CONT, "fio_vsession_destroy: Attempt to delete invalid vsession %d.\n", id);
    return 0;
  }

  /* make sure we don't leak any splitting rules */
  fio_flush_split_rules(rsp, id);
  rsp->vsession_alloc[id] = 0;
  rsp->vsession_count--;

  return 1;                     /* success */
}

/*
 * Handle vsession-related ioctls
 *
 */
int
fio_vsession_ioctl(fio_devstate_t * rsp, int cmd, int arg)
{
  struct ink_cmd_msg msg;

  switch (cmd) {
  case INKFIO_VSESSION_CREATE:
    return fio_vsession_create(rsp);
  case INKFIO_VSESSION_DESTROY:
    return fio_vsession_destroy(rsp, (int) arg);
  case INKFIO_VSESSION_CMD:
    if (ddi_copyin((char *) arg, &msg, sizeof(struct ink_cmd_msg), 0)) {
      cmn_err(CE_WARN, "fio_vsession_ioctl: Invalid userspace pointer 0x%x.\n", (int) arg);
      return -1;
    }
    return fio_vsession_cmd(rsp, &msg);


  }

  cmn_err(CE_WARN, "fio: Unrecognized vsession ioctl 0x%x\n", cmd);
  return -1;
}

#endif
