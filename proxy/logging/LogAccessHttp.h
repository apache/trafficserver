/** @file

  A brief file description

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

#include "ts/Arena.h"
#include "HTTP.h"
#include "LogAccess.h"

class HttpSM;
class URL;

/*-------------------------------------------------------------------------
  LogAccessHttp

  This class extends the logging system interface as implemented by the
  HttpStateMachineGet class.
  -------------------------------------------------------------------------*/

class LogAccessHttp : public LogAccess
{
public:
  LogAccessHttp(HttpSM *sm);
  ~LogAccessHttp() override;

  void init() override;

  LogEntryType
  entry_type() const override
  {
    return LOG_ENTRY_HTTP;
  }

  //
  // client -> proxy fields
  //
  int marshal_client_host_ip(char *) override;                // STR
  int marshal_host_interface_ip(char *) override;             // STR
  int marshal_client_host_port(char *) override;              // INT
  int marshal_client_auth_user_name(char *) override;         // STR
  int marshal_client_req_text(char *) override;               // STR
  int marshal_client_req_http_method(char *) override;        // INT
  int marshal_client_req_url(char *) override;                // STR
  int marshal_client_req_url_canon(char *) override;          // STR
  int marshal_client_req_unmapped_url_canon(char *) override; // STR
  int marshal_client_req_unmapped_url_path(char *) override;  // STR
  int marshal_client_req_unmapped_url_host(char *) override;  // STR
  int marshal_client_req_url_path(char *) override;           // STR
  int marshal_client_req_url_scheme(char *) override;         // STR
  int marshal_client_req_http_version(char *) override;       // INT
  int marshal_client_req_protocol_version(char *) override;   // STR
  int marshal_client_req_header_len(char *) override;         // INT
  int marshal_client_req_content_len(char *) override;        // INT
  int marshal_client_req_squid_len(char *) override;          // INT
  int marshal_client_req_tcp_reused(char *) override;         // INT
  int marshal_client_req_is_ssl(char *) override;             // INT
  int marshal_client_req_ssl_reused(char *) override;         // INT
  int marshal_client_req_timestamp_sec(char *) override;      // INT
  int marshal_client_req_timestamp_ms(char *) override;       // INT
  int marshal_client_security_protocol(char *) override;      // STR
  int marshal_client_security_cipher_suite(char *) override;  // STR
  int marshal_client_finish_status_code(char *) override;     // INT
  int marshal_client_req_id(char *) override;                 // INT
  int marshal_client_req_uuid(char *) override;               // STR
  int marshal_client_rx_error_code(char *) override;          // STR
  int marshal_client_tx_error_code(char *) override;          // STR

  //
  // proxy -> client fields
  //
  int marshal_proxy_resp_content_type(char *) override;  // STR
  int marshal_proxy_resp_reason_phrase(char *) override; // STR
  int marshal_proxy_resp_header_len(char *) override;    // INT
  int marshal_proxy_resp_content_len(char *) override;   // INT
  int marshal_proxy_resp_squid_len(char *) override;     // INT
  int marshal_proxy_resp_status_code(char *) override;   // INT
  int marshal_proxy_finish_status_code(char *) override; // INT
  int marshal_cache_result_code(char *) override;        // INT
  int marshal_cache_result_subcode(char *) override;     // INT
  int marshal_cache_hit_miss(char *) override;           // INT

  //
  // proxy -> server fields
  //
  int marshal_proxy_req_header_len(char *) override;  // INT
  int marshal_proxy_req_content_len(char *) override; // INT
  int marshal_proxy_req_squid_len(char *) override;   // INT
  int marshal_proxy_req_server_ip(char *) override;   // INT
  int marshal_proxy_req_server_port(char *) override; // INT
  int marshal_proxy_hierarchy_route(char *) override; // INT
  int marshal_proxy_host_port(char *) override;       // INT
  int marshal_proxy_req_is_ssl(char *) override;      // INT

  //
  // server -> proxy fields
  //
  int marshal_server_host_ip(char *) override;           // INT
  int marshal_server_host_name(char *) override;         // STR
  int marshal_server_resp_status_code(char *) override;  // INT
  int marshal_server_resp_header_len(char *) override;   // INT
  int marshal_server_resp_content_len(char *) override;  // INT
  int marshal_server_resp_squid_len(char *) override;    // INT
  int marshal_server_resp_http_version(char *) override; // INT
  int marshal_server_resp_time_ms(char *) override;      // INT
  int marshal_server_resp_time_s(char *) override;       // INT
  int marshal_server_transact_count(char *) override;    // INT
  int marshal_server_connect_attempts(char *) override;  // INT

  //
  // cache -> client fields
  //
  int marshal_cache_resp_status_code(char *) override;  // INT
  int marshal_cache_resp_header_len(char *) override;   // INT
  int marshal_cache_resp_content_len(char *) override;  // INT
  int marshal_cache_resp_squid_len(char *) override;    // INT
  int marshal_cache_resp_http_version(char *) override; // INT

  //
  // congestion control client_retry_after_time
  //
  int marshal_client_retry_after_time(char *) override; // INT

  //
  // cache write fields
  //
  int marshal_cache_write_code(char *) override;           // INT
  int marshal_cache_write_transform_code(char *) override; // INT

  //
  // other fields
  //
  int marshal_transfer_time_ms(char *) override;           // INT
  int marshal_transfer_time_s(char *) override;            // INT
  int marshal_file_size(char *) override;                  // INT
  int marshal_plugin_identity_id(char *) override;         // INT
  int marshal_plugin_identity_tag(char *) override;        // STR
  int marshal_cache_lookup_url_canon(char *) override;     // STR
  int marshal_client_http_connection_id(char *) override;  // INT
  int marshal_client_http_transaction_id(char *) override; // INT

  //
  // named fields from within a http header
  //
  int marshal_http_header_field(LogField::Container container, char *field, char *buf) override;
  int marshal_http_header_field_escapify(LogField::Container container, char *field, char *buf) override;

  int marshal_milestone(TSMilestonesType ms, char *buf) override;
  int marshal_milestone_fmt_sec(TSMilestonesType ms, char *buf) override;
  int marshal_milestone_diff(TSMilestonesType ms1, TSMilestonesType ms2, char *buf) override;

  int marshal_milestone_fmt_ms(TSMilestonesType ms, char *buf);

  void set_client_req_url(char *, int) override;                // STR
  void set_client_req_url_canon(char *, int) override;          // STR
  void set_client_req_unmapped_url_canon(char *, int) override; // STR
  void set_client_req_unmapped_url_path(char *, int) override;  // STR
  void set_client_req_unmapped_url_host(char *, int) override;  // STR
  void set_client_req_url_path(char *, int) override;           // STR

  // noncopyable
  // -- member functions that are not allowed --
  LogAccessHttp(const LogAccessHttp &rhs) = delete;
  LogAccessHttp &operator=(LogAccessHttp &rhs) = delete;

private:
  HttpSM *m_http_sm;

  Arena m_arena;
  //  URL *m_url;

  HTTPHdr *m_client_request;
  HTTPHdr *m_proxy_response;
  HTTPHdr *m_proxy_request;
  HTTPHdr *m_server_response;
  HTTPHdr *m_cache_response;

  char *m_client_req_url_str;
  int m_client_req_url_len;
  char *m_client_req_url_canon_str;
  int m_client_req_url_canon_len;
  char *m_client_req_unmapped_url_canon_str;
  int m_client_req_unmapped_url_canon_len;
  char *m_client_req_unmapped_url_path_str;
  int m_client_req_unmapped_url_path_len;
  char *m_client_req_unmapped_url_host_str;
  int m_client_req_unmapped_url_host_len;
  char const *m_client_req_url_path_str;
  int m_client_req_url_path_len;
  char *m_proxy_resp_content_type_str;
  int m_proxy_resp_content_type_len;
  char *m_proxy_resp_reason_phrase_str;
  int m_proxy_resp_reason_phrase_len;
  char *m_cache_lookup_url_canon_str;
  int m_cache_lookup_url_canon_len;

  void validate_unmapped_url(void);
  void validate_unmapped_url_path(void);

  void validate_lookup_url(void);
};
