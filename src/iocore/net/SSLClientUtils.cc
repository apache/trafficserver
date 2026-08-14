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

#include "P_SSLClientUtils.h"
#include "P_SSLConfig.h"
#include "P_SSLNetVConnection.h"
#include "P_TLSKeyLogger.h"
#include "SSLRPKUtils.h"
#include "SSLSessionCache.h"
#include "TLSCertCompression.h"
#include "iocore/net/TLSBasicSupport.h"
#include "iocore/net/YamlSNIConfig.h"
#include "iocore/net/SSLDiags.h"
#include "tscore/ink_config.h"
#include "tscore/SimpleTokenizer.h"
#include "tscore/Filenames.h"
#include "tscore/X509HostnameValidator.h"

#include <openssl/err.h>
#include <openssl/pem.h>

#include <mutex>

SSLOriginSessionCache *origin_sess_cache;

namespace
{
DbgCtl dbg_ctl_ssl_verify{"ssl_verify"};
DbgCtl dbg_ctl_ssl_origin_session_cache{"ssl.origin_session_cache"};

#if TS_USE_RPK
// SSL-level (not SSL_CTX-level, unlike the inbound side) ex_data index holding the trusted
// next-hop raw public keys for this connection. Outbound cert selection is already per-connection
// here, and SSLConfigParams::getCTX() caches contexts by (cert, key, CA) -- attaching pins to the
// shared context would leak one next hop's pin set onto every other hop sharing that cache entry.
int ssl_server_rpk_index = -1;

void
ssl_server_rpk_ex_free(void * /*parent*/, void *ptr, CRYPTO_EX_DATA * /*ad*/, int /*idx*/, long /*argl*/, void * /*argp*/)
{
  delete static_cast<SSLRPKUtils::TrustedKeySet *>(ptr);
}

const SSLRPKUtils::TrustedKeySet *
ssl_get_trusted_rpk(const SSL *ssl)
{
  if (ssl_server_rpk_index < 0) {
    return nullptr;
  }
  return static_cast<const SSLRPKUtils::TrustedKeySet *>(SSL_get_ex_data(ssl, ssl_server_rpk_index));
}
#endif

} // end anonymous namespace

int
verify_callback(int signature_ok, X509_STORE_CTX *ctx)
{
  X509 *cert;
  int   depth;
  int   err;
  SSL  *ssl;

  Dbg(dbg_ctl_ssl_verify, "Entered cert verify callback");

  /*
   * Retrieve the pointer to the SSL of the connection currently treated
   * and the application specific data stored into the SSL object.
   */
  ssl                      = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);

  // No enforcing, go away
  if (netvc == nullptr) {
    // No netvc, very bad.  Go away.  Things are not good.
    Dbg(dbg_ctl_ssl_verify, "WARNING, NetVC is NULL in cert verify callback");
    return false;
  } else if (netvc->options.verifyServerPolicy == YamlSNIConfig::Policy::DISABLED) {
    return true; // Tell them that all is well
  }

  depth = X509_STORE_CTX_get_error_depth(ctx);
  cert  = X509_STORE_CTX_get_current_cert(ctx);
  err   = X509_STORE_CTX_get_error(ctx);

  bool enforce_mode = (netvc->options.verifyServerPolicy == YamlSNIConfig::Policy::ENFORCED);
  bool check_sig =
    static_cast<uint8_t>(netvc->options.verifyServerProperties) & static_cast<uint8_t>(YamlSNIConfig::Property::SIGNATURE_MASK);

#if HAVE_SSL_CTX_SET1_SERVER_CERT_TYPE
  if (EVP_PKEY *peer_rpk = X509_STORE_CTX_get0_rpk(ctx); peer_rpk != nullptr) {
    // The next hop authenticated with a raw public key. There is no chain to walk and no SAN to
    // match, so the configured pin set replaces both the signature and name checks below; the
    // SIGNATURE_MASK/NAME_MASK properties have nothing to act on. verifyServerPolicy still
    // decides whether a mismatch is fatal, matching the X.509 paths.
    //
    // Note `signature_ok` is always 0 here: without DANE enabled, OpenSSL presets
    // X509_V_ERR_RPK_UNTRUSTED and invokes this callback with `ctx->error == X509_V_OK` being
    // false (see verify_rpk() in crypto/x509/x509_vfy.c). Our pin match is what decides the
    // outcome, so on success the preset error has to be cleared -- otherwise it survives into
    // SSL_get_verify_result() and marks a properly pinned connection as unverified.
    const SSLRPKUtils::TrustedKeySet *trusted = ssl_get_trusted_rpk(ssl);
    bool                              pin_ok  = trusted != nullptr && SSLRPKUtils::pinnedKeyMatches(peer_rpk, *trusted);
    Dbg(dbg_ctl_ssl_verify, "Origin authenticated with a raw public key (RFC 7250), pin match=%s", pin_ok ? "yes" : "no");
    if (pin_ok) {
      X509_STORE_CTX_set_error(ctx, X509_V_OK);
    } else {
      char buff[INET6_ADDRSTRLEN];
      ats_ip_ntop(netvc->get_effective_remote_addr(), buff, INET6_ADDRSTRLEN);
      Warning("Origin raw public key did not match any trusted key. Action=%s server=%s(%s)",
              enforce_mode ? "Terminate" : "Continue", netvc->options.ssl_servername.get(), buff);
      if (!enforce_mode) {
        // Permissive mode continues the handshake, and the X.509 paths likewise leave the
        // recorded error in place for a failure that is only warned about.
        X509_STORE_CTX_set_error(ctx, X509_V_ERR_RPK_UNTRUSTED);
      }
    }
    // The hook always runs, as on the X.509 path below: plugins observe every attempt and may add
    // rejection, but cannot turn a failed pin match into acceptance.
    TLSBasicSupport *tbs = TLSBasicSupport::getInstance(ssl);
    if (tbs == nullptr) {
      Dbg(dbg_ctl_ssl_verify, "call back on stale netvc");
      return false;
    }
    if (tbs->verify_certificate(ctx) == 1) {
      Warning("TS_EVENT_SSL_VERIFY_SERVER plugin failed the origin raw public key check for %s. Action=%s",
              netvc->options.ssl_servername.get(), enforce_mode ? "Terminate" : "Continue");
      return !enforce_mode;
    }
    return pin_ok || !enforce_mode;
  }
#endif

  if (check_sig) {
    if (!signature_ok) {
      Dbg(dbg_ctl_ssl_verify, "verification error:num=%d:%s:depth=%d", err, X509_verify_cert_error_string(err), depth);
      const char *sni_name;
      char        buff[INET6_ADDRSTRLEN];
      ats_ip_ntop(netvc->get_effective_remote_addr(), buff, INET6_ADDRSTRLEN);
      if (netvc->options.sni_servername) {
        sni_name = netvc->options.sni_servername.get();
      } else {
        sni_name = buff;
      }
      Warning("Core server certificate verification failed for (%s). Action=%s Error=%s server=%s(%s) depth=%d", sni_name,
              enforce_mode ? "Terminate" : "Continue", X509_verify_cert_error_string(err), netvc->options.ssl_servername.get(),
              buff, depth);
      // If not enforcing ignore the error, just log warning
      return enforce_mode ? signature_ok : 1;
    }
  }
  // Don't check names and other things unless this is the terminal cert
  if (depth != 0) {
    // Not server cert....
    return signature_ok;
  }

  bool check_name =
    static_cast<uint8_t>(netvc->options.verifyServerProperties) & static_cast<uint8_t>(YamlSNIConfig::Property::NAME_MASK);
  if (check_name) {
    char            *matched_name = nullptr;
    std::string_view sni_name;
    char             buff[INET6_ADDRSTRLEN];
    if (netvc->options.sni_servername) {
      sni_name = netvc->options.sni_servername.get();
    } else {
      ats_ip_ntop(netvc->get_effective_remote_addr(), buff, INET6_ADDRSTRLEN);
      sni_name = buff;
    }
    if (validate_hostname(cert, sni_name, false, &matched_name)) {
      Dbg(dbg_ctl_ssl_verify, "Hostname %.*s verified OK, matched %s", static_cast<int>(sni_name.length()), sni_name.data(),
          matched_name);
      ats_free(matched_name);
    } else { // Name validation failed
      // Get the server address if we did't already compute it
      if (netvc->options.sni_servername) {
        ats_ip_ntop(netvc->get_effective_remote_addr(), buff, INET6_ADDRSTRLEN);
      }
      // If we got here the verification failed
      Warning("SNI (%.*s) not in certificate. Action=%s server=%s(%s)", static_cast<int>(sni_name.length()), sni_name.data(),
              enforce_mode ? "Terminate" : "Continue", netvc->options.ssl_servername.get(), buff);
      return !enforce_mode;
    }
  }

  // If the previous configured checks passed, give the hook a try
  TLSBasicSupport *tbs = TLSBasicSupport::getInstance(ssl);
  if (tbs == nullptr) {
    Dbg(dbg_ctl_ssl_verify, "call back on stale netvc");
    return false;
  }
  if (tbs->verify_certificate(ctx) == 1) {
    // Verify server hook failed and set the status to SSL_HANDSHAKE_ERROR
    unsigned char *sni_name;
    char           buff[INET6_ADDRSTRLEN];
    if (netvc->options.sni_servername) {
      sni_name = reinterpret_cast<unsigned char *>(netvc->options.sni_servername.get());
    } else {
      sni_name = reinterpret_cast<unsigned char *>(buff);
      ats_ip_ntop(netvc->get_effective_remote_addr(), buff, INET6_ADDRSTRLEN);
    }
    Warning("TS_EVENT_SSL_VERIFY_SERVER plugin failed the origin certificate check for %s.  Action=%s SNI=%s",
            netvc->options.ssl_servername.get(), enforce_mode ? "Terminate" : "Continue", sni_name);
    return !enforce_mode;
  }
  // Made it this far.  All is good
  return true;
}

#if HAVE_SSL_CREDENTIAL_NEW_RAW_PUBLIC_KEY
// BoringSSL rejects raw public keys outright unless a custom verify callback is installed, and
// SSL_set_custom_verify() displaces SSL_set_verify() (and with it BoringSSL's automatic chain
// verification) for the whole connection. So this callback owns both cases: pin the peer's raw
// public key, or -- when the next hop negotiated X.509 after all, the normal state mid-rollout --
// rebuild and verify the chain by hand before deferring to the usual policy/name/hook logic.
static enum ssl_verify_result_t
ssl_client_custom_verify_callback(SSL *ssl, uint8_t *out_alert)
{
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  if (netvc == nullptr) {
    Dbg(dbg_ctl_ssl_verify, "WARNING, NetVC is NULL in custom cert verify callback");
    *out_alert = SSL_AD_INTERNAL_ERROR;
    return ssl_verify_invalid;
  }
  if (netvc->options.verifyServerPolicy == YamlSNIConfig::Policy::DISABLED) {
    return ssl_verify_ok;
  }

  bool const enforce_mode = netvc->options.verifyServerPolicy == YamlSNIConfig::Policy::ENFORCED;

  TLSBasicSupport *tbs = TLSBasicSupport::getInstance(ssl);
  if (tbs == nullptr) {
    Dbg(dbg_ctl_ssl_verify, "custom verify callback on stale netvc");
    *out_alert = SSL_AD_INTERNAL_ERROR;
    return ssl_verify_invalid;
  }

  if (SSL_get_peer_cert_type(ssl) == TLSEXT_cert_type_rpk) {
    EVP_PKEY                         *peer_rpk = SSL_get0_peer_rpk(ssl);
    const SSLRPKUtils::TrustedKeySet *trusted  = ssl_get_trusted_rpk(ssl);
    bool                              pin_ok   = trusted != nullptr && SSLRPKUtils::pinnedKeyMatches(peer_rpk, *trusted);
    Dbg(dbg_ctl_ssl_verify, "Origin authenticated with a raw public key (RFC 7250), pin match=%s", pin_ok ? "yes" : "no");
    if (!pin_ok) {
      char buff[INET6_ADDRSTRLEN];
      ats_ip_ntop(netvc->get_effective_remote_addr(), buff, INET6_ADDRSTRLEN);
      Warning("Origin raw public key did not match any trusted key. Action=%s server=%s(%s)",
              enforce_mode ? "Terminate" : "Continue", netvc->options.ssl_servername.get(), buff);
    }

    // There is no X509_STORE_CTX to hand the hook for a raw public key, but the hook still runs
    // on every attempt, as on the X.509 paths.
    if (tbs->verify_certificate(nullptr) == 1) {
      Warning("TS_EVENT_SSL_VERIFY_SERVER plugin failed the origin raw public key check for %s. Action=%s",
              netvc->options.ssl_servername.get(), enforce_mode ? "Terminate" : "Continue");
      if (enforce_mode) {
        *out_alert = SSL_AD_CERTIFICATE_UNKNOWN;
        return ssl_verify_invalid;
      }
      return ssl_verify_ok;
    }
    if (!pin_ok && enforce_mode) {
      *out_alert = SSL_AD_CERTIFICATE_UNKNOWN;
      return ssl_verify_invalid;
    }
    return ssl_verify_ok;
  }

  // X.509 fallback. Rebuild the chain BoringSSL hands back as CRYPTO_BUFFERs so the shared
  // verify_callback() logic (signature/name/policy/hook) can run against a real X509_STORE_CTX.
  const STACK_OF(CRYPTO_BUFFER) *chain = SSL_get0_peer_certificates(ssl);
  if (chain == nullptr || sk_CRYPTO_BUFFER_num(chain) == 0) {
    if (enforce_mode) {
      *out_alert = SSL_AD_CERTIFICATE_REQUIRED;
      return ssl_verify_invalid;
    }
    return ssl_verify_ok;
  }

  X509 *leaf                    = nullptr;
  STACK_OF(X509) *intermediates = sk_X509_new_null();
  if (intermediates == nullptr) {
    *out_alert = SSL_AD_INTERNAL_ERROR;
    return ssl_verify_invalid;
  }
  for (size_t i = 0; i < sk_CRYPTO_BUFFER_num(chain); i++) {
    const CRYPTO_BUFFER *buf  = sk_CRYPTO_BUFFER_value(chain, i);
    const uint8_t       *data = CRYPTO_BUFFER_data(buf);
    X509                *cert = d2i_X509(nullptr, &data, CRYPTO_BUFFER_len(buf));
    if (cert == nullptr) {
      SSLError("failed to parse an origin certificate on a RPK-enabled connection");
      X509_free(leaf);
      sk_X509_pop_free(intermediates, X509_free);
      *out_alert = SSL_AD_BAD_CERTIFICATE;
      return ssl_verify_invalid;
    }
    if (i == 0) {
      leaf = cert;
    } else {
      sk_X509_push(intermediates, cert);
    }
  }

  X509_STORE_CTX *store_ctx = X509_STORE_CTX_new();
  bool const      initialized =
    store_ctx != nullptr && X509_STORE_CTX_init(store_ctx, SSL_CTX_get_cert_store(SSL_get_SSL_CTX(ssl)), leaf, intermediates);
  bool accepted = false;
  if (initialized) {
    X509_STORE_CTX_set_depth(store_ctx, SSL_CTX_get_verify_depth(SSL_get_SSL_CTX(ssl)));

    bool const signature_ok = X509_verify_cert(store_ctx) == 1;
    bool const check_sig =
      static_cast<uint8_t>(netvc->options.verifyServerProperties) & static_cast<uint8_t>(YamlSNIConfig::Property::SIGNATURE_MASK);
    bool const check_name =
      static_cast<uint8_t>(netvc->options.verifyServerProperties) & static_cast<uint8_t>(YamlSNIConfig::Property::NAME_MASK);

    char buff[INET6_ADDRSTRLEN];
    ats_ip_ntop(netvc->get_effective_remote_addr(), buff, INET6_ADDRSTRLEN);
    std::string_view sni_name = netvc->options.sni_servername ? netvc->options.sni_servername.get() : buff;

    // This mirrors verify_callback()'s terminal-certificate logic rather than delegating to it.
    // OpenSSL drives that callback once per chain depth and it bails out at the depth that
    // failed; here X509_verify_cert() has already collapsed the whole chain into one verdict, so
    // running the remaining checks inline is what keeps permissive mode behaving the same --
    // a chain failure must still fall through to the name check and the hook, not return early.
    accepted = true;
    if (check_sig && !signature_ok) {
      int const err = X509_STORE_CTX_get_error(store_ctx);
      Dbg(dbg_ctl_ssl_verify, "verification error:num=%d:%s", err, X509_verify_cert_error_string(err));
      Warning("Core server certificate verification failed for (%.*s). Action=%s Error=%s server=%s(%s)",
              static_cast<int>(sni_name.length()), sni_name.data(), enforce_mode ? "Terminate" : "Continue",
              X509_verify_cert_error_string(err), netvc->options.ssl_servername.get(), buff);
      accepted = !enforce_mode;
    }

    if (accepted && check_name) {
      char *matched_name = nullptr;
      if (validate_hostname(leaf, sni_name, false, &matched_name)) {
        Dbg(dbg_ctl_ssl_verify, "Hostname %.*s verified OK, matched %s", static_cast<int>(sni_name.length()), sni_name.data(),
            matched_name);
        ats_free(matched_name);
      } else {
        Warning("SNI (%.*s) not in certificate. Action=%s server=%s(%s)", static_cast<int>(sni_name.length()), sni_name.data(),
                enforce_mode ? "Terminate" : "Continue", netvc->options.ssl_servername.get(), buff);
        accepted = !enforce_mode;
      }
    }

    // As on the other paths, the hook always runs and may only add rejection.
    if (tbs->verify_certificate(store_ctx) == 1) {
      Warning("TS_EVENT_SSL_VERIFY_SERVER plugin failed the origin certificate check for %s.  Action=%s SNI=%.*s",
              netvc->options.ssl_servername.get(), enforce_mode ? "Terminate" : "Continue", static_cast<int>(sni_name.length()),
              sni_name.data());
      accepted = !enforce_mode;
    }
  } else {
    SSLError("failed to initialize X509_STORE_CTX for origin certificate verification");
  }

  X509_STORE_CTX_free(store_ctx);
  X509_free(leaf);
  sk_X509_pop_free(intermediates, X509_free);

  if (!initialized) {
    *out_alert = SSL_AD_INTERNAL_ERROR;
    return ssl_verify_invalid;
  }
  if (!accepted) {
    *out_alert = SSL_AD_CERTIFICATE_UNKNOWN;
    return ssl_verify_invalid;
  }
  return ssl_verify_ok;
}
#endif

bool
validate_server_certificate_hostname(NetVConnection *netvc, std::string_view hostname)
{
  if (netvc == nullptr || hostname.empty() || netvc->options.verifyServerPolicy == YamlSNIConfig::Policy::DISABLED) {
    return true;
  }

  auto *tls = netvc->get_service<TLSBasicSupport>();
  auto *ssl = tls != nullptr ? tls->get_tls_handle() : nullptr;
  if (ssl == nullptr) {
    return true;
  }

#if TS_USE_RPK
  // A resumed session that originally authenticated with a raw public key has no certificate and
  // no SAN to match a hostname against; the pin check done during the original handshake stands.
  // The live connection's negotiation state is gone by now, so this has to consult the session
  // rather than SSL_get0_peer_rpk().
  if (SSL_SESSION *session = SSL_get_session(ssl); session != nullptr && SSL_SESSION_get0_peer_rpk(session) != nullptr) {
    Dbg(dbg_ctl_ssl_verify, "Skipping hostname validation for session reuse: peer authenticated with a raw public key");
    return true;
  }
#endif

  bool check_name =
    static_cast<uint8_t>(netvc->options.verifyServerProperties) & static_cast<uint8_t>(YamlSNIConfig::Property::NAME_MASK);
  if (!check_name) {
    return true;
  }

  char      *matched_name = nullptr;
  bool const enforce_mode = netvc->options.verifyServerPolicy == YamlSNIConfig::Policy::ENFORCED;
  bool       verified     = false;
#ifdef OPENSSL_IS_AT_LEAST_OPENSSL3
  X509 *cert = SSL_get1_peer_certificate(ssl);
#else
  X509 *cert = SSL_get_peer_certificate(ssl);
#endif

  if (cert != nullptr) {
    verified = validate_hostname(cert, hostname, false, &matched_name);
    X509_free(cert);
  }

  if (verified) {
    Dbg(dbg_ctl_ssl_verify, "Hostname %.*s verified OK for session reuse, matched %s", static_cast<int>(hostname.length()),
        hostname.data(), matched_name != nullptr ? matched_name : "<unknown>");
    ats_free(matched_name);
    return true;
  }

  char        buff[INET6_ADDRSTRLEN];
  const char *server_name = netvc->options.ssl_servername ? netvc->options.ssl_servername.get() : "<unknown>";
  ats_ip_ntop(netvc->get_effective_remote_addr(), buff, INET6_ADDRSTRLEN);
  Warning("Origin hostname (%.*s) not in certificate. Action=%s server=%s(%s)", static_cast<int>(hostname.length()),
          hostname.data(), enforce_mode ? "Terminate" : "Continue", server_name, buff);

  return !enforce_mode;
}

static int
ssl_client_cert_callback(SSL *ssl, void * /*arg*/)
{
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  SSL_CTX           *ctx   = SSL_get_SSL_CTX(ssl);
  if (ctx) {
    // Do not need to free either the cert or the ssl_ctx
    // both are internal pointers
    X509 *cert = SSL_CTX_get0_certificate(ctx);
    netvc->set_sent_cert(cert != nullptr ? 2 : 1);
    Dbg(dbg_ctl_ssl_verify, "sent cert: %d", cert != nullptr ? 2 : 1);
  }
  return 1;
}

static int
ssl_new_session_callback(SSL *ssl, SSL_SESSION *sess)
{
  std::string sni_addr = get_sni_addr(ssl);
  if (!sni_addr.empty()) {
    std::string lookup_key;
    swoc::bwprint(lookup_key, "{}:{}:{}", sni_addr.c_str(), SSL_get_SSL_CTX(ssl), get_verify_str(ssl));
    origin_sess_cache->insert_session(lookup_key, sess, ssl);
  } else {
    Dbg(dbg_ctl_ssl_origin_session_cache, "Failed to fetch SNI/IP.");
  }

  // return 0 here since we're converting the sessions using i2d_SSL_SESSION,
  // meaning if we return 1, openssl will keep an extra refcount on the session.
  return 0;
}

#if TS_USE_RPK
bool
ssl_client_setup_rpk(SSL *ssl, bool offer_rpk, const std::string &trusted_key_file)
{
  if (!offer_rpk && trusted_key_file.empty()) {
    return true;
  }

  static std::once_flag rpk_index_once;
  std::call_once(rpk_index_once, []() {
    ssl_server_rpk_index = SSL_get_ex_new_index(0, (void *)"Trusted next-hop RPK keys", nullptr, nullptr, ssl_server_rpk_ex_free);
  });
  if (ssl_server_rpk_index < 0) {
    SSLError("failed to reserve an ex_data index for next-hop raw public keys");
    return false;
  }

  if (!trusted_key_file.empty()) {
    auto *trusted = new SSLRPKUtils::TrustedKeySet();
    if (!SSLRPKUtils::loadTrustedKeys(trusted_key_file.c_str(), *trusted)) {
      delete trusted;
      return false;
    }
    // ssl_server_rpk_ex_free() releases `trusted` when ssl is freed.
    if (!SSL_set_ex_data(ssl, ssl_server_rpk_index, trusted)) {
      delete trusted;
      SSLError("failed to attach trusted next-hop raw public keys to the connection");
      return false;
    }

    // Accept a raw public key from the next hop, still preferring it over X.509 only when the
    // peer also supports it.
    static const unsigned char server_types[] = {TLSEXT_cert_type_rpk, TLSEXT_cert_type_x509};
#if HAVE_SSL_CTX_SET1_SERVER_CERT_TYPE
    if (!SSL_set1_server_cert_type(ssl, server_types, sizeof(server_types))) {
#else
    if (!SSL_set1_accepted_peer_cert_types(ssl, server_types, sizeof(server_types))) {
#endif
      SSLError("failed to enable RPK server cert type negotiation for the outbound connection");
      return false;
    }
  }

  if (offer_rpk) {
    static const unsigned char client_types[] = {TLSEXT_cert_type_rpk, TLSEXT_cert_type_x509};
    // Both libraries derive/wrap the offered raw public key from the client certificate/key
    // already configured on the context -- there is nothing to offer if that's unset.
    if (SSL_CTX_get0_privatekey(SSL_get_SSL_CTX(ssl)) == nullptr) {
      SSLError("client_rpk_enabled requires a client certificate/key configured for this next hop");
      return false;
    }
#if HAVE_SSL_CTX_SET1_SERVER_CERT_TYPE
    // OpenSSL derives the offered raw public key from the certificate/key already on the context.
    if (!SSL_set1_client_cert_type(ssl, client_types, sizeof(client_types))) {
      SSLError("failed to enable RPK client cert type negotiation for the outbound connection");
      return false;
    }
#else
    // BoringSSL needs an explicit credential, wrapping that same already-configured key.
    EVP_PKEY       *pkey = SSL_CTX_get0_privatekey(SSL_get_SSL_CTX(ssl));
    SSL_CREDENTIAL *cred = SSL_CREDENTIAL_new_raw_public_key(pkey);
    if (cred == nullptr || !SSL_add1_credential(ssl, cred)) {
      SSLError("failed to add the outbound RPK credential");
      SSL_CREDENTIAL_free(cred);
      return false;
    }
    SSL_CREDENTIAL_free(cred);
    if (!SSL_set1_available_client_cert_types(ssl, client_types, sizeof(client_types))) {
      SSLError("failed to advertise RPK client cert types for the outbound connection");
      return false;
    }
#endif
  }

#if HAVE_SSL_CREDENTIAL_NEW_RAW_PUBLIC_KEY
  // BoringSSL rejects raw public keys unless a custom verify callback is installed, and this
  // displaces the SSL_set_verify()/verify_callback() pair the caller already set for this
  // connection. Only next hops actually prepared to accept/pin an RPK server key take this path
  // -- an offer-only connection (client_rpk_enabled with no server_rpk_ca) never advertises RPK
  // acceptance above, so the peer will always present X.509 and the classic callback suffices.
  if (!trusted_key_file.empty()) {
    SSL_set_custom_verify(ssl, SSL_VERIFY_PEER, ssl_client_custom_verify_callback);
  }
#endif

  return true;
}
#endif

SSL_CTX *
SSLInitClientContext(const SSLConfigParams *params)
{
  const SSL_METHOD *meth       = nullptr;
  SSL_CTX          *client_ctx = nullptr;

  // Note that we do not call RAND_seed() explicitly here, we depend on OpenSSL
  // to do the seeding of the PRNG for us. This is the case for all platforms that
  // has /dev/urandom for example.

  meth       = SSLv23_client_method();
  client_ctx = SSL_CTX_new(meth);

  if (!client_ctx) {
    SSLError("cannot create new client context");
    ::exit(1);
  }

  SSL_CTX_set_options(client_ctx, params->ssl_client_ctx_options);
  if (params->client_cipherSuite != nullptr) {
    if (!SSL_CTX_set_cipher_list(client_ctx, params->client_cipherSuite)) {
      SSLError("invalid client cipher suite in %s", ts::filename::RECORDS);
      goto fail;
    }
  }

  if (params->client_tls_ver_min >= 0 || params->client_tls_ver_max >= 0) {
    int ver = 0;
    if (params->client_tls_ver_min >= 0) {
      ver = TLS1_VERSION + params->client_tls_ver_min;
    }
    // Setting 0 enables version down to the lowest version supported by the SSL library
    SSL_CTX_set_min_proto_version(client_ctx, ver);

    ver = 0;
    if (params->client_tls_ver_max >= 0) {
      ver = TLS1_VERSION + params->client_tls_ver_max;
    }
    // Setting 0 enables version up to the highest version supported by the SSL library
    SSL_CTX_set_max_proto_version(client_ctx, ver);
  }

#if TS_USE_TLS_SET_CIPHERSUITES
  if (params->client_tls13_cipher_suites != nullptr) {
    if (!SSL_CTX_set_ciphersuites(client_ctx, params->client_tls13_cipher_suites)) {
      SSLError("invalid tls client cipher suites in %s", ts::filename::RECORDS);
      goto fail;
    }
  }
#endif

#if defined(SSL_CTX_set1_groups_list) || defined(SSL_CTX_set1_curves_list)
  if (params->client_groups_list != nullptr) {
#ifdef SSL_CTX_set1_groups_list
    if (!SSL_CTX_set1_groups_list(client_ctx, params->client_groups_list)) {
#else
    if (!SSL_CTX_set1_curves_list(client_ctx, params->client_groups_list)) {
#endif
      SSLError("invalid groups list for client in %s", ts::filename::RECORDS);
      goto fail;
    }
  }
#endif

  if (params->client_cert_compression_algorithms) {
    std::vector<std::string> algs;
    SimpleTokenizer          tok(params->client_cert_compression_algorithms, ',');
    for (const char *token = tok.getNext(); token; token = tok.getNext()) {
      algs.emplace_back(token);
    }
    if (register_certificate_compression_preference(client_ctx, algs, true) != 1) {
      SSLError("invalid client certificate compression algorithm list in %s", ts::filename::RECORDS);
      goto fail;
    }
  }

  SSL_CTX_set_verify_depth(client_ctx, params->client_verify_depth);
  if (SSLConfigParams::init_ssl_ctx_cb) {
    SSLConfigParams::init_ssl_ctx_cb(client_ctx, false);
  }

  SSL_CTX_set_cert_cb(client_ctx, ssl_client_cert_callback, nullptr);

  if (params->ssl_origin_session_cache == 1) {
    SSL_CTX_set_session_cache_mode(client_ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_AUTO_CLEAR | SSL_SESS_CACHE_NO_INTERNAL);
    SSL_CTX_sess_set_new_cb(client_ctx, ssl_new_session_callback);
  }

#if TS_HAS_TLS_KEYLOGGING
  if (unlikely(TLSKeyLogger::is_enabled())) {
    SSL_CTX_set_keylog_callback(client_ctx, TLSKeyLogger::ssl_keylog_cb);
  }
#endif

  return client_ctx;

fail:
  SSLReleaseContext(client_ctx);
  ::exit(1);
}

SSL_CTX *
SSLCreateClientContext(const struct SSLConfigParams *params, const char *ca_bundle_path, const char *ca_bundle_file,
                       const char *cert_path, const char *key_path)
{
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(nullptr, &SSL_CTX_free);

  if (nullptr == params || nullptr == cert_path) {
    return nullptr;
  }

  ctx.reset(SSLInitClientContext(params));

  if (!ctx) {
    return nullptr;
  }

  if (!SSL_CTX_use_certificate_chain_file(ctx.get(), cert_path)) {
    SSLError("SSLCreateClientContext(): failed to load client certificate: %s",
             (!cert_path || cert_path[0] == '\0') ? "[empty file name]" : cert_path);
    return nullptr;
  }

  if (!key_path || key_path[0] == '\0') {
    key_path = cert_path;
  }

  if (!SSL_CTX_use_PrivateKey_file(ctx.get(), key_path, SSL_FILETYPE_PEM)) {
    SSLError("SSLCreateClientContext(): failed to load client private key: %s",
             (!key_path || key_path[0] == '\0') ? "[empty file]" : key_path);
    return nullptr;
  }

  if (!SSL_CTX_check_private_key(ctx.get())) {
    SSLError("SSLCreateClientContext(): client private key: %s does not match client certificate: %s",
             (!key_path || key_path[0] == '\0') ? "[empty file]" : key_path,
             (!cert_path || cert_path[0] == '\0') ? "[empty file]" : cert_path);
    return nullptr;
  }

  if (ca_bundle_file || ca_bundle_path) {
    if (!SSL_CTX_load_verify_locations(ctx.get(), ca_bundle_file, ca_bundle_path)) {
      SSLError("SSLCreateClientContext(): Invalid CA Certificate file: %s or CA Certificate path: %s",
               (!ca_bundle_file || ca_bundle_file[0] == '\0') ? "[empty file name]" : ca_bundle_file,
               (!ca_bundle_path || ca_bundle_path[0] == '\0') ? "[empty path]" : ca_bundle_path);
      SSLError("SSLCreateClientContext(): Invalid client CA cert file/CA path.");
      return nullptr;
    }
  } else if (!SSL_CTX_set_default_verify_paths(ctx.get())) {
    SSLError("SSLCreateClientContext(): failed to set the default verify paths.");
    return nullptr;
  }
  return ctx.release();
}
