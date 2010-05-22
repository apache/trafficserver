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
 * fastio.h
 *
 *
 * Data structures for FastIO
 *
 *
 *
 */

#ifndef FASTIO_H
#define FASTIO_H

#include "IncludeFiles.h"


  struct fastIO_state;
  struct fastIO_session;

#define BLOCK_INDEX_TYPE uint32_t
#define FASTIO_MAX_FLOWS 4
#define FASTIO_BLOCK_SIZE 1500
#define FASTIO_MAX_REQS_PER_REQ_BLOCK 100       /* for 1500-byte blocks */

/*
 * Describes a block of FastIO memory
 */
  struct fastIO_block
  {
    void *ptr;                  /* where is it at (in userland) */
    uint32_t id;
  };

/*
 * Describes a request header, part of a request block
 */
  struct fastIO_request
  {
    uint64_t startTime;
    uint32_t destIP;
    uint32_t destQ;
    uint16_t destPort;
    uint16_t pktCount;
  };

/*
 * Describes a packet to be sent.  Found after a request header in a request block
 */
  struct fastIO_pkt
  {
    uint32_t blockID;
    uint16_t pktsize;
    uint16_t delaydelta;
    uint16_t inChain:1;
    uint16_t reserved:15;
  };

  struct fastIO_split_rule
  {
    struct fastIO_session *splitTo;
    // XXX: We don't have sunos in configure
#if defined(sunos)
    /* FIXME: for other platfroms */
    queue_t *dst_queue;         /*internal use only */
#endif

    uint32_t flow_bw_weight;
    /* If src isn't specified, then take all the packets recd. for this
     * session */
    uint32_t srcIP;
    uint16_t srcPort;
    uint32_t dstIP;
    uint16_t dstPort;
    uint8_t flags;
  };

  struct ink_cmd_msg
  {
    uint32_t cmd;
    uint32_t id;
    union
    {
      uint32_t nbytesSent[FASTIO_MAX_FLOWS];
      struct fastIO_split_rule split_rule;
    } payload;
  };




/*
 * Structure for retreiving statistics information
 *
 */
  struct ink_fio_stats
  {

    /* Aggregate statistics */
    uint32_t pkts_sent;
    uint32_t bytes_sent;
    uint32_t xmit_failures;

    /* Session Statistics */
    uint32_t sessions_open;
    uint32_t vsessions_open;

    /* IOCTL Statistics */
    uint32_t metasend_requests;
    uint32_t sendto_requests;
    uint32_t swap_requests;
    uint32_t ioctl_requests;


    /* Vsession statistics */
    uint32_t vsession_pkts_sent;
    uint32_t vsession_bytes_sent;

    /* Packet Clock Performance */
    uint32_t kernel_timeout_requests;
  };

/*
 * Streams command message command types
 *
 */

#define INK_CMD_SPLIT_IOCTLMASK 0x100
#define INK_CMD_SPLIT_ADD      0x101
#define INK_CMD_SPLIT_DELETE   0x102
#define INK_CMD_SPLIT_FLUSH    0x103
#define INK_CMD_GET_BYTES_STATS 0x104
#define INK_CMD_NOSE_PICK      0x105

/*
 *
 *  Vsession-related Ioctl CMDs for the INKFIO module
 *
 */
#define INKFIO_VSESSION_MASK    0x200
#define INKFIO_VSESSION_CREATE  0x201
#define INKFIO_VSESSION_DESTROY 0x202
#define INKFIO_VSESSION_CMD     0x203

#define INKFIO_DEST_VSESSION    0xffffffff


/*
 * Ioctl CMD's
 */
#define INKUDP_INIT    0x0
#define INKUDP_SENDTO  0x1
#define INKUDP_SWAP    0x2
#define INKUDP_FINI    0x3
#define INKUDP_GETQ    0x4


#define FIO_INIT          0x0
#define FIO_SENDTO        0x1
#define FIO_SWAP          0x2
#define FIO_FINI          0x3
#define FIO_METASEND      0x4
#define FIO_GET_TIME_STAT 0x5
#define FIO_REG_SENDTO    0x6
#define FIO_DELETE_QUEUE  0x7
#define FIO_GET_STATS     0x8

/*
 * Ioctl status codes
 *
 */
#define INKUDP_SUCCESS        0x0
#define INKUDP_SENDTO_RETRY   0x1




#endif
