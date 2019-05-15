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
#include "QUICCryptoStream.h"

/**
 * @class QUICHandshake
 * @brief Send/Receive CRYPTO frame and do Cryptographic Handshake
 */
class QUICVersionNegotiator;
class QUICPacketFactory;
class SSLNextProtocolSet;

class QUICHandshake : public QUICFrameHandler, public QUICFrameGenerator
{
public:
  // Constructor for client side
  QUICHandshake(QUICConnection *qc, QUICHandshakeProtocol *hsp);
  // Constructor for server side
  QUICHandshake(QUICConnection *qc, QUICHandshakeProtocol *hsp, QUICStatelessResetToken token, bool stateless_retry);
  ~QUICHandshake();

  // QUICFrameHandler
  virtual std::vector<QUICFrameType> interests() override;
  virtual QUICConnectionErrorUPtr handle_frame(QUICEncryptionLevel level, const QUICFrame &frame) override;

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp) override;
  QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                            ink_hrtime timestamp) override;

  // for client side
  QUICConnectionErrorUPtr start(const QUICTPConfig &tp_config, QUICPacketFactory *packet_factory, bool vn_exercise_enabled);
  QUICConnectionErrorUPtr negotiate_version(const QUICPacket &packet, QUICPacketFactory *packet_factory);
  void reset();

  // for server side
  QUICConnectionErrorUPtr start(const QUICTPConfig &tp_config, const QUICPacket &initial_packet, QUICPacketFactory *packet_factory,
                                const QUICPreferredAddress *pref_addr);

  QUICConnectionErrorUPtr do_handshake();

  bool check_remote_transport_parameters();

  // Getters
  QUICVersion negotiated_version();
  const char *negotiated_cipher_suite();
  void negotiated_application_name(const uint8_t **name, unsigned int *len);
  std::shared_ptr<const QUICTransportParameters> local_transport_parameters();
  std::shared_ptr<const QUICTransportParameters> remote_transport_parameters();

  bool is_version_negotiated() const;
  bool is_completed() const;
  bool is_stateless_retry_enabled() const;
  bool has_remote_tp() const;

protected:
  virtual bool _is_level_matched(QUICEncryptionLevel level) override;

private:
  QUICConnection *_qc                 = nullptr;
  QUICHandshakeProtocol *_hs_protocol = nullptr;

  QUICVersionNegotiator *_version_negotiator = nullptr;
  QUICStatelessResetToken _reset_token;
  bool _client_initial  = false;
  bool _stateless_retry = false;

  QUICCryptoStream _crypto_streams[4];

  void _load_local_server_transport_parameters(const QUICTPConfig &tp_config, const QUICPreferredAddress *pref_addr);
  void _load_local_client_transport_parameters(const QUICTPConfig &tp_config);
  bool _check_remote_transport_parameters(std::shared_ptr<const QUICTransportParametersInClientHello> tp);
  bool _check_remote_transport_parameters(std::shared_ptr<const QUICTransportParametersInEncryptedExtensions> tp);
  std::shared_ptr<const QUICTransportParameters> _local_transport_parameters  = nullptr;
  std::shared_ptr<const QUICTransportParameters> _remote_transport_parameters = nullptr;

  void _abort_handshake(QUICTransErrorCode code);
};
