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

#include "SSLCertConfigLoader.h"

#include "tscore/ink_mutex.h"

#include "P_Net.h"

#include "P_OCSPStapling.h"
#include "P_SSLSNI.h"
#include "P_SSLConfig.h"
#include "SSLSessionCache.h"
#include "SSLSessionTicket.h"
#include "SSLDynlock.h"
#include "SSLDiags.h"
#include "SSLStats.h"

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

SSLSessionCache *session_cache; // declared extern in P_SSLConfig.h

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

// Callback function for verifying client certificate
static int
ssl_verify_client_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
  Debug("ssl", "Callback: verify client cert");
  auto *ssl                = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);

  if (!netvc || netvc->ssl != ssl) {
    Debug("ssl.error", "ssl_verify_client_callback call back on stale netvc");
    return false;
  }

  netvc->set_verify_cert(ctx);
  netvc->callHooks(TS_EVENT_SSL_VERIFY_CLIENT);
  netvc->set_verify_cert(nullptr);

  if (netvc->getSSLHandShakeComplete()) { // hook moved the handshake state to terminal
    Warning("TS_EVENT_SSL_VERIFY_CLIENT plugin failed the client certificate check for %s.", netvc->options.sni_servername.get());
    return false;
  }

  return preverify_ok;
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

#if TS_USE_TLS_OCSP
  ssl_stapling_ex_init();
#endif /* TS_USE_TLS_OCSP */

  // Reserve an application data index so that we can attach
  // the SSLNetVConnection to the SSL session.
  ssl_vc_index = SSL_get_ex_new_index(0, (void *)"NetVC index", nullptr, nullptr, nullptr);

  TLSSessionResumptionSupport::initialize();

  open_ssl_initialized = true;
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
  SSL_set_verify(ssl, server_verify_client, ssl_verify_client_callback);
  SSL_set_verify_depth(ssl, params->verify_depth); // might want to make configurable at some point.
}

SSL_CTX *
SSLCreateServerContext(const SSLConfigParams *params, const SSLMultiCertConfigParams *sslMultiCertSettings, const char *cert_path,
                       const char *key_path)
{
  SSLMultiCertConfigLoader loader(params);
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(nullptr, &SSL_CTX_free);
  std::vector<X509 *> cert_list;
  std::set<std::string> common_names;
  std::unordered_map<int, std::set<std::string>> unique_names;
  SSLMultiCertConfigLoader::CertLoadData data;
  if (loader.load_certs_and_cross_reference_names(cert_list, data, params, sslMultiCertSettings, common_names, unique_names)) {
    ctx.reset(loader.init_server_ssl_ctx(data, sslMultiCertSettings, common_names));
  }
  for (auto &i : cert_list) {
    X509_free(i);
  }
  if (ctx && cert_path) {
    if (!SSL_CTX_use_certificate_file(ctx.get(), cert_path, SSL_FILETYPE_PEM)) {
      SSLError("SSLCreateServerContext(): failed to load server certificate.");
      ctx = nullptr;
    } else if (!key_path || key_path[0] == '\0') {
      key_path = cert_path;
    }
    if (ctx) {
      if (!SSL_CTX_use_PrivateKey_file(ctx.get(), key_path, SSL_FILETYPE_PEM)) {
        SSLError("SSLCreateServerContext(): failed to load server private key.");
        ctx = nullptr;
      } else if (!SSL_CTX_check_private_key(ctx.get())) {
        SSLError("SSLCreateServerContext(): server private key does not match server certificate.");
        ctx = nullptr;
      }
    }
  }
  return ctx.release();
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

ssl_error_t
SSLWriteBuffer(SSL *ssl, const void *buf, int64_t nbytes, int64_t &nwritten)
{
  nwritten = 0;

  if (unlikely(nbytes == 0)) {
    return SSL_ERROR_NONE;
  }
  ERR_clear_error();

  int ret;
#if TS_HAS_TLS_EARLY_DATA
  if (SSL_version(ssl) >= TLS1_3_VERSION) {
    if (SSL_is_init_finished(ssl)) {
      ret = SSL_write(ssl, buf, static_cast<int>(nbytes));
    } else {
      size_t nwrite;
      ret = SSL_write_early_data(ssl, buf, static_cast<size_t>(nbytes), &nwrite);
      if (ret == 1) {
        ret = nwrite;
      }
    }
  } else {
    ret = SSL_write(ssl, buf, static_cast<int>(nbytes));
  }
#else
  ret = SSL_write(ssl, buf, static_cast<int>(nbytes));
#endif

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
    char tempbuf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, tempbuf, sizeof(tempbuf));
    Debug("ssl.error.write", "SSL write returned %d, ssl_error=%d, ERR_get_error=%ld (%s)", ret, ssl_error, e, tempbuf);
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

#if TS_HAS_TLS_EARLY_DATA
  if (SSL_version(ssl) >= TLS1_3_VERSION) {
    SSLNetVConnection *netvc = SSLNetVCAccess(ssl);

    int64_t early_data_len = 0;
    if (netvc->early_data_reader != nullptr) {
      early_data_len = netvc->early_data_reader->read_avail();
    }

    if (early_data_len > 0) {
      Debug("ssl_early_data", "Reading from early data buffer.");
      netvc->read_from_early_data += netvc->early_data_reader->read(buf, nbytes < early_data_len ? nbytes : early_data_len);

      if (nbytes < early_data_len) {
        nread = nbytes;
      } else {
        nread = early_data_len;
      }

      return SSL_ERROR_NONE;
    }

    if (SSLConfigParams::server_max_early_data > 0 && !netvc->early_data_finish) {
      Debug("ssl_early_data", "More early data to read.");
      ssl_error_t ssl_error = SSL_ERROR_NONE;
      size_t read_bytes     = 0;

      int ret = SSL_read_early_data(ssl, buf, static_cast<size_t>(nbytes), &read_bytes);

      if (ret == SSL_READ_EARLY_DATA_ERROR) {
        Debug("ssl_early_data", "SSL_READ_EARLY_DATA_ERROR");
        ssl_error = SSL_get_error(ssl, ret);
        Debug("ssl_early_data", "Error reading early data: %s", ERR_error_string(ERR_get_error(), nullptr));
      } else {
        if ((nread = read_bytes) > 0) {
          netvc->read_from_early_data += read_bytes;
          SSL_INCREMENT_DYN_STAT(ssl_early_data_received_count);
          if (is_debug_tag_set("ssl_early_data_show_received")) {
            std::string early_data_str(reinterpret_cast<char *>(buf), nread);
            Debug("ssl_early_data_show_received", "Early data buffer: \n%s", early_data_str.c_str());
          }
        }

        if (ret == SSL_READ_EARLY_DATA_FINISH) {
          netvc->early_data_finish = true;
          Debug("ssl_early_data", "SSL_READ_EARLY_DATA_FINISH: size = %" PRId64, nread);
        } else {
          Debug("ssl_early_data", "SSL_READ_EARLY_DATA_SUCCESS: size = %" PRId64, nread);
        }
      }

      return ssl_error;
    }
  }
#endif

  int ret = SSL_read(ssl, buf, static_cast<int>(nbytes));
  if (ret > 0) {
    nread = ret;
    return SSL_ERROR_NONE;
  }
  int ssl_error = SSL_get_error(ssl, ret);
  if (ssl_error == SSL_ERROR_SSL && is_debug_tag_set("ssl.error.read")) {
    char tempbuf[512];
    unsigned long e = ERR_peek_last_error();
    ERR_error_string_n(e, tempbuf, sizeof(tempbuf));
    Debug("ssl.error.read", "SSL read returned %d, ssl_error=%d, ERR_get_error=%ld (%s)", ret, ssl_error, e, tempbuf);
  }

  return ssl_error;
}

ssl_error_t
SSLAccept(SSL *ssl)
{
  ERR_clear_error();

  int ret       = 0;
  int ssl_error = SSL_ERROR_NONE;

#if TS_HAS_TLS_EARLY_DATA
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);

  if (SSLConfigParams::server_max_early_data > 0 && !netvc->early_data_finish) {
    size_t nread;

    while (true) {
      IOBufferBlock *block = new_IOBufferBlock();
      block->alloc(BUFFER_SIZE_INDEX_16K);
      ret = SSL_read_early_data(ssl, block->buf(), index_to_buffer_size(BUFFER_SIZE_INDEX_16K), &nread);

      if (ret == SSL_READ_EARLY_DATA_ERROR) {
        Debug("ssl_early_data", "SSL_READ_EARLY_DATA_ERROR");
        block->free();
        break;
      } else {
        if (nread > 0) {
          if (netvc->early_data_buf == nullptr) {
            netvc->early_data_buf    = new_MIOBuffer(BUFFER_SIZE_INDEX_16K);
            netvc->early_data_reader = netvc->early_data_buf->alloc_reader();
          }
          block->fill(nread);
          netvc->early_data_buf->append_block(block);
          SSL_INCREMENT_DYN_STAT(ssl_early_data_received_count);

          if (is_debug_tag_set("ssl_early_data_show_received")) {
            std::string early_data_str(reinterpret_cast<char *>(block->buf()), nread);
            Debug("ssl_early_data_show_received", "Early data buffer: \n%s", early_data_str.c_str());
          }
        } else {
          block->free();
        }

        if (ret == SSL_READ_EARLY_DATA_FINISH) {
          netvc->early_data_finish = true;
          Debug("ssl_early_data", "SSL_READ_EARLY_DATA_FINISH: size = %lu", nread);

          if (netvc->early_data_reader == nullptr || netvc->early_data_reader->read_avail() == 0) {
            Debug("ssl_early_data", "no data in early data buffer");
            ERR_clear_error();
            ret = SSL_accept(ssl);
          }
          break;
        }
        Debug("ssl_early_data", "SSL_READ_EARLY_DATA_SUCCESS: size = %lu", nread);
      }
    }
  } else {
    ret = SSL_accept(ssl);
  }
#else
  ret = SSL_accept(ssl);
#endif

  if (ret > 0) {
    return SSL_ERROR_NONE;
  }
  ssl_error = SSL_get_error(ssl, ret);
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

ssl_curve_id
SSLGetCurveNID(SSL *ssl)
{
#ifndef OPENSSL_IS_BORINGSSL
  return SSL_get_shared_curve(ssl, 0);
#else
  return SSL_get_curve_id(ssl);
#endif
}
