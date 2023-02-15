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
  int64_t getPktLength() const;

  /**
     Add IOBufferBlock (chain) to end of packet.
     @param block block chain to add.

   */
  void append_block(IOBufferBlock *block);

  IpEndpoint from; // what address came from
  IpEndpoint to;   // what address to send to

  int from_size;

  LINK(UDPPacket, link);

  // Factory (static) methods
  static UDPPacket *new_UDPPacket();
  static UDPPacket *new_UDPPacket(struct sockaddr const *to, ink_hrtime when, Ptr<IOBufferBlock> &buf, uint16_t segment_size = 0);
  static UDPPacket *new_incoming_UDPPacket(struct sockaddr *from, struct sockaddr *to, Ptr<IOBufferBlock> &block);
};
