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
#include "QUICFrameHandler.h"

class QUICApplication;
class QUICStreamManager;
class UDPPacket;

class QUICConnectionInfoProvider
{
public:
  virtual ~QUICConnectionInfoProvider() {}
  virtual QUICConnectionId peer_connection_id() const     = 0;
  virtual QUICConnectionId original_connection_id() const = 0;
  /**
   * This is S1 on 7.3.Authenticating Connection IDs
   */
  virtual QUICConnectionId first_connection_id() const = 0;
  /**
   * This is S2 on 7.3.Authenticating Connection IDs
   */
  virtual QUICConnectionId retry_source_connection_id() const = 0;
  /**
   * This is C1 or S3 on 7.3.Authenticating Connection IDs
   */
  virtual QUICConnectionId initial_source_connection_id() const = 0;
  virtual QUICConnectionId connection_id() const                = 0;
  virtual std::string_view cids() const                         = 0;
  virtual const QUICFiveTuple five_tuple() const                = 0;

  virtual uint32_t pmtu() const                                = 0;
  virtual NetVConnectionContext_t direction() const            = 0;
  virtual bool is_closed() const                               = 0;
  virtual bool is_at_anti_amplification_limit() const          = 0;
  virtual bool is_address_validation_completed() const         = 0;
  virtual bool is_handshake_completed() const                  = 0;
  virtual bool has_keys_for(QUICPacketNumberSpace space) const = 0;
  virtual QUICVersion negotiated_version() const               = 0;
  virtual std::string_view negotiated_application_name() const = 0;
};

class QUICConnection : public QUICFrameHandler, public QUICConnectionInfoProvider
{
public:
  virtual QUICStreamManager *stream_manager()                       = 0;
  virtual void close_quic_connection(QUICConnectionErrorUPtr error) = 0;
  virtual void reset_quic_connection()                              = 0;
  virtual void handle_received_packet(UDPPacket *packet)            = 0;
  virtual void ping()                                               = 0;
};
