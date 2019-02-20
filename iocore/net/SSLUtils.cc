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

#include "P_SSLUtils.h"

#include "tscore/ink_platform.h"
#include "tscore/SimpleTokenizer.h"
#include "tscore/I_Layout.h"
#include "tscore/ink_cap.h"
#include "tscore/ink_mutex.h"
#include "records/I_RecHttp.h"

#include "P_Net.h"
#include "InkAPIInternal.h"

#include "P_OCSPStapling.h"
#include "P_SSLSNI.h"
#include "P_SSLConfig.h"
#include "SSLSessionCache.h"
#include "SSLDynlock.h"
#include "SSLDiags.h"
#include "SSLStats.h"

#include <string>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/asn1.h>
#include <openssl/rand.h>
#include <openssl/dh.h>
#include <openssl/bn.h>
#include <openssl/engine.h>
#include <openssl/conf.h>
#include <unistd.h>
#include <termios.h>
#include <vector>

#if HAVE_OPENSSL_EVP_H
#include <openssl/evp.h>
#endif

#if HAVE_OPENSSL_HMAC_H
#include <openssl/hmac.h>
#endif

#if HAVE_OPENSSL_TS_H
#include <openssl/ts.h>
#endif

#if HAVE_OPENSSL_EC_H
#include <openssl/ec.h>
#endif

// ssl_multicert.config field names:
#define SSL_IP_TAG "dest_ip"
#define SSL_CERT_TAG "ssl_cert_name"
#define SSL_PRIVATE_KEY_TAG "ssl_key_name"
#define SSL_CA_TAG "ssl_ca_name"
#define SSL_ACTION_TAG "action"
#define SSL_ACTION_TUNNEL_TAG "tunnel"
#define SSL_SESSION_TICKET_ENABLED "ssl_ticket_enabled"
#define SSL_KEY_DIALOG "ssl_key_dialog"
#define SSL_SERVERNAME "dest_fqdn"
#define SSL_CERT_SEPARATE_DELIM ','

// openssl version must be 0.9.4 or greater
#if (OPENSSL_VERSION_NUMBER < 0x00090400L)
#error Traffic Server requires an OpenSSL library version 0.9.4 or greater
#endif

#ifndef evp_md_func
#ifdef OPENSSL_NO_SHA256
#define evp_md_func EVP_sha1()
#else
#define evp_md_func EVP_sha256()
#endif
#endif

/*
 * struct ssl_user_config: gather user provided settings from ssl_multicert.config in to this single struct
 * ssl_ticket_enabled - session ticket enabled
 * ssl_cert_name - certificate
 * dest_ip - IPv[64] address to match
 * ssl_cert_name - certificate
 * first_cert - the first certificate name when multiple cert files are in 'ssl_cert_name'
 * ssl_ca_name - CA public certificate
 * ssl_key_name - Private key
 * ticket_key_name - session key file. [key_name (16Byte) + HMAC_secret (16Byte) + AES_key (16Byte)]
 * ssl_key_dialog - Private key dialog
 * servername - Destination server
 */
struct ssl_user_config {
  ssl_user_config() : opt(SSLCertContext::OPT_NONE)
  {
    REC_ReadConfigInt32(session_ticket_enabled, "proxy.config.ssl.server.session_ticket.enable");
  }

  int session_ticket_enabled;
  ats_scoped_str addr;
  ats_scoped_str cert;
  ats_scoped_str first_cert;
  ats_scoped_str ca;
  ats_scoped_str key;
  ats_scoped_str dialog;
  ats_scoped_str servername;
  SSLCertContext::Option opt;
};

SSLSessionCache *session_cache; // declared extern in P_SSLConfig.h

// Check if the ticket_key callback #define is available, and if so, enable session tickets.
#ifdef SSL_CTX_set_tlsext_ticket_key_cb

#define HAVE_OPENSSL_SESSION_TICKETS 1

static void session_ticket_free(void *, void *, CRYPTO_EX_DATA *, int, long, void *);
static int ssl_callback_session_ticket(SSL *, unsigned char *, unsigned char *, EVP_CIPHER_CTX *, HMAC_CTX *, int);
#endif /* SSL_CTX_set_tlsext_ticket_key_cb */

#if HAVE_OPENSSL_SESSION_TICKETS
static int ssl_session_ticket_index = -1;
#endif

static int ssl_vc_index = -1;

static ink_mutex *mutex_buf      = nullptr;
static bool open_ssl_initialized = false;

/* Using pthread thread ID and mutex functions directly, instead of
 * ATS this_ethread / ProxyMutex, so that other linked libraries
 * may use pthreads and openssl without confusing us here. (TS-2271).
 */

#if !defined(CRYPTO_THREADID_set_callback)
static void
SSL_pthreads_thread_id(CRYPTO_THREADID *id)
{
  CRYPTO_THREADID_set_numeric(id, (unsigned long)pthread_self());
}
#endif

// The locking callback goes away with openssl 1.1 and CRYPTO_LOCK is on longer defined
#if defined(CRYPTO_LOCK) && !defined(CRYPTO_set_locking_callback)
static void
SSL_locking_callback(int mode, int type, const char *file, int line)
{
  Debug("v_ssl_lock", "file: %s line: %d type: %d", file, line, type);
  ink_assert(type < CRYPTO_num_locks());

#ifdef OPENSSL_FIPS
  // don't need to lock for FIPS if it has POSTed and we are not going to change the mode on the fly
  if (type == CRYPTO_LOCK_FIPS || type == CRYPTO_LOCK_FIPS2) {
    return;
  }
#endif

  if (mode & CRYPTO_LOCK) {
    ink_mutex_acquire(&mutex_buf[type]);
  } else if (mode & CRYPTO_UNLOCK) {
    ink_mutex_release(&mutex_buf[type]);
  } else {
    Debug("ssl", "invalid SSL locking mode 0x%x", mode);
    ink_assert(0);
  }
}
#endif

static bool
SSL_CTX_add_extra_chain_cert_bio(SSL_CTX *ctx, BIO *bio)
{
  X509 *cert;

  for (;;) {
    cert = PEM_read_bio_X509_AUX(bio, nullptr, nullptr, nullptr);

    if (!cert) {
      // No more the certificates in this file.
      break;
    }

// This transfers ownership of the cert (X509) to the SSL context, if successful.
#ifdef SSL_CTX_add0_chain_cert
    if (!SSL_CTX_add0_chain_cert(ctx, cert)) {
#else
    if (!SSL_CTX_add_extra_chain_cert(ctx, cert)) {
#endif
      X509_free(cert);
      return false;
    }
  }

  return true;
}

static bool
SSL_CTX_add_extra_chain_cert_file(SSL_CTX *ctx, const char *chainfile)
{
  scoped_BIO bio(BIO_new_file(chainfile, "r"));
  return SSL_CTX_add_extra_chain_cert_bio(ctx, bio);
}

bool
ssl_session_timed_out(SSL_SESSION *session)
{
  return SSL_SESSION_get_timeout(session) < (long)(time(nullptr) - SSL_SESSION_get_time(session));
}

static void ssl_rm_cached_session(SSL_CTX *ctx, SSL_SESSION *sess);

static SSL_SESSION *
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
ssl_get_cached_session(SSL *ssl, unsigned char *id, int len, int *copy)
#else
ssl_get_cached_session(SSL *ssl, const unsigned char *id, int len, int *copy)
#endif
{
  SSLSessionID sid(id, len);

  *copy = 0;
  if (diags->tag_activated("ssl.session_cache")) {
    char printable_buf[(len * 2) + 1];
    sid.toString(printable_buf, sizeof(printable_buf));
    Debug("ssl.session_cache.get", "ssl_get_cached_session cached session '%s' context %p", printable_buf, SSL_get_SSL_CTX(ssl));
  }

  APIHook *hook = ssl_hooks->get(TS_SSL_SESSION_INTERNAL_HOOK);
  while (hook) {
    hook->invoke(TS_EVENT_SSL_SESSION_GET, &sid);
    hook = hook->m_link.next;
  }

  SSL_SESSION *session = nullptr;
  if (session_cache->getSession(sid, &session)) {
    ink_assert(session);

    // Double check the timeout
    if (ssl_session_timed_out(session)) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_miss);
// Due to bug in openssl, the timeout is checked, but only removed
// from the openssl built-in hash table.  The external remove cb is not called
#if 0 // This is currently eliminated, since it breaks things in odd ways (see TS-3710)
      ssl_rm_cached_session(SSL_get_SSL_CTX(ssl), session);
#endif
      session = nullptr;
    } else {
      SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_hit);
      netvc->setSSLSessionCacheHit(true);
    }
  } else {
    SSL_INCREMENT_DYN_STAT(ssl_session_cache_miss);
  }
  return session;
}

static int
ssl_new_cached_session(SSL *ssl, SSL_SESSION *sess)
{
  unsigned int len        = 0;
  const unsigned char *id = SSL_SESSION_get_id(sess, &len);
  SSLSessionID sid(id, len);

  if (diags->tag_activated("ssl.session_cache")) {
    char printable_buf[(len * 2) + 1];

    sid.toString(printable_buf, sizeof(printable_buf));
    Debug("ssl.session_cache.insert", "ssl_new_cached_session session '%s' and context %p", printable_buf, SSL_get_SSL_CTX(ssl));
  }

  SSL_INCREMENT_DYN_STAT(ssl_session_cache_new_session);
  session_cache->insertSession(sid, sess);

  // Call hook after new session is created
  APIHook *hook = ssl_hooks->get(TS_SSL_SESSION_INTERNAL_HOOK);
  while (hook) {
    hook->invoke(TS_EVENT_SSL_SESSION_NEW, &sid);
    hook = hook->m_link.next;
  }

  return 0;
}

static void
ssl_rm_cached_session(SSL_CTX *ctx, SSL_SESSION *sess)
{
  unsigned int len        = 0;
  const unsigned char *id = SSL_SESSION_get_id(sess, &len);
  SSLSessionID sid(id, len);

  // Call hook before session is removed
  APIHook *hook = ssl_hooks->get(TS_SSL_SESSION_INTERNAL_HOOK);
  while (hook) {
    hook->invoke(TS_EVENT_SSL_SESSION_REMOVE, &sid);
    hook = hook->m_link.next;
  }

  if (diags->tag_activated("ssl.session_cache")) {
    char printable_buf[(len * 2) + 1];
    sid.toString(printable_buf, sizeof(printable_buf));
    Debug("ssl.session_cache.remove", "ssl_rm_cached_session cached session '%s'", printable_buf);
  }

  session_cache->removeSession(sid);
}

int
set_context_cert(SSL *ssl)
{
  SSL_CTX *ctx       = nullptr;
  SSLCertContext *cc = nullptr;
  SSLCertificateConfig::scoped_config lookup;
  const char *servername   = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  bool found               = true;
  int retval               = 1;

  Debug("ssl", "set_context_cert ssl=%p server=%s handshake_complete=%d", ssl, servername, netvc->getSSLHandShakeComplete());

  // catch the client renegotiation early on
  if (SSLConfigParams::ssl_allow_client_renegotiation == false && netvc->getSSLHandShakeComplete()) {
    Debug("ssl", "set_context_cert trying to renegotiate from the client");
    retval = 0; // Error
    goto done;
  }

  // The incoming SSL_CTX is either the one mapped from the inbound IP address or the default one. If we
  // don't find a name-based match at this point, we *do not* want to mess with the context because we've
  // already made a best effort to find the best match.
  if (likely(servername)) {
    cc = lookup->find((char *)servername);
    if (cc && cc->ctx) {
      ctx = cc->ctx;
    }
    if (cc && SSLCertContext::OPT_TUNNEL == cc->opt && netvc->get_is_transparent()) {
      netvc->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
      netvc->setSSLHandShakeComplete(SSL_HANDSHAKE_DONE);
      retval = -1;
      goto done;
    }
  }

  // If there's no match on the server name, try to match on the peer address.
  if (ctx == nullptr) {
    IpEndpoint ip;
    int namelen = sizeof(ip);

    if (0 == safe_getsockname(netvc->get_socket(), &ip.sa, &namelen)) {
      cc = lookup->find(ip);
    }
    if (cc && cc->ctx) {
      ctx = cc->ctx;
    }
  }

  if (ctx != nullptr) {
    SSL_set_SSL_CTX(ssl, ctx);
#if HAVE_OPENSSL_SESSION_TICKETS
    // Reset the ticket callback if needed
    SSL_CTX_set_tlsext_ticket_key_cb(ctx, ssl_callback_session_ticket);
#endif
  } else {
    found = false;
  }

  ctx = SSL_get_SSL_CTX(ssl);
  Debug("ssl", "ssl_cert_callback %s SSL context %p for requested name '%s'", found ? "found" : "using", ctx, servername);

  if (ctx == nullptr) {
    retval = 0;
    goto done;
  }
done:
  return retval;
}

// Callback function for verifying client certificate
int
ssl_verify_client_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
  Debug("ssl", "Callback: verify client cert");
  auto *ssl                = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);

  netvc->callHooks(TS_EVENT_SSL_VERIFY_CLIENT);
  return preverify_ok;
}

static int
PerformAction(Continuation *cont, const char *servername)
{
  SNIConfig::scoped_config params;
  const actionVector *actionvec = params->get(servername);
  if (!actionvec) {
    Debug("ssl_sni", "%s not available in the map", servername);
  } else {
    for (auto &&item : *actionvec) {
      auto ret = item->SNIAction(cont);
      if (ret != SSL_TLSEXT_ERR_OK) {
        return ret;
      }
    }
  }
  return SSL_TLSEXT_ERR_OK;
}

#if TS_USE_HELLO_CB
// Pausable callback
static int
ssl_client_hello_callback(SSL *s, int *al, void *arg)
{
  SSLNetVConnection *netvc = SSLNetVCAccess(s);
  const char *servername   = nullptr;
  const unsigned char *p;
  size_t remaining, len;
  // Parse the servrer name if the get extension call succeeds and there are more than 2 bytes to parse
  if (SSL_client_hello_get0_ext(s, TLSEXT_TYPE_server_name, &p, &remaining) && remaining > 2) {
    // Parse to get to the name, originally from test/handshake_helper.c in openssl tree
    /* Extract the length of the supplied list of names. */
    len = *(p++) << 8;
    len += *(p++);
    if (len + 2 == remaining) {
      remaining = len;
      /*
       * The list in practice only has a single element, so we only consider
       * the first one.
       */
      if (remaining != 0 && *p++ == TLSEXT_NAMETYPE_host_name) {
        remaining--;
        /* Now we can finally pull out the byte array with the actual hostname. */
        if (remaining > 2) {
          len = *(p++) << 8;
          len += *(p++);
          if (len + 2 <= remaining) {
            remaining  = len;
            servername = reinterpret_cast<const char *>(p);
          }
        }
      }
    }
  }
  netvc->serverName = servername ? servername : "";
  int ret           = PerformAction(netvc, netvc->serverName);
  if (ret != SSL_TLSEXT_ERR_OK) {
    return SSL_CLIENT_HELLO_ERROR;
  }
  if (netvc->has_tunnel_destination() && !netvc->decrypt_tunnel()) {
    netvc->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
  }
  if (netvc->protocol_mask_set) {
    setTLSValidProtocols(s, netvc->protocol_mask, TLSValidProtocols::max_mask);
  }

  bool reenabled = netvc->callHooks(TS_EVENT_SSL_CLIENT_HELLO);

  if (!reenabled) {
    return SSL_CLIENT_HELLO_RETRY;
  }
  return SSL_CLIENT_HELLO_SUCCESS;
}
#endif

// Use the certificate callback for openssl 1.0.2 and greater
// otherwise use the SNI callback
#if TS_USE_CERT_CB
/**
 * Called before either the server or the client certificate is used
 * Return 1 on success, 0 on error, or -1 to pause
 */
static int
ssl_cert_callback(SSL *ssl, void * /*arg*/)
{
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  bool reenabled;
  int retval = 1;

  // If we are in tunnel mode, don't select a cert.  Pause!
  if (HttpProxyPort::TRANSPORT_BLIND_TUNNEL == netvc->attributes) {
    return -1; // Pause
  }

  // Do the common certificate lookup only once.  If we pause
  // and restart processing, do not execute the common logic again
  if (!netvc->calledHooks(TS_EVENT_SSL_CERT)) {
    retval = set_context_cert(ssl);
    if (retval != 1) {
      return retval;
    }
  }

  // Call the plugin cert code
  reenabled = netvc->callHooks(TS_EVENT_SSL_CERT);
  // If it did not re-enable, return the code to
  // stop the accept processing
  if (!reenabled) {
    retval = -1; // Pause
  }

  // Return 1 for success, 0 for error, or -1 to pause
  return retval;
}

/*
 * Cannot stop this callback. Always reeneabled
 */
static int
ssl_servername_only_callback(SSL *ssl, int * /* ad */, void * /*arg*/)
{
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  netvc->callHooks(TS_EVENT_SSL_SERVERNAME);

  netvc->serverName = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (nullptr == netvc->serverName) {
    netvc->serverName = "";
  }

  // Rerun the actions in case a plugin changed the server name
  int ret = PerformAction(netvc, netvc->serverName);
  if (ret != SSL_TLSEXT_ERR_OK) {
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }
  if (netvc->has_tunnel_destination() && !netvc->decrypt_tunnel()) {
    netvc->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
  }
  return SSL_TLSEXT_ERR_OK;
}

#else
static int
ssl_servername_and_cert_callback(SSL *ssl, int * /* ad */, void * /*arg*/)
{
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  bool reenabled;
  int retval = 1;

  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (servername == nullptr) {
    servername = "";
  }
  Debug("ssl", "Requested servername is %s", servername);
  int ret = PerformAction(netvc, servername);
  if (ret != SSL_TLSEXT_ERR_OK) {
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  // If we are in tunnel mode, don't select a cert.  Pause!
  if (HttpProxyPort::TRANSPORT_BLIND_TUNNEL == netvc->attributes) {
    return -1; // Pause
  }

  // Do the common certificate lookup only once.  If we pause
  // and restart processing, do not execute the common logic again
  if (!netvc->calledHooks(TS_EVENT_SSL_CERT)) {
    retval = set_context_cert(ssl);
    if (retval != 1) {
      goto done;
    }
  }

  // Call the plugin SNI code
  reenabled = netvc->callHooks(TS_EVENT_SSL_CERT);
  // If it did not re-enable, return the code to
  // stop the accept processing
  if (!reenabled) {
    retval = -1;
  }

done:
  // Map 1 to SSL_TLSEXT_ERR_OK
  // Map 0 to SSL_TLSEXT_ERR_ALERT_FATAL
  // Map -1 to SSL_TLSEXT_ERR_READ_AGAIN, if present
  switch (retval) {
  case 1:
    retval = SSL_TLSEXT_ERR_OK;
    break;
  case -1:
#ifdef SSL_TLSEXT_ERR_READ_AGAIN
    retval = SSL_TLSEXT_ERR_READ_AGAIN;
#else
    Error("Cannot pause SNI processsing with this version of openssl");
    retval = SSL_TLSEXT_ERR_ALERT_FATAL;
#endif
    break;
  case 0:
  default:
    retval = SSL_TLSEXT_ERR_ALERT_FATAL;
    break;
  }
  return retval;
}
#endif

#if TS_USE_GET_DH_2048_256 == 0
/* Build 2048-bit MODP Group with 256-bit Prime Order Subgroup from RFC 5114 */
static DH *
DH_get_2048_256()
{
  static const unsigned char dh2048_p[] = {
    0x87, 0xA8, 0xE6, 0x1D, 0xB4, 0xB6, 0x66, 0x3C, 0xFF, 0xBB, 0xD1, 0x9C, 0x65, 0x19, 0x59, 0x99, 0x8C, 0xEE, 0xF6, 0x08,
    0x66, 0x0D, 0xD0, 0xF2, 0x5D, 0x2C, 0xEE, 0xD4, 0x43, 0x5E, 0x3B, 0x00, 0xE0, 0x0D, 0xF8, 0xF1, 0xD6, 0x19, 0x57, 0xD4,
    0xFA, 0xF7, 0xDF, 0x45, 0x61, 0xB2, 0xAA, 0x30, 0x16, 0xC3, 0xD9, 0x11, 0x34, 0x09, 0x6F, 0xAA, 0x3B, 0xF4, 0x29, 0x6D,
    0x83, 0x0E, 0x9A, 0x7C, 0x20, 0x9E, 0x0C, 0x64, 0x97, 0x51, 0x7A, 0xBD, 0x5A, 0x8A, 0x9D, 0x30, 0x6B, 0xCF, 0x67, 0xED,
    0x91, 0xF9, 0xE6, 0x72, 0x5B, 0x47, 0x58, 0xC0, 0x22, 0xE0, 0xB1, 0xEF, 0x42, 0x75, 0xBF, 0x7B, 0x6C, 0x5B, 0xFC, 0x11,
    0xD4, 0x5F, 0x90, 0x88, 0xB9, 0x41, 0xF5, 0x4E, 0xB1, 0xE5, 0x9B, 0xB8, 0xBC, 0x39, 0xA0, 0xBF, 0x12, 0x30, 0x7F, 0x5C,
    0x4F, 0xDB, 0x70, 0xC5, 0x81, 0xB2, 0x3F, 0x76, 0xB6, 0x3A, 0xCA, 0xE1, 0xCA, 0xA6, 0xB7, 0x90, 0x2D, 0x52, 0x52, 0x67,
    0x35, 0x48, 0x8A, 0x0E, 0xF1, 0x3C, 0x6D, 0x9A, 0x51, 0xBF, 0xA4, 0xAB, 0x3A, 0xD8, 0x34, 0x77, 0x96, 0x52, 0x4D, 0x8E,
    0xF6, 0xA1, 0x67, 0xB5, 0xA4, 0x18, 0x25, 0xD9, 0x67, 0xE1, 0x44, 0xE5, 0x14, 0x05, 0x64, 0x25, 0x1C, 0xCA, 0xCB, 0x83,
    0xE6, 0xB4, 0x86, 0xF6, 0xB3, 0xCA, 0x3F, 0x79, 0x71, 0x50, 0x60, 0x26, 0xC0, 0xB8, 0x57, 0xF6, 0x89, 0x96, 0x28, 0x56,
    0xDE, 0xD4, 0x01, 0x0A, 0xBD, 0x0B, 0xE6, 0x21, 0xC3, 0xA3, 0x96, 0x0A, 0x54, 0xE7, 0x10, 0xC3, 0x75, 0xF2, 0x63, 0x75,
    0xD7, 0x01, 0x41, 0x03, 0xA4, 0xB5, 0x43, 0x30, 0xC1, 0x98, 0xAF, 0x12, 0x61, 0x16, 0xD2, 0x27, 0x6E, 0x11, 0x71, 0x5F,
    0x69, 0x38, 0x77, 0xFA, 0xD7, 0xEF, 0x09, 0xCA, 0xDB, 0x09, 0x4A, 0xE9, 0x1E, 0x1A, 0x15, 0x97};
  static const unsigned char dh2048_g[] = {
    0x3F, 0xB3, 0x2C, 0x9B, 0x73, 0x13, 0x4D, 0x0B, 0x2E, 0x77, 0x50, 0x66, 0x60, 0xED, 0xBD, 0x48, 0x4C, 0xA7, 0xB1, 0x8F,
    0x21, 0xEF, 0x20, 0x54, 0x07, 0xF4, 0x79, 0x3A, 0x1A, 0x0B, 0xA1, 0x25, 0x10, 0xDB, 0xC1, 0x50, 0x77, 0xBE, 0x46, 0x3F,
    0xFF, 0x4F, 0xED, 0x4A, 0xAC, 0x0B, 0xB5, 0x55, 0xBE, 0x3A, 0x6C, 0x1B, 0x0C, 0x6B, 0x47, 0xB1, 0xBC, 0x37, 0x73, 0xBF,
    0x7E, 0x8C, 0x6F, 0x62, 0x90, 0x12, 0x28, 0xF8, 0xC2, 0x8C, 0xBB, 0x18, 0xA5, 0x5A, 0xE3, 0x13, 0x41, 0x00, 0x0A, 0x65,
    0x01, 0x96, 0xF9, 0x31, 0xC7, 0x7A, 0x57, 0xF2, 0xDD, 0xF4, 0x63, 0xE5, 0xE9, 0xEC, 0x14, 0x4B, 0x77, 0x7D, 0xE6, 0x2A,
    0xAA, 0xB8, 0xA8, 0x62, 0x8A, 0xC3, 0x76, 0xD2, 0x82, 0xD6, 0xED, 0x38, 0x64, 0xE6, 0x79, 0x82, 0x42, 0x8E, 0xBC, 0x83,
    0x1D, 0x14, 0x34, 0x8F, 0x6F, 0x2F, 0x91, 0x93, 0xB5, 0x04, 0x5A, 0xF2, 0x76, 0x71, 0x64, 0xE1, 0xDF, 0xC9, 0x67, 0xC1,
    0xFB, 0x3F, 0x2E, 0x55, 0xA4, 0xBD, 0x1B, 0xFF, 0xE8, 0x3B, 0x9C, 0x80, 0xD0, 0x52, 0xB9, 0x85, 0xD1, 0x82, 0xEA, 0x0A,
    0xDB, 0x2A, 0x3B, 0x73, 0x13, 0xD3, 0xFE, 0x14, 0xC8, 0x48, 0x4B, 0x1E, 0x05, 0x25, 0x88, 0xB9, 0xB7, 0xD2, 0xBB, 0xD2,
    0xDF, 0x01, 0x61, 0x99, 0xEC, 0xD0, 0x6E, 0x15, 0x57, 0xCD, 0x09, 0x15, 0xB3, 0x35, 0x3B, 0xBB, 0x64, 0xE0, 0xEC, 0x37,
    0x7F, 0xD0, 0x28, 0x37, 0x0D, 0xF9, 0x2B, 0x52, 0xC7, 0x89, 0x14, 0x28, 0xCD, 0xC6, 0x7E, 0xB6, 0x18, 0x4B, 0x52, 0x3D,
    0x1D, 0xB2, 0x46, 0xC3, 0x2F, 0x63, 0x07, 0x84, 0x90, 0xF0, 0x0E, 0xF8, 0xD6, 0x47, 0xD1, 0x48, 0xD4, 0x79, 0x54, 0x51,
    0x5E, 0x23, 0x27, 0xCF, 0xEF, 0x98, 0xC5, 0x82, 0x66, 0x4B, 0x4C, 0x0F, 0x6C, 0xC4, 0x16, 0x59};
  DH *dh;

  if ((dh = DH_new()) == nullptr)
    return (nullptr);
  dh->p = BN_bin2bn(dh2048_p, sizeof(dh2048_p), nullptr);
  dh->g = BN_bin2bn(dh2048_g, sizeof(dh2048_g), nullptr);
  if ((dh->p == nullptr) || (dh->g == nullptr)) {
    DH_free(dh);
    return (nullptr);
  }
  return (dh);
}
#endif

static SSL_CTX *
ssl_context_enable_dhe(const char *dhparams_file, SSL_CTX *ctx)
{
  DH *server_dh;

  if (dhparams_file) {
    scoped_BIO bio(BIO_new_file(dhparams_file, "r"));
    server_dh = PEM_read_bio_DHparams(bio.get(), nullptr, nullptr, nullptr);
  } else {
    server_dh = DH_get_2048_256();
  }

  if (!server_dh) {
    Error("SSL dhparams source returned invalid parameters");
    return nullptr;
  }

  if (!SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE) || !SSL_CTX_set_tmp_dh(ctx, server_dh)) {
    DH_free(server_dh);
    Error("failed to configure SSL DH");
    return nullptr;
  }

  DH_free(server_dh);

  return ctx;
}

// SSL_CTX_set_ecdh_auto() is removed by OpenSSL v1.1.0 and ECDH is enabled in default.
// TODO: remove this function when we drop support of OpenSSL v1.0.2* and lower.
static SSL_CTX *
ssl_context_enable_ecdh(SSL_CTX *ctx)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000

#if TS_USE_TLS_ECKEY

#if defined(SSL_CTRL_SET_ECDH_AUTO)
  SSL_CTX_set_ecdh_auto(ctx, 1);
#elif defined(HAVE_EC_KEY_NEW_BY_CURVE_NAME) && defined(NID_X9_62_prime256v1)
  EC_KEY *ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);

  if (ecdh) {
    SSL_CTX_set_tmp_ecdh(ctx, ecdh);
    EC_KEY_free(ecdh);
  }
#endif
#endif
#endif

  return ctx;
}

static ssl_ticket_key_block *
ssl_context_enable_tickets(SSL_CTX *ctx, const char *ticket_key_path)
{
#if HAVE_OPENSSL_SESSION_TICKETS
  ssl_ticket_key_block *keyblock = nullptr;

  keyblock = ssl_create_ticket_keyblock(ticket_key_path);

  // Increase the stats.
  if (ssl_rsb != nullptr) { // ssl_rsb is not initialized during the first run.
    SSL_INCREMENT_DYN_STAT(ssl_total_ticket_keys_renewed_stat);
  }

  // Setting the callback can only fail if OpenSSL does not recognize the
  // SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB constant. we set the callback first
  // so that we don't leave a ticket_key pointer attached if it fails.
  if (SSL_CTX_set_tlsext_ticket_key_cb(ctx, ssl_callback_session_ticket) == 0) {
    Error("failed to set session ticket callback");
    ticket_block_free(keyblock);
    return nullptr;
  }

  SSL_CTX_clear_options(ctx, SSL_OP_NO_TICKET);
  return keyblock;

#else  /* !HAVE_OPENSSL_SESSION_TICKETS */
  (void)ticket_key_path;
  return nullptr;
#endif /* HAVE_OPENSSL_SESSION_TICKETS */
}

struct passphrase_cb_userdata {
  const SSLConfigParams *_configParams;
  const char *_serverDialog;
  const char *_serverCert;
  const char *_serverKey;

  passphrase_cb_userdata(const SSLConfigParams *params, const char *dialog, const char *cert, const char *key)
    : _configParams(params), _serverDialog(dialog), _serverCert(cert), _serverKey(key)
  {
  }
};

// RAII implementation for struct termios
struct ssl_termios : public termios {
  ssl_termios(int fd)
  {
    _fd = -1;
    // populate base class data
    if (tcgetattr(fd, this) == 0) { // success
      _fd = fd;
    }
    // save our copy
    _initialAttr = *this;
  }

  ~ssl_termios()
  {
    if (_fd != -1) {
      tcsetattr(_fd, 0, &_initialAttr);
    }
  }

  bool
  ok() const
  {
    return (_fd != -1);
  }

private:
  int _fd;
  struct termios _initialAttr;
};

static int
ssl_getpassword(const char *prompt, char *buffer, int size)
{
  fprintf(stdout, "%s", prompt);

  // disable echo and line buffering
  ssl_termios tty_attr(STDIN_FILENO);

  if (!tty_attr.ok()) {
    return -1;
  }

  tty_attr.c_lflag &= ~ICANON; // no buffer, no backspace
  tty_attr.c_lflag &= ~ECHO;   // no echo
  tty_attr.c_lflag &= ~ISIG;   // no signal for ctrl-c

  if (tcsetattr(STDIN_FILENO, 0, &tty_attr) < 0) {
    return -1;
  }

  int i  = 0;
  int ch = 0;

  *buffer = 0;
  while ((ch = getchar()) != '\n' && ch != EOF) {
    // make sure room in buffer
    if (i >= size - 1) {
      return -1;
    }

    buffer[i]   = ch;
    buffer[++i] = 0;
  }

  return i;
}

static int
ssl_private_key_passphrase_callback_exec(char *buf, int size, int rwflag, void *userdata)
{
  if (0 == size) {
    return 0;
  }

  *buf                       = 0;
  passphrase_cb_userdata *ud = static_cast<passphrase_cb_userdata *>(userdata);

  Debug("ssl", "ssl_private_key_passphrase_callback_exec rwflag=%d serverDialog=%s", rwflag, ud->_serverDialog);

  // only respond to reading private keys, not writing them (does ats even do that?)
  if (0 == rwflag) {
    // execute the dialog program and use the first line output as the passphrase
    FILE *f = popen(ud->_serverDialog, "r");
    if (f) {
      if (fgets(buf, size, f)) {
        // remove any ending CR or LF
        for (char *pass = buf; *pass; pass++) {
          if (*pass == '\n' || *pass == '\r') {
            *pass = 0;
            break;
          }
        }
      }
      pclose(f);
    } else { // popen failed
      Error("could not open dialog '%s' - %s", ud->_serverDialog, strerror(errno));
    }
  }
  return strlen(buf);
}

static int
ssl_private_key_passphrase_callback_builtin(char *buf, int size, int rwflag, void *userdata)
{
  if (0 == size) {
    return 0;
  }

  *buf                       = 0;
  passphrase_cb_userdata *ud = static_cast<passphrase_cb_userdata *>(userdata);

  Debug("ssl", "ssl_private_key_passphrase_callback rwflag=%d serverDialog=%s", rwflag, ud->_serverDialog);

  // only respond to reading private keys, not writing them (does ats even do that?)
  if (0 == rwflag) {
    // output request
    fprintf(stdout, "Some of your private key files are encrypted for security reasons.\n");
    fprintf(stdout, "In order to read them you have to provide the pass phrases.\n");
    fprintf(stdout, "ssl_cert_name=%s", ud->_serverCert);
    if (ud->_serverKey) { // output ssl_key_name if provided
      fprintf(stdout, " ssl_key_name=%s", ud->_serverKey);
    }
    fprintf(stdout, "\n");
    // get passphrase
    // if error, then no passphrase
    if (ssl_getpassword("Enter passphrase:", buf, size) <= 0) {
      *buf = 0;
    }
    fprintf(stdout, "\n");
  }
  return strlen(buf);
}

static bool
ssl_private_key_validate_exec(const char *cmdLine)
{
  if (nullptr == cmdLine) {
    errno = EINVAL;
    return false;
  }

  bool bReturn      = false;
  char *cmdLineCopy = ats_strdup(cmdLine);
  char *ptr         = cmdLineCopy;

  while (*ptr && !isspace(*ptr)) {
    ++ptr;
  }
  *ptr = 0;
  if (access(cmdLineCopy, X_OK) != -1) {
    bReturn = true;
  }
  ats_free(cmdLineCopy);
  return bReturn;
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
#define ssl_malloc(size, file, line) ssl_malloc(size)
#define ssl_realloc(ptr, size, file, line) ssl_realloc(ptr, size)
#define ssl_free(ptr, file, line) ssl_free(ptr)
#define ssl_track_malloc(size, file, line) ssl_track_malloc(size)
#define ssl_track_realloc(ptr, size, file, line) ssl_track_realloc(ptr, size)
#define ssl_track_free(ptr, file, line) ssl_track_free(ptr)
#endif

void *
ssl_malloc(size_t size, const char * /*filename */, int /*lineno*/)
{
  return ats_malloc(size);
}

void *
ssl_realloc(void *ptr, size_t size, const char * /*filename*/, int /*lineno*/)
{
  return ats_realloc(ptr, size);
}

void
ssl_free(void *ptr, const char * /*filename*/, int /*lineno*/)
{
  ats_free(ptr);
}

void *
ssl_track_malloc(size_t size, const char * /*filename*/, int /*lineno*/)
{
  return ats_track_malloc(size, &ssl_memory_allocated);
}

void *
ssl_track_realloc(void *ptr, size_t size, const char * /*filename*/, int /*lineno*/)
{
  return ats_track_realloc(ptr, size, &ssl_memory_allocated, &ssl_memory_freed);
}

void
ssl_track_free(void *ptr, const char * /*filename*/, int /*lineno*/)
{
  ats_track_free(ptr, &ssl_memory_freed);
}

/*
 * Some items are only initialized if certain config values are set
 * Must have a second pass that initializes after loading the SSL config
 */
void
SSLPostConfigInitialize()
{
  if (SSLConfigParams::engine_conf_file) {
#ifndef OPENSSL_IS_BORINGSSL
    ENGINE_load_dynamic();
#endif

    OPENSSL_load_builtin_modules();
    if (CONF_modules_load_file(SSLConfigParams::engine_conf_file, nullptr, 0) <= 0) {
      Error("FATAL: error loading engine configuration file %s", SSLConfigParams::engine_conf_file);
      // ERR_print_errors_fp(stderr);
    }
  }
}

void
SSLInitializeLibrary()
{
  if (!open_ssl_initialized) {
// BoringSSL does not have the memory functions
#ifdef HAVE_CRYPTO_SET_MEM_FUNCTIONS
    if (res_track_memory >= 2) {
      CRYPTO_set_mem_functions(ssl_track_malloc, ssl_track_realloc, ssl_track_free);
    } else {
      CRYPTO_set_mem_functions(ssl_malloc, ssl_realloc, ssl_free);
    }
#endif

    SSL_load_error_strings();
    SSL_library_init();

#ifdef OPENSSL_FIPS
    // calling FIPS_mode_set() will force FIPS to POST (Power On Self Test)
    // After POST we don't have to lock for FIPS
    int mode = FIPS_mode();
    FIPS_mode_set(mode);
    Debug("ssl", "FIPS_mode: %d", mode);
#endif

    mutex_buf = (ink_mutex *)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(ink_mutex));

    for (int i = 0; i < CRYPTO_num_locks(); i++) {
      ink_mutex_init(&mutex_buf[i]);
    }

    CRYPTO_set_locking_callback(SSL_locking_callback);
#if !defined(CRYPTO_THREADID_set_callback)
    CRYPTO_THREADID_set_callback(SSL_pthreads_thread_id);
#endif
    CRYPTO_set_dynlock_create_callback(ssl_dyn_create_callback);
    CRYPTO_set_dynlock_lock_callback(ssl_dyn_lock_callback);
    CRYPTO_set_dynlock_destroy_callback(ssl_dyn_destroy_callback);
  }

#ifdef SSL_CTX_set_tlsext_ticket_key_cb
  ssl_session_ticket_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, session_ticket_free);
  if (ssl_session_ticket_index == -1) {
    SSLError("failed to create session ticket index");
  }
#endif

#ifdef TS_USE_TLS_OCSP
  ssl_stapling_ex_init();
#endif /* TS_USE_TLS_OCSP */

  // Reserve an application data index so that we can attach
  // the SSLNetVConnection to the SSL session.
  ssl_vc_index = SSL_get_ex_new_index(0, (void *)"NetVC index", nullptr, nullptr, nullptr);

  open_ssl_initialized = true;
}

SSL_CTX *
SSLDefaultServerContext()
{
  return SSL_CTX_new(SSLv23_server_method());
}

static bool
SSLPrivateKeyHandler(SSL_CTX *ctx, const SSLConfigParams *params, const std::string &completeServerCertPath, const char *keyPath)
{
  ENGINE *e = ENGINE_get_default_RSA();
  if (e != nullptr) {
    const char *argkey = (keyPath == nullptr || keyPath[0] == '\0') ? completeServerCertPath.c_str() : keyPath;
    if (!SSL_CTX_use_PrivateKey(ctx, ENGINE_load_private_key(e, argkey, nullptr, nullptr))) {
      SSLError("failed to load server private key from engine");
    }
  } else if (!keyPath) {
    // assume private key is contained in cert obtained from multicert file.
    if (!SSL_CTX_use_PrivateKey_file(ctx, completeServerCertPath.c_str(), SSL_FILETYPE_PEM)) {
      SSLError("failed to load server private key from %s", completeServerCertPath.c_str());
      return false;
    }
  } else if (params->serverKeyPathOnly != nullptr) {
    ats_scoped_str completeServerKeyPath(Layout::get()->relative_to(params->serverKeyPathOnly, keyPath));
    if (!SSL_CTX_use_PrivateKey_file(ctx, completeServerKeyPath, SSL_FILETYPE_PEM)) {
      SSLError("failed to load server private key from %s", (const char *)completeServerKeyPath);
      return false;
    }
    if (SSLConfigParams::load_ssl_file_cb) {
      SSLConfigParams::load_ssl_file_cb(completeServerKeyPath, CONFIG_FLAG_UNVERSIONED);
    }
  } else {
    SSLError("empty SSL private key path in records.config");
    return false;
  }

  if (e == nullptr && !SSL_CTX_check_private_key(ctx)) {
    SSLError("server private key does not match the certificate public key");
    return false;
  }

  return true;
}

static int
SSLCheckServerCertNow(X509 *cert, const char *certname)
{
  int timeCmpValue;

  // SSLCheckServerCertNow() -  returns 0 on OK or negative value on failure
  // and update log as appropriate.
  // Will check:
  // - if file exists, and has read permissions
  // - for truncation or other PEM read fail
  // - current time is between notBefore and notAfter dates of certificate
  // if anything is not kosher, a negative value is returned and appropriate error logged.

  if (!cert) {
    // a truncated certificate would fall into here
    Error("invalid certificate %s: file is truncated or corrupted", certname);
    return -3;
  }

  // XXX we should log the notBefore and notAfter dates in the errors ...

  timeCmpValue = X509_cmp_current_time(X509_get_notBefore(cert));
  if (timeCmpValue == 0) {
    // an error occured parsing the time, which we'll call a bogosity
    Error("invalid certificate %s: unable to parse notBefore time", certname);
    return -3;
  } else if (timeCmpValue > 0) {
    // cert contains a date before the notBefore
    Error("invalid certificate %s: notBefore date is in the future", certname);
    return -4;
  }

  timeCmpValue = X509_cmp_current_time(X509_get_notAfter(cert));
  if (timeCmpValue == 0) {
    // an error occured parsing the time, which we'll call a bogosity
    Error("invalid certificate %s: unable to parse notAfter time", certname);
    return -3;
  } else if (timeCmpValue < 0) {
    // cert is expired
    Error("invalid certificate %s: certificate expired", certname);
    return -5;
  }

  Debug("ssl", "server certificate %s passed accessibility and date checks", certname);
  return 0; // all good

} /* CheckServerCertNow() */

static char *
asn1_strdup(ASN1_STRING *s)
{
  // Make sure we have an 8-bit encoding.
  ink_assert(ASN1_STRING_type(s) == V_ASN1_IA5STRING || ASN1_STRING_type(s) == V_ASN1_UTF8STRING ||
             ASN1_STRING_type(s) == V_ASN1_PRINTABLESTRING || ASN1_STRING_type(s) == V_ASN1_T61STRING);

  return ats_strndup((const char *)ASN1_STRING_get0_data(s), ASN1_STRING_length(s));
}

// Given a certificate and it's corresponding SSL_CTX context, insert hash
// table aliases for subject CN and subjectAltNames DNS without wildcard,
// insert trie aliases for those with wildcard.
static bool
ssl_index_certificate(SSLCertLookup *lookup, SSLCertContext const &cc, X509 *cert, const char *certname)
{
  X509_NAME *subject = nullptr;
  bool inserted      = false;

  if (nullptr == cert) {
    Error("Failed to load certificate %s", certname);
    lookup->is_valid = false;
    return false;
  }

  // Insert a key for the subject CN.
  subject = X509_get_subject_name(cert);
  ats_scoped_str subj_name;
  if (subject) {
    int pos = -1;
    for (;;) {
      pos = X509_NAME_get_index_by_NID(subject, NID_commonName, pos);
      if (pos == -1) {
        break;
      }

      X509_NAME_ENTRY *e = X509_NAME_get_entry(subject, pos);
      ASN1_STRING *cn    = X509_NAME_ENTRY_get_data(e);
      subj_name          = asn1_strdup(cn);

      Debug("ssl", "mapping '%s' to certificate %s", (const char *)subj_name, certname);
      if (lookup->insert(subj_name, cc) >= 0) {
        inserted = true;
      }
    }
  }

#if HAVE_OPENSSL_TS_H
  // Traverse the subjectAltNames (if any) and insert additional keys for the SSL context.
  GENERAL_NAMES *names = (GENERAL_NAMES *)X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr);
  if (names) {
    unsigned count = sk_GENERAL_NAME_num(names);
    for (unsigned i = 0; i < count; ++i) {
      GENERAL_NAME *name;

      name = sk_GENERAL_NAME_value(names, i);
      if (name->type == GEN_DNS) {
        ats_scoped_str dns(asn1_strdup(name->d.dNSName));
        // only try to insert if the alternate name is not the main name
        if (subj_name == nullptr || strcmp(dns, subj_name) != 0) {
          Debug("ssl", "mapping '%s' to certificates %s", (const char *)dns, certname);
          if (lookup->insert(dns, cc) >= 0) {
            inserted = true;
          }
        }
      }
    }

    GENERAL_NAMES_free(names);
  }
#endif // HAVE_OPENSSL_TS_H
  return inserted;
}

// This callback function is executed while OpenSSL processes the SSL
// handshake and does SSL record layer stuff.  It's used to trap
// client-initiated renegotiations and update cipher stats
static void
ssl_callback_info(const SSL *ssl, int where, int ret)
{
  Debug("ssl", "ssl_callback_info ssl: %p where: %d ret: %d State: %s", ssl, where, ret, SSL_state_string_long(ssl));
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);

  if ((where & SSL_CB_ACCEPT_LOOP) && netvc->getSSLHandShakeComplete() == true &&
      SSLConfigParams::ssl_allow_client_renegotiation == false) {
    int state = SSL_get_state(ssl);

// TODO: ifdef can be removed in the future
// Support for SSL23 only if we have it
#ifdef SSL23_ST_SR_CLNT_HELLO_A
    if (state == SSL3_ST_SR_CLNT_HELLO_A || state == SSL23_ST_SR_CLNT_HELLO_A) {
#else
#ifdef SSL3_ST_SR_CLNT_HELLO_A
    if (state == SSL3_ST_SR_CLNT_HELLO_A) {
#else
#ifdef SSL_ST_RENEGOTIATE
    // This is for BoringSSL
    if (state == SSL_ST_RENEGOTIATE) {
#else
    if (state == TLS_ST_SR_CLNT_HELLO) {
#endif
#endif
#endif
      netvc->setSSLClientRenegotiationAbort(true);
      Debug("ssl", "ssl_callback_info trying to renegotiate from the client");
    }
  }
  if (where & SSL_CB_HANDSHAKE_DONE) {
    // handshake is complete
    const SSL_CIPHER *cipher = SSL_get_current_cipher(ssl);
    if (cipher) {
      const char *cipherName = SSL_CIPHER_get_name(cipher);
      // lookup index of stat by name and incr count
      if (auto it = cipher_map.find(cipherName); it != cipher_map.end()) {
        SSL_INCREMENT_DYN_STAT((intptr_t)it->second);
      }
    }
  }
}

static void
ssl_set_handshake_callbacks(SSL_CTX *ctx)
{
// Make sure the callbacks are set
#if TS_USE_CERT_CB
  SSL_CTX_set_cert_cb(ctx, ssl_cert_callback, nullptr);
  SSL_CTX_set_tlsext_servername_callback(ctx, ssl_servername_only_callback);
#else
  SSL_CTX_set_tlsext_servername_callback(ctx, ssl_servername_and_cert_callback);
#endif
#if TS_USE_HELLO_CB
  SSL_CTX_set_client_hello_cb(ctx, ssl_client_hello_callback, nullptr);
#endif
}

void
setTLSValidProtocols(SSL *ssl, unsigned long proto_mask, unsigned long max_mask)
{
  SSL_set_options(ssl, proto_mask);
  SSL_clear_options(ssl, max_mask & ~proto_mask);
}

void
setClientCertLevel(SSL *ssl, uint8_t certLevel)
{
  SSLConfig::scoped_config params;
  int server_verify_client = SSL_VERIFY_NONE;

  if (certLevel == 2) {
    server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE;
  } else if (certLevel == 1) {
    server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE;
  } else if (certLevel == 0) {
    server_verify_client = SSL_VERIFY_NONE;
  } else {
    ink_release_assert(!"Invalid client verify level");
  }

  Debug("ssl", "setting cert level to %d", server_verify_client);
  SSL_set_verify(ssl, server_verify_client, nullptr);
  SSL_set_verify_depth(ssl, params->verify_depth); // might want to make configurable at some point.
}

SSL_CTX *
SSLInitServerContext(const SSLConfigParams *params, const ssl_user_config *sslMultCertSettings, std::vector<X509 *> &cert_list)
{
  int server_verify_client;
  SSL_CTX *ctx                 = SSLDefaultServerContext();
  EVP_MD_CTX *digest           = EVP_MD_CTX_new();
  STACK_OF(X509_NAME) *ca_list = nullptr;
  unsigned char hash_buf[EVP_MAX_MD_SIZE];
  unsigned int hash_len    = 0;
  const char *setting_cert = sslMultCertSettings ? sslMultCertSettings->cert.get() : nullptr;

  // disable selected protocols
  SSL_CTX_set_options(ctx, params->ssl_ctx_options);

  Debug("ssl.session_cache",
        "ssl context=%p: using session cache options, enabled=%d, size=%d, num_buckets=%d, "
        "skip_on_contention=%d, timeout=%d, auto_clear=%d",
        ctx, params->ssl_session_cache, params->ssl_session_cache_size, params->ssl_session_cache_num_buckets,
        params->ssl_session_cache_skip_on_contention, params->ssl_session_cache_timeout, params->ssl_session_cache_auto_clear);

  if (params->ssl_session_cache_timeout) {
    SSL_CTX_set_timeout(ctx, params->ssl_session_cache_timeout);
  }

  int additional_cache_flags = 0;
  additional_cache_flags |= (params->ssl_session_cache_auto_clear == 0) ? SSL_SESS_CACHE_NO_AUTO_CLEAR : 0;

  switch (params->ssl_session_cache) {
  case SSLConfigParams::SSL_SESSION_CACHE_MODE_OFF:
    Debug("ssl.session_cache", "disabling SSL session cache");

    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF | SSL_SESS_CACHE_NO_INTERNAL);
    break;
  case SSLConfigParams::SSL_SESSION_CACHE_MODE_SERVER_OPENSSL_IMPL:
    Debug("ssl.session_cache", "enabling SSL session cache with OpenSSL implementation");

    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER | additional_cache_flags);
    SSL_CTX_sess_set_cache_size(ctx, params->ssl_session_cache_size);
    break;
  case SSLConfigParams::SSL_SESSION_CACHE_MODE_SERVER_ATS_IMPL: {
    Debug("ssl.session_cache", "enabling SSL session cache with ATS implementation");
    /* Add all the OpenSSL callbacks */
    SSL_CTX_sess_set_new_cb(ctx, ssl_new_cached_session);
    SSL_CTX_sess_set_remove_cb(ctx, ssl_rm_cached_session);
    SSL_CTX_sess_set_get_cb(ctx, ssl_get_cached_session);

    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER | SSL_SESS_CACHE_NO_INTERNAL | additional_cache_flags);

    break;
  }
  }

#ifdef SSL_MODE_RELEASE_BUFFERS
  if (OPENSSL_VERSION_NUMBER > 0x1000107fL) {
    Debug("ssl", "enabling SSL_MODE_RELEASE_BUFFERS");
    SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS);
  }
#endif

#ifdef SSL_OP_SAFARI_ECDHE_ECDSA_BUG
  SSL_CTX_set_options(ctx, SSL_OP_SAFARI_ECDHE_ECDSA_BUG);
#endif

  if (sslMultCertSettings) {
    if (sslMultCertSettings->dialog) {
      passphrase_cb_userdata ud(params, sslMultCertSettings->dialog, sslMultCertSettings->first_cert, sslMultCertSettings->key);
      // pass phrase dialog configuration
      pem_password_cb *passwd_cb = nullptr;
      if (strncmp(sslMultCertSettings->dialog, "exec:", 5) == 0) {
        ud._serverDialog = &sslMultCertSettings->dialog[5];
        // validate the exec program
        if (!ssl_private_key_validate_exec(ud._serverDialog)) {
          SSLError("failed to access '%s' pass phrase program: %s", (const char *)ud._serverDialog, strerror(errno));
          memset(static_cast<void *>(&ud), 0, sizeof(ud));
          goto fail;
        }
        passwd_cb = ssl_private_key_passphrase_callback_exec;
      } else if (strcmp(sslMultCertSettings->dialog, "builtin") == 0) {
        passwd_cb = ssl_private_key_passphrase_callback_builtin;
      } else { // unknown config
        SSLError("unknown " SSL_KEY_DIALOG " configuration value '%s'", (const char *)sslMultCertSettings->dialog);
        memset(static_cast<void *>(&ud), 0, sizeof(ud));
        goto fail;
      }
      SSL_CTX_set_default_passwd_cb(ctx, passwd_cb);
      SSL_CTX_set_default_passwd_cb_userdata(ctx, &ud);
      // Clear any password info lingering in the UD data structure
      memset(static_cast<void *>(&ud), 0, sizeof(ud));
    }

    if (sslMultCertSettings->cert) {
      SimpleTokenizer cert_tok((const char *)sslMultCertSettings->cert, SSL_CERT_SEPARATE_DELIM);
      SimpleTokenizer key_tok((sslMultCertSettings->key ? (const char *)sslMultCertSettings->key : ""), SSL_CERT_SEPARATE_DELIM);

      if (sslMultCertSettings->key && cert_tok.getNumTokensRemaining() != key_tok.getNumTokensRemaining()) {
        Error("the number of certificates in ssl_cert_name and ssl_key_name doesn't match");
        goto fail;
      }
      SimpleTokenizer ca_tok("", SSL_CERT_SEPARATE_DELIM);
      if (sslMultCertSettings->ca) {
        ca_tok.setString(sslMultCertSettings->ca);
        if (cert_tok.getNumTokensRemaining() != ca_tok.getNumTokensRemaining()) {
          Error("the number of certificates in ssl_cert_name and ssl_ca_name doesn't match");
          goto fail;
        }
      }

      for (const char *certname = cert_tok.getNext(); certname; certname = cert_tok.getNext()) {
        std::string completeServerCertPath = Layout::relative_to(params->serverCertPathOnly, certname);
        scoped_BIO bio(BIO_new_file(completeServerCertPath.c_str(), "r"));
        X509 *cert = nullptr;
        if (bio) {
          cert = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
        }
        if (!bio || !cert) {
          SSLError("failed to load certificate chain from %s", completeServerCertPath.c_str());
          goto fail;
        }
        if (!SSL_CTX_use_certificate(ctx, cert)) {
          SSLError("Failed to assign cert from %s to SSL_CTX", completeServerCertPath.c_str());
          X509_free(cert);
          goto fail;
        }

        // Load up any additional chain certificates
        SSL_CTX_add_extra_chain_cert_bio(ctx, bio);

        const char *keyPath = key_tok.getNext();
        if (!SSLPrivateKeyHandler(ctx, params, completeServerCertPath, keyPath)) {
          goto fail;
        }

        cert_list.push_back(cert);
        if (SSLConfigParams::load_ssl_file_cb) {
          SSLConfigParams::load_ssl_file_cb(completeServerCertPath.c_str(), CONFIG_FLAG_UNVERSIONED);
        }

        // Must load all the intermediate certificates before starting the next chain

        // First, load any CA chains from the global chain file.  This should probably
        // eventually be a comma separated list too.  For now we will load it in all chains even
        // though it only makes sense in one chain
        if (params->serverCertChainFilename) {
          ats_scoped_str completeServerCertChainPath(
            Layout::relative_to(params->serverCertPathOnly, params->serverCertChainFilename));
          if (!SSL_CTX_add_extra_chain_cert_file(ctx, completeServerCertChainPath)) {
            SSLError("failed to load global certificate chain from %s", (const char *)completeServerCertChainPath);
            goto fail;
          }
          if (SSLConfigParams::load_ssl_file_cb) {
            SSLConfigParams::load_ssl_file_cb(completeServerCertChainPath, CONFIG_FLAG_UNVERSIONED);
          }
        }

        // Now, load any additional certificate chains specified in this entry.
        if (sslMultCertSettings->ca) {
          const char *ca_name = ca_tok.getNext();
          if (ca_name != nullptr) {
            ats_scoped_str completeServerCertChainPath(Layout::relative_to(params->serverCertPathOnly, ca_name));
            if (!SSL_CTX_add_extra_chain_cert_file(ctx, completeServerCertChainPath)) {
              SSLError("failed to load certificate chain from %s", (const char *)completeServerCertChainPath);
              goto fail;
            }
            if (SSLConfigParams::load_ssl_file_cb) {
              SSLConfigParams::load_ssl_file_cb(completeServerCertChainPath, CONFIG_FLAG_UNVERSIONED);
            }
          }
        }
      }
    }

    // SSL_CTX_load_verify_locations() builds the cert chain from the
    // serverCACertFilename if that is not nullptr.  Otherwise, it uses the hashed
    // symlinks in serverCACertPath.
    //
    // if ssl_ca_name is NOT configured for this cert in ssl_multicert.config
    //     AND
    // if proxy.config.ssl.CA.cert.filename and proxy.config.ssl.CA.cert.path
    //     are configured
    //   pass that file as the chain (include all certs in that file)
    // else if proxy.config.ssl.CA.cert.path is configured (and
    //       proxy.config.ssl.CA.cert.filename is nullptr)
    //   use the hashed symlinks in that directory to build the chain
    if (!sslMultCertSettings->ca && params->serverCACertPath != nullptr) {
      if ((!SSL_CTX_load_verify_locations(ctx, params->serverCACertFilename, params->serverCACertPath)) ||
          (!SSL_CTX_set_default_verify_paths(ctx))) {
        SSLError("invalid CA Certificate file or CA Certificate path");
        goto fail;
      }
    }

#if defined(SSL_OP_NO_TICKET)
    // Session tickets are enabled by default. Disable if explicitly requested.
    if (sslMultCertSettings->session_ticket_enabled == 0) {
      SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET);
      Debug("ssl", "ssl session ticket is disabled");
    }
#endif
  }

  if (params->clientCertLevel != 0) {
    if (params->serverCACertFilename != nullptr && params->serverCACertPath != nullptr) {
      if ((!SSL_CTX_load_verify_locations(ctx, params->serverCACertFilename, params->serverCACertPath)) ||
          (!SSL_CTX_set_default_verify_paths(ctx))) {
        SSLError("CA Certificate file or CA Certificate path invalid");
        goto fail;
      }
    }

    if (params->clientCertLevel == 2) {
      server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE;
    } else if (params->clientCertLevel == 1) {
      server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE;
    } else {
      // disable client cert support
      server_verify_client = SSL_VERIFY_NONE;
      Error("illegal client certification level %d in records.config", server_verify_client);
    }
    SSL_CTX_set_verify(ctx, server_verify_client, ssl_verify_client_callback);
    SSL_CTX_set_verify_depth(ctx, params->verify_depth); // might want to make configurable at some point.
  }

  // Set the list of CA's to send to client if we ask for a client
  // certificate
  if (params->serverCACertFilename) {
    ca_list = SSL_load_client_CA_file(params->serverCACertFilename);
    if (ca_list) {
      SSL_CTX_set_client_CA_list(ctx, ca_list);
    }
  }

  if (EVP_DigestInit_ex(digest, evp_md_func, nullptr) == 0) {
    SSLError("EVP_DigestInit_ex failed");
    goto fail;
  }

  if (nullptr != setting_cert) {
    Debug("ssl", "Using '%s' in hash for session id context", sslMultCertSettings->cert.get());
    if (EVP_DigestUpdate(digest, sslMultCertSettings->cert, strlen(setting_cert)) == 0) {
      SSLError("EVP_DigestUpdate failed");
      goto fail;
    }
  }

  if (ca_list != nullptr) {
    size_t num_certs = sk_X509_NAME_num(ca_list);

    for (size_t i = 0; i < num_certs; i++) {
      X509_NAME *name = sk_X509_NAME_value(ca_list, i);
      if (X509_NAME_digest(name, evp_md_func, hash_buf /* borrow our final hash buffer. */, &hash_len) == 0 ||
          EVP_DigestUpdate(digest, hash_buf, hash_len) == 0) {
        SSLError("Adding X509 name to digest failed");
        goto fail;
      }
    }
  }

  if (EVP_DigestFinal_ex(digest, hash_buf, &hash_len) == 0) {
    SSLError("EVP_DigestFinal_ex failed");
    goto fail;
  }
  EVP_MD_CTX_free(digest);
  digest = nullptr;

  if (SSL_CTX_set_session_id_context(ctx, hash_buf, hash_len) == 0) {
    SSLError("SSL_CTX_set_session_id_context failed");
    goto fail;
  }

  if (params->cipherSuite != nullptr) {
    if (!SSL_CTX_set_cipher_list(ctx, params->cipherSuite)) {
      SSLError("invalid cipher suite in records.config");
      goto fail;
    }
  }

#if TS_USE_TLS_SET_CIPHERSUITES
  if (params->server_tls13_cipher_suites != nullptr) {
    if (!SSL_CTX_set_ciphersuites(ctx, params->server_tls13_cipher_suites)) {
      SSLError("invalid tls server cipher suites in records.config");
      goto fail;
    }
  }
#endif

#ifdef SSL_CTX_set1_groups_list
  if (params->server_groups_list != nullptr) {
    if (!SSL_CTX_set1_groups_list(ctx, params->server_groups_list)) {
      SSLError("invalid groups list for server in records.config");
      goto fail;
    }
  }
#endif

  if (!ssl_context_enable_dhe(params->dhparamsFile, ctx)) {
    goto fail;
  }

  ssl_context_enable_ecdh(ctx);
#define SSL_CLEAR_PW_REFERENCES(CTX)                      \
  {                                                       \
    SSL_CTX_set_default_passwd_cb(CTX, nullptr);          \
    SSL_CTX_set_default_passwd_cb_userdata(CTX, nullptr); \
  }
  if (sslMultCertSettings && sslMultCertSettings->dialog) {
    SSL_CLEAR_PW_REFERENCES(ctx);
  }
  SSL_CTX_set_info_callback(ctx, ssl_callback_info);

#if TS_USE_TLS_NPN
  SSL_CTX_set_next_protos_advertised_cb(ctx, SSLNetVConnection::advertise_next_protocol, nullptr);
#endif /* TS_USE_TLS_NPN */

#if TS_USE_TLS_ALPN
  SSL_CTX_set_alpn_select_cb(ctx, SSLNetVConnection::select_next_protocol, nullptr);
#endif /* TS_USE_TLS_ALPN */

#ifdef TS_USE_TLS_OCSP
  if (SSLConfigParams::ssl_ocsp_enabled) {
    Debug("ssl", "SSL OCSP Stapling is enabled");
    SSL_CTX_set_tlsext_status_cb(ctx, ssl_callback_ocsp_stapling);

    for (auto cert : cert_list) {
      if (!ssl_stapling_init_cert(ctx, cert, setting_cert)) {
        Warning("failed to configure SSL_CTX for OCSP Stapling info for certificate at %s", setting_cert);
      }
    }
  } else {
    Debug("ssl", "SSL OCSP Stapling is disabled");
  }
#else
  if (SSLConfigParams::ssl_ocsp_enabled) {
    Warning("failed to enable SSL OCSP Stapling; this version of OpenSSL does not support it");
  }
#endif /* TS_USE_TLS_OCSP */

  if (SSLConfigParams::init_ssl_ctx_cb) {
    SSLConfigParams::init_ssl_ctx_cb(ctx, true);
  }
  return ctx;

fail:
  if (digest) {
    EVP_MD_CTX_free(digest);
  }
  SSL_CLEAR_PW_REFERENCES(ctx)
  SSLReleaseContext(ctx);
  for (auto cert : cert_list) {
    X509_free(cert);
  }

  return nullptr;
}

SSL_CTX *
SSLCreateServerContext(const SSLConfigParams *params)
{
  std::vector<X509 *> cert_list;
  SSL_CTX *ctx = SSLInitServerContext(params, nullptr, cert_list);
  ink_assert(cert_list.empty());
  return ctx;
}

/**
   Insert SSLCertContext (SSL_CTX ans options) into SSLCertLookup with key.
   Do NOT call SSL_CTX_set_* functions from here. SSL_CTX should be set up by SSLInitServerContext().
 */
static SSL_CTX *
ssl_store_ssl_context(const SSLConfigParams *params, SSLCertLookup *lookup, const ssl_user_config *sslMultCertSettings)
{
  std::vector<X509 *> cert_list;
  SSL_CTX *ctx                   = SSLInitServerContext(params, sslMultCertSettings, cert_list);
  ssl_ticket_key_block *keyblock = nullptr;
  bool inserted                  = false;

  if (!ctx || !sslMultCertSettings) {
    lookup->is_valid = false;
    return nullptr;
  }

  const char *certname = sslMultCertSettings->cert.get();
  for (auto cert : cert_list) {
    if (0 > SSLCheckServerCertNow(cert, certname)) {
      /* At this point, we know cert is bad, and we've already printed a
         descriptive reason as to why cert is bad to the log file */
      Debug("ssl", "Marking certificate as NOT VALID: %s", certname);
      lookup->is_valid = false;
    }
  }

  // Load the session ticket key if session tickets are not disabled
  if (sslMultCertSettings->session_ticket_enabled != 0) {
    keyblock = ssl_context_enable_tickets(ctx, nullptr);
  }

  // Index this certificate by the specified IP(v6) address. If the address is "*", make it the default context.
  if (sslMultCertSettings->addr) {
    if (strcmp(sslMultCertSettings->addr, "*") == 0) {
      if (lookup->insert(sslMultCertSettings->addr, SSLCertContext(ctx, sslMultCertSettings->opt, keyblock)) >= 0) {
        inserted            = true;
        lookup->ssl_default = ctx;
        ssl_set_handshake_callbacks(ctx);
      }
    } else {
      IpEndpoint ep;

      if (ats_ip_pton(sslMultCertSettings->addr, &ep) == 0) {
        Debug("ssl", "mapping '%s' to certificate %s", (const char *)sslMultCertSettings->addr, (const char *)certname);
        if (lookup->insert(ep, SSLCertContext(ctx, sslMultCertSettings->opt, keyblock)) >= 0) {
          inserted = true;
        }
      } else {
        Error("'%s' is not a valid IPv4 or IPv6 address", (const char *)sslMultCertSettings->addr);
        lookup->is_valid = false;
      }
    }
  }
  if (!inserted) {
#if HAVE_OPENSSL_SESSION_TICKETS
    if (keyblock != nullptr) {
      ticket_block_free(keyblock);
    }
#endif
  }

  // Insert additional mappings. Note that this maps multiple keys to the same value, so when
  // this code is updated to reconfigure the SSL certificates, it will need some sort of
  // refcounting or alternate way of avoiding double frees.
  Debug("ssl", "importing SNI names from %s", (const char *)certname);
  for (auto cert : cert_list) {
    if (ssl_index_certificate(lookup, SSLCertContext(ctx, sslMultCertSettings->opt), cert, certname)) {
      inserted = true;
    }
  }

  if (inserted) {
    if (SSLConfigParams::init_ssl_ctx_cb) {
      SSLConfigParams::init_ssl_ctx_cb(ctx, true);
    }
  }

  if (!inserted) {
    SSLReleaseContext(ctx);
    ctx = nullptr;
  }

  for (auto &i : cert_list) {
    X509_free(i);
  }

  return ctx;
}

static bool
ssl_extract_certificate(const matcher_line *line_info, ssl_user_config &sslMultCertSettings)
{
  for (int i = 0; i < MATCHER_MAX_TOKENS; ++i) {
    const char *label;
    const char *value;

    label = line_info->line[0][i];
    value = line_info->line[1][i];

    if (label == nullptr) {
      continue;
    }

    if (strcasecmp(label, SSL_IP_TAG) == 0) {
      sslMultCertSettings.addr = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_CERT_TAG) == 0) {
      sslMultCertSettings.cert = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_CA_TAG) == 0) {
      sslMultCertSettings.ca = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_PRIVATE_KEY_TAG) == 0) {
      sslMultCertSettings.key = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_SESSION_TICKET_ENABLED) == 0) {
      sslMultCertSettings.session_ticket_enabled = atoi(value);
    }

    if (strcasecmp(label, SSL_KEY_DIALOG) == 0) {
      sslMultCertSettings.dialog = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_SERVERNAME) == 0) {
      sslMultCertSettings.servername = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_ACTION_TAG) == 0) {
      if (strcasecmp(SSL_ACTION_TUNNEL_TAG, value) == 0) {
        sslMultCertSettings.opt = SSLCertContext::OPT_TUNNEL;
      } else {
        Error("Unrecognized action for " SSL_ACTION_TAG);
        return false;
      }
    }
  }
  // TS-4679:  It is ok to be missing the cert.  At least if the action is set to tunnel
  if (sslMultCertSettings.cert) {
    SimpleTokenizer cert_tok(sslMultCertSettings.cert, SSL_CERT_SEPARATE_DELIM);
    const char *first_cert = cert_tok.getNext();
    if (first_cert) {
      sslMultCertSettings.first_cert = ats_strdup(first_cert);
    }
  }

  return true;
}

bool
SSLParseCertificateConfiguration(const SSLConfigParams *params, SSLCertLookup *lookup)
{
  char *tok_state = nullptr;
  char *line      = nullptr;
  ats_scoped_str file_buf;
  unsigned line_num = 0;
  matcher_line line_info;

  const matcher_tags sslCertTags = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false};

  Note("ssl_multicert.config loading ...");

  if (params->configFilePath) {
    file_buf = readIntoBuffer(params->configFilePath, __func__, nullptr);
  }

  if (!file_buf) {
    Error("failed to read SSL certificate configuration from %s", params->configFilePath);
    return false;
  }

  // Optionally elevate/allow file access to read root-only
  // certificates. The destructor will drop privilege for us.
  uint32_t elevate_setting = 0;
  REC_ReadConfigInteger(elevate_setting, "proxy.config.ssl.cert.load_elevated");
  ElevateAccess elevate_access(elevate_setting ? ElevateAccess::FILE_PRIVILEGE : 0);

  line = tokLine(file_buf, &tok_state);
  while (line != nullptr) {
    line_num++;

    // Skip all blank spaces at beginning of line.
    while (*line && isspace(*line)) {
      line++;
    }

    if (*line != '\0' && *line != '#') {
      ssl_user_config sslMultiCertSettings;
      const char *errPtr;

      errPtr = parseConfigLine(line, &line_info, &sslCertTags);
      Debug("ssl", "currently parsing %s", line);
      if (errPtr != nullptr) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s: discarding %s entry at line %d: %s", __func__, params->configFilePath,
                         line_num, errPtr);
      } else {
        if (ssl_extract_certificate(&line_info, sslMultiCertSettings)) {
          // There must be a certificate specified unless the tunnel action is set
          if (sslMultiCertSettings.cert || sslMultiCertSettings.opt != SSLCertContext::OPT_TUNNEL) {
            ssl_store_ssl_context(params, lookup, &sslMultiCertSettings);
          } else {
            Warning("No ssl_cert_name specified and no tunnel action set");
          }
        }
      }
    }

    line = tokLine(nullptr, &tok_state);
  }

  // We *must* have a default context even if it can't possibly work. The default context is used to
  // bootstrap the SSL handshake so that we can subsequently do the SNI lookup to switch to the real
  // context.
  if (lookup->ssl_default == nullptr) {
    ssl_user_config sslMultiCertSettings;
    sslMultiCertSettings.addr = ats_strdup("*");
    if (ssl_store_ssl_context(params, lookup, &sslMultiCertSettings) == nullptr) {
      Error("failed set default context");
      return false;
    }
  }

  return true;
}

#if HAVE_OPENSSL_SESSION_TICKETS

static void
session_ticket_free(void * /*parent*/, void *ptr, CRYPTO_EX_DATA * /*ad*/, int /*idx*/, long /*argl*/, void * /*argp*/)
{
  ticket_block_free((struct ssl_ticket_key_block *)ptr);
}

/*
 * RFC 5077. Create session ticket to resume SSL session without requiring session-specific state at the TLS server.
 * Specifically, it distributes the encrypted session-state information to the client in the form of a ticket and
 * a mechanism to present the ticket back to the server.
 * */
static int
ssl_callback_session_ticket(SSL *ssl, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx,
                            int enc)
{
  SSLCertificateConfig::scoped_config lookup;
  SSLTicketKeyConfig::scoped_config params;
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);

  // Get the IP address to look up the keyblock
  IpEndpoint ip;
  int namelen        = sizeof(ip);
  SSLCertContext *cc = nullptr;
  if (0 == safe_getsockname(netvc->get_socket(), &ip.sa, &namelen)) {
    cc = lookup->find(ip);
  }
  ssl_ticket_key_block *keyblock = nullptr;
  if (cc == nullptr || cc->keyblock == nullptr) {
    // Try the default
    keyblock = params->default_global_keyblock;
  } else {
    keyblock = cc->keyblock;
  }
  ink_release_assert(keyblock != nullptr && keyblock->num_keys > 0);

  if (enc == 1) {
    const ssl_ticket_key_t &most_recent_key = keyblock->keys[0];
    memcpy(keyname, most_recent_key.key_name, sizeof(most_recent_key.key_name));
    RAND_bytes(iv, EVP_MAX_IV_LENGTH);
    EVP_EncryptInit_ex(cipher_ctx, EVP_aes_128_cbc(), nullptr, most_recent_key.aes_key, iv);
    HMAC_Init_ex(hctx, most_recent_key.hmac_secret, sizeof(most_recent_key.hmac_secret), evp_md_func, nullptr);

    Debug("ssl", "create ticket for a new session.");
    SSL_INCREMENT_DYN_STAT(ssl_total_tickets_created_stat);
    return 1;
  } else if (enc == 0) {
    for (unsigned i = 0; i < keyblock->num_keys; ++i) {
      if (memcmp(keyname, keyblock->keys[i].key_name, sizeof(keyblock->keys[i].key_name)) == 0) {
        EVP_DecryptInit_ex(cipher_ctx, EVP_aes_128_cbc(), nullptr, keyblock->keys[i].aes_key, iv);
        HMAC_Init_ex(hctx, keyblock->keys[i].hmac_secret, sizeof(keyblock->keys[i].hmac_secret), evp_md_func, nullptr);

        Debug("ssl", "verify the ticket for an existing session.");
        // Increase the total number of decrypted tickets.
        SSL_INCREMENT_DYN_STAT(ssl_total_tickets_verified_stat);

        if (i != 0) { // The number of tickets decrypted with "older" keys.
          SSL_INCREMENT_DYN_STAT(ssl_total_tickets_verified_old_key_stat);
        }

        SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
        netvc->setSSLSessionCacheHit(true);
        // When we decrypt with an "older" key, encrypt the ticket again with the most recent key.
        return (i == 0) ? 1 : 2;
      }
    }

    Debug("ssl", "keyname is not consistent.");
    SSL_INCREMENT_DYN_STAT(ssl_total_tickets_not_found_stat);
    return 0;
  }

  return -1;
}
#endif /* HAVE_OPENSSL_SESSION_TICKETS */

// Release SSL_CTX and the associated data. This works for both
// client and server contexts and gracefully accepts nullptr.
void
SSLReleaseContext(SSL_CTX *ctx)
{
  SSL_CTX_free(ctx);
}

void
SSLNetVCAttach(SSL *ssl, SSLNetVConnection *vc)
{
  SSL_set_ex_data(ssl, ssl_vc_index, vc);
}

void
SSLNetVCDetach(SSL *ssl)
{
  SSL_set_ex_data(ssl, ssl_vc_index, nullptr);
}

SSLNetVConnection *
SSLNetVCAccess(const SSL *ssl)
{
  SSLNetVConnection *netvc;
  netvc = static_cast<SSLNetVConnection *>(SSL_get_ex_data(ssl, ssl_vc_index));

  ink_assert(dynamic_cast<SSLNetVConnection *>(static_cast<NetVConnection *>(SSL_get_ex_data(ssl, ssl_vc_index))));

  return netvc;
}

ssl_error_t
SSLWriteBuffer(SSL *ssl, const void *buf, int64_t nbytes, int64_t &nwritten)
{
  nwritten = 0;

  if (unlikely(nbytes == 0)) {
    return SSL_ERROR_NONE;
  }
  ERR_clear_error();
  int ret = SSL_write(ssl, buf, (int)nbytes);
  if (ret > 0) {
    nwritten = ret;
    BIO *bio = SSL_get_wbio(ssl);
    if (bio != nullptr) {
      (void)BIO_flush(bio);
    }
    return SSL_ERROR_NONE;
  }
  int ssl_error = SSL_get_error(ssl, ret);
  if (ssl_error == SSL_ERROR_SSL && is_debug_tag_set("ssl.error.write")) {
    char buf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, buf, sizeof(buf));
    Debug("ssl.error.write", "SSL write returned %d, ssl_error=%d, ERR_get_error=%ld (%s)", ret, ssl_error, e, buf);
  }
  return ssl_error;
}

ssl_error_t
SSLReadBuffer(SSL *ssl, void *buf, int64_t nbytes, int64_t &nread)
{
  nread = 0;

  if (unlikely(nbytes == 0)) {
    return SSL_ERROR_NONE;
  }
  ERR_clear_error();
  int ret = SSL_read(ssl, buf, (int)nbytes);
  if (ret > 0) {
    nread = ret;
    return SSL_ERROR_NONE;
  }
  int ssl_error = SSL_get_error(ssl, ret);
  if (ssl_error == SSL_ERROR_SSL && is_debug_tag_set("ssl.error.read")) {
    char buf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, buf, sizeof(buf));
    Debug("ssl.error.read", "SSL read returned %d, ssl_error=%d, ERR_get_error=%ld (%s)", ret, ssl_error, e, buf);
  }

  return ssl_error;
}

ssl_error_t
SSLAccept(SSL *ssl)
{
  ERR_clear_error();
  int ret = SSL_accept(ssl);
  if (ret > 0) {
    return SSL_ERROR_NONE;
  }
  int ssl_error = SSL_get_error(ssl, ret);
  if (ssl_error == SSL_ERROR_SSL && is_debug_tag_set("ssl.error.accept")) {
    char buf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, buf, sizeof(buf));
    Debug("ssl.error.accept", "SSL accept returned %d, ssl_error=%d, ERR_get_error=%ld (%s)", ret, ssl_error, e, buf);
  }

  return ssl_error;
}

ssl_error_t
SSLConnect(SSL *ssl)
{
  ERR_clear_error();
  int ret = SSL_connect(ssl);
  if (ret > 0) {
    return SSL_ERROR_NONE;
  }
  int ssl_error = SSL_get_error(ssl, ret);
  if (ssl_error == SSL_ERROR_SSL && is_debug_tag_set("ssl.error.connect")) {
    char buf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, buf, sizeof(buf));
    Debug("ssl.error.connect", "SSL connect returned %d, ssl_error=%d, ERR_get_error=%ld (%s)", ret, ssl_error, e, buf);
  }

  return ssl_error;
}
