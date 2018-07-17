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

#include "QUICConnection.h"
#include "QUICTransportParameters.h"
#include "QUICFrameHandler.h"
#include "QUICFrameGenerator.h"
#include "QUICStream.h"

/**
 * @class QUICHandshake
 * @brief Send/Receive CRYPTO frame and do Cryptographic Handshake
 */
class QUICVersionNegotiator;
class SSLNextProtocolSet;

class QUICHandshake : public QUICFrameHandler, public QUICFrameGenerator
{
public:
  // Constructor for client side
  QUICHandshake(QUICConnection *qc, SSL_CTX *ssl_ctx);
  // Constructor for server side
  QUICHandshake(QUICConnection *qc, SSL_CTX *ssl_ctx, QUICStatelessResetToken token, bool stateless_retry);
  ~QUICHandshake();

  // QUICFrameHandler
  virtual std::vector<QUICFrameType> interests() override;
  virtual QUICErrorUPtr handle_frame(QUICEncryptionLevel level, std::shared_ptr<const QUICFrame> frame) override;

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level) override;
  QUICFrameUPtr generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size) override;

  // for client side
  QUICErrorUPtr start(QUICPacketFactory *packet_factory, bool vn_exercise_enabled);
  QUICErrorUPtr negotiate_version(const QUICPacket *packet, QUICPacketFactory *packet_factory);
  void reset();

  // for server side
  QUICErrorUPtr start(const QUICPacket *initial_packet, QUICPacketFactory *packet_factory);

  int do_handshake();

  // Getters
  QUICHandshakeProtocol *protocol();
  QUICVersion negotiated_version();
  const char *negotiated_cipher_suite();
  void negotiated_application_name(const uint8_t **name, unsigned int *len);
  std::shared_ptr<const QUICTransportParameters> local_transport_parameters();
  std::shared_ptr<const QUICTransportParameters> remote_transport_parameters();

  bool is_version_negotiated() const;
  bool is_completed() const;
  bool is_stateless_retry_enabled() const;

  void set_transport_parameters(std::shared_ptr<QUICTransportParametersInClientHello> tp);
  void set_transport_parameters(std::shared_ptr<QUICTransportParametersInEncryptedExtensions> tp);
  void set_transport_parameters(std::shared_ptr<QUICTransportParametersInNewSessionTicket> tp);

private:
  QUICConnection *_qc                                                   = nullptr;
  SSL *_ssl                                                             = nullptr;
  QUICHandshakeProtocol *_hs_protocol                                   = nullptr;
  std::shared_ptr<QUICTransportParameters> _local_transport_parameters  = nullptr;
  std::shared_ptr<QUICTransportParameters> _remote_transport_parameters = nullptr;

  QUICVersionNegotiator *_version_negotiator = nullptr;
  QUICStatelessResetToken _reset_token;
  bool _client_initial  = false;
  bool _stateless_retry = false;

  QUICCryptoStream _crypto_streams[4];

  std::vector<QUICEncryptionLevel>
  _encryption_level_filter() override
  {
    return {
      QUICEncryptionLevel::INITIAL,
      QUICEncryptionLevel::ZERO_RTT,
      QUICEncryptionLevel::HANDSHAKE,
      QUICEncryptionLevel::ONE_RTT,
    };
  }
  void _load_local_server_transport_parameters(QUICVersion negotiated_version);
  void _load_local_client_transport_parameters(QUICVersion initial_version);
  void _abort_handshake(QUICTransErrorCode code);
};
