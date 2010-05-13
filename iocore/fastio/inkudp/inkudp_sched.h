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
#include <netinet/in.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#ifndef _INK_UDP_SCHED_H
#define _INK_UDP_SCHED_H

#define inline

#define HRTIME_FOREVER  (10*HRTIME_DECADE)
#define HRTIME_DECADE   (10*HRTIME_YEAR)
#define HRTIME_YEAR     (365*HRTIME_DAY+HRTIME_DAY/4)
#define HRTIME_WEEK     (7*HRTIME_DAY)
#define HRTIME_DAY      (24*HRTIME_HOUR)
#define HRTIME_HOUR     (60*HRTIME_MINUTE)
#define HRTIME_MINUTE   (60*HRTIME_SECOND)
#define HRTIME_SECOND   (1000*HRTIME_MSECOND)
#define HRTIME_MSECOND  (1000*HRTIME_USECOND)
#define HRTIME_USECOND  (1000*HRTIME_NSECOND)
#define HRTIME_NSECOND	(1LL)   /* 1LL */

#define HRTIME_YEARS(_x)    ((_x)*HRTIME_YEAR)
#define HRTIME_WEEKS(_x)    ((_x)*HRTIME_WEEK)
#define HRTIME_DAYS(_x)     ((_x)*HRTIME_DAY)
#define HRTIME_HOURS(_x)    ((_x)*HRTIME_HOUR)
#define HRTIME_MINUTES(_x)  ((_x)*HRTIME_MINUTE)
#define HRTIME_SECONDS(_x)  ((_x)*HRTIME_SECOND)
#define HRTIME_MSECONDS(_x) ((_x)*HRTIME_MSECOND)
#define HRTIME_USECONDS(_x) ((_x)*HRTIME_USECOND)
#define HRTIME_NSECONDS(_x) ((_x)*HRTIME_NSECOND)

#define HRTIME_TO_SECONDS(_x) ((_x) / HRTIME_SECOND)

struct udp_recv_pkt
{
  char hdr[22];

  uint16_t src_port;
  uint32_t src_ip;
  char ftr[8];
};


struct ink_recv_pktQ_node
{
  mblk_t *m_recvPkt;
  /* The time at which this packet has to be sent out */
  uint32_t m_start_xmitTime;
  uint32_t m_finish_xmitTime;
  /* the redirect list to which this packet belongs */
  struct ink_redirect_list *m_redir_list;
  /*
   * The meaning of next depends on where this node is placed: if it is in
   * the q of the incoming packets, then next points to the next packet of
   * the redir list of this packet; if it is in the queue of outgoing
   *  packets, next points to the next packet that needs to be sent; that
   * next packet can be on any redir list.
   */
  struct ink_recv_pktQ_node *m_next;
};

struct ink_recv_pktQ
{
  struct ink_recv_pktQ_node *m_head;
  struct ink_recv_pktQ_node *m_tail;
};


struct ink_redirect_list_node
{
  uint32_t destIP;
  uint16_t destPort;
  queue_t *destSession;
  /* Need to get rid of this */
  mblk_t *dst_mblk;             /* only in inkfio for vsession redirect lists */
  struct ink_redirect_list_node *next, *prev;

};

struct ink_redirect_list
{
  uint32_t srcIP;
  uint16_t srcPort;
  queue_t *incomingQ;
  uint32_t m_flow_bw_weight;
  uint32_t m_flow_bw_share;
  /* uint32_t nbytesSent; */
  uint32_t nbytesSent;

  /* Take this out */
  int can_add_clients;
  kmutex_t list_mutex;

  struct ink_recv_pktQ m_recvPktQ;

  uint16_t num_redirect_nodes;
  struct ink_redirect_list_node *redirect_nodes;
  struct ink_redirect_list *next, *prev;
};

int inkudp_ioctl_verify(mblk_t * mp, queue_t * q);
void inkudp_ioctl_ack(mblk_t * mp, queue_t * q);
void inkudp_ioctl_ack_retry(mblk_t * mp, queue_t * q);
void inkudp_free_cb(char *dat);

int inkudp_ioctl_sendto(mblk_t * mp, queue_t * q);
int inkudp_ioctl_init(mblk_t * mp, queue_t * q);
int inkudp_ioctl_swap(mblk_t * mp, queue_t * q);

int inkudp_ioctl_fini(mblk_t * mp, queue_t * q);

int inkudp_recv(mblk_t * mp, queue_t * q);


int inkudp_handle_cmsg(mblk_t * mp, queue_t * q);
void inkudp_udppkt_init(struct udppkt *p);


int inkudp_add_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule);
int
inkudp_create_redir_rule_node(queue_t * incomingQ,
                              struct ink_redirect_list **redir_rule, struct fastIO_split_rule *rule);

int inkudp_adjust_flow_bw_share(void);


int inkudp_create_redir_list_node(struct ink_redirect_list_node **list_node, struct fastIO_split_rule *rule);

int inkudp_find_split_rule(queue_t * incomingQ,
                           struct ink_redirect_list *rule_list,
                           struct ink_redirect_list **redir_node, struct fastIO_split_rule *rule);
int inkudp_find_add_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule);

int
inkudp_delete_split_rule_from_list(queue_t * incomingQ,
                                   struct ink_redirect_list *rule_list, struct fastIO_split_rule *rule);

int inkudp_delete_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule);

int inkudp_flush_split_rule_list(queue_t * incomingQ,
                                 struct ink_redirect_list **rule_list, struct fastIO_split_rule *rule);

int inkudp_flush_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule);

int inkudp_get_bytes_stats(uint32_t * nbytesSent);

int inkudp_get_pkt_ip_port(mblk_t * mp, struct fastIO_split_rule *rule);


int inkudp_create_recv_pktQ_node(struct ink_recv_pktQ_node **resultNode, mblk_t * mp);

int inkudp_enqueue_recv_pkt(mblk_t * mp, struct ink_redirect_list *redir_node);

int inkudp_create_recv_pktQ_node(struct ink_recv_pktQ_node **resultNode, mblk_t * mp);

void inkudp_send_pkts(void *ptr);

int inkudp_find_pkt_to_send(struct ink_redirect_list **incoming_list, struct ink_redirect_list **outgoing_list);


#endif
