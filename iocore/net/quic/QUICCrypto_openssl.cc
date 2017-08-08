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
#include "QUICCrypto.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/kdf.h>
#include <openssl/evp.h>

const static char tag[] = "quic_crypto";

const EVP_CIPHER *
QUICCrypto::_get_evp_aead(const SSL_CIPHER *cipher) const
{
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
}

const EVP_MD *
QUICCrypto::_get_handshake_digest(const SSL_CIPHER *cipher) const
{
  switch (SSL_CIPHER_get_id(cipher)) {
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

size_t
QUICCrypto::_get_aead_tag_len(const SSL_CIPHER *cipher) const
{
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
}

size_t
QUICCrypto::_get_aead_key_len(const EVP_CIPHER *aead) const
{
  return EVP_CIPHER_key_length(aead);
}

size_t
QUICCrypto::_get_aead_nonce_len(const EVP_CIPHER *aead) const
{
  return EVP_CIPHER_iv_length(aead);
}

int
QUICCrypto::_hkdf_expand_label(uint8_t *dst, size_t dst_len, const uint8_t *secret, size_t secret_len, const char *label,
                               size_t label_len, const EVP_MD *digest) const
{
  uint8_t info[256] = {0};
  size_t info_len   = 0;
  _gen_info(info, info_len, label, label_len, dst_len);

  EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
  if (!EVP_PKEY_derive_init(pctx)) {
    return -1;
  }
  if (!EVP_PKEY_CTX_hkdf_mode(pctx, EVP_PKEY_HKDEF_MODE_EXPAND_ONLY)) {
    return -1;
  }
  if (!EVP_PKEY_CTX_set_hkdf_md(pctx, digest)) {
    return -1;
  }
  if (!EVP_PKEY_CTX_set1_hkdf_salt(pctx, "", 0)) {
    return -1;
  }
  if (!EVP_PKEY_CTX_set1_hkdf_key(pctx, secret, secret_len)) {
    return -1;
  }
  if (!EVP_PKEY_CTX_add1_hkdf_info(pctx, info, info_len)) {
    return -1;
  }
  if (!EVP_PKEY_derive(pctx, dst, &dst_len)) {
    return -1;
  }

  return 1;
}

bool
QUICCrypto::_encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len,
                     uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const uint8_t *key, size_t key_len, const uint8_t *iv,
                     size_t iv_len, size_t tag_len) const
{
  uint8_t nonce[EVP_MAX_IV_LENGTH] = {0};
  size_t nonce_len                 = 0;
  _gen_nonce(nonce, nonce_len, pkt_num, iv, iv_len);

  EVP_CIPHER_CTX *aead_ctx;
  int len;

  if (!(aead_ctx = EVP_CIPHER_CTX_new())) {
    return false;
  }
  if (!EVP_EncryptInit_ex(aead_ctx, this->_aead, nullptr, nullptr, nullptr)) {
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_IVLEN, nonce_len, nullptr)) {
    return false;
  }
  if (!EVP_EncryptInit_ex(aead_ctx, nullptr, nullptr, key, nonce)) {
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
QUICCrypto::_decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len,
                     uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const uint8_t *key, size_t key_len, const uint8_t *iv,
                     size_t iv_len, size_t tag_len) const
{
  uint8_t nonce[EVP_MAX_IV_LENGTH] = {0};
  size_t nonce_len                 = 0;
  _gen_nonce(nonce, nonce_len, pkt_num, iv, iv_len);

  EVP_CIPHER_CTX *aead_ctx;
  int len;

  if (!(aead_ctx = EVP_CIPHER_CTX_new())) {
    return false;
  }
  if (!EVP_DecryptInit_ex(aead_ctx, this->_aead, nullptr, nullptr, nullptr)) {
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_IVLEN, nonce_len, nullptr)) {
    return false;
  }
  if (!EVP_DecryptInit_ex(aead_ctx, nullptr, nullptr, key, nonce)) {
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
