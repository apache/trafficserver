/** @file
 *
 *  QUIC TLS
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

#include <openssl/ssl.h>

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/digest.h>
#include <openssl/cipher.h>
#else
#include <openssl/evp.h>
#endif

#include "I_EventSystem.h"
#include "I_NetVConnection.h"
#include "QUICHandshakeProtocol.h"

// TODO: fix size
static constexpr int MAX_HANDSHAKE_MSG_LEN = 65527;

class QUICTLS : public QUICHandshakeProtocol
{
public:
  QUICTLS(QUICPacketProtectionKeyInfo &pp_key_info, SSL_CTX *ssl_ctx, NetVConnectionContext_t nvc_ctx,
          const NetVCOptions &netvc_options, const char *session_file = nullptr, const char *keylog_file = nullptr);
  ~QUICTLS();

  // TODO: integrate with _early_data_processed
  enum class HandshakeState {
    PROCESSING,
    ABORTED,
  };

  static QUICEncryptionLevel get_encryption_level(int msg_type);
  static uint64_t convert_to_quic_trans_error_code(uint8_t alert);

  std::shared_ptr<const QUICTransportParameters> local_transport_parameters() override;
  std::shared_ptr<const QUICTransportParameters> remote_transport_parameters() override;
  void set_local_transport_parameters(std::shared_ptr<const QUICTransportParameters> tp) override;
  void set_remote_transport_parameters(std::shared_ptr<const QUICTransportParameters> tp) override;

  const char *session_file() const;
  const char *keylog_file() const;

  // FIXME Should not exist
  SSL *ssl_handle();

  // QUICHandshakeProtocol
  int handshake(QUICHandshakeMsgs **out, const QUICHandshakeMsgs *in) override;
  void reset() override;
  bool is_handshake_finished() const override;
  bool is_ready_to_derive() const override;
  int initialize_key_materials(QUICConnectionId cid, QUICVersion version) override;
  void update_negotiated_cipher();
  void update_key_materials_for_read(QUICEncryptionLevel level, const uint8_t *secret, size_t secret_len);
  void update_key_materials_for_write(QUICEncryptionLevel level, const uint8_t *secret, size_t secret_len);
  const char *negotiated_cipher_suite() const override;
  void negotiated_application_name(const uint8_t **name, unsigned int *len) const override;
  QUICEncryptionLevel current_encryption_level() const override;
  void abort_handshake() override;
  bool has_crypto_error() const override;
  uint64_t crypto_error() const override;

  void set_ready_for_write();

  void on_handshake_data_generated(QUICEncryptionLevel level, const uint8_t *data, size_t len);
  void on_tls_alert(uint8_t alert);

private:
  QUICKeyGenerator _keygen_for_client = QUICKeyGenerator(QUICKeyGenerator::Context::CLIENT);
  QUICKeyGenerator _keygen_for_server = QUICKeyGenerator(QUICKeyGenerator::Context::SERVER);
  const char *_get_handshake_digest() const;

  int _read_early_data();
  int _write_early_data();
  void _pass_quic_data_to_ssl_impl(const QUICHandshakeMsgs &in);
  int _handshake(QUICHandshakeMsgs **out, const QUICHandshakeMsgs *in);
  int _process_post_handshake_messages(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in);
  void _generate_0rtt_key();
  void _update_encryption_level(QUICEncryptionLevel level);

  void _store_negotiated_cipher();
  void _store_negotiated_cipher_for_hp();

  void _print_km(const char *header, const uint8_t *key_for_hp, size_t key_for_hp_len, const uint8_t *key, size_t key_len,
                 const uint8_t *iv, size_t iv_len, const uint8_t *secret = nullptr, size_t secret_len = 0,
                 QUICKeyPhase phase = QUICKeyPhase::INITIAL);
  static void _print_hs_message(int content_type, const void *buf, size_t len);

  static void _msg_cb(int write_p, int version, int content_type, const void *buf, size_t len, SSL *ssl, void *arg);
  const char *_session_file              = nullptr;
  const char *_keylog_file               = nullptr;
  SSL *_ssl                              = nullptr;
  NetVConnectionContext_t _netvc_context = NET_VCONNECTION_UNSET;
  bool _early_data_processed             = false;
  bool _is_session_reused                = false;
  bool _early_data                       = true;
  QUICEncryptionLevel _current_level     = QUICEncryptionLevel::INITIAL;
  HandshakeState _state                  = HandshakeState::PROCESSING;

  std::shared_ptr<const QUICTransportParameters> _local_transport_parameters  = nullptr;
  std::shared_ptr<const QUICTransportParameters> _remote_transport_parameters = nullptr;

  uint8_t _out_buf[MAX_HANDSHAKE_MSG_LEN] = {0};
  QUICHandshakeMsgs _out                  = {_out_buf, MAX_HANDSHAKE_MSG_LEN, {0}, 0};
  bool _should_flush                      = false;
  bool _has_crypto_error                  = false;
  uint64_t _crypto_error                  = 0;
};
