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

  /**
   * Records the CA store being used to verify this connection's client certificate.
   *
   * sni.yaml's verify_client_ca_certs installs a store on the connection, and the TLS libraries do
   * not uniformly let that store be read back, so it is remembered here as it is applied.
   *
   * @param store The store selected for the current SNI, or nullptr when the SSL_CTX's store applies.
   */
  void set_verify_cert_store(X509_STORE *store);

  /**
   * Re-verifies the peer certificate against the CA store in effect for this connection.
   *
   * A resumed session carries forward the verification result cached by whichever handshake created
   * it, evaluated against the CA store that applied to that connection's SNI. Re-running the chain
   * verification is the only portable way to tell whether the peer certificate is trusted by the
   * SNI in use now.
   *
   * @return true if the peer presented a certificate that chains to a CA this connection trusts.
   */
  bool is_peer_cert_trusted_by_current_ca() const;

protected:
  void clear();

  virtual SSL *_get_ssl_object() const        = 0;
  virtual ssl_curve_id _get_tls_curve() const = 0;

  void _record_tls_handshake_begin_time();
  void _record_tls_handshake_end_time();

private:
  static int _ex_data_index;

  X509_STORE *_verify_cert_store = nullptr;

  ink_hrtime _tls_handshake_begin_time = 0;
  ink_hrtime _tls_handshake_end_time   = 0;
};
