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
#include "SSLRPKUtils.h"

#include <openssl/ssl.h>
#include <memory>
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

/** @return the key that selects an outbound connection's sni.yaml next-hop properties.

    That is the SNI the connection sends, or the text of @a remote_addr when it sends none, which
    includes the case of an IP literal since one is never sent as an SNI. The handshake and the
    origin session reuse checks both select through here, so they cannot disagree about the entry.

    @param sni The outbound SNI a connection would be given; may be empty or an IP literal.
    @param remote_addr The next hop's address.
    @param buffer Storage for the address text, which the returned key may refer to.
 */
std::string_view next_hop_property_key(std::string_view sni, sockaddr const *remote_addr, char (&buffer)[INET6_ADDRSTRLEN]);

/** @return true if this origin connection authenticated with an RFC 7250 raw public key.

    Such a connection was vouched for by the pinned key set attached to it rather than by a
    certificate, so callers gating origin session reuse must check that set with
    origin_rpk_pin_matches() instead of a name. Always false when server verification is disabled
    for the connection, since nothing was pinned.
 */
bool origin_pinned_raw_public_key(NetVConnection *netvc);

/** @return true if the key set that authenticated @a netvc is the set sni.yaml now pins for the
    next-hop entry @a property_key selects.

    A raw public key session can only carry a request whose own connection would have pinned the
    same set; a name says nothing about which set that is. False when the entry pins no set.

    @param netvc A pooled origin connection for which origin_pinned_raw_public_key() is true.
    @param property_key The new request's key, from next_hop_property_key().
 */
bool origin_rpk_pin_matches(NetVConnection *netvc, std::string_view property_key);

#if TS_USE_RPK
/** Configure @a ssl to offer and/or pin RFC 7250 raw public keys for an outbound connection.

    @a trusted_key_set, when non-null, is the next hop's acceptable raw public keys (parsed once at
    config load, not re-parsed per connection); it is attached to @a ssl for the verify callback to
    pin against. @a offer_rpk advertises a raw public key (derived from the client certificate/key
    already configured on the context) as an alternative to X.509 for our own identity.

    Both are advertised alongside X.509 rather than replacing it, so a next hop that doesn't
    support RPK negotiates down to a normal certificate exchange.

    @a trusted_key_digest identifies the pinned set for the origin session cache key, so that a
    session authenticated by one set is not resumed after the configuration named a different one.
 */
bool ssl_client_setup_rpk(SSL *ssl, bool offer_rpk, std::shared_ptr<const SSLRPKUtils::TrustedKeySet> trusted_key_set,
                          std::string_view trusted_key_digest);

/** @return the identity of the pinned next-hop key set attached to @a ssl, empty if there is none.
    Part of the origin session cache key, so a cached session cannot outlive the pin set that
    authorized it. See ssl_client_setup_rpk().
 */
std::string_view origin_rpk_pin_identity(const SSL *ssl);
#endif
