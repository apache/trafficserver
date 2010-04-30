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

/****************************************************************************

   HttpTransactHeaders.h --
   Created On      : Fri Mar 27 12:13:52 1998

   
 ****************************************************************************/
#if !defined (_HttpTransactHeaders_h_)
#define _HttpTransactHeaders_h_

#define ink_time_t time_t

struct WUTSCode
{
  SquidHitMissCode squid_hit_miss_code;
  SquidLogCode squid_log_code[5];
  SquidHierarchyCode squid_hier_code[5];
  WUTSProxyId proxy_id[5];
  WUTSProxyStatusCode proxy_status_code;
};

extern int nstrhex(char *d, unsigned int i);

class HttpTransactHeaders
{
public:
  static bool is_this_http_method_supported(int method);
  static bool is_method_cacheable(int method);
  static bool is_method_cache_lookupable(int method);
  static bool is_this_a_hop_by_hop_header(const char *field_name_wks);
  static bool is_this_method_supported(int the_scheme, int the_method);

  static void insert_supported_methods_in_response(HTTPHdr * response, int the_scheme);

  static void build_base_response(HTTPHdr * outgoing_response, HTTPStatus status,
                                  const char *reason_phrase, int reason_phrase_len, ink_time_t date);

  static void copy_header_fields(HTTPHdr * src_hdr, HTTPHdr * new_hdr,
                                 bool retain_proxy_auth_hdrs, ink_time_t date = 0);

  static void convert_request(HTTPVersion outgoing_ver, HTTPHdr * outgoing_request);
  static void convert_response(HTTPVersion outgoing_ver, HTTPHdr * outgoing_response);
  static void convert_to_0_9_request_header(HTTPHdr * outgoing_request);
  static void convert_to_1_0_request_header(HTTPHdr * outgoing_request);
  static void convert_to_1_1_request_header(HTTPHdr * outgoing_request);
  static void convert_to_0_9_response_header(HTTPHdr * outgoing_response);
  static void convert_to_1_0_response_header(HTTPHdr * outgoing_response);
  static void convert_to_1_1_response_header(HTTPHdr * outgoing_response);

  static ink_time_t calculate_document_age(ink_time_t request_time, ink_time_t response_time,
                                           HTTPHdr * base_response, ink_time_t base_response_date, ink_time_t now);
  static bool does_server_allow_response_to_be_stored(HTTPHdr * resp);
  static bool downgrade_request(bool * origin_server_keep_alive, HTTPHdr * outgoing_request);

  static bool generate_basic_authorization_from_request(Arena *arena, HTTPHdr *h, char **username, char **password);
  static bool get_wuts_code(HTTPHdr * hdr, WUTSCode * w);
  static void set_wuts_codes(HTTPHdr * hdr, WUTSCode * code);
  static void set_wuts_codes(HTTPHdr * hdr, SquidHitMissCode hit_miss_code,
                             SquidLogCode log_code, SquidHierarchyCode hier_code,
                             WUTSProxyId proxy_id, WUTSProxyStatusCode proxy_status_code);
  static void generate_and_set_wuts_codes(HTTPHdr * header, char *via_string,
                                          HttpTransact::SquidLogInfo * squid_codes,
                                          int wuts_id, bool set_codes_in_hdr, bool log_spider_codes = false);
  //INKqa09773
  static void convert_wuts_code_to_normal_reason(HTTPHdr * header);
  static void handle_conditional_headers(HttpTransact::CacheLookupInfo * cache_info, HTTPHdr * header);
  static void insert_warning_header(HttpConfigParams *http_config_param,
                                    HTTPHdr *header, HTTPWarningCode code,
                                    const char *warn_text = NULL, int warn_text_len = 0);
  static void insert_time_and_age_headers_in_response(ink_time_t request_sent_time,
                                                      ink_time_t response_received_time,
                                                      ink_time_t now, HTTPHdr * base, HTTPHdr * outgoing);
  static void insert_server_header_in_response(const char *server_tag, int server_tag_size, HTTPHdr * header);
  static void insert_via_header_in_request(HttpConfigParams * http_config_param, int scheme,
                                           HttpTransact::CacheLookupInfo * cache_info, HTTPHdr * header,
                                           char *incoming_via, int proxy_ip_address);
  static void insert_via_header_in_response(HttpConfigParams * http_config_param, int scheme,
                                            HttpTransact::CacheLookupInfo * cache_info,
                                            HTTPHdr * header, char *incoming_via);

  static bool is_request_proxy_authorized(HTTPHdr * incoming_hdr);

  // to fix INKqa09089
  static void insert_basic_realm_in_proxy_authenticate(const char *realm, HTTPHdr * header, bool bRevPrxy);

  static void process_connection_headers(HTTPHdr * base, HTTPHdr * outgoing);
  static void process_connection_field_in_outgoing_header(HTTPHdr * base, HTTPHdr * header);
  static void process_proxy_connection_field_in_outgoing_header(HTTPHdr * base, HTTPHdr * header);
  static void _process_xxx_connection_field_in_outgoing_header(const char *wks_field_name, int wks_field_name_len,
                                                               HTTPHdr * base, HTTPHdr * header);

  static void remove_conditional_headers(HTTPHdr * base, HTTPHdr * outgoing);
  static void remove_host_name_from_url(HTTPHdr * outgoing_request);
  static void add_global_user_agent_header_to_request(HttpConfigParams * http_config_param, HTTPHdr * header);
  static void add_server_header_to_response(HttpConfigParams * http_config_param, HTTPHdr * header);
  static void remove_privacy_headers_from_request(HttpConfigParams * http_config_param, HTTPHdr * header);

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
  return ((method == HTTP_WKSIDX_GET) ||
          (method == HTTP_WKSIDX_POST) ||
          (method == HTTP_WKSIDX_CONNECT) ||
          (method == HTTP_WKSIDX_DELETE) ||
          (method == HTTP_WKSIDX_PURGE) ||
          (method == HTTP_WKSIDX_HEAD) ||
          (method == HTTP_WKSIDX_OPTIONS) ||
          (method == HTTP_WKSIDX_PUT) || (method == HTTP_WKSIDX_PUSH) || (method == HTTP_WKSIDX_TRACE));
}

inline int
HttpTransactHeaders::nstrcpy(char *d, const char *as)
{
  const char *s = as;
  while (*s)
    *d++ = *s++;
  return s - as;
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

///////////////////////////////////////////////////////////////////////////////
// Name       : is_request_proxy_authorized
// Description: does request authorization meet our authentication requirement
//
// Input      :
// Output     :
//
// Details    :
//   Currently a place holder.
//
//-----------------------------------------------------------------------------
//     For future reference, courtesy of bri:
//
// Date: Fri, 30 Jan 1998 17:24:02 +1000 (EST)
// From: David Richards <dj.richards@qut.edu.au>
// Subject: MSIE 3.0 and Squid
// To: Squid Discussion List <squid-users@nlanr.net>
// MIME-version: 1.0
// Resent-From: squid-users@nlanr.net
// X-Mailing-List: <squid-users@nlanr.net> archive/latest/628
// X-Loop: squid-users@nlanr.net
// Precedence: list
// Resent-Sender: squid-users-request@nlanr.net
// Content-Type: TEXT/PLAIN; charset=US-ASCII
//
// Just to let you all know, I have found a bug, whether it be in squid or in
// MSIE or a combination of both.
//
// When authentication is turned on, sometime MSIE provides the Proxy
// Authorization header incorrectly.
//
// The header usually has the following information:
//
// Proxy Authorization: /y^M
//
// Which some of you may realise, is incorrect.  The correct version is:
//
// Proxy Authorization: Basic WERFV@$#F@$#RWERFSDF@243=ewa^M
//
// or something similar.  The problem is that squid assumes that there is a
// "Basic" after the header, you may recall the code:
//
//     s += strlen(" Basic");
//     sent_userandpw = xstrdup(s);
//
// Well this is a memory violation and hence squid core dumps.  I fixed it
// putting before this code:
//
//      if( strlen( s ) <= 6 ) {     /* 6 => strlen( " Basic" ) */
//              return( dash_str );
//      }
//
// I have also made some modifications so that it returns a different error
// message, rather than Access Denied.
//-----------------------------------------------------------------------------
//
///////////////////////////////////////////////////////////////////////////////

inline bool
HttpTransactHeaders::is_request_proxy_authorized(HTTPHdr * incoming_hdr)
{
  HTTP_DEBUG_ASSERT(incoming_hdr);
  return true;
}



#endif
