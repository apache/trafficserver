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

#pragma once

#include "tsutil/Metrics.h"

#include <openssl/ssl.h>

#include <unordered_map>

using ts::Metrics;

// For some odd reason, these have to be initialized with nullptr, because the order
// of initialization and how we load certs is weird... In reality only the metric
// for ssl_rsb.total_ticket_keys_renewed needs this initialization, but lets be
// consistent at least.
struct SSLStatsBlock {
  Metrics::Counter::AtomicType *early_data_received_count                      = nullptr;
  Metrics::Counter::AtomicType *error_async                                    = nullptr;
  Metrics::Counter::AtomicType *error_ssl                                      = nullptr;
  Metrics::Counter::AtomicType *error_syscall                                  = nullptr;
  Metrics::Counter::AtomicType *ocsp_refresh_cert_failure                      = nullptr;
  Metrics::Counter::AtomicType *ocsp_refreshed_cert                            = nullptr;
  Metrics::Counter::AtomicType *ocsp_revoked_cert                              = nullptr;
  Metrics::Counter::AtomicType *ocsp_unknown_cert                              = nullptr;
  Metrics::Counter::AtomicType *origin_server_bad_cert                         = nullptr;
  Metrics::Counter::AtomicType *origin_server_cert_verify_failed               = nullptr;
  Metrics::Counter::AtomicType *origin_server_decryption_failed                = nullptr;
  Metrics::Counter::AtomicType *origin_server_expired_cert                     = nullptr;
  Metrics::Counter::AtomicType *origin_server_other_errors                     = nullptr;
  Metrics::Counter::AtomicType *origin_server_revoked_cert                     = nullptr;
  Metrics::Counter::AtomicType *origin_server_unknown_ca                       = nullptr;
  Metrics::Counter::AtomicType *origin_server_unknown_cert                     = nullptr;
  Metrics::Counter::AtomicType *origin_server_wrong_version                    = nullptr;
  Metrics::Counter::AtomicType *origin_session_cache_hit                       = nullptr;
  Metrics::Counter::AtomicType *origin_session_cache_miss                      = nullptr;
  Metrics::Counter::AtomicType *origin_session_reused_count                    = nullptr;
  Metrics::Counter::AtomicType *session_cache_eviction                         = nullptr;
  Metrics::Counter::AtomicType *session_cache_hit                              = nullptr;
  Metrics::Counter::AtomicType *session_cache_lock_contention                  = nullptr;
  Metrics::Counter::AtomicType *session_cache_miss                             = nullptr;
  Metrics::Counter::AtomicType *session_cache_new_session                      = nullptr;
  Metrics::Counter::AtomicType *sni_name_set_failure                           = nullptr;
  Metrics::Counter::AtomicType *total_attempts_handshake_count_in              = nullptr;
  Metrics::Counter::AtomicType *total_attempts_handshake_count_out             = nullptr;
  Metrics::Counter::AtomicType *total_dyn_def_tls_record_count                 = nullptr;
  Metrics::Counter::AtomicType *total_dyn_max_tls_record_count                 = nullptr;
  Metrics::Counter::AtomicType *total_dyn_redo_tls_record_count                = nullptr;
  Metrics::Counter::AtomicType *total_handshake_time                           = nullptr;
  Metrics::Counter::AtomicType *total_sslv3                                    = nullptr;
  Metrics::Counter::AtomicType *total_success_handshake_count_in               = nullptr;
  Metrics::Counter::AtomicType *total_success_handshake_count_out              = nullptr;
  Metrics::Counter::AtomicType *total_ticket_keys_renewed                      = nullptr;
  Metrics::Counter::AtomicType *total_tickets_created                          = nullptr;
  Metrics::Counter::AtomicType *total_tickets_not_found                        = nullptr;
  Metrics::Counter::AtomicType *total_tickets_renewed                          = nullptr;
  Metrics::Counter::AtomicType *total_tickets_verified_old_key                 = nullptr;
  Metrics::Counter::AtomicType *total_tickets_verified                         = nullptr;
  Metrics::Counter::AtomicType *total_tlsv1                                    = nullptr;
  Metrics::Counter::AtomicType *total_tlsv11                                   = nullptr;
  Metrics::Counter::AtomicType *total_tlsv12                                   = nullptr;
  Metrics::Counter::AtomicType *total_tlsv13                                   = nullptr;
  Metrics::Counter::AtomicType *user_agent_bad_cert                            = nullptr;
  Metrics::Counter::AtomicType *user_agent_cert_verify_failed                  = nullptr;
  Metrics::Counter::AtomicType *user_agent_decryption_failed                   = nullptr;
  Metrics::Counter::AtomicType *user_agent_decryption_failed_or_bad_record_mac = nullptr;
  Metrics::Counter::AtomicType *user_agent_expired_cert                        = nullptr;
  Metrics::Counter::AtomicType *user_agent_http_request                        = nullptr;
  Metrics::Counter::AtomicType *user_agent_inappropriate_fallback              = nullptr;
  Metrics::Counter::AtomicType *user_agent_no_shared_cipher                    = nullptr;
  Metrics::Counter::AtomicType *user_agent_other_errors                        = nullptr;
  Metrics::Counter::AtomicType *user_agent_revoked_cert                        = nullptr;
  Metrics::Counter::AtomicType *user_agent_unknown_ca                          = nullptr;
  Metrics::Counter::AtomicType *user_agent_unknown_cert                        = nullptr;
  Metrics::Counter::AtomicType *user_agent_version_too_high                    = nullptr;
  Metrics::Counter::AtomicType *user_agent_version_too_low                     = nullptr;
  Metrics::Counter::AtomicType *user_agent_wrong_version                       = nullptr;
  Metrics::Gauge::AtomicType   *user_agent_session_hit                         = nullptr;
  Metrics::Gauge::AtomicType   *user_agent_session_miss                        = nullptr;
  Metrics::Gauge::AtomicType   *user_agent_session_timeout                     = nullptr;
  Metrics::Gauge::AtomicType   *user_agent_sessions                            = nullptr;
};

extern SSLStatsBlock                                                   ssl_rsb;
extern std::unordered_map<std::string, Metrics::Counter::AtomicType *> cipher_map;

#if defined(OPENSSL_IS_BORINGSSL)
extern std::unordered_map<std::string, Metrics::Counter::AtomicType *> tls_group_map;
#elif defined(SSL_get_negotiated_group)
extern std::unordered_map<int, Metrics::Counter::AtomicType *> tls_group_map;
constexpr int                                                  SSL_GROUP_STAT_OTHER_KEY = 0;
#endif

// Initialize SSL statistics.
void SSLInitializeStatistics();

const std::string SSL_CIPHER_STAT_OTHER = "OTHER";
