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
  uint64_t pktLength    = 0;
  uint16_t segment_size = 0;

  int reqGenerationNum     = 0;
  ink_hrtime delivery_time = 0; // when to deliver packet

  Ptr<IOBufferBlock> chain;
  Continuation *cont          = nullptr; // callback on error
  UDPConnectionInternal *conn = nullptr; // connection where packet should be sent to.

  int in_the_priority_queue = 0;
  int in_heap               = 0;
};
