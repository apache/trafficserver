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
#include "QUICApplication.h"
#include "QUICTransportParameters.h"

/**
 * @class QUICHandshake
 * @brief Do handshake as a QUIC application
 * @detail
 *
 * state_handshake()
 *  |
 *  v
 * state_complete() on success
 *  or
 * state_closed() on error
 */
class QUICVersionNegotiator;
class SSLNextProtocolSet;

class QUICHandshake : public QUICApplication
{
public:
  // Constructor for client side
  QUICHandshake(QUICConnection *qc, SSL_CTX *ssl_ctx);
  // Constructor for server side
  QUICHandshake(QUICConnection *qc, SSL_CTX *ssl_ctx, QUICStatelessResetToken token, bool stateless_retry);
  ~QUICHandshake();

  // for client side
  QUICErrorUPtr start(QUICPacketFactory *packet_factory, bool vn_exercise_enabled);
  QUICErrorUPtr negotiate_version(const QUICPacket *packet, QUICPacketFactory *packet_factory);
  void reset();

  // for server side
  QUICErrorUPtr start(const QUICPacket *initial_packet, QUICPacketFactory *packet_factory, bool &need_challege);

  // States
  int state_handshake(int event, Event *data);
  int state_complete(int event, void *data);
  int state_closed(int event, void *data);

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

  // A workaround API to indicate handshake msg type to QUICNetVConnection
  QUICHandshakeMsgType msg_type() const;

private:
  SSL *_ssl                                                             = nullptr;
  QUICHandshakeProtocol *_hs_protocol                                   = nullptr;
  std::shared_ptr<QUICTransportParameters> _local_transport_parameters  = nullptr;
  std::shared_ptr<QUICTransportParameters> _remote_transport_parameters = nullptr;

  QUICVersionNegotiator *_version_negotiator = nullptr;
  NetVConnectionContext_t _netvc_context     = NET_VCONNECTION_UNSET;
  QUICStatelessResetToken _reset_token;
  bool _initial         = false;
  bool _stateless_retry = false;

  int _initial_packet_count = 0;

  void _load_local_server_transport_parameters(QUICVersion negotiated_version);
  void _load_local_client_transport_parameters(QUICVersion initial_version);

  int _do_handshake(size_t &out_len);

  QUICErrorUPtr _process_handshake_msg();

  int _complete_handshake();
  void _abort_handshake(QUICTransErrorCode code);
};
