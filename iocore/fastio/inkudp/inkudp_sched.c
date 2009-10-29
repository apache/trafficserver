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
#include <sys/time.h>
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

#include "fastio.h"
#include "solstruct.h"
#include "inkudp_sched.h"

#define	MTYPE(m)	((m)->b_datap->db_type)

/* 90 is in Mbps: convert to bytes per 100 ms---a "round" is 100 ms long */
/* #define G_MAX_BYTES_PER_ROUND (((90.0 / 8.0) / 10.0) * 1024.0 * 1024.0) */
#define G_MAX_BYTES_PER_ROUND (((90 / 8) / 10) * 1024 * 1024)

#define G_PKT_SEND_TIMEOUT_MSEC 100
#define G_PKT_SEND_TIMEOUT_USEC (G_PKT_SEND_TIMEOUT_MSEC * 1000)

extern int redirect_enabled;
extern int redirect_passthrough;
extern struct ink_redirect_list *G_redirect_incoming_list;
extern struct ink_redirect_list *G_redirect_outgoing_list;
extern kmutex_t G_incoming_splitmx;
extern kmutex_t G_outgoing_splitmx;
uint32_t G_sfq_virtual_clock;

queue_t *fio_lookup_queue(int qid);

typedef struct udphdr udphdr_t;
typedef struct ip ip_t;

uint32_t G_num_timeouts;
int G_timeout_id;
hrtime_t G_lastStatPrintTime;

int
inkudp_create_redir_rule_node(queue_t * incomingQ,
                              struct ink_redirect_list **redir_rule, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *node;
  struct ink_redirect_list_node *list_node;

  node = kmem_alloc(sizeof(struct ink_redirect_list), 0);
  if (!node) {
    cmn_err(CE_WARN, "inkudp_handle_cmsg: Out of memory.\n");
    return 0;
  }
  node->srcIP = rule->srcIP;
  node->srcPort = rule->srcPort;
  node->incomingQ = incomingQ;
  node->num_redirect_nodes = 0;
  node->nbytesSent = 0;
  node->m_flow_bw_weight = rule->flow_bw_weight;
  node->m_recvPktQ.m_head = node->m_recvPktQ.m_tail = NULL;

  inkudp_create_redir_list_node(&list_node, rule);

  if (!list_node) {
    kmem_free(node, sizeof(struct ink_redirect_list));
    return 0;
  }

  node->num_redirect_nodes++;
  node->redirect_nodes = list_node;

  if (!(*redir_rule)) {
    node->next = node->prev = NULL;
    *redir_rule = node;
    return 1;
  }

  /* put it to the head of the list */
  node->prev = NULL;
  node->next = *redir_rule;
  (*redir_rule)->prev = node;
  *redir_rule = node;
  return 1;
}

int
inkudp_adjust_flow_bw_share()
{
  struct ink_redirect_list *node;
  int total_flow_weights = 0;

  node = G_redirect_incoming_list;
  while (node) {
    total_flow_weights += node->m_flow_bw_weight;
    node = node->next;
  }

  node = G_redirect_incoming_list;
  while (node) {
    node->m_flow_bw_share = (G_MAX_BYTES_PER_ROUND * node->m_flow_bw_weight) / total_flow_weights;
#if 0
    cmn_err(CE_NOTE, "setting flow's bw share: %d, %d\n", node->m_flow_bw_weight, node->m_flow_bw_share);
#endif
    node = node->next;
  }
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

/**
  Add the specified splitting rule.

  @return 0 on error.

*/
int
inkudp_add_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule)
{
  int status;
  int release_incoming_mutex = 0;
  int release_outgoing_mutex = 0;

  if (!mutex_owned(&G_incoming_splitmx)) {
    mutex_enter(&G_incoming_splitmx);
    release_incoming_mutex = 1;
  }

  if (!mutex_owned(&G_outgoing_splitmx)) {
    mutex_enter(&G_outgoing_splitmx);
    release_outgoing_mutex = 1;
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
    if (release_incoming_mutex)
      mutex_exit(&G_incoming_splitmx);
    if (release_outgoing_mutex)
      mutex_exit(&G_outgoing_splitmx);

    return status;
  }

  if ((status = inkudp_create_redir_rule_node(incomingQ, &G_redirect_incoming_list, rule)) <= 0) {
    if (release_incoming_mutex)
      mutex_exit(&G_incoming_splitmx);
    if (release_outgoing_mutex)
      mutex_exit(&G_outgoing_splitmx);

    return status;
  }

  if ((status = inkudp_create_redir_rule_node(incomingQ, &G_redirect_outgoing_list, rule)) <= 0) {

    inkudp_flush_split_rule_list(incomingQ, &G_redirect_incoming_list, rule);
    if (release_incoming_mutex)
      mutex_exit(&G_incoming_splitmx);
    if (release_outgoing_mutex)
      mutex_exit(&G_outgoing_splitmx);

    return status;
  }

  inkudp_adjust_flow_bw_share();

  if (release_incoming_mutex)
    mutex_exit(&G_incoming_splitmx);
  if (release_outgoing_mutex)
    mutex_exit(&G_outgoing_splitmx);


  return 1;
}


/**
  Assumes that the rule_list is already locked; finds a split rule from
  the list of redirection rules.

  @return 1 if there is a valid rule; 0 otherwise

*/
int
inkudp_find_split_rule(queue_t * incomingQ,
                       struct ink_redirect_list *rule_list,
                       struct ink_redirect_list **redir_node, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *node;

  node = rule_list;
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

/**
  Assumes that the rule_list is already locked.

  @return 1 if the addition to appropriate redir--list is successful; 0
    if there is an error; -1 if the appropriate redir. List isn't there =>
    create one and add the thing.

*/
int
inkudp_find_add_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *node1, *node2;
  struct ink_redirect_list_node *list_node1, *list_node2;

  inkudp_find_split_rule(incomingQ, G_redirect_incoming_list, &node1, rule);
  if (!node1)
    /* the appropriate redir. list needs to be created */
    return -1;
  inkudp_find_split_rule(incomingQ, G_redirect_outgoing_list, &node2, rule);
  if (!node2)
    /* This is impossible given that the lists are identical.  Just defense */
    return -1;

  /* found the right list! */
  inkudp_create_redir_list_node(&list_node1, rule);
  if (!list_node1)
    /* something bad happened */
    return 0;

  inkudp_create_redir_list_node(&list_node2, rule);
  if (!list_node2) {
    kmem_free(list_node1, sizeof(struct ink_redirect_list_node));
    /* something bad happened */
    return 0;
  }
  /* Now that we got the memory for the rules, stick them in both the
     incoming as well as outgoing lists
   */

  node1->num_redirect_nodes++;
  list_node1->prev = NULL;
  list_node1->next = node1->redirect_nodes;
  node1->redirect_nodes->prev = list_node1;
  node1->redirect_nodes = list_node1;

  node2->num_redirect_nodes++;
  list_node2->prev = NULL;
  list_node2->next = node2->redirect_nodes;
  node2->redirect_nodes->prev = list_node2;
  node2->redirect_nodes = list_node2;

  /* yeah! we succeded */
  return 1;
}

/** Assumes that the rule list is already locked. */
int
inkudp_delete_split_rule_from_list(queue_t * incomingQ,
                                   struct ink_redirect_list *rule_list, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *node;
  struct ink_redirect_list_node *list_node;

  inkudp_find_split_rule(incomingQ, rule_list, &node, rule);

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
      node->num_redirect_nodes--;
      kmem_free(list_node, sizeof(struct ink_redirect_list_node));
      return 1;
    }
    list_node = list_node->next;
  }
  cmn_err(CE_NOTE, "inkudp_delete_split_rule: Unable to find requested split rule in database.\n");
  return 0;                     /* failure */
}

/**
  Remove the specified splitting rule.

  @return 0 on failure, nonzero on success.

*/
int
inkudp_delete_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule)
{
  int release_incoming_mutex = 0;
  int release_outgoing_mutex = 0;

  if (!mutex_owned(&G_incoming_splitmx)) {
    mutex_enter(&G_incoming_splitmx);
    release_incoming_mutex = 1;
  }

  if (!mutex_owned(&G_outgoing_splitmx)) {
    mutex_enter(&G_outgoing_splitmx);
    release_outgoing_mutex = 1;
  }

  inkudp_delete_split_rule_from_list(incomingQ, G_redirect_incoming_list, rule);
  inkudp_delete_split_rule_from_list(incomingQ, G_redirect_outgoing_list, rule);

  if (release_incoming_mutex)
    mutex_exit(&G_incoming_splitmx);
  if (release_outgoing_mutex)
    mutex_exit(&G_outgoing_splitmx);

  return 1;
}

/**
  Remove the redirect list for the given split rule. Assumes that the
  mutex associated with the rule_list is held.

  @return 0 on failure, nonzero on success.

*/
int
inkudp_flush_split_rule_list(queue_t * incomingQ, struct ink_redirect_list **rule_list, struct fastIO_split_rule *rule)
{
  struct ink_redirect_list *node = NULL;
  struct ink_redirect_list_node *list_node;
  struct ink_recv_pktQ_node *recvPkt;

  inkudp_find_split_rule(incomingQ, *rule_list, &node, rule);

  if (!node)
    /* trying to delete something that doesn't exist... */
    return 0;

  /* Free all the queued packets */
  while (node->m_recvPktQ.m_head) {
    recvPkt = node->m_recvPktQ.m_head;
    node->m_recvPktQ.m_head = node->m_recvPktQ.m_head->m_next;
    freemsg(recvPkt->m_recvPkt);
    kmem_free(recvPkt, sizeof(struct ink_recv_pktQ_node));
  }

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
  if (node == *rule_list)
    *rule_list = node->next;

  kmem_free(node, sizeof(struct ink_redirect_list));
  return 1;
}

int
inkudp_flush_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule)
{
  int release_incoming_mutex = 0;
  int release_outgoing_mutex = 0;

  if (!mutex_owned(&G_incoming_splitmx)) {
    mutex_enter(&G_incoming_splitmx);
    release_incoming_mutex = 1;
  }

  if (!mutex_owned(&G_outgoing_splitmx)) {
    mutex_enter(&G_outgoing_splitmx);
    release_outgoing_mutex = 1;
  }

  inkudp_flush_split_rule_list(incomingQ, &G_redirect_incoming_list, rule);
  inkudp_flush_split_rule_list(incomingQ, &G_redirect_outgoing_list, rule);

  inkudp_adjust_flow_bw_share();

  if (release_incoming_mutex)
    mutex_exit(&G_incoming_splitmx);
  if (release_outgoing_mutex)
    mutex_exit(&G_outgoing_splitmx);

  return 1;
}

/**
  Process a control message. These messages sometimes contain important
  data, so we should make sure they are all ignored.

  @return 0 on failure, nonzero on success.

*/
int
inkudp_handle_cmsg(mblk_t * mp, queue_t * q)
{
  struct ink_cmd_msg *cmsg;

  cmsg = (struct ink_cmd_msg *) mp->b_rptr;

  switch (cmsg->cmd) {

  case INK_CMD_SPLIT_ADD:
    inkudp_add_split_rule(q, &(cmsg->payload.split_rule));
    break;
  case INK_CMD_SPLIT_DELETE:
    cmn_err(CE_CONT, "deleting split rule for port = %d, q = 0x%x\n", cmsg->payload.split_rule.srcPort, q);

    inkudp_delete_split_rule(q, &cmsg->payload.split_rule);
    break;

  case INK_CMD_SPLIT_FLUSH:
    cmn_err(CE_CONT, "flushing split rule for port = %d, q = 0x%x\n", cmsg->payload.split_rule.srcPort, q);

    inkudp_flush_split_rule(q, &cmsg->payload.split_rule);
    break;

  case INK_CMD_GET_BYTES_STATS:
    inkudp_get_bytes_stats(cmsg->payload.nbytesSent);
    break;

  case INK_CMD_NOSE_PICK:
    cmn_err(CE_CONT, "inkudp_handle_cmsg: Ewww.  That's disgusting.\n");
    break;
  default:
    cmn_err(CE_WARN, "inkudp_handle_cmsg: Unsupported or unrecognized control command.\n");
    return 0;                   /* error */
  }

  return 1;                     /* success */
}


int
inkudp_get_bytes_stats(uint32_t * nbytesSent)
{
  int release_incoming_mutex = 0, i;
  struct ink_redirect_list *outgoing_list, *incoming_list;

  return;

  if (!mutex_owned(&G_incoming_splitmx)) {
    mutex_enter(&G_incoming_splitmx);
    release_incoming_mutex = 1;
  }

  incoming_list = G_redirect_incoming_list;

  for (i = 0; i < FASTIO_MAX_FLOWS; i++)
    nbytesSent[i] = 0;

  while (incoming_list) {
    if (incoming_list->m_flow_bw_weight < FASTIO_MAX_FLOWS)
      nbytesSent[incoming_list->m_flow_bw_weight] += incoming_list->nbytesSent;
    incoming_list->nbytesSent = 0;
    incoming_list = incoming_list->next;
  }
  for (i = 0; i < 4; i++) {
    cmn_err(CE_NOTE, "flow = %d, bytes = %d", i, nbytesSent[i]);
  }

  if (release_incoming_mutex)
    mutex_exit(&G_incoming_splitmx);
  return 1;
}

/** @return 1 on success and 0 on failure. */
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

int
inkudp_create_recv_pktQ_node(struct ink_recv_pktQ_node **resultNode, mblk_t * mp)
{
  struct ink_recv_pktQ_node *node;

  node = kmem_alloc(sizeof(struct ink_recv_pktQ_node), 0);
  if (!node) {
    *resultNode = NULL;
    return 0;
  }
  node->m_recvPkt = mp;
  node->m_start_xmitTime = 0;
  node->m_finish_xmitTime = 0;
  node->m_next = NULL;
  *resultNode = node;
  return 1;
}

/** Process inbound packets. */
int
inkudp_recv(mblk_t * mp, queue_t * q)
{

  int release_mutex = 0, pktLen;
  uint32_t finishTag;
  struct ink_redirect_list *node;
  struct fastIO_split_rule rule;
  struct ink_recv_pktQ_node *recv_pktQ_node;

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

  if (!mutex_owned(&G_incoming_splitmx)) {
    mutex_enter(&G_incoming_splitmx);
    release_mutex = 1;
  }

  if (!redirect_enabled) {
    putnext(q, mp);

    if (release_mutex)
      mutex_exit(&G_incoming_splitmx);

    return 1;
  }

  if (!inkudp_get_pkt_ip_port(mp, &rule)) {
    /* What do you if the full packet isn't there??? */
    putnext(q, mp);

    if (release_mutex)
      mutex_exit(&G_incoming_splitmx);

    return 1;
  }

  inkudp_find_split_rule(q, G_redirect_incoming_list, &node, &rule);
  if (!node) {
    /* Doesn't belong to anything we split. So, simply put it back */
    putnext(q, mp);

    if (release_mutex)
      mutex_exit(&G_incoming_splitmx);

    return 1;
  }

  inkudp_create_recv_pktQ_node(&recv_pktQ_node, mp);

  if (!recv_pktQ_node) {
    /* Problem: we are out of memory. So, floor the packet */
    freemsg(mp);
    if (release_mutex)
      mutex_exit(&G_incoming_splitmx);
    return 0;
  }

  /* convert it to bits */
  pktLen = msgdsize(mp) * 8;

  /* Enqueue the packet */
  if (node->m_recvPktQ.m_tail) {
    finishTag = node->m_recvPktQ.m_tail->m_finish_xmitTime;
    node->m_recvPktQ.m_tail->m_next = recv_pktQ_node;
  } else
    finishTag = 0;

  node->m_recvPktQ.m_tail = recv_pktQ_node;
  if (node->m_recvPktQ.m_head == NULL)
    node->m_recvPktQ.m_head = node->m_recvPktQ.m_tail;

  /* Compute the transmission time here */
  recv_pktQ_node->m_start_xmitTime = MAX(G_sfq_virtual_clock, finishTag);

  recv_pktQ_node->m_finish_xmitTime = recv_pktQ_node->m_start_xmitTime + ((pktLen * 1000) / node->m_flow_bw_share);

  if (G_timeout_id == 0) {
    cmn_err(CE_NOTE, "recv: setting timeout for: %d usec (%d hz)\n",
            G_PKT_SEND_TIMEOUT_USEC, drv_usectohz(G_PKT_SEND_TIMEOUT_USEC));

    G_timeout_id = timeout(inkudp_send_pkts, (caddr_t) NULL, drv_usectohz(G_PKT_SEND_TIMEOUT_USEC));
  }

  if (release_mutex)
    mutex_exit(&G_incoming_splitmx);
  /* We are done! */
  return 1;
}

/** Callback function that sends out packets. */
void
inkudp_send_pkts(void *ptr)
{
  int release_incoming_mutex = 0, release_outgoing_mutex = 0;
  struct ink_redirect_list *outgoing_list, *incoming_list;
  struct ink_redirect_list_node *list_node;
  mblk_t *mp;
  mblk_t *msgDest;
  mblk_t *msgData;
  struct udppkt *udpheaders;
  struct ink_recv_pktQ_node *recv_pktQ_node;
  struct ink_recv_pktQ pkt_xmit_Q;
  int pktSize, nbytesSent;
  int i, nSec;
  int flow_bw_vals[4];
  hrtime_t now;

  pkt_xmit_Q.m_head = pkt_xmit_Q.m_tail = NULL;

  if (!mutex_owned(&G_incoming_splitmx)) {
    mutex_enter(&G_incoming_splitmx);
    release_incoming_mutex = 1;
  }
  if (!mutex_owned(&G_outgoing_splitmx)) {
    mutex_enter(&G_outgoing_splitmx);
    release_outgoing_mutex = 1;
  }

  outgoing_list = G_redirect_outgoing_list;
  incoming_list = G_redirect_incoming_list;

  /* The incoming and outgoing lists are identical.  That is, if node is in
   * the incoming list, a copy of the same node is in the outgoing list.
   * Here, we compute the transmission order and update the virtual clock values.
   */
  nbytesSent = 0;
  while (nbytesSent < G_MAX_BYTES_PER_ROUND) {
    if (!inkudp_find_pkt_to_send(&incoming_list, &outgoing_list))
      break;

    if (!incoming_list->m_recvPktQ.m_head)
      /* what is going on??? */
      continue;

    /* Take this packet */
    recv_pktQ_node = incoming_list->m_recvPktQ.m_head;

    if (!recv_pktQ_node->m_recvPkt)
      continue;

    pktSize = msgdsize(recv_pktQ_node->m_recvPkt);

    /* need to multiply this by the # of copies we are going to send */
    nbytesSent += (incoming_list->num_redirect_nodes * pktSize);
    outgoing_list->nbytesSent += (incoming_list->num_redirect_nodes * pktSize);
    incoming_list->nbytesSent += (incoming_list->num_redirect_nodes * pktSize);

    /* Remove the packet from the incoming list. The order of the
       statements is critical: recv_pktQ_node is an alias for the head
       ptr; if we change recv_pktQ_node->m_next before adjusting the head
       ptr, we are in trouble.
     */
    incoming_list->m_recvPktQ.m_head = incoming_list->m_recvPktQ.m_head->m_next;
    if (!incoming_list->m_recvPktQ.m_head)
      incoming_list->m_recvPktQ.m_tail = NULL;

    recv_pktQ_node->m_next = NULL;
    recv_pktQ_node->m_redir_list = outgoing_list;

    /* Add the pkt block to the transmission queue */
    if (pkt_xmit_Q.m_tail)
      pkt_xmit_Q.m_tail->m_next = recv_pktQ_node;
    pkt_xmit_Q.m_tail = recv_pktQ_node;
    if (!pkt_xmit_Q.m_head)
      pkt_xmit_Q.m_head = recv_pktQ_node;

  }
  if (pkt_xmit_Q.m_tail)
    G_sfq_virtual_clock = pkt_xmit_Q.m_tail->m_finish_xmitTime;

  now = gethrtime();
  if (G_lastStatPrintTime == 0)
    G_lastStatPrintTime = now;

  nSec = (int) HRTIME_TO_SECONDS(now - G_lastStatPrintTime);

  /* nSec = 0; */

  /* The app. will query and print the stats---this isn't working as yet */
  if (G_num_timeouts && (nSec >= 5)) {
    incoming_list = G_redirect_incoming_list;

    for (i = 0; i < 4; i++)
      flow_bw_vals[i] = 0;

    while (incoming_list) {
      if (incoming_list->m_flow_bw_weight < 4)
        flow_bw_vals[incoming_list->m_flow_bw_weight] += incoming_list->nbytesSent;
      incoming_list->nbytesSent = 0;
      incoming_list = incoming_list->next;
    }
    for (i = 0; i < 4; i++) {
      if (flow_bw_vals[i])
        cmn_err(CE_NOTE, "Thruput: nsec = %d  wt = %d, bytes = %d is: %d (Mbps)",
                nSec, i, flow_bw_vals[i], ((flow_bw_vals[i] * 8) / (1024 * 1024)) / nSec);

    }
    G_lastStatPrintTime = now;
    G_num_timeouts = 0;
  }


  if (release_incoming_mutex)
    mutex_exit(&G_incoming_splitmx);

  while (pkt_xmit_Q.m_head) {
    recv_pktQ_node = pkt_xmit_Q.m_head;
    pkt_xmit_Q.m_head = pkt_xmit_Q.m_head->m_next;

    if (!recv_pktQ_node->m_recvPkt) {
      /* huh? */
      kmem_free(recv_pktQ_node, sizeof(struct ink_recv_pktQ_node));
      continue;
    }
    mp = recv_pktQ_node->m_recvPkt;
    if (!recv_pktQ_node->m_redir_list) {
      /* what happened??? */
      freemsg(mp);
      /* De-alloc memory */
      kmem_free(recv_pktQ_node, sizeof(struct ink_recv_pktQ_node));
      continue;
    }
    list_node = recv_pktQ_node->m_redir_list->redirect_nodes;

    while (list_node) {
      if (!list_node->destSession) {
        list_node = list_node->next;
        continue;
      }
      /* destination block needs to be its own piece of memory */
      msgDest = copyb(mp);
      if (msgDest == NULL) {
        list_node = list_node->next;
        continue;
      }
      /* data block can be shared :) */
      msgData = dupmsg(mp->b_cont);
      if (msgData == NULL) {
        freeb(msgDest);
        list_node = list_node->next;
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
    /* May need to teeup the data to the application */
    freemsg(mp);

    /* De-alloc memory */
    kmem_free(recv_pktQ_node, sizeof(struct ink_recv_pktQ_node));
  }

  G_num_timeouts++;

  if (nbytesSent)
    G_timeout_id = timeout(inkudp_send_pkts, (caddr_t) NULL, drv_usectohz(G_PKT_SEND_TIMEOUT_USEC));
  else
    G_timeout_id = 0;

  if (release_outgoing_mutex)
    mutex_exit(&G_outgoing_splitmx);
}

/** Assumes that the outgoing splitmx is already held. */
int
inkudp_find_pkt_to_send(struct ink_redirect_list **incoming_list, struct ink_redirect_list **outgoing_list)
{
  struct ink_redirect_list *min_list, *cur_list;
  struct ink_redirect_list *temp_list;

  min_list = NULL;
  cur_list = G_redirect_incoming_list;

  /* 
   *  Initialize the min. element.  Order here is critical: in the next loop,
   * we are deref'ing min_list->m_recvPktQ.m_head; better make sure that the
   * m_head pointer is not NULL.
   */
  while ((min_list == NULL) && (cur_list)) {
    if (cur_list->m_recvPktQ.m_head)
      min_list = cur_list;
    cur_list = cur_list->next;
  }
  /* Find the packet with the min. transmission time */
  while (cur_list) {
    if ((cur_list->m_recvPktQ.m_head) &&
        (cur_list->m_recvPktQ.m_head->m_start_xmitTime < min_list->m_recvPktQ.m_head->m_start_xmitTime))
      min_list = cur_list;
    cur_list = cur_list->next;
  }
  *incoming_list = min_list;
  if (!min_list) {
    /* didn't get anything */
    *outgoing_list = NULL;
    return 0;
  }
  /* Now that we got the  packet in the incoming list, find the matching
     rule in the outgoing list.
   */
  temp_list = G_redirect_outgoing_list;
  while (temp_list) {
    if ((temp_list->srcIP == min_list->srcIP) &&
        (temp_list->srcPort == min_list->srcPort) && (temp_list->incomingQ == min_list->incomingQ)) {
      *outgoing_list = temp_list;
      break;
    }
    temp_list = temp_list->next;
  }
  /* Defense here */
  if (!temp_list) {
    cmn_err(CE_WARN, "inkudp: found incoming link; but no outgoing!\n");
    *incoming_list = *outgoing_list = NULL;
    return 0;
  }
  return 1;
}
