/** @file

  CompletedTransactionLogData implementation: read TransactionLogData from a
  live HttpSM.

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

#include "proxy/http/CompletedTransactionLogData.h"
#include "proxy/http/HttpSM.h"
#include "proxy/logging/LogAccess.h"
#include "proxy/hdrs/MIME.h"
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

CompletedTransactionLogData::CompletedTransactionLogData(HttpSM *sm) : m_http_sm(sm)
{
  ink_assert(sm != nullptr);
}

void *
CompletedTransactionLogData::http_sm_for_plugins() const
{
  return m_http_sm;
}

// ===== Milestones =====

TransactionMilestones const *
CompletedTransactionLogData::get_milestones() const
{
  return &m_http_sm->milestones;
}

// ===== Headers =====

HTTPHdr *
CompletedTransactionLogData::get_client_request() const
{
  HttpTransact::HeaderInfo *hdr = &m_http_sm->t_state.hdr_info;
  if (hdr->client_request.valid()) {
    return &hdr->client_request;
  }
  return nullptr;
}

HTTPHdr *
CompletedTransactionLogData::get_proxy_response() const
{
  HttpTransact::HeaderInfo *hdr = &m_http_sm->t_state.hdr_info;
  if (hdr->client_response.valid()) {
    return &hdr->client_response;
  }
  return nullptr;
}

HTTPHdr *
CompletedTransactionLogData::get_proxy_request() const
{
  HttpTransact::HeaderInfo *hdr = &m_http_sm->t_state.hdr_info;
  if (hdr->server_request.valid()) {
    return &hdr->server_request;
  }
  return nullptr;
}

HTTPHdr *
CompletedTransactionLogData::get_server_response() const
{
  HttpTransact::HeaderInfo *hdr = &m_http_sm->t_state.hdr_info;
  if (hdr->server_response.valid()) {
    return &hdr->server_response;
  }
  return nullptr;
}

HTTPHdr *
CompletedTransactionLogData::get_cache_response() const
{
  HttpTransact::HeaderInfo *hdr = &m_http_sm->t_state.hdr_info;
  if (hdr->cache_response.valid()) {
    return &hdr->cache_response;
  }
  return nullptr;
}

// ===== Client request URL / path =====

void
CompletedTransactionLogData::cache_url_strings() const
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
CompletedTransactionLogData::get_client_req_url_str() const
{
  cache_url_strings();
  return m_client_req_url_str;
}

int
CompletedTransactionLogData::get_client_req_url_len() const
{
  cache_url_strings();
  return m_client_req_url_len;
}

const char *
CompletedTransactionLogData::get_client_req_url_path_str() const
{
  cache_url_strings();
  return m_client_req_url_path_str;
}

int
CompletedTransactionLogData::get_client_req_url_path_len() const
{
  cache_url_strings();
  return m_client_req_url_path_len;
}

// ===== Proxy response content-type / reason =====

void
CompletedTransactionLogData::cache_content_type() const
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
CompletedTransactionLogData::get_proxy_resp_content_type_str() const
{
  cache_content_type();
  return m_proxy_resp_content_type_str;
}

int
CompletedTransactionLogData::get_proxy_resp_content_type_len() const
{
  cache_content_type();
  return m_proxy_resp_content_type_len;
}

char *
CompletedTransactionLogData::get_proxy_resp_reason_phrase_str() const
{
  cache_content_type();
  return m_proxy_resp_reason_phrase_str;
}

int
CompletedTransactionLogData::get_proxy_resp_reason_phrase_len() const
{
  cache_content_type();
  return m_proxy_resp_reason_phrase_len;
}

// ===== Unmapped URL =====

char *
CompletedTransactionLogData::get_unmapped_url_str() const
{
  if (m_http_sm->t_state.unmapped_url.valid()) {
    int len = 0;
    return m_http_sm->t_state.unmapped_url.string_get_ref(&len);
  }
  return nullptr;
}

int
CompletedTransactionLogData::get_unmapped_url_len() const
{
  if (m_http_sm->t_state.unmapped_url.valid()) {
    int len = 0;
    m_http_sm->t_state.unmapped_url.string_get_ref(&len);
    return len;
  }
  return 0;
}

// ===== Cache lookup URL =====

char *
CompletedTransactionLogData::get_cache_lookup_url_str() const
{
  if (m_http_sm->t_state.cache_info.lookup_url_storage.valid()) {
    int len = 0;
    return m_http_sm->t_state.cache_info.lookup_url_storage.string_get_ref(&len);
  }
  return nullptr;
}

int
CompletedTransactionLogData::get_cache_lookup_url_len() const
{
  if (m_http_sm->t_state.cache_info.lookup_url_storage.valid()) {
    int len = 0;
    m_http_sm->t_state.cache_info.lookup_url_storage.string_get_ref(&len);
    return len;
  }
  return 0;
}

// ===== Client addressing =====

sockaddr const *
CompletedTransactionLogData::get_client_addr() const
{
  return &m_http_sm->t_state.effective_client_addr.sa;
}

sockaddr const *
CompletedTransactionLogData::get_client_src_addr() const
{
  return &m_http_sm->t_state.client_info.src_addr.sa;
}

sockaddr const *
CompletedTransactionLogData::get_client_dst_addr() const
{
  return &m_http_sm->t_state.client_info.dst_addr.sa;
}

sockaddr const *
CompletedTransactionLogData::get_verified_client_addr() const
{
  if (auto txn = m_http_sm->get_ua_txn(); txn) {
    sockaddr const *vaddr = txn->get_verified_client_addr();
    if (vaddr && ats_is_ip(vaddr)) {
      return vaddr;
    }
  }
  return nullptr;
}

uint16_t
CompletedTransactionLogData::get_client_port() const
{
  if (auto txn = m_http_sm->get_ua_txn(); txn) {
    return txn->get_client_port();
  }
  return 0;
}

// ===== Server addressing =====

sockaddr const *
CompletedTransactionLogData::get_server_src_addr() const
{
  if (m_http_sm->t_state.current.server) {
    return &m_http_sm->t_state.current.server->src_addr.sa;
  }
  return nullptr;
}

sockaddr const *
CompletedTransactionLogData::get_server_dst_addr() const
{
  if (m_http_sm->t_state.current.server) {
    return &m_http_sm->t_state.current.server->dst_addr.sa;
  }
  return nullptr;
}

sockaddr const *
CompletedTransactionLogData::get_server_info_dst_addr() const
{
  return &m_http_sm->t_state.server_info.dst_addr.sa;
}

const char *
CompletedTransactionLogData::get_server_name() const
{
  if (m_http_sm->t_state.current.server) {
    return m_http_sm->t_state.current.server->name;
  }
  return nullptr;
}

// ===== Squid codes =====

SquidLogCode
CompletedTransactionLogData::get_log_code() const
{
  return m_http_sm->t_state.squid_codes.log_code;
}

SquidSubcode
CompletedTransactionLogData::get_subcode() const
{
  return m_http_sm->t_state.squid_codes.subcode;
}

SquidHitMissCode
CompletedTransactionLogData::get_hit_miss_code() const
{
  return m_http_sm->t_state.squid_codes.hit_miss_code;
}

SquidHierarchyCode
CompletedTransactionLogData::get_hier_code() const
{
  return m_http_sm->t_state.squid_codes.hier_code;
}

// ===== Byte counters =====

int64_t
CompletedTransactionLogData::get_client_request_body_bytes() const
{
  return m_http_sm->client_request_body_bytes;
}

int64_t
CompletedTransactionLogData::get_client_response_hdr_bytes() const
{
  return m_http_sm->client_response_hdr_bytes;
}

int64_t
CompletedTransactionLogData::get_client_response_body_bytes() const
{
  return m_http_sm->client_response_body_bytes;
}

int64_t
CompletedTransactionLogData::get_server_request_body_bytes() const
{
  return m_http_sm->server_request_body_bytes;
}

int64_t
CompletedTransactionLogData::get_server_response_body_bytes() const
{
  return m_http_sm->server_response_body_bytes;
}

int64_t
CompletedTransactionLogData::get_cache_response_body_bytes() const
{
  return m_http_sm->cache_response_body_bytes;
}

int64_t
CompletedTransactionLogData::get_cache_response_hdr_bytes() const
{
  return m_http_sm->cache_response_hdr_bytes;
}

// ===== Transaction identifiers =====

int64_t
CompletedTransactionLogData::get_sm_id() const
{
  return m_http_sm->sm_id;
}

int64_t
CompletedTransactionLogData::get_connection_id() const
{
  return m_http_sm->client_connection_id();
}

int
CompletedTransactionLogData::get_transaction_id() const
{
  return m_http_sm->client_transaction_id();
}

int
CompletedTransactionLogData::get_transaction_priority_weight() const
{
  return m_http_sm->client_transaction_priority_weight();
}

int
CompletedTransactionLogData::get_transaction_priority_dependence() const
{
  return m_http_sm->client_transaction_priority_dependence();
}

// ===== Plugin info =====

int64_t
CompletedTransactionLogData::get_plugin_id() const
{
  return m_http_sm->plugin_id;
}

const char *
CompletedTransactionLogData::get_plugin_tag() const
{
  return m_http_sm->plugin_tag;
}

// ===== Protocol info =====

const char *
CompletedTransactionLogData::get_client_protocol() const
{
  return m_http_sm->get_user_agent().get_client_protocol();
}

const char *
CompletedTransactionLogData::get_server_protocol() const
{
  return m_http_sm->server_protocol;
}

const char *
CompletedTransactionLogData::get_client_sec_protocol() const
{
  return m_http_sm->get_user_agent().get_client_sec_protocol();
}

const char *
CompletedTransactionLogData::get_client_cipher_suite() const
{
  return m_http_sm->get_user_agent().get_client_cipher_suite();
}

const char *
CompletedTransactionLogData::get_client_curve() const
{
  return m_http_sm->get_user_agent().get_client_curve();
}

const char *
CompletedTransactionLogData::get_client_security_group() const
{
  return m_http_sm->get_user_agent().get_client_security_group();
}

int
CompletedTransactionLogData::get_client_alpn_id() const
{
  return m_http_sm->get_user_agent().get_client_alpn_id();
}

// ===== SNI =====

const char *
CompletedTransactionLogData::get_sni_server_name() const
{
  if (auto txn = m_http_sm->get_ua_txn(); txn) {
    if (auto ssn = txn->get_proxy_ssn(); ssn) {
      if (auto ssl = ssn->ssl(); ssl) {
        return ssl->client_sni_server_name();
      }
    }
  }
  return nullptr;
}

// ===== Connection flags =====

bool
CompletedTransactionLogData::get_client_tcp_reused() const
{
  return m_http_sm->get_user_agent().get_client_tcp_reused();
}

bool
CompletedTransactionLogData::get_client_connection_is_ssl() const
{
  return m_http_sm->get_user_agent().get_client_connection_is_ssl();
}

bool
CompletedTransactionLogData::get_client_ssl_reused() const
{
  return m_http_sm->get_user_agent().get_client_ssl_reused();
}

bool
CompletedTransactionLogData::get_is_internal() const
{
  return m_http_sm->is_internal;
}

bool
CompletedTransactionLogData::get_server_connection_is_ssl() const
{
  return m_http_sm->server_connection_is_ssl;
}

bool
CompletedTransactionLogData::get_server_ssl_reused() const
{
  return m_http_sm->server_ssl_reused;
}

int
CompletedTransactionLogData::get_server_connection_provided_cert() const
{
  return m_http_sm->server_connection_provided_cert;
}

int
CompletedTransactionLogData::get_client_provided_cert() const
{
  if (auto txn = m_http_sm->get_ua_txn(); txn) {
    if (auto ssn = txn->get_proxy_ssn(); ssn) {
      if (auto ssl = ssn->ssl(); ssl) {
        return ssl->client_provided_certificate();
      }
    }
  }
  return 0;
}

// ===== Server transaction count =====

int64_t
CompletedTransactionLogData::get_server_transact_count() const
{
  return m_http_sm->server_transact_count;
}

// ===== Finish status =====

int
CompletedTransactionLogData::get_client_finish_status_code() const
{
  return compute_client_finish_status(m_http_sm);
}

int
CompletedTransactionLogData::get_proxy_finish_status_code() const
{
  return compute_proxy_finish_status(m_http_sm);
}

// ===== Error codes =====

void
CompletedTransactionLogData::format_error_codes() const
{
  if (m_error_codes_formatted) {
    return;
  }
  m_error_codes_formatted = true;
  m_http_sm->t_state.client_info.rx_error_code.str(m_client_rx_error_code, sizeof(m_client_rx_error_code));
  m_http_sm->t_state.client_info.tx_error_code.str(m_client_tx_error_code, sizeof(m_client_tx_error_code));
}

const char *
CompletedTransactionLogData::get_client_rx_error_code() const
{
  format_error_codes();
  return m_client_rx_error_code;
}

const char *
CompletedTransactionLogData::get_client_tx_error_code() const
{
  format_error_codes();
  return m_client_tx_error_code;
}

// ===== MPTCP =====

std::optional<bool>
CompletedTransactionLogData::get_mptcp_state() const
{
  return m_http_sm->mptcp_state;
}

// ===== Misc transaction state =====

in_port_t
CompletedTransactionLogData::get_incoming_port() const
{
  return m_http_sm->t_state.request_data.incoming_port;
}

int
CompletedTransactionLogData::get_orig_scheme() const
{
  return m_http_sm->t_state.orig_scheme;
}

int64_t
CompletedTransactionLogData::get_congestion_control_crat() const
{
  return m_http_sm->t_state.congestion_control_crat;
}

// ===== Cache state =====

int
CompletedTransactionLogData::get_cache_write_code() const
{
  return convert_cache_write_code(m_http_sm->t_state.cache_info.write_status);
}

int
CompletedTransactionLogData::get_cache_transform_write_code() const
{
  return convert_cache_write_code(m_http_sm->t_state.cache_info.transform_write_status);
}

int
CompletedTransactionLogData::get_cache_open_read_tries() const
{
  return m_http_sm->get_cache_sm().get_open_read_tries();
}

int
CompletedTransactionLogData::get_cache_open_write_tries() const
{
  return m_http_sm->get_cache_sm().get_open_write_tries();
}

int
CompletedTransactionLogData::get_max_cache_open_write_retries() const
{
  return m_http_sm->t_state.txn_conf->max_cache_open_write_retries;
}

// ===== Retry attempts =====

int64_t
CompletedTransactionLogData::get_simple_retry_attempts() const
{
  return m_http_sm->t_state.current.simple_retry_attempts;
}

int64_t
CompletedTransactionLogData::get_unavailable_retry_attempts() const
{
  return m_http_sm->t_state.current.unavailable_server_retry_attempts;
}

int64_t
CompletedTransactionLogData::get_retry_attempts_saved() const
{
  return m_http_sm->t_state.current.retry_attempts.saved();
}

// ===== Status plugin entry name =====

std::string_view
CompletedTransactionLogData::get_http_return_code_setter_name() const
{
  return m_http_sm->t_state.http_return_code_setter_name;
}

// ===== Proxy Protocol =====

int
CompletedTransactionLogData::get_pp_version() const
{
  if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
    return static_cast<int>(m_http_sm->t_state.pp_info.version);
  }
  return 0;
}

sockaddr const *
CompletedTransactionLogData::get_pp_src_addr() const
{
  if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
    return &m_http_sm->t_state.pp_info.src_addr.sa;
  }
  return nullptr;
}

sockaddr const *
CompletedTransactionLogData::get_pp_dst_addr() const
{
  if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
    return &m_http_sm->t_state.pp_info.dst_addr.sa;
  }
  return nullptr;
}

std::string_view
CompletedTransactionLogData::get_pp_authority() const
{
  if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
    if (auto authority_opt = m_http_sm->t_state.pp_info.get_tlv(PP2_TYPE_AUTHORITY); authority_opt) {
      return *authority_opt;
    }
  }
  return {};
}

std::string_view
CompletedTransactionLogData::get_pp_tls_cipher() const
{
  if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
    if (auto cipher = m_http_sm->t_state.pp_info.get_tlv_ssl_cipher(); cipher) {
      return *cipher;
    }
  }
  return {};
}

std::string_view
CompletedTransactionLogData::get_pp_tls_version() const
{
  if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
    if (auto version = m_http_sm->t_state.pp_info.get_tlv_ssl_version(); version) {
      return *version;
    }
  }
  return {};
}

std::string_view
CompletedTransactionLogData::get_pp_tls_group() const
{
  if (m_http_sm->t_state.pp_info.version != ProxyProtocolVersion::UNDEFINED) {
    if (auto group = m_http_sm->t_state.pp_info.get_tlv_ssl_group(); group) {
      return *group;
    }
  }
  return {};
}

// ===== Server response Transfer-Encoding =====

std::string_view
CompletedTransactionLogData::get_server_response_transfer_encoding() const
{
  return m_http_sm->t_state.hdr_info.server_response_transfer_encoding;
}
