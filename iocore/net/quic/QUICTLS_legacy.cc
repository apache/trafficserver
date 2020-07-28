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
#include "QUICTLS.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/kdf.h>
#include <openssl/evp.h>

#include "QUICConfig.h"
#include "QUICGlobals.h"
#include "QUICDebugNames.h"
#include "QUICPacketProtectionKeyInfo.h"

static constexpr char tag[] = "quic_tls";

using namespace std::literals;

static constexpr std::string_view QUIC_CLIENT_EARLY_TRAFFIC_SECRET_LABEL("QUIC_CLIENT_EARLY_TRAFFIC_SECRET"sv);
static constexpr std::string_view QUIC_CLIENT_HANDSHAKE_TRAFFIC_SECRET_LABEL("QUIC_CLIENT_HANDSHAKE_TRAFFIC_SECRET"sv);
static constexpr std::string_view QUIC_SERVER_HANDSHAKE_TRAFFIC_SECRET_LABEL("QUIC_SERVER_HANDSHAKE_TRAFFIC_SECRET"sv);
// TODO: support key update
static constexpr std::string_view QUIC_CLIENT_TRAFFIC_SECRET_LABEL("QUIC_CLIENT_TRAFFIC_SECRET_0"sv);
static constexpr std::string_view QUIC_SERVER_TRAFFIC_SECRET_LABEL("QUIC_SERVER_TRAFFIC_SECRET_0"sv);

void
QUICTLS::_msg_cb(int write_p, int version, int content_type, const void *buf, size_t len, SSL *ssl, void *arg)
{
  // Debug for reading
  if (write_p == 0) {
    QUICTLS::_print_hs_message(content_type, buf, len);
    return;
  }

  if (!write_p || (content_type != SSL3_RT_HANDSHAKE && content_type != SSL3_RT_ALERT)) {
    return;
  }

  QUICTLS *qtls       = static_cast<QUICTLS *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_tls_index));
  const uint8_t *data = reinterpret_cast<const uint8_t *>(buf);
  if (content_type == SSL3_RT_HANDSHAKE) {
    if (version != TLS1_3_VERSION) {
      return;
    }

    QUICEncryptionLevel level = QUICTLS::get_encryption_level(data[0]);
    qtls->on_handshake_data_generated(level, data, len);
    qtls->set_ready_for_write();
  } else if (content_type == SSL3_RT_ALERT && data[0] == SSL3_AL_FATAL && len == 2) {
    qtls->on_tls_alert(data[1]);
  }

  return;
}

/**
   This is very inspired from writing keylog format of ngtcp2's examples
   https://github.com/ngtcp2/ngtcp2/blob/894ed23c970d61eede74f69d9178090af63fdf70/examples/keylog.cc
 */
static void
log_secret(SSL *ssl, int name, const unsigned char *secret, size_t secretlen)
{
  if (auto keylog_cb = SSL_CTX_get_keylog_callback(SSL_get_SSL_CTX(ssl))) {
    unsigned char crandom[32];
    if (SSL_get_client_random(ssl, crandom, sizeof(crandom)) != sizeof(crandom)) {
      return;
    }
    uint8_t line[256] = {0};
    size_t len        = 0;
    switch (name) {
    case SSL_KEY_CLIENT_EARLY_TRAFFIC:
      memcpy(line, QUIC_CLIENT_EARLY_TRAFFIC_SECRET_LABEL.data(), QUIC_CLIENT_EARLY_TRAFFIC_SECRET_LABEL.size());
      len += QUIC_CLIENT_EARLY_TRAFFIC_SECRET_LABEL.size();
      break;
    case SSL_KEY_CLIENT_HANDSHAKE_TRAFFIC:
      memcpy(line, QUIC_CLIENT_HANDSHAKE_TRAFFIC_SECRET_LABEL.data(), QUIC_CLIENT_HANDSHAKE_TRAFFIC_SECRET_LABEL.size());
      len += QUIC_CLIENT_HANDSHAKE_TRAFFIC_SECRET_LABEL.size();
      break;
    case SSL_KEY_SERVER_HANDSHAKE_TRAFFIC:
      memcpy(line, QUIC_SERVER_HANDSHAKE_TRAFFIC_SECRET_LABEL.data(), QUIC_SERVER_HANDSHAKE_TRAFFIC_SECRET_LABEL.size());
      len += QUIC_SERVER_HANDSHAKE_TRAFFIC_SECRET_LABEL.size();
      break;
    case SSL_KEY_CLIENT_APPLICATION_TRAFFIC:
      memcpy(line, QUIC_CLIENT_TRAFFIC_SECRET_LABEL.data(), QUIC_CLIENT_TRAFFIC_SECRET_LABEL.size());
      len += QUIC_CLIENT_TRAFFIC_SECRET_LABEL.size();
      break;
    case SSL_KEY_SERVER_APPLICATION_TRAFFIC:
      memcpy(line, QUIC_SERVER_TRAFFIC_SECRET_LABEL.data(), QUIC_SERVER_TRAFFIC_SECRET_LABEL.size());
      len += QUIC_SERVER_TRAFFIC_SECRET_LABEL.size();
      break;

    default:
      return;
    }

    line[len] = ' ';
    ++len;
    QUICDebug::to_hex(line + len, crandom, sizeof(crandom));
    len += sizeof(crandom) * 2;
    line[len] = ' ';
    ++len;
    QUICDebug::to_hex(line + len, secret, secretlen);

    keylog_cb(ssl, reinterpret_cast<char *>(line));
  }
}

static int
key_cb(SSL *ssl, int name, const unsigned char *secret, size_t secret_len, void *arg)
{
  if (arg == nullptr) {
    return 0;
  }

  QUICTLS *qtls = reinterpret_cast<QUICTLS *>(arg);

  qtls->update_negotiated_cipher();

  QUICEncryptionLevel level;
  switch (name) {
  case SSL_KEY_CLIENT_EARLY_TRAFFIC:
    Debug("vv_quic_crypto", "%s", QUIC_CLIENT_EARLY_TRAFFIC_SECRET_LABEL.data());
    level = QUICEncryptionLevel::ZERO_RTT;
    if (SSL_is_server(ssl)) {
      qtls->update_key_materials_for_read(level, secret, secret_len);
    } else {
      qtls->update_key_materials_for_write(level, secret, secret_len);
    }
    break;
  case SSL_KEY_CLIENT_HANDSHAKE_TRAFFIC:
    Debug("vv_quic_crypto", "%s", QUIC_CLIENT_HANDSHAKE_TRAFFIC_SECRET_LABEL.data());
    level = QUICEncryptionLevel::HANDSHAKE;
    if (SSL_is_server(ssl)) {
      qtls->update_key_materials_for_read(level, secret, secret_len);
    } else {
      qtls->update_key_materials_for_write(level, secret, secret_len);
    }
    break;
  case SSL_KEY_SERVER_HANDSHAKE_TRAFFIC:
    Debug("vv_quic_crypto", "%s", QUIC_SERVER_HANDSHAKE_TRAFFIC_SECRET_LABEL.data());
    level = QUICEncryptionLevel::HANDSHAKE;
    if (SSL_is_server(ssl)) {
      qtls->update_key_materials_for_write(level, secret, secret_len);
    } else {
      qtls->update_key_materials_for_read(level, secret, secret_len);
    }
    break;
  case SSL_KEY_CLIENT_APPLICATION_TRAFFIC:
    Debug("vv_quic_crypto", "%s", QUIC_CLIENT_TRAFFIC_SECRET_LABEL.data());
    level = QUICEncryptionLevel::ONE_RTT;
    if (SSL_is_server(ssl)) {
      qtls->update_key_materials_for_read(level, secret, secret_len);
    } else {
      qtls->update_key_materials_for_write(level, secret, secret_len);
    }
    break;
  case SSL_KEY_SERVER_APPLICATION_TRAFFIC:
    Debug("vv_quic_crypto", "%s", QUIC_SERVER_TRAFFIC_SECRET_LABEL.data());
    level = QUICEncryptionLevel::ONE_RTT;
    if (SSL_is_server(ssl)) {
      qtls->update_key_materials_for_write(level, secret, secret_len);
    } else {
      qtls->update_key_materials_for_read(level, secret, secret_len);
    }
    break;
  default:
    level = QUICEncryptionLevel::NONE;
    break;
  }

  log_secret(ssl, name, secret, secret_len);

  return 1;
}

QUICTLS::QUICTLS(QUICPacketProtectionKeyInfo &pp_key_info, SSL_CTX *ssl_ctx, NetVConnectionContext_t nvc_ctx,
                 const NetVCOptions &netvc_options, const char *session_file, const char *keylog_file)
  : QUICHandshakeProtocol(pp_key_info),
    _session_file(session_file),
    _keylog_file(keylog_file),
    _ssl(SSL_new(ssl_ctx)),
    _netvc_context(nvc_ctx)
{
  ink_assert(this->_netvc_context != NET_VCONNECTION_UNSET);

  if (this->_netvc_context == NET_VCONNECTION_OUT) {
    SSL_set_connect_state(this->_ssl);

    SSL_set_alpn_protos(this->_ssl, reinterpret_cast<const unsigned char *>(netvc_options.alpn_protos.data()),
                        netvc_options.alpn_protos.size());
    const ats_scoped_str &tlsext_host_name = netvc_options.sni_hostname ? netvc_options.sni_hostname : netvc_options.sni_servername;
    SSL_set_tlsext_host_name(this->_ssl, tlsext_host_name.get());
  } else {
    SSL_set_accept_state(this->_ssl);
  }

  SSL_set_ex_data(this->_ssl, QUIC::ssl_quic_tls_index, this);
  SSL_set_key_callback(this->_ssl, key_cb, this);

  if (session_file && this->_netvc_context == NET_VCONNECTION_OUT) {
    auto file = BIO_new_file(session_file, "r");
    if (file == nullptr) {
      Debug(tag, "Could not read tls session file %s", session_file);
      return;
    }

    auto session = PEM_read_bio_SSL_SESSION(file, nullptr, nullptr, nullptr);
    if (session == nullptr) {
      Debug(tag, "Could not read tls session file %s", session_file);
    } else {
      if (!SSL_set_session(this->_ssl, session)) {
        Debug(tag, "Session resumption failed : %s", session_file);
      } else {
        Debug(tag, "Session resumption success : %s", session_file);
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

void
QUICTLS::set_local_transport_parameters(std::shared_ptr<const QUICTransportParameters> tp)
{
  this->_local_transport_parameters = tp;
}

int
QUICTLS::_process_post_handshake_messages(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in)
{
  ink_assert(this->_ssl != nullptr);

  int err = SSL_ERROR_NONE;
  ERR_clear_error();
  int ret = 0;

  SSL_set_msg_callback(this->_ssl, QUICTLS::_msg_cb);
  SSL_set_msg_callback_arg(this->_ssl, out);

  this->_pass_quic_data_to_ssl_impl(*in);

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
QUICTLS::_store_negotiated_cipher()
{
  ink_assert(this->_ssl);

  const QUIC_EVP_CIPHER *cipher = nullptr;
  size_t tag_len                = 0;
  const SSL_CIPHER *ssl_cipher  = SSL_get_current_cipher(this->_ssl);

  if (ssl_cipher) {
    switch (SSL_CIPHER_get_id(ssl_cipher)) {
    case TLS1_3_CK_AES_128_GCM_SHA256:
      cipher  = EVP_aes_128_gcm();
      tag_len = EVP_GCM_TLS_TAG_LEN;
      break;
    case TLS1_3_CK_AES_256_GCM_SHA384:
      cipher  = EVP_aes_256_gcm();
      tag_len = EVP_GCM_TLS_TAG_LEN;
      break;
    case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
      cipher  = EVP_chacha20_poly1305();
      tag_len = EVP_CHACHAPOLY_TLS_TAG_LEN;
      break;
    case TLS1_3_CK_AES_128_CCM_SHA256:
      cipher  = EVP_aes_128_ccm();
      tag_len = EVP_GCM_TLS_TAG_LEN;
      break;
    case TLS1_3_CK_AES_128_CCM_8_SHA256:
      cipher  = EVP_aes_128_ccm();
      tag_len = EVP_CCM8_TLS_TAG_LEN;
      break;
    default:
      ink_assert(false);
    }
  } else {
    ink_assert(false);
  }

  this->_pp_key_info.set_cipher(cipher, tag_len);
}

void
QUICTLS::_store_negotiated_cipher_for_hp()
{
  ink_assert(this->_ssl);

  const QUIC_EVP_CIPHER *cipher_for_hp = nullptr;
  const SSL_CIPHER *ssl_cipher         = SSL_get_current_cipher(this->_ssl);

  if (ssl_cipher) {
    switch (SSL_CIPHER_get_id(ssl_cipher)) {
    case TLS1_3_CK_AES_128_GCM_SHA256:
      cipher_for_hp = EVP_aes_128_ecb();
      break;
    case TLS1_3_CK_AES_256_GCM_SHA384:
      cipher_for_hp = EVP_aes_256_ecb();
      break;
    case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
      cipher_for_hp = EVP_chacha20();
      break;
    case TLS1_3_CK_AES_128_CCM_SHA256:
    case TLS1_3_CK_AES_128_CCM_8_SHA256:
      cipher_for_hp = EVP_aes_128_ecb();
      break;
    default:
      ink_assert(false);
      break;
    }
  } else {
    ink_assert(false);
  }

  this->_pp_key_info.set_cipher_for_hp(cipher_for_hp);
}

int
QUICTLS::_read_early_data()
{
  uint8_t early_data[8];
  size_t early_data_len = 0;

  // Early data within the TLS connection MUST NOT be used. As it is for other TLS application data, a server MUST treat receiving
  // early data on the TLS connection as a connection error of type PROTOCOL_VIOLATION.
  int ret = SSL_read_early_data(this->_ssl, early_data, sizeof(early_data), &early_data_len);
  // error or reading empty data return 1, otherwise return 0.
  if (early_data_len != 0) {
    return -1;
  }
  if (ret == SSL_READ_EARLY_DATA_FINISH) {
    return 0;
  } else {
    return 1;
  }
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

void
QUICTLS::_pass_quic_data_to_ssl_impl(const QUICHandshakeMsgs &in)
{
  // TODO: set BIO_METHOD which read from QUICHandshakeMsgs directly
  BIO *rbio = BIO_new(BIO_s_mem());
  // TODO: set dummy BIO_METHOD which do nothing
  BIO *wbio = BIO_new(BIO_s_mem());
  if (in.offsets[4] != 0) {
    BIO_write(rbio, in.buf, in.offsets[4]);
  }
  SSL_set_bio(this->_ssl, rbio, wbio);
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
