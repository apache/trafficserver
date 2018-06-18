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

static constexpr char tag[] = "quic_tls";

const EVP_CIPHER *
QUICTLS::_get_evp_aead(QUICKeyPhase phase) const
{
  if (phase == QUICKeyPhase::CLEARTEXT) {
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

const EVP_CIPHER *
QUICTLS::_get_evp_aead_for_pne(QUICKeyPhase phase) const
{
  if (phase == QUICKeyPhase::CLEARTEXT) {
    return EVP_aes_128_ctr();
  } else {
    const SSL_CIPHER *cipher = SSL_get_current_cipher(this->_ssl);
    if (cipher) {
      switch (SSL_CIPHER_get_id(cipher)) {
      case TLS1_3_CK_AES_128_GCM_SHA256:
        return EVP_aes_128_ctr();
      case TLS1_3_CK_AES_256_GCM_SHA384:
        return EVP_aes_256_ctr();
      case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
        return EVP_chacha20();
      case TLS1_3_CK_AES_128_CCM_SHA256:
      case TLS1_3_CK_AES_128_CCM_8_SHA256:
        return EVP_aes_128_ctr();
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
  if (phase == QUICKeyPhase::CLEARTEXT) {
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
    Debug(tag, "Failed to decrypt");
    return false;
  }
}

bool
QUICTLS::_encrypt_pn(uint8_t *protected_pn, size_t &protected_pn_len, const uint8_t *unprotected_pn, size_t unprotected_pn_len,
                     const uint8_t *sample, const KeyMaterial &km, const EVP_CIPHER *aead) const
{
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int len             = 0;

  if (!ctx || !EVP_EncryptInit_ex(ctx, aead, nullptr, km.pn, sample)) {
    return false;
  }
  if (!EVP_EncryptUpdate(ctx, protected_pn, &len, unprotected_pn, unprotected_pn_len)) {
    return false;
  }
  protected_pn_len = len;
  if (!EVP_EncryptFinal_ex(ctx, protected_pn + len, &len)) {
    return false;
  }
  protected_pn_len += len;
  EVP_CIPHER_CTX_free(ctx);

  return true;
}

bool
QUICTLS::_decrypt_pn(uint8_t *unprotected_pn, size_t &unprotected_pn_len, const uint8_t *protected_pn, size_t protected_pn_len,
                     const uint8_t *sample, const KeyMaterial &km, const EVP_CIPHER *aead) const
{
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int len             = 0;

  if (!ctx || !EVP_DecryptInit_ex(ctx, aead, nullptr, km.pn, sample)) {
    return false;
  }
  if (!EVP_DecryptUpdate(ctx, unprotected_pn, &len, protected_pn, protected_pn_len)) {
    return false;
  }
  unprotected_pn_len = len;
  if (!EVP_DecryptFinal_ex(ctx, unprotected_pn, &len)) {
    return false;
  }
  unprotected_pn_len += len;
  EVP_CIPHER_CTX_free(ctx);

  return true;
}
