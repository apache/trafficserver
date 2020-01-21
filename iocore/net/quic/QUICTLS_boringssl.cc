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

static constexpr char tag[] = "quic_tls";

static int
set_encryption_secrets(SSL *ssl, enum ssl_encryption_level_t level, const uint8_t *read_secret, const uint8_t *write_secret,
                       size_t secret_len)
{
  // QUICTLS *qtls = static_cast<QUICTLS *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_tls_index));
  Debug(tag, "%d, read_secret=%p, write_secret=%p secret_len=%zu", level, read_secret, write_secret, secret_len);
  return 1;
}

static int
add_handshake_data(SSL *ssl, enum ssl_encryption_level_t level, const uint8_t *data, size_t len)
{
  QUICEncryptionLevel ats_level;
  switch (level) {
  case ssl_encryption_initial:
    ats_level = QUICEncryptionLevel::INITIAL;
    break;
  case ssl_encryption_early_data:
    ats_level = QUICEncryptionLevel::ZERO_RTT;
    break;
  case ssl_encryption_handshake:
    ats_level = QUICEncryptionLevel::HANDSHAKE;
    break;
  case ssl_encryption_application:
    ats_level = QUICEncryptionLevel::ONE_RTT;
    break;
  }

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
  return 1;
}

static const SSL_QUIC_METHOD quic_method = {set_encryption_secrets, add_handshake_data, flush_flight, send_alert};

void
QUICTLS::_msg_cb(int write_p, int version, int content_type, const void *buf, size_t len, SSL *ssl, void *arg)
{
  // Debug for reading
  if (write_p == 0) {
    QUICTLS::_print_hs_message(content_type, buf, len);
    return;
  }

  return;
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
  SSL_set_quic_method(this->_ssl, &quic_method);

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

int
QUICTLS::_process_post_handshake_messages(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in)
{
  return SSL_process_quic_post_handshake(this->_ssl);
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
      // Should not be happend
      ossl_level = ssl_encryption_application;
      break;
    }
    if (in.offsets[index]) {
      int start = 0;
      for (int i = 0; i < index; --i) {
        start += in.offsets[index];
      }
      SSL_provide_quic_data(this->_ssl, ossl_level, in.buf + start, in.offsets[index]);
    }
  }
}
