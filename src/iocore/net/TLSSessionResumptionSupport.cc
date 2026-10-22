/** @file

  TLSSessionResumptionSupport.cc provides implementations for
  TLSSessionResumptionSupport methods

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

// Check if the ticket_key callback #define is available, and if so, enable session tickets.

#include "iocore/net/TLSSessionResumptionSupport.h"
#include "P_SSLCertLookup.h"
#include "P_SSLUtils.h"
#include "iocore/net/SSLAPIHooks.h"

#include "P_SSLConfig.h"
#include "SSLStats.h"

#include <memory>
#include <string_view>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/x509.h>
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
#include <openssl/core_names.h>
#endif

// Remove this when drop OpenSSL 1.0.2 support
#ifndef evp_md_func
#ifdef OPENSSL_NO_SHA256
#define evp_md_func EVP_sha1()
char mac_param_digest[] = "sha1";
#else
#define evp_md_func EVP_sha256()
char mac_param_digest[] = "sha256";
#endif
#endif

int TLSSessionResumptionSupport::_ex_data_index = -1;

namespace
{
DbgCtl dbg_ctl_ssl_session_ticket{"ssl_session_ticket"};

using unique_ssl_ticket_key_block = std::unique_ptr<ssl_ticket_key_block, void (*)(void *)>;

/** Derive one ticket secret from a global secret and a certificate digest.

    @param md_ctx A digest context to reuse; it is reinitialized here.
    @return Whether the derivation succeeded.
 */
bool
derive_ticket_secret(EVP_MD_CTX *md_ctx, std::string_view label, const unsigned char *secret, size_t secret_len,
                     const unsigned char *cert_digest, unsigned cert_digest_len, unsigned char *out, size_t out_len)
{
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned      md_len = 0;
  bool const    ok     = EVP_DigestInit_ex(md_ctx, EVP_sha256(), nullptr) == 1 &&
                  EVP_DigestUpdate(md_ctx, label.data(), label.size()) == 1 && EVP_DigestUpdate(md_ctx, secret, secret_len) == 1 &&
                  EVP_DigestUpdate(md_ctx, cert_digest, cert_digest_len) == 1 && EVP_DigestFinal_ex(md_ctx, md, &md_len) == 1 &&
                  md_len >= out_len;

  if (ok) {
    memcpy(out, md, out_len);
  }
  OPENSSL_cleanse(md, sizeof(md));
  return ok;
}

/** Derive the keys protecting the tickets of one certificate context from the global keys.

    The global keys are shared by every certificate context, so on their own a ticket issued under
    one certificate would resume under any other. Mixing in a digest of the context's certificate
    confines each ticket to contexts serving that certificate, while servers that share both the
    ticket keys and the certificate still derive the same keys and resume each other's tickets.

    The key names are derived too, so a ticket from another certificate matches no key by name and
    is counted as not found rather than as verified. Each derived key keeps the index of the key it
    came from, so rotation order is unchanged.

    The keys are derived on every call rather than cached, because the global keys can be replaced
    at any time independently of the certificate configuration, and a cached derivation would keep
    using keys that were rotated out.

    @return The derived keys, or null if the context has no certificate, or if computing the
    certificate digest or any derived key fails.
 */
unique_ssl_ticket_key_block
ticket_keyblock_for_certificate(const ssl_ticket_key_block &global, SSLCertContext &cc)
{
  unique_ssl_ticket_key_block derived{nullptr, ticket_block_free};
  shared_SSL_CTX              ctx  = cc.getCtx();
  X509                       *cert = ctx ? SSL_CTX_get0_certificate(ctx.get()) : nullptr;
  unsigned char               cert_digest[EVP_MAX_MD_SIZE];
  unsigned                    cert_digest_len = 0;

  if (cert == nullptr || X509_digest(cert, EVP_sha256(), cert_digest, &cert_digest_len) != 1) {
    return derived;
  }

  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md_ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
  if (md_ctx == nullptr) {
    return derived;
  }

  derived.reset(ticket_block_alloc(global.num_keys));
  for (unsigned i = 0; i < global.num_keys; ++i) {
    ssl_ticket_key_t const &from = global.keys[i];
    ssl_ticket_key_t       &to   = derived->keys[i];

    if (!derive_ticket_secret(md_ctx.get(), "ATS session ticket key name", from.key_name, sizeof(from.key_name), cert_digest,
                              cert_digest_len, to.key_name, sizeof(to.key_name)) ||
        !derive_ticket_secret(md_ctx.get(), "ATS session ticket HMAC secret", from.hmac_secret, sizeof(from.hmac_secret),
                              cert_digest, cert_digest_len, to.hmac_secret, sizeof(to.hmac_secret)) ||
        !derive_ticket_secret(md_ctx.get(), "ATS session ticket AES key", from.aes_key, sizeof(from.aes_key), cert_digest,
                              cert_digest_len, to.aes_key, sizeof(to.aes_key))) {
      derived.reset();
      break;
    }
  }
  return derived;
}

bool
is_ssl_session_timed_out(SSL_SESSION *session)
{
  return SSL_SESSION_get_timeout(session) < (time(nullptr) - SSL_SESSION_get_time(session));
}

} // end anonymous namespace

void
TLSSessionResumptionSupport::initialize()
{
  ink_assert(_ex_data_index == -1);
  if (_ex_data_index == -1) {
    _ex_data_index = SSL_get_ex_new_index(0, (void *)"TLSSessionResumptionSupport index", nullptr, nullptr, nullptr);
  }
}

TLSSessionResumptionSupport *
TLSSessionResumptionSupport::getInstance(SSL *ssl)
{
  return static_cast<TLSSessionResumptionSupport *>(SSL_get_ex_data(ssl, _ex_data_index));
}

void
TLSSessionResumptionSupport::bind(SSL *ssl, TLSSessionResumptionSupport *srs)
{
  SSL_set_ex_data(ssl, _ex_data_index, srs);
}

void
TLSSessionResumptionSupport::unbind(SSL *ssl)
{
  SSL_set_ex_data(ssl, _ex_data_index, nullptr);
}

#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
int
TLSSessionResumptionSupport::processSessionTicket(SSL *ssl, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx,
                                                  EVP_MAC_CTX *hctx, int enc)
#else
int
TLSSessionResumptionSupport::processSessionTicket(SSL *ssl, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx,
                                                  HMAC_CTX *hctx, int enc)
#endif
{
  SSLConfig::scoped_config            config;
  SSLCertificateConfig::scoped_config lookup;
  SSLTicketKeyConfig::scoped_config   params;

  ssl_ticket_key_block *keyblock = params->default_global_keyblock;
  ink_release_assert(keyblock != nullptr && keyblock->num_keys > 0);

  // The context is looked up by destination address for every connection. A client that sends no
  // SNI has nothing else to keep its tickets apart from another address's, so bind the global keys
  // to that context's certificate. Connections to an address with no context of its own use the
  // global keys as they are, and so do connections to a tunnel entry, which deliberately has no
  // certificate and terminates only the clients another entry serves by SNI.
  const IpEndpoint           &ip = this->_getCertLookupEndpoint();
  SSLCertContext             *cc = lookup->find(ip);
  unique_ssl_ticket_key_block cert_keyblock{nullptr, ticket_block_free};
  if (cc != nullptr && cc->opt != SSLCertContextOption::OPT_TUNNEL) {
    cert_keyblock = ticket_keyblock_for_certificate(*keyblock, *cc);
    if (cert_keyblock == nullptr) {
      // Neither issue nor accept a ticket that is not bound to a certificate.
      Metrics::Counter::increment(ssl_rsb.total_tickets_no_certificate);
      ip_port_text_buffer ipb;
      SiteThrottledWarning("could not bind session ticket keys to the certificate for %s, not using session tickets",
                           ats_ip_nptop(&ip, ipb, sizeof(ipb)));
      return 0;
    }
    keyblock = cert_keyblock.get();
  }

  if (enc == 1) {
    return this->_setSessionInformation(keyblock, keyname, iv, cipher_ctx, hctx);
  } else if (enc == 0) {
    return this->_getSessionInformation(keyblock, ssl, keyname, iv, cipher_ctx, hctx);
  }

  return -1;
}

bool
TLSSessionResumptionSupport::getIsResumedSSLSession() const
{
  return (this->_resumptionType == ResumptionType::RESUMED_FROM_SESSION_CACHE ||
          this->_resumptionType == ResumptionType::RESUMED_FROM_SESSION_TICKET) &&
         !this->_isResumedOriginSession;
}

bool
TLSSessionResumptionSupport::getIsResumedOriginSSLSession() const
{
  return (this->_resumptionType == ResumptionType::RESUMED_FROM_SESSION_CACHE ||
          this->_resumptionType == ResumptionType::RESUMED_FROM_SESSION_TICKET) &&
         this->_isResumedOriginSession;
}

bool
TLSSessionResumptionSupport::getIsResumedFromSessionCache() const
{
  return this->_resumptionType == ResumptionType::RESUMED_FROM_SESSION_CACHE;
}

bool
TLSSessionResumptionSupport::getIsResumedFromSessionTicket() const
{
  return this->_resumptionType == ResumptionType::RESUMED_FROM_SESSION_TICKET;
}

ssl_curve_id
TLSSessionResumptionSupport::getSSLCurveNID() const
{
  return this->_sslCurveNID;
}

std::string_view
TLSSessionResumptionSupport::getSSLGroupName() const
{
  return this->_sslGroupName;
}

std::shared_ptr<SSL_SESSION>
TLSSessionResumptionSupport::getOriginSession(const std::string &lookup_key)
{
  ssl_curve_id                 curve = 0;
  std::string                  group_name;
  std::shared_ptr<SSL_SESSION> shared_sess = origin_sess_cache->get_session(lookup_key, &curve, group_name);

  if (shared_sess != nullptr) {
    // Double check the timeout
    if (is_ssl_session_timed_out(shared_sess.get())) {
      Metrics::Counter::increment(ssl_rsb.origin_session_cache_miss);
      Metrics::Counter::increment(ssl_rsb.origin_session_cache_timeout);
      origin_sess_cache->remove_session(lookup_key);
      shared_sess.reset();
    } else {
      Metrics::Counter::increment(ssl_rsb.origin_session_cache_hit);
      this->_setResumptionType(ResumptionType::RESUMED_FROM_SESSION_CACHE, IS_RESUMED_ORIGIN_SESSION);
      this->_setSSLCurveNID(curve);
      this->_setSSLGroupName(group_name);
    }
  } else {
    Metrics::Counter::increment(ssl_rsb.origin_session_cache_miss);
  }
  return shared_sess;
}

void
TLSSessionResumptionSupport::clear()
{
  this->_resumptionType         = ResumptionType::NOT_RESUMED;
  this->_isResumedOriginSession = false;
  this->_sslCurveNID            = NID_undef;
  this->_sslGroupName.clear();
}

#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
int
TLSSessionResumptionSupport::_setSessionInformation(ssl_ticket_key_block *keyblock, unsigned char *keyname, unsigned char *iv,
                                                    EVP_CIPHER_CTX *cipher_ctx, EVP_MAC_CTX *hctx)
#else
int
TLSSessionResumptionSupport::_setSessionInformation(ssl_ticket_key_block *keyblock, unsigned char *keyname, unsigned char *iv,
                                                    EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx)
#endif
{
  const ssl_ticket_key_t &most_recent_key = keyblock->keys[0];
  memcpy(keyname, most_recent_key.key_name, sizeof(most_recent_key.key_name));
  if (RAND_bytes(iv, EVP_MAX_IV_LENGTH) != 1) {
    return -1;
  }
  if (EVP_EncryptInit_ex(cipher_ctx, EVP_aes_128_cbc(), nullptr, most_recent_key.aes_key, iv) != 1) {
    return -2;
  }
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
  const OSSL_PARAM params[] = {
    OSSL_PARAM_construct_octet_string(OSSL_MAC_PARAM_KEY, const_cast<unsigned char *>(most_recent_key.hmac_secret),
                                      sizeof(most_recent_key.hmac_secret)),
    OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, mac_param_digest, 0),
    OSSL_PARAM_construct_end(),
  };
  if (EVP_MAC_CTX_set_params(hctx, params) != 1) {
    return -3;
  }
#else
  if (HMAC_Init_ex(hctx, most_recent_key.hmac_secret, sizeof(most_recent_key.hmac_secret), evp_md_func, nullptr) != 1) {
    return -3;
  }
#endif

  Dbg(dbg_ctl_ssl_session_ticket, "create ticket for a new session.");
  Metrics::Counter::increment(ssl_rsb.total_tickets_created);
  return 1;
}

#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
int
TLSSessionResumptionSupport::_getSessionInformation(ssl_ticket_key_block *keyblock, [[maybe_unused]] SSL *ssl,
                                                    unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx,
                                                    EVP_MAC_CTX *hctx)
#else
int
TLSSessionResumptionSupport::_getSessionInformation(ssl_ticket_key_block *keyblock, [[maybe_unused]] SSL *ssl,
                                                    unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx,
                                                    HMAC_CTX *hctx)
#endif
{
  for (unsigned i = 0; i < keyblock->num_keys; ++i) {
    if (memcmp(keyname, keyblock->keys[i].key_name, sizeof(keyblock->keys[i].key_name)) == 0) {
      if (EVP_DecryptInit_ex(cipher_ctx, EVP_aes_128_cbc(), nullptr, keyblock->keys[i].aes_key, iv) != 1) {
        return -2;
      }
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
      const OSSL_PARAM params[] = {
        OSSL_PARAM_construct_octet_string(OSSL_MAC_PARAM_KEY, keyblock->keys[i].hmac_secret, sizeof(keyblock->keys[i].hmac_secret)),
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, mac_param_digest, 0),
        OSSL_PARAM_construct_end(),
      };
      if (EVP_MAC_CTX_set_params(hctx, params) != 1) {
        return -3;
      }
#else
      if (HMAC_Init_ex(hctx, keyblock->keys[i].hmac_secret, sizeof(keyblock->keys[i].hmac_secret), evp_md_func, nullptr) != 1) {
        return -3;
      }
#endif

      Dbg(dbg_ctl_ssl_session_ticket, "verify the ticket for an existing session.");
      // Increase the total number of decrypted tickets.
      Metrics::Counter::increment(ssl_rsb.total_tickets_verified);

      if (i != 0) { // The number of tickets decrypted with "older" keys.
        Metrics::Counter::increment(ssl_rsb.total_tickets_verified_old_key);
      }

      this->_setResumptionType(ResumptionType::RESUMED_FROM_SESSION_TICKET, !IS_RESUMED_ORIGIN_SESSION);
      // Do not call _setSSLCurveNID() and _setSSLGroupName() here because the
      // SSL object is not fully configured for this yet and the curve/group
      // info is not available. OpenSSL will crash.

#ifdef TLS1_3_VERSION
      if (SSL_version(ssl) >= TLS1_3_VERSION) {
        Dbg(dbg_ctl_ssl_session_ticket, "make sure tickets are only used once.");
        return 2;
      }
#endif

      // When we decrypt with an "older" key, encrypt the ticket again with the most recent key.
      return (i == 0) ? 1 : 2;
    }
  }

  Dbg(dbg_ctl_ssl_session_ticket, "keyname is not consistent.");
  Metrics::Counter::increment(ssl_rsb.total_tickets_not_found);
  return 0;
}

void
TLSSessionResumptionSupport::_setResumptionType(ResumptionType type, bool isOrigin)
{
  this->_resumptionType         = type;
  this->_isResumedOriginSession = isOrigin;
}

void
TLSSessionResumptionSupport::_setSSLCurveNID(ssl_curve_id curve_nid)
{
  this->_sslCurveNID = curve_nid;
}

void
TLSSessionResumptionSupport::_setSSLGroupName(std::string_view group_name)
{
  this->_sslGroupName = std::string{group_name};
}
