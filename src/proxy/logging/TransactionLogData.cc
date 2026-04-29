/** @file

  TransactionLogData implementation.

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

#include "proxy/logging/TransactionLogData.h"
#include "proxy/PreTransactionLogData.h"
#include "proxy/http/HttpSM.h"
#include "proxy/logging/LogAccess.h"
#include "proxy/hdrs/MIME.h"
#include "tscore/ink_defs.h"
#include "../private/SSLProxySession.h"

namespace
{
/** Map HttpTransact::CacheWriteStatus_t to LogCacheWriteCodeType. */
int
convert_cache_write_code(HttpTransact::CacheWriteStatus_t t)
{
  switch (t) {
  case HttpTransact::CacheWriteStatus_t::NO_WRITE:
    return LOG_CACHE_WRITE_NONE;
  case HttpTransact::CacheWriteStatus_t::LOCK_MISS:
    return LOG_CACHE_WRITE_LOCK_MISSED;
  case HttpTransact::CacheWriteStatus_t::IN_PROGRESS:
    return LOG_CACHE_WRITE_LOCK_ABORTED;
  case HttpTransact::CacheWriteStatus_t::ERROR:
    return LOG_CACHE_WRITE_ERROR;
  case HttpTransact::CacheWriteStatus_t::COMPLETE:
    return LOG_CACHE_WRITE_COMPLETE;
  default:
    ink_assert(!"bad cache write code");
    return LOG_CACHE_WRITE_NONE;
  }
}

int
compute_client_finish_status(HttpSM *sm)
{
  HttpTransact::AbortState_t cl_abort_state = sm->t_state.client_info.abort;
  if (cl_abort_state == HttpTransact::ABORTED) {
    if (sm->t_state.client_info.state == HttpTransact::ACTIVE_TIMEOUT ||
        sm->t_state.client_info.state == HttpTransact::INACTIVE_TIMEOUT) {
      return LOG_FINISH_TIMEOUT;
    }
    return LOG_FINISH_INTR;
  }
  return LOG_FINISH_FIN;
}

int
compute_proxy_finish_status(HttpSM *sm)
{
  if (sm->t_state.current.server) {
    switch (sm->t_state.current.server->state) {
    case HttpTransact::ACTIVE_TIMEOUT:
    case HttpTransact::INACTIVE_TIMEOUT:
      return LOG_FINISH_TIMEOUT;
    case HttpTransact::CONNECTION_ERROR:
      return LOG_FINISH_INTR;
    default:
      if (sm->t_state.current.server->abort == HttpTransact::ABORTED) {
        return LOG_FINISH_INTR;
      }
      break;
    }
  }
  return LOG_FINISH_FIN;
}
} // end anonymous namespace

TransactionLogData::TransactionLogData(HttpSM *sm) : m_http_sm(sm)
{
  ink_assert(sm != nullptr);
}

TransactionLogData::TransactionLogData(PreTransactionLogData const &pre_data) : m_pre_data(&pre_data) {}

void *
TransactionLogData::http_sm_for_plugins() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm;
  }
  return nullptr;
}

// ===== Milestones =====

TransactionMilestones const *
TransactionLogData::get_milestones() const
{
  if (likely(m_http_sm != nullptr)) {
    return &m_http_sm->milestones;
  }
  return &m_pre_data->owned_milestones;
}

// ===== Headers =====

HTTPHdr *
TransactionLogData::get_client_request() const
{
  if (likely(m_http_sm != nullptr)) {
    HttpTransact::HeaderInfo *hdr = &m_http_sm->t_state.hdr_info;
    if (hdr->client_request.valid()) {
      return &hdr->client_request;
    }
    return nullptr;
  }

  if (m_pre_data->owned_client_request.valid()) {
    return const_cast<HTTPHdr *>(&m_pre_data->owned_client_request);
  }
  return nullptr;
}

HTTPHdr *
TransactionLogData::get_proxy_response() const
{
  if (likely(m_http_sm != nullptr)) {
    HttpTransact::HeaderInfo *hdr = &m_http_sm->t_state.hdr_info;
    if (hdr->client_response.valid()) {
      return &hdr->client_response;
    }
  }
  return nullptr;
}

HTTPHdr *
TransactionLogData::get_proxy_request() const
{
  if (likely(m_http_sm != nullptr)) {
    HttpTransact::HeaderInfo *hdr = &m_http_sm->t_state.hdr_info;
    if (hdr->server_request.valid()) {
      return &hdr->server_request;
    }
  }
  return nullptr;
}

HTTPHdr *
TransactionLogData::get_server_response() const
{
  if (likely(m_http_sm != nullptr)) {
    HttpTransact::HeaderInfo *hdr = &m_http_sm->t_state.hdr_info;
    if (hdr->server_response.valid()) {
      return &hdr->server_response;
    }
  }
  return nullptr;
}

HTTPHdr *
TransactionLogData::get_cache_response() const
{
  if (likely(m_http_sm != nullptr)) {
    HttpTransact::HeaderInfo *hdr = &m_http_sm->t_state.hdr_info;
    if (hdr->cache_response.valid()) {
      return &hdr->cache_response;
    }
  }
  return nullptr;
}

// ===== Client request URL / path =====

void
TransactionLogData::cache_url_strings() const
{
  if (m_url_cached) {
    return;
  }
  m_url_cached = true;

  HTTPHdr *client_request = get_client_request();
  if (client_request) {
    m_client_req_url_str      = client_request->url_string_get_ref(&m_client_req_url_len);
    auto path_sv              = client_request->path_get();
    m_client_req_url_path_str = path_sv.data();
    m_client_req_url_path_len = static_cast<int>(path_sv.length());
  }
}

const char *
TransactionLogData::get_client_req_url_str() const
{
  if (likely(m_http_sm != nullptr)) {
    cache_url_strings();
    return m_client_req_url_str;
  }
  return m_pre_data->owned_url.empty() ? nullptr : m_pre_data->owned_url.c_str();
}

int
TransactionLogData::get_client_req_url_len() const
{
  if (likely(m_http_sm != nullptr)) {
    cache_url_strings();
    return m_client_req_url_len;
  }
  return static_cast<int>(m_pre_data->owned_url.size());
}

const char *
TransactionLogData::get_client_req_url_path_str() const
{
  if (likely(m_http_sm != nullptr)) {
    cache_url_strings();
    return m_client_req_url_path_str;
  }
  return m_pre_data->owned_path.empty() ? nullptr : m_pre_data->owned_path.c_str();
}

int
TransactionLogData::get_client_req_url_path_len() const
{
  if (likely(m_http_sm != nullptr)) {
    cache_url_strings();
    return m_client_req_url_path_len;
  }
  return static_cast<int>(m_pre_data->owned_path.size());
}

// ===== Proxy response content-type / reason =====

void
TransactionLogData::cache_content_type() const
{
  if (m_content_type_cached) {
    return;
  }
  m_content_type_cached = true;

  HTTPHdr *proxy_response = get_proxy_response();
  if (proxy_response) {
    MIMEField *field = proxy_response->field_find(static_cast<std::string_view>(MIME_FIELD_CONTENT_TYPE));
    if (field) {
      auto ct                       = field->value_get();
      m_proxy_resp_content_type_str = const_cast<char *>(ct.data());
      m_proxy_resp_content_type_len = ct.length();
    } else {
      constexpr std::string_view hidden_ct{"@Content-Type"};
      field = proxy_response->field_find(hidden_ct);
      if (field) {
        auto ct                       = field->value_get();
        m_proxy_resp_content_type_str = const_cast<char *>(ct.data());
        m_proxy_resp_content_type_len = ct.length();
      }
    }
    auto reason                    = proxy_response->reason_get();
    m_proxy_resp_reason_phrase_str = const_cast<char *>(reason.data());
    m_proxy_resp_reason_phrase_len = static_cast<int>(reason.length());
  }
}

char *
TransactionLogData::get_proxy_resp_content_type_str() const
{
  if (likely(m_http_sm != nullptr)) {
    cache_content_type();
    return m_proxy_resp_content_type_str;
  }
  return nullptr;
}

int
TransactionLogData::get_proxy_resp_content_type_len() const
{
  if (likely(m_http_sm != nullptr)) {
    cache_content_type();
    return m_proxy_resp_content_type_len;
  }
  return 0;
}

char *
TransactionLogData::get_proxy_resp_reason_phrase_str() const
{
  if (likely(m_http_sm != nullptr)) {
    cache_content_type();
    return m_proxy_resp_reason_phrase_str;
  }
  return nullptr;
}

int
TransactionLogData::get_proxy_resp_reason_phrase_len() const
{
  if (likely(m_http_sm != nullptr)) {
    cache_content_type();
    return m_proxy_resp_reason_phrase_len;
  }
  return 0;
}

// ===== Unmapped URL =====

char *
TransactionLogData::get_unmapped_url_str() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.unmapped_url.valid()) {
      int len = 0;
      return m_http_sm->t_state.unmapped_url.string_get_ref(&len);
    }
  }
  return nullptr;
}

int
TransactionLogData::get_unmapped_url_len() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.unmapped_url.valid()) {
      int len = 0;
      m_http_sm->t_state.unmapped_url.string_get_ref(&len);
      return len;
    }
  }
  return 0;
}

// ===== Cache lookup URL =====

char *
TransactionLogData::get_cache_lookup_url_str() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.cache_info.lookup_url_storage.valid()) {
      int len = 0;
      return m_http_sm->t_state.cache_info.lookup_url_storage.string_get_ref(&len);
    }
  }
  return nullptr;
}

int
TransactionLogData::get_cache_lookup_url_len() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.cache_info.lookup_url_storage.valid()) {
      int len = 0;
      m_http_sm->t_state.cache_info.lookup_url_storage.string_get_ref(&len);
      return len;
    }
  }
  return 0;
}

// ===== Client addressing =====

sockaddr const *
TransactionLogData::get_client_addr() const
{
  if (likely(m_http_sm != nullptr)) {
    return &m_http_sm->t_state.effective_client_addr.sa;
  }
  return &m_pre_data->owned_client_addr.sa;
}

sockaddr const *
TransactionLogData::get_client_src_addr() const
{
  if (likely(m_http_sm != nullptr)) {
    return &m_http_sm->t_state.client_info.src_addr.sa;
  }
  return &m_pre_data->owned_client_src_addr.sa;
}

sockaddr const *
TransactionLogData::get_client_dst_addr() const
{
  if (likely(m_http_sm != nullptr)) {
    return &m_http_sm->t_state.client_info.dst_addr.sa;
  }
  return &m_pre_data->owned_client_dst_addr.sa;
}

sockaddr const *
TransactionLogData::get_verified_client_addr() const
{
  if (likely(m_http_sm != nullptr)) {
    if (auto txn = m_http_sm->get_ua_txn(); txn) {
      sockaddr const *vaddr = txn->get_verified_client_addr();
      if (vaddr && ats_is_ip(vaddr)) {
        return vaddr;
      }
    }
  }
  return nullptr;
}

uint16_t
TransactionLogData::get_client_port() const
{
  if (likely(m_http_sm != nullptr)) {
    if (auto txn = m_http_sm->get_ua_txn(); txn) {
      return txn->get_client_port();
    }
    return 0;
  }
  return m_pre_data->m_client_port;
}

// ===== Server addressing =====

sockaddr const *
TransactionLogData::get_server_src_addr() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.current.server) {
      return &m_http_sm->t_state.current.server->src_addr.sa;
    }
  }
  return nullptr;
}

sockaddr const *
TransactionLogData::get_server_dst_addr() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.current.server) {
      return &m_http_sm->t_state.current.server->dst_addr.sa;
    }
  }
  return nullptr;
}

sockaddr const *
TransactionLogData::get_server_info_dst_addr() const
{
  if (likely(m_http_sm != nullptr)) {
    return &m_http_sm->t_state.server_info.dst_addr.sa;
  }
  return nullptr;
}

const char *
TransactionLogData::get_server_name() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.current.server) {
      return m_http_sm->t_state.current.server->name;
    }
  }
  return nullptr;
}

// ===== Squid codes =====

SquidLogCode
TransactionLogData::get_log_code() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.squid_codes.log_code;
  }
  return m_pre_data->m_log_code;
}

SquidSubcode
TransactionLogData::get_subcode() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.squid_codes.subcode;
  }
  return SquidSubcode::EMPTY;
}

SquidHitMissCode
TransactionLogData::get_hit_miss_code() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.squid_codes.hit_miss_code;
  }
  return m_pre_data->m_hit_miss_code;
}

SquidHierarchyCode
TransactionLogData::get_hier_code() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.squid_codes.hier_code;
  }
  return m_pre_data->m_hier_code;
}

// ===== Byte counters =====

int64_t
TransactionLogData::get_client_request_body_bytes() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->client_request_body_bytes;
  }
  return 0;
}

int64_t
TransactionLogData::get_client_response_hdr_bytes() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->client_response_hdr_bytes;
  }
  return 0;
}

int64_t
TransactionLogData::get_client_response_body_bytes() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->client_response_body_bytes;
  }
  return 0;
}

int64_t
TransactionLogData::get_server_request_body_bytes() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->server_request_body_bytes;
  }
  return 0;
}

int64_t
TransactionLogData::get_server_response_body_bytes() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->server_response_body_bytes;
  }
  return 0;
}

int64_t
TransactionLogData::get_cache_response_body_bytes() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->cache_response_body_bytes;
  }
  return 0;
}

int64_t
TransactionLogData::get_cache_response_hdr_bytes() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->cache_response_hdr_bytes;
  }
  return 0;
}

// ===== Transaction identifiers =====

int64_t
TransactionLogData::get_sm_id() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->sm_id;
  }
  return 0;
}

int64_t
TransactionLogData::get_connection_id() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->client_connection_id();
  }
  return m_pre_data->m_connection_id;
}

int
TransactionLogData::get_transaction_id() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->client_transaction_id();
  }
  return m_pre_data->m_transaction_id;
}

int
TransactionLogData::get_transaction_priority_weight() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->client_transaction_priority_weight();
  }
  return 0;
}

int
TransactionLogData::get_transaction_priority_dependence() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->client_transaction_priority_dependence();
  }
  return 0;
}

// ===== Plugin info =====

int64_t
TransactionLogData::get_plugin_id() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->plugin_id;
  }
  return 0;
}

const char *
TransactionLogData::get_plugin_tag() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->plugin_tag;
  }
  return nullptr;
}

// ===== Protocol info =====

const char *
TransactionLogData::get_client_protocol() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_user_agent().get_client_protocol();
  }
  return m_pre_data->owned_client_protocol_str.empty() ? nullptr : m_pre_data->owned_client_protocol_str.c_str();
}

const char *
TransactionLogData::get_server_protocol() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->server_protocol;
  }
  return nullptr;
}

const char *
TransactionLogData::get_client_sec_protocol() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_user_agent().get_client_sec_protocol();
  }
  return nullptr;
}

const char *
TransactionLogData::get_client_cipher_suite() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_user_agent().get_client_cipher_suite();
  }
  return nullptr;
}

const char *
TransactionLogData::get_client_curve() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_user_agent().get_client_curve();
  }
  return nullptr;
}

const char *
TransactionLogData::get_client_security_group() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_user_agent().get_client_security_group();
  }
  return nullptr;
}

int
TransactionLogData::get_client_alpn_id() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_user_agent().get_client_alpn_id();
  }
  return -1;
}

// ===== SNI =====

const char *
TransactionLogData::get_sni_server_name() const
{
  if (likely(m_http_sm != nullptr)) {
    if (auto txn = m_http_sm->get_ua_txn(); txn) {
      if (auto ssn = txn->get_proxy_ssn(); ssn) {
        if (auto ssl = ssn->ssl(); ssl) {
          return ssl->client_sni_server_name();
        }
      }
    }
  }
  return nullptr;
}

// ===== Connection flags =====

bool
TransactionLogData::get_client_tcp_reused() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_user_agent().get_client_tcp_reused();
  }
  return false;
}

bool
TransactionLogData::get_client_connection_is_ssl() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_user_agent().get_client_connection_is_ssl();
  }
  return m_pre_data->m_client_connection_is_ssl;
}

bool
TransactionLogData::get_client_ssl_reused() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_user_agent().get_client_ssl_reused();
  }
  return false;
}

bool
TransactionLogData::get_is_internal() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->is_internal;
  }
  return false;
}

bool
TransactionLogData::get_server_connection_is_ssl() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->server_connection_is_ssl;
  }
  return false;
}

bool
TransactionLogData::get_server_ssl_reused() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->server_ssl_reused;
  }
  return false;
}

int
TransactionLogData::get_server_connection_provided_cert() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->server_connection_provided_cert;
  }
  return 0;
}

int
TransactionLogData::get_client_provided_cert() const
{
  if (likely(m_http_sm != nullptr)) {
    if (auto txn = m_http_sm->get_ua_txn(); txn) {
      if (auto ssn = txn->get_proxy_ssn(); ssn) {
        if (auto ssl = ssn->ssl(); ssl) {
          return ssl->client_provided_certificate();
        }
      }
    }
  }
  return 0;
}

// ===== Server transaction count =====

int64_t
TransactionLogData::get_server_transact_count() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->server_transact_count;
  }
  return m_pre_data->m_server_transact_count;
}

// ===== Finish status =====

int
TransactionLogData::get_client_finish_status_code() const
{
  if (likely(m_http_sm != nullptr)) {
    return compute_client_finish_status(m_http_sm);
  }
  return 0;
}

int
TransactionLogData::get_proxy_finish_status_code() const
{
  if (likely(m_http_sm != nullptr)) {
    return compute_proxy_finish_status(m_http_sm);
  }
  return 0;
}

// ===== Error codes =====

void
TransactionLogData::format_error_codes() const
{
  if (m_error_codes_formatted) {
    return;
  }
  m_error_codes_formatted = true;
  ink_assert(m_http_sm != nullptr);
  m_http_sm->t_state.client_info.rx_error_code.str(m_client_rx_error_code, sizeof(m_client_rx_error_code));
  m_http_sm->t_state.client_info.tx_error_code.str(m_client_tx_error_code, sizeof(m_client_tx_error_code));
}

const char *
TransactionLogData::get_client_rx_error_code() const
{
  if (likely(m_http_sm != nullptr)) {
    format_error_codes();
    return m_client_rx_error_code;
  }
  return "-";
}

const char *
TransactionLogData::get_client_tx_error_code() const
{
  if (likely(m_http_sm != nullptr)) {
    format_error_codes();
    return m_client_tx_error_code;
  }
  return "-";
}

// ===== MPTCP =====

std::optional<bool>
TransactionLogData::get_mptcp_state() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->mptcp_state;
  }
  return std::nullopt;
}

// ===== Misc transaction state =====

in_port_t
TransactionLogData::get_incoming_port() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.request_data.incoming_port;
  }
  return 0;
}

int
TransactionLogData::get_orig_scheme() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.orig_scheme;
  }
  return -1;
}

int64_t
TransactionLogData::get_congestion_control_crat() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.congestion_control_crat;
  }
  return 0;
}

// ===== Cache state =====

int
TransactionLogData::get_cache_write_code() const
{
  if (likely(m_http_sm != nullptr)) {
    return convert_cache_write_code(m_http_sm->t_state.cache_info.write_status);
  }
  return 0;
}

int
TransactionLogData::get_cache_transform_write_code() const
{
  if (likely(m_http_sm != nullptr)) {
    return convert_cache_write_code(m_http_sm->t_state.cache_info.transform_write_status);
  }
  return 0;
}

int
TransactionLogData::get_cache_open_read_tries() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_cache_sm().get_open_read_tries();
  }
  return 0;
}

int
TransactionLogData::get_cache_open_write_tries() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->get_cache_sm().get_open_write_tries();
  }
  return 0;
}

int
TransactionLogData::get_max_cache_open_write_retries() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.txn_conf->max_cache_open_write_retries;
  }
  return -1;
}

// ===== Retry attempts =====

int64_t
TransactionLogData::get_simple_retry_attempts() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.current.simple_retry_attempts;
  }
  return 0;
}

int64_t
TransactionLogData::get_unavailable_retry_attempts() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.current.unavailable_server_retry_attempts;
  }
  return 0;
}

int64_t
TransactionLogData::get_retry_attempts_saved() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.current.retry_attempts.saved();
  }
  return 0;
}

// ===== Status plugin entry name =====

std::string_view
TransactionLogData::get_http_return_code_setter_name() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.http_return_code_setter_name;
  }
  return {};
}

// ===== Proxy Protocol =====

int
TransactionLogData::get_pp_version() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
      return static_cast<int>(m_http_sm->t_state.pp_info.version);
    }
  }
  return 0;
}

sockaddr const *
TransactionLogData::get_pp_src_addr() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
      return &m_http_sm->t_state.pp_info.src_addr.sa;
    }
  }
  return nullptr;
}

sockaddr const *
TransactionLogData::get_pp_dst_addr() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
      return &m_http_sm->t_state.pp_info.dst_addr.sa;
    }
  }
  return nullptr;
}

std::string_view
TransactionLogData::get_pp_authority() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
      if (auto authority_opt = m_http_sm->t_state.pp_info.get_tlv(PP2_TYPE_AUTHORITY); authority_opt) {
        return *authority_opt;
      }
    }
  }
  return {};
}

std::string_view
TransactionLogData::get_pp_tls_cipher() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
      if (auto cipher = m_http_sm->t_state.pp_info.get_tlv_ssl_cipher(); cipher) {
        return *cipher;
      }
    }
  }
  return {};
}

std::string_view
TransactionLogData::get_pp_tls_version() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
      if (auto version = m_http_sm->t_state.pp_info.get_tlv_ssl_version(); version) {
        return *version;
      }
    }
  }
  return {};
}

std::string_view
TransactionLogData::get_pp_tls_group() const
{
  if (likely(m_http_sm != nullptr)) {
    if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
      if (auto group = m_http_sm->t_state.pp_info.get_tlv_ssl_group(); group) {
        return *group;
      }
    }
  }
  return {};
}

// ===== Server response Transfer-Encoding =====

std::string_view
TransactionLogData::get_server_response_transfer_encoding() const
{
  if (likely(m_http_sm != nullptr)) {
    return m_http_sm->t_state.hdr_info.server_response_transfer_encoding;
  }
  return {};
}

// ===== Fallback fields for pre-transaction logging =====

std::string_view
TransactionLogData::get_method() const
{
  if (likely(m_http_sm != nullptr)) {
    return {};
  }
  return m_pre_data->owned_method;
}

std::string_view
TransactionLogData::get_scheme() const
{
  if (likely(m_http_sm != nullptr)) {
    return {};
  }
  return m_pre_data->owned_scheme;
}

std::string_view
TransactionLogData::get_client_protocol_str() const
{
  if (likely(m_http_sm != nullptr)) {
    return {};
  }
  return m_pre_data->owned_client_protocol_str;
}
