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

  P_UDPPacket.h
  Implementation of UDPPacket


 ****************************************************************************/


#ifndef __P_UDPPPACKET_H_
#define __P_UDPPPACKET_H_

#include "I_UDPNet.h"

//#define PACKETQUEUE_IMPL_AS_PQLIST
#define PACKETQUEUE_IMPL_AS_RING

class UDPPacketInternal:public UDPPacket
{

public:
  UDPPacketInternal();
  virtual ~ UDPPacketInternal();

  void append_bytes(char *buf, int len);
  void append_block_internal(IOBufferBlock * block);

  virtual void free();

  SLINK(UDPPacketInternal, alink);  // atomic link
  // packet scheduling stuff: keep it a doubly linked list
  uint64_t pktSendStartTime;
  uint64_t pktSendFinishTime;
  uint64_t pktLength;

  bool isReliabilityPkt;

  int reqGenerationNum;
  ink_hrtime delivery_time;   // when to deliver packet
  ink_hrtime arrival_time;    // when packet arrived

  Ptr<IOBufferBlock> chain;
  Continuation *cont;         // callback on error
  UDPConnectionInternal *conn;        // connection where packet should be sent to.

#if defined(PACKETQUEUE_IMPL_AS_PQLIST) || defined(PACKETQUEUE_IMPL_AS_RING)
  int in_the_priority_queue;
  int in_heap;
#endif

  virtual void UDPPacket_is_abstract() { }
};

inkcoreapi extern ClassAllocator<UDPPacketInternal> udpPacketAllocator;

TS_INLINE
UDPPacketInternal::UDPPacketInternal()
  : pktSendStartTime(0), pktSendFinishTime(0), pktLength(0), isReliabilityPkt(false),
    reqGenerationNum(0), delivery_time(0), arrival_time(0), cont(NULL) , conn(NULL)
#if defined(PACKETQUEUE_IMPL_AS_PQLIST) || defined(PACKETQUEUE_IMPL_AS_RING)
  ,in_the_priority_queue(0), in_heap(0)
#endif
{
  memset(&from, '\0', sizeof(from));
  memset(&to, '\0', sizeof(to));
}

TS_INLINE
UDPPacketInternal::~
UDPPacketInternal()
{
  chain = NULL;
}

TS_INLINE void
UDPPacketInternal::free()
{
  chain = NULL;
  if (conn)
    conn->Release();
  conn = NULL;
  udpPacketAllocator.free(this);
}

TS_INLINE void
UDPPacketInternal::append_bytes(char *buf, int len)
{
  IOBufferData *d = NULL;
  if (buf) {
    d = new_xmalloc_IOBufferData(buf, len);
    append_block(new_IOBufferBlock(d, len));
  }
}

TS_INLINE void
UDPPacket::setReliabilityPkt()
{
  UDPPacketInternal *p = (UDPPacketInternal *) this;

  p->isReliabilityPkt = true;
}

TS_INLINE void
UDPPacket::append_block(IOBufferBlock * block)
{
  UDPPacketInternal *p = (UDPPacketInternal *) this;

  if (block) {
    if (p->chain) {           // append to end
      IOBufferBlock *last = p->chain;
      while (last->next != NULL) {
        last = last->next;
      }
      last->next = block;
    } else {
      p->chain = block;
    }
  }
}

TS_INLINE int64_t
UDPPacket::getPktLength()
{
  UDPPacketInternal *p = (UDPPacketInternal *) this;
  IOBufferBlock *b;

  p->pktLength = 0;
  b = p->chain;
  while (b) {
    p->pktLength += b->read_avail();
    b = b->next;
  }
  return p->pktLength;
}

TS_INLINE void
UDPPacket::free()
{
  ((UDPPacketInternal *) this)->free();
}

TS_INLINE void
UDPPacket::setContinuation(Continuation * c)
{
  ((UDPPacketInternal *) this)->cont = c;
}

TS_INLINE void
UDPPacket::setConnection(UDPConnection * c)
{
  /*Code reviewed by Case Larsen.  Previously, we just had
     ink_assert(!conn).  This prevents tunneling of packets
     correctly---that is, you get packets from a server on a udp
     conn. and want to send it to a player on another connection, the
     assert will prevent that.  The "if" clause enables correct
     handling of the connection ref. counts in such a scenario. */

  UDPConnectionInternal *&conn = ((UDPPacketInternal *) this)->conn;

  if (conn) {
    if (conn == c)
      return;
    conn->Release();
    conn = NULL;
  }
  conn = (UDPConnectionInternal *) c;
  conn->AddRef();
}

TS_INLINE IOBufferBlock *
UDPPacket::getIOBlockChain(void)
{
  return ((UDPPacketInternal *) this)->chain;
}

TS_INLINE UDPConnection *
UDPPacket::getConnection(void)
{
  return ((UDPPacketInternal *) this)->conn;
}

TS_INLINE void
UDPPacket::setArrivalTime(ink_hrtime t)
{
  ((UDPPacketInternal *) this)->arrival_time = t;
}

TS_INLINE UDPPacket *
new_UDPPacket(struct sockaddr const* to, ink_hrtime when, char *buf, int len)
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();

#if defined(PACKETQUEUE_IMPL_AS_PQLIST) || defined(PACKETQUEUE_IMPL_AS_RING)
  p->in_the_priority_queue = 0;
  p->in_heap = 0;
#endif
  p->delivery_time = when;
  ats_ip_copy(&p->to, to);

  if (buf) {
    IOBufferBlock *body = new_IOBufferBlock();
    body->alloc(iobuffer_size_to_index(len));
    memcpy(body->end(), buf, len);
    body->fill(len);
    p->append_block(body);
  }

  return p;
}

TS_INLINE UDPPacket *
new_UDPPacket(struct sockaddr const* to, ink_hrtime when, IOBufferBlock * buf, int len)
{
  (void) len;
  UDPPacketInternal *p = udpPacketAllocator.alloc();
  IOBufferBlock *body;

#if defined(PACKETQUEUE_IMPL_AS_PQLIST) || defined(PACKETQUEUE_IMPL_AS_RING)
  p->in_the_priority_queue = 0;
  p->in_heap = 0;
#endif
  p->delivery_time = when;
  ats_ip_copy(&p->to, to);

  while (buf) {
    body = buf->clone();
    p->append_block(body);
    buf = buf->next;
  }
  return p;
}

TS_INLINE UDPPacket *
new_UDPPacket(struct sockaddr const* to, ink_hrtime when, Ptr<IOBufferBlock> buf)
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();

#if defined(PACKETQUEUE_IMPL_AS_PQLIST) || defined(PACKETQUEUE_IMPL_AS_RING)
  p->in_the_priority_queue = 0;
  p->in_heap = 0;
#endif
  p->delivery_time = when;
  if (to)
    ats_ip_copy(&p->to, to);
  p->chain = buf;
  return p;
}

TS_INLINE UDPPacket *
new_UDPPacket(ink_hrtime when, Ptr<IOBufferBlock> buf)
{
  return new_UDPPacket(NULL, when, buf);
}

TS_INLINE UDPPacket *
new_incoming_UDPPacket(struct sockaddr * from, char *buf, int len)
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();

#if defined(PACKETQUEUE_IMPL_AS_PQLIST) || defined(PACKETQUEUE_IMPL_AS_RING)
  p->in_the_priority_queue = 0;
  p->in_heap = 0;
#endif
  p->delivery_time = 0;
  ats_ip_copy(&p->from, from);

  IOBufferBlock *body = new_IOBufferBlock();
  body->alloc(iobuffer_size_to_index(len));
  memcpy(body->end(), buf, len);
  body->fill(len);
  p->append_block(body);

  return p;
}

TS_INLINE UDPPacket *
new_UDPPacket()
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();
  return p;
}

#endif //__P_UDPPPACKET_H_
