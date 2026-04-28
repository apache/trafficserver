/** @file

  Concrete data accessor for access log entries.

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

#include "proxy/Milestones.h"
#include "proxy/hdrs/HTTP.h"
#include "tscore/ink_inet.h"

#include <optional>
#include <string_view>

class HttpSM;
class PreTransactionLogData;

/** Provide access-log data from either a completed HttpSM or pre-transaction storage.
 *
 * The common completed-transaction path reads directly from @c HttpSM.  The
 * rare pre-transaction path reads from @c PreTransactionLogData, which owns
 * copied request/session state for malformed requests rejected before HttpSM
 * creation.
 */
class TransactionLogData
{
public:
  explicit TransactionLogData(HttpSM *sm);
  explicit TransactionLogData(PreTransactionLogData const &pre_data);

  void *http_sm_for_plugins() const;

  // ===== Milestones =====
  TransactionMilestones const *get_milestones() const;

  // ===== Headers =====
  HTTPHdr *get_client_request() const;
  HTTPHdr *get_proxy_response() const;
  HTTPHdr *get_proxy_request() const;
  HTTPHdr *get_server_response() const;
  HTTPHdr *get_cache_response() const;

  // ===== Client request URL / path =====
  const char *get_client_req_url_str() const;
  int         get_client_req_url_len() const;
  const char *get_client_req_url_path_str() const;
  int         get_client_req_url_path_len() const;

  // ===== Proxy response content-type / reason =====
  char *get_proxy_resp_content_type_str() const;
  int   get_proxy_resp_content_type_len() const;
  char *get_proxy_resp_reason_phrase_str() const;
  int   get_proxy_resp_reason_phrase_len() const;

  // ===== Unmapped URL =====
  char *get_unmapped_url_str() const;
  int   get_unmapped_url_len() const;

  // ===== Cache lookup URL =====
  char *get_cache_lookup_url_str() const;
  int   get_cache_lookup_url_len() const;

  // ===== Client addressing =====
  sockaddr const *get_client_addr() const;
  sockaddr const *get_client_src_addr() const;
  sockaddr const *get_client_dst_addr() const;
  sockaddr const *get_verified_client_addr() const;
  uint16_t        get_client_port() const;

  // ===== Server addressing =====
  sockaddr const *get_server_src_addr() const;
  sockaddr const *get_server_dst_addr() const;
  sockaddr const *get_server_info_dst_addr() const;
  const char     *get_server_name() const;

  // ===== Squid codes =====
  SquidLogCode       get_log_code() const;
  SquidSubcode       get_subcode() const;
  SquidHitMissCode   get_hit_miss_code() const;
  SquidHierarchyCode get_hier_code() const;

  // ===== Byte counters =====
  int64_t get_client_request_body_bytes() const;
  int64_t get_client_response_hdr_bytes() const;
  int64_t get_client_response_body_bytes() const;
  int64_t get_server_request_body_bytes() const;
  int64_t get_server_response_body_bytes() const;
  int64_t get_cache_response_body_bytes() const;
  int64_t get_cache_response_hdr_bytes() const;

  // ===== Transaction identifiers =====
  int64_t get_sm_id() const;
  int64_t get_connection_id() const;
  int     get_transaction_id() const;
  int     get_transaction_priority_weight() const;
  int     get_transaction_priority_dependence() const;

  // ===== Plugin info =====
  int64_t     get_plugin_id() const;
  const char *get_plugin_tag() const;

  // ===== Protocol info =====
  const char *get_client_protocol() const;
  const char *get_server_protocol() const;
  const char *get_client_sec_protocol() const;
  const char *get_client_cipher_suite() const;
  const char *get_client_curve() const;
  const char *get_client_security_group() const;
  int         get_client_alpn_id() const;

  // ===== SNI =====
  const char *get_sni_server_name() const;

  // ===== Connection flags =====
  bool get_client_tcp_reused() const;
  bool get_client_connection_is_ssl() const;
  bool get_client_ssl_reused() const;
  int  get_client_ssl_resumption_type() const;
  bool get_is_internal() const;
  bool get_server_connection_is_ssl() const;
  bool get_server_ssl_reused() const;
  int  get_server_connection_provided_cert() const;
  int  get_client_provided_cert() const;

  // ===== Server transaction count =====
  int64_t get_server_transact_count() const;

  // ===== Finish status =====
  int get_client_finish_status_code() const;
  int get_proxy_finish_status_code() const;

  // ===== Error codes =====
  const char *get_client_rx_error_code() const;
  const char *get_client_tx_error_code() const;

  // ===== MPTCP =====
  std::optional<bool> get_mptcp_state() const;

  // ===== Misc transaction state =====
  in_port_t get_incoming_port() const;
  int       get_orig_scheme() const;
  int64_t   get_congestion_control_crat() const;

  // ===== Cache state =====
  int get_cache_write_code() const;
  int get_cache_transform_write_code() const;
  int get_cache_open_read_tries() const;
  int get_cache_open_write_tries() const;
  int get_max_cache_open_write_retries() const;

  // ===== Retry attempts =====
  int64_t get_simple_retry_attempts() const;
  int64_t get_unavailable_retry_attempts() const;
  int64_t get_retry_attempts_saved() const;

  // ===== Status plugin entry name =====
  std::string_view get_http_return_code_setter_name() const;

  // ===== Proxy Protocol =====
  int              get_pp_version() const;
  sockaddr const  *get_pp_src_addr() const;
  sockaddr const  *get_pp_dst_addr() const;
  std::string_view get_pp_authority() const;
  std::string_view get_pp_tls_cipher() const;
  std::string_view get_pp_tls_version() const;
  std::string_view get_pp_tls_group() const;

  // ===== Server response Transfer-Encoding =====
  std::string_view get_server_response_transfer_encoding() const;

  // ===== Fallback fields for pre-transaction logging =====
  std::string_view get_method() const;
  std::string_view get_scheme() const;
  std::string_view get_client_protocol_str() const;

  TransactionLogData(const TransactionLogData &)            = delete;
  TransactionLogData &operator=(const TransactionLogData &) = delete;

private:
  HttpSM                      *m_http_sm  = nullptr;
  PreTransactionLogData const *m_pre_data = nullptr;

  // Cached values for fields that require computation or string formatting.
  mutable char m_client_rx_error_code[10] = {'-', '\0'};
  mutable char m_client_tx_error_code[10] = {'-', '\0'};
  mutable bool m_error_codes_formatted    = false;

  // Cached URL string pointers (computed on first access).
  mutable const char *m_client_req_url_str      = nullptr;
  mutable int         m_client_req_url_len      = 0;
  mutable const char *m_client_req_url_path_str = nullptr;
  mutable int         m_client_req_url_path_len = 0;
  mutable bool        m_url_cached              = false;

  // Cached content-type pointers (computed on first access).
  mutable char *m_proxy_resp_content_type_str  = nullptr;
  mutable int   m_proxy_resp_content_type_len  = 0;
  mutable char *m_proxy_resp_reason_phrase_str = nullptr;
  mutable int   m_proxy_resp_reason_phrase_len = 0;
  mutable bool  m_content_type_cached          = false;

  void cache_url_strings() const;
  void cache_content_type() const;
  void format_error_codes() const;
};
