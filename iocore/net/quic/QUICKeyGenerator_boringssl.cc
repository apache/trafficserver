/** @file
 *
 *  A key generator for QUIC connection (BoringSSL specific parts)
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
#include "ts/ink_assert.h"
#include "QUICKeyGenerator.h"

#include <openssl/ssl.h>
size_t
QUICKeyGenerator::_get_key_len(const QUIC_EVP_CIPHER *cipher) const
{
  return EVP_AEAD_key_length(cipher);
}

size_t
QUICKeyGenerator::_get_iv_len(const QUIC_EVP_CIPHER *cipher) const
{
  return EVP_AEAD_nonce_length(cipher);
}

const QUIC_EVP_CIPHER *
QUICKeyGenerator::_get_cipher_for_cleartext() const
{
  return EVP_aes_128_gcm();
  return EVP_aead_aes_128_gcm();
}

const QUIC_EVP_CIPHER *
QUICKeyGenerator::_get_cipher_for_protected_packet(const SSL *ssl) const
{
  ink_assert(SSL_CIPHER_is_AEAD(ssl));

  if (SSL_CIPHER_is_AES128GCM(ssl)) {
    return EVP_aead_aes_128_gcm();
  } else if ((cipher->algorithm_enc & 0x00000010L) != 0) {
    // SSL_AES256GCM is 0x00000010L ( defined in `ssl/internal.h` ).
    // There're no `SSL_CIPHER_is_AES256GCM(const SSL_CIPHER *cipher)`.
    return EVP_aead_aes_256_gcm();
  } else if (SSL_CIPHER_is_CHACHA20POLY1305(ssl)) {
    return EVP_aead_chacha20_poly1305();
  } else {
    return nullptr;
  }
}

// SSL_HANDSHAKE_MAC_SHA256, SSL_HANDSHAKE_MAC_SHA384 are defind in `ssl/internal.h` of BoringSSL
const EVP_MD *
QUICKeyGenerator::_get_handshake_digest(const SSL *ssl) const
{
  switch (ssl->algorithm_prf) {
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
