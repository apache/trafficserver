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

#include <unordered_map>

#include "records/RecProcess.h"
#include "SSLDiags.h"

#include "api/Metrics.h"

using ts::Metrics;

// For some odd reason, these have to be initialized with nullptr, because the order
// of initialization and how we load certs is weird... In reality only the metric
// for ssl_rsb.total_ticket_keys_renewed needs this initialization, but lets be
// consistent at least.
struct SSLStatsBlock {
  ts::Metrics::IntType *early_data_received_count          = nullptr;
  ts::Metrics::IntType *error_async                        = nullptr;
  ts::Metrics::IntType *error_ssl                          = nullptr;
  ts::Metrics::IntType *error_syscall                      = nullptr;
  ts::Metrics::IntType *ocsp_refresh_cert_failure          = nullptr;
  ts::Metrics::IntType *ocsp_refreshed_cert                = nullptr;
  ts::Metrics::IntType *ocsp_revoked_cert                  = nullptr;
  ts::Metrics::IntType *ocsp_unknown_cert                  = nullptr;
  ts::Metrics::IntType *origin_server_bad_cert             = nullptr;
  ts::Metrics::IntType *origin_server_cert_verify_failed   = nullptr;
  ts::Metrics::IntType *origin_server_decryption_failed    = nullptr;
  ts::Metrics::IntType *origin_server_expired_cert         = nullptr;
  ts::Metrics::IntType *origin_server_other_errors         = nullptr;
  ts::Metrics::IntType *origin_server_revoked_cert         = nullptr;
  ts::Metrics::IntType *origin_server_unknown_ca           = nullptr;
  ts::Metrics::IntType *origin_server_unknown_cert         = nullptr;
  ts::Metrics::IntType *origin_server_wrong_version        = nullptr;
  ts::Metrics::IntType *origin_session_cache_hit           = nullptr;
  ts::Metrics::IntType *origin_session_cache_miss          = nullptr;
  ts::Metrics::IntType *origin_session_reused_count        = nullptr;
  ts::Metrics::IntType *session_cache_eviction             = nullptr;
  ts::Metrics::IntType *session_cache_hit                  = nullptr;
  ts::Metrics::IntType *session_cache_lock_contention      = nullptr;
  ts::Metrics::IntType *session_cache_miss                 = nullptr;
  ts::Metrics::IntType *session_cache_new_session          = nullptr;
  ts::Metrics::IntType *sni_name_set_failure               = nullptr;
  ts::Metrics::IntType *total_attempts_handshake_count_in  = nullptr;
  ts::Metrics::IntType *total_attempts_handshake_count_out = nullptr;
  ts::Metrics::IntType *total_dyn_def_tls_record_count     = nullptr;
  ts::Metrics::IntType *total_dyn_max_tls_record_count     = nullptr;
  ts::Metrics::IntType *total_dyn_redo_tls_record_count    = nullptr;
  ts::Metrics::IntType *total_handshake_time               = nullptr;
  ts::Metrics::IntType *total_sslv3                        = nullptr;
  ts::Metrics::IntType *total_success_handshake_count_in   = nullptr;
  ts::Metrics::IntType *total_success_handshake_count_out  = nullptr;
  ts::Metrics::IntType *total_ticket_keys_renewed          = nullptr;
  ts::Metrics::IntType *total_tickets_created              = nullptr;
  ts::Metrics::IntType *total_tickets_not_found            = nullptr;
  ts::Metrics::IntType *total_tickets_renewed              = nullptr;
  ts::Metrics::IntType *total_tickets_verified_old_key     = nullptr;
  ts::Metrics::IntType *total_tickets_verified             = nullptr;
  ts::Metrics::IntType *total_tlsv1                        = nullptr;
  ts::Metrics::IntType *total_tlsv11                       = nullptr;
  ts::Metrics::IntType *total_tlsv12                       = nullptr;
  ts::Metrics::IntType *total_tlsv13                       = nullptr;
  ts::Metrics::IntType *user_agent_bad_cert                = nullptr;
  ts::Metrics::IntType *user_agent_cert_verify_failed      = nullptr;
  ts::Metrics::IntType *user_agent_decryption_failed       = nullptr;
  ts::Metrics::IntType *user_agent_expired_cert            = nullptr;
  ts::Metrics::IntType *user_agent_other_errors            = nullptr;
  ts::Metrics::IntType *user_agent_revoked_cert            = nullptr;
  ts::Metrics::IntType *user_agent_session_hit             = nullptr;
  ts::Metrics::IntType *user_agent_session_miss            = nullptr;
  ts::Metrics::IntType *user_agent_session_timeout         = nullptr;
  ts::Metrics::IntType *user_agent_sessions                = nullptr;
  ts::Metrics::IntType *user_agent_unknown_ca              = nullptr;
  ts::Metrics::IntType *user_agent_unknown_cert            = nullptr;
  ts::Metrics::IntType *user_agent_wrong_version           = nullptr;
};

extern SSLStatsBlock ssl_rsb;
extern std::unordered_map<std::string, Metrics::IntType *> cipher_map;

// Initialize SSL statistics.
void SSLInitializeStatistics();

const std::string SSL_CIPHER_STAT_OTHER = "OTHER";
