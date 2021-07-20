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

#pragma once

#include "I_UDPNet.h"

class UDPPacketInternal : public UDPPacket
{
public:
  UDPPacketInternal();
  ~UDPPacketInternal() override;

  void free() override;

  SLINK(UDPPacketInternal, alink); // atomic link
  // packet scheduling stuff: keep it a doubly linked list
  uint64_t pktLength = 0;

  int reqGenerationNum     = 0;
  ink_hrtime delivery_time = 0; // when to deliver packet

  Ptr<IOBufferBlock> chain;
  Continuation *cont          = nullptr; // callback on error
  UDPConnectionInternal *conn = nullptr; // connection where packet should be sent to.

  int in_the_priority_queue = 0;
  int in_heap               = 0;
};

extern ClassAllocator<UDPPacketInternal> udpPacketAllocator;

TS_INLINE
UDPPacketInternal::UDPPacketInternal()

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
  UDPPacketInternal *p = static_cast<UDPPacketInternal *>(this);

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
  UDPPacketInternal *p = const_cast<UDPPacketInternal *>(static_cast<const UDPPacketInternal *>(this));
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
  static_cast<UDPPacketInternal *>(this)->free();
}

TS_INLINE void
UDPPacket::setContinuation(Continuation *c)
{
  static_cast<UDPPacketInternal *>(this)->cont = c;
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

  UDPConnectionInternal *&conn = static_cast<UDPPacketInternal *>(this)->conn;

  if (conn) {
    if (conn == c)
      return;
    conn->Release();
    conn = nullptr;
  }
  conn = static_cast<UDPConnectionInternal *>(c);
  conn->AddRef();
}

TS_INLINE IOBufferBlock *
UDPPacket::getIOBlockChain()
{
  ink_assert(dynamic_cast<UDPPacketInternal *>(this) != nullptr);
  return static_cast<UDPPacketInternal *>(this)->chain.get();
}

TS_INLINE UDPConnection *
UDPPacket::getConnection()
{
  return static_cast<UDPPacketInternal *>(this)->conn;
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
new_incoming_UDPPacket(struct sockaddr *from, struct sockaddr *to, Ptr<IOBufferBlock> &block)
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();

  p->in_the_priority_queue = 0;
  p->in_heap               = 0;
  p->delivery_time         = 0;
  ats_ip_copy(&p->from, from);
  ats_ip_copy(&p->to, to);
  p->chain = block;

  return p;
}

TS_INLINE UDPPacket *
new_UDPPacket()
{
  UDPPacketInternal *p = udpPacketAllocator.alloc();
  return p;
}
