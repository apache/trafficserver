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

#include "ink_config.h"
#include "libts.h"
#include "I_Layout.h"
#include "P_Net.h"
#include "ink_cap.h"

#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/asn1.h>
#include <openssl/rand.h>

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
#define SSL_IP_TAG            "dest_ip"
#define SSL_CERT_TAG          "ssl_cert_name"
#define SSL_PRIVATE_KEY_TAG   "ssl_key_name"
#define SSL_CA_TAG            "ssl_ca_name"
#define SSL_SESSION_TICKET_ENABLED "ssl_ticket_enabled"
#define SSL_SESSION_TICKET_KEY_FILE_TAG "ticket_key_name"

#ifndef evp_md_func
#ifdef OPENSSL_NO_SHA256
#define evp_md_func EVP_sha1()
#else
#define evp_md_func EVP_sha256()
#endif
#endif

#if (OPENSSL_VERSION_NUMBER >= 0x10000000L) // openssl returns a const SSL_METHOD
typedef const SSL_METHOD * ink_ssl_method_t;
#else
typedef SSL_METHOD * ink_ssl_method_t;
#endif

// Check if the ticket_key callback #define is available, and if so, enable session tickets.
#ifdef SSL_CTX_set_tlsext_ticket_key_cb
#  define HAVE_OPENSSL_SESSION_TICKETS 1
   static int ssl_callback_session_ticket(SSL *, unsigned char *, unsigned char *, EVP_CIPHER_CTX *, HMAC_CTX *, int);
#endif /* SSL_CTX_set_tlsext_ticket_key_cb */

struct ssl_ticket_key_t
{
  unsigned char key_name[16];
  unsigned char hmac_secret[16];
  unsigned char aes_key[16];
};

static int ssl_session_ticket_index = 0;
static pthread_mutex_t *mutex_buf = NULL;
static bool open_ssl_initialized = false;

struct ats_file_bio
{
  ats_file_bio(const char * path, const char * mode)
    : bio(BIO_new_file(path, mode)) {
  }

  ~ats_file_bio() {
    (void)BIO_set_close(bio, BIO_CLOSE);
    BIO_free(bio);
  }

  operator bool() const {
    return bio != NULL;
  }

  BIO * bio;

private:
  ats_file_bio(const ats_file_bio&);
  ats_file_bio& operator=(const ats_file_bio&);
};

/* Using pthread thread ID and mutex functions directly, instead of
 * ATS this_ethread / ProxyMutex, so that other linked libraries
 * may use pthreads and openssl without confusing us here. (TS-2271).
 */

static unsigned long
SSL_pthreads_thread_id()
{
  return (unsigned long)pthread_self();
}

static void
SSL_locking_callback(int mode, int type, const char * /* file ATS_UNUSED */, int /* line ATS_UNUSED */)
{
  ink_assert(type < CRYPTO_num_locks());

  if (mode & CRYPTO_LOCK) {
    pthread_mutex_lock(&mutex_buf[type]);
  } else if (mode & CRYPTO_UNLOCK) {
    pthread_mutex_unlock(&mutex_buf[type]);
  } else {
    Debug("ssl", "invalid SSL locking mode 0x%x", mode);
    ink_assert(0);
  }
}

static bool
SSL_CTX_add_extra_chain_cert_file(SSL_CTX * ctx, const char * chainfile)
{
  X509 *cert;
  ats_file_bio bio(chainfile, "r");

  if (!bio) {
    return false;
  }

  for (;;) {
    cert = PEM_read_bio_X509_AUX(bio.bio, NULL, NULL, NULL);

    if (!cert) {
      // No more the certificates in this file.
      break;
    }

    // This transfers ownership of the cert (X509) to the SSL context, if successful.
    if (!SSL_CTX_add_extra_chain_cert(ctx, cert)) {
      X509_free(cert);
      return false;
    }
  }

  return true;
}

#if TS_USE_TLS_SNI

static int
ssl_servername_callback(SSL * ssl, int * ad, void * arg)
{
  SSL_CTX *           ctx = NULL;
  SSLCertLookup *     lookup = (SSLCertLookup *) arg;
  const char *        servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  SSLNetVConnection * netvc = (SSLNetVConnection *)SSL_get_app_data(ssl);

  Debug("ssl", "ssl_servername_callback ssl=%p ad=%d lookup=%p server=%s handshake_complete=%d", ssl, *ad, lookup, servername,
    netvc->getSSLHandShakeComplete());

  // catch the client renegotiation early on
  if (SSLConfigParams::ssl_allow_client_renegotiation == false && netvc->getSSLHandShakeComplete()) {
    Debug("ssl", "ssl_servername_callback trying to renegotiate from the client");
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  // The incoming SSL_CTX is either the one mapped from the inbound IP address or the default one. If we
  // don't find a name-based match at this point, we *do not* want to mess with the context because we've
  // already made a best effort to find the best match.
  if (likely(servername)) {
    ctx = lookup->findInfoInHash((char *)servername);
  }

  // If there's no match on the server name, try to match on the peer address.
  if (ctx == NULL) {
    IpEndpoint ip;
    int namelen = sizeof(ip);

    safe_getsockname(netvc->get_socket(), &ip.sa, &namelen);
    ctx = lookup->findInfoInHash(ip);
  }

  if (ctx != NULL) {
    SSL_set_SSL_CTX(ssl, ctx);
  }

  ctx = SSL_get_SSL_CTX(ssl);
  Debug("ssl", "ssl_servername_callback found SSL context %p for requested name '%s'", ctx, servername);

  if (ctx == NULL) {
    return SSL_TLSEXT_ERR_NOACK;
  }

  // We need to return one of the SSL_TLSEXT_ERR constants. If we return an
  // error, we can fill in *ad with an alert code to propgate to the
  // client, see SSL_AD_*.
  return SSL_TLSEXT_ERR_OK;
}

#endif /* TS_USE_TLS_SNI */

static SSL_CTX *
ssl_context_enable_sni(SSL_CTX * ctx, SSLCertLookup * lookup)
{
#if TS_USE_TLS_SNI
  if (ctx) {
    Debug("ssl", "setting SNI callbacks with for ctx %p", ctx);
    SSL_CTX_set_tlsext_servername_callback(ctx, ssl_servername_callback);
    SSL_CTX_set_tlsext_servername_arg(ctx, lookup);
  }
#else
  (void)lookup;
#endif /* TS_USE_TLS_SNI */

  return ctx;
}

static SSL_CTX *
ssl_context_enable_ecdh(SSL_CTX * ctx)
{
#if TS_USE_TLS_ECKEY

#if defined(SSL_CTRL_SET_ECDH_AUTO)
  SSL_CTX_set_ecdh_auto(ctx, 1);
#elif defined(HAVE_EC_KEY_NEW_BY_CURVE_NAME) && defined(NID_X9_62_prime256v1)
  EC_KEY * ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);

  if (ecdh) {
    SSL_CTX_set_tmp_ecdh(ctx, ecdh);
    EC_KEY_free(ecdh);
  }
#endif
#endif

  return ctx;
}

static SSL_CTX *
ssl_context_enable_tickets(SSL_CTX * ctx, const char * ticket_key_path)
{
#if HAVE_OPENSSL_SESSION_TICKETS
  xptr<char>          ticket_key_data;
  int                 ticket_key_len;
  ssl_ticket_key_t *  ticket_key = NULL;

  ticket_key_data = readIntoBuffer(ticket_key_path, __func__, &ticket_key_len);
  if (!ticket_key_data) {
    Error("failed to read SSL session ticket key from %s", (const char *)ticket_key_path);
    goto fail;
  }

  if (ticket_key_len < 48) {
    Error("SSL session ticket key from %s is too short (48 bytes are required)", (const char *)ticket_key_path);
    goto fail;
  }

  ticket_key = NEW(new ssl_ticket_key_t());
  memcpy(ticket_key->key_name, (const char *)ticket_key_data, 16);
  memcpy(ticket_key->hmac_secret, (const char *)ticket_key_data + 16, 16);
  memcpy(ticket_key->aes_key, (const char *)ticket_key_data + 32, 16);

  // Setting the callback can only fail if OpenSSL does not recognize the
  // SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB constant. we set the callback first
  // so that we don't leave a ticket_key pointer attached if it fails.
  if (SSL_CTX_set_tlsext_ticket_key_cb(ctx, ssl_callback_session_ticket) == 0) {
    Error("failed to set session ticket callback");
    goto fail;
  }

  if (SSL_CTX_set_ex_data(ctx, ssl_session_ticket_index, ticket_key) == 0) {
    Error ("failed to set session ticket data to ctx");
    goto fail;
  }

  SSL_CTX_clear_options(ctx, SSL_OP_NO_TICKET);
  return ctx;

fail:
  delete ticket_key;
  return ctx;

#else /* !HAVE_OPENSSL_SESSION_TICKETS */
  (void)ticket_key_path;
  return ctx;
#endif /* HAVE_OPENSSL_SESSION_TICKETS */
}

void
SSLInitializeLibrary()
{
  if (!open_ssl_initialized) {
    CRYPTO_set_mem_functions(ats_malloc, ats_realloc, ats_free);

    SSL_load_error_strings();
    SSL_library_init();

    mutex_buf = (pthread_mutex_t *) OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));

    for (int i = 0; i < CRYPTO_num_locks(); i++) {
      pthread_mutex_init(&mutex_buf[i], NULL);
    }

    CRYPTO_set_locking_callback(SSL_locking_callback);
    CRYPTO_set_id_callback(SSL_pthreads_thread_id);
  }

  int iRet = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);
  if (iRet == -1) {
    SSLError("failed to create session ticket index");
  }
  ssl_session_ticket_index = (iRet == -1 ? 0 : iRet);

  open_ssl_initialized = true;
}

void
SSLDiagnostic(const SrcLoc& loc, bool debug, const char * fmt, ...)
{
  unsigned long l;
  char buf[256];
  const char *file, *data;
  int line, flags;
  unsigned long es;

  va_list ap;

  es = CRYPTO_thread_id();
  while ((l = ERR_get_error_line_data(&file, &line, &data, &flags)) != 0) {
    if (debug) {
      if (unlikely(diags->on())) {
        diags->log("ssl", DL_Debug, loc.file, loc.func, loc.line,
            "SSL::%lu:%s:%s:%d%s%s", es, ERR_error_string(l, buf), file, line,
          (flags & ERR_TXT_STRING) ? ":" : "", (flags & ERR_TXT_STRING) ? data : "");
      }
    } else {
      diags->error(DL_Error, loc.file, loc.func, loc.line,
          "SSL::%lu:%s:%s:%d%s%s", es, ERR_error_string(l, buf), file, line,
          (flags & ERR_TXT_STRING) ? ":" : "", (flags & ERR_TXT_STRING) ? data : "");
    }
  }

  va_start(ap, fmt);
  if (debug) {
    diags->log_va("ssl", DL_Debug, &loc, fmt, ap);
  } else {
    diags->error_va(DL_Error, loc.file, loc.func, loc.line, fmt, ap);
  }
  va_end(ap);

}

const char *
SSLErrorName(int ssl_error)
{
  static const char * names[] =  {
    "SSL_ERROR_NONE",
    "SSL_ERROR_SSL",
    "SSL_ERROR_WANT_READ",
    "SSL_ERROR_WANT_WRITE",
    "SSL_ERROR_WANT_X509_LOOKUP",
    "SSL_ERROR_SYSCALL",
    "SSL_ERROR_ZERO_RETURN",
    "SSL_ERROR_WANT_CONNECT",
    "SSL_ERROR_WANT_ACCEPT"
  };

  if (ssl_error < 0 || ssl_error >= (int)countof(names)) {
    return "unknown SSL error";
  }

  return names[ssl_error];
}

void
SSLDebugBufferPrint(const char * tag, const char * buffer, unsigned buflen, const char * message)
{
  if (is_debug_tag_set(tag)) {
    if (message != NULL) {
      fprintf(stdout, "%s\n", message);
    }
    for (unsigned ii = 0; ii < buflen; ii++) {
      putc(buffer[ii], stdout);
    }
    putc('\n', stdout);
  }
}

SSL_CTX *
SSLDefaultServerContext()
{
  ink_ssl_method_t meth = NULL;

  meth = SSLv23_server_method();
  return SSL_CTX_new(meth);
}

SSL_CTX *
SSLInitServerContext(
    const SSLConfigParams * params,
    const char * serverCertPtr,
    const char * serverCaCertPtr,
    const char * serverKeyPtr)
{
  int         session_id_context;
  int         server_verify_client;
  xptr<char>  completeServerCertPath;
  SSL_CTX *   ctx = SSLDefaultServerContext();

  // disable selected protocols
  SSL_CTX_set_options(ctx, params->ssl_ctx_options);

  switch (params->ssl_session_cache) {
  case SSLConfigParams::SSL_SESSION_CACHE_MODE_OFF:
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF|SSL_SESS_CACHE_NO_INTERNAL);
    break;
  case SSLConfigParams::SSL_SESSION_CACHE_MODE_SERVER:
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);
    SSL_CTX_sess_set_cache_size(ctx, params->ssl_session_cache_size);
    if (params->ssl_session_cache_timeout) {
        SSL_CTX_set_timeout(ctx, params->ssl_session_cache_timeout);
    }
    break;
  }

#ifdef SSL_MODE_RELEASE_BUFFERS
  if (OPENSSL_VERSION_NUMBER > 0x1000107fL) {
    Debug("ssl", "enabling SSL_MODE_RELEASE_BUFFERS");
    SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS);
  }
#endif
  SSL_CTX_set_quiet_shutdown(ctx, 1);

  // if serverCertPtr == NULL, then we are initing the default context so skip server cert init
  if (serverCertPtr) {
    // XXX OpenSSL recommends that we should use SSL_CTX_use_certificate_chain_file() here. That API
    // also loads only the first certificate, but it allows the intermediate CA certificate chain to
    // be in the same file. SSL_CTX_use_certificate_chain_file() was added in OpenSSL 0.9.3.
    completeServerCertPath = Layout::relative_to(params->serverCertPathOnly, serverCertPtr);
    if (!SSL_CTX_use_certificate_file(ctx, completeServerCertPath, SSL_FILETYPE_PEM)) {
      SSLError("failed to load certificate from %s", (const char *)completeServerCertPath);
      goto fail;
    }

    // First, load any CA chains from the global chain file.
    if (params->serverCertChainFilename) {
      xptr<char> completeServerCertChainPath(Layout::relative_to(params->serverCertPathOnly, params->serverCertChainFilename));
      if (!SSL_CTX_add_extra_chain_cert_file(ctx, completeServerCertChainPath)) {
        SSLError("failed to load global certificate chain from %s", (const char *)completeServerCertChainPath);
        goto fail;
      }
    }

    // Now, load any additional certificate chains specified in this entry.
    if (serverCaCertPtr) {
      xptr<char> completeServerCertChainPath(Layout::relative_to(params->serverCertPathOnly, serverCaCertPtr));
      if (!SSL_CTX_add_extra_chain_cert_file(ctx, completeServerCertChainPath)) {
        SSLError("failed to load certificate chain from %s", (const char *)completeServerCertChainPath);
        goto fail;
      }
    }

    if (serverKeyPtr == NULL) {
      // assume private key is contained in cert obtained from multicert file.
      if (!SSL_CTX_use_PrivateKey_file(ctx, completeServerCertPath, SSL_FILETYPE_PEM)) {
        SSLError("failed to load server private key from %s", (const char *)completeServerCertPath);
        goto fail;
      }
    } else if (params->serverKeyPathOnly != NULL) {
      xptr<char> completeServerKeyPath(Layout::get()->relative_to(params->serverKeyPathOnly, serverKeyPtr));
      if (!SSL_CTX_use_PrivateKey_file(ctx, completeServerKeyPath, SSL_FILETYPE_PEM)) {
        SSLError("failed to load server private key from %s", (const char *)completeServerKeyPath);
        goto fail;
      }
    } else {
      SSLError("empty SSL private key path in records.config");
    }

    if (!SSL_CTX_check_private_key(ctx)) {
      SSLError("server private key does not match the certificate public key");
      goto fail;
    }
  }
  if (params->clientCertLevel != 0) {

    if (params->serverCACertFilename != NULL && params->serverCACertPath != NULL) {
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

    // XXX I really don't think that this is a good idea. We should be setting this a some finer granularity,
    // possibly per SSL CTX. httpd uses md5(host:port), which seems reasonable.
    session_id_context = 1;
    SSL_CTX_set_session_id_context(ctx, (const unsigned char *) &session_id_context, sizeof(session_id_context));

    SSL_CTX_set_verify(ctx, server_verify_client, NULL);
    SSL_CTX_set_verify_depth(ctx, params->verify_depth); // might want to make configurable at some point.

    SSL_CTX_set_client_CA_list(ctx, SSL_load_client_CA_file(params->serverCACertFilename));
  }

  if (params->cipherSuite != NULL) {
    if (!SSL_CTX_set_cipher_list(ctx, params->cipherSuite)) {
      SSLError("invalid cipher suite in records.config");
      goto fail;
    }
  }

  return ssl_context_enable_ecdh(ctx);

fail:
  SSL_CTX_free(ctx);
  return NULL;
}

SSL_CTX *
SSLInitClientContext(const SSLConfigParams * params)
{
  ink_ssl_method_t meth = NULL;
  SSL_CTX * client_ctx = NULL;
  char * clientKeyPtr = NULL;

  // Note that we do not call RAND_seed() explicitly here, we depend on OpenSSL
  // to do the seeding of the PRNG for us. This is the case for all platforms that
  // has /dev/urandom for example.

  meth = SSLv23_client_method();
  client_ctx = SSL_CTX_new(meth);

  // disable selected protocols
  SSL_CTX_set_options(client_ctx, params->ssl_ctx_options);
  if (!client_ctx) {
    SSLError("cannot create new client context");
    return NULL;
  }

  // if no path is given for the client private key,
  // assume it is contained in the client certificate file.
  clientKeyPtr = params->clientKeyPath;
  if (clientKeyPtr == NULL) {
    clientKeyPtr = params->clientCertPath;
  }

  if (params->clientCertPath != 0) {
    if (!SSL_CTX_use_certificate_file(client_ctx, params->clientCertPath, SSL_FILETYPE_PEM)) {
      SSLError("failed to load client certificate from %s", params->clientCertPath);
      goto fail;
    }

    if (!SSL_CTX_use_PrivateKey_file(client_ctx, clientKeyPtr, SSL_FILETYPE_PEM)) {
      SSLError("failed to load client private key file from %s", clientKeyPtr);
      goto fail;
    }

    if (!SSL_CTX_check_private_key(client_ctx)) {
      SSLError("client private key (%s) does not match the certificate public key (%s)",
          clientKeyPtr, params->clientCertPath);
      goto fail;
    }
  }

  if (params->clientVerify) {
    int client_verify_server;

    client_verify_server = params->clientVerify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
    SSL_CTX_set_verify(client_ctx, client_verify_server, NULL);
    SSL_CTX_set_verify_depth(client_ctx, params->client_verify_depth);

    if (params->clientCACertFilename != NULL && params->clientCACertPath != NULL) {
      if ((!SSL_CTX_load_verify_locations(client_ctx, params->clientCACertFilename, params->clientCACertPath)) ||
          (!SSL_CTX_set_default_verify_paths(client_ctx))) {
        SSLError("invalid client CA Certificate file (%s) or CA Certificate path (%s)",
            params->clientCACertFilename, params->clientCACertPath);
        goto fail;
      }
    }
  }

  return client_ctx;

fail:
  SSL_CTX_free(client_ctx);
  return NULL;
}

static char *
asn1_strdup(ASN1_STRING * s)
{
  // Make sure we have an 8-bit encoding.
  ink_assert(ASN1_STRING_type(s) == V_ASN1_IA5STRING ||
    ASN1_STRING_type(s) == V_ASN1_UTF8STRING ||
    ASN1_STRING_type(s) == V_ASN1_PRINTABLESTRING ||
    ASN1_STRING_type(s) == V_ASN1_T61STRING);

  return ats_strndup((const char *)ASN1_STRING_data(s), ASN1_STRING_length(s));
}

// Given a certificate and it's corresponding SSL_CTX context, insert hash
// table aliases for subject CN and subjectAltNames DNS without wildcard,
// insert trie aliases for those with wildcard.
static void
ssl_index_certificate(SSLCertLookup * lookup, SSL_CTX * ctx, const char * certfile)
{
  X509_NAME *   subject = NULL;
  X509 *        cert;
  ats_file_bio  bio(certfile, "r");

  cert = PEM_read_bio_X509_AUX(bio.bio, NULL, NULL, NULL);
  if (NULL == cert) {
    return;
  }

  // Insert a key for the subject CN.
  subject = X509_get_subject_name(cert);
  if (subject) {
    int pos = -1;
    for (;;) {
      pos = X509_NAME_get_index_by_NID(subject, NID_commonName, pos);
      if (pos == -1) {
        break;
      }

      X509_NAME_ENTRY * e = X509_NAME_get_entry(subject, pos);
      ASN1_STRING * cn = X509_NAME_ENTRY_get_data(e);
      xptr<char> name(asn1_strdup(cn));

      Debug("ssl", "mapping '%s' to certificate %s", (const char *) name, certfile);
      lookup->insert(ctx, name);
    }
  }

#if HAVE_OPENSSL_TS_H
  // Traverse the subjectAltNames (if any) and insert additional keys for the SSL context.
  GENERAL_NAMES * names = (GENERAL_NAMES *) X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
  if (names) {
    unsigned count = sk_GENERAL_NAME_num(names);
    for (unsigned i = 0; i < count; ++i) {
      GENERAL_NAME * name;

      name = sk_GENERAL_NAME_value(names, i);
      if (name->type == GEN_DNS) {
        xptr<char> dns(asn1_strdup(name->d.dNSName));
        Debug("ssl", "mapping '%s' to certificate %s", (const char *) dns, certfile);
        lookup->insert(ctx, dns);
      }
    }

    GENERAL_NAMES_free(names);
  }
#endif // HAVE_OPENSSL_TS_H
  X509_free(cert);
}

// This callback function is executed while OpenSSL processes the SSL
// handshake and does SSL record layer stuff.  It's used to trap
// client-initiated renegotiations
static void
ssl_callback_info(const SSL *ssl, int where, int ret)
{
  Debug("ssl", "ssl_callback_info ssl: %p where: %d ret: %d", ssl, where, ret);
  SSLNetVConnection * netvc = (SSLNetVConnection *)SSL_get_app_data(ssl);

  if ((where & SSL_CB_ACCEPT_LOOP) && netvc->getSSLHandShakeComplete() == true &&
      SSLConfigParams::ssl_allow_client_renegotiation == false) {
    int state = SSL_get_state(ssl);

    if (state == SSL3_ST_SR_CLNT_HELLO_A || state == SSL23_ST_SR_CLNT_HELLO_A) {
      netvc->setSSLClientRenegotiationAbort(true);
      Debug("ssl", "ssl_callback_info trying to renegotiate from the client");
    }
  }
}

static bool
ssl_store_ssl_context(
    const SSLConfigParams * params,
    SSLCertLookup *         lookup,
    xptr<char>& addr,
    xptr<char>& cert,
    xptr<char>& ca,
    xptr<char>& key,
    const int session_ticket_enabled,
    xptr<char>& ticket_key_filename)
{
  SSL_CTX *   ctx;
  xptr<char>  certpath;
  xptr<char>  session_key_path;

  ctx = ssl_context_enable_sni(SSLInitServerContext(params, cert, ca, key), lookup);
  if (!ctx) {
    return false;
  }

  SSL_CTX_set_info_callback(ctx, ssl_callback_info);

#if TS_USE_TLS_NPN
  SSL_CTX_set_next_protos_advertised_cb(ctx, SSLNetVConnection::advertise_next_protocol, NULL);
#endif /* TS_USE_TLS_NPN */

  certpath = Layout::relative_to(params->serverCertPathOnly, cert);

  // Index this certificate by the specified IP(v6) address. If the address is "*", make it the default context.
  if (addr) {
    if (strcmp(addr, "*") == 0) {
      lookup->ssl_default = ctx;
      lookup->insert(ctx, addr);
    } else {
      IpEndpoint ep;

      if (ats_ip_pton(addr, &ep) == 0) {
        Debug("ssl", "mapping '%s' to certificate %s", (const char *)addr, (const char *)certpath);
        lookup->insert(ctx, ep);
      } else {
        Error("'%s' is not a valid IPv4 or IPv6 address", (const char *)addr);
      }
    }
  }

#if defined(SSL_OP_NO_TICKET)
  // Session tickets are enabled by default. Disable if explicitly requested.
  if (session_ticket_enabled == 0) {
    SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET);
    Debug("ssl", "ssl session ticket is disabled");
  }
#endif

  // Load the session ticket key if session tickets are not disabled and we have key name.
  if (session_ticket_enabled != 0 && ticket_key_filename) {
    xptr<char> ticket_key_path(Layout::relative_to(params->serverCertPathOnly, ticket_key_filename));
    ssl_context_enable_tickets(ctx, ticket_key_path);
  }

  // Insert additional mappings. Note that this maps multiple keys to the same value, so when
  // this code is updated to reconfigure the SSL certificates, it will need some sort of
  // refcounting or alternate way of avoiding double frees.
  Debug("ssl", "importing SNI names from %s", (const char *)certpath);
  ssl_index_certificate(lookup, ctx, certpath);

  return true;
}

static bool
ssl_extract_certificate(
    const matcher_line * line_info,
    xptr<char>& addr,   // IPv[64] address to match
    xptr<char>& cert,   // certificate
    xptr<char>& ca,     // CA public certificate
    xptr<char>& key,    // Private key
    int&  session_ticket_enabled,  // session ticket enabled
    xptr<char>& ticket_key_filename) // session key file. [key_name (16Byte) + HMAC_secret (16Byte) + AES_key (16Byte)]
{
  for (int i = 0; i < MATCHER_MAX_TOKENS; ++i) {
    const char * label;
    const char * value;

    label = line_info->line[0][i];
    value = line_info->line[1][i];

    if (label == NULL) {
      continue;
    }

    if (strcasecmp(label, SSL_IP_TAG) == 0) {
      addr = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_CERT_TAG) == 0) {
      cert = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_CA_TAG) == 0) {
      ca = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_PRIVATE_KEY_TAG) == 0) {
      key = ats_strdup(value);
    }

    if (strcasecmp(label, SSL_SESSION_TICKET_ENABLED) == 0) {
      session_ticket_enabled = atoi(value);
    }

    if (strcasecmp(label, SSL_SESSION_TICKET_KEY_FILE_TAG) == 0) {
      ticket_key_filename = ats_strdup(value);
    }
  }

  if (!cert) {
    Error("missing %s tag", SSL_CERT_TAG);
    return false;
  }

  return true;
}

bool
SSLParseCertificateConfiguration(
    const SSLConfigParams * params,
    SSLCertLookup *         lookup)
{
  char *      tok_state = NULL;
  char *      line = NULL;
  xptr<char>  file_buf;
  unsigned    line_num = 0;
  matcher_line line_info;

  bool alarmAlready = false;
  char errBuf[1024];

  const matcher_tags sslCertTags = {
    NULL, NULL, NULL, NULL, NULL, NULL, false
  };

  Note("loading SSL certificate configuration from %s", params->configFilePath);

  if (params->configFilePath) {
    file_buf = readIntoBuffer(params->configFilePath, __func__, NULL);
  }

  if (!file_buf) {
    Error("failed to read SSL certificate configuration from %s", params->configFilePath);
    return false;
  }

  // elevate/allow file access to root read only files/certs
  uint32_t elevate_setting = 0;
  REC_ReadConfigInteger(elevate_setting, "proxy.config.ssl.cert.load_elevated");
  ElevateAccess elevate_access(elevate_setting != 0); // destructor will demote for us

  line = tokLine(file_buf, &tok_state);
  while (line != NULL) {

    line_num++;

    // skip all blank spaces at beginning of line
    while (*line && isspace(*line)) {
      line++;
    }

    if (*line != '\0' && *line != '#') {
      xptr<char> addr;
      xptr<char> cert;
      xptr<char> ca;
      xptr<char> key;
      int session_ticket_enabled = -1;
      xptr<char> ticket_key_filename;
      const char * errPtr;

      errPtr = parseConfigLine(line, &line_info, &sslCertTags);

      if (errPtr != NULL) {
        snprintf(errBuf, sizeof(errBuf), "%s: discarding %s entry at line %d: %s",
                     __func__, params->configFilePath, line_num, errPtr);
        REC_SignalError(errBuf, alarmAlready);
      } else {
        if (ssl_extract_certificate(&line_info, addr, cert, ca, key, session_ticket_enabled, ticket_key_filename)) {
          if (!ssl_store_ssl_context(params, lookup, addr, cert, ca, key, session_ticket_enabled, ticket_key_filename)) {
            Error("failed to load SSL certificate specification from %s line %u",
                params->configFilePath, line_num);
          }
        } else {
          snprintf(errBuf, sizeof(errBuf), "%s: discarding invalid %s entry at line %u",
                       __func__, params->configFilePath, line_num);
          REC_SignalError(errBuf, alarmAlready);
        }
      }

    }

    line = tokLine(NULL, &tok_state);
  }

  // We *must* have a default context even if it can't possibly work. The default context is used to
  // bootstrap the SSL handshake so that we can subsequently do the SNI lookup to switch to the real
  // context.
  if (lookup->ssl_default == NULL) {
    xptr<char> addr;
    xptr<char> cert;
    xptr<char> ca;
    xptr<char> key;
    int session_ticket_enabled = -1;
    xptr<char> ticket_key_filename;

    addr = ats_strdup("*");
    if (!ssl_store_ssl_context(params, lookup, addr, cert, ca, key, session_ticket_enabled, ticket_key_filename)) {
      Error("failed to store default ctx ");
      return false;
    }
  }

  return true;
}

#if HAVE_OPENSSL_SESSION_TICKETS
/*
 * RFC 5077. Create session ticket to resume SSL session without requiring session-specific state at the TLS server.
 * Specifically, it distributes the encrypted session-state information to the client in the form of a ticket and
 * a mechanism to present the ticket back to the server.
 * */
static int
ssl_callback_session_ticket(
    SSL * ssl,
    unsigned char * keyname,
    unsigned char * iv,
    EVP_CIPHER_CTX * cipher_ctx,
    HMAC_CTX * hctx,
    int enc)
{
  ssl_ticket_key_t* ssl_ticket_key = (ssl_ticket_key_t*) SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), ssl_session_ticket_index);

  if (NULL == ssl_ticket_key) {
    Error("ssl ticket key is null.");
    return -1;
  }

  if (enc == 1) {
    memcpy(keyname, ssl_ticket_key->key_name, 16);
    RAND_pseudo_bytes(iv, EVP_MAX_IV_LENGTH);
    EVP_EncryptInit_ex(cipher_ctx, EVP_aes_128_cbc(), NULL, ssl_ticket_key->aes_key, iv);
    HMAC_Init_ex(hctx, ssl_ticket_key->hmac_secret, 16, evp_md_func, NULL);
    Note("create ticket for a new session");

    return 0;
  } else if (enc == 0) {
    if (memcmp(keyname, ssl_ticket_key->key_name, 16)) {
      Error("keyname is not consistent.");
      return 0;
    }

    EVP_DecryptInit_ex(cipher_ctx, EVP_aes_128_cbc(), NULL, ssl_ticket_key->aes_key, iv);
    HMAC_Init_ex(hctx, ssl_ticket_key->hmac_secret, 16, evp_md_func, NULL);

    Note("verify the ticket for an existing session." );
    return 1;
  }

  return -1;
}
#endif /* HAVE_OPENSSL_SESSION_TICKETS */

void
SSLReleaseContext(SSL_CTX * ctx)
{
  ssl_ticket_key_t * ssl_ticket_key = (ssl_ticket_key_t *)SSL_CTX_get_ex_data(ctx, ssl_session_ticket_index);

  // Free the ticket if this is the last reference.
  if (ctx->references == 1 && ssl_ticket_key) {
     delete ssl_ticket_key;
  }

  SSL_CTX_free(ctx);
}
