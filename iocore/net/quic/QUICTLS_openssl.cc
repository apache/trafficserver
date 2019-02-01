/** @file
 *
 *  QUIC Crypto (TLS to Secure QUIC) using OpenSSL
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
#include "QUICGlobals.h"
#include "QUICTLS.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/kdf.h>
#include <openssl/evp.h>

#include "QUICConfig.h"

#include "QUICDebugNames.h"

static constexpr char tag[] = "quic_tls";

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
  case SSL3_RT_INNER_CONTENT_TYPE:
    // Used when an encrypted TLSv1.3 record is sent or received. In encrypted TLSv1.3 records the content type in the record header
    // is always SSL3_RT_APPLICATION_DATA. The real content type for the record is contained in an "inner" content type. buf
    // contains the encoded "inner" content type byte.
    return "INNER_CONTENT_TYPE";
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

static void
msg_cb(int write_p, int version, int content_type, const void *buf, size_t len, SSL *ssl, void *arg)
{
  // Debug for reading
  if (write_p == 0 && (content_type == SSL3_RT_HANDSHAKE || content_type == SSL3_RT_ALERT)) {
    const uint8_t *tmp = reinterpret_cast<const uint8_t *>(buf);
    int msg_type       = tmp[0];

    Debug(tag, "%s (%d), %s (%d) len=%zu", content_type_str(content_type), content_type, hs_type_str(msg_type), msg_type, len);
    return;
  }

  if (!write_p || !arg || (content_type != SSL3_RT_HANDSHAKE && content_type != SSL3_RT_ALERT)) {
    return;
  }

  QUICHandshakeMsgs *msg = reinterpret_cast<QUICHandshakeMsgs *>(arg);
  if (msg == nullptr) {
    return;
  }

  const uint8_t *msg_buf = reinterpret_cast<const uint8_t *>(buf);

  if (content_type == SSL3_RT_HANDSHAKE) {
    if (version != TLS1_3_VERSION) {
      return;
    }

    int msg_type = msg_buf[0];

    QUICEncryptionLevel level = QUICTLS::get_encryption_level(msg_type);
    int index                 = static_cast<int>(level);
    int next_index            = index + 1;

    size_t offset            = msg->offsets[next_index];
    size_t next_level_offset = offset + len;

    memcpy(msg->buf + offset, buf, len);

    for (int i = next_index; i < 5; ++i) {
      msg->offsets[i] = next_level_offset;
    }
  } else if (content_type == SSL3_RT_ALERT && msg_buf[0] == SSL3_AL_FATAL && len == 2) {
    msg->error_code = QUICTLS::convert_to_quic_trans_error_code(msg_buf[1]);
  }

  return;
}

static int
key_cb(SSL *ssl, int name, const unsigned char *secret, size_t secret_len, void *arg)
{
  if (arg == nullptr) {
    return 0;
  }

  QUICTLS *qtls = reinterpret_cast<QUICTLS *>(arg);
  qtls->update_key_materials_on_key_cb(name, secret, secret_len);

  return 1;
}

void
QUICTLS::update_key_materials_on_key_cb(int name, const uint8_t *secret, size_t secret_len)
{
  if (is_debug_tag_set("vv_quic_crypto")) {
    switch (name) {
    case SSL_KEY_CLIENT_EARLY_TRAFFIC:
      Debug("vv_quic_crypto", "client_early_traffic");
      break;
    case SSL_KEY_CLIENT_HANDSHAKE_TRAFFIC:
      Debug("vv_quic_crypto", "client_handshake_traffic");
      break;
    case SSL_KEY_CLIENT_APPLICATION_TRAFFIC:
      Debug("vv_quic_crypto", "client_application_traffic");
      break;
    case SSL_KEY_SERVER_HANDSHAKE_TRAFFIC:
      Debug("vv_quic_crypto", "server_handshake_traffic");
      break;
    case SSL_KEY_SERVER_APPLICATION_TRAFFIC:
      Debug("vv_quic_crypto", "server_application_traffic");
      break;
    default:
      break;
    }
  }

  if (this->_state == HandshakeState::ABORTED) {
    return;
  }

  QUICKeyPhase phase;
  const QUIC_EVP_CIPHER *cipher;
  QUICHKDF hkdf(this->_get_handshake_digest());
  std::unique_ptr<KeyMaterial> km = nullptr;

  switch (name) {
  case SSL_KEY_CLIENT_EARLY_TRAFFIC:
    // this->_update_encryption_level(QUICEncryptionLevel::ZERO_RTT);
    phase  = QUICKeyPhase::ZERO_RTT;
    cipher = this->_get_evp_aead(phase);
    km     = this->_keygen_for_client.regenerate(secret, secret_len, cipher, hkdf);
    this->_print_km("update - client - 0rtt", *km, secret, secret_len);
    this->_client_pp->set_key(std::move(km), phase);
    break;
  case SSL_KEY_CLIENT_HANDSHAKE_TRAFFIC:
    this->_update_encryption_level(QUICEncryptionLevel::HANDSHAKE);
    phase  = QUICKeyPhase::HANDSHAKE;
    cipher = this->_get_evp_aead(phase);
    km     = this->_keygen_for_client.regenerate(secret, secret_len, cipher, hkdf);
    this->_print_km("update - client - handshake", *km, secret, secret_len);
    this->_client_pp->set_key(std::move(km), phase);
    break;
  case SSL_KEY_CLIENT_APPLICATION_TRAFFIC:
    this->_update_encryption_level(QUICEncryptionLevel::ONE_RTT);
    phase  = QUICKeyPhase::PHASE_0;
    cipher = this->_get_evp_aead(phase);
    km     = this->_keygen_for_client.regenerate(secret, secret_len, cipher, hkdf);
    this->_print_km("update - client - 1rtt", *km, secret, secret_len);
    this->_client_pp->set_key(std::move(km), phase);
    break;
  case SSL_KEY_SERVER_HANDSHAKE_TRAFFIC:
    this->_update_encryption_level(QUICEncryptionLevel::HANDSHAKE);
    phase  = QUICKeyPhase::HANDSHAKE;
    cipher = this->_get_evp_aead(phase);
    km     = this->_keygen_for_server.regenerate(secret, secret_len, cipher, hkdf);
    this->_print_km("update - server - handshake", *km, secret, secret_len);
    this->_server_pp->set_key(std::move(km), phase);
    break;
  case SSL_KEY_SERVER_APPLICATION_TRAFFIC:
    this->_update_encryption_level(QUICEncryptionLevel::ONE_RTT);
    phase  = QUICKeyPhase::PHASE_0;
    cipher = this->_get_evp_aead(phase);
    km     = this->_keygen_for_server.regenerate(secret, secret_len, cipher, hkdf);
    this->_print_km("update - server - 1rtt", *km, secret, secret_len);
    this->_server_pp->set_key(std::move(km), phase);
    break;
  default:
    break;
  }

  return;
}

QUICTLS::QUICTLS(SSL_CTX *ssl_ctx, NetVConnectionContext_t nvc_ctx)
  : QUICHandshakeProtocol(), _ssl(SSL_new(ssl_ctx)), _netvc_context(nvc_ctx)
{
  ink_assert(this->_netvc_context != NET_VCONNECTION_UNSET);

  if (this->_netvc_context == NET_VCONNECTION_OUT) {
    SSL_set_connect_state(this->_ssl);
  } else {
    SSL_set_accept_state(this->_ssl);
  }

  this->_client_pp = new QUICPacketProtection();
  this->_server_pp = new QUICPacketProtection();

  SSL_set_ex_data(this->_ssl, QUIC::ssl_quic_tls_index, this);
  SSL_set_key_callback(this->_ssl, key_cb, this);

  QUICConfig::scoped_config params;
  if (params->session_file() && this->_netvc_context == NET_VCONNECTION_OUT) {
    auto file = BIO_new_file(params->session_file(), "r");
    if (file == nullptr) {
      Debug(tag, "Could not read tls session file %s", params->session_file());
      return;
    }

    auto session = PEM_read_bio_SSL_SESSION(file, nullptr, 0, nullptr);
    if (session == nullptr) {
      Debug(tag, "Could not read tls session file %s", params->session_file());
    } else {
      if (!SSL_set_session(this->_ssl, session)) {
        Debug(tag, "Session resumption failed : %s", params->session_file());
      } else {
        Debug(tag, "Session resumption success : %s", params->session_file());
        this->_is_session_reused = true;
      }
      SSL_SESSION_free(session);
    }

    BIO_free(file);
  }
}

QUICEncryptionLevel
QUICTLS::get_encryption_level(int msg_type)
{
  switch (msg_type) {
  case SSL3_MT_CLIENT_HELLO:
  case SSL3_MT_SERVER_HELLO:
    return QUICEncryptionLevel::INITIAL;
  case SSL3_MT_END_OF_EARLY_DATA:
    return QUICEncryptionLevel::ZERO_RTT;
  case SSL3_MT_ENCRYPTED_EXTENSIONS:
  case SSL3_MT_CERTIFICATE_REQUEST:
  case SSL3_MT_CERTIFICATE:
  case SSL3_MT_CERTIFICATE_VERIFY:
  case SSL3_MT_FINISHED:
    return QUICEncryptionLevel::HANDSHAKE;
  case SSL3_MT_KEY_UPDATE:
  case SSL3_MT_NEWSESSION_TICKET:
    return QUICEncryptionLevel::ONE_RTT;
  default:
    return QUICEncryptionLevel::NONE;
  }
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

  SSL_set_msg_callback(this->_ssl, msg_cb);
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
      if (this->_read_early_data() != 1) {
        out->error_code = static_cast<uint16_t>(QUICTransErrorCode::PROTOCOL_VIOLATION);
        return 0;
      } else {
        this->_early_data_processed = true;
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

int
QUICTLS::_process_post_handshake_messages(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in)
{
  ink_assert(this->_ssl != nullptr);

  int err = SSL_ERROR_NONE;
  ERR_clear_error();
  int ret = 0;

  SSL_set_msg_callback(this->_ssl, msg_cb);
  SSL_set_msg_callback_arg(this->_ssl, out);

  // TODO: set BIO_METHOD which read from QUICHandshakeMsgs directly
  BIO *rbio = BIO_new(BIO_s_mem());
  // TODO: set dummy BIO_METHOD which do nothing
  BIO *wbio = BIO_new(BIO_s_mem());
  if (in != nullptr && in->offsets[4] != 0) {
    BIO_write(rbio, in->buf, in->offsets[4]);
  }
  SSL_set_bio(this->_ssl, rbio, wbio);

  uint8_t data[2048];
  size_t l = 0;
  ret      = SSL_read_ex(this->_ssl, data, 2048, &l);

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
QUICTLS::reset()
{
  SSL_clear(this->_ssl);
}

const EVP_CIPHER *
QUICTLS::cipher_for_hp(QUICKeyPhase phase) const
{
  if (phase == QUICKeyPhase::INITIAL) {
    return EVP_aes_128_ecb();
  } else {
    const SSL_CIPHER *cipher = SSL_get_current_cipher(this->_ssl);
    if (cipher) {
      switch (SSL_CIPHER_get_id(cipher)) {
      case TLS1_3_CK_AES_128_GCM_SHA256:
        return EVP_aes_128_ecb();
      case TLS1_3_CK_AES_256_GCM_SHA384:
        return EVP_aes_256_ecb();
      case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
        return EVP_chacha20();
      case TLS1_3_CK_AES_128_CCM_SHA256:
      case TLS1_3_CK_AES_128_CCM_8_SHA256:
        return EVP_aes_128_ecb();
      default:
        ink_assert(false);
        return nullptr;
      }
    } else {
      ink_assert(false);
      return nullptr;
    }
  }
}

int
QUICTLS::_read_early_data()
{
  uint8_t early_data[8];
  size_t early_data_len = 0;

  // Early data within the TLS connection MUST NOT be used. As it is for other TLS application data, a server MUST treat receiving
  // early data on the TLS connection as a connection error of type PROTOCOL_VIOLATION.
  SSL_read_early_data(this->_ssl, early_data, sizeof(early_data), &early_data_len);
  // error or reading empty data return 1, otherwise return 0.
  return early_data_len != 0 ? 0 : 1;
}

int
QUICTLS::_write_early_data()
{
  size_t early_data_len = 0;

  // Early data within the TLS connection MUST NOT be used. As it is for other TLS application data, a server MUST treat receiving
  // early data on the TLS connection as a connection error of type PROTOCOL_VIOLATION.
  SSL_write_early_data(this->_ssl, "", 0, &early_data_len);
  // always return 1
  return 1;
}

const EVP_CIPHER *
QUICTLS::_get_evp_aead(QUICKeyPhase phase) const
{
  if (phase == QUICKeyPhase::INITIAL) {
    return EVP_aes_128_gcm();
  } else {
    const SSL_CIPHER *cipher = SSL_get_current_cipher(this->_ssl);
    if (cipher) {
      switch (SSL_CIPHER_get_id(cipher)) {
      case TLS1_3_CK_AES_128_GCM_SHA256:
        return EVP_aes_128_gcm();
      case TLS1_3_CK_AES_256_GCM_SHA384:
        return EVP_aes_256_gcm();
      case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
        return EVP_chacha20_poly1305();
      case TLS1_3_CK_AES_128_CCM_SHA256:
      case TLS1_3_CK_AES_128_CCM_8_SHA256:
        return EVP_aes_128_ccm();
      default:
        ink_assert(false);
        return nullptr;
      }
    } else {
      ink_assert(false);
      return nullptr;
    }
  }
}

size_t
QUICTLS::_get_aead_tag_len(QUICKeyPhase phase) const
{
  if (phase == QUICKeyPhase::INITIAL) {
    return EVP_GCM_TLS_TAG_LEN;
  } else {
    const SSL_CIPHER *cipher = SSL_get_current_cipher(this->_ssl);
    if (cipher) {
      switch (SSL_CIPHER_get_id(cipher)) {
      case TLS1_3_CK_AES_128_GCM_SHA256:
      case TLS1_3_CK_AES_256_GCM_SHA384:
        return EVP_GCM_TLS_TAG_LEN;
      case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
        return EVP_CHACHAPOLY_TLS_TAG_LEN;
      case TLS1_3_CK_AES_128_CCM_SHA256:
        return EVP_CCM_TLS_TAG_LEN;
      case TLS1_3_CK_AES_128_CCM_8_SHA256:
        return EVP_CCM8_TLS_TAG_LEN;
      default:
        ink_assert(false);
        return -1;
      }
    } else {
      ink_assert(false);
      return -1;
    }
  }
}

const EVP_MD *
QUICTLS::_get_handshake_digest() const
{
  switch (SSL_CIPHER_get_id(SSL_get_current_cipher(this->_ssl))) {
  case TLS1_3_CK_AES_128_GCM_SHA256:
  case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
  case TLS1_3_CK_AES_128_CCM_SHA256:
  case TLS1_3_CK_AES_128_CCM_8_SHA256:
    return EVP_sha256();
  case TLS1_3_CK_AES_256_GCM_SHA384:
    return EVP_sha384();
  default:
    ink_assert(false);
    return nullptr;
  }
}

bool
QUICTLS::_encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len,
                  uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const KeyMaterial &km, const EVP_CIPHER *aead,
                  size_t tag_len) const
{
  uint8_t nonce[EVP_MAX_IV_LENGTH] = {0};
  size_t nonce_len                 = 0;
  _gen_nonce(nonce, nonce_len, pkt_num, km.iv, km.iv_len);

  EVP_CIPHER_CTX *aead_ctx;
  int len;

  if (!(aead_ctx = EVP_CIPHER_CTX_new())) {
    return false;
  }
  if (!EVP_EncryptInit_ex(aead_ctx, aead, nullptr, nullptr, nullptr)) {
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_IVLEN, nonce_len, nullptr)) {
    return false;
  }
  if (!EVP_EncryptInit_ex(aead_ctx, nullptr, nullptr, km.key, nonce)) {
    return false;
  }
  if (!EVP_EncryptUpdate(aead_ctx, nullptr, &len, ad, ad_len)) {
    return false;
  }
  if (!EVP_EncryptUpdate(aead_ctx, cipher, &len, plain, plain_len)) {
    return false;
  }
  cipher_len = len;

  if (!EVP_EncryptFinal_ex(aead_ctx, cipher + len, &len)) {
    return false;
  }
  cipher_len += len;

  if (max_cipher_len < cipher_len + tag_len) {
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_GET_TAG, tag_len, cipher + cipher_len)) {
    return false;
  }
  cipher_len += tag_len;

  EVP_CIPHER_CTX_free(aead_ctx);

  return true;
}

bool
QUICTLS::_decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len,
                  uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const KeyMaterial &km, const EVP_CIPHER *aead,
                  size_t tag_len) const
{
  uint8_t nonce[EVP_MAX_IV_LENGTH] = {0};
  size_t nonce_len                 = 0;
  _gen_nonce(nonce, nonce_len, pkt_num, km.iv, km.iv_len);

  EVP_CIPHER_CTX *aead_ctx;
  int len;

  if (!(aead_ctx = EVP_CIPHER_CTX_new())) {
    return false;
  }
  if (!EVP_DecryptInit_ex(aead_ctx, aead, nullptr, nullptr, nullptr)) {
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_IVLEN, nonce_len, nullptr)) {
    return false;
  }
  if (!EVP_DecryptInit_ex(aead_ctx, nullptr, nullptr, km.key, nonce)) {
    return false;
  }
  if (!EVP_DecryptUpdate(aead_ctx, nullptr, &len, ad, ad_len)) {
    return false;
  }

  if (cipher_len < tag_len) {
    return false;
  }
  cipher_len -= tag_len;
  if (!EVP_DecryptUpdate(aead_ctx, plain, &len, cipher, cipher_len)) {
    return false;
  }
  plain_len = len;

  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_TAG, tag_len, const_cast<uint8_t *>(cipher + cipher_len))) {
    return false;
  }

  int ret = EVP_DecryptFinal_ex(aead_ctx, plain + len, &len);

  EVP_CIPHER_CTX_free(aead_ctx);

  if (ret > 0) {
    plain_len += len;
    return true;
  } else {
    Debug(tag, "Failed to decrypt -- the first 4 bytes decrypted are %0x %0x %0x %0x", plain[0], plain[1], plain[2], plain[3]);
    return false;
  }
}
