/** @file
 *
 *  QUIC Crypto (TLS to Secure QUIC) using BoringSSL
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

#include <openssl/base.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/hkdf.h>
#include <openssl/aead.h>

#include "QUICGlobals.h"
#include "QUICConnection.h"
#include "QUICPacketProtectionKeyInfo.h"

static constexpr char tag[] = "quic_tls";

static QUICEncryptionLevel
convert_level_ats2ssl(enum ssl_encryption_level_t level)
{
  switch (level) {
  case ssl_encryption_initial:
    return QUICEncryptionLevel::INITIAL;
  case ssl_encryption_early_data:
    return QUICEncryptionLevel::ZERO_RTT;
  case ssl_encryption_handshake:
    return QUICEncryptionLevel::HANDSHAKE;
  case ssl_encryption_application:
    return QUICEncryptionLevel::ONE_RTT;
  default:
    return QUICEncryptionLevel::NONE;
  }
}

#if BORINGSSL_API_VERSION >= 10
static int
set_read_secret(SSL *ssl, enum ssl_encryption_level_t level, const SSL_CIPHER *cipher, const uint8_t *secret, size_t secret_len)
{
  QUICTLS *qtls = static_cast<QUICTLS *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_tls_index));

  qtls->update_negotiated_cipher();

  QUICEncryptionLevel ats_level = convert_level_ats2ssl(level);
  qtls->update_key_materials_for_read(ats_level, secret, secret_len);

  return 1;
}

static int
set_write_secret(SSL *ssl, enum ssl_encryption_level_t level, const SSL_CIPHER *cipher, const uint8_t *secret, size_t secret_len)
{
  QUICTLS *qtls = static_cast<QUICTLS *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_tls_index));

  qtls->update_negotiated_cipher();

  QUICEncryptionLevel ats_level = convert_level_ats2ssl(level);
  qtls->update_key_materials_for_write(ats_level, secret, secret_len);

  if (ats_level == QUICEncryptionLevel::ONE_RTT) {
    // FIXME Where should this be placed?
    const uint8_t *tp_buf;
    size_t tp_buf_len;
    SSL_get_peer_quic_transport_params(ssl, &tp_buf, &tp_buf_len);
    const QUICConnection *qc = static_cast<const QUICConnection *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_qc_index));
    QUICVersion version      = qc->negotiated_version();
    if (SSL_is_server(ssl)) {
      qtls->set_remote_transport_parameters(std::make_shared<QUICTransportParametersInClientHello>(tp_buf, tp_buf_len, version));
    } else {
      qtls->set_remote_transport_parameters(
        std::make_shared<QUICTransportParametersInEncryptedExtensions>(tp_buf, tp_buf_len, version));
    }
  }

  return 1;
}
#else
static int
set_encryption_secrets(SSL *ssl, enum ssl_encryption_level_t level, const uint8_t *read_secret, const uint8_t *write_secret,
                       size_t secret_len)
{
  QUICTLS *qtls = static_cast<QUICTLS *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_tls_index));

  qtls->update_negotiated_cipher();

  QUICEncryptionLevel ats_level = convert_level_ats2ssl(level);
  if (read_secret) {
    qtls->update_key_materials_for_read(ats_level, read_secret, secret_len);
  }
  if (write_secret) {
    qtls->update_key_materials_for_write(ats_level, write_secret, secret_len);
  }

  if (ats_level == QUICEncryptionLevel::ONE_RTT) {
    // FIXME Where should this be placed?
    const uint8_t *tp_buf;
    size_t tp_buf_len;
    SSL_get_peer_quic_transport_params(ssl, &tp_buf, &tp_buf_len);
    const QUICConnection *qc = static_cast<const QUICConnection *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_qc_index));
    QUICVersion version      = qc->negotiated_version();
    if (SSL_is_server(ssl)) {
      qtls->set_remote_transport_parameters(std::make_shared<QUICTransportParametersInClientHello>(tp_buf, tp_buf_len, version));
    } else {
      qtls->set_remote_transport_parameters(
        std::make_shared<QUICTransportParametersInEncryptedExtensions>(tp_buf, tp_buf_len, version));
    }
  }

  return 1;
}
#endif

static int
add_handshake_data(SSL *ssl, enum ssl_encryption_level_t level, const uint8_t *data, size_t len)
{
  QUICEncryptionLevel ats_level = convert_level_ats2ssl(level);

  QUICTLS *qtls = static_cast<QUICTLS *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_tls_index));
  qtls->on_handshake_data_generated(ats_level, data, len);

  return 1;
}

static int
flush_flight(SSL *ssl)
{
  QUICTLS *qtls = static_cast<QUICTLS *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_tls_index));
  qtls->set_ready_for_write();

  return 1;
}

static int
send_alert(SSL *ssl, enum ssl_encryption_level_t level, uint8_t alert)
{
  QUICTLS *qtls = static_cast<QUICTLS *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_tls_index));
  qtls->on_tls_alert(alert);
  return 1;
}

#if BORINGSSL_API_VERSION >= 10
static const SSL_QUIC_METHOD quic_method = {set_read_secret, set_write_secret, add_handshake_data, flush_flight, send_alert};
#else
static const SSL_QUIC_METHOD quic_method = {set_encryption_secrets, add_handshake_data, flush_flight, send_alert};
#endif

void
QUICTLS::_msg_cb(int write_p, int version, int content_type, const void *buf, size_t len, SSL *ssl, void *arg)
{
  // Debug for reading
  if (write_p == 0) {
    QUICTLS::_print_hs_message(content_type, buf, len);
  }
}

QUICTLS::QUICTLS(QUICPacketProtectionKeyInfo &pp_key_info, SSL_CTX *ssl_ctx, NetVConnectionContext_t nvc_ctx,
                 const NetVCOptions &netvc_options, const char *session_file)
  : QUICHandshakeProtocol(pp_key_info), _session_file(session_file), _ssl(SSL_new(ssl_ctx)), _netvc_context(nvc_ctx)
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
  SSL_set_quic_method(this->_ssl, &quic_method);
  SSL_set_early_data_enabled(this->_ssl, 1);

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

void
QUICTLS::set_local_transport_parameters(std::shared_ptr<const QUICTransportParameters> tp)
{
  this->_local_transport_parameters = tp;

  uint8_t buf[UINT16_MAX];
  uint16_t len;
  this->_local_transport_parameters->store(buf, &len);
  SSL_set_quic_transport_params(this->_ssl, buf, len);
}

int
QUICTLS::_process_post_handshake_messages(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in)
{
  this->_pass_quic_data_to_ssl_impl(*in);
  return SSL_process_quic_post_handshake(this->_ssl);
}

void
QUICTLS::_store_negotiated_cipher()
{
  ink_assert(this->_ssl);

  const EVP_CIPHER *cipher     = nullptr;
  size_t tag_len               = 0;
  const SSL_CIPHER *ssl_cipher = SSL_get_current_cipher(this->_ssl);

  if (ssl_cipher) {
    switch (SSL_CIPHER_get_id(ssl_cipher)) {
    case TLS1_CK_AES_128_GCM_SHA256:
      cipher  = EVP_aes_128_gcm();
      tag_len = EVP_GCM_TLS_TAG_LEN;
      break;
    case TLS1_CK_AES_256_GCM_SHA384:
      cipher  = EVP_aes_256_gcm();
      tag_len = EVP_GCM_TLS_TAG_LEN;
      break;
    case TLS1_CK_CHACHA20_POLY1305_SHA256:
      // cipher  = EVP_chacha20_poly1305();
      cipher  = nullptr;
      tag_len = 16;
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

  const EVP_CIPHER *cipher_for_hp = nullptr;
  const SSL_CIPHER *ssl_cipher    = SSL_get_current_cipher(this->_ssl);

  if (ssl_cipher) {
    switch (SSL_CIPHER_get_id(ssl_cipher)) {
    case TLS1_CK_AES_128_GCM_SHA256:
      cipher_for_hp = EVP_aes_128_ecb();
      break;
    case TLS1_CK_AES_256_GCM_SHA384:
      cipher_for_hp = EVP_aes_256_ecb();
      break;
    case TLS1_CK_CHACHA20_POLY1305_SHA256:
      // cipher_for_hp = EVP_chacha20();
      cipher_for_hp = nullptr;
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
  // This is for Hacked OpenSSL. Do nothing here.
  return 1;
}

int
QUICTLS::_write_early_data()
{
  // This is for Hacked OpenSSL. Do nothing here.
  return 1;
}

void
QUICTLS::_pass_quic_data_to_ssl_impl(const QUICHandshakeMsgs &in)
{
  for (auto level : QUIC_ENCRYPTION_LEVELS) {
    int index = static_cast<int>(level);
    ssl_encryption_level_t ossl_level;
    switch (level) {
    case QUICEncryptionLevel::INITIAL:
      ossl_level = ssl_encryption_initial;
      break;
    case QUICEncryptionLevel::ZERO_RTT:
      ossl_level = ssl_encryption_early_data;
      break;
    case QUICEncryptionLevel::HANDSHAKE:
      ossl_level = ssl_encryption_handshake;
      break;
    case QUICEncryptionLevel::ONE_RTT:
      ossl_level = ssl_encryption_application;
      break;
    default:
      // Should not be happened
      ossl_level = ssl_encryption_application;
      break;
    }
    if (in.offsets[index + 1] - in.offsets[index]) {
      int start = 0;
      for (int i = 0; i < index; ++i) {
        start += in.offsets[index];
      }
      SSL_provide_quic_data(this->_ssl, ossl_level, in.buf + start, in.offsets[index + 1] - in.offsets[index]);
    }
  }
}

const char *
QUICTLS::_get_handshake_digest() const
{
  switch (SSL_CIPHER_get_id(SSL_get_current_cipher(this->_ssl))) {
  case TLS1_CK_AES_128_GCM_SHA256:
  case TLS1_CK_CHACHA20_POLY1305_SHA256:
    return "SHA256";
  case TLS1_CK_AES_256_GCM_SHA384:
    return "SHA384";
  default:
    ink_assert(false);
    return nullptr;
  }
}
