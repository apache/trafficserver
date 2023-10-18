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

  I_UDPPacket.h
  UDPPacket interface


 ****************************************************************************/

#pragma once

#include "I_UDPConnection.h"

struct UDPPacketInternal {
  // packet scheduling stuff: keep it a doubly linked list
  uint64_t pktLength    = 0;
  uint16_t segment_size = 0;

  int reqGenerationNum     = 0;
  ink_hrtime delivery_time = 0; // when to deliver packet

  Ptr<IOBufferBlock> chain;
  Continuation *cont  = nullptr; // callback on error
  UDPConnection *conn = nullptr; // connection where packet should be sent to.

  int in_the_priority_queue = 0;
  int in_heap               = 0;
};

/** @name UDPPacket
    UDP packet functions used by UDPConnection
 */
//@{
/**
   UDP data with destination
 */
class UDPPacket
{
  friend class UDPQueue;
  friend class PacketQueue;
  friend class UDPConnection;
  friend class UnixUDPConnection;

public:
  UDPPacket();
  ~UDPPacket();
  void free(); // fast deallocate

  void setContinuation(Continuation *c);
  void setConnection(UDPConnection *c);
  UDPConnection *getConnection();
  IOBufferBlock *getIOBlockChain();
  int64_t getPktLength();
  uint8_t *get_entire_chain_buffer(size_t *buf_len);

  /**
     Add IOBufferBlock (chain) to end of packet.
     @param block block chain to add.
   */
  void append_block(IOBufferBlock *block);

  IpEndpoint from; // what address came from
  IpEndpoint to;   // what address to send to

  LINK(UDPPacket, link);
  // Factory (static) methods

  /**
     Create a new packet to be sent over UDPConnection.  Packet has no
     destination or data.
  */
  static UDPPacket *new_UDPPacket();

  /**
     Create a new packet to be sent over UDPConnection. This actually
     copies data from a buffer.

     @param to  address of where to send packet
     @param when ink_hrtime relative to ink_get_hrtime()
     @param buf IOBufferBlock chain of data to use
     @param segment_size Segment size
  */
  static UDPPacket *new_UDPPacket(struct sockaddr const *to, ink_hrtime when, Ptr<IOBufferBlock> &buf, uint16_t segment_size = 0);

  /**
     Create a new packet to be delivered to application.
     Internal function only
  */
  static UDPPacket *new_incoming_UDPPacket(struct sockaddr *from, struct sockaddr *to, Ptr<IOBufferBlock> block);

private:
  SLINK(UDPPacket, alink); // atomic link
  UDPPacketInternal p;
  ats_unique_buf _payload{nullptr};
};

// Inline definitions

inline void
UDPPacket::setContinuation(Continuation *c)
{
  p.cont = c;
}

inline void
UDPPacket::setConnection(UDPConnection *c)
{
  /*Code reviewed by Case Larsen.  Previously, we just had
     ink_assert(!conn).  This prevents tunneling of packets
     correctly---that is, you get packets from a server on a udp
     conn. and want to send it to a player on another connection, the
     assert will prevent that.  The "if" clause enables correct
     handling of the connection ref. counts in such a scenario. */

  if (p.conn) {
    if (p.conn == c) {
      return;
    }
    p.conn->Release();
    p.conn = nullptr;
  }
  p.conn = c;
  p.conn->AddRef();
}

inline UDPConnection *
UDPPacket::getConnection()
{
  return p.conn;
}

inline IOBufferBlock *
UDPPacket::getIOBlockChain()
{
  return p.chain.get();
}
