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

#ifndef __P_SSLUTILS_H__
#define __P_SSLUTILS_H__

#include "ink_config.h"
#include "Diags.h"

#define OPENSSL_THREAD_DEFINES
#include <openssl/opensslconf.h>
#include <openssl/ssl.h>

#if !defined(OPENSSL_THREADS)
#error Traffic Server requires a OpenSSL library that support threads
#endif

struct SSLConfigParams;
struct SSLCertLookup;
class SSLNetVConnection;
struct RecRawStatBlock;

enum SSL_Stats
{
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

  ssl_cipher_stats_start = 100,
  ssl_cipher_stats_end = 300,

  Ssl_Stat_Count
};

extern RecRawStatBlock *ssl_rsb;

/* Stats should only be accessed using these macros */
#define SSL_INCREMENT_DYN_STAT(x) RecIncrRawStat(ssl_rsb, NULL, (int) x, 1)
#define SSL_DECREMENT_DYN_STAT(x) RecIncrRawStat(ssl_rsb, NULL, (int) x, -1)
#define SSL_SET_COUNT_DYN_STAT(x,count) RecSetRawStatCount(ssl_rsb, x, count)
#define SSL_CLEAR_DYN_STAT(x) \
  do { \
    RecSetRawStatSum(ssl_rsb, (x), 0); \
    RecSetRawStatCount(ssl_rsb, (x), 0); \
  } while (0);

// Create a default SSL server context.
SSL_CTX * SSLDefaultServerContext();

// Create and initialize a SSL client context.
SSL_CTX * SSLInitClientContext(const SSLConfigParams * param);

// Initialize the SSL library.
void SSLInitializeLibrary();

// Initialize SSL statistics.
void SSLInitializeStatistics();

// Release SSL_CTX and the associated data
void SSLReleaseContext(SSL_CTX* ctx);

// Log an SSL error.
#define SSLError(fmt, ...) SSLDiagnostic(DiagsMakeLocation(), false, NULL, fmt, ##__VA_ARGS__)
#define SSLErrorVC(vc,fmt, ...) SSLDiagnostic(DiagsMakeLocation(), false, vc, fmt, ##__VA_ARGS__)
// Log a SSL diagnostic using the "ssl" diagnostic tag.
#define SSLDebug(fmt, ...) SSLDiagnostic(DiagsMakeLocation(), true, NULL, fmt, ##__VA_ARGS__)
#define SSLDebugVC(vc,fmt, ...) SSLDiagnostic(DiagsMakeLocation(), true, vc, fmt, ##__VA_ARGS__)

void SSLDiagnostic(const SrcLoc& loc, bool debug, SSLNetVConnection * vc, const char * fmt, ...) TS_PRINTFLIKE(4, 5);

// Return a static string name for a SSL_ERROR constant.
const char * SSLErrorName(int ssl_error);

// Log a SSL network buffer.
void SSLDebugBufferPrint(const char * tag, const char * buffer, unsigned buflen, const char * message);

// Load the SSL certificate configuration.
bool SSLParseCertificateConfiguration(const SSLConfigParams * params, SSLCertLookup * lookup);

#endif /* __P_SSLUTILS_H__ */
