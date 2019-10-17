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

// static constexpr char tag[] = "quic_tls";

QUICTLS::QUICTLS(QUICPacketProtectionKeyInfo &pp_key_info, SSL_CTX *ssl_ctx, NetVConnectionContext_t nvc_ctx,
                 const NetVCOptions &netvc_options, const char *session_file, const char *keylog_file)
  : QUICHandshakeProtocol(pp_key_info),
    _session_file(session_file),
    _keylog_file(keylog_file),
    _ssl(SSL_new(ssl_ctx)),
    _netvc_context(nvc_ctx)
{
  ink_assert(this->_netvc_context != NET_VCONNECTION_UNSET);
}

int
QUICTLS::handshake(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in)
{
  ink_assert(false);
  return 0;
}

int
QUICTLS::_process_post_handshake_messages(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in)
{
  ink_assert(false);
  return 0;
}

int
QUICTLS::_read_early_data()
{
  uint8_t early_data[8];
  size_t early_data_len = 0;
  do {
    ERR_clear_error();
    early_data_len = SSL_read(this->_ssl, early_data, sizeof(early_data));
  } while (SSL_in_early_data(this->_ssl));

  return 1;
}

/*
const EVP_AEAD *
QUICTLS::_get_evp_aead(QUICKeyPhase phase) const
{
  if (phase == QUICKeyPhase::INITIAL) {
    return EVP_aead_aes_128_gcm();
  } else {
    const SSL_CIPHER *cipher = SSL_get_current_cipher(this->_ssl);
    if (cipher) {
      switch (SSL_CIPHER_get_id(cipher)) {
      case TLS1_CK_AES_128_GCM_SHA256:
        return EVP_aead_aes_128_gcm();
      case TLS1_CK_AES_256_GCM_SHA384:
        return EVP_aead_aes_256_gcm();
      case TLS1_CK_CHACHA20_POLY1305_SHA256:
        return EVP_aead_chacha20_poly1305();
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
      case TLS1_CK_AES_128_GCM_SHA256:
      case TLS1_CK_AES_256_GCM_SHA384:
        return EVP_GCM_TLS_TAG_LEN;
      case TLS1_CK_CHACHA20_POLY1305_SHA256:
        return 16;
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
QUICKeyGenerator::_get_handshake_digest()
{
  // TODO not implemented
  return nullptr;
}

bool
QUICTLS::_encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len,
                  uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const KeyMaterial &km, const EVP_AEAD *aead,
                  size_t tag_len) const
{
  uint8_t nonce[EVP_MAX_IV_LENGTH] = {0};
  size_t nonce_len                 = 0;
  _gen_nonce(nonce, nonce_len, pkt_num, km.iv, km.iv_len);

  EVP_AEAD_CTX *aead_ctx = EVP_AEAD_CTX_new(aead, km.key, km.key_len, tag_len);
  if (!aead_ctx) {
    Debug(tag, "Failed to create EVP_AEAD_CTX");
    return false;
  }

  if (!EVP_AEAD_CTX_seal(aead_ctx, cipher, &cipher_len, max_cipher_len, nonce, nonce_len, plain, plain_len, ad, ad_len)) {
    Debug(tag, "Failed to encrypt");
    return false;
  }

  EVP_AEAD_CTX_free(aead_ctx);

  return true;
}

bool
QUICTLS::_decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len,
                  uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const KeyMaterial &km, const EVP_AEAD *aead,
                  size_t tag_len) const
{
  uint8_t nonce[EVP_MAX_IV_LENGTH] = {0};
  size_t nonce_len                 = 0;
  _gen_nonce(nonce, nonce_len, pkt_num, km.iv, km.iv_len);

  EVP_AEAD_CTX *aead_ctx = EVP_AEAD_CTX_new(aead, km.key, km.key_len, tag_len);
  if (!aead_ctx) {
    Debug(tag, "Failed to create EVP_AEAD_CTX");
    return false;
  }

  if (!EVP_AEAD_CTX_open(aead_ctx, plain, &plain_len, max_plain_len, nonce, nonce_len, cipher, cipher_len, ad, ad_len)) {
    Debug(tag, "Failed to decrypt");
    return false;
  }

  EVP_AEAD_CTX_free(aead_ctx);

  return true;
}
*/
