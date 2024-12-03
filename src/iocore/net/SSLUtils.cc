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

#include "swoc/swoc_file.h"
#include "swoc/Errata.h"
#include "swoc/bwf_std.h"

#include "P_SSLUtils.h"

#include "tscore/ink_config.h"
#include "tscore/ink_platform.h"
#include "tscore/SimpleTokenizer.h"
#include "tscore/Layout.h"
#include "tscore/ink_cap.h"
#include "tscore/ink_mutex.h"
#include "tscore/Filenames.h"
#include "records/RecHttp.h"

#include "P_Net.h"
#include "api/InkAPIInternal.h"

#include "P_OCSPStapling.h"
#include "P_SSLConfig.h"
#include "P_TLSKeyLogger.h"
#include "BoringSSLUtils.h"
#include "iocore/net/SSLMultiCertConfigLoader.h"
#include "iocore/net/ProxyProtocol.h"
#include "iocore/net/SSLAPIHooks.h"
#include "SSLSessionCache.h"
#include "SSLSessionTicket.h"
#include "SSLDynlock.h"
#include "iocore/net/SSLDiags.h"
#include "SSLStats.h"
#include "iocore/net/TLSSessionResumptionSupport.h"
#if TS_USE_QUIC == 1
#include "iocore/net/QUICSupport.h"
#endif
#include "P_SSLNetVConnection.h"

#include <string>
#include <unistd.h>
#include <termios.h>
#include <vector>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/conf.h>
#include <openssl/dh.h>
#include <openssl/ec.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#if HAVE_OPENSSL_TS_H
#include <openssl/ts.h>
#endif

#include <utility>

using namespace std::literals;

// ssl_multicert.config field names:
static constexpr std::string_view SSL_IP_TAG("dest_ip"sv);
static constexpr std::string_view SSL_CERT_TAG("ssl_cert_name"sv);
static constexpr std::string_view SSL_PRIVATE_KEY_TAG("ssl_key_name"sv);
static constexpr std::string_view SSL_OCSP_RESPONSE_TAG("ssl_ocsp_name"sv);
static constexpr std::string_view SSL_CA_TAG("ssl_ca_name"sv);
static constexpr std::string_view SSL_ACTION_TAG("action"sv);
static constexpr std::string_view SSL_ACTION_TUNNEL_TAG("tunnel"sv);
static constexpr std::string_view SSL_SESSION_TICKET_ENABLED("ssl_ticket_enabled"sv);
static constexpr std::string_view SSL_SESSION_TICKET_NUMBER("ssl_ticket_number"sv);
static constexpr std::string_view SSL_KEY_DIALOG("ssl_key_dialog"sv);
static constexpr std::string_view SSL_SERVERNAME("dest_fqdn"sv);
static constexpr char             SSL_CERT_SEPARATE_DELIM = ',';

#ifndef evp_md_func
#ifdef OPENSSL_NO_SHA256
#define evp_md_func EVP_sha1()
#else
#define evp_md_func EVP_sha256()
#endif
#endif

SSLSessionCache *session_cache; // declared extern in P_SSLConfig.h

static int ssl_vc_index = -1;

static ink_mutex *mutex_buf            = nullptr;
static bool       open_ssl_initialized = false;

static DbgCtl dbg_ctl_ssl_load{"ssl_load"};
static DbgCtl dbg_ctl_ssl_session_cache{"ssl.session_cache"};
static DbgCtl dbg_ctl_ssl_error{"ssl.error"};
static DbgCtl dbg_ctl_ssl_verify{"ssl_verify"};

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
  DbgCtl dbg_ctl{"v_ssl_lock"};
  Dbg(dbg_ctl, "file: %s line: %d type: %d", file, line, type);
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
    Dbg(dbg_ctl_ssl_load, "invalid SSL locking mode 0x%x", mode);
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
  return SSL_CTX_add_extra_chain_cert_bio(ctx, bio.get());
}

static SSL_SESSION *
#if defined(LIBRESSL_VERSION_NUMBER)
ssl_get_cached_session(SSL *ssl, unsigned char *id, int len, int *copy)
#else
ssl_get_cached_session(SSL *ssl, const unsigned char *id, int len, int *copy)
#endif
{
  TLSSessionResumptionSupport *srs = TLSSessionResumptionSupport::getInstance(ssl);

  ink_assert(srs);
  if (srs) {
    return srs->getSession(ssl, id, len, copy);
  }

  return nullptr;
}

static int
ssl_new_cached_session(SSL *ssl, SSL_SESSION *sess)
{
#ifdef TLS1_3_VERSION
  if (SSL_SESSION_get_protocol_version(sess) == TLS1_3_VERSION) {
    return 0;
  }
#endif

  unsigned int         len = 0;
  const unsigned char *id  = SSL_SESSION_get_id(sess, &len);

  SSLSessionID sid(id, len);

  if (diags()->on()) {
    static DbgCtl dbg_ctl("ssl_session_cache.insert");
    if (dbg_ctl.tag_on()) {
      char printable_buf[(len * 2) + 1];

      sid.toString(printable_buf, sizeof(printable_buf));
      DbgPrint(dbg_ctl, "ssl_new_cached_session session '%s' and context %p", printable_buf, SSL_get_SSL_CTX(ssl));
    }
  }

  Metrics::Counter::increment(ssl_rsb.session_cache_new_session);
  session_cache->insertSession(sid, sess, ssl);

  // Call hook after new session is created
  APIHook *hook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_SSL_SESSION_HOOK));
  while (hook) {
    hook->invoke(TS_EVENT_SSL_SESSION_NEW, &sid);
    hook = hook->m_link.next;
  }

  return 0;
}

static void
ssl_rm_cached_session(SSL_CTX * /* ctx ATS_UNUSED */, SSL_SESSION *sess)
{
#ifdef TLS1_3_VERSION
  if (SSL_SESSION_get_protocol_version(sess) == TLS1_3_VERSION) {
    return;
  }
#endif

  unsigned int         len = 0;
  const unsigned char *id  = SSL_SESSION_get_id(sess, &len);
  SSLSessionID         sid(id, len);

  // Call hook before session is removed
  APIHook *hook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_SSL_SESSION_HOOK));
  while (hook) {
    hook->invoke(TS_EVENT_SSL_SESSION_REMOVE, &sid);
    hook = hook->m_link.next;
  }

  if (diags()->on()) {
    static DbgCtl dbg_ctl("ssl_session_cache.remove");
    if (dbg_ctl.tag_on()) {
      char printable_buf[(len * 2) + 1];
      sid.toString(printable_buf, sizeof(printable_buf));
      DbgPrint(dbg_ctl, "ssl_rm_cached_session cached session '%s'", printable_buf);
    }
  }

  session_cache->removeSession(sid);
}

// Callback function for verifying client certificate
static int
ssl_verify_client_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
  Dbg(dbg_ctl_ssl_verify, "Callback: verify client cert");
  auto              *ssl   = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  TLSBasicSupport   *tbs   = TLSBasicSupport::getInstance(ssl);

  if (tbs == nullptr) {
    Dbg(dbg_ctl_ssl_verify, "call back on stale netvc");
    return false;
  }

  if (tbs->verify_certificate(ctx) == 1) { // hook moved the handshake state to terminal
    Warning("TS_EVENT_SSL_VERIFY_CLIENT plugin failed the client certificate check for %s.", netvc->options.sni_servername.get());
    return false;
  }

  return preverify_ok;
}

#if HAVE_SSL_CTX_SET_CLIENT_HELLO_CB
// Pausable callback
static int
ssl_client_hello_callback(SSL *s, int * /* al ATS_UNUSED */, void * /* arg ATS_UNUSED */)
{
  TLSSNISupport::ClientHello ch = {s};
#elif HAVE_SSL_CTX_SET_SELECT_CERTIFICATE_CB
static ssl_select_cert_result_t
ssl_client_hello_callback(const SSL_CLIENT_HELLO *client_hello)
{
  SSL                       *s  = client_hello->ssl;
  TLSSNISupport::ClientHello ch = {client_hello};
#endif

  TLSSNISupport *snis = TLSSNISupport::getInstance(s);
  if (snis) {
    snis->on_client_hello(ch);
    int ret = snis->perform_sni_action(*s);
    if (ret != SSL_TLSEXT_ERR_OK) {
      return CLIENT_HELLO_ERROR;
    }
  } else {
    // This error suggests either of these:
    // 1) Call back on unsupported netvc -- Don't register callback unnecessarily
    // 2) Call back on stale netvc
    Dbg(dbg_ctl_ssl_error, "ssl_client_hello_callback was called unexpectedly");
    return CLIENT_HELLO_ERROR;
  }

  TLSEventSupport *es = TLSEventSupport::getInstance(s);
  if (es) {
    bool reenabled = es->callHooks(TS_EVENT_SSL_CLIENT_HELLO);
    if (!reenabled) {
      return CLIENT_HELLO_RETRY;
    }
  } else {
    Dbg(dbg_ctl_ssl_error, "ssl_client_hello_callback call back on stale netvc");
    return CLIENT_HELLO_ERROR;
  }

  return CLIENT_HELLO_SUCCESS;
}

/**
 * Called before either the server or the client certificate is used
 * Return 1 on success, 0 on error, or -1 to pause, -2 to retry
 */
static int
ssl_cert_callback(SSL *ssl, [[maybe_unused]] void *arg)
{
  TLSCertSwitchSupport *tcss     = TLSCertSwitchSupport::getInstance(ssl);
  TLSEventSupport      *tes      = TLSEventSupport::getInstance(ssl);
  SSLNetVConnection    *sslnetvc = dynamic_cast<SSLNetVConnection *>(tcss);
  bool                  reenabled;
  int                   retval = 1;

  // If we are in tunnel mode, don't select a cert.  Pause!
  if (sslnetvc) {
    NetVConnection *netvc = reinterpret_cast<NetVConnection *>(sslnetvc);
    if (HttpProxyPort::TRANSPORT_BLIND_TUNNEL == netvc->attributes) {
#ifdef OPENSSL_IS_BORINGSSL
      return -2; // Retry
#else
      return -1; // Pause
#endif
    }
  }

  SSLCertContextType ctxType = SSLCertContextType::GENERIC;
#ifndef HAVE_NATIVE_DUAL_CERT_SUPPORT
  if (arg != nullptr) {
    const SSL_CLIENT_HELLO *client_hello         = (const SSL_CLIENT_HELLO *)arg;
    const bool              client_ecdsa_capable = BoringSSLUtils::isClientEcdsaCapable(client_hello);
    ctxType                                      = client_ecdsa_capable ? SSLCertContextType::EC : SSLCertContextType::RSA;
  }
#endif

  if (tcss) {
    if (tes) {
      // Do the common certificate lookup only once.  If we pause
      // and restart processing, do not execute the common logic again
      if (!tes->calledHooks(TS_EVENT_SSL_CERT)) {
        retval = tcss->selectCertificate(ssl, ctxType);
        if (retval != 1) {
          return retval;
        }
      }

      // Call the plugin cert code
      reenabled = tes->callHooks(TS_EVENT_SSL_CERT);
      // If it did not re-enable, return the code to
      // stop the accept processing
      if (!reenabled) {
        retval = -1; // Pause
      }
    } else {
      if (tcss->selectCertificate(ssl, ctxType) == 1) {
        retval = 1;
      } else {
        retval = 0;
      }
    }
  }

#if TS_HAS_TLS_SESSION_TICKET
  if (retval == 1) {
    // After replacing the SSL_CTX, make sure the overridden ca_cert_file is still set
    if (sslnetvc) {
      setClientCertCACerts(ssl, sslnetvc->get_ca_cert_file(), sslnetvc->get_ca_cert_dir());
    }

    // Reset the ticket callback if needed
    SSL_CTX *ctx = SSL_get_SSL_CTX(ssl);
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
    SSL_CTX_set_tlsext_ticket_key_evp_cb(ctx, ssl_callback_session_ticket);
#else
    SSL_CTX_set_tlsext_ticket_key_cb(ctx, ssl_callback_session_ticket);
#endif
  }
#endif

  // Return 1 for success, 0 for error, or -1 to pause
  return retval;
}

/*
 * Cannot stop this callback. Always reeneabled
 */
static int
ssl_servername_callback(SSL *ssl, int *al, void *arg)
{
  TLSSNISupport *snis = TLSSNISupport::getInstance(ssl);
  if (snis) {
    if (TLSEventSupport *es = TLSEventSupport::getInstance(ssl); es) {
      es->callHooks(TS_EVENT_SSL_SERVERNAME);
    }
    snis->on_servername(ssl, al, arg);
#if !TS_USE_HELLO_CB
    // Only call the SNI actions here if not already performed in the HELLO_CB
    int ret = snis->perform_sni_action(*ssl);
    if (ret != SSL_TLSEXT_ERR_OK) {
      return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
#endif
  } else {
    // This error suggests either of these:
    // 1) Call back on unsupported netvc -- Don't register callback unnecessarily
    // 2) Call back on stale netvc
    Dbg(dbg_ctl_ssl_error, "ssl_servername_callback was called unexpectedly");
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  return SSL_TLSEXT_ERR_OK;
}

// NextProtocolNegotiation TLS extension callback. The NPN extension
// allows the client to select a preferred protocol, so all we have
// to do here is tell them what out protocol set is.
int
ssl_next_protos_advertised_callback(SSL *ssl, const unsigned char **out, unsigned *outlen, void *)
{
  ALPNSupport *alpns = ALPNSupport::getInstance(ssl);

  ink_assert(alpns);
  if (alpns) {
    return alpns->advertise_next_protocol(out, outlen);
  }

  return SSL_TLSEXT_ERR_NOACK;
}

int
ssl_alpn_select_callback(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned inlen,
                         void *)
{
  ALPNSupport *alpns = ALPNSupport::getInstance(ssl);

  ink_assert(alpns);
  if (alpns) {
    return alpns->select_next_protocol(out, outlen, in, inlen);
  }

  return SSL_TLSEXT_ERR_NOACK;
}

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
  DH     *dh;
  BIGNUM *p;
  BIGNUM *g;

  if ((dh = DH_new()) == nullptr) {
    return nullptr;
  }
  p = BN_bin2bn(dh2048_p, sizeof(dh2048_p), nullptr);
  g = BN_bin2bn(dh2048_g, sizeof(dh2048_g), nullptr);
  if (p == nullptr || g == nullptr) {
    DH_free(dh);
    BN_free(p);
    BN_free(g);
    return nullptr;
  }
  DH_set0_pqg(dh, p, nullptr, g);
  return (dh);
}
#endif

bool
SSLMultiCertConfigLoader::_enable_ktls([[maybe_unused]] SSL_CTX *ctx)
{
#ifdef SSL_OP_ENABLE_KTLS
  if (SSLConfigParams::ssl_ktls_enabled) {
    if (SSL_CTX_set_options(ctx, SSL_OP_ENABLE_KTLS)) {
      static DbgCtl dbg_ctl{"ssl.ktls"};
      Dbg(dbg_ctl, "KTLS is enabled");
    } else {
      return false;
    }
  }
#endif
  return true;
}

bool
SSLMultiCertConfigLoader::_enable_early_data([[maybe_unused]] SSL_CTX *ctx)
{
#if TS_HAS_TLS_EARLY_DATA
  if (SSLConfigParams::server_max_early_data > 0) {
#if HAVE_SSL_IN_EARLY_DATA
    // If SSL_in_early_data is available, it's probably BoringSSL
    // and SSL_set_early_data_enabled should be available.
    SSL_CTX_set_early_data_enabled(ctx, 1);
#endif
  }
#endif
  return true;
}

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

static ssl_ticket_key_block *
ssl_context_enable_tickets(SSL_CTX *ctx, const char *ticket_key_path)
{
#if TS_HAS_TLS_SESSION_TICKET
  ssl_ticket_key_block *keyblock = nullptr;

  keyblock = ssl_create_ticket_keyblock(ticket_key_path);

  // On the "first run" the metrics have not been initialized, so this has to check it.
  if (ssl_rsb.total_ticket_keys_renewed) {
    Metrics::Counter::increment(ssl_rsb.total_ticket_keys_renewed);
  }

// Setting the callback can only fail if OpenSSL does not recognize the
// SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB constant. we set the callback first
// so that we don't leave a ticket_key pointer attached if it fails.
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
  if (SSL_CTX_set_tlsext_ticket_key_evp_cb(ctx, ssl_callback_session_ticket) == 0) {
#else
  if (SSL_CTX_set_tlsext_ticket_key_cb(ctx, ssl_callback_session_ticket) == 0) {
#endif
    Error("failed to set session ticket callback");
    ticket_block_free(keyblock);
    return nullptr;
  }

  SSL_CTX_clear_options(ctx, SSL_OP_NO_TICKET);
  return keyblock;

#else  /* !TS_HAS_TLS_SESSION_TICKET */
  (void)ticket_key_path;
  return nullptr;
#endif /* TS_HAS_TLS_SESSION_TICKET */
}

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
  int            _fd;
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

  *buf                                                = 0;
  const SSLMultiCertConfigParams *sslMultCertSettings = static_cast<SSLMultiCertConfigParams *>(userdata);

  Dbg(dbg_ctl_ssl_load, "ssl_private_key_passphrase_callback_exec rwflag=%d dialog=%s", rwflag, sslMultCertSettings->dialog.get());

  // only respond to reading private keys, not writing them (does ats even do that?)
  if (0 == rwflag) {
    // execute the dialog program and use the first line output as the passphrase
    ink_assert(strncmp(sslMultCertSettings->dialog, "exec:", 5) == 0);
    const char *serverDialog = &sslMultCertSettings->dialog[5];
    FILE       *f            = popen(serverDialog, "r");
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
      Error("could not open dialog '%s' - %s", serverDialog, strerror(errno));
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

  *buf                                                = 0;
  const SSLMultiCertConfigParams *sslMultCertSettings = static_cast<SSLMultiCertConfigParams *>(userdata);

  Dbg(dbg_ctl_ssl_load, "ssl_private_key_passphrase_callback rwflag=%d dialog=%s", rwflag, sslMultCertSettings->dialog.get());

  // only respond to reading private keys, not writing them (does ats even do that?)
  if (0 == rwflag) {
    // output request
    fprintf(stdout, "Some of your private key files are encrypted for security reasons.\n");
    fprintf(stdout, "In order to read them you have to provide the pass phrases.\n");
    fprintf(stdout, "ssl_cert_name=%s", sslMultCertSettings->cert.get());
    if (sslMultCertSettings->key.get()) { // output ssl_key_name if provided
      fprintf(stdout, " ssl_key_name=%s", sslMultCertSettings->key.get());
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

  bool  bReturn     = false;
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

#if defined(LIBRESSL_VERSION_NUMBER)
#define ssl_malloc(size, file, line)             ssl_malloc(size)
#define ssl_realloc(ptr, size, file, line)       ssl_realloc(ptr, size)
#define ssl_free(ptr, file, line)                ssl_free(ptr)
#define ssl_track_malloc(size, file, line)       ssl_track_malloc(size)
#define ssl_track_realloc(ptr, size, file, line) ssl_track_realloc(ptr, size)
#define ssl_track_free(ptr, file, line)          ssl_track_free(ptr)
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
#if HAVE_ENGINE_LOAD_DYNAMIC
    ENGINE_load_dynamic();
#endif

    OPENSSL_load_builtin_modules();
    if (CONF_modules_load_file(SSLConfigParams::engine_conf_file, nullptr, 0) <= 0) {
      char err_buf[256] = {0};
      ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
      Error("Could not load SSL engine configuration file %s: %s", SSLConfigParams::engine_conf_file, err_buf);
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
    Dbg(dbg_ctl_ssl_load, "FIPS_mode: %d", mode);
#endif

    mutex_buf = static_cast<ink_mutex *>(OPENSSL_malloc(CRYPTO_num_locks() * sizeof(ink_mutex)));

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

  ssl_stapling_ex_init();

  // Reserve an application data index so that we can attach
  // the SSLNetVConnection to the SSL session.
  ssl_vc_index = SSL_get_ex_new_index(0, (void *)"NetVC index", nullptr, nullptr, nullptr);

  TLSBasicSupport::initialize();
  TLSEventSupport::initialize();
  ALPNSupport::initialize();
  TLSSessionResumptionSupport::initialize();
  TLSSNISupport::initialize();
  TLSEarlyDataSupport::initialize();
  TLSTunnelSupport::initialize();
  TLSCertSwitchSupport::initialize();
#if TS_USE_QUIC == 1
  QUICSupport::initialize();
#endif

  open_ssl_initialized = true;
}

SSL_CTX *
SSLMultiCertConfigLoader::default_server_ssl_ctx()
{
  return SSL_CTX_new(SSLv23_server_method());
}

static bool
SSLPrivateKeyHandler(SSL_CTX *ctx, const char *keyPath, const char *secret_data, int secret_data_len)
{
  EVP_PKEY *pkey = nullptr;
#if HAVE_ENGINE_GET_DEFAULT_RSA && HAVE_ENGINE_LOAD_PRIVATE_KEY
  ENGINE *e = ENGINE_get_default_RSA();
  if (e != nullptr) {
    pkey = ENGINE_load_private_key(e, keyPath, nullptr, nullptr);
    if (pkey) {
      if (!SSL_CTX_use_PrivateKey(ctx, pkey)) {
        Dbg(dbg_ctl_ssl_load, "failed to load server private key from engine");
        EVP_PKEY_free(pkey);
        return false;
      }
    }
  }
#else
  void *e = nullptr;
#endif
  if (pkey == nullptr) {
    scoped_BIO bio(BIO_new_mem_buf(secret_data, secret_data_len));

    pem_password_cb *password_cb = SSL_CTX_get_default_passwd_cb(ctx);
    void            *u           = SSL_CTX_get_default_passwd_cb_userdata(ctx);
    pkey                         = PEM_read_bio_PrivateKey(bio.get(), nullptr, password_cb, u);
    if (nullptr == pkey) {
      Dbg(dbg_ctl_ssl_load, "failed to load server private key (%.*s) from %s", secret_data_len < 50 ? secret_data_len : 50,
          secret_data, (!keyPath || keyPath[0] == '\0') ? "[empty key path]" : keyPath);
      return false;
    }
    if (!SSL_CTX_use_PrivateKey(ctx, pkey)) {
      Dbg(dbg_ctl_ssl_load, "failed to attach server private key loaded from %s",
          (!keyPath || keyPath[0] == '\0') ? "[empty key path]" : keyPath);
      EVP_PKEY_free(pkey);
      return false;
    }
    if (e == nullptr && !SSL_CTX_check_private_key(ctx)) {
      Dbg(dbg_ctl_ssl_load, "server private key does not match the certificate public key");
      return false;
    }
  }

  return true;
}

/**
   returns 0 on OK or negative value on failure and update log as appropriate.

   Will check:
   - if file exists, and has read permissions
   - for truncation or other PEM read fail
   - current time is between notBefore and notAfter dates of certificate
   if anything is not kosher, a negative value is returned and appropriate error logged.

   @static
 */
int
SSLMultiCertConfigLoader::check_server_cert_now(X509 *cert, const char *certname)
{
  int timeCmpValue;

  if (!cert) {
    // a truncated certificate would fall into here
    Error("invalid certificate %s: file is truncated or corrupted", certname);
    return -3;
  }

  // XXX we should log the notBefore and notAfter dates in the errors ...

  timeCmpValue = X509_cmp_current_time(X509_get_notBefore(cert));
  if (timeCmpValue == 0) {
    // an error occurred parsing the time, which we'll call a bogosity
    Error("invalid certificate %s: unable to parse notBefore time", certname);
    return -3;
  } else if (timeCmpValue > 0) {
    // cert contains a date before the notBefore
    Error("invalid certificate %s: notBefore date is in the future", certname);
    return -4;
  }

  timeCmpValue = X509_cmp_current_time(X509_get_notAfter(cert));
  if (timeCmpValue == 0) {
    // an error occurred parsing the time, which we'll call a bogosity
    Error("invalid certificate %s: unable to parse notAfter time", certname);
    return -3;
  } else if (timeCmpValue < 0) {
    // cert is expired
    Error("invalid certificate %s: certificate expired", certname);
    return -5;
  }

  Dbg(dbg_ctl_ssl_load, "server certificate %s passed accessibility and date checks", certname);
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

// This callback function is executed while OpenSSL processes the SSL
// handshake and does SSL record layer stuff.  It's used to trap
// client-initiated renegotiations and update cipher stats
static void
ssl_callback_info(const SSL *ssl, int where, int ret)
{
  Dbg(dbg_ctl_ssl_load, "ssl_callback_info ssl: %p, where: %d, ret: %d, State: %s", ssl, where, ret, SSL_state_string_long(ssl));

  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);

  if (!netvc || netvc->ssl != ssl) {
    Dbg(dbg_ctl_ssl_error, "ssl_callback_info call back on stale netvc");
    return;
  }

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
#ifdef TLS1_3_VERSION
      // TLSv1.3 has no renegotiation.
      if (SSL_version(ssl) >= TLS1_3_VERSION) {
        Dbg(dbg_ctl_ssl_load, "TLSv1.3 has no renegotiation.");
        return;
      }
#endif
      netvc->setSSLClientRenegotiationAbort(true);
      Dbg(dbg_ctl_ssl_load, "ssl_callback_info trying to renegotiate from the client");
    }
  }
  if (where & SSL_CB_HANDSHAKE_DONE) {
    // handshake is complete
    const SSL_CIPHER *cipher = SSL_get_current_cipher(ssl);
    if (cipher) {
      const char *cipherName = SSL_CIPHER_get_name(cipher);
      // lookup index of stat by name and incr count
      auto it = cipher_map.find(cipherName);
      if (it == cipher_map.end()) {
        it = cipher_map.find(SSL_CIPHER_STAT_OTHER);
        ink_assert(it != cipher_map.end());
      }
      Metrics::Counter::increment(it->second);
    }

#if defined(OPENSSL_IS_BORINGSSL)
    uint16_t group_id = SSL_get_group_id(ssl);
    if (group_id != 0) {
      const char *group_name = SSL_get_group_name(group_id);
      if (auto it = tls_group_map.find(group_name); it != tls_group_map.end()) {
        Metrics::Counter::increment(it->second);
      } else {
        Warning("Unknown TLS Group");
      }
    }
#elif defined(SSL_get_negotiated_group)
    int nid = SSL_get_negotiated_group(const_cast<SSL *>(ssl));
    if (nid != NID_undef) {
      if (auto it = tls_group_map.find(nid); it != tls_group_map.end()) {
        Metrics::Counter::increment(it->second);
      } else {
        auto other = tls_group_map.find(SSL_GROUP_STAT_OTHER_KEY);
        Metrics::Counter::increment(other->second);
      }
    }
#endif // OPENSSL_IS_BORINGSSL or SSL_get_negotiated_group
  }
}

void
SSLMultiCertConfigLoader::_set_handshake_callbacks(SSL_CTX *ctx)
{
  // Make sure the callbacks are set
#if !HAVE_SSL_CTX_SET_SELECT_CERTIFICATE_CB
  SSL_CTX_set_cert_cb(ctx, ssl_cert_callback, nullptr);
  SSL_CTX_set_tlsext_servername_callback(ctx, ssl_servername_callback);
#endif

#if HAVE_SSL_CTX_SET_CLIENT_HELLO_CB
  SSL_CTX_set_client_hello_cb(ctx, ssl_client_hello_callback, nullptr);
#elif HAVE_SSL_CTX_SET_SELECT_CERTIFICATE_CB
  SSL_CTX_set_select_certificate_cb(ctx, [](const SSL_CLIENT_HELLO *client_hello) -> ssl_select_cert_result_t {
    ssl_select_cert_result_t res;
    res = ssl_client_hello_callback(client_hello);
    if (res == ssl_select_cert_error) {
      return res;
    }

    res = (ssl_servername_callback(client_hello->ssl, nullptr, nullptr) == SSL_TLSEXT_ERR_OK) ? ssl_select_cert_success :
                                                                                                ssl_select_cert_error;
    if (res == ssl_select_cert_error) {
      return res;
    }

    int cbres = ssl_cert_callback(client_hello->ssl, (void *)client_hello);
    switch (cbres) {
    case -2:
      res = ssl_select_cert_retry;
      break;
    case -1:
      res = ssl_select_cert_success;
      break;
    case 0:
      res = ssl_select_cert_error;
      break;
    case 1:
      res = ssl_select_cert_success;
      break;
    default:
      ink_assert(!"unhandled cert result");
    }

    return res;
  });
#endif
}

void
setClientCertLevel(SSL *ssl, uint8_t certLevel)
{
  SSLConfig::scoped_config params;
  int                      server_verify_client = SSL_VERIFY_NONE;

  if (certLevel == 2) {
    server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE;
  } else if (certLevel == 1) {
    server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE;
  } else if (certLevel == 0) {
    server_verify_client = SSL_VERIFY_NONE;
  } else {
    ink_release_assert(!"Invalid client verify level");
  }

  Dbg(dbg_ctl_ssl_load, "setting cert level to %d", server_verify_client);
  SSL_set_verify(ssl, server_verify_client, ssl_verify_client_callback);
  SSL_set_verify_depth(ssl, params->verify_depth); // might want to make configurable at some point.
}

void
setClientCertCACerts(SSL *ssl, const char *file, const char *dir)
{
  if ((file != nullptr && file[0] != '\0') || (dir != nullptr && dir[0] != '\0')) {
#if TS_HAS_VERIFY_CERT_STORE
    // The set0 version will take ownership of the X509_STORE object
    X509_STORE *ctx = X509_STORE_new();
    if (X509_STORE_load_locations(ctx, file && file[0] != '\0' ? file : nullptr, dir && dir[0] != '\0' ? dir : nullptr)) {
      SSL_set0_verify_cert_store(ssl, ctx);
    }

    // SSL_set_client_CA_list takes ownership of the STACK_OF(X509) structure
    // So we load it each time to pass into the SSL object
    if (file != nullptr && file[0] != '\0') {
      SSL_set_client_CA_list(ssl, SSL_load_client_CA_file(file));
    }
#else
    ink_assert(!"Configuration checking should prevent reaching this code");
#endif
  }
}

/**
   Initialize SSL_CTX for server
   This is public function because of used by SSLCreateServerContext.
 */
std::vector<SSLLoadingContext>
SSLMultiCertConfigLoader::init_server_ssl_ctx(CertLoadData const &data, const SSLMultiCertConfigParams *sslMultCertSettings)
{
  std::vector<std::vector<std::string>> cert_names;
  std::vector<std::vector<std::string>> key_names;
  std::vector<std::string>              key_names_list;

  bool generate_default_ctx = data.cert_names_list.empty();
  if (!generate_default_ctx) {
#ifndef HAVE_NATIVE_DUAL_CERT_SUPPORT
    for (auto const &name : data.cert_names_list) {
      cert_names.emplace_back(std::vector({name}));
    }
    for (auto const &name : data.key_list) {
      key_names.emplace_back(std::vector({name}));
    }
#else
    if (!data.cert_names_list.empty()) {
      cert_names.emplace_back(data.cert_names_list);
      key_names.emplace_back(data.key_list);
    }
#endif
  } else {
    // In the case of no cert_names, we still want to create a
    // ctx with all the bells and whistles (as much as possible)
    cert_names.emplace_back(std::vector({std::string("default")}));
    key_names.emplace_back(std::vector({std::string("default")}));
  }

  SSLCertContextType             ctx_type = SSLCertContextType::GENERIC;
  std::vector<SSLLoadingContext> ret;
  unsigned int                   i   = 0;
  SSL_CTX                       *ctx = nullptr;
  for (auto const &cert_names_list : cert_names) {
    if (i < key_names.size()) {
      key_names_list = key_names[i];
    } else {
      key_names_list.clear();
    }

    ctx = this->default_server_ssl_ctx();

    ctx_type = (!generate_default_ctx && i < data.cert_type_list.size()) ? data.cert_type_list[i] : SSLCertContextType::GENERIC;

    Dbg(dbg_ctl_ssl_load, "Creating new context %p cert_count=%ld initial: %s", ctx, cert_names_list.size(),
        cert_names_list[0].c_str());

    SSL_CTX_set_min_proto_version(ctx, TLS1_VERSION);
    SSL_CTX_set_options(ctx, _params->ssl_ctx_options);

    if (_params->server_tls_ver_min >= 0 || _params->server_tls_ver_max >= 0) {
      int ver = 0;
      if (_params->server_tls_ver_min >= 0) {
        ver = TLS1_VERSION + _params->server_tls_ver_min;
      }
      // Setting 0 enables version down to the lowest version supported by the SSL library
      SSL_CTX_set_min_proto_version(ctx, ver);

      ver = 0;
      if (_params->server_tls_ver_max >= 0) {
        ver = TLS1_VERSION + _params->server_tls_ver_max;
      }
      // Setting 0 enables version up to the highest version supported by the SSL library
      SSL_CTX_set_max_proto_version(ctx, ver);
    }

    if (!this->_setup_session_cache(ctx)) {
      goto fail;
    }

#ifdef SSL_MODE_RELEASE_BUFFERS
    Dbg(dbg_ctl_ssl_load, "enabling SSL_MODE_RELEASE_BUFFERS");
    SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS);
#endif

#ifdef SSL_OP_SAFARI_ECDHE_ECDSA_BUG
    SSL_CTX_set_options(ctx, SSL_OP_SAFARI_ECDHE_ECDSA_BUG);
#endif

    if (sslMultCertSettings) {
      if (!this->_setup_dialog(ctx, sslMultCertSettings)) {
        goto fail;
      }

      if (sslMultCertSettings->cert && !generate_default_ctx) {
        if (!SSLMultiCertConfigLoader::load_certs(ctx, cert_names_list, key_names_list, data, _params, sslMultCertSettings)) {
          goto fail;
        }
      }

      if (!this->_set_verify_path(ctx, sslMultCertSettings)) {
        goto fail;
      }

      if (!this->_setup_session_ticket(ctx, sslMultCertSettings)) {
        goto fail;
      }
    }

    if (!this->_setup_client_cert_verification(ctx)) {
      goto fail;
    }

    if (!SSLMultiCertConfigLoader::set_session_id_context(ctx, _params, sslMultCertSettings)) {
      goto fail;
    }

    if (!this->_set_cipher_suites_for_legacy_versions(ctx)) {
      goto fail;
    }

    if (!this->_set_cipher_suites(ctx)) {
      goto fail;
    }

    if (!this->_set_curves(ctx)) {
      goto fail;
    }

    if (!this->_enable_ktls(ctx)) {
      goto fail;
    }

    if (!this->_enable_early_data(ctx)) {
      goto fail;
    }

    if (!ssl_context_enable_dhe(_params->dhparamsFile, ctx)) {
      goto fail;
    }

    if (sslMultCertSettings && sslMultCertSettings->dialog) {
      SSLMultiCertConfigLoader::clear_pw_references(ctx);
    }

    if (!this->_set_info_callback(ctx)) {
      goto fail;
    }

    if (!this->_set_npn_callback(ctx)) {
      goto fail;
    }

    if (!this->_set_alpn_callback(ctx)) {
      goto fail;
    }

#if TS_HAS_TLS_KEYLOGGING
    if (unlikely(TLSKeyLogger::is_enabled()) && !this->_set_keylog_callback(ctx)) {
      goto fail;
    }
#endif

    if (SSLConfigParams::init_ssl_ctx_cb) {
      SSLConfigParams::init_ssl_ctx_cb(ctx, true);
    }

    ret.emplace_back(SSLLoadingContext(ctx, ctx_type));
    i++;
  }
  return ret;

fail:
  ink_assert(ctx != nullptr);
  SSLMultiCertConfigLoader::clear_pw_references(ctx);
  SSL_CTX_free(ctx);

  return ret;
}

bool
SSLMultiCertConfigLoader::_setup_session_cache(SSL_CTX *ctx)
{
  const SSLConfigParams *params = this->_params;

  Dbg(dbg_ctl_ssl_session_cache,
      "ssl context=%p: using session cache options, enabled=%d, size=%d, num_buckets=%d, "
      "skip_on_contention=%d, timeout=%d, auto_clear=%d",
      ctx, params->ssl_session_cache, params->ssl_session_cache_size, params->ssl_session_cache_num_buckets,
      params->ssl_session_cache_skip_on_contention, params->ssl_session_cache_timeout, params->ssl_session_cache_auto_clear);

  if (params->ssl_session_cache_timeout) {
    SSL_CTX_set_timeout(ctx, params->ssl_session_cache_timeout);
  }

  int additional_cache_flags  = 0;
  additional_cache_flags     |= (params->ssl_session_cache_auto_clear == 0) ? SSL_SESS_CACHE_NO_AUTO_CLEAR : 0;

  switch (params->ssl_session_cache) {
  case SSLConfigParams::SSL_SESSION_CACHE_MODE_OFF:
    Dbg(dbg_ctl_ssl_session_cache, "disabling SSL session cache");

    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF | SSL_SESS_CACHE_NO_INTERNAL);
    break;
  case SSLConfigParams::SSL_SESSION_CACHE_MODE_SERVER_OPENSSL_IMPL:
    Dbg(dbg_ctl_ssl_session_cache, "enabling SSL session cache with OpenSSL implementation");

    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER | additional_cache_flags);
    SSL_CTX_sess_set_cache_size(ctx, params->ssl_session_cache_size);
    break;
  case SSLConfigParams::SSL_SESSION_CACHE_MODE_SERVER_ATS_IMPL: {
    Dbg(dbg_ctl_ssl_session_cache, "enabling SSL session cache with ATS implementation");
    /* Add all the OpenSSL callbacks */
    SSL_CTX_sess_set_new_cb(ctx, ssl_new_cached_session);
    SSL_CTX_sess_set_remove_cb(ctx, ssl_rm_cached_session);
    SSL_CTX_sess_set_get_cb(ctx, ssl_get_cached_session);

    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER | SSL_SESS_CACHE_NO_INTERNAL | additional_cache_flags);
    break;
  }
  }
  return true;
}

bool
SSLMultiCertConfigLoader::_setup_dialog(SSL_CTX *ctx, const SSLMultiCertConfigParams *sslMultCertSettings)
{
  if (sslMultCertSettings->dialog) {
    // pass phrase dialog configuration
    pem_password_cb *passwd_cb = nullptr;
    if (strncmp(sslMultCertSettings->dialog, "exec:", 5) == 0) {
      const char *serverDialog = &sslMultCertSettings->dialog[5];
      Dbg(dbg_ctl_ssl_load, "exec:%s", serverDialog);
      // validate the exec program
      if (!ssl_private_key_validate_exec(serverDialog)) {
        SSLError("failed to access '%s' pass phrase program: %s", serverDialog, strerror(errno));
        return false;
      }
      passwd_cb = ssl_private_key_passphrase_callback_exec;
    } else if (strcmp(sslMultCertSettings->dialog, "builtin") == 0) {
      passwd_cb = ssl_private_key_passphrase_callback_builtin;
    } else { // unknown config
      SSLError("unknown %s configuration value '%s'", SSL_KEY_DIALOG.data(), (const char *)sslMultCertSettings->dialog);
      return false;
    }
    SSL_CTX_set_default_passwd_cb(ctx, passwd_cb);
    SSL_CTX_set_default_passwd_cb_userdata(ctx, const_cast<SSLMultiCertConfigParams *>(sslMultCertSettings));
  }
  return true;
}

bool
SSLMultiCertConfigLoader::_set_verify_path(SSL_CTX *ctx, const SSLMultiCertConfigParams *sslMultCertSettings)
{
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
  const SSLConfigParams *params = this->_params;
  if (!sslMultCertSettings->ca && params->serverCACertPath != nullptr) {
    if ((!SSL_CTX_load_verify_locations(ctx, params->serverCACertFilename, params->serverCACertPath)) ||
        (!SSL_CTX_set_default_verify_paths(ctx))) {
      SSLError("invalid CA Certificate file: %s or CA Certificate path: %s",
               (!params->serverCACertFilename || params->serverCACertFilename[0] == '\0') ? "[empty file name]" :
                                                                                            params->serverCACertFilename,
               (!params->serverCACertPath || params->serverCACertPath[0] == '\0') ? "[empty path]" : params->serverCACertPath);
      return false;
    }
  }
  return true;
}

bool
SSLMultiCertConfigLoader::_setup_session_ticket(SSL_CTX *ctx, const SSLMultiCertConfigParams *sslMultCertSettings)
{
  // Session tickets are enabled by default. Disable if explicitly requested.
  if (sslMultCertSettings->session_ticket_enabled == 0) {
    SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET);
    Dbg(dbg_ctl_ssl_load, "ssl session ticket is disabled");
  }

  if (!(this->_params->ssl_ctx_options & SSL_OP_NO_TLSv1_3)) {
    SSL_CTX_set_num_tickets(ctx, sslMultCertSettings->session_ticket_number);
    Dbg(dbg_ctl_ssl_load, "ssl session ticket number set to %d", sslMultCertSettings->session_ticket_number);
  }
  return true;
}

bool
SSLMultiCertConfigLoader::_setup_client_cert_verification(SSL_CTX *ctx)
{
  int                    server_verify_client;
  const SSLConfigParams *params = this->_params;

  if (params->clientCertLevel != 0) {
    if (params->serverCACertFilename != nullptr && params->serverCACertPath != nullptr) {
      if ((!SSL_CTX_load_verify_locations(ctx, params->serverCACertFilename, params->serverCACertPath)) ||
          (!SSL_CTX_set_default_verify_paths(ctx))) {
        SSLError("invalid CA Certificate file: %s or CA Certificate path: %s",
                 (!params->serverCACertFilename || params->serverCACertFilename[0] == '\0') ? "[empty file name]" :
                                                                                              params->serverCACertFilename,
                 (!params->serverCACertPath || params->serverCACertPath[0] == '\0') ? "[empty path]" : params->serverCACertPath);
        return false;
      }
    }

    if (params->clientCertLevel == 2) {
      server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE;
    } else if (params->clientCertLevel == 1) {
      server_verify_client = SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE;
    } else {
      // disable client cert support
      server_verify_client = SSL_VERIFY_NONE;
      Error("illegal client certification level %d in %s", server_verify_client, ts::filename::RECORDS);
    }
    SSL_CTX_set_verify(ctx, server_verify_client, ssl_verify_client_callback);
    SSL_CTX_set_verify_depth(ctx, params->verify_depth); // might want to make configurable at some point.
  }
  return true;
}

bool
SSLMultiCertConfigLoader::_set_cipher_suites_for_legacy_versions(SSL_CTX *ctx)
{
  if (this->_params->cipherSuite != nullptr) {
    if (!SSL_CTX_set_cipher_list(ctx, this->_params->cipherSuite)) {
      SSLError("invalid cipher suite in %s", ts::filename::RECORDS);
      return false;
    }
  }
  return true;
}

bool
SSLMultiCertConfigLoader::_set_cipher_suites([[maybe_unused]] SSL_CTX *ctx)
{
#if TS_USE_TLS_SET_CIPHERSUITES
  if (this->_params->server_tls13_cipher_suites != nullptr) {
    if (!SSL_CTX_set_ciphersuites(ctx, this->_params->server_tls13_cipher_suites)) {
      SSLError("invalid tls server cipher suites in %s", ts::filename::RECORDS);
      return false;
    }
  }
#endif
  return true;
}

bool
SSLMultiCertConfigLoader::_set_curves(SSL_CTX *ctx)
{
  if (this->_params->server_groups_list != nullptr) {
    if (!SSL_CTX_set1_groups_list(ctx, this->_params->server_groups_list)) {
      SSLError("invalid groups list for server in %s", ts::filename::RECORDS);
      return false;
    }
  }

  return true;
}

bool
SSLMultiCertConfigLoader::_set_info_callback(SSL_CTX *ctx)
{
  SSL_CTX_set_info_callback(ctx, ssl_callback_info);
  return true;
}

bool
SSLMultiCertConfigLoader::_set_npn_callback(SSL_CTX *ctx)
{
  SSL_CTX_set_next_protos_advertised_cb(ctx, ssl_next_protos_advertised_callback, nullptr);
  return true;
}

bool
SSLMultiCertConfigLoader::_set_alpn_callback(SSL_CTX *ctx)
{
  SSL_CTX_set_alpn_select_cb(ctx, ssl_alpn_select_callback, nullptr);
  return true;
}

bool
SSLMultiCertConfigLoader::_set_keylog_callback(SSL_CTX *ctx)
{
#if TS_HAS_TLS_KEYLOGGING
  SSL_CTX_set_keylog_callback(ctx, TLSKeyLogger::ssl_keylog_cb);
#endif
  return true;
}

SSL_CTX *
SSLCreateServerContext(const SSLConfigParams *params, const SSLMultiCertConfigParams *sslMultiCertSettings, const char *cert_path,
                       const char *key_path)
{
  SSLMultiCertConfigLoader                       loader(params);
  std::vector<X509 *>                            cert_list;
  std::set<std::string>                          common_names;
  std::unordered_map<int, std::set<std::string>> unique_names;
  SSLMultiCertConfigLoader::CertLoadData         data;
  SSLCertContextType                             cert_type;
  if (!loader.load_certs_and_cross_reference_names(cert_list, data, params, sslMultiCertSettings, common_names, unique_names,
                                                   &cert_type)) {
    return nullptr;
  }
  for (auto &i : cert_list) {
    X509_free(i);
  }

  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(nullptr, &SSL_CTX_free);

  std::vector<SSLLoadingContext> ctxs = loader.init_server_ssl_ctx(data, sslMultiCertSettings);
  for (auto const &loaderctx : ctxs) {
    ctx.reset(loaderctx.ctx);

    if (ctx && cert_path) {
      if (!SSL_CTX_use_certificate_file(ctx.get(), cert_path, SSL_FILETYPE_PEM)) {
        SSLError("SSLCreateServerContext(): failed to load server certificate file: %s",
                 (!cert_path || cert_path[0] == '\0') ? "[empty file]" : cert_path);
        ctx = nullptr;
      } else if (!key_path || key_path[0] == '\0') {
        key_path = cert_path;
      }
      if (ctx) {
        if (!SSL_CTX_use_PrivateKey_file(ctx.get(), key_path, SSL_FILETYPE_PEM)) {
          SSLError("SSLCreateServerContext(): failed to load server private key: %s",
                   (!key_path || key_path[0] == '\0') ? "[empty file]" : key_path);
          ctx = nullptr;
        } else if (!SSL_CTX_check_private_key(ctx.get())) {
          SSLError("SSLCreateServerContext(): server private key: %s does not match server certificate: %s",
                   (!key_path || key_path[0] == '\0') ? "[empty file]" : key_path,
                   (!cert_path || cert_path[0] == '\0') ? "[empty file]" : cert_path);
          ctx = nullptr;
        }
      }
    }
  }
  return ctx.release();
}

/**
 * Common name resolution and cert validation
 */
bool
SSLMultiCertConfigLoader::_prep_ssl_ctx(const shared_SSLMultiCertConfigParams  &sslMultCertSettings,
                                        SSLMultiCertConfigLoader::CertLoadData &data, std::set<std::string> &common_names,
                                        std::unordered_map<int, std::set<std::string>> &unique_names)
{
  std::vector<X509 *>    cert_list;
  const SSLConfigParams *params = this->_params;

  SSLCertContextType cert_type;
  if (!this->load_certs_and_cross_reference_names(cert_list, data, params, sslMultCertSettings.get(), common_names, unique_names,
                                                  &cert_type)) {
    return false;
  }

  int  i          = 0;
  bool good_certs = true;
  for (auto const &cert : cert_list) {
    const char *current_cert_name = data.cert_names_list[i].c_str();
    if (0 > SSLMultiCertConfigLoader::check_server_cert_now(cert, current_cert_name)) {
      /* At this point, we know cert is bad, and we've already printed a
         descriptive reason as to why cert is bad to the log file */
      Dbg(this->_dbg_ctl(), "Marking certificate as NOT VALID: %s", current_cert_name);
      good_certs = false;
    }
    i++;
  }

  for (auto &cert : cert_list) {
    X509_free(cert);
  }
  return good_certs;
}

/**
   Insert SSLCertContext (SSL_CTX and options) into SSLCertLookup with key.
   Do NOT call SSL_CTX_set_* functions from here. SSL_CTX should be set up by SSLMultiCertConfigLoader::init_server_ssl_ctx().
 */
bool
SSLMultiCertConfigLoader::_store_ssl_ctx(SSLCertLookup *lookup, const shared_SSLMultiCertConfigParams sslMultCertSettings)
{
  bool                                           retval = true;
  std::set<std::string>                          common_names;
  std::unordered_map<int, std::set<std::string>> unique_names;
  SSLMultiCertConfigLoader::CertLoadData         data;

  if (!this->_prep_ssl_ctx(sslMultCertSettings, data, common_names, unique_names)) {
    lookup->is_valid = false;
    return false;
  }

  std::vector<SSLLoadingContext> ctxs = this->init_server_ssl_ctx(data, sslMultCertSettings.get());
  for (const auto &loadingctx : ctxs) {
    if (!sslMultCertSettings ||
        !this->_store_single_ssl_ctx(lookup, sslMultCertSettings, shared_SSL_CTX{loadingctx.ctx, SSL_CTX_free}, loadingctx.ctx_type,
                                     common_names)) {
      if (!common_names.empty()) {
        std::string names;
        for (auto const &name : data.cert_names_list) {
          names.append(name);
          names.append(" ");
        }
        Warning("(%s) Failed to insert SSL_CTX for certificate %s entries for names already made", this->_debug_tag(),
                names.c_str());
      } else {
        Warning("(%s) Failed to insert SSL_CTX", this->_debug_tag());
      }
    } else {
      if (!common_names.empty()) {
        lookup->register_cert_secrets(data.cert_names_list, common_names);
      }
    }
  }

  for (auto iter = unique_names.begin(); retval && iter != unique_names.end(); ++iter) {
    size_t i = iter->first;

    SSLMultiCertConfigLoader::CertLoadData single_data;
    single_data.cert_names_list.push_back(data.cert_names_list[i]);
    if (i < data.key_list.size()) {
      single_data.key_list.push_back(data.key_list[i]);
    }
    single_data.ca_list.push_back(i < data.ca_list.size() ? data.ca_list[i] : "");
    single_data.ocsp_list.push_back(i < data.ocsp_list.size() ? data.ocsp_list[i] : "");

    std::vector<SSLLoadingContext> ctxs = this->init_server_ssl_ctx(single_data, sslMultCertSettings.get());
    for (const auto &loadingctx : ctxs) {
      if (!this->_store_single_ssl_ctx(lookup, sslMultCertSettings, shared_SSL_CTX{loadingctx.ctx, SSL_CTX_free},
                                       loadingctx.ctx_type, iter->second)) {
        retval = false;
      } else {
        lookup->register_cert_secrets(data.cert_names_list, iter->second);
      }
    }
  }
  return retval;
}

/**
 * Much like _store_ssl_ctx, but this updates the existing lookup entries rather than creating them
 * If it fails to create the new SSL_CTX, don't invalidate the lookup structure, just keep working with the
 * previous entry
 */
bool
SSLMultiCertConfigLoader::update_ssl_ctx(const std::string &secret_name)
{
  bool retval = true;

  SSLCertificateConfig::scoped_config lookup;
  if (!lookup) {
    // SSLCertificateConfig is still being configured, thus there are no SSL
    // contexts to update. This situation can happen during startup if a
    // registered hook updates certs before SSLCertContext configuration is
    // complete.
    return retval;
  }
  std::set<shared_SSLMultiCertConfigParams> policies;
  lookup->getPolicies(secret_name, policies);

  for (auto policy_iter = policies.begin(); policy_iter != policies.end() && retval; ++policy_iter) {
    std::set<std::string>                          common_names;
    std::unordered_map<int, std::set<std::string>> unique_names;
    SSLMultiCertConfigLoader::CertLoadData         data;
    if (!this->_prep_ssl_ctx(*policy_iter, data, common_names, unique_names)) {
      retval = false;
      break;
    }

    std::vector<SSLLoadingContext> ctxs = this->init_server_ssl_ctx(data, policy_iter->get());
    for (const auto &loadingctx : ctxs) {
      shared_SSL_CTX ctx(loadingctx.ctx, SSL_CTX_free);

      if (!ctx) {
        retval = false;
      } else {
        for (auto const &name : common_names) {
          SSLCertContext *cc = lookup->find(name, loadingctx.ctx_type);
          if (cc && cc->userconfig.get() == policy_iter->get()) {
            cc->setCtx(ctx);
          }
        }
      }
    }

    for (auto iter = unique_names.begin(); retval && iter != unique_names.end(); ++iter) {
      size_t i = iter->first;

      SSLMultiCertConfigLoader::CertLoadData single_data;
      single_data.cert_names_list.push_back(data.cert_names_list[i]);
      single_data.key_list.push_back(i < data.key_list.size() ? data.key_list[i] : "");
      single_data.ca_list.push_back(i < data.ca_list.size() ? data.ca_list[i] : "");
      single_data.ocsp_list.push_back(i < data.ocsp_list.size() ? data.ocsp_list[i] : "");

      std::vector<SSLLoadingContext> ctxs = this->init_server_ssl_ctx(single_data, policy_iter->get());
      for (auto const &loadingctx : ctxs) {
        shared_SSL_CTX unique_ctx(loadingctx.ctx, SSL_CTX_free);

        if (!unique_ctx) {
          retval = false;
        } else {
          for (auto const &name : iter->second) {
            SSLCertContext *cc = lookup->find(name, loadingctx.ctx_type);
            if (cc && cc->userconfig.get() == policy_iter->get()) {
              cc->setCtx(unique_ctx);
            }
          }
        }
      }
    }
  }
  return retval;
}

bool
SSLMultiCertConfigLoader::_store_single_ssl_ctx(SSLCertLookup *lookup, const shared_SSLMultiCertConfigParams &sslMultCertSettings,
                                                shared_SSL_CTX ctx, SSLCertContextType ctx_type, std::set<std::string> &names)
{
  bool                        inserted = false;
  shared_ssl_ticket_key_block keyblock = nullptr;
  // Load the session ticket key if session tickets are not disabled
  if (sslMultCertSettings->session_ticket_enabled != 0) {
    keyblock = shared_ssl_ticket_key_block(ssl_context_enable_tickets(ctx.get(), nullptr), ticket_block_free);
  }

  // Index this certificate by the specified IP(v6) address. If the address is "*", make it the default context.
  if (sslMultCertSettings->addr) {
    if (strcmp(sslMultCertSettings->addr, "*") == 0) {
      Dbg(dbg_ctl_ssl_load, "Addr is '*'; setting %p to default", ctx.get());
      if (lookup->insert(sslMultCertSettings->addr, SSLCertContext(ctx, ctx_type, sslMultCertSettings, keyblock)) >= 0) {
        inserted            = true;
        lookup->ssl_default = ctx;
        this->_set_handshake_callbacks(ctx.get());
      }
    } else {
      IpEndpoint ep;

      if (ats_ip_pton(sslMultCertSettings->addr, &ep) == 0) {
        if (lookup->insert(ep, SSLCertContext(ctx, ctx_type, sslMultCertSettings, keyblock)) >= 0) {
          inserted = true;
        }
      } else {
        Error("'%s' is not a valid IPv4 or IPv6 address", (const char *)sslMultCertSettings->addr);
        lookup->is_valid = false;
      }
    }
  }

  // Insert additional mappings. Note that this maps multiple keys to the same value, so when
  // this code is updated to reconfigure the SSL certificates, it will need some sort of
  // refcounting or alternate way of avoiding double frees.
  for (auto const &sni_name : names) {
    if (lookup->insert(sni_name.c_str(), SSLCertContext(ctx, ctx_type, sslMultCertSettings, keyblock)) >= 0) {
      inserted = true;
    }
  }

  if (inserted) {
    if (SSLConfigParams::init_ssl_ctx_cb) {
      SSLConfigParams::init_ssl_ctx_cb(ctx.get(), true);
    }
  }

  if (!inserted) {
    ctx = nullptr;
  }

  return ctx.get();
}

static bool
ssl_extract_certificate(const matcher_line *line_info, SSLMultiCertConfigParams *sslMultCertSettings)
{
  for (int i = 0; i < MATCHER_MAX_TOKENS; ++i) {
    const char *label;
    const char *value;

    label = line_info->line[0][i];
    value = line_info->line[1][i];

    if (label == nullptr) {
      continue;
    }
    Dbg(dbg_ctl_ssl_load, "Extracting certificate label: %s, value: %s", label, value);

    if (strcasecmp(label, SSL_IP_TAG) == 0) {
      sslMultCertSettings->addr = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_CERT_TAG) == 0) {
      sslMultCertSettings->cert = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_CA_TAG) == 0) {
      sslMultCertSettings->ca = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_PRIVATE_KEY_TAG) == 0) {
      sslMultCertSettings->key = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_OCSP_RESPONSE_TAG) == 0) {
      sslMultCertSettings->ocsp_response = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_SESSION_TICKET_ENABLED) == 0) {
      sslMultCertSettings->session_ticket_enabled = atoi(value);
    }

    if (strcasecmp(label, SSL_SESSION_TICKET_NUMBER) == 0) {
      sslMultCertSettings->session_ticket_number = atoi(value);
    }

    if (strcasecmp(label, SSL_KEY_DIALOG) == 0) {
      sslMultCertSettings->dialog = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_SERVERNAME) == 0) {
      sslMultCertSettings->servername = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_ACTION_TAG) == 0) {
      if (strcasecmp(SSL_ACTION_TUNNEL_TAG, value) == 0) {
        sslMultCertSettings->opt = SSLCertContextOption::OPT_TUNNEL;
      } else {
        Error("Unrecognized action for %s", SSL_ACTION_TAG.data());
        return false;
      }
    }
  }
  // TS-4679:  It is ok to be missing the cert.  At least if the action is set to tunnel
  if (sslMultCertSettings->cert) {
    SimpleTokenizer cert_tok(sslMultCertSettings->cert, SSL_CERT_SEPARATE_DELIM);
    const char     *first_cert = cert_tok.getNext();
    if (first_cert) {
      sslMultCertSettings->first_cert = ats_strdup(first_cert);
    }
  }

  return true;
}

swoc::Errata
SSLMultiCertConfigLoader::load(SSLCertLookup *lookup)
{
  const SSLConfigParams *params = this->_params;

  char        *tok_state = nullptr;
  char        *line      = nullptr;
  unsigned     line_num  = 0;
  matcher_line line_info;

  const matcher_tags sslCertTags = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false};

  Note("(%s) %s loading ...", this->_debug_tag(), ts::filename::SSL_MULTICERT);

  std::error_code ec;
  std::string     content{swoc::file::load(swoc::file::path{params->configFilePath}, ec)};
  if (ec) {
    switch (ec.value()) {
    case ENOENT:
      // missing config file is an acceptable runtime state
      return swoc::Errata(ERRATA_WARN, "Cannot open SSL certificate configuration \"{}\" - {}", params->configFilePath, ec);
    default:
      return swoc::Errata(ERRATA_ERROR, "Failed to read SSL certificate configuration from \"{}\" - {}", params->configFilePath,
                          ec);
    }
  }

  // Optionally elevate/allow file access to read root-only
  // certificates. The destructor will drop privilege for us.
  uint32_t elevate_setting = 0;
  REC_ReadConfigInteger(elevate_setting, "proxy.config.ssl.cert.load_elevated");
  ElevateAccess elevate_access(elevate_setting ? ElevateAccess::FILE_PRIVILEGE : 0);

  line = tokLine(content.data(), &tok_state);
  swoc::Errata errata(ERRATA_NOTE);
  while (line != nullptr) {
    line_num++;

    // Skip all blank spaces at beginning of line.
    while (*line && isspace(*line)) {
      line++;
    }

    if (*line != '\0' && *line != '#') {
      shared_SSLMultiCertConfigParams sslMultiCertSettings = std::make_shared<SSLMultiCertConfigParams>();
      const char                     *errPtr;

      errPtr = parseConfigLine(line, &line_info, &sslCertTags);
      Dbg(dbg_ctl_ssl_load, "currently parsing %s at line %d from config file: %s", line, line_num, params->configFilePath);
      if (errPtr != nullptr) {
        Warning("%s: discarding %s entry at line %d: %s", __func__, params->configFilePath, line_num, errPtr);
      } else {
        if (ssl_extract_certificate(&line_info, sslMultiCertSettings.get())) {
          // There must be a certificate specified unless the tunnel action is set
          if (sslMultiCertSettings->cert || sslMultiCertSettings->opt != SSLCertContextOption::OPT_TUNNEL) {
            if (!this->_store_ssl_ctx(lookup, sslMultiCertSettings)) {
              errata.note(ERRATA_ERROR, "Failed to load certificate on line {}", line_num);
            }
          } else {
            errata.note(ERRATA_WARN, "No ssl_cert_name specified and no tunnel action set on line {}", line_num);
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
    shared_SSLMultiCertConfigParams sslMultiCertSettings(new SSLMultiCertConfigParams);
    sslMultiCertSettings->addr = ats_strdup("*");
    if (!this->_store_ssl_ctx(lookup, sslMultiCertSettings)) {
      errata.note(ERRATA_ERROR, "failed set default context");
    }
  }

  return errata;
}

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

std::string
get_sni_addr(SSL *ssl)
{
  std::string sni_addr;

  if (ssl != nullptr) {
    const char *sni_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (sni_name) {
      sni_addr.assign(sni_name);
    } else {
      int              sock_fd = SSL_get_fd(ssl);
      sockaddr_storage addr;
      socklen_t        addr_len = sizeof(addr);
      if (sock_fd >= 0) {
        getpeername(sock_fd, reinterpret_cast<sockaddr *>(&addr), &addr_len);
        if (addr.ss_family == AF_INET || addr.ss_family == AF_INET6) {
          char ip_addr[INET6_ADDRSTRLEN];
          ats_ip_ntop(reinterpret_cast<sockaddr *>(&addr), ip_addr, INET6_ADDRSTRLEN);
          sni_addr.assign(ip_addr);
        }
      }
    }
  }

  return sni_addr;
}

std::string
get_verify_str(SSL *ssl)
{
  std::string verify_str;

  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  if (netvc != nullptr) {
    std::string policy_str;
    switch (netvc->options.verifyServerPolicy) {
    case YamlSNIConfig::Policy::DISABLED:
      policy_str.assign("DISABLED");
      break;
    case YamlSNIConfig::Policy::PERMISSIVE:
      policy_str.assign("PERMISSIVE");
      break;
    case YamlSNIConfig::Policy::ENFORCED:
      policy_str.assign("ENFORCED");
      break;
    case YamlSNIConfig::Policy::UNSET:
      policy_str.assign("UNSET");
      break;
    }

    std::string property_str;
    switch (netvc->options.verifyServerProperties) {
    case YamlSNIConfig::Property::NONE:
      property_str.assign("NONE");
      break;
    case YamlSNIConfig::Property::SIGNATURE_MASK:
      property_str.assign("SIGNATURE_MASK");
      break;
    case YamlSNIConfig::Property::NAME_MASK:
      property_str.assign("NAME_MASK");
      break;
    case YamlSNIConfig::Property::ALL_MASK:
      property_str.assign("ALL_MASK");
      break;
    case YamlSNIConfig::Property::UNSET:
      property_str.assign("UNSET");
      break;
    }

    swoc::bwprint(verify_str, "{}:{}", policy_str.c_str(), property_str.c_str());
  }

  return verify_str;
}

/**
 * Process the config to pull out the list of file names, and process the certs to get the list
 * of subject and sni names.  Thanks to dual cert configurations, there may be multiple files of each type.
 * If some names are not in all the listed certs they are listed in the uniqe_names map, keyed by the index
 * of the including certificate
 */
bool
SSLMultiCertConfigLoader::load_certs_and_cross_reference_names(
  std::vector<X509 *> &cert_list, SSLMultiCertConfigLoader::CertLoadData &data, const SSLConfigParams *params,
  const SSLMultiCertConfigParams *sslMultCertSettings, std::set<std::string> &common_names,
  std::unordered_map<int, std::set<std::string>> &unique_names, SSLCertContextType *certType)
{
  SimpleTokenizer cert_tok(sslMultCertSettings && sslMultCertSettings->cert ? (const char *)sslMultCertSettings->cert : "",
                           SSL_CERT_SEPARATE_DELIM);

  SimpleTokenizer key_tok(SSL_CERT_SEPARATE_DELIM);
  if (sslMultCertSettings && sslMultCertSettings->key) {
    key_tok.setString((const char *)sslMultCertSettings->key);
  } else {
    key_tok.setString("");
  }

  size_t cert_tok_num = cert_tok.getNumTokensRemaining();
  if (sslMultCertSettings && sslMultCertSettings->key && cert_tok_num != key_tok.getNumTokensRemaining()) {
    Error("the number of certificates in ssl_cert_name (%zu) and ssl_key_name (%zu) do not match", cert_tok_num,
          key_tok.getNumTokensRemaining());
    return false;
  }

  SimpleTokenizer ca_tok("", SSL_CERT_SEPARATE_DELIM);
  if (sslMultCertSettings && sslMultCertSettings->ca) {
    ca_tok.setString(sslMultCertSettings->ca);
    if (cert_tok.getNumTokensRemaining() != ca_tok.getNumTokensRemaining()) {
      Error("the number of certificates in ssl_cert_name (%zu) and ssl_ca_name (%zu) do not match", cert_tok_num,
            ca_tok.getNumTokensRemaining());
      return false;
    }
  }

  SimpleTokenizer ocsp_tok("", SSL_CERT_SEPARATE_DELIM);
  if (sslMultCertSettings && sslMultCertSettings->ocsp_response) {
    ocsp_tok.setString(sslMultCertSettings->ocsp_response);
    if (cert_tok.getNumTokensRemaining() != ocsp_tok.getNumTokensRemaining()) {
      Error("the number of certificates in ssl_cert_name (%zu) and ssl_ocsp_name (%zu) do not match", cert_tok_num,
            ocsp_tok.getNumTokensRemaining());
      return false;
    }
  }

  for (const char *keyname = key_tok.getNext(); keyname; keyname = key_tok.getNext()) {
    std::string completeServerKeyPath = Layout::get()->relative_to(params->serverKeyPathOnly, keyname);
    data.key_list.push_back(completeServerKeyPath);
  }

  for (const char *caname = ca_tok.getNext(); caname; caname = ca_tok.getNext()) {
    data.ca_list.push_back(caname);
  }

  for (const char *ocspname = ocsp_tok.getNext(); ocspname; ocspname = ocsp_tok.getNext()) {
    data.ocsp_list.push_back(ocspname);
  }

  bool first_pass = true;
  int  cert_index = 0;
  for (const char *certname = cert_tok.getNext(); certname; certname = cert_tok.getNext()) {
    std::string completeServerCertPath = Layout::relative_to(params->serverCertPathOnly, certname);
    data.cert_names_list.push_back(completeServerCertPath);
  }

  for (size_t i = 0; i < data.cert_names_list.size(); i++) {
    std::string secret_data;
    std::string secret_key_data;
    params->secrets.getOrLoadSecret(data.cert_names_list[i], data.key_list.size() > i ? data.key_list[i] : "", secret_data,
                                    secret_key_data);
    if (secret_data.empty()) {
      SSLError("failed to load certificate secret for %s with key path %s", data.cert_names_list[i].c_str(),
               data.key_list.size() > i ? data.key_list[i].c_str() : "[empty key path]");
      return false;
    }
    scoped_BIO bio(BIO_new_mem_buf(secret_data.data(), secret_data.size()));
    X509      *cert = nullptr;
    if (bio) {
      cert = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
    } else {
      SSLError("failed to create bio for certificate secret %s of length %ld", data.cert_names_list[i].c_str(), secret_data.size());
      return false;
    }
    if (!bio || !cert) {
      SSLError("failed to load certificate chain from %s of length %ld with key path %s", data.cert_names_list[i].c_str(),
               secret_data.size(), data.key_list.size() > i ? data.key_list[i].c_str() : "[empty key path]");
      return false;
    }

    if (certType != nullptr) {
#ifndef HAVE_NATIVE_DUAL_CERT_SUPPORT
      std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> public_key(X509_get_pubkey(cert), &EVP_PKEY_free);
      int                                                 pkey_id = EVP_PKEY_id(public_key.get());

      switch (pkey_id) {
      case EVP_PKEY_EC:
        *certType = SSLCertContextType::EC;
        break;
      case EVP_PKEY_RSA:
        *certType = SSLCertContextType::RSA;
        break;
      default:
        ink_assert(false);
      }
#else
      *certType = SSLCertContextType::GENERIC;
#endif
      data.cert_type_list.push_back(*certType);
    }

    cert_list.push_back(cert);

    std::set<std::string> name_set;
    // Grub through the names in the certs
    X509_NAME *subject = nullptr;

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

        X509_NAME_ENTRY *e  = X509_NAME_get_entry(subject, pos);
        ASN1_STRING     *cn = X509_NAME_ENTRY_get_data(e);
        subj_name           = asn1_strdup(cn);

        Dbg(dbg_ctl_ssl_load, "subj '%s' in certificate %s %p", subj_name.get(), data.cert_names_list[i].c_str(), cert);
        name_set.insert(subj_name.get());
      }
      if (name_set.empty()) {
        Dbg(dbg_ctl_ssl_load, "no subj name in certificate %s", data.cert_names_list[i].c_str());
      }
    }

    // Traverse the subjectAltNames (if any) and insert additional keys for the SSL context.
    GENERAL_NAMES *names = static_cast<GENERAL_NAMES *>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
    if (names) {
      unsigned count = sk_GENERAL_NAME_num(names);
      for (unsigned i = 0; i < count; ++i) {
        GENERAL_NAME *name;

        name = sk_GENERAL_NAME_value(names, i);
        if (name->type == GEN_DNS) {
          ats_scoped_str dns(asn1_strdup(name->d.dNSName));
          Dbg(dbg_ctl_ssl_load, "inserting dns '%s' in certificate", dns.get());
          name_set.insert(dns.get());
        }
      }
      sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
    }

    if (first_pass) {
      first_pass   = false;
      common_names = name_set;
    } else {
      // Check that all elements in common_names are in name_set
      auto common_iter = common_names.begin();
      while (common_iter != common_names.end()) {
        auto iter = name_set.find(*common_iter);
        if (iter == name_set.end()) {
          // Common_name not in new set, move name to unique set
          auto iter = unique_names.find(cert_index - 1);
          if (iter == unique_names.end()) {
            std::set<std::string> new_set;
            new_set.insert(*common_iter);
            unique_names.insert({cert_index - 1, new_set});
          } else {
            iter->second.insert(*common_iter);
          }
          auto erase_iter = common_iter;
          ++common_iter;
          common_names.erase(erase_iter);
        } else {
          // New name already in common set, go ahead and remove it from further consideration
          name_set.erase(iter);
          ++common_iter;
        }
      }
      // Anything still in name_set was not in common_names
      for (auto const &name : name_set) {
        auto iter = unique_names.find(cert_index);
        if (iter == unique_names.end()) {
          std::set<std::string> new_set;
          new_set.insert(name);
          unique_names.insert({cert_index, new_set});
        } else {
          iter->second.insert(name);
        }
      }
    }
    cert_index++;
  }
  return true;
}

/**
   Load certificates to SSL_CTX
   @static
 */
bool
SSLMultiCertConfigLoader::load_certs(SSL_CTX *ctx, const std::vector<std::string> &cert_names_list,
                                     const std::vector<std::string> &key_list, CertLoadData const &data,
                                     const SSLConfigParams *params, const SSLMultiCertConfigParams *sslMultCertSettings)
{
  if (SSLConfigParams::ssl_ocsp_enabled) {
    Dbg(dbg_ctl_ssl_load, "SSL OCSP Stapling is enabled");
    SSL_CTX_set_tlsext_status_cb(ctx, ssl_callback_ocsp_stapling);
  } else {
    Dbg(dbg_ctl_ssl_load, "SSL OCSP Stapling is disabled");
  }

  ink_assert(!cert_names_list.empty());

  for (size_t i = 0; i < cert_names_list.size(); i++) {
    std::string keyPath = (i < key_list.size()) ? key_list[i] : "";
    std::string secret_data;
    std::string secret_key_data;
    params->secrets.getOrLoadSecret(cert_names_list[i], keyPath, secret_data, secret_key_data);
    if (secret_data.empty()) {
      SSLError("failed to load certificate secret for %s with key path %s", cert_names_list[i].c_str(),
               keyPath.empty() ? "[empty key path]" : keyPath.c_str());
      return false;
    }
    scoped_BIO bio(BIO_new_mem_buf(secret_data.data(), secret_data.size()));
    X509      *cert = nullptr;
    if (bio) {
      cert = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
    } else {
      SSLError("failed to create bio for certificate secret %s of length %ld", data.cert_names_list[i].c_str(), secret_data.size());
      return false;
    }

    if (!bio || !cert) {
      SSLError("failed to load certificate chain from %s", cert_names_list[i].c_str());
      return false;
    }

    Dbg(dbg_ctl_ssl_load, "for ctx=%p, using certificate %s", ctx, cert_names_list[i].c_str());
    if (!SSL_CTX_use_certificate(ctx, cert)) {
      SSLError("Failed to assign cert from %s to SSL_CTX", cert_names_list[i].c_str());
      X509_free(cert);
      return false;
    }

    // Load up any additional chain certificates
    if (!SSL_CTX_add_extra_chain_cert_bio(ctx, bio.get())) {
      Dbg(dbg_ctl_ssl_load, "couldn't add chain to %p", ctx);
      SSLError("failed to load intermediate certificate chain from %s", cert_names_list[i].c_str());
      return false;
    }

    if (secret_key_data.empty()) {
      Dbg(dbg_ctl_ssl_load, "empty private key for public key %s", cert_names_list[i].c_str());
      secret_key_data = std::move(secret_data);
    }
    if (!SSLPrivateKeyHandler(ctx, keyPath.c_str(), secret_key_data.data(), secret_key_data.size())) {
      SSLError("failed to load certificate: %s of length %ld with key path: %s", cert_names_list[i].c_str(), secret_key_data.size(),
               keyPath.empty() ? "[empty key path]" : keyPath.c_str());
      return false;
    }
    // Must load all the intermediate certificates before starting the next chain

    // First, load any CA chains from the global chain file.  This should probably
    // eventually be a comma separated list too.  For now we will load it in all chains even
    // though it only makes sense in one chain
    if (params->serverCertChainFilename) {
      std::string completeServerCertChainPath(Layout::relative_to(params->serverCertPathOnly, params->serverCertChainFilename));
      if (!SSL_CTX_add_extra_chain_cert_file(ctx, completeServerCertChainPath.c_str())) {
        SSLError("failed to load global certificate chain from %s", completeServerCertChainPath.c_str());
        return false;
      }
      if (SSLConfigParams::load_ssl_file_cb) {
        SSLConfigParams::load_ssl_file_cb(completeServerCertChainPath.c_str());
      }
    }

    // Now, load any additional certificate chains specified in this entry.
    if (sslMultCertSettings->ca) {
      const char *ca_name = data.ca_list[i].c_str();
      if (ca_name != nullptr) {
        std::string completeServerCertChainPath(Layout::relative_to(params->serverCertPathOnly, ca_name));
        if (!SSL_CTX_add_extra_chain_cert_file(ctx, completeServerCertChainPath.c_str())) {
          SSLError("failed to load certificate chain from %s", completeServerCertChainPath.c_str());
          return false;
        }
        if (SSLConfigParams::load_ssl_file_cb) {
          SSLConfigParams::load_ssl_file_cb(completeServerCertChainPath.c_str());
        }
      }
    }
    if (SSLConfigParams::ssl_ocsp_enabled) {
      if (sslMultCertSettings->ocsp_response) {
        const char *ocsp_response_name = data.ocsp_list[i].c_str();
        std::string completeOCSPResponsePath(Layout::relative_to(params->ssl_ocsp_response_path_only, ocsp_response_name));
        if (!ssl_stapling_init_cert(ctx, cert, cert_names_list[i].c_str(), completeOCSPResponsePath.c_str())) {
          Warning("failed to configure SSL_CTX for OCSP Stapling info for certificate at %s", cert_names_list[i].c_str());
        }
      } else {
        if (!ssl_stapling_init_cert(ctx, cert, cert_names_list[i].c_str(), nullptr)) {
          Warning("failed to configure SSL_CTX for OCSP Stapling info for certificate at %s", cert_names_list[i].c_str());
        }
      }
    }
    X509_free(cert);
  }
  return true;
}

/**
    Set session_id context for session reuse
    @static
 */
bool
SSLMultiCertConfigLoader::set_session_id_context(SSL_CTX *ctx, const SSLConfigParams *params,
                                                 const SSLMultiCertConfigParams *sslMultCertSettings)
{
  EVP_MD_CTX *digest           = EVP_MD_CTX_new();
  STACK_OF(X509_NAME) *ca_list = nullptr;
  unsigned char hash_buf[EVP_MAX_MD_SIZE];
  unsigned int  hash_len     = 0;
  const char   *setting_cert = sslMultCertSettings ? sslMultCertSettings->cert.get() : nullptr;
  bool          result       = false;

  if (params->serverCACertFilename) {
    ca_list = SSL_load_client_CA_file(params->serverCACertFilename);
  }

  if (EVP_DigestInit_ex(digest, evp_md_func, nullptr) == 0) {
    SSLError("EVP_DigestInit_ex failed");
    goto fail;
  }

  if (nullptr != setting_cert) {
    Dbg(dbg_ctl_ssl_load, "Using '%s' in hash for session id context", sslMultCertSettings->cert.get());
    if (EVP_DigestUpdate(digest, sslMultCertSettings->cert, strlen(setting_cert)) == 0) {
      SSLError("EVP_DigestUpdate failed using '%s' in hash for session id context", sslMultCertSettings->cert.get());
      goto fail;
    }
  }

  if (ca_list != nullptr) {
    size_t num_certs = sk_X509_NAME_num(ca_list);

    for (size_t i = 0; i < num_certs; i++) {
      X509_NAME *name = sk_X509_NAME_value(ca_list, i);
      if (X509_NAME_digest(name, evp_md_func, hash_buf /* borrow our final hash buffer. */, &hash_len) == 0 ||
          EVP_DigestUpdate(digest, hash_buf, hash_len) == 0) {
        SSLError("Adding X509 name to digest failed using '%s' in hash for session id context", hash_buf);
        goto fail;
      }
    }

    // Set the list of CA's to send to client if we ask for a client certificate
    SSL_CTX_set_client_CA_list(ctx, ca_list);
  }

  if (EVP_DigestFinal_ex(digest, hash_buf, &hash_len) == 0) {
    SSLError("EVP_DigestFinal_ex failed using '%s' in hash for session id context", hash_buf);
    goto fail;
  }

  if (SSL_CTX_set_session_id_context(ctx, hash_buf, hash_len) == 0) {
    SSLError("SSL_CTX_set_session_id_context failed using '%s' in hash for session id context", hash_buf);
    goto fail;
  }

  result = true;

fail:
  EVP_MD_CTX_free(digest);

  return result;
}

const char *
SSLMultiCertConfigLoader::_debug_tag() const
{
  return "ssl";
}

const DbgCtl &
SSLMultiCertConfigLoader::_dbg_ctl() const
{
  static DbgCtl dc{_debug_tag()};
  return dc;
}

/**
   Clear password in SSL_CTX
   @static
 */
void
SSLMultiCertConfigLoader::clear_pw_references(SSL_CTX *ssl_ctx)
{
  Dbg(dbg_ctl_ssl_load, "clearing pw preferences");
  SSL_CTX_set_default_passwd_cb(ssl_ctx, nullptr);
  SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, nullptr);
}

ssl_curve_id
SSLGetCurveNID(SSL *ssl)
{
#if HAVE_SSL_GET_SHARED_CURVE
  return SSL_get_shared_curve(ssl, 0);
#else
  return SSL_get_curve_id(ssl);
#endif
}

SSL_SESSION *
SSLSessionDup(SSL_SESSION *sess)
{
#ifdef HAVE_SSL_SESSION_DUP
  return SSL_SESSION_dup(sess);
#else
  SSL_SESSION *duplicated = nullptr;
  int          len        = i2d_SSL_SESSION(sess, nullptr);
  if (len < 0) {
    return nullptr;
  }
  uint8_t *buf = static_cast<uint8_t *>(alloca(len));
  uint8_t *tmp = buf;

  i2d_SSL_SESSION(sess, &tmp);
  tmp = buf;
  if (d2i_SSL_SESSION(&duplicated, const_cast<const uint8_t **>(&tmp), len) == nullptr) {
    return nullptr;
  }

  return duplicated;
#endif
}
