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

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <openssl/ssl.h>

#include "tscore/ink_hrtime.h"
#include "iocore/net/SSLTypes.h"

using TLSHandle = SSL *;

class TLSBasicSupport
{
public:
  virtual ~TLSBasicSupport() = default;

  static void             initialize();
  static TLSBasicSupport *getInstance(SSL *ssl);
  static void             bind(SSL *ssl, TLSBasicSupport *srs);
  static void             unbind(SSL *ssl);

  TLSHandle        get_tls_handle() const;
  const char      *get_tls_protocol_name() const;
  const char      *get_tls_cipher_suite() const;
  const char      *get_tls_curve() const;
  std::string_view get_tls_group() const;
  ink_hrtime       get_tls_handshake_begin_time() const;
  ink_hrtime       get_tls_handshake_end_time() const;
  bool             get_tls_handshake_bytes(uint64_t &bytes_in, uint64_t &bytes_out) const;
  bool             is_tls_handshake_complete() const;

  /**
   * Returns a certificate that need to be verified.
   *
   * Note: This function is only available when verify_certification is being called.
   * This function is probably just for TSVConnSslVerifyCTXGet.
   * We could (and probably should) pass a cert to verify as an argument of TS_EVENT_SSL_VERIFY_CLIENT/SERVER event.
   */
  X509_STORE_CTX *get_tls_cert_to_verify() const;

  /// Length of a verify-scope digest, which is a SHA-256 hash.
  static constexpr size_t VERIFY_SCOPE_DIGEST_LENGTH = 32;

  using VerifyScopeDigest = std::array<unsigned char, VERIFY_SCOPE_DIGEST_LENGTH>;

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

  void set_valid_tls_version_min(int min);
  void set_valid_tls_version_max(int max);
  void set_valid_tls_protocols(unsigned long proto_mask, unsigned long max_mask);
  void set_legacy_cipher_suite(std::string const &cipher_suite);
  void set_cipher_suite(std::string const &cipher_suite);
  bool set_groups_list(std::string const &groups_list);

  /**
   * Give the plugin access to the data structure passed in during the underlying
   * openssl callback so the plugin can make more detailed decisions about the
   * validity of the certificate in their cases.
   *
   * This function is supposed to be called from callback functions for TLS libraries.
   *
   * @return 1 if verification failed
   */
  int verify_certificate(X509_STORE_CTX *ctx);

protected:
  void clear();

  virtual SSL             *_get_ssl_object() const            = 0;
  virtual ssl_curve_id     _get_tls_curve() const             = 0;
  virtual std::string_view _get_tls_group() const             = 0;
  virtual bool             _is_tls_handshake_complete() const = 0;

  void _record_tls_handshake_begin_time();
  void _record_tls_handshake_end_time();
  void _update_end_of_handshake_stats();

  /**
   * Implementation should schedule either TS_EVENT_SSL_VERIFY_SERVER or TS_EVENT_SSL_VERIFY_CLIENT accordingly.
   *
   * @return 1 if verification failed
   */
  virtual int _verify_certificate(X509_STORE_CTX *ctx) = 0;

private:
  static int _ex_data_index;

  X509_STORE_CTX                  *_cert_to_verify = nullptr;
  std::optional<VerifyScopeDigest> _verify_scope_digest;
  bool                             _verify_scope_digest_failed = false;

  ink_hrtime       _tls_handshake_begin_time     = 0;
  ink_hrtime       _tls_handshake_end_time       = 0;
  mutable bool     _tls_handshake_bytes_measured = false;
  mutable uint64_t _tls_handshake_bytes_in       = 0;
  mutable uint64_t _tls_handshake_bytes_out      = 0;
};
