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

#define ink_time_t time_t

extern int nstrhex(char *d, unsigned int i);

class HttpTransactHeaders
{
public:
  static bool is_this_http_method_supported(int method);
  static bool is_method_cacheable(const HttpConfigParams *http_config_param, const int method);
  static bool is_method_cache_lookupable(int method);
  static bool is_this_a_hop_by_hop_header(const char *field_name_wks);
  static bool is_this_method_supported(int the_scheme, int the_method);

  static void insert_supported_methods_in_response(HTTPHdr *response, int the_scheme);

  static void build_base_response(HTTPHdr *outgoing_response, HTTPStatus status, const char *reason_phrase, int reason_phrase_len,
                                  ink_time_t date);

  static void copy_header_fields(HTTPHdr *src_hdr, HTTPHdr *new_hdr, bool retain_proxy_auth_hdrs, ink_time_t date = 0);

  static void convert_request(HTTPVersion outgoing_ver, HTTPHdr *outgoing_request);
  static void convert_response(HTTPVersion outgoing_ver, HTTPHdr *outgoing_response);
  static void convert_to_0_9_request_header(HTTPHdr *outgoing_request);
  static void convert_to_1_0_request_header(HTTPHdr *outgoing_request);
  static void convert_to_1_1_request_header(HTTPHdr *outgoing_request);
  static void convert_to_0_9_response_header(HTTPHdr *outgoing_response);
  static void convert_to_1_0_response_header(HTTPHdr *outgoing_response);
  static void convert_to_1_1_response_header(HTTPHdr *outgoing_response);

  static ink_time_t calculate_document_age(ink_time_t request_time, ink_time_t response_time, HTTPHdr *base_response,
                                           ink_time_t base_response_date, ink_time_t now);
  static bool does_server_allow_response_to_be_stored(HTTPHdr *resp);
  static bool downgrade_request(bool *origin_server_keep_alive, HTTPHdr *outgoing_request);
  static bool is_method_safe(int method);
  static bool is_method_idempotent(int method);

  static void generate_and_set_squid_codes(HTTPHdr *header, char *via_string, HttpTransact::SquidLogInfo *squid_codes);

  enum class ProtocolStackDetail { Compact, Standard, Full };

  static int write_hdr_protocol_stack(char *hdr_string, size_t len, ProtocolStackDetail pSDetail, std::string_view *proto_buf,
                                      int n_proto, char separator = ' ');

  // Removing handle_conditional_headers.  Functionality appears to be elsewhere (issue_revalidate)
  // and the only condition when it does anything causes an assert to go
  // off
  // static void handle_conditional_headers(HttpTransact::CacheLookupInfo * cache_info, HTTPHdr * header);
  static void insert_warning_header(HttpConfigParams *http_config_param, HTTPHdr *header, HTTPWarningCode code,
                                    const char *warn_text = nullptr, int warn_text_len = 0);
  static void insert_time_and_age_headers_in_response(ink_time_t request_sent_time, ink_time_t response_received_time,
                                                      ink_time_t now, HTTPHdr *base, HTTPHdr *outgoing);
  static void insert_via_header_in_request(HttpTransact::State *s, HTTPHdr *header);
  static void insert_via_header_in_response(HttpTransact::State *s, HTTPHdr *header);
  static void insert_hsts_header_in_response(HttpTransact::State *s, HTTPHdr *header);

  static void add_forwarded_field_to_request(HttpTransact::State *s, HTTPHdr *request);

  static bool is_request_proxy_authorized(HTTPHdr *incoming_hdr);

  static void normalize_accept_encoding(const OverridableHttpConfigParams *ohcp, HTTPHdr *header);

  static void remove_conditional_headers(HTTPHdr *outgoing);
  static void remove_100_continue_headers(HttpTransact::State *s, HTTPHdr *outgoing);
  static void remove_host_name_from_url(HTTPHdr *outgoing_request);
  static void add_global_user_agent_header_to_request(OverridableHttpConfigParams *http_txn_conf, HTTPHdr *header);
  static void add_server_header_to_response(OverridableHttpConfigParams *http_txn_conf, HTTPHdr *header);
  static void remove_privacy_headers_from_request(HttpConfigParams *http_config_param, OverridableHttpConfigParams *http_txn_conf,
                                                  HTTPHdr *header);
  static void add_connection_close(HTTPHdr *header);

  static int nstrcpy(char *d, const char *as);
};

/*****************************************************************************
 *****************************************************************************
 ****                                                                     ****
 ****                     Inline Utility Routines                         ****
 ****                                                                     ****
 *****************************************************************************
 *****************************************************************************/
inline bool
HttpTransactHeaders::is_this_http_method_supported(int method)
{
  return ((method == HTTP_WKSIDX_GET) || (method == HTTP_WKSIDX_POST) || (method == HTTP_WKSIDX_CONNECT) ||
          (method == HTTP_WKSIDX_DELETE) || (method == HTTP_WKSIDX_PURGE) || (method == HTTP_WKSIDX_HEAD) ||
          (method == HTTP_WKSIDX_OPTIONS) || (method == HTTP_WKSIDX_PUT) || (method == HTTP_WKSIDX_PUSH) ||
          (method == HTTP_WKSIDX_TRACE));
}

inline int
HttpTransactHeaders::nstrcpy(char *d, const char *as)
{
  const char *s = as;
  while (*s)
    *d++ = *s++;
  return s - as;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : is_request_proxy_authorized
// Description: does request authorization meet our authentication requirement
//
// Input      :
// Output     :
//
// Details    :
//   Currently a place holder.
inline bool
HttpTransactHeaders::is_request_proxy_authorized(HTTPHdr *incoming_hdr)
{
  ink_assert(incoming_hdr);
  // TODO: What do we need to do here?
  return true;
}
