/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "I_UDPPacket.h"
#include "tscore/List.h"

#include "QUICPacket.h"

class QUICPacketFactory;

class QUICPacketReceiveQueue
{
public:
  QUICPacketReceiveQueue(QUICPacketFactory &packet_factory, QUICPacketHeaderProtector &ph_protector);

  void enqueue(UDPPacket *packet);
  QUICPacketUPtr dequeue(QUICPacketCreationResult &result);
  uint32_t size();
  void reset();

private:
  CountQueue<UDPPacket> _queue;
  QUICPacketFactory &_packet_factory;
  QUICPacketHeaderProtector &_ph_protector;
  QUICPacketNumber _largest_received_packet_number = 0;
  // FIXME: workaround code for coalescing packets
  ats_unique_buf _payload = {nullptr};
  size_t _payload_len     = 0;
  size_t _offset          = 0;
  UDPConnection *_udp_con;
  IpEndpoint _from;
};
