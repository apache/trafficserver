/** @file

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

// Borrowed from Envoy
// https://github.com/envoyproxy/envoy/blob/329b2491949fc52f4dc5c4a778ea158bfe6fe979/source/extensions/transport_sockets/tls/context_impl.cc#L962

#include "BoringSSLUtils.h"

#ifdef OPENSSL_IS_BORINGSSL
namespace BoringSSLUtils
{
bool
cbsContainsU16(CBS &cbs, uint16_t n)
{
  while (CBS_len(&cbs) > 0) {
    uint16_t v;
    if (!CBS_get_u16(&cbs, &v)) {
      return false;
    }
    if (v == n) {
      return true;
    }
  }

  return false;
}

bool
isCipherEnabled(SSL_CTX *ctx, uint16_t cipher_id, uint16_t client_version)
{
  const SSL_CIPHER *c = SSL_get_cipher_by_value(cipher_id);
  if (c == nullptr) {
    return false;
  }
  // Skip TLS 1.2 only ciphersuites unless the client supports it.
  if (SSL_CIPHER_get_min_version(c) > client_version) {
    return false;
  }
  if (SSL_CIPHER_get_auth_nid(c) != NID_auth_ecdsa) {
    return false;
  }
  for (const SSL_CIPHER *our_c : SSL_CTX_get_ciphers(ctx)) {
    if (SSL_CIPHER_get_id(our_c) == SSL_CIPHER_get_id(c)) {
      return true;
    }
  }
  return false;
}

bool
isClientEcdsaCapable(const SSL_CLIENT_HELLO *ssl_client_hello)
{
  CBS client_hello;
  CBS_init(&client_hello, ssl_client_hello->client_hello, ssl_client_hello->client_hello_len);

  // This is the TLSv1.3 case (TLSv1.2 on the wire and the supported_versions extensions present).
  // We just need to look at signature algorithms.
  const uint16_t client_version = ssl_client_hello->version;
  if (client_version == TLS1_2_VERSION && true) {
    // If the supported_versions extension is found then we assume that the client is competent
    // enough that just checking the signature_algorithms is sufficient.
    const uint8_t *supported_versions_data;
    size_t supported_versions_len;
    if (SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_supported_versions, &supported_versions_data,
                                             &supported_versions_len)) {
      const uint8_t *signature_algorithms_data;
      size_t signature_algorithms_len;
      if (SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_signature_algorithms, &signature_algorithms_data,
                                               &signature_algorithms_len)) {
        CBS signature_algorithms_ext, signature_algorithms;
        CBS_init(&signature_algorithms_ext, signature_algorithms_data, signature_algorithms_len);
        if (!CBS_get_u16_length_prefixed(&signature_algorithms_ext, &signature_algorithms) ||
            CBS_len(&signature_algorithms_ext) != 0) {
          return false;
        }
        if (cbsContainsU16(signature_algorithms, SSL_SIGN_ECDSA_SECP256R1_SHA256)) {
          return true;
        }
      }

      return false;
    }
  }

  // Otherwise we are < TLSv1.3 and need to look at both the curves in the supported_groups for
  // ECDSA and also for a compatible cipher suite. https://tools.ietf.org/html/rfc4492#section-5.1.1
  const uint8_t *curvelist_data;
  size_t curvelist_len;
  if (!SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_supported_groups, &curvelist_data, &curvelist_len)) {
    return false;
  }

  CBS curvelist;
  CBS_init(&curvelist, curvelist_data, curvelist_len);

  // We only support P256 ECDSA curves today.
  if (!cbsContainsU16(curvelist, SSL_CURVE_SECP256R1)) {
    return false;
  }

  // The client must have offered an ECDSA ciphersuite that we like.
  CBS cipher_suites;
  CBS_init(&cipher_suites, ssl_client_hello->cipher_suites, ssl_client_hello->cipher_suites_len);

  while (CBS_len(&cipher_suites) > 0) {
    uint16_t cipher_id;
    if (!CBS_get_u16(&cipher_suites, &cipher_id)) {
      return false;
    }

    SSL_CTX *ctx = SSL_get_SSL_CTX(ssl_client_hello->ssl);
    if (isCipherEnabled(ctx, cipher_id, client_version)) {
      return true;
    }
  }
  return false;
}

} // namespace BoringSSLUtils
#endif
