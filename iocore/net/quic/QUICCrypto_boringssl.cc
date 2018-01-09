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
#include "QUICCryptoTls.h"

#include <openssl/base.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/hkdf.h>
#include <openssl/aead.h>

static constexpr char tag[] = "quic_crypto";

const EVP_AEAD *
QUICCryptoTls::_get_evp_aead(const SSL_CIPHER *cipher) const
{
  ink_assert(SSL_CIPHER_is_AEAD(cipher));

  if (SSL_CIPHER_is_AES128GCM(cipher)) {
    return EVP_aead_aes_128_gcm();
  } else if ((cipher->algorithm_enc & 0x00000010L) != 0) {
    // SSL_AES256GCM is 0x00000010L ( defined in `ssl/internal.h` ).
    // There're no `SSL_CIPHER_is_AES256GCM(const SSL_CIPHER *cipher)`.
    return EVP_aead_aes_256_gcm();
  } else if (SSL_CIPHER_is_CHACHA20POLY1305(cipher)) {
    return EVP_aead_chacha20_poly1305();
  } else {
    return nullptr;
  }
}

// SSL_HANDSHAKE_MAC_SHA256, SSL_HANDSHAKE_MAC_SHA384 are defind in `ssl/internal.h` of BoringSSL
const EVP_MD *
QUICCryptoTls::_get_handshake_digest(const SSL_CIPHER *cipher) const
{
  switch (cipher->algorithm_prf) {
  case 0x2:
    // SSL_HANDSHAKE_MAC_SHA256:
    return EVP_sha256();
  case 0x4:
    // SSL_HANDSHAKE_MAC_SHA384:
    return EVP_sha384();
  default:
    return nullptr;
  }
}

size_t
QUICCryptoTls::_get_aead_tag_len(const SSL_CIPHER * /* cipher */) const
{
  return EVP_AEAD_DEFAULT_TAG_LENGTH;
}

int
QUICCryptoTls::_hkdf_expand_label(uint8_t *dst, size_t dst_len, const uint8_t *secret, size_t secret_len, const char *label,
                                  size_t label_len, const EVP_MD *digest) const
{
  uint8_t info[256] = {0};
  size_t info_len   = 0;
  _gen_info(info, info_len, label, label_len, dst_len);
  return HKDF(dst, dst_len, digest, secret, secret_len, nullptr, 0, info, info_len);
}

bool
QUICCryptoTls::_encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len,
                        uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const uint8_t *key, size_t key_len, const uint8_t *iv,
                        size_t iv_len, size_t tag_len) const
{
  uint8_t nonce[EVP_MAX_IV_LENGTH] = {0};
  size_t nonce_len                 = 0;
  _gen_nonce(nonce, nonce_len, pkt_num, iv, iv_len);

  EVP_AEAD_CTX *aead_ctx = EVP_AEAD_CTX_new(this->_aead, key, key_len, tag_len);
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
QUICCryptoTls::_decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len,
                        uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const uint8_t *key, size_t key_len, const uint8_t *iv,
                        size_t iv_len, size_t tag_len) const
{
  uint8_t nonce[EVP_MAX_IV_LENGTH] = {0};
  size_t nonce_len                 = 0;
  _gen_nonce(nonce, nonce_len, pkt_num, iv, iv_len);

  EVP_AEAD_CTX *aead_ctx = EVP_AEAD_CTX_new(this->_aead, key, key_len, tag_len);
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
