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

#if defined(solaris)
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/mman.h>

#include <netinet/in_systm.h>

#include "P_InkBulkIO.h"

struct InkBulkIOState {
  int biofd;
  void *sharedBuffer;
  int sharedBufferSize;
  InkBulkIOFreeBlockInfo_t freeList;
  struct InkBulkIOBlock *blockInfo;
  int numBlocks;
};

struct InkBulkIOSplit {
  char *header;
  int nbytes;
  struct InkBulkIOAddrInfo dest;
};

struct InkBulkIOAggregator {
  InkBulkIOAggregator()
  {
    metaReqCount      = 0;
    metablockInfo.ptr = nullptr;
    metablockInfo.id  = 0xffffffff;
    metablockReqPtr   = nullptr;

    lastReqFragCount = 0;
    lastReq          = nullptr;
    reqblockInfo.ptr = nullptr;
    reqblockInfo.id  = 0xffffffff;
    reqblockPktPtr   = nullptr;
  };
  struct InkBulkIOBlock metablockInfo;
  // Location where the next req. block id should be stuffed in the meta block.
  uint32_t *metablockReqPtr;
  uint32_t metaReqCount;
  struct InkBulkIOBlock reqblockInfo;
  // Location where the next packet should be stuffed in the req. block
  struct InkBulkIOPkt *reqblockPktPtr;
  // # of fragments in the last request.
  uint32_t lastReqFragCount;
  struct InkBulkIORequest *lastReq;
  void
  ResetLastRequestInfo()
  {
    lastReqFragCount = 0;
    lastReq          = nullptr;
    reqblockInfo.ptr = nullptr;
    reqblockInfo.id  = 0xffffffff;
    reqblockPktPtr   = nullptr;
  };
  void
  ResetMetaBlockInfo()
  {
    metaReqCount      = 0;
    metablockInfo.ptr = nullptr;
    metablockInfo.id  = 0xffffffff;
    metablockReqPtr   = nullptr;
  };
  bool
  AppendLastRequest()
  {
    if (metaReqCount >= INKBIO_MAX_REQS_PER_REQ_BLOCK)
      return false;

    memcpy(metablockReqPtr, &(reqblockInfo.id), sizeof(uint32_t));
    metablockReqPtr++;
    metaReqCount++;
    return true;
  };
  void
  TerminateMetaBlock()
  {
    *metablockReqPtr = 0xffffffff;
  };
  void
  TerminateLastRequest()
  {
    reqblockPktPtr->blockID  = 0xffffffff;
    reqblockPktPtr->pktsize  = 0xffff;
    reqblockPktPtr->inChain  = 0;
    reqblockPktPtr->reserved = 0;
  };
  void
  InitMetaBlock()
  {
    metablockReqPtr = (uint32_t *)metablockInfo.ptr;
    metaReqCount    = 0;
  };
  void
  InitSendtoReqBlock()
  {
    reqblockPktPtr                   = (struct InkBulkIOPkt *)((caddr_t)reqblockInfo.ptr + sizeof(InkBulkIORequest));
    lastReq                          = (struct InkBulkIORequest *)reqblockInfo.ptr;
    lastReq->reqType                 = INKBIO_SENDTO_REQUEST;
    lastReq->request.sendto.pktCount = 0;
    lastReqFragCount                 = 0;
  };
  void
  InitSplitReqBlock()
  {
    reqblockPktPtr                       = (struct InkBulkIOPkt *)((caddr_t)reqblockInfo.ptr + sizeof(InkBulkIORequest));
    lastReq                              = (struct InkBulkIORequest *)reqblockInfo.ptr;
    lastReq->reqType                     = INKBIO_SPLIT_REQUEST;
    lastReq->request.split.recvCount     = 0;
    lastReq->request.split.perDestHeader = 0;
    lastReqFragCount                     = 0;
  };
};

/*
 * Initialize the Bulk IO system and create a state cookie
 */
struct InkBulkIOState *BulkIOInit(int blockcount);
void BulkIOClose(struct InkBulkIOState *bioCookie);

int BulkIOBlkAlloc(struct InkBulkIOState *bioCookie, int blkCount, struct InkBulkIOBlock *bioResult);

int BulkIOAddPkt(struct InkBulkIOState *bioCookie, struct InkBulkIOAggregator *bioAggregator, UDPPacketInternal *pkt,
                 int sourcePort);

int BulkIOSplitPkt(struct InkBulkIOState *bioCookie, struct InkBulkIOAggregator *bioAggregator, UDPPacketInternal *pkt,
                   int sourcePort);

int BulkIOAppendToReqBlock(struct InkBulkIOState *bioCookie, struct InkBulkIOAggregator *bioAggregator, Ptr<IOBufferBlock> pkt);

void BulkIORequestComplete(struct InkBulkIOState *bioCookie, struct InkBulkIOAggregator *bioAggregator);

void BulkIOFlush(struct InkBulkIOState *bioCookie, struct InkBulkIOAggregator *bioAggregator);

void CopyFromIOBufferBlock(char *dest, Ptr<IOBufferBlock> pktChain, uint32_t nbytes);
#endif
