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

#ifndef __I_UDPPACKET_H_
#define __I_UDPPACKET_H_

#include "I_UDPConnection.h"
/** @name UDPPacket
    UDP packet functions used by UDPConnection
 */
//@{
/**
   UDP data with destination
 */
class UDPPacket
{
public:
  virtual ~UDPPacket() {}
  virtual void free(); // fast deallocate
  void setContinuation(Continuation *c);
  void setConnection(UDPConnection *c);
  UDPConnection *getConnection();
  IOBufferBlock *getIOBlockChain();
  int64_t getPktLength();

  /**
     Add IOBufferBlock (chain) to end of packet.
     @param block block chain to add.

   */
  inkcoreapi void append_block(IOBufferBlock *block);

  IpEndpoint from; // what address came from
  IpEndpoint to;   // what address to send to

  int from_size;

  LINK(UDPPacket, link);
};

/**
   Create a new packet to be sent over UDPConnection. This actually
   copies data from a buffer.


   @param to  address of where to send packet
   @param when ink_hrtime relative to ink_get_hrtime_internal()
   @param buf if !NULL, then len bytes copied from buf and made into packet.
   @param len # of bytes to copy from buf
 */
extern UDPPacket *new_UDPPacket(struct sockaddr const *to, ink_hrtime when = 0, char *buf = NULL, int len = 0);
/**
   Create a new packet to be sent over UDPConnection. This clones and
   makes a reference to an existing IOBufferBlock chain.


   @param to  address of where to send packet
   @param when ink_hrtime relative to ink_get_hrtime_internal()
   @param block if !NULL, then the IOBufferBlock chain of data to use
   for packet
   @param len # of bytes to reference from block
 */

TS_INLINE UDPPacket *new_UDPPacket(struct sockaddr const *to, ink_hrtime when = 0, IOBufferBlock *block = NULL, int len = 0);
/**
   Create a new packet to be sent over UDPConnection.  Packet has no
   destination or data.
*/
extern UDPPacket *new_UDPPacket();

/**
   Create a new packet to be delivered to application.
   Internal function only
*/
extern UDPPacket *new_incoming_UDPPacket(struct sockaddr *from, char *buf, int len);

//@}
#endif //__I_UDPPACKET_H_
