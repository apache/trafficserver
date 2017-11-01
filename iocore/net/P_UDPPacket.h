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

class UDPPacketInternal : public UDPPacket
{
public:
  UDPPacketInternal();
  virtual ~UDPPacketInternal();

  void append_block_internal(IOBufferBlock *block);

  virtual void free();

  SLINK(UDPPacketInternal, alink); // atomic link
  // packet scheduling stuff: keep it a doubly linked list
  uint64_t pktLength;

  int reqGenerationNum;
  ink_hrtime delivery_time; // when to deliver packet

  Ptr<IOBufferBlock> chain;
  Continuation *cont;          // callback on error
  UDPConnectionInternal *conn; // connection where packet should be sent to.

  int in_the_priority_queue;
  int in_heap;
};

inkcoreapi extern ClassAllocator<UDPPacketInternal> udpPacketAllocator;

TS_INLINE
UDPPacketInternal::UDPPacketInternal()
  : pktLength(0), reqGenerationNum(0), delivery_time(0), cont(nullptr), conn(nullptr), in_the_priority_queue(0), in_heap(0)
{
  memset(&from, '\0', sizeof(from));
  memset(&to, '\0', sizeof(to));
}

TS_INLINE
UDPPacketInternal::~UDPPacketInternal()
{
  chain = nullptr;
}

TS_INLINE void
UDPPacketInternal::free()
{
  chain = nullptr;
  if (conn)
    conn->Release();
  conn = nullptr;
  udpPacketAllocator.free(this);
}

TS_INLINE void
UDPPacket::append_block(IOBufferBlock *block)
{
  UDPPacketInternal *p = (UDPPacketInternal *)this;

  if (block) {
    if (p->chain) { // append to end
      IOBufferBlock *last = p->chain.get();
      while (last->next) {
        last = last->next.get();
      }
      last->next = block;
    } else {
      p->chain = block;
    }
  }
}

TS_INLINE int64_t
UDPPacket::getPktLength() const
{
  UDPPacketInternal *p = (UDPPacketInternal *)this;
  IOBufferBlock *b;

  p->pktLength = 0;
  b            = p->chain.get();
  while (b) {
    p->pktLength += b->read_avail();
    b = b->next.get();
  }
  return p->pktLength;
}

TS_INLINE void
UDPPacket::free()
{
  ((UDPPacketInternal *)this)->free();
}

TS_INLINE void
UDPPacket::setContinuation(Continuation *c)
{
  ((UDPPacketInternal *)this)->cont = c;
}

TS_INLINE void
UDPPacket::setConnection(UDPConnection *c)
{
  /*Code reviewed by Case Larsen.  Previously, we just had
     ink_assert(!conn).  This prevents tunneling of packets
     correctly---that is, you get packets from a server on a udp
     conn. and want to send it to a player on another connection, the
     assert will prevent that.  The "if" clause enables correct
     handling of the connection ref. counts in such a scenario. */

  UDPConnectionInternal *&conn = ((UDPPacketInternal *)this)->conn;

  if (conn) {
    if (conn == c)
      return;
    conn->Release();
    conn = nullptr;
  }
  conn = (UDPConnectionInternal *)c;
  conn->AddRef();
}

TS_INLINE IOBufferBlock *
UDPPacket::getIOBlockChain(void)
{
  ink_assert(dynamic_cast<UDPPacketInternal *>(this) != nullptr);
  return ((UDPPacketInternal *)this)->chain.get();
}

TS_INLINE UDPConnection *
UDPPacket::getConnection(void)
{
  return ((UDPPacketInternal *)this)->conn;
}

TS_INLINE UDPPacket *
new_UDPPacket(struct sockaddr const *to, ink_hrtime when, char *buf, int len)
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();

  p->in_the_priority_queue = 0;
  p->in_heap               = 0;
  p->delivery_time         = when;
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
new_UDPPacket(struct sockaddr const *to, ink_hrtime when, IOBufferBlock *buf, int len)
{
  (void)len;
  UDPPacketInternal *p = udpPacketAllocator.alloc();
  IOBufferBlock *body;

  p->in_the_priority_queue = 0;
  p->in_heap               = 0;
  p->delivery_time         = when;
  ats_ip_copy(&p->to, to);

  while (buf) {
    body = buf->clone();
    p->append_block(body);
    buf = buf->next.get();
  }

  return p;
}

TS_INLINE UDPPacket *
new_UDPPacket(struct sockaddr const *to, ink_hrtime when, Ptr<IOBufferBlock> &buf)
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();

  p->in_the_priority_queue = 0;
  p->in_heap               = 0;
  p->delivery_time         = when;
  if (to)
    ats_ip_copy(&p->to, to);
  p->chain = buf;
  return p;
}

TS_INLINE UDPPacket *
new_UDPPacket(ink_hrtime when, Ptr<IOBufferBlock> buf)
{
  return new_UDPPacket(nullptr, when, buf);
}

TS_INLINE UDPPacket *
new_incoming_UDPPacket(struct sockaddr *from, char *buf, int len)
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();

  p->in_the_priority_queue = 0;
  p->in_heap               = 0;
  p->delivery_time         = 0;
  ats_ip_copy(&p->from, from);

  IOBufferBlock *body = new_IOBufferBlock();
  body->alloc(iobuffer_size_to_index(len));
  memcpy(body->end(), buf, len);
  body->fill(len);
  p->append_block(body);

  return p;
}

TS_INLINE UDPPacket *
new_incoming_UDPPacket(struct sockaddr *from, Ptr<IOBufferBlock> &block)
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();

  p->in_the_priority_queue = 0;
  p->in_heap               = 0;
  p->delivery_time         = 0;
  ats_ip_copy(&p->from, from);
  p->chain = block;

  return p;
}

TS_INLINE UDPPacket *
new_UDPPacket()
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();
  return p;
}

#endif //__P_UDPPPACKET_H_
