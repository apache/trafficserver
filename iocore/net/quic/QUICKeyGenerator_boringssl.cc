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
#include "tscore/ink_assert.h"
#include "QUICKeyGenerator.h"

#include <openssl/ssl.h>
size_t
QUICKeyGenerator::_get_key_len(const EVP_CIPHER *cipher) const
{
  return EVP_CIPHER_key_length(cipher);
}

size_t
QUICKeyGenerator::_get_iv_len(const EVP_CIPHER *cipher) const
{
  return EVP_CIPHER_iv_length(cipher);
}

const EVP_CIPHER *
QUICKeyGenerator::_get_cipher_for_initial() const
{
  return EVP_aes_128_gcm();
}

const EVP_CIPHER *
QUICKeyGenerator::_get_cipher_for_protected_packet(const SSL *ssl) const
{
  switch (SSL_CIPHER_get_id(SSL_get_current_cipher(ssl))) {
  case TLS1_CK_AES_128_GCM_SHA256:
    return EVP_aes_128_gcm();
  case TLS1_CK_AES_256_GCM_SHA384:
    return EVP_aes_256_gcm();
  case TLS1_CK_CHACHA20_POLY1305_SHA256:
    return nullptr;
    // return EVP_aead_chacha20_poly1305();
  default:
    ink_assert(false);
    return nullptr;
  }
}

// SSL_HANDSHAKE_MAC_SHA256, SSL_HANDSHAKE_MAC_SHA384 are defined in `ssl/internal.h` of BoringSSL
/*
const EVP_MD *
QUICKeyGenerator::get_handshake_digest(const SSL *ssl)
{
  switch (SSL_CIPHER_get_id(SSL_get_current_cipher(ssl))) {
  case TLS1_CK_AES_128_GCM_SHA256:
  case TLS1_CK_CHACHA20_POLY1305_SHA256:
    return EVP_sha256();
  case TLS1_CK_AES_256_GCM_SHA384:
    return EVP_sha384();
  default:
    ink_assert(false);
    return nullptr;
  }
}
*/
