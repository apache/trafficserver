/** @file

  Stats of TLS

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

#include "SSLStats.h"

#include <openssl/err.h>

#include "iocore/net/SSLMultiCertConfigLoader.h"
#include "P_SSLConfig.h"
#include "P_SSLUtils.h"
#include "../../records/P_RecProcess.h"

SSLStatsBlock                                                   ssl_rsb;
std::unordered_map<std::string, Metrics::Counter::AtomicType *> cipher_map;

namespace
{
DbgCtl dbg_ctl_ssl{"ssl"};

} // end anonymous namespace

// ToDo: This gets called once per global sync, for now at least.
void
SSLPeriodicMetricsUpdate()
{
  SSLCertificateConfig::scoped_config certLookup;

  int64_t sessions = 0;
  int64_t hits     = 0;
  int64_t misses   = 0;
  int64_t timeouts = 0;

  Dbg(dbg_ctl_ssl, "Starting to update the new session metrics");
  if (certLookup) {
    const unsigned ctxCount = certLookup->count();
    for (size_t i = 0; i < ctxCount; i++) {
      SSLCertContext *cc = certLookup->get(i);
      if (cc) {
        shared_SSL_CTX ctx = cc->getCtx();
        if (ctx) {
          sessions += SSL_CTX_sess_accept_good(ctx.get());
          hits     += SSL_CTX_sess_hits(ctx.get());
          misses   += SSL_CTX_sess_misses(ctx.get());
          timeouts += SSL_CTX_sess_timeouts(ctx.get());
        }
      }
    }
  }

  Metrics::Gauge::store(ssl_rsb.user_agent_sessions, sessions);
  Metrics::Gauge::store(ssl_rsb.user_agent_session_hit, hits);
  Metrics::Gauge::store(ssl_rsb.user_agent_session_miss, misses);
  Metrics::Gauge::store(ssl_rsb.user_agent_session_timeout, timeouts);
}

static void
add_cipher_stat(const char *cipherName, const std::string &statName)
{
  // If not already registered ...
  if (cipherName && cipher_map.find(cipherName) == cipher_map.end()) {
    Metrics::Counter::AtomicType *metric = Metrics::Counter::createPtr(statName);

    cipher_map.emplace(cipherName, metric);
    Dbg(dbg_ctl_ssl, "registering SSL cipher metric '%s'", statName.c_str());
  }
}

void
SSLInitializeStatistics()
{
  SSL_CTX *ctx;
  SSL     *ssl;
  STACK_OF(SSL_CIPHER) * ciphers;

  // For now, register with the librecords global sync.
  RecRegNewSyncStatSync(SSLPeriodicMetricsUpdate);

  ssl_rsb.early_data_received_count          = Metrics::Counter::createPtr("proxy.process.ssl.early_data_received");
  ssl_rsb.error_async                        = Metrics::Counter::createPtr("proxy.process.ssl.ssl_error_async");
  ssl_rsb.error_ssl                          = Metrics::Counter::createPtr("proxy.process.ssl.ssl_error_ssl");
  ssl_rsb.error_syscall                      = Metrics::Counter::createPtr("proxy.process.ssl.ssl_error_syscall");
  ssl_rsb.ocsp_refresh_cert_failure          = Metrics::Counter::createPtr("proxy.process.ssl.ssl_ocsp_refresh_cert_failure");
  ssl_rsb.ocsp_refreshed_cert                = Metrics::Counter::createPtr("proxy.process.ssl.ssl_ocsp_refreshed_cert");
  ssl_rsb.ocsp_revoked_cert                  = Metrics::Counter::createPtr("proxy.process.ssl.ssl_ocsp_revoked_cert");
  ssl_rsb.ocsp_unknown_cert                  = Metrics::Counter::createPtr("proxy.process.ssl.ssl_ocsp_unknown_cert");
  ssl_rsb.origin_server_bad_cert             = Metrics::Counter::createPtr("proxy.process.ssl.origin_server_bad_cert");
  ssl_rsb.origin_server_cert_verify_failed   = Metrics::Counter::createPtr("proxy.process.ssl.origin_server_cert_verify_failed");
  ssl_rsb.origin_server_decryption_failed    = Metrics::Counter::createPtr("proxy.process.ssl.origin_server_decryption_failed");
  ssl_rsb.origin_server_expired_cert         = Metrics::Counter::createPtr("proxy.process.ssl.origin_server_expired_cert");
  ssl_rsb.origin_server_other_errors         = Metrics::Counter::createPtr("proxy.process.ssl.origin_server_other_errors");
  ssl_rsb.origin_server_revoked_cert         = Metrics::Counter::createPtr("proxy.process.ssl.origin_server_revoked_cert");
  ssl_rsb.origin_server_unknown_ca           = Metrics::Counter::createPtr("proxy.process.ssl.origin_server_unknown_ca");
  ssl_rsb.origin_server_unknown_cert         = Metrics::Counter::createPtr("proxy.process.ssl.origin_server_unknown_cert");
  ssl_rsb.origin_server_wrong_version        = Metrics::Counter::createPtr("proxy.process.ssl.origin_server_wrong_version");
  ssl_rsb.origin_session_reused_count        = Metrics::Counter::createPtr("proxy.process.ssl.origin_session_reused");
  ssl_rsb.sni_name_set_failure               = Metrics::Counter::createPtr("proxy.process.ssl.ssl_sni_name_set_failure");
  ssl_rsb.origin_session_cache_hit           = Metrics::Counter::createPtr("proxy.process.ssl.ssl_origin_session_cache_hit");
  ssl_rsb.origin_session_cache_miss          = Metrics::Counter::createPtr("proxy.process.ssl.ssl_origin_session_cache_miss");
  ssl_rsb.session_cache_eviction             = Metrics::Counter::createPtr("proxy.process.ssl.ssl_session_cache_eviction");
  ssl_rsb.session_cache_hit                  = Metrics::Counter::createPtr("proxy.process.ssl.ssl_session_cache_hit");
  ssl_rsb.session_cache_lock_contention      = Metrics::Counter::createPtr("proxy.process.ssl.ssl_session_cache_lock_contention");
  ssl_rsb.session_cache_miss                 = Metrics::Counter::createPtr("proxy.process.ssl.ssl_session_cache_miss");
  ssl_rsb.session_cache_new_session          = Metrics::Counter::createPtr("proxy.process.ssl.ssl_session_cache_new_session");
  ssl_rsb.total_attempts_handshake_count_in  = Metrics::Counter::createPtr("proxy.process.ssl.total_attempts_handshake_count_in");
  ssl_rsb.total_attempts_handshake_count_out = Metrics::Counter::createPtr("proxy.process.ssl.total_attempts_handshake_count_out");
  ssl_rsb.total_dyn_def_tls_record_count     = Metrics::Counter::createPtr("proxy.process.ssl.default_record_size_count");
  ssl_rsb.total_dyn_max_tls_record_count     = Metrics::Counter::createPtr("proxy.process.ssl.max_record_size_count");
  ssl_rsb.total_dyn_redo_tls_record_count    = Metrics::Counter::createPtr("proxy.process.ssl.redo_record_size_count");
  ssl_rsb.total_handshake_time               = Metrics::Counter::createPtr("proxy.process.ssl.total_handshake_time");
  ssl_rsb.total_sslv3                        = Metrics::Counter::createPtr("proxy.process.ssl.ssl_total_sslv3");
  ssl_rsb.total_success_handshake_count_in   = Metrics::Counter::createPtr("proxy.process.ssl.total_success_handshake_count_in");
  ssl_rsb.total_success_handshake_count_out  = Metrics::Counter::createPtr("proxy.process.ssl.total_success_handshake_count_out");
  ssl_rsb.total_ticket_keys_renewed          = Metrics::Counter::createPtr("proxy.process.ssl.total_ticket_keys_renewed");
  ssl_rsb.total_tickets_created              = Metrics::Counter::createPtr("proxy.process.ssl.total_tickets_created");
  ssl_rsb.total_tickets_not_found            = Metrics::Counter::createPtr("proxy.process.ssl.total_tickets_not_found");
  ssl_rsb.total_tickets_renewed              = Metrics::Counter::createPtr("proxy.process.ssl.total_tickets_renewed");
  ssl_rsb.total_tickets_verified             = Metrics::Counter::createPtr("proxy.process.ssl.total_tickets_verified");
  ssl_rsb.total_tickets_verified_old_key     = Metrics::Counter::createPtr("proxy.process.ssl.total_tickets_verified_old_key");
  ssl_rsb.total_tlsv1                        = Metrics::Counter::createPtr("proxy.process.ssl.ssl_total_tlsv1");
  ssl_rsb.total_tlsv11                       = Metrics::Counter::createPtr("proxy.process.ssl.ssl_total_tlsv11");
  ssl_rsb.total_tlsv12                       = Metrics::Counter::createPtr("proxy.process.ssl.ssl_total_tlsv12");
  ssl_rsb.total_tlsv13                       = Metrics::Counter::createPtr("proxy.process.ssl.ssl_total_tlsv13");
  ssl_rsb.user_agent_bad_cert                = Metrics::Counter::createPtr("proxy.process.ssl.user_agent_bad_cert");
  ssl_rsb.user_agent_cert_verify_failed      = Metrics::Counter::createPtr("proxy.process.ssl.user_agent_cert_verify_failed");
  ssl_rsb.user_agent_decryption_failed       = Metrics::Counter::createPtr("proxy.process.ssl.user_agent_decryption_failed");
  ssl_rsb.user_agent_expired_cert            = Metrics::Counter::createPtr("proxy.process.ssl.user_agent_expired_cert");
  ssl_rsb.user_agent_other_errors            = Metrics::Counter::createPtr("proxy.process.ssl.user_agent_other_errors");
  ssl_rsb.user_agent_revoked_cert            = Metrics::Counter::createPtr("proxy.process.ssl.user_agent_revoked_cert");
  ssl_rsb.user_agent_session_hit             = Metrics::Gauge::createPtr("proxy.process.ssl.user_agent_session_hit");
  ssl_rsb.user_agent_session_miss            = Metrics::Gauge::createPtr("proxy.process.ssl.user_agent_session_miss");
  ssl_rsb.user_agent_session_timeout         = Metrics::Gauge::createPtr("proxy.process.ssl.user_agent_session_timeout");
  ssl_rsb.user_agent_sessions                = Metrics::Gauge::createPtr("proxy.process.ssl.user_agent_sessions");
  ssl_rsb.user_agent_unknown_ca              = Metrics::Counter::createPtr("proxy.process.ssl.user_agent_unknown_ca");
  ssl_rsb.user_agent_unknown_cert            = Metrics::Counter::createPtr("proxy.process.ssl.user_agent_unknown_cert");
  ssl_rsb.user_agent_wrong_version           = Metrics::Counter::createPtr("proxy.process.ssl.user_agent_wrong_version");

  // Get and register the SSL cipher stats. Note that we are using the default SSL context to obtain
  // the cipher list. This means that the set of ciphers is fixed by the build configuration and not
  // filtered by proxy.config.ssl.server.cipher_suite. This keeps the set of cipher suites stable across
  // configuration reloads and works for the case where we honor the client cipher preference.
  SSLMultiCertConfigLoader loader(nullptr);
  ctx     = loader.default_server_ssl_ctx();
  ssl     = SSL_new(ctx);
  ciphers = SSL_get_ciphers(ssl);

  // BoringSSL has sk_SSL_CIPHER_num() return a size_t (well, sk_num() is)
  for (int index = 0; index < static_cast<int>(sk_SSL_CIPHER_num(ciphers)); index++) {
    SSL_CIPHER *cipher     = const_cast<SSL_CIPHER *>(sk_SSL_CIPHER_value(ciphers, index));
    const char *cipherName = SSL_CIPHER_get_name(cipher);
    std::string statName   = "proxy.process.ssl.cipher.user_agent." + std::string(cipherName);

    add_cipher_stat(cipherName, statName);
  }

  // Add "OTHER" for ciphers not on the map
  add_cipher_stat(SSL_CIPHER_STAT_OTHER.c_str(), "proxy.process.ssl.cipher.user_agent." + SSL_CIPHER_STAT_OTHER);

  SSL_free(ssl);
  SSLReleaseContext(ctx);
}
