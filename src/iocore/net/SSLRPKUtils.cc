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

#include "SSLRPKUtils.h"

#include "P_SSLUtils.h"
#include "iocore/net/SSLDiags.h"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <cstring>

namespace SSLRPKUtils
{
bool
loadTrustedKeys(const char *path, TrustedKeySet &out)
{
  scoped_BIO bio(BIO_new_file(path, "r"));
  if (!bio) {
    SSLError("SSLRPKUtils: failed to open trusted RPK key file %s", path);
    return false;
  }

  // Discard anything already on this thread's error queue so the PEM_R_NO_START_LINE check
  // below on the first iteration can't be confused by an unrelated error left over from earlier,
  // unrelated OpenSSL/BoringSSL calls on this thread.
  ERR_clear_error();

  for (;;) {
    // Read the PEM envelope first and decode it separately. Asking PEM_read_bio_PUBKEY() to do
    // both makes end-of-file indistinguishable from a decode failure: on OpenSSL 3 it runs the
    // decoder framework, so the error left on the queue at EOF is the decoder's "unsupported"
    // rather than PEM_R_NO_START_LINE, and a "no more keys" stop looks exactly like a malformed
    // key. PEM_read_bio() reports EOF unambiguously via PEM_R_NO_START_LINE.
    char          *name   = nullptr;
    char          *header = nullptr;
    unsigned char *data   = nullptr;
    long           len    = 0;

    if (PEM_read_bio(bio.get(), &name, &header, &data, &len) != 1) {
      unsigned long err    = ERR_peek_last_error();
      bool const    at_eof = ERR_GET_REASON(err) == PEM_R_NO_START_LINE;
      ERR_clear_error();
      if (at_eof && !out.empty()) {
        break;
      }
      SSLError("SSLRPKUtils: failed to read a PEM block from %s", path);
      return false;
    }

    bool const is_pubkey = name != nullptr && strcmp(name, PEM_STRING_PUBLIC) == 0;
    if (!is_pubkey) {
      SSLError("SSLRPKUtils: %s contains a '%s' block; only bare public keys are supported", path,
               name != nullptr ? name : "(unnamed)");
    }

    EVP_PKEY *pkey = nullptr;
    if (is_pubkey) {
      const unsigned char *p = data;
      // Decode the PEM payload so a corrupt key is rejected at config load rather than silently
      // pinned as opaque bytes -- and so the pin is the canonical re-encoding of the key rather
      // than the payload as received: d2i_PUBKEY() doesn't reject trailing bytes after a valid
      // DER structure, so a non-canonically-encoded payload would otherwise get pinned including
      // whatever garbage follows the key, and could never match a peer's cleanly-encoded SPKI.
      pkey = d2i_PUBKEY(nullptr, &p, len);
      if (pkey == nullptr) {
        SSLError("SSLRPKUtils: failed to parse a raw public key from %s", path);
      }
    }

    OPENSSL_free(name);
    OPENSSL_free(header);

    if (pkey == nullptr) {
      OPENSSL_free(data);
      return false;
    }
    OPENSSL_free(data);

    int canonical_len = i2d_PUBKEY(pkey, nullptr);
    if (canonical_len <= 0) {
      SSLError("SSLRPKUtils: failed to re-encode a raw public key from %s", path);
      EVP_PKEY_free(pkey);
      return false;
    }
    TrustedKey     canonical(canonical_len);
    unsigned char *cp = canonical.data();
    i2d_PUBKEY(pkey, &cp);
    EVP_PKEY_free(pkey);

    out.push_back(std::move(canonical));
  }

  return true;
}

bool
pinnedKeyMatches(const unsigned char *peer_spki_der, int peer_spki_len, const TrustedKeySet &trusted)
{
  if (peer_spki_der == nullptr || peer_spki_len <= 0) {
    return false;
  }

  for (auto const &key : trusted) {
    if (key.size() == static_cast<size_t>(peer_spki_len) && memcmp(key.data(), peer_spki_der, key.size()) == 0) {
      return true;
    }
  }

  return false;
}

bool
pinnedKeyMatches(EVP_PKEY *pkey, const TrustedKeySet &trusted)
{
  if (pkey == nullptr) {
    return false;
  }

  unsigned char *der     = nullptr;
  int            der_len = i2d_PUBKEY(pkey, &der);
  if (der_len <= 0) {
    return false;
  }

  bool matched = pinnedKeyMatches(der, der_len, trusted);
  OPENSSL_free(der);
  return matched;
}

} // namespace SSLRPKUtils
