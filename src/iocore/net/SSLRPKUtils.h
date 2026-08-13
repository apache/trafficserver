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

#pragma once

#include <openssl/evp.h>

#include <vector>

/**
   Shared helpers for RFC 7250 Raw Public Key (RPK) support.

   These are used identically regardless of whether ATS is built against
   OpenSSL or BoringSSL, so that RPK pinning behaves the same way no matter
   which TLS library a given build links. Only the code that calls into these
   helpers (extracting the peer's offered key from a `X509_STORE_CTX` on
   OpenSSL vs. a `SSL *` on BoringSSL) is library-specific.
 */
namespace SSLRPKUtils
{
/// One trusted peer key, DER-encoded as a SubjectPublicKeyInfo.
using TrustedKey    = std::vector<unsigned char>;
using TrustedKeySet = std::vector<TrustedKey>;

/** Load a PEM file containing one or more concatenated SubjectPublicKeyInfo
    blocks (bare public keys, not certificates) as a set of trusted/pinned
    peer keys. Supporting more than one key in the same file allows an
    operator to roll an "old" and "new" key during rotation.
    @return true if at least one key was loaded, false on I/O or parse failure.
*/
bool loadTrustedKeys(const char *path, TrustedKeySet &out);

/// Compare a peer-offered raw public key (DER-encoded SubjectPublicKeyInfo) against @a trusted.
bool pinnedKeyMatches(const unsigned char *peer_spki_der, int peer_spki_len, const TrustedKeySet &trusted);

/// Convenience wrapper: DER-encode @a pkey as a SubjectPublicKeyInfo and check it against @a trusted.
bool pinnedKeyMatches(EVP_PKEY *pkey, const TrustedKeySet &trusted);

} // namespace SSLRPKUtils
