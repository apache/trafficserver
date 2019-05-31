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

#include "P_SSLConfig.h"
#include "P_SSLUtils.h"

RecRawStatBlock *ssl_rsb = nullptr;
std::unordered_map<std::string, intptr_t> cipher_map;

static int
SSLRecRawStatSyncCount(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  // Grab all the stats we want from OpenSSL and set the stats. This function only needs to be called by one of the
  // involved stats, all others *must* call RecRawStatSyncSum.
  SSLCertificateConfig::scoped_config certLookup;

  int64_t sessions = 0;
  int64_t hits     = 0;
  int64_t misses   = 0;
  int64_t timeouts = 0;

  if (certLookup) {
    const unsigned ctxCount = certLookup->count();
    for (size_t i = 0; i < ctxCount; i++) {
      SSLCertContext *cc = certLookup->get(i);
      if (cc && cc->ctx) {
        sessions += SSL_CTX_sess_accept_good(cc->ctx);
        hits += SSL_CTX_sess_hits(cc->ctx);
        misses += SSL_CTX_sess_misses(cc->ctx);
        timeouts += SSL_CTX_sess_timeouts(cc->ctx);
      }
    }
  }

  SSL_SET_COUNT_DYN_STAT(ssl_user_agent_sessions_stat, sessions);
  SSL_SET_COUNT_DYN_STAT(ssl_user_agent_session_hit_stat, hits);
  SSL_SET_COUNT_DYN_STAT(ssl_user_agent_session_miss_stat, misses);
  SSL_SET_COUNT_DYN_STAT(ssl_user_agent_session_timeout_stat, timeouts);

  return RecRawStatSyncCount(name, data_type, data, rsb, id);
}

void
SSLInitializeStatistics()
{
  SSL_CTX *ctx;
  SSL *ssl;
  STACK_OF(SSL_CIPHER) * ciphers;

  // Allocate SSL statistics block.
  ssl_rsb = RecAllocateRawStatBlock((int)Ssl_Stat_Count);
  ink_assert(ssl_rsb != nullptr);

  // SSL client errors.
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_other_errors", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_user_agent_other_errors_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_expired_cert", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_user_agent_expired_cert_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_revoked_cert", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_user_agent_revoked_cert_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_unknown_cert", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_user_agent_unknown_cert_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_cert_verify_failed", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_user_agent_cert_verify_failed_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_bad_cert", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_user_agent_bad_cert_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_decryption_failed", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_user_agent_decryption_failed_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_wrong_version", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_user_agent_wrong_version_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_unknown_ca", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_user_agent_unknown_ca_stat, RecRawStatSyncSum);

  // Polled SSL context statistics.
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_sessions", RECD_COUNTER, RECP_NON_PERSISTENT,
                     (int)ssl_user_agent_sessions_stat,
                     SSLRecRawStatSyncCount); //<- only use this fn once
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_session_hit", RECD_COUNTER, RECP_NON_PERSISTENT,
                     (int)ssl_user_agent_session_hit_stat, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_session_miss", RECD_COUNTER, RECP_NON_PERSISTENT,
                     (int)ssl_user_agent_session_miss_stat, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.user_agent_session_timeout", RECD_COUNTER, RECP_NON_PERSISTENT,
                     (int)ssl_user_agent_session_timeout_stat, RecRawStatSyncCount);

  // SSL server errors.
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.origin_server_other_errors", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_origin_server_other_errors_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.origin_server_expired_cert", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_origin_server_expired_cert_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.origin_server_revoked_cert", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_origin_server_revoked_cert_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.origin_server_unknown_cert", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_origin_server_unknown_cert_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.origin_server_cert_verify_failed", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_origin_server_cert_verify_failed_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.origin_server_bad_cert", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_origin_server_bad_cert_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.origin_server_decryption_failed", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_origin_server_decryption_failed_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.origin_server_wrong_version", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_origin_server_wrong_version_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.origin_server_unknown_ca", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_origin_server_unknown_ca_stat, RecRawStatSyncSum);

  // SSL handshake time
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.total_handshake_time", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_handshake_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.total_success_handshake_count_in", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_success_handshake_count_in_stat, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.total_success_handshake_count_out", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_success_handshake_count_out_stat, RecRawStatSyncCount);

  // TLS tickets
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.total_tickets_created", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_tickets_created_stat, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.total_tickets_verified", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_tickets_verified_stat, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.total_tickets_not_found", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_tickets_not_found_stat, RecRawStatSyncCount);
  // TODO: ticket renewal is not used right now.
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.total_tickets_renewed", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_tickets_renewed_stat, RecRawStatSyncCount);
  // The number of session tickets verified with an "old" key.
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.total_tickets_verified_old_key", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_tickets_verified_old_key_stat, RecRawStatSyncCount);
  // The number of ticket keys renewed.
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.total_ticket_keys_renewed", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_ticket_keys_renewed_stat, RecRawStatSyncCount);

  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_session_cache_hit", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_session_cache_hit, RecRawStatSyncCount);

  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_session_cache_new_session", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_session_cache_new_session, RecRawStatSyncCount);

  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_session_cache_miss", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_session_cache_miss, RecRawStatSyncCount);

  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_session_cache_eviction", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_session_cache_eviction, RecRawStatSyncCount);

  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_session_cache_lock_contention", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_session_cache_lock_contention, RecRawStatSyncCount);

  /* Track dynamic record size */
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.default_record_size_count", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_dyn_def_tls_record_count, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.max_record_size_count", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_dyn_max_tls_record_count, RecRawStatSyncSum);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.redo_record_size_count", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_total_dyn_redo_tls_record_count, RecRawStatSyncCount);

  /* error stats */
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_error_want_write", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_error_want_write, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_error_want_read", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_error_want_read, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_error_want_x509_lookup", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_error_want_x509_lookup, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_error_syscall", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_error_syscall, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_error_read_eos", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_error_read_eos, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_error_zero_return", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_error_zero_return, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_error_ssl", RECD_COUNTER, RECP_PERSISTENT, (int)ssl_error_ssl,
                     RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_sni_name_set_failure", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_sni_name_set_failure, RecRawStatSyncCount);

  /* ocsp stapling stats */
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_ocsp_revoked_cert_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_ocsp_revoked_cert_stat, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_ocsp_unknown_cert_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)ssl_ocsp_unknown_cert_stat, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_ocsp_refreshed_cert", RECD_INT, RECP_PERSISTENT,
                     (int)ssl_ocsp_refreshed_cert_stat, RecRawStatSyncCount);
  RecRegisterRawStat(ssl_rsb, RECT_PROCESS, "proxy.process.ssl.ssl_ocsp_refresh_cert_failure", RECD_INT, RECP_PERSISTENT,
                     (int)ssl_ocsp_refresh_cert_failure_stat, RecRawStatSyncCount);

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

    // If room in allocated space ...
    if ((ssl_cipher_stats_start + index) > ssl_cipher_stats_end) {
      // Too many ciphers, increase ssl_cipher_stats_end.
      SSLError("too many ciphers to register metric '%s', increase SSL_Stats::ssl_cipher_stats_end", statName.c_str());
      continue;
    }

    // If not already registered ...
    if (cipherName && cipher_map.find(cipherName) == cipher_map.end()) {
      cipher_map.emplace(cipherName, (intptr_t)(ssl_cipher_stats_start + index));
      // Register as non-persistent since the order/index is dependent upon configuration.
      RecRegisterRawStat(ssl_rsb, RECT_PROCESS, statName.c_str(), RECD_INT, RECP_NON_PERSISTENT,
                         (int)ssl_cipher_stats_start + index, RecRawStatSyncSum);
      SSL_CLEAR_DYN_STAT((int)ssl_cipher_stats_start + index);
      Debug("ssl", "registering SSL cipher metric '%s'", statName.c_str());
    }
  }

  SSL_free(ssl);
  SSLReleaseContext(ctx);
}
