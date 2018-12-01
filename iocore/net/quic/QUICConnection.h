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

#include "I_EventSystem.h"
#include "I_NetVConnection.h"
#include "QUICPacketTransmitter.h"
#include "QUICFrameHandler.h"

class QUICApplication;
class QUICStreamManager;
class UDPPacket;
class SSLNextProtocolSet;

class QUICConnectionInfoProvider
{
public:
  virtual QUICConnectionId peer_connection_id() const     = 0;
  virtual QUICConnectionId original_connection_id() const = 0;
  virtual QUICConnectionId first_connection_id() const    = 0;
  virtual QUICConnectionId connection_id() const          = 0;
  virtual std::string_view cids() const                   = 0;
  virtual const QUICFiveTuple five_tuple() const          = 0;

  /*
   * Retruns the maximum packet size at the time called
   *
   * The size depends on PMTU.
   */
  virtual uint32_t maximum_quic_packet_size() const = 0;

  /*
   * Returns the mimimum packet size at the time called
   *
   * If the connection is an outgoing connection and you have not sent Client Initial packet,
   * this return the minimum size for it, which is 1200.
   */
  virtual uint32_t minimum_quic_packet_size() = 0;

  virtual uint32_t pmtu() const                                                         = 0;
  virtual NetVConnectionContext_t direction() const                                     = 0;
  virtual SSLNextProtocolSet *next_protocol_set() const                                 = 0;
  virtual bool is_closed() const                                                        = 0;
  virtual QUICPacketNumber largest_acked_packet_number(QUICEncryptionLevel level) const = 0;
};

class QUICConnection : public QUICPacketTransmitter, public QUICFrameHandler, public QUICConnectionInfoProvider
{
public:
  virtual QUICStreamManager *stream_manager()             = 0;
  virtual void close(QUICConnectionErrorUPtr error)       = 0;
  virtual void handle_received_packet(UDPPacket *packeet) = 0;
};
