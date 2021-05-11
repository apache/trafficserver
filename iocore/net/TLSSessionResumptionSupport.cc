/** @file

  TLSSessionResumptionSupport.cc provides implmentations for
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

#include "TLSSessionResumptionSupport.h"

#include "P_SSLConfig.h"
#include "SSLStats.h"
#include <openssl/evp.h>
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
#include <openssl/core_names.h>
#endif
#include "InkAPIInternal.h"
#include "tscore/ink_config.h"

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

static bool
is_ssl_session_timed_out(SSL_SESSION *session)
{
  return SSL_SESSION_get_timeout(session) < (time(nullptr) - SSL_SESSION_get_time(session));
}

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
  SSLConfig::scoped_config config;
  SSLCertificateConfig::scoped_config lookup;
  SSLTicketKeyConfig::scoped_config params;

  // Get the IP address to look up the keyblock
  const IpEndpoint &ip           = this->_getLocalEndpoint();
  SSLCertContext *cc             = lookup->find(ip);
  ssl_ticket_key_block *keyblock = nullptr;
  if (cc == nullptr || cc->keyblock == nullptr) {
    // Try the default
    keyblock = params->default_global_keyblock;
  } else {
    keyblock = cc->keyblock.get();
  }
  ink_release_assert(keyblock != nullptr && keyblock->num_keys > 0);

  if (enc == 1) {
    return this->_setSessionInformation(keyblock, ssl, keyname, iv, cipher_ctx, hctx);
  } else if (enc == 0) {
    return this->_getSessionInformation(keyblock, ssl, keyname, iv, cipher_ctx, hctx);
  }

  return -1;
}

bool
TLSSessionResumptionSupport::getSSLSessionCacheHit() const
{
  return this->_sslSessionCacheHit;
}

ssl_curve_id
TLSSessionResumptionSupport::getSSLCurveNID() const
{
  return this->_sslCurveNID;
}

SSL_SESSION *
TLSSessionResumptionSupport::getSession(SSL *ssl, const unsigned char *id, int len, int *copy)
{
  SSLSessionID sid(id, len);

  *copy = 0;
  if (diags->tag_activated("ssl.session_cache")) {
    char printable_buf[(len * 2) + 1];
    sid.toString(printable_buf, sizeof(printable_buf));
    Debug("ssl.session_cache.get", "ssl_get_cached_session cached session '%s' context %p", printable_buf, SSL_get_SSL_CTX(ssl));
  }

  APIHook *hook = ssl_hooks->get(TSSslHookInternalID(TS_SSL_SESSION_HOOK));
  while (hook) {
    hook->invoke(TS_EVENT_SSL_SESSION_GET, &sid);
    hook = hook->m_link.next;
  }

  SSL_SESSION *session             = nullptr;
  ssl_session_cache_exdata *exdata = nullptr;
  if (session_cache->getSession(sid, &session, &exdata)) {
    ink_assert(session);
    ink_assert(exdata);

    // Double check the timeout
    if (is_ssl_session_timed_out(session)) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_miss);
// Due to bug in openssl, the timeout is checked, but only removed
// from the openssl built-in hash table.  The external remove cb is not called
#if 0 // This is currently eliminated, since it breaks things in odd ways (see TS-3710)
      ssl_rm_cached_session(SSL_get_SSL_CTX(ssl), session);
#endif
      session = nullptr;
    } else {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_hit);
      this->_setSSLSessionCacheHit(true);
      this->_setSSLCurveNID(exdata->curve);
    }
  } else {
    SSL_INCREMENT_DYN_STAT(ssl_session_cache_miss);
  }
  return session;
}

SSL_SESSION *
TLSSessionResumptionSupport::getOriginSession(SSL *ssl, const std::string &lookup_key)
{
  SSL_SESSION *session             = nullptr;
  ssl_session_cache_exdata *exdata = nullptr;
  if (origin_sess_cache->get_session(lookup_key, &session, &exdata)) {
    ink_assert(session);
    ink_assert(exdata);

    // Double check the timeout
    if (is_ssl_session_timed_out(session)) {
      SSL_INCREMENT_DYN_STAT(ssl_origin_session_cache_miss);
      session = nullptr;
    } else {
      SSL_INCREMENT_DYN_STAT(ssl_origin_session_cache_hit);
      this->_setSSLSessionCacheHit(true);
      this->_setSSLCurveNID(exdata->curve);
    }
  } else {
    SSL_INCREMENT_DYN_STAT(ssl_origin_session_cache_miss);
  }
  return session;
}

void
TLSSessionResumptionSupport::clear()
{
  this->_sslSessionCacheHit = false;
}

#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
int
TLSSessionResumptionSupport::_setSessionInformation(ssl_ticket_key_block *keyblock, SSL *ssl, unsigned char *keyname,
                                                    unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, EVP_MAC_CTX *hctx)
#else
int
TLSSessionResumptionSupport::_setSessionInformation(ssl_ticket_key_block *keyblock, SSL *ssl, unsigned char *keyname,
                                                    unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx)
#endif
{
  const ssl_ticket_key_t &most_recent_key = keyblock->keys[0];
  memcpy(keyname, most_recent_key.key_name, sizeof(most_recent_key.key_name));
  RAND_bytes(iv, EVP_MAX_IV_LENGTH);
  EVP_EncryptInit_ex(cipher_ctx, EVP_aes_128_cbc(), nullptr, most_recent_key.aes_key, iv);
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
  const OSSL_PARAM params[] = {
    OSSL_PARAM_construct_octet_string(OSSL_MAC_PARAM_KEY, const_cast<unsigned char *>(most_recent_key.hmac_secret),
                                      sizeof(most_recent_key.hmac_secret)),
    OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, mac_param_digest, 0),
    OSSL_PARAM_construct_end(),
  };
  EVP_MAC_CTX_set_params(hctx, params);
#else
  HMAC_Init_ex(hctx, most_recent_key.hmac_secret, sizeof(most_recent_key.hmac_secret), evp_md_func, nullptr);
#endif

  Debug("ssl_session_ticket", "create ticket for a new session.");
  SSL_INCREMENT_DYN_STAT(ssl_total_tickets_created_stat);
  return 1;
}

#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
int
TLSSessionResumptionSupport::_getSessionInformation(ssl_ticket_key_block *keyblock, SSL *ssl, unsigned char *keyname,
                                                    unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, EVP_MAC_CTX *hctx)
#else
int
TLSSessionResumptionSupport::_getSessionInformation(ssl_ticket_key_block *keyblock, SSL *ssl, unsigned char *keyname,
                                                    unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx)
#endif
{
  for (unsigned i = 0; i < keyblock->num_keys; ++i) {
    if (memcmp(keyname, keyblock->keys[i].key_name, sizeof(keyblock->keys[i].key_name)) == 0) {
      EVP_DecryptInit_ex(cipher_ctx, EVP_aes_128_cbc(), nullptr, keyblock->keys[i].aes_key, iv);
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
      const OSSL_PARAM params[] = {
        OSSL_PARAM_construct_octet_string(OSSL_MAC_PARAM_KEY, keyblock->keys[i].hmac_secret, sizeof(keyblock->keys[i].hmac_secret)),
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, mac_param_digest, 0),
        OSSL_PARAM_construct_end(),
      };
      EVP_MAC_CTX_set_params(hctx, params);
#else
      HMAC_Init_ex(hctx, keyblock->keys[i].hmac_secret, sizeof(keyblock->keys[i].hmac_secret), evp_md_func, nullptr);
#endif

      Debug("ssl_session_ticket", "verify the ticket for an existing session.");
      // Increase the total number of decrypted tickets.
      SSL_INCREMENT_DYN_STAT(ssl_total_tickets_verified_stat);

      if (i != 0) { // The number of tickets decrypted with "older" keys.
        SSL_INCREMENT_DYN_STAT(ssl_total_tickets_verified_old_key_stat);
      }

      this->_setSSLSessionCacheHit(true);

#ifdef TLS1_3_VERSION
      if (SSL_version(ssl) >= TLS1_3_VERSION) {
        Debug("ssl_session_ticket", "make sure tickets are only used once.");
        return 2;
      }
#endif

      // When we decrypt with an "older" key, encrypt the ticket again with the most recent key.
      return (i == 0) ? 1 : 2;
    }
  }

  Debug("ssl_session_ticket", "keyname is not consistent.");
  SSL_INCREMENT_DYN_STAT(ssl_total_tickets_not_found_stat);
  return 0;
}

void
TLSSessionResumptionSupport::_setSSLSessionCacheHit(bool state)
{
  this->_sslSessionCacheHit = state;
}

void
TLSSessionResumptionSupport::_setSSLCurveNID(ssl_curve_id curve_nid)
{
  this->_sslCurveNID = curve_nid;
}
