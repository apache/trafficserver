/** @file
 *
 *  QUIC TLS
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

#pragma once

#include <openssl/ssl.h>

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/digest.h>
#include <openssl/cipher.h>
#else
#include <openssl/evp.h>
#endif

#include "I_EventSystem.h"
#include "I_NetVConnection.h"
#include "QUICTypes.h"

struct KeyMaterial {
  KeyMaterial(size_t secret_len, size_t key_len, size_t iv_len) : secret_len(secret_len), key_len(key_len), iv_len(iv_len) {}

  uint8_t secret[EVP_MAX_MD_SIZE] = {0};
  uint8_t key[EVP_MAX_KEY_LENGTH] = {0};
  uint8_t iv[EVP_MAX_IV_LENGTH]   = {0};
  size_t secret_len               = 0;
  size_t key_len                  = 0;
  size_t iv_len                   = 0;
};

class QUICPacketProtection
{
public:
  QUICPacketProtection(){};
  ~QUICPacketProtection();
  void set_key(KeyMaterial *km, QUICKeyPhase phase);
  const KeyMaterial *get_key(QUICKeyPhase phase) const;
  QUICKeyPhase key_phase() const;

private:
  KeyMaterial *_phase_0_key = nullptr;
  KeyMaterial *_phase_1_key = nullptr;
  QUICKeyPhase _key_phase   = QUICKeyPhase::PHASE_UNINITIALIZED;
};

class QUICCrypto
{
public:
  QUICCrypto(SSL_CTX *, NetVConnection *);
  ~QUICCrypto();

  bool handshake(uint8_t *out, size_t &out_len, size_t max_out_len, const uint8_t *in, size_t in_len);
  bool is_handshake_finished() const;
  int setup_session();
  bool encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len, uint64_t pkt_num,
               const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const;
  bool decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len, uint64_t pkt_num,
               const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const;
  int update_client_keymaterial();
  int update_server_keymaterial();

  // FIXME SSL handle should not be exported
  SSL *ssl_handle();

private:
  int _export_secret(uint8_t *dst, size_t dst_len, const char *label, size_t label_len) const;
  int _export_client_keymaterial(size_t secret_len, size_t key_len, size_t iv_len);
  int _export_server_keymaterial(size_t secret_len, size_t key_len, size_t iv_len);
  void _gen_nonce(uint8_t *nonce, size_t &nonce_len, uint64_t pkt_num, const uint8_t *iv, size_t iv_len) const;
  bool _gen_info(uint8_t *info, size_t &info_len, const char *label, size_t label_len, size_t length) const;
  int _hkdf_expand_label(uint8_t *dst, size_t dst_len, const uint8_t *secret, size_t secret_len, const char *label,
                         size_t label_len, const EVP_MD *digest) const;
#ifdef OPENSSL_IS_BORINGSSL
  const EVP_AEAD *_get_evp_aead(const SSL_CIPHER *cipher) const;
  size_t _get_aead_key_len(const EVP_AEAD *aead) const;
  size_t _get_aead_nonce_len(const EVP_AEAD *aead) const;
#else
  const EVP_CIPHER *_get_evp_aead(const SSL_CIPHER *cipher) const;
  size_t _get_aead_key_len(const EVP_CIPHER *aead) const;
  size_t _get_aead_nonce_len(const EVP_CIPHER *aead) const;
#endif // OPENSSL_IS_BORINGSSL
  const EVP_MD *_get_handshake_digest(const SSL_CIPHER *cipher) const;
  size_t _get_aead_tag_len(const SSL_CIPHER *cipher) const;

  bool _encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len,
                uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const uint8_t *key, size_t key_len, const uint8_t *iv,
                size_t iv_len, size_t tag_len) const;
  bool _decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len, uint64_t pkt_num,
                const uint8_t *ad, size_t ad_len, const uint8_t *key, size_t key_len, const uint8_t *iv, size_t iv_len,
                size_t tag_len) const;

  SSL *_ssl = nullptr;
#ifdef OPENSSL_IS_BORINGSSL
  const EVP_AEAD *_aead = nullptr;
#else
  const EVP_CIPHER *_aead = nullptr;
#endif // OPENSSL_IS_BORINGSSL
  const EVP_MD *_digest                  = nullptr;
  QUICPacketProtection *_client_pp       = nullptr;
  QUICPacketProtection *_server_pp       = nullptr;
  NetVConnectionContext_t _netvc_context = NET_VCONNECTION_UNSET;
};
