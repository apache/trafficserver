/** @file

  Base class providing the data interface for access log entries.

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

class HTTPHdr;

/** Abstract base for the data backing a single access log entry.
 *
 * Subclasses provide data from the appropriate source via virtual getters:
 *   - CompletedTransactionLogData reads from HttpSM (defined in the http
 *     module) for transactions that completed normally.
 *   - PreTransactionLogData returns owned storage (defined in the proxy
 *     module), for requests that never create an HttpSM.
 *
 * LogAccess reads only from this interface, so the logging module has no
 * compile-time dependency on the http module.
 */
class TransactionLogData
{
public:
  virtual ~TransactionLogData() = default;

  /** Return the HttpSM pointer for plugin custom marshal functions.
   *
   * Only TransactionLogData provides a non-null value.  Pre-transaction
   * entries have no HttpSM, so plugins receive nullptr.
   *
   * @return An opaque pointer to the HttpSM, or nullptr.
   */
  virtual void *
  http_sm_for_plugins() const
  {
    return nullptr;
  }

  // ===== Milestones =====

  virtual TransactionMilestones const *
  get_milestones() const
  {
    return nullptr;
  }

  // ===== Headers =====

  virtual HTTPHdr *
  get_client_request() const
  {
    return nullptr;
  }
  virtual HTTPHdr *
  get_proxy_response() const
  {
    return nullptr;
  }
  virtual HTTPHdr *
  get_proxy_request() const
  {
    return nullptr;
  }
  virtual HTTPHdr *
  get_server_response() const
  {
    return nullptr;
  }
  virtual HTTPHdr *
  get_cache_response() const
  {
    return nullptr;
  }

  // ===== Client request URL / path =====

  virtual const char *
  get_client_req_url_str() const
  {
    return nullptr;
  }
  virtual int
  get_client_req_url_len() const
  {
    return 0;
  }
  virtual const char *
  get_client_req_url_path_str() const
  {
    return nullptr;
  }
  virtual int
  get_client_req_url_path_len() const
  {
    return 0;
  }

  // ===== Proxy response content-type / reason =====

  virtual char *
  get_proxy_resp_content_type_str() const
  {
    return nullptr;
  }
  virtual int
  get_proxy_resp_content_type_len() const
  {
    return 0;
  }
  virtual char *
  get_proxy_resp_reason_phrase_str() const
  {
    return nullptr;
  }
  virtual int
  get_proxy_resp_reason_phrase_len() const
  {
    return 0;
  }

  // ===== Unmapped URL =====

  virtual char *
  get_unmapped_url_str() const
  {
    return nullptr;
  }
  virtual int
  get_unmapped_url_len() const
  {
    return 0;
  }

  // ===== Cache lookup URL =====

  virtual char *
  get_cache_lookup_url_str() const
  {
    return nullptr;
  }
  virtual int
  get_cache_lookup_url_len() const
  {
    return 0;
  }

  // ===== Client addressing =====

  virtual sockaddr const *
  get_client_addr() const
  {
    return nullptr;
  }
  virtual sockaddr const *
  get_client_src_addr() const
  {
    return nullptr;
  }
  virtual sockaddr const *
  get_client_dst_addr() const
  {
    return nullptr;
  }
  virtual sockaddr const *
  get_verified_client_addr() const
  {
    return nullptr;
  }
  virtual uint16_t
  get_client_port() const
  {
    return 0;
  }

  // ===== Server addressing =====

  virtual sockaddr const *
  get_server_src_addr() const
  {
    return nullptr;
  }
  virtual sockaddr const *
  get_server_dst_addr() const
  {
    return nullptr;
  }
  virtual sockaddr const *
  get_server_info_dst_addr() const
  {
    return nullptr;
  }
  virtual const char *
  get_server_name() const
  {
    return nullptr;
  }

  // ===== Squid codes =====

  virtual SquidLogCode
  get_log_code() const
  {
    return SquidLogCode::EMPTY;
  }
  virtual SquidSubcode
  get_subcode() const
  {
    return SquidSubcode::EMPTY;
  }
  virtual SquidHitMissCode
  get_hit_miss_code() const
  {
    return SQUID_MISS_NONE;
  }
  virtual SquidHierarchyCode
  get_hier_code() const
  {
    return SquidHierarchyCode::NONE;
  }

  // ===== Byte counters =====

  virtual int64_t
  get_client_request_body_bytes() const
  {
    return 0;
  }
  virtual int64_t
  get_client_response_hdr_bytes() const
  {
    return 0;
  }
  virtual int64_t
  get_client_response_body_bytes() const
  {
    return 0;
  }
  virtual int64_t
  get_server_request_body_bytes() const
  {
    return 0;
  }
  virtual int64_t
  get_server_response_body_bytes() const
  {
    return 0;
  }
  virtual int64_t
  get_cache_response_body_bytes() const
  {
    return 0;
  }
  virtual int64_t
  get_cache_response_hdr_bytes() const
  {
    return 0;
  }

  // ===== Transaction identifiers =====

  virtual int64_t
  get_sm_id() const
  {
    return 0;
  }
  virtual int64_t
  get_connection_id() const
  {
    return 0;
  }
  virtual int
  get_transaction_id() const
  {
    return 0;
  }
  virtual int
  get_transaction_priority_weight() const
  {
    return 0;
  }
  virtual int
  get_transaction_priority_dependence() const
  {
    return 0;
  }

  // ===== Plugin info =====

  virtual int64_t
  get_plugin_id() const
  {
    return 0;
  }
  virtual const char *
  get_plugin_tag() const
  {
    return nullptr;
  }

  // ===== Protocol info =====

  virtual const char *
  get_client_protocol() const
  {
    return nullptr;
  }
  virtual const char *
  get_server_protocol() const
  {
    return nullptr;
  }
  virtual const char *
  get_client_sec_protocol() const
  {
    return nullptr;
  }
  virtual const char *
  get_client_cipher_suite() const
  {
    return nullptr;
  }
  virtual const char *
  get_client_curve() const
  {
    return nullptr;
  }
  virtual const char *
  get_client_security_group() const
  {
    return nullptr;
  }
  virtual int
  get_client_alpn_id() const
  {
    return -1;
  }

  // ===== SNI =====

  virtual const char *
  get_sni_server_name() const
  {
    return nullptr;
  }

  // ===== Connection flags =====

  virtual bool
  get_client_tcp_reused() const
  {
    return false;
  }
  virtual bool
  get_client_connection_is_ssl() const
  {
    return false;
  }
  virtual bool
  get_client_ssl_reused() const
  {
    return false;
  }
  virtual bool
  get_is_internal() const
  {
    return false;
  }
  virtual bool
  get_server_connection_is_ssl() const
  {
    return false;
  }
  virtual bool
  get_server_ssl_reused() const
  {
    return false;
  }
  virtual int
  get_server_connection_provided_cert() const
  {
    return 0;
  }
  virtual int
  get_client_provided_cert() const
  {
    return 0;
  }

  // ===== Server transaction count =====

  virtual int64_t
  get_server_transact_count() const
  {
    return 0;
  }

  // ===== Finish status =====

  virtual int
  get_client_finish_status_code() const
  {
    return 0;
  }
  virtual int
  get_proxy_finish_status_code() const
  {
    return 0;
  }

  // ===== Error codes =====

  virtual const char *
  get_client_rx_error_code() const
  {
    return "-";
  }
  virtual const char *
  get_client_tx_error_code() const
  {
    return "-";
  }

  // ===== MPTCP =====

  virtual std::optional<bool>
  get_mptcp_state() const
  {
    return std::nullopt;
  }

  // ===== Misc transaction state =====

  virtual in_port_t
  get_incoming_port() const
  {
    return 0;
  }
  virtual int
  get_orig_scheme() const
  {
    return -1;
  }
  virtual int64_t
  get_congestion_control_crat() const
  {
    return 0;
  }

  // ===== Cache state =====

  virtual int
  get_cache_write_code() const
  {
    return 0;
  }
  virtual int
  get_cache_transform_write_code() const
  {
    return 0;
  }
  virtual int
  get_cache_open_read_tries() const
  {
    return 0;
  }
  virtual int
  get_cache_open_write_tries() const
  {
    return 0;
  }
  virtual int
  get_max_cache_open_write_retries() const
  {
    return -1;
  }

  // ===== Retry attempts =====

  virtual int64_t
  get_simple_retry_attempts() const
  {
    return 0;
  }
  virtual int64_t
  get_unavailable_retry_attempts() const
  {
    return 0;
  }
  virtual int64_t
  get_retry_attempts_saved() const
  {
    return 0;
  }

  // ===== Status plugin entry name =====

  virtual std::string_view
  get_http_return_code_setter_name() const
  {
    return {};
  }

  // ===== Proxy Protocol =====

  virtual int
  get_pp_version() const
  {
    return 0;
  }
  virtual sockaddr const *
  get_pp_src_addr() const
  {
    return nullptr;
  }
  virtual sockaddr const *
  get_pp_dst_addr() const
  {
    return nullptr;
  }
  virtual std::string_view
  get_pp_authority() const
  {
    return {};
  }
  virtual std::string_view
  get_pp_tls_cipher() const
  {
    return {};
  }
  virtual std::string_view
  get_pp_tls_version() const
  {
    return {};
  }
  virtual std::string_view
  get_pp_tls_group() const
  {
    return {};
  }

  // ===== Server response Transfer-Encoding =====

  virtual std::string_view
  get_server_response_transfer_encoding() const
  {
    return {};
  }

  // ===== Fallback fields for pre-transaction logging =====

  virtual std::string_view
  get_method() const
  {
    return {};
  }
  virtual std::string_view
  get_scheme() const
  {
    return {};
  }
  virtual std::string_view
  get_client_protocol_str() const
  {
    return {};
  }

  // noncopyable
  TransactionLogData(const TransactionLogData &)            = delete;
  TransactionLogData &operator=(const TransactionLogData &) = delete;

protected:
  TransactionLogData() = default;
};
