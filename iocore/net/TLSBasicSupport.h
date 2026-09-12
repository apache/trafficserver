/** @file

  TLSBasicSupport implements common methods and members to
  support basic features on TLS connections

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

#include <optional>

#include <openssl/ssl.h>

#include "tscore/ink_hrtime.h"
#include "P_SSLUtils.h"

class TLSBasicSupport
{
public:
  virtual ~TLSBasicSupport() = default;

  static void initialize();
  static TLSBasicSupport *getInstance(SSL *ssl);
  static void bind(SSL *ssl, TLSBasicSupport *srs);
  static void unbind(SSL *ssl);

  const char *get_tls_protocol_name() const;
  const char *get_tls_cipher_suite() const;
  const char *get_tls_curve() const;
  ink_hrtime get_tls_handshake_begin_time() const;
  ink_hrtime get_tls_handshake_end_time() const;

  /// Length of a verify-scope digest, which is a SHA-256 hash. Defined in P_SSLUtils.h, which
  /// SSLConfigParams can reach without including this header.
  static constexpr size_t VERIFY_SCOPE_DIGEST_LENGTH = ::VERIFY_SCOPE_DIGEST_LENGTH;

  using VerifyScopeDigest = ::VerifyScopeDigest;

  /**
   * Records a digest of the CA store sni.yaml's verify_client_ca_certs selected for this connection.
   *
   * The digest is taken where the store is loaded, because the TLS libraries do not uniformly let a
   * store be read back off the SSL object, and because it must be folded into the session id context
   * before the TLS library creates or looks up a session.
   *
   * @param digest Digest of the selected store, or no value when the SSL_CTX's own store applies.
   */
  void set_verify_scope_digest(std::optional<VerifyScopeDigest> const &digest);

  /**
   * Records that a store was selected for this connection but could not be digested.
   *
   * Distinct from having no store at all: a failure must not silently resolve to some other scope,
   * so the handshake is failed rather than bound to a context that would permit resumption across
   * scopes.
   */
  void mark_verify_scope_digest_failed();

  /**
   * @return true if a store was selected but could not be loaded or digested.
   */
  bool verify_scope_digest_failed() const;

  /**
   * @return The digest recorded by set_verify_scope_digest(), if any.
   */
  std::optional<VerifyScopeDigest> const &get_verify_scope_digest() const;

protected:
  void clear();

  virtual SSL *_get_ssl_object() const        = 0;
  virtual ssl_curve_id _get_tls_curve() const = 0;

  void _record_tls_handshake_begin_time();
  void _record_tls_handshake_end_time();

private:
  static int _ex_data_index;

  std::optional<VerifyScopeDigest> _verify_scope_digest;
  bool _verify_scope_digest_failed = false;

  ink_hrtime _tls_handshake_begin_time = 0;
  ink_hrtime _tls_handshake_end_time   = 0;
};
