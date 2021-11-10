/** @file
 *
 *  QUIC Handshake Protocol (TLS to Secure QUIC)
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

#include "QUICTLS.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>

#include "QUICPacketProtectionKeyInfo.h"
#include "QUICDebugNames.h"

constexpr static char tag[] = "quic_tls";

static const char *
content_type_str(int type)
{
  switch (type) {
  case SSL3_RT_CHANGE_CIPHER_SPEC:
    return "CHANGE_CIPHER_SPEC";
  case SSL3_RT_ALERT:
    return "ALERT";
  case SSL3_RT_HANDSHAKE:
    return "HANDSHAKE";
  case SSL3_RT_APPLICATION_DATA:
    return "APPLICATION_DATA";
  case SSL3_RT_HEADER:
    // The buf contains the record header bytes only
    return "HEADER";
  default:
    return "UNKNOWN";
  }
}

static const char *
hs_type_str(int type)
{
  switch (type) {
  case SSL3_MT_CLIENT_HELLO:
    return "CLIENT_HELLO";
  case SSL3_MT_SERVER_HELLO:
    return "SERVER_HELLO";
  case SSL3_MT_NEWSESSION_TICKET:
    return "NEWSESSION_TICKET";
  case SSL3_MT_END_OF_EARLY_DATA:
    return "END_OF_EARLY_DATA";
  case SSL3_MT_ENCRYPTED_EXTENSIONS:
    return "ENCRYPTED_EXTENSIONS";
  case SSL3_MT_CERTIFICATE:
    return "CERTIFICATE";
  case SSL3_MT_CERTIFICATE_VERIFY:
    return "CERTIFICATE_VERIFY";
  case SSL3_MT_FINISHED:
    return "FINISHED";
  case SSL3_MT_KEY_UPDATE:
    return "KEY_UPDATE";
  case SSL3_MT_MESSAGE_HASH:
    return "MESSAGE_HASH";
  default:
    return "UNKNOWN";
  }
}

SSL *
QUICTLS::ssl_handle()
{
  return this->_ssl;
}

std::shared_ptr<const QUICTransportParameters>
QUICTLS::local_transport_parameters()
{
  return this->_local_transport_parameters;
}

std::shared_ptr<const QUICTransportParameters>
QUICTLS::remote_transport_parameters()
{
  return this->_remote_transport_parameters;
}

void
QUICTLS::set_remote_transport_parameters(std::shared_ptr<const QUICTransportParameters> tp)
{
  this->_remote_transport_parameters = tp;
}

const char *
QUICTLS::session_file() const
{
  return this->_session_file;
}

QUICTLS::~QUICTLS()
{
  SSL_free(this->_ssl);
}

int
QUICTLS::handshake(QUICHandshakeMsgs **out, const QUICHandshakeMsgs *in)
{
  if (this->is_handshake_finished()) {
    if (in != nullptr && in->offsets[4] != 0) {
      return this->_process_post_handshake_messages(*out, in);
    }

    return 1;
  }

  return this->_handshake(out, in);
}

void
QUICTLS::reset()
{
  SSL_clear(this->_ssl);
}

uint64_t
QUICTLS::convert_to_quic_trans_error_code(uint8_t alert)
{
  return static_cast<uint64_t>(QUICTransErrorCode::CRYPTO_ERROR) + alert;
}

bool
QUICTLS::is_handshake_finished() const
{
  return SSL_is_init_finished(this->_ssl);
}

bool
QUICTLS::is_ready_to_derive() const
{
  if (this->_netvc_context == NET_VCONNECTION_IN) {
    return SSL_get_current_cipher(this->_ssl) != nullptr;
  } else {
    return this->is_handshake_finished();
  }
}

int
QUICTLS::initialize_key_materials(QUICConnectionId cid, QUICVersion version)
{
  this->_pp_key_info.set_cipher_initial(EVP_aes_128_gcm());
  this->_pp_key_info.set_cipher_for_hp_initial(EVP_aes_128_ecb());

  // Generate keys
  if (is_debug_tag_set(tag)) {
    Debug(tag, "Generating %s keys with cid %s", QUICDebugNames::key_phase(QUICKeyPhase::INITIAL), cid.hex().c_str());
  }

  uint8_t *client_key_for_hp;
  uint8_t *client_key;
  uint8_t *client_iv;
  size_t client_key_for_hp_len;
  size_t client_key_len;
  size_t *client_iv_len;

  uint8_t *server_key_for_hp;
  uint8_t *server_key;
  uint8_t *server_iv;
  size_t server_key_for_hp_len;
  size_t server_key_len;
  size_t *server_iv_len;

  if (this->_netvc_context == NET_VCONNECTION_IN) {
    client_key_for_hp     = this->_pp_key_info.decryption_key_for_hp(QUICKeyPhase::INITIAL);
    client_key_for_hp_len = this->_pp_key_info.decryption_key_for_hp_len(QUICKeyPhase::INITIAL);
    client_key            = this->_pp_key_info.decryption_key(QUICKeyPhase::INITIAL);
    client_key_len        = this->_pp_key_info.decryption_key_len(QUICKeyPhase::INITIAL);
    client_iv             = this->_pp_key_info.decryption_iv(QUICKeyPhase::INITIAL);
    client_iv_len         = this->_pp_key_info.decryption_iv_len(QUICKeyPhase::INITIAL);
    server_key_for_hp     = this->_pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL);
    server_key_for_hp_len = this->_pp_key_info.encryption_key_for_hp_len(QUICKeyPhase::INITIAL);
    server_key            = this->_pp_key_info.encryption_key(QUICKeyPhase::INITIAL);
    server_key_len        = this->_pp_key_info.encryption_key_len(QUICKeyPhase::INITIAL);
    server_iv             = this->_pp_key_info.encryption_iv(QUICKeyPhase::INITIAL);
    server_iv_len         = this->_pp_key_info.encryption_iv_len(QUICKeyPhase::INITIAL);
  } else {
    client_key_for_hp     = this->_pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL);
    client_key_for_hp_len = this->_pp_key_info.encryption_key_for_hp_len(QUICKeyPhase::INITIAL);
    client_key            = this->_pp_key_info.encryption_key(QUICKeyPhase::INITIAL);
    client_key_len        = this->_pp_key_info.encryption_key_len(QUICKeyPhase::INITIAL);
    client_iv             = this->_pp_key_info.encryption_iv(QUICKeyPhase::INITIAL);
    client_iv_len         = this->_pp_key_info.encryption_iv_len(QUICKeyPhase::INITIAL);
    server_key_for_hp     = this->_pp_key_info.decryption_key_for_hp(QUICKeyPhase::INITIAL);
    server_key_for_hp_len = this->_pp_key_info.decryption_key_for_hp_len(QUICKeyPhase::INITIAL);
    server_key            = this->_pp_key_info.decryption_key(QUICKeyPhase::INITIAL);
    server_key_len        = this->_pp_key_info.decryption_key_len(QUICKeyPhase::INITIAL);
    server_iv             = this->_pp_key_info.decryption_iv(QUICKeyPhase::INITIAL);
    server_iv_len         = this->_pp_key_info.decryption_iv_len(QUICKeyPhase::INITIAL);
  }

  this->_keygen_for_client.generate(version, client_key_for_hp, client_key, client_iv, client_iv_len, cid);
  this->_keygen_for_server.generate(version, server_key_for_hp, server_key, server_iv, server_iv_len, cid);

  this->_pp_key_info.set_decryption_key_available(QUICKeyPhase::INITIAL);
  this->_pp_key_info.set_encryption_key_available(QUICKeyPhase::INITIAL);

  this->_print_km("initial - server", server_key_for_hp, server_key_for_hp_len, server_key, server_key_len, server_iv,
                  *server_iv_len);
  this->_print_km("initial - client", client_key_for_hp, client_key_for_hp_len, client_key, client_key_len, client_iv,
                  *client_iv_len);

  return 1;
}

void
QUICTLS::update_negotiated_cipher()
{
  this->_store_negotiated_cipher();
  this->_store_negotiated_cipher_for_hp();
}

void
QUICTLS::update_key_materials_for_read(QUICEncryptionLevel level, const uint8_t *secret, size_t secret_len)
{
  if (this->_state == HandshakeState::ABORTED) {
    return;
  }

  QUICKeyPhase phase;
  const EVP_CIPHER *cipher;

  switch (level) {
  case QUICEncryptionLevel::ZERO_RTT:
    phase  = QUICKeyPhase::ZERO_RTT;
    cipher = this->_pp_key_info.get_cipher(phase);
    break;
  case QUICEncryptionLevel::HANDSHAKE:
    this->_update_encryption_level(QUICEncryptionLevel::HANDSHAKE);
    phase = QUICKeyPhase::HANDSHAKE;
    break;
  case QUICEncryptionLevel::ONE_RTT:
    this->_update_encryption_level(QUICEncryptionLevel::ONE_RTT);
    // TODO Support Key Update
    phase = QUICKeyPhase::PHASE_0;
    break;
  default:
    phase = QUICKeyPhase::INITIAL;
    break;
  }

  QUICHKDF hkdf(this->_get_handshake_digest());

  uint8_t *key_for_hp;
  uint8_t *key;
  uint8_t *iv;
  size_t key_for_hp_len;
  size_t key_len;
  size_t *iv_len;

  cipher         = this->_pp_key_info.get_cipher(phase);
  key_for_hp     = this->_pp_key_info.decryption_key_for_hp(phase);
  key_for_hp_len = this->_pp_key_info.decryption_key_for_hp_len(phase);
  key            = this->_pp_key_info.decryption_key(phase);
  key_len        = this->_pp_key_info.decryption_key_len(phase);
  iv             = this->_pp_key_info.decryption_iv(phase);
  iv_len         = this->_pp_key_info.decryption_iv_len(phase);

  if (this->_netvc_context == NET_VCONNECTION_IN) {
    this->_keygen_for_client.regenerate(key_for_hp, key, iv, iv_len, secret, secret_len, cipher, hkdf);
    this->_print_km("update - client", key_for_hp, key_for_hp_len, key, key_len, iv, *iv_len, secret, secret_len, phase);
  } else {
    this->_keygen_for_server.regenerate(key_for_hp, key, iv, iv_len, secret, secret_len, cipher, hkdf);
    this->_print_km("update - server", key_for_hp, key_for_hp_len, key, key_len, iv, *iv_len, secret, secret_len, phase);
  }

  this->_pp_key_info.set_decryption_key_available(phase);
}

void
QUICTLS::update_key_materials_for_write(QUICEncryptionLevel level, const uint8_t *secret, size_t secret_len)
{
  if (this->_state == HandshakeState::ABORTED) {
    return;
  }

  QUICKeyPhase phase;
  const EVP_CIPHER *cipher = nullptr;

  switch (level) {
  case QUICEncryptionLevel::ZERO_RTT:
    phase  = QUICKeyPhase::ZERO_RTT;
    cipher = this->_pp_key_info.get_cipher(phase);
    break;
  case QUICEncryptionLevel::HANDSHAKE:
    this->_update_encryption_level(QUICEncryptionLevel::HANDSHAKE);
    phase  = QUICKeyPhase::HANDSHAKE;
    cipher = this->_pp_key_info.get_cipher(phase);
    break;
  case QUICEncryptionLevel::ONE_RTT:
    this->_update_encryption_level(QUICEncryptionLevel::ONE_RTT);
    phase  = QUICKeyPhase::PHASE_0;
    cipher = this->_pp_key_info.get_cipher(phase);
    break;
  default:
    phase = QUICKeyPhase::INITIAL;
    break;
  }

  QUICHKDF hkdf(this->_get_handshake_digest());

  uint8_t *key_for_hp;
  uint8_t *key;
  uint8_t *iv;
  size_t key_for_hp_len;
  size_t key_len;
  size_t *iv_len;

  key_for_hp     = this->_pp_key_info.encryption_key_for_hp(phase);
  key_for_hp_len = this->_pp_key_info.encryption_key_for_hp_len(phase);
  key            = this->_pp_key_info.encryption_key(phase);
  key_len        = this->_pp_key_info.encryption_key_len(phase);
  iv             = this->_pp_key_info.encryption_iv(phase);
  iv_len         = this->_pp_key_info.encryption_iv_len(phase);

  if (this->_netvc_context == NET_VCONNECTION_IN) {
    this->_keygen_for_server.regenerate(key_for_hp, key, iv, iv_len, secret, secret_len, cipher, hkdf);
    this->_print_km("update - server", key_for_hp, key_for_hp_len, key, key_len, iv, *iv_len, secret, secret_len, phase);
  } else {
    this->_keygen_for_client.regenerate(key_for_hp, key, iv, iv_len, secret, secret_len, cipher, hkdf);
    this->_print_km("update - client", key_for_hp, key_for_hp_len, key, key_len, iv, *iv_len, secret, secret_len, phase);
  }

  this->_pp_key_info.set_encryption_key_available(phase);
}

const char *
QUICTLS::negotiated_cipher_suite() const
{
  return SSL_get_cipher_name(this->_ssl);
}

void
QUICTLS::negotiated_application_name(const uint8_t **name, unsigned int *len) const
{
  SSL_get0_alpn_selected(this->_ssl, name, len);
}

QUICEncryptionLevel
QUICTLS::current_encryption_level() const
{
  return this->_current_level;
}

void
QUICTLS::abort_handshake()
{
  this->_state = HandshakeState::ABORTED;

  return;
}

void
QUICTLS::set_ready_for_write()
{
  this->_should_flush = true;
}

void
QUICTLS::on_handshake_data_generated(QUICEncryptionLevel level, const uint8_t *data, size_t len)
{
  int index      = static_cast<int>(level);
  int next_index = index + 1;

  size_t offset            = this->_out.offsets[next_index];
  size_t next_level_offset = offset + len;

  memcpy(this->_out.buf + offset, data, len);

  for (int i = next_index; i < 5; ++i) {
    this->_out.offsets[i] = next_level_offset;
  }
}

void
QUICTLS::on_tls_alert(uint8_t alert)
{
  this->_has_crypto_error = true;
  this->_crypto_error     = QUICTLS::convert_to_quic_trans_error_code(alert);
}

bool
QUICTLS::has_crypto_error() const
{
  return this->_has_crypto_error;
}

uint64_t
QUICTLS::crypto_error() const
{
  return this->_crypto_error;
}

int
QUICTLS::_handshake(QUICHandshakeMsgs **out, const QUICHandshakeMsgs *in)
{
  ink_assert(this->_ssl != nullptr);
  if (this->_state == HandshakeState::ABORTED) {
    return 0;
  }

  int err = SSL_ERROR_NONE;
  ERR_clear_error();
  int ret = 0;

  SSL_set_msg_callback(this->_ssl, QUICTLS::_msg_cb);

  this->_out.offsets[0] = 0;
  this->_out.offsets[1] = 0;
  this->_out.offsets[2] = 0;
  this->_out.offsets[3] = 0;
  this->_out.offsets[4] = 0;

  if (in) {
    this->_pass_quic_data_to_ssl_impl(*in);
  }

  if (this->_netvc_context == NET_VCONNECTION_IN) {
    if (!this->_early_data_processed) {
      if (auto ret = this->_read_early_data(); ret == 0) {
        this->_early_data_processed = true;
      } else if (ret < 0) {
        return 0;
      } else {
        // Early data is not arrived yet -- can be multiple initial packets
      }
    }

    ret = SSL_accept(this->_ssl);
  } else {
    if (!this->_early_data_processed) {
      if (this->_write_early_data()) {
        this->_early_data_processed = true;
      }
    }

    ret = SSL_connect(this->_ssl);
  }

  if (ret <= 0) {
    err = SSL_get_error(this->_ssl, ret);

    switch (err) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      break;
    default:
      char err_buf[256] = {0};
      ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
      Debug(tag, "Handshake: %s", err_buf);
      return ret;
    }
  }

  if (this->_should_flush) {
    this->_should_flush = false;
    *out                = &this->_out;
  } else {
    *out = nullptr;
  }

  return 1;
}

void
QUICTLS::_update_encryption_level(QUICEncryptionLevel level)
{
  if (this->_current_level < level) {
    this->_current_level = level;
  }

  return;
}

void
QUICTLS::_print_km(const char *header, const uint8_t *key_for_hp, size_t key_for_hp_len, const uint8_t *key, size_t key_len,
                   const uint8_t *iv, size_t iv_len, const uint8_t *secret, size_t secret_len, QUICKeyPhase phase)
{
  if (is_debug_tag_set("vv_quic_crypto")) {
    Debug("vv_quic_crypto", "%s - %s", header, QUICDebugNames::key_phase(phase));
    uint8_t print_buf[128];
    if (secret) {
      QUICDebug::to_hex(print_buf, static_cast<const uint8_t *>(secret), secret_len);
      Debug("vv_quic_crypto", "secret=%s", print_buf);
    }
    QUICDebug::to_hex(print_buf, key, key_len);
    Debug("vv_quic_crypto", "key=%s", print_buf);
    QUICDebug::to_hex(print_buf, iv, iv_len);
    Debug("vv_quic_crypto", "iv=%s", print_buf);
    QUICDebug::to_hex(print_buf, key_for_hp, key_for_hp_len);
    Debug("vv_quic_crypto", "hp=%s", print_buf);
  }
}

void
QUICTLS::_print_hs_message(int content_type, const void *buf, size_t len)
{
  if ((content_type == SSL3_RT_HANDSHAKE || content_type == SSL3_RT_ALERT)) {
    int msg_type = reinterpret_cast<const uint8_t *>(buf)[0];
    Debug(tag, "%s (%d), %s (%d) len=%zu", content_type_str(content_type), content_type, hs_type_str(msg_type), msg_type, len);
  }
}
