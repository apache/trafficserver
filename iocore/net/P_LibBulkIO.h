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


#ifndef _LIB_BULK_IO_H
#define _LIB_BULK_IO_H

#if (HOST_OS == sunos)
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

struct InkBulkIOState
{
  int biofd;
  void *sharedBuffer;
  int sharedBufferSize;
  InkBulkIOFreeBlockInfo_t freeList;
  struct InkBulkIOBlock *blockInfo;
  int numBlocks;
};

struct InkBulkIOSplit
{
  char *header;
  int nbytes;
  struct InkBulkIOAddrInfo dest;
};

struct InkBulkIOAggregator
{
  InkBulkIOAggregator()
  {
    m_metaReqCount = 0;
    m_metablockInfo.ptr = NULL;
    m_metablockInfo.id = 0xffffffff;
    m_metablockReqPtr = NULL;

    m_lastReqFragCount = 0;
    m_lastReq = NULL;
    m_reqblockInfo.ptr = NULL;
    m_reqblockInfo.id = 0xffffffff;
    m_reqblockPktPtr = NULL;

  };
  struct InkBulkIOBlock m_metablockInfo;
  // Location where the next req. block id should be stuffed in the meta block.
  uint32_t *m_metablockReqPtr;
  uint32_t m_metaReqCount;
  struct InkBulkIOBlock m_reqblockInfo;
  // Location where the next packet should be stuffed in the req. block
  struct InkBulkIOPkt *m_reqblockPktPtr;
  // # of fragments in the last request.
  uint32_t m_lastReqFragCount;
  struct InkBulkIORequest *m_lastReq;
  void ResetLastRequestInfo()
  {
    m_lastReqFragCount = 0;
    m_lastReq = NULL;
    m_reqblockInfo.ptr = NULL;
    m_reqblockInfo.id = 0xffffffff;
    m_reqblockPktPtr = NULL;
  };
  void ResetMetaBlockInfo()
  {
    m_metaReqCount = 0;
    m_metablockInfo.ptr = NULL;
    m_metablockInfo.id = 0xffffffff;
    m_metablockReqPtr = NULL;
  };
  bool AppendLastRequest()
  {
    if (m_metaReqCount >= INKBIO_MAX_REQS_PER_REQ_BLOCK)
      return false;

    memcpy(m_metablockReqPtr, &(m_reqblockInfo.id), sizeof(uint32_t));
    m_metablockReqPtr++;
    m_metaReqCount++;
    return true;
  };
  void TerminateMetaBlock()
  {
    *m_metablockReqPtr = 0xffffffff;
  };
  void TerminateLastRequest()
  {
    m_reqblockPktPtr->blockID = 0xffffffff;
    m_reqblockPktPtr->pktsize = 0xffff;
    m_reqblockPktPtr->inChain = 0;
    m_reqblockPktPtr->reserved = 0;
  };
  void InitMetaBlock()
  {
    m_metablockReqPtr = (uint32_t *) m_metablockInfo.ptr;
    m_metaReqCount = 0;
  };
  void InitSendtoReqBlock()
  {
    m_reqblockPktPtr = (struct InkBulkIOPkt *)
      ((caddr_t) m_reqblockInfo.ptr + sizeof(InkBulkIORequest));
    m_lastReq = (struct InkBulkIORequest *) m_reqblockInfo.ptr;
    m_lastReq->reqType = INKBIO_SENDTO_REQUEST;
    m_lastReq->request.sendto.pktCount = 0;
    m_lastReqFragCount = 0;
  };
  void InitSplitReqBlock()
  {
    m_reqblockPktPtr = (struct InkBulkIOPkt *)
      ((caddr_t) m_reqblockInfo.ptr + sizeof(InkBulkIORequest));
    m_lastReq = (struct InkBulkIORequest *) m_reqblockInfo.ptr;
    m_lastReq->reqType = INKBIO_SPLIT_REQUEST;
    m_lastReq->request.split.recvCount = 0;
    m_lastReq->request.split.perDestHeader = 0;
    m_lastReqFragCount = 0;
  };

};

/*
 * Initialize the Bulk IO system and create a state cookie
 */
struct InkBulkIOState *BulkIOInit(int blockcount);
void BulkIOClose(struct InkBulkIOState *bioCookie);

int BulkIOBlkAlloc(struct InkBulkIOState *bioCookie, int blkCount, struct InkBulkIOBlock *bioResult);

int BulkIOAddPkt(struct InkBulkIOState *bioCookie,
                 struct InkBulkIOAggregator *bioAggregator, UDPPacketInternal * pkt, int sourcePort);

int BulkIOSplitPkt(struct InkBulkIOState *bioCookie,
                   struct InkBulkIOAggregator *bioAggregator, UDPPacketInternal * pkt, int sourcePort);

int BulkIOAppendToReqBlock(struct InkBulkIOState *bioCookie,
                           struct InkBulkIOAggregator *bioAggregator, Ptr<IOBufferBlock> pkt);

int BulkIOSend(struct InkBulkIOState *bioCookie, uint32_t blkId);

void BulkIORequestComplete(struct InkBulkIOState *bioCookie, struct InkBulkIOAggregator *bioAggregator);

void BulkIOFlush(struct InkBulkIOState *bioCookie, struct InkBulkIOAggregator *bioAggregator);

void CopyFromIOBufferBlock(char *dest, Ptr<IOBufferBlock> pktChain, uint32_t nbytes);
#endif

#endif
