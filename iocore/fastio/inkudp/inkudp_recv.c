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
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/modctl.h>
#include <sys/stream.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/socket.h>
#include <sys/int_types.h>
#include <sys/stropts.h>
#include <inet/common.h>
#include <inet/led.h>
#include <inet/ip.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#include <sys/sunddi.h>
#include <sys/strsubr.h>

#include "../include/fastio.h"
#include "solstruct.h"
#include "inkudp.h"

#define	MTYPE(m)	((m)->b_datap->db_type)

extern int redirect_enabled;
extern int redirect_passthrough;
extern struct ink_redirect_list *redirect_list_head;
extern kmutex_t splitmx;

int printed_once = 0;
queue_t *fio_lookup_queue(int qid);

typedef struct udphdr udphdr_t;
typedef struct ip ip_t;
/*
 *  Add the specified splitting rule.
 *
 *  Return 0 on error.
 *
 */
int
inkudp_add_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *node;
  struct ink_redirect_list_node *list_node;
  int status, release_mutex = 0;

  if (!mutex_owned(&splitmx)) {
    mutex_enter(&splitmx);
    release_mutex = 1;
  }

  redirect_enabled = 1;
  redirect_passthrough = 1;

#if 0
  cmn_err(CE_CONT, "inkudp_add_split_rule\n");
#endif

  /* 
   * bail out if this is an error or we have successfully added to
   * the appropriate split list; otherwise, try to add the thing.
   */
  if ((status = inkudp_find_add_split_rule(incomingQ, rule)) >= 0) {
    if (release_mutex)
      mutex_exit(&splitmx);
    return status;
  }

  node = kmem_alloc(sizeof(struct ink_redirect_list), 0);
  if (!node) {
    cmn_err(CE_WARN, "inkudp_handle_cmsg: Out of memory.\n");
    if (release_mutex)
      mutex_exit(&splitmx);
    return 0;
  }
  node->srcIP = rule->srcIP;
  node->srcPort = rule->srcPort;
  node->incomingQ = incomingQ;
  mutex_init(&node->list_mutex, NULL, MUTEX_DRIVER, NULL);

  inkudp_create_redir_list_node(&list_node, rule);

  if (!list_node) {
    mutex_destroy(&node->list_mutex);
    kmem_free(node, sizeof(struct ink_redirect_list));
    if (release_mutex)
      mutex_exit(&splitmx);

    return 0;
  }

  node->redirect_nodes = list_node;

  if (!redirect_list_head) {
    node->next = node->prev = NULL;
    redirect_list_head = node;
    if (release_mutex)
      mutex_exit(&splitmx);
    return 1;
  }

  /* put it to the head of the list */
  node->prev = NULL;
  node->next = redirect_list_head;
  redirect_list_head->prev = node;
  redirect_list_head = node;

  if (release_mutex)
    mutex_exit(&splitmx);

  return 1;
}

int
inkudp_create_redir_list_node(struct ink_redirect_list_node **list_node, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list_node *node;

  *list_node = NULL;
  node = kmem_alloc(sizeof(struct ink_redirect_list_node), 0);

  if (!node) {
    cmn_err(CE_WARN, "inkudp_handle_cmsg: Out of memory.\n");
    return 0;
  }
  node->destIP = rule->dstIP;
  node->destPort = rule->dstPort;

  node->destSession = fio_lookup_queue((int) rule->dst_queue);
  if (!node->destSession) {
    kmem_free(node, sizeof(struct ink_redirect_list_node));
    cmn_err(CE_WARN, "inkudp_add_split_rule: Bad qid %d.\n", rule->dst_queue);
    return 0;
  }
  node->next = node->prev = NULL;
  *list_node = node;
  /* everything went thru... */
  return 1;
}

/*
 * finds a split rule from the list of redirection rules.
 * return 1 if there is a valid rule; 0 otherwise
 */

int
inkudp_find_split_rule(queue_t * incomingQ, struct ink_redirect_list **redir_node, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *node = redirect_list_head;

  while (node) {
    if ((node->incomingQ == incomingQ) && (node->srcIP == rule->srcIP) && (node->srcPort == rule->srcPort)) {
      *redir_node = node;
      return 1;
    }
    node = node->next;
  }
  *redir_node = NULL;
  return 0;
}

/*
 * Returns : 1 if the addition to appropriate redir. list is successful;
 *           0 if there is an error.
 *	     -1 if the appropriate redir. list isn't there => create
 *		 one and add the thing.
 */
int
inkudp_find_add_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *node;
  struct ink_redirect_list_node *list_node;
  int release_list_mutex = 0;

  inkudp_find_split_rule(incomingQ, &node, rule);
  if (!node)
    /* the appropriate redir. list needs to be created */
    return -1;

  /* found the right list! */
  inkudp_create_redir_list_node(&list_node, rule);
  if (!list_node)
    /* something bad happened */
    return 0;
  if (!mutex_owned(&node->list_mutex)) {
    release_list_mutex = 1;
    mutex_enter(&node->list_mutex);
  }
  list_node->prev = NULL;
  list_node->next = node->redirect_nodes;
  node->redirect_nodes->prev = list_node;
  node->redirect_nodes = list_node;
  if (release_list_mutex)
    mutex_exit(&node->list_mutex);
  /* yeah! we succeded */
  return 1;
}

/*
 *
 * inkudp_delete_split_rule()
 *
 * Remove the specified splitting rule
 * Return 0 on failure, nonzero on success
 */
int
inkudp_delete_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *node;
  struct ink_redirect_list_node *list_node;

  inkudp_find_split_rule(incomingQ, &node, rule);

  if (!node)
    /* trying to delete something that doesn't exist... */
    return 0;
  list_node = node->redirect_nodes;
  while (list_node) {
    if ((list_node->destIP == rule->dstIP) && (list_node->destPort == rule->dstPort)) {
      /* found the right one */
      if (list_node->prev)
        list_node->prev->next = list_node->next;
      if (list_node->next)
        list_node->next->prev = list_node->prev;
      /* if we are removing the head, adjust the pointers */
      if (node->redirect_nodes == list_node)
        node->redirect_nodes = list_node->next;
      kmem_free(list_node, sizeof(struct ink_redirect_list_node));
      return 1;
    }
    list_node = list_node->next;
  }
  cmn_err(CE_NOTE, "inkudp_delete_split_rule: Unable to find requested split rule in database.\n");
  return 0;                     /* failure */
}

/*
 *
 * inkudp_flush_split_rule()
 *
 * Remove the redirect list for the given split rule.
 * Return 0 on failure, nonzero on success
 */

int
inkudp_flush_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *node = NULL;
  struct ink_redirect_list_node *list_node;

  inkudp_find_split_rule(incomingQ, &node, rule);

  if (!node)
    /* trying to delete something that doesn't exist... */
    return 0;

  /* Remove all the redirect nodes */
  while (node->redirect_nodes) {
    list_node = node->redirect_nodes;
    node->redirect_nodes = list_node->next;
    kmem_free(list_node, sizeof(struct ink_redirect_list_node));
  }

  /* Now remove the node from the redirect list */
  if (node->prev)
    node->prev->next = node->next;
  if (node->next)
    node->next->prev = node->prev;
  if (node == redirect_list_head)
    redirect_list_head = node->next;
  if (redirect_list_head == NULL)
    redirect_enabled = 0;
  kmem_free(node, sizeof(struct ink_redirect_list));
  return 1;
}

/*
 *  inkudp_handle_cmsg()
 *  Process a control message.  
 *
 *  These messages sometimes contain important data, so we should
 *  make sure they are all ignored.
 *
 *  Return 0 on failure, nonzero on success
 *
 */
int
inkudp_handle_cmsg(mblk_t * mp, queue_t * q)
{

  int release_mutex = 0;
  struct ink_cmd_msg *cmsg;

#if 0
  cmn_err(CE_CONT, "inkdup_handle_csmg \n");
#endif

  cmsg = (struct ink_cmd_msg *) mp->b_rptr;

  if (!mutex_owned(&splitmx)) {
    mutex_enter(&splitmx);
    release_mutex = 1;
  }

  switch (cmsg->cmd) {

  case INK_CMD_SPLIT_ADD:
#if 0
    cmn_err(CE_CONT, "Adding split rule for port = %d, q = 0x%x\n", cmsg->payload.split_rule.srcPort, q);
#endif
    inkudp_add_split_rule(q, &(cmsg->payload.split_rule));
    break;
  case INK_CMD_SPLIT_DELETE:
    inkudp_delete_split_rule(q, &cmsg->payload.split_rule);
    break;

  case INK_CMD_SPLIT_FLUSH:
    inkudp_flush_split_rule(q, &cmsg->payload.split_rule);
    break;
  case INK_CMD_NOSE_PICK:
    cmn_err(CE_CONT, "inkudp_handle_cmsg: Ewww.  That's disgusting.\n");
    break;
  default:
    cmn_err(CE_WARN, "inkudp_handle_cmsg: Unsupported or unrecognized control command.\n");
    if (release_mutex)
      mutex_exit(&splitmx);
    return 0;                   /* error */
  }

  if (release_mutex)
    mutex_exit(&splitmx);

  return 1;                     /* success */
}



/*
 *   Process Inbound Packets
 *
 *
 */
int
inkudp_recv(mblk_t * mp, queue_t * q)
{

  int release_mutex = 0, release_list_mutex = 0;
  struct ink_redirect_list *node;
  struct ink_redirect_list_node *list_node;
  struct fastIO_split_rule rule;
  mblk_t *msgDest;
  mblk_t *msgData;
  struct udppkt *udpheaders;

  if (!mp || !q) {
    cmn_err(CE_WARN, "inkudp_recv: Null parameters!\n");
    return 0;
  }

  if ((mp->b_datap->db_type != M_PROTO) || (!mp->b_cont) || (mp->b_cont->b_datap->db_type != M_DATA)) {
    cmn_err(CE_NOTE, "^Mystrey Message....\n");
    inkudp_dump_mblk(mp);
    putnext(q, mp);
    return 1;
  }

  if (!mutex_owned(&splitmx)) {
    mutex_enter(&splitmx);
    release_mutex = 1;
  }

  if (!redirect_enabled) {
    putnext(q, mp);

    if (release_mutex)
      mutex_exit(&splitmx);

    return 1;
  }

  if (!inkudp_get_pkt_ip_port(mp, &rule)) {
    /* What do you if the full packet isn't there??? */
    putnext(q, mp);

    if (release_mutex)
      mutex_exit(&splitmx);

    return 1;
  }

  inkudp_find_split_rule(q, &node, &rule);
  if (!node) {
    cmn_err(CE_CONT, "^got packet that doesn't belong from port = %d, queue = 0x%x\n", rule.srcPort, q);
    /* Doesn't belong to anything we split. So, simply put it back */
    putnext(q, mp);

    if (release_mutex)
      mutex_exit(&splitmx);

    return 1;
  }

  if (!mutex_owned(&node->list_mutex)) {
    release_list_mutex = 1;
    mutex_enter(&node->list_mutex);
    if (release_mutex)
      mutex_exit(&splitmx);
    release_mutex = 0;
  }
  list_node = node->redirect_nodes;

  while (list_node) {
#if 0
    if ((!list_node->destSession) || (!canputnext(list_node->destSession))) {
      list_node = list_node->next;
      continue;
    }
#endif
    if (!list_node->destSession) {
      list_node = list_node->next;
      continue;
    }
    /* destination block needs to be its own piece of memory */
    msgDest = copyb(mp);
    if (msgDest == NULL)
      continue;
    /* data block can be shared :) */
    msgData = dupmsg(mp->b_cont);
    if (msgData == NULL) {
      freeb(msgDest);
      continue;
    }

    /* link the destination and data blocks */
    msgDest->b_cont = msgData;

    udpheaders = (struct udppkt *) msgDest->b_rptr;

    /* make sure everything is set right for an outbound UDP message */
    inkudp_udppkt_init(udpheaders);
    udpheaders->ip = list_node->destIP;
    udpheaders->port = list_node->destPort;

    /* ideally, putnext should take care of it; but, for whatever
     * reason, the thing needs this run_queues nonsense
     */
    /* run_queues=1; */
    putnext(list_node->destSession, msgDest);
    /* queuerun(); */

    list_node = list_node->next;
  }
#if 1
  freemsg(mp);
#else
  putnext(q, mp);
#endif

#if 0
  if (redirect_passthrough) {
    putnext(q, mp);
  } else
    freemsg(mp);
#endif

  if (release_list_mutex)
    mutex_exit(&node->list_mutex);
  return 1;
}

/* Returns 1 on success and 0 on failure */
int
inkudp_get_pkt_ip_port(mblk_t * mp, struct fastIO_split_rule *rule)
{
  int mlen;
  u_char msgPkt[128];
  struct udp_recv_pkt *udpheaders;

  mlen = msgdsize(mp);
  if (mlen < sizeof(struct udp_recv_pkt))
    return 0;

  udpheaders = (struct udp_recv_pkt *) mp->b_rptr;

  if (!OK_32PTR(udpheaders)) {
    bcopy(mp->b_rptr, msgPkt, sizeof(struct udp_recv_pkt));
    udpheaders = (struct udp_recv_pkt *) msgPkt;
  }

  /* bcopy(src, dest, size); */
  bcopy(&(udpheaders->src_port), &(rule->srcPort), sizeof(uint16_t));
  bcopy(&(udpheaders->src_ip), &(rule->srcIP), sizeof(uint32_t));

  return 1;
}
