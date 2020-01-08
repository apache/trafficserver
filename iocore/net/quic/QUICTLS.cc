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
QUICTLS::set_local_transport_parameters(std::shared_ptr<const QUICTransportParameters> tp)
{
  this->_local_transport_parameters = tp;
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

const char *
QUICTLS::keylog_file() const
{
  return this->_keylog_file;
}

QUICTLS::~QUICTLS()
{
  SSL_free(this->_ssl);
}

int
QUICTLS::handshake(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in)
{
  if (this->is_handshake_finished()) {
    if (in != nullptr && in->offsets[4] != 0) {
      return this->_process_post_handshake_messages(out, in);
    }

    return 0;
  }

  return this->_handshake(out, in);
}

void
QUICTLS::reset()
{
  SSL_clear(this->_ssl);
}

uint16_t
QUICTLS::convert_to_quic_trans_error_code(uint8_t alert)
{
  return 0x100 | alert;
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
QUICTLS::initialize_key_materials(QUICConnectionId cid)
{
  this->_pp_key_info.set_cipher_initial(EVP_aes_128_gcm());
  this->_pp_key_info.set_cipher_for_hp_initial(EVP_aes_128_ecb());

  // Generate keys
  if (is_debug_tag_set(tag)) {
    char cid_hex_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    cid.hex(cid_hex_str, QUICConnectionId::MAX_HEX_STR_LENGTH);
    Debug(tag, "Generating %s keys with cid %s", QUICDebugNames::key_phase(QUICKeyPhase::INITIAL), cid_hex_str);
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

  this->_keygen_for_client.generate(client_key_for_hp, client_key, client_iv, client_iv_len, cid);
  this->_keygen_for_server.generate(server_key_for_hp, server_key, server_iv, server_iv_len, cid);

  this->_pp_key_info.set_decryption_key_available(QUICKeyPhase::INITIAL);
  this->_pp_key_info.set_encryption_key_available(QUICKeyPhase::INITIAL);

  this->_print_km("initial - server", server_key_for_hp, server_key_for_hp_len, server_key, server_key_len, server_iv,
                  *server_iv_len);
  this->_print_km("initial - client", client_key_for_hp, client_key_for_hp_len, client_key, client_key_len, client_iv,
                  *client_iv_len);

  return 1;
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

int
QUICTLS::_handshake(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in)
{
  ink_assert(this->_ssl != nullptr);
  if (this->_state == HandshakeState::ABORTED) {
    return 0;
  }

  int err = SSL_ERROR_NONE;
  ERR_clear_error();
  int ret = 0;

  SSL_set_msg_callback(this->_ssl, QUICTLS::_msg_cb);
  SSL_set_msg_callback_arg(this->_ssl, out);

  // TODO: set BIO_METHOD which read from QUICHandshakeMsgs directly
  BIO *rbio = BIO_new(BIO_s_mem());
  // TODO: set dummy BIO_METHOD which do nothing
  BIO *wbio = BIO_new(BIO_s_mem());
  if (in != nullptr && in->offsets[4] != 0) {
    BIO_write(rbio, in->buf, in->offsets[4]);
  }
  SSL_set_bio(this->_ssl, rbio, wbio);

  if (this->_netvc_context == NET_VCONNECTION_IN) {
    if (!this->_early_data_processed) {
      if (auto ret = this->_read_early_data(); ret == 0) {
        this->_early_data_processed = true;
      } else if (ret < 0) {
        out->error_code = static_cast<uint16_t>(QUICTransErrorCode::PROTOCOL_VIOLATION);
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
                   const uint8_t *iv, size_t iv_len, const uint8_t *secret, size_t secret_len)
{
  if (is_debug_tag_set("vv_quic_crypto")) {
    Debug("vv_quic_crypto", "%s", header);
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
