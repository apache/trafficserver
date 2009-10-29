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

#ifndef _INK_UDP_H
#define _INK_UDP_H

struct udp_recv_pkt
{
  char hdr[22];

  uint16_t src_port;
  uint32_t src_ip;
  char ftr[8];
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
int inkudp_create_redir_list_node(struct ink_redirect_list_node **list_node, struct fastIO_split_rule *rule);

int inkudp_find_split_rule(queue_t * incomingQ, struct ink_redirect_list **redir_node, struct fastIO_split_rule *rule);
int inkudp_find_add_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule);
int inkudp_delete_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule);

int inkudp_flush_split_rule(queue_t * incomingQ, struct fastIO_split_rule *rule);

int inkudp_get_pkt_ip_port(mblk_t * mp, struct fastIO_split_rule *rule);

int inkudp_get_bytes_stats(uint32_t * nbytesSent);
#endif
