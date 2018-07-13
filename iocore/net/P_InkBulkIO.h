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

#pragma once

#ifndef _KERNEL_
#include <netinet/ip.h>
#include <netinet/udp.h>
#endif

/*
 * We are following the convention of the ioctl cmd constants:
 *  - the first 8 bits contain the character representing the device
 *  - bits 8-15 refer to the ioctl
 */
#define INKBIO_IOC ('x' << 8) /* 'x' to represent 'xx' */

#define INKBIO_SEND (INKBIO_IOC | 1)
#define INKBIO_BALLOC (INKBIO_IOC | 2)

#define INKBIO_GET_STATS (INKBIO_IOC | 3)

#define INKBIO_NOP (INKBIO_IOC | 7)
#define INKBIO_MEMCPY (INKBIO_IOC | 8)

/* For ioctl's that are destined to the STREAMS module for getting at q ptrs */
#define INKBIO_REGISTER 1024

#define INKBIO_MAX_BLOCKS 512

/* 1500 bytes of data; 100 bytes for header */
#define INKBIO_MTU_SIZE 1500
#define INKBIO_PKT_SIZE_WITH_UDPHDR (INKBIO_MTU_SIZE - (sizeof(struct ip) + sizeof(struct udphdr)))
#define INKBIO_PKT_SIZE_WO_UDPHDR (INKBIO_MTU_SIZE - sizeof(struct ip))
/* 100 for ethernet and anything else; 20 for ip---every pkt got an ip header */
#define INKBIO_PKT_HEADER_SIZE (100 + sizeof(struct ip))
#define INKBIO_PKT_FOOTER_SIZE 0
#define INKBIO_BLOCK_SIZE (INKBIO_MTU_SIZE + INKBIO_PKT_HEADER_SIZE + INKBIO_PKT_FOOTER_SIZE)

#define INKBIO_MAX_UMEM_SIZE (INKBIO_BLOCK_SIZE * INKBIO_MAX_BLOCKS)
/*
 * Describes a block of BulkIO memory
 */
struct InkBulkIOBlock {
  void *ptr; /* where is it at */
  uint32_t id;
};

typedef struct {
  uint32_t nextFreeIdx;
  uint32_t numFreeBlocks;
  uint32_t freeBlockId[INKBIO_MAX_BLOCKS];
} InkBulkIOFreeBlockInfo_t;

/*
 * Describes a packet to be sent.  Found after a request header in a request block
 */
struct InkBulkIOPkt {
  uint32_t blockID;
  /* Set only in the first fragment of a chain.  Contains the size of the packet */
  uint32_t pktsize;
  /* If the thing is a chain, the size of the fragment */
  uint16_t fragsize;
  uint16_t inChain : 1;
  uint16_t reserved : 15;
};

struct InkBulkIOAddrInfo {
  uint32_t ip;
  uint16_t port;
};

/*
 * Format of a sendto request:
 *   - sender, receiver: ip/port info.
 *   - list of InkBulkIOPkt terminated by a 0xffffffff
 */
struct InkBulkIOSendtoRequest {
  /* declarations are done so that things in a req. block are usually 4-byte aligned */
  uint16_t pktCount;
  struct InkBulkIOAddrInfo src;
  struct InkBulkIOAddrInfo dest;
};

/*
 * Format of a split request:
 *   - sender: ip/port info. and count of # of receivers; also a boolean that
 *      describes if there is a per-receiver specific header that has to be
 *      tacked on before each data-payload.
 *   - a list of InkBulkIOPkt that describes the payload being split;
 *   - a list of tuples <receiver info, {optional InkBulkIOPkt}>
 *      terminate list by 0xffffffff
 */

struct InkBulkIOSplitRequest {
  /* declarations are done so that things in a req. block are usually 4-byte
   * aligned */
  uint16_t recvCount;
  struct InkBulkIOAddrInfo src;
  uint16_t perDestHeader; /* boolean */
};

/*
 * Describes a request header, part of a request block
 */
struct InkBulkIORequest {
  uint16_t reqType; /* one of sendto or split */
  union {
    struct InkBulkIOSendtoRequest sendto;
    struct InkBulkIOSplitRequest split;
  } request;
};

#define INKBIO_SENDTO_REQUEST 0x0a
#define INKBIO_SPLIT_REQUEST 0xf1

/*
 * Purposely, under specify the size; we need to leave space for the "terminating" packet.
 * Every block contains at least 1 request.
 */
#define INKBIO_MAX_PKTS_PER_REQ_BLOCK                                                              \
  ((INKBIO_PKT_SIZE_WO_UDPHDR - (sizeof(struct InkBulkIORequest) + sizeof(struct InkBulkIOPkt))) / \
   std::max((sizeof(struct InkBulkIORequest)), (sizeof(struct InkBulkIOPkt))))

/*
 * Requests are just block-ids---the block id points to the inkbio-block
 * that describes the request.
 */
#define INKBIO_MAX_REQS_PER_REQ_BLOCK ((INKBIO_PKT_SIZE_WO_UDPHDR - sizeof(uint32_t)) / sizeof(uint32_t))

#define INKBIO_MAX_FRAGS_PER_REQ_BLOCK INKBIO_MAX_PKTS_PER_REQ_BLOCK

/*
 * There is always 1 req. block and 1 pkt. block.  Next,
 * Leave space for 1 "nullptr" block for the Address information.
 */

#define INKBIO_MAX_SPLIT_WO_HDR_PER_SPLIT_BLOCK                                                           \
  ((INKBIO_PKT_SIZE_WO_UDPHDR -                                                                           \
    (sizeof(struct InkBulkIORequest) + sizeof(struct InkBulkIOPkt) + sizeof(struct InkBulkIOAddrInfo))) / \
   (sizeof(struct InkBulkIOAddrInfo)))

#define INKBIO_MAX_SPLIT_WITH_HDR_PER_SPLIT_BLOCK                                                         \
  ((INKBIO_PKT_SIZE_WO_UDPHDR -                                                                           \
    (sizeof(struct InkBulkIORequest) + sizeof(struct InkBulkIOPkt) + sizeof(struct InkBulkIOAddrInfo))) / \
   (sizeof(struct InkBulkIOPkt) + sizeof(struct InkBulkIOAddrInfo)))
