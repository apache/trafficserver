/** @file

  CompletedTransactionLogData populates TransactionLogData from a live HttpSM.

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

#include "proxy/logging/TransactionLogData.h"

class HttpSM;

/** Provide TransactionLogData from a live HttpSM via virtual getters.
 *
 * Each getter reads directly from m_http_sm on demand, avoiding the need to
 * copy all fields upfront. The HttpSM must outlive this object.
 */
class CompletedTransactionLogData : public TransactionLogData
{
public:
  /** Construct from a live HttpSM.
   *
   * @param[in] sm The HttpSM for the completing transaction.
   */
  explicit CompletedTransactionLogData(HttpSM *sm);

  void *http_sm_for_plugins() const override;

  // ===== Milestones =====
  TransactionMilestones const *get_milestones() const override;

  // ===== Headers =====
  HTTPHdr *get_client_request() const override;
  HTTPHdr *get_proxy_response() const override;
  HTTPHdr *get_proxy_request() const override;
  HTTPHdr *get_server_response() const override;
  HTTPHdr *get_cache_response() const override;

  // ===== Client request URL / path =====
  const char *get_client_req_url_str() const override;
  int         get_client_req_url_len() const override;
  const char *get_client_req_url_path_str() const override;
  int         get_client_req_url_path_len() const override;

  // ===== Proxy response content-type / reason =====
  char *get_proxy_resp_content_type_str() const override;
  int   get_proxy_resp_content_type_len() const override;
  char *get_proxy_resp_reason_phrase_str() const override;
  int   get_proxy_resp_reason_phrase_len() const override;

  // ===== Unmapped URL =====
  char *get_unmapped_url_str() const override;
  int   get_unmapped_url_len() const override;

  // ===== Cache lookup URL =====
  char *get_cache_lookup_url_str() const override;
  int   get_cache_lookup_url_len() const override;

  // ===== Client addressing =====
  sockaddr const *get_client_addr() const override;
  sockaddr const *get_client_src_addr() const override;
  sockaddr const *get_client_dst_addr() const override;
  sockaddr const *get_verified_client_addr() const override;
  uint16_t        get_client_port() const override;

  // ===== Server addressing =====
  sockaddr const *get_server_src_addr() const override;
  sockaddr const *get_server_dst_addr() const override;
  sockaddr const *get_server_info_dst_addr() const override;
  const char     *get_server_name() const override;

  // ===== Squid codes =====
  SquidLogCode       get_log_code() const override;
  SquidSubcode       get_subcode() const override;
  SquidHitMissCode   get_hit_miss_code() const override;
  SquidHierarchyCode get_hier_code() const override;

  // ===== Byte counters =====
  int64_t get_client_request_body_bytes() const override;
  int64_t get_client_response_hdr_bytes() const override;
  int64_t get_client_response_body_bytes() const override;
  int64_t get_server_request_body_bytes() const override;
  int64_t get_server_response_body_bytes() const override;
  int64_t get_cache_response_body_bytes() const override;
  int64_t get_cache_response_hdr_bytes() const override;

  // ===== Transaction identifiers =====
  int64_t get_sm_id() const override;
  int64_t get_connection_id() const override;
  int     get_transaction_id() const override;
  int     get_transaction_priority_weight() const override;
  int     get_transaction_priority_dependence() const override;

  // ===== Plugin info =====
  int64_t     get_plugin_id() const override;
  const char *get_plugin_tag() const override;

  // ===== Protocol info =====
  const char *get_client_protocol() const override;
  const char *get_server_protocol() const override;
  const char *get_client_sec_protocol() const override;
  const char *get_client_cipher_suite() const override;
  const char *get_client_curve() const override;
  const char *get_client_security_group() const override;
  int         get_client_alpn_id() const override;

  // ===== SNI =====
  const char *get_sni_server_name() const override;

  // ===== Connection flags =====
  bool get_client_tcp_reused() const override;
  bool get_client_connection_is_ssl() const override;
  bool get_client_ssl_reused() const override;
  bool get_is_internal() const override;
  bool get_server_connection_is_ssl() const override;
  bool get_server_ssl_reused() const override;
  int  get_server_connection_provided_cert() const override;
  int  get_client_provided_cert() const override;

  // ===== Server transaction count =====
  int64_t get_server_transact_count() const override;

  // ===== Finish status =====
  int get_client_finish_status_code() const override;
  int get_proxy_finish_status_code() const override;

  // ===== Error codes =====
  const char *get_client_rx_error_code() const override;
  const char *get_client_tx_error_code() const override;

  // ===== MPTCP =====
  std::optional<bool> get_mptcp_state() const override;

  // ===== Misc transaction state =====
  in_port_t get_incoming_port() const override;
  int       get_orig_scheme() const override;
  int64_t   get_congestion_control_crat() const override;

  // ===== Cache state =====
  int get_cache_write_code() const override;
  int get_cache_transform_write_code() const override;
  int get_cache_open_read_tries() const override;
  int get_cache_open_write_tries() const override;
  int get_max_cache_open_write_retries() const override;

  // ===== Retry attempts =====
  int64_t get_simple_retry_attempts() const override;
  int64_t get_unavailable_retry_attempts() const override;
  int64_t get_retry_attempts_saved() const override;

  // ===== Status plugin entry name =====
  std::string_view get_http_return_code_setter_name() const override;

  // ===== Proxy Protocol =====
  int              get_pp_version() const override;
  sockaddr const  *get_pp_src_addr() const override;
  sockaddr const  *get_pp_dst_addr() const override;
  std::string_view get_pp_authority() const override;
  std::string_view get_pp_tls_cipher() const override;
  std::string_view get_pp_tls_version() const override;
  std::string_view get_pp_tls_group() const override;

  // ===== Server response Transfer-Encoding =====
  std::string_view get_server_response_transfer_encoding() const override;

private:
  HttpSM *m_http_sm;

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
