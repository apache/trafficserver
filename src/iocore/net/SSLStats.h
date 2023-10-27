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
#include "iocore/net/SSLDiags.h"

#include "api/Metrics.h"

using ts::Metrics::Counter;

// For some odd reason, these have to be initialized with nullptr, because the order
// of initialization and how we load certs is weird... In reality only the metric
// for ssl_rsb.total_ticket_keys_renewed needs this initialization, but lets be
// consistent at least.
struct SSLStatsBlock {
  Counter::AtomicType *early_data_received_count          = nullptr;
  Counter::AtomicType *error_async                        = nullptr;
  Counter::AtomicType *error_ssl                          = nullptr;
  Counter::AtomicType *error_syscall                      = nullptr;
  Counter::AtomicType *ocsp_refresh_cert_failure          = nullptr;
  Counter::AtomicType *ocsp_refreshed_cert                = nullptr;
  Counter::AtomicType *ocsp_revoked_cert                  = nullptr;
  Counter::AtomicType *ocsp_unknown_cert                  = nullptr;
  Counter::AtomicType *origin_server_bad_cert             = nullptr;
  Counter::AtomicType *origin_server_cert_verify_failed   = nullptr;
  Counter::AtomicType *origin_server_decryption_failed    = nullptr;
  Counter::AtomicType *origin_server_expired_cert         = nullptr;
  Counter::AtomicType *origin_server_other_errors         = nullptr;
  Counter::AtomicType *origin_server_revoked_cert         = nullptr;
  Counter::AtomicType *origin_server_unknown_ca           = nullptr;
  Counter::AtomicType *origin_server_unknown_cert         = nullptr;
  Counter::AtomicType *origin_server_wrong_version        = nullptr;
  Counter::AtomicType *origin_session_cache_hit           = nullptr;
  Counter::AtomicType *origin_session_cache_miss          = nullptr;
  Counter::AtomicType *origin_session_reused_count        = nullptr;
  Counter::AtomicType *session_cache_eviction             = nullptr;
  Counter::AtomicType *session_cache_hit                  = nullptr;
  Counter::AtomicType *session_cache_lock_contention      = nullptr;
  Counter::AtomicType *session_cache_miss                 = nullptr;
  Counter::AtomicType *session_cache_new_session          = nullptr;
  Counter::AtomicType *sni_name_set_failure               = nullptr;
  Counter::AtomicType *total_attempts_handshake_count_in  = nullptr;
  Counter::AtomicType *total_attempts_handshake_count_out = nullptr;
  Counter::AtomicType *total_dyn_def_tls_record_count     = nullptr;
  Counter::AtomicType *total_dyn_max_tls_record_count     = nullptr;
  Counter::AtomicType *total_dyn_redo_tls_record_count    = nullptr;
  Counter::AtomicType *total_handshake_time               = nullptr;
  Counter::AtomicType *total_sslv3                        = nullptr;
  Counter::AtomicType *total_success_handshake_count_in   = nullptr;
  Counter::AtomicType *total_success_handshake_count_out  = nullptr;
  Counter::AtomicType *total_ticket_keys_renewed          = nullptr;
  Counter::AtomicType *total_tickets_created              = nullptr;
  Counter::AtomicType *total_tickets_not_found            = nullptr;
  Counter::AtomicType *total_tickets_renewed              = nullptr;
  Counter::AtomicType *total_tickets_verified_old_key     = nullptr;
  Counter::AtomicType *total_tickets_verified             = nullptr;
  Counter::AtomicType *total_tlsv1                        = nullptr;
  Counter::AtomicType *total_tlsv11                       = nullptr;
  Counter::AtomicType *total_tlsv12                       = nullptr;
  Counter::AtomicType *total_tlsv13                       = nullptr;
  Counter::AtomicType *user_agent_bad_cert                = nullptr;
  Counter::AtomicType *user_agent_cert_verify_failed      = nullptr;
  Counter::AtomicType *user_agent_decryption_failed       = nullptr;
  Counter::AtomicType *user_agent_expired_cert            = nullptr;
  Counter::AtomicType *user_agent_other_errors            = nullptr;
  Counter::AtomicType *user_agent_revoked_cert            = nullptr;
  Counter::AtomicType *user_agent_session_hit             = nullptr;
  Counter::AtomicType *user_agent_session_miss            = nullptr;
  Counter::AtomicType *user_agent_session_timeout         = nullptr;
  Counter::AtomicType *user_agent_sessions                = nullptr;
  Counter::AtomicType *user_agent_unknown_ca              = nullptr;
  Counter::AtomicType *user_agent_unknown_cert            = nullptr;
  Counter::AtomicType *user_agent_wrong_version           = nullptr;
};

extern SSLStatsBlock ssl_rsb;
extern std::unordered_map<std::string, Counter::AtomicType *> cipher_map;

// Initialize SSL statistics.
void SSLInitializeStatistics();

const std::string SSL_CIPHER_STAT_OTHER = "OTHER";
