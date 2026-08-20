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

#include "P_SSLConfig.h"

#include <openssl/ssl.h>
#include <string>
#include <string_view>

// BoringSSL does not have this include file
#if __has_include(<openssl/opensslconf.h>)
#include <openssl/opensslconf.h>
#endif

class NetVConnection;

// Create and initialize a SSL client context.
SSL_CTX *SSLInitClientContext(const struct SSLConfigParams *param);
SSL_CTX *SSLCreateClientContext(const struct SSLConfigParams *params, const char *ca_bundle_file, const char *ca_bundle_path,
                                const char *cert_path, const char *key_path);

int  verify_callback(int preverify_ok, X509_STORE_CTX *ctx);
bool validate_server_certificate_hostname(NetVConnection *netvc, std::string_view hostname);

#if TS_USE_RPK
/** Configure @a ssl to offer and/or pin RFC 7250 raw public keys for an outbound connection.

    @a trusted_key_file, when non-empty, is a PEM of the next hop's acceptable raw public keys;
    the loaded set is attached to @a ssl for the verify callback to pin against. @a offer_rpk
    advertises a raw public key (derived from the client certificate/key already configured on
    the context) as an alternative to X.509 for our own identity.

    Both are advertised alongside X.509 rather than replacing it, so a next hop that doesn't
    support RPK negotiates down to a normal certificate exchange.
 */
bool ssl_client_setup_rpk(SSL *ssl, bool offer_rpk, const std::string &trusted_key_file);
#endif
