/**

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

#include "tscore/ink_config.h"
#include "tscore/Diags.h"
#include "P_SSLClientUtils.h"

#define OPENSSL_THREAD_DEFINES

// BoringSSL does not have this include file
#ifndef OPENSSL_IS_BORINGSSL
#include <openssl/opensslconf.h>
#endif
#include <openssl/ssl.h>

#include <unordered_map>

struct SSLConfigParams;
struct SSLCertLookup;
class SSLNetVConnection;
struct RecRawStatBlock;

typedef int ssl_error_t;

enum SSL_Stats {
  ssl_origin_server_expired_cert_stat,
  ssl_user_agent_expired_cert_stat,
  ssl_origin_server_revoked_cert_stat,
  ssl_user_agent_revoked_cert_stat,
  ssl_origin_server_unknown_cert_stat,
  ssl_user_agent_unknown_cert_stat,
  ssl_origin_server_cert_verify_failed_stat,
  ssl_user_agent_cert_verify_failed_stat,
  ssl_origin_server_bad_cert_stat,
  ssl_user_agent_bad_cert_stat,
  ssl_origin_server_decryption_failed_stat,
  ssl_user_agent_decryption_failed_stat,
  ssl_origin_server_wrong_version_stat,
  ssl_user_agent_wrong_version_stat,
  ssl_origin_server_other_errors_stat,
  ssl_user_agent_other_errors_stat,
  ssl_origin_server_unknown_ca_stat,
  ssl_user_agent_unknown_ca_stat,
  ssl_user_agent_sessions_stat,
  ssl_user_agent_session_hit_stat,
  ssl_user_agent_session_miss_stat,
  ssl_user_agent_session_timeout_stat,
  ssl_total_handshake_time_stat,
  ssl_total_success_handshake_count_in_stat,
  ssl_total_tickets_created_stat,
  ssl_total_tickets_verified_stat,
  ssl_total_tickets_verified_old_key_stat, // verified with old key.
  ssl_total_ticket_keys_renewed_stat,      // number of keys renewed.
  ssl_total_tickets_not_found_stat,
  ssl_total_tickets_renewed_stat,
  ssl_total_dyn_def_tls_record_count,
  ssl_total_dyn_max_tls_record_count,
  ssl_total_dyn_redo_tls_record_count,
  ssl_session_cache_hit,
  ssl_session_cache_miss,
  ssl_session_cache_eviction,
  ssl_session_cache_lock_contention,
  ssl_session_cache_new_session,

  /* error stats */
  ssl_error_want_write,
  ssl_error_want_read,
  ssl_error_want_client_hello_cb,
  ssl_error_want_x509_lookup,
  ssl_error_syscall,
  ssl_error_read_eos,
  ssl_error_zero_return,
  ssl_error_ssl,
  ssl_sni_name_set_failure,
  ssl_total_success_handshake_count_out_stat,

  /* ocsp stapling stats */
  ssl_ocsp_revoked_cert_stat,
  ssl_ocsp_unknown_cert_stat,
  ssl_ocsp_refreshed_cert_stat,
  ssl_ocsp_refresh_cert_failure_stat,

  ssl_cipher_stats_start = 100,
  ssl_cipher_stats_end   = 300,

  Ssl_Stat_Count
};

extern RecRawStatBlock *ssl_rsb;

/* Stats should only be accessed using these macros */
#define SSL_INCREMENT_DYN_STAT(x) RecIncrRawStat(ssl_rsb, nullptr, (int)x, 1)
#define SSL_DECREMENT_DYN_STAT(x) RecIncrRawStat(ssl_rsb, nullptr, (int)x, -1)
#define SSL_SET_COUNT_DYN_STAT(x, count) RecSetRawStatCount(ssl_rsb, x, count)
#define SSL_INCREMENT_DYN_STAT_EX(x, y) RecIncrRawStat(ssl_rsb, nullptr, (int)x, y)
#define SSL_CLEAR_DYN_STAT(x)            \
  do {                                   \
    RecSetRawStatSum(ssl_rsb, (x), 0);   \
    RecSetRawStatCount(ssl_rsb, (x), 0); \
  } while (0)

// Create a default SSL server context.
SSL_CTX *SSLDefaultServerContext();

// Create a new SSL server context fully configured.
SSL_CTX *SSLCreateServerContext(const SSLConfigParams *params);

// Initialize the SSL library.
void SSLInitializeLibrary();

// Initialize SSL library based on configuration settings
void SSLPostConfigInitialize();

// Initialize SSL statistics.
void SSLInitializeStatistics();

// Release SSL_CTX and the associated data. This works for both
// client and server contexts and gracefully accepts nullptr.
void SSLReleaseContext(SSL_CTX *ctx);

// Wrapper functions to SSL I/O routines
ssl_error_t SSLWriteBuffer(SSL *ssl, const void *buf, int64_t nbytes, int64_t &nwritten);
ssl_error_t SSLReadBuffer(SSL *ssl, void *buf, int64_t nbytes, int64_t &nread);
ssl_error_t SSLAccept(SSL *ssl);
ssl_error_t SSLConnect(SSL *ssl);

// Log an SSL error.
#define SSLError(fmt, ...) SSLDiagnostic(MakeSourceLocation(), false, nullptr, fmt, ##__VA_ARGS__)
#define SSLErrorVC(vc, fmt, ...) SSLDiagnostic(MakeSourceLocation(), false, (vc), fmt, ##__VA_ARGS__)
// Log a SSL diagnostic using the "ssl" diagnostic tag.
#define SSLDebug(fmt, ...) SSLDiagnostic(MakeSourceLocation(), true, nullptr, fmt, ##__VA_ARGS__)
#define SSLVCDebug(vc, fmt, ...) SSLDiagnostic(MakeSourceLocation(), true, (vc), fmt, ##__VA_ARGS__)

#define SSL_CLR_ERR_INCR_DYN_STAT(vc, x, fmt, ...) \
  do {                                             \
    SSLVCDebug((vc), fmt, ##__VA_ARGS__);          \
    RecIncrRawStat(ssl_rsb, nullptr, (int)x, 1);   \
  } while (0)

void SSLDiagnostic(const SourceLocation &loc, bool debug, SSLNetVConnection *vc, const char *fmt, ...) TS_PRINTFLIKE(4, 5);

// Return a static string name for a SSL_ERROR constant.
const char *SSLErrorName(int ssl_error);

// Log a SSL network buffer.
void SSLDebugBufferPrint(const char *tag, const char *buffer, unsigned buflen, const char *message);

// Load the SSL certificate configuration.
bool SSLParseCertificateConfiguration(const SSLConfigParams *params, SSLCertLookup *lookup);

// Attach a SSL NetVC back pointer to a SSL session.
void SSLNetVCAttach(SSL *ssl, SSLNetVConnection *vc);

// Detach from SSL NetVC back pointer to a SSL session.
void SSLNetVCDetach(SSL *ssl);

// Return the SSLNetVConnection (if any) attached to this SSL session.
SSLNetVConnection *SSLNetVCAccess(const SSL *ssl);

void setClientCertLevel(SSL *ssl, uint8_t certLevel);
void setTLSValidProtocols(SSL *ssl, unsigned long proto_mask, unsigned long max_mask);

namespace ssl
{
namespace detail
{
  struct SCOPED_X509_TRAITS {
    typedef X509 *value_type;
    static value_type
    initValue()
    {
      return nullptr;
    }
    static bool
    isValid(value_type x)
    {
      return x != nullptr;
    }
    static void
    destroy(value_type x)
    {
      X509_free(x);
    }
  };

  struct SCOPED_BIO_TRAITS {
    typedef BIO *value_type;
    static value_type
    initValue()
    {
      return nullptr;
    }
    static bool
    isValid(value_type x)
    {
      return x != nullptr;
    }
    static void
    destroy(value_type x)
    {
      BIO_free(x);
    }
  };
  /* namespace ssl */ // namespace detail
} /* namespace detail */
} // namespace ssl

struct ats_wildcard_matcher {
  ats_wildcard_matcher()
  {
    if (!regex.compile("^\\*\\.[^\\*.]+")) {
      Fatal("failed to compile TLS wildcard matching regex");
    }
  }

  ~ats_wildcard_matcher() {}
  bool
  match(const char *hostname) const
  {
    return regex.match(hostname) != -1;
  }

private:
  DFA regex;
};

typedef ats_scoped_resource<ssl::detail::SCOPED_X509_TRAITS> scoped_X509;
typedef ats_scoped_resource<ssl::detail::SCOPED_BIO_TRAITS> scoped_BIO;
