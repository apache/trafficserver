/** @file
 *
 *  QUIC packet factory
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

#include "QUICTypes.h"
#include "QUICPacket.h"
#include "QUICPacketPayloadProtector.h"

class QUICPacketProtectionKeyInfo;

class QUICPacketNumberGenerator
{
public:
  QUICPacketNumberGenerator();
  QUICPacketNumber next();
  void reset();

private:
  std::atomic<QUICPacketNumber> _current = 0;
};

class QUICPacketFactory
{
public:
  static QUICPacketUPtr create_null_packet();
  static QUICPacketUPtr create_version_negotiation_packet(QUICConnectionId dcid, QUICConnectionId scid,
                                                          QUICVersion version_in_version);
  static QUICPacketUPtr create_stateless_reset_packet(QUICStatelessResetToken stateless_reset_token, size_t maximum_size);
  static QUICPacketUPtr create_retry_packet(QUICVersion version, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                            QUICRetryToken &token);

  QUICPacketFactory(const QUICPacketProtectionKeyInfo &pp_key_info) : _pp_key_info(pp_key_info), _pp_protector(pp_key_info) {}

  QUICPacketUPtr create(uint8_t *packet_buf, UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, ats_unique_buf buf, size_t len,
                        QUICPacketNumber base_packet_number, QUICPacketCreationResult &result);
  QUICPacketUPtr create_initial_packet(uint8_t *packet_buf, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                       QUICPacketNumber base_packet_number, Ptr<IOBufferBlock> payload, size_t length,
                                       bool ack_eliciting, bool probing, bool crypto,
                                       ats_unique_buf token = ats_unique_buf(nullptr), size_t token_len = 0);
  QUICPacketUPtr create_handshake_packet(uint8_t *packet_buf, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                         QUICPacketNumber base_packet_number, Ptr<IOBufferBlock> payload, size_t length,
                                         bool ack_eliciting, bool probing, bool crypto);
  QUICPacketUPtr create_zero_rtt_packet(uint8_t *packet_buf, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                        QUICPacketNumber base_packet_number, Ptr<IOBufferBlock> payload, size_t length,
                                        bool ack_eliciting, bool probing);
  QUICPacketUPtr create_short_header_packet(uint8_t *packet_buf, QUICConnectionId destination_cid,
                                            QUICPacketNumber base_packet_number, Ptr<IOBufferBlock> payload, size_t length,
                                            bool ack_eliciting, bool probing);
  void set_version(QUICVersion negotiated_version);

  bool is_ready_to_create_protected_packet();
  void reset();

private:
  QUICVersion _version = QUIC_SUPPORTED_VERSIONS[0];

  const QUICPacketProtectionKeyInfo &_pp_key_info;
  QUICPacketPayloadProtector _pp_protector;

  // Initial, 0/1-RTT, and Handshake
  QUICPacketNumberGenerator _packet_number_generator[3];
};
