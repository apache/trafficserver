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

/*************************** -*- Mod: C++ -*- ******************************

  HttpConfig.h
   Created On      : Tue Oct 22 20:19:04 1996

   Description:
   Http Configurations


 ****************************************************************************/
#pragma once

#include <cstdlib>
#include <cstdio>
#include <bitset>
#include <map>

#ifdef HAVE_CTYPE_H
#include <cctype>
#endif

#include "tscore/ink_platform.h"
#include "tscore/ink_inet.h"
#include "tscore/IpMap.h"
#include "tscore/Regex.h"
#include "string_view"
#include "tscore/BufferWriter.h"
#include "HttpProxyAPIEnums.h"
#include "ProxyConfig.h"
#include "records/P_RecProcess.h"
#include "HttpConnectionCount.h"

static const unsigned HTTP_STATUS_NUMBER = 600;
using HttpStatusBitset                   = std::bitset<HTTP_STATUS_NUMBER>;

/* Instead of enumerating the stats in DynamicStats.h, each module needs
   to enumerate its stats separately and register them with librecords
   */
enum {
  http_background_fill_current_count_stat,
  http_current_client_connections_stat,
  http_current_active_client_connections_stat,
  http_websocket_current_active_client_connections_stat,
  http_current_client_transactions_stat,
  http_total_incoming_connections_stat,
  http_current_server_transactions_stat,

  //  Http Abort information (from HttpNetConnection)
  http_ua_msecs_counts_errors_pre_accept_hangups_stat,

  // Http Total Connections Stats
  //
  // it is assumed that this inequality will always be satisfied:
  //   http_total_client_connections_stat >=
  //     http_total_client_connections_ipv4_stat +
  //     http_total_client_connections_ipv6_stat
  http_total_client_connections_stat,
  http_total_client_connections_ipv4_stat,
  http_total_client_connections_ipv6_stat,
  http_total_server_connections_stat,
  http_total_parent_proxy_connections_stat,
  http_total_parent_retries_stat,
  http_total_parent_switches_stat,
  http_total_parent_retries_exhausted_stat,
  http_total_parent_marked_down_count,
  http_current_parent_proxy_connections_stat,
  http_current_server_connections_stat,
  http_current_cache_connections_stat,

  // Http K-A Stats
  http_transactions_per_client_con,
  http_transactions_per_server_con,

  // Transactional stats
  http_incoming_requests_stat,
  http_outgoing_requests_stat,
  http_incoming_responses_stat,
  http_invalid_client_requests_stat,
  http_missing_host_hdr_stat,
  http_get_requests_stat,
  http_head_requests_stat,
  http_trace_requests_stat,
  http_options_requests_stat,
  http_post_requests_stat,
  http_put_requests_stat,
  http_push_requests_stat,
  http_delete_requests_stat,
  http_purge_requests_stat,
  http_connect_requests_stat,
  http_extension_method_requests_stat,

  http_completed_requests_stat,
  http_broken_server_connections_stat,

  http_cache_lookups_stat,
  http_cache_writes_stat,
  http_cache_updates_stat,
  http_cache_deletes_stat,

  http_tunnels_stat,

  // document size stats
  http_user_agent_request_header_total_size_stat,
  http_user_agent_response_header_total_size_stat,
  http_user_agent_request_document_total_size_stat,
  http_user_agent_response_document_total_size_stat,

  http_origin_server_request_header_total_size_stat,
  http_origin_server_response_header_total_size_stat,
  http_origin_server_request_document_total_size_stat,
  http_origin_server_response_document_total_size_stat,

  http_parent_proxy_request_total_bytes_stat,
  http_parent_proxy_response_total_bytes_stat,

  http_pushed_response_header_total_size_stat,
  http_pushed_document_total_size_stat,

  http_background_fill_bytes_aborted_stat,
  http_background_fill_bytes_completed_stat,

  http_response_document_size_100_stat,
  http_response_document_size_1K_stat,
  http_response_document_size_3K_stat,
  http_response_document_size_5K_stat,
  http_response_document_size_10K_stat,
  http_response_document_size_1M_stat,
  http_response_document_size_inf_stat,

  http_request_document_size_100_stat,
  http_request_document_size_1K_stat,
  http_request_document_size_3K_stat,
  http_request_document_size_5K_stat,
  http_request_document_size_10K_stat,
  http_request_document_size_1M_stat,
  http_request_document_size_inf_stat,

  // connection speed stats
  http_user_agent_speed_bytes_per_sec_100_stat,
  http_user_agent_speed_bytes_per_sec_1K_stat,
  http_user_agent_speed_bytes_per_sec_10K_stat,
  http_user_agent_speed_bytes_per_sec_100K_stat,
  http_user_agent_speed_bytes_per_sec_1M_stat,
  http_user_agent_speed_bytes_per_sec_10M_stat,
  http_user_agent_speed_bytes_per_sec_100M_stat,
  http_origin_server_speed_bytes_per_sec_100_stat,
  http_origin_server_speed_bytes_per_sec_1K_stat,
  http_origin_server_speed_bytes_per_sec_10K_stat,
  http_origin_server_speed_bytes_per_sec_100K_stat,
  http_origin_server_speed_bytes_per_sec_1M_stat,
  http_origin_server_speed_bytes_per_sec_10M_stat,
  http_origin_server_speed_bytes_per_sec_100M_stat,

  // cache result stats
  http_cache_hit_fresh_stat,
  http_cache_hit_mem_fresh_stat,
  http_cache_hit_reval_stat,
  http_cache_hit_ims_stat,
  http_cache_hit_stale_served_stat,
  http_cache_miss_cold_stat,
  http_cache_miss_changed_stat,
  http_cache_miss_client_no_cache_stat,
  http_cache_miss_uncacheable_stat,
  http_cache_miss_ims_stat,
  http_cache_read_error_stat,

  // bandwidth savings stats
  http_tcp_hit_count_stat,
  http_tcp_hit_user_agent_bytes_stat,
  http_tcp_hit_origin_server_bytes_stat,
  http_tcp_miss_count_stat,
  http_tcp_miss_user_agent_bytes_stat,
  http_tcp_miss_origin_server_bytes_stat,
  http_tcp_expired_miss_count_stat,
  http_tcp_expired_miss_user_agent_bytes_stat,
  http_tcp_expired_miss_origin_server_bytes_stat,
  http_tcp_refresh_hit_count_stat,
  http_tcp_refresh_hit_user_agent_bytes_stat,
  http_tcp_refresh_hit_origin_server_bytes_stat,
  http_tcp_refresh_miss_count_stat,
  http_tcp_refresh_miss_user_agent_bytes_stat,
  http_tcp_refresh_miss_origin_server_bytes_stat,
  http_tcp_client_refresh_count_stat,
  http_tcp_client_refresh_user_agent_bytes_stat,
  http_tcp_client_refresh_origin_server_bytes_stat,
  http_tcp_ims_hit_count_stat,
  http_tcp_ims_hit_user_agent_bytes_stat,
  http_tcp_ims_hit_origin_server_bytes_stat,
  http_tcp_ims_miss_count_stat,
  http_tcp_ims_miss_user_agent_bytes_stat,
  http_tcp_ims_miss_origin_server_bytes_stat,
  http_err_client_abort_count_stat,
  http_err_client_abort_user_agent_bytes_stat,
  http_err_client_abort_origin_server_bytes_stat,
  http_err_client_read_error_count_stat,
  http_err_client_read_error_user_agent_bytes_stat,
  http_err_client_read_error_origin_server_bytes_stat,
  http_err_connect_fail_count_stat,
  http_err_connect_fail_user_agent_bytes_stat,
  http_err_connect_fail_origin_server_bytes_stat,
  http_misc_count_stat,
  http_misc_user_agent_bytes_stat,
  http_misc_origin_server_bytes_stat,

  // http - time and count of transactions classified by client's point of view
  http_ua_msecs_counts_hit_fresh_stat,

  http_ua_msecs_counts_hit_fresh_process_stat,
  http_ua_msecs_counts_hit_reval_stat,
  http_ua_msecs_counts_miss_cold_stat,
  http_ua_msecs_counts_miss_changed_stat,
  http_ua_msecs_counts_miss_client_no_cache_stat,
  http_ua_msecs_counts_miss_uncacheable_stat,
  http_ua_msecs_counts_errors_aborts_stat,
  http_ua_msecs_counts_errors_possible_aborts_stat,
  http_ua_msecs_counts_errors_connect_failed_stat,
  http_ua_msecs_counts_errors_other_stat,
  http_ua_msecs_counts_other_unclassified_stat,

  disallowed_post_100_continue,
  http_post_body_too_large,

  http_total_x_redirect_stat,

  // Times
  http_total_transactions_time_stat,
  http_parent_proxy_transaction_time_stat,

  // Http cache errors
  http_cache_write_errors,
  http_cache_read_errors,

  // status code stats
  http_response_status_100_count_stat,
  http_response_status_101_count_stat,
  http_response_status_1xx_count_stat,
  http_response_status_200_count_stat,
  http_response_status_201_count_stat,
  http_response_status_202_count_stat,
  http_response_status_203_count_stat,
  http_response_status_204_count_stat,
  http_response_status_205_count_stat,
  http_response_status_206_count_stat,
  http_response_status_2xx_count_stat,
  http_response_status_300_count_stat,
  http_response_status_301_count_stat,
  http_response_status_302_count_stat,
  http_response_status_303_count_stat,
  http_response_status_304_count_stat,
  http_response_status_305_count_stat,
  http_response_status_307_count_stat,
  http_response_status_308_count_stat,
  http_response_status_3xx_count_stat,
  http_response_status_400_count_stat,
  http_response_status_401_count_stat,
  http_response_status_402_count_stat,
  http_response_status_403_count_stat,
  http_response_status_404_count_stat,
  http_response_status_405_count_stat,
  http_response_status_406_count_stat,
  http_response_status_407_count_stat,
  http_response_status_408_count_stat,
  http_response_status_409_count_stat,
  http_response_status_410_count_stat,
  http_response_status_411_count_stat,
  http_response_status_412_count_stat,
  http_response_status_413_count_stat,
  http_response_status_414_count_stat,
  http_response_status_415_count_stat,
  http_response_status_416_count_stat,
  http_response_status_4xx_count_stat,
  http_response_status_500_count_stat,
  http_response_status_501_count_stat,
  http_response_status_502_count_stat,
  http_response_status_503_count_stat,
  http_response_status_504_count_stat,
  http_response_status_505_count_stat,
  http_response_status_5xx_count_stat,

  https_incoming_requests_stat,
  https_total_client_connections_stat,

  // milestone timing statistics in milliseconds
  http_ua_begin_time_stat,
  http_ua_first_read_time_stat,
  http_ua_read_header_done_time_stat,
  http_ua_begin_write_time_stat,
  http_ua_close_time_stat,
  http_server_first_connect_time_stat,
  http_server_connect_time_stat,
  http_server_connect_end_time_stat,
  http_server_begin_write_time_stat,
  http_server_first_read_time_stat,
  http_server_read_header_done_time_stat,
  http_server_close_time_stat,
  http_cache_open_read_begin_time_stat,
  http_cache_open_read_end_time_stat,
  http_cache_open_write_begin_time_stat,
  http_cache_open_write_end_time_stat,
  http_dns_lookup_begin_time_stat,
  http_dns_lookup_end_time_stat,
  http_sm_start_time_stat,
  http_sm_finish_time_stat,

  http_origin_connections_throttled_stat,

  http_stat_count
};

enum CacheOpenWriteFailAction_t {
  CACHE_WL_FAIL_ACTION_DEFAULT                           = 0x00,
  CACHE_WL_FAIL_ACTION_ERROR_ON_MISS                     = 0x01,
  CACHE_WL_FAIL_ACTION_STALE_ON_REVALIDATE               = 0x02,
  CACHE_WL_FAIL_ACTION_ERROR_ON_MISS_STALE_ON_REVALIDATE = 0x03,
  CACHE_WL_FAIL_ACTION_ERROR_ON_MISS_OR_REVALIDATE       = 0x04,
  CACHE_WL_FAIL_ACTION_READ_RETRY                        = 0x05,
  TOTAL_CACHE_WL_FAIL_ACTION_TYPES
};

extern RecRawStatBlock *http_rsb;

/* Stats should only be accessed using these macros */
#define HTTP_INCREMENT_DYN_STAT(x) RecIncrRawStat(http_rsb, this_ethread(), (int)x, 1)
#define HTTP_DECREMENT_DYN_STAT(x) RecIncrRawStat(http_rsb, this_ethread(), (int)x, -1)
#define HTTP_SUM_DYN_STAT(x, y) RecIncrRawStat(http_rsb, this_ethread(), (int)x, (int64_t)y)
#define HTTP_SUM_GLOBAL_DYN_STAT(x, y) RecIncrGlobalRawStatSum(http_rsb, x, y)

#define HTTP_CLEAR_DYN_STAT(x)          \
  do {                                  \
    RecSetRawStatSum(http_rsb, x, 0);   \
    RecSetRawStatCount(http_rsb, x, 0); \
  } while (0);

#define HTTP_READ_DYN_SUM(x, S) RecGetRawStatSum(http_rsb, (int)x, &S) // This aggregates threads too
#define HTTP_READ_GLOBAL_DYN_SUM(x, S) RecGetGlobalRawStatSum(http_rsb, (int)x, &S)

/////////////////////////////////////////////////////////////
//
// struct HttpConfigPortRange
//
// configuration parameters for a range of valid SSL ports
// if "low" == "high" a single port is part of this range
// if "low" == "high" == -1 any port number is allowed
//   (corresponds to a "*" in the config file)
/////////////////////////////////////////////////////////////
struct HttpConfigPortRange {
  int low                   = 0;
  int high                  = 0;
  HttpConfigPortRange *next = nullptr;

  HttpConfigPortRange() {}
  ~HttpConfigPortRange()
  {
    if (next)
      delete next;
  }
};

namespace HttpForwarded
{
// Options for what parameters will be included in "Forwarded" field header.
//
enum Option {
  FOR,
  BY_IP,              // by=<numeric IP address>.
  BY_UNKNOWN,         // by=unknown.
  BY_SERVER_NAME,     // by=<configured server name>.
  BY_UUID,            // Obfuscated value for by, by=_<UUID>.
  PROTO,              // Basic protocol (http, https) of incoming message.
  HOST,               // Host from URL before any remapping.
  CONNECTION_COMPACT, // Same value as 'proto' parameter.
  CONNECTION_STD,     // Verbose protocol from Via: field, with dashes instead of spaces.
  CONNECTION_FULL,    // Ultra-verbose protocol from Via: field, with dashes instead of spaces.

  NUM_OPTIONS // Number of options.
};

using OptionBitSet = std::bitset<NUM_OPTIONS>;

// Converts string specifier for Forwarded options to bitset of options, and return the result.  If there are errors, an error
// message will be inserted into 'error'.
//
OptionBitSet optStrToBitset(std::string_view optConfigStr, ts::FixedBufferWriter &error);

} // namespace HttpForwarded

namespace RedirectEnabled
{
enum class AddressClass {
  INVALID = -1,
  DEFAULT,
  PRIVATE,
  LOOPBACK,
  MULTICAST,
  LINKLOCAL,
  ROUTABLE,
  SELF,
};

enum class Action {
  INVALID = -1,
  RETURN,
  REJECT,
  FOLLOW,
};

static std::map<std::string, AddressClass> address_class_map = {
  {"default", AddressClass::DEFAULT},     {"private", AddressClass::PRIVATE},     {"loopback", AddressClass::LOOPBACK},
  {"multicast", AddressClass::MULTICAST}, {"linklocal", AddressClass::LINKLOCAL}, {"routable", AddressClass::ROUTABLE},
  {"self", AddressClass::SELF},
};

static std::map<std::string, Action> action_map = {
  {"return", Action::RETURN},
  {"reject", Action::REJECT},
  {"follow", Action::FOLLOW},
};
} // namespace RedirectEnabled

/////////////////////////////////////////////////////////////
// This is a little helper class, used by the HttpConfigParams
// and State (txn) structure. It allows for certain configs
// to be overridable per transaction more easily.
struct OverridableHttpConfigParams {
  OverridableHttpConfigParams() : insert_forwarded(HttpForwarded::OptionBitSet()) {}

  // A simple rules here:
  //   * Place all MgmtByte configs before all other configs
  MgmtByte maintain_pristine_host_hdr = 1;
  MgmtByte chunking_enabled           = 1;

  ////////////////////////////////
  //  Negative Response Caching //
  ////////////////////////////////
  MgmtByte negative_caching_enabled      = 0;
  MgmtByte negative_revalidating_enabled = 0;

  MgmtByte cache_when_to_revalidate = 0;

  MgmtByte keep_alive_enabled_in  = 1;
  MgmtByte keep_alive_enabled_out = 1;
  MgmtByte keep_alive_post_out    = 1; // share server sessions for post

  MgmtInt server_min_keep_alive_conns         = 0;
  MgmtByte server_session_sharing_match       = TS_SERVER_SESSION_SHARING_MATCH_BOTH;
  MgmtByte auth_server_session_private        = 1;
  MgmtByte fwd_proxy_auth_to_parent           = 0;
  MgmtByte uncacheable_requests_bypass_parent = 1;
  MgmtByte attach_server_session_to_client    = 0;

  MgmtByte forward_connect_method = 0;

  MgmtByte insert_age_in_response = 1;

  ///////////////////////////////////////////////////////////////////
  // Privacy: fields which are removed from the user agent request //
  ///////////////////////////////////////////////////////////////////
  MgmtByte anonymize_remove_from       = 0;
  MgmtByte anonymize_remove_referer    = 0;
  MgmtByte anonymize_remove_user_agent = 0;
  MgmtByte anonymize_remove_cookie     = 0;
  MgmtByte anonymize_remove_client_ip  = 0;
  MgmtByte anonymize_insert_client_ip  = 1;

  MgmtByte proxy_response_server_enabled          = 1;
  MgmtByte proxy_response_hsts_include_subdomains = 0;

  /////////////////////
  // X-Forwarded-For //
  /////////////////////
  MgmtByte insert_squid_x_forwarded_for = 1;

  ///////////////
  // Forwarded //
  ///////////////
  HttpForwarded::OptionBitSet insert_forwarded;

  //////////////////////
  //  Version Hell    //
  //////////////////////
  MgmtByte send_http11_requests = 1;

  ///////////////////
  // cache control //
  ///////////////////
  MgmtByte cache_http                     = 1;
  MgmtByte cache_ignore_client_no_cache   = 1;
  MgmtByte cache_ignore_client_cc_max_age = 0;
  MgmtByte cache_ims_on_client_no_cache   = 1;
  MgmtByte cache_ignore_server_no_cache   = 0;
  MgmtByte cache_responses_to_cookies     = 1;
  MgmtByte cache_ignore_auth              = 0;
  MgmtByte cache_urls_that_look_dynamic   = 1;
  MgmtByte cache_required_headers         = 2;
  MgmtByte cache_range_lookup             = 1;
  MgmtByte cache_range_write              = 0;
  MgmtByte allow_multi_range              = 0;

  MgmtByte ignore_accept_mismatch          = 0;
  MgmtByte ignore_accept_language_mismatch = 0;
  MgmtByte ignore_accept_encoding_mismatch = 0;
  MgmtByte ignore_accept_charset_mismatch  = 0;

  MgmtByte insert_request_via_string  = 1;
  MgmtByte insert_response_via_string = 0;

  //////////////////////
  //  DOC IN CACHE NO DNS//
  //////////////////////
  MgmtByte doc_in_cache_skip_dns = 1;
  MgmtByte flow_control_enabled  = 0;

  ////////////////////////////////
  // Optimize gzip alternates   //
  ////////////////////////////////
  MgmtByte normalize_ae = 0;

  //////////////////////////
  // hostdb/dns variables //
  //////////////////////////
  MgmtByte srv_enabled                   = 0;
  MgmtByte parent_failures_update_hostdb = 0;

  MgmtByte cache_open_write_fail_action = 0;

  ////////////////////////
  // Check Post request //
  ////////////////////////
  MgmtByte post_check_content_length_enabled = 1;

  ////////////////////////////////////////////////
  // Buffer post body before connecting servers //
  ////////////////////////////////////////////////
  MgmtByte request_buffer_enabled = 0;

  /////////////////////////////////////////////////
  // Keep connection open after client sends FIN //
  /////////////////////////////////////////////////
  MgmtByte allow_half_open = 1;

  //////////////////////////////
  // server verification mode //
  //////////////////////////////
  MgmtByte ssl_client_verify_server         = 0;
  char *ssl_client_verify_server_policy     = nullptr;
  char *ssl_client_verify_server_properties = nullptr;
  char *ssl_client_sni_policy               = nullptr;

  //////////////////
  // Redirection  //
  //////////////////
  MgmtByte redirect_use_orig_cache_key = 0;
  MgmtInt number_of_redirections       = 0;

  MgmtInt proxy_response_hsts_max_age = -1;

  ////////////////////////////////
  //  Negative cache lifetimes  //
  ////////////////////////////////
  MgmtInt negative_caching_lifetime      = 1800;
  MgmtInt negative_revalidating_lifetime = 1800;

  ///////////////////////////////////////
  // origin server connection settings //
  ///////////////////////////////////////
  MgmtInt sock_recv_buffer_size_out = 0;
  MgmtInt sock_send_buffer_size_out = 0;
  MgmtInt sock_option_flag_out      = 0;
  MgmtInt sock_packet_mark_out      = 0;
  MgmtInt sock_packet_tos_out       = 0;

  ///////////////
  // Hdr Limit //
  ///////////////
  MgmtInt request_hdr_max_size  = 131072;
  MgmtInt response_hdr_max_size = 131072;

  /////////////////////
  // cache variables //
  /////////////////////
  MgmtInt cache_heuristic_min_lifetime  = 3600;
  MgmtInt cache_heuristic_max_lifetime  = 86400;
  MgmtInt cache_guaranteed_min_lifetime = 0;
  MgmtInt cache_guaranteed_max_lifetime = 31536000;
  MgmtInt cache_max_stale_age           = 604800;

  ///////////////////////////////////////////////////
  // connection variables. timeouts are in seconds //
  ///////////////////////////////////////////////////
  MgmtInt keep_alive_no_activity_timeout_in   = 120;
  MgmtInt keep_alive_no_activity_timeout_out  = 120;
  MgmtInt transaction_no_activity_timeout_in  = 30;
  MgmtInt transaction_no_activity_timeout_out = 30;
  MgmtInt transaction_active_timeout_out      = 0;
  MgmtInt transaction_active_timeout_in       = 900;
  MgmtInt websocket_active_timeout            = 3600;
  MgmtInt websocket_inactive_timeout          = 600;

  ////////////////////////////////////
  // origin server connect attempts //
  ////////////////////////////////////
  MgmtInt connect_attempts_max_retries             = 0;
  MgmtInt connect_attempts_max_retries_dead_server = 3;
  MgmtInt connect_attempts_rr_retries              = 3;
  MgmtInt connect_attempts_timeout                 = 30;
  MgmtInt post_connect_attempts_timeout            = 1800;

  ////////////////////////////////////
  // parent proxy connect attempts //
  ///////////////////////////////////
  MgmtInt parent_connect_attempts     = 4;
  MgmtInt parent_retry_time           = 300;
  MgmtInt parent_fail_threshold       = 10;
  MgmtInt per_parent_connect_attempts = 2;
  MgmtInt parent_connect_timeout      = 30;

  MgmtInt down_server_timeout    = 300;
  MgmtInt client_abort_threshold = 10;

  // open read failure retries.
  MgmtInt max_cache_open_read_retries = -1;
  MgmtInt cache_open_read_retry_time  = 10; // time is in mseconds
  MgmtInt cache_generation_number     = -1;

  // open write failure retries.
  MgmtInt max_cache_open_write_retries = 1;

  MgmtInt background_fill_active_timeout = 60;

  MgmtInt http_chunking_size   = 4096; // Maximum chunk size for chunked output.
  MgmtInt flow_high_water_mark = 0;    ///< Flow control high water mark.
  MgmtInt flow_low_water_mark  = 0;    ///< Flow control low water mark.

  MgmtInt default_buffer_size_index = 8;
  MgmtInt default_buffer_water_mark = 32768;
  MgmtInt slow_log_threshold        = 0;

  OutboundConnTrack::TxnConfig outbound_conntrack;

  ///////////////////////////////////////////////////////////////////
  // Server header                                                 //
  ///////////////////////////////////////////////////////////////////
  char *body_factory_template_base        = nullptr;
  size_t body_factory_template_base_len   = 0;
  char *proxy_response_server_string      = nullptr; // This does not get free'd by us!
  size_t proxy_response_server_string_len = 0;       // Updated when server_string is set.

  ///////////////////////////////////////////////////////////////////
  // Global User Agent header                                      //
  ///////////////////////////////////////////////////////////////////
  char *global_user_agent_header       = nullptr; // This does not get free'd by us!
  size_t global_user_agent_header_size = 0;       // Updated when user_agent is set.

  MgmtFloat cache_heuristic_lm_factor = 0.10;
  MgmtFloat background_fill_threshold = 0.5;

  // Various strings, good place for them here ...
  char *ssl_client_cert_filename        = nullptr;
  char *ssl_client_private_key_filename = nullptr;
  char *ssl_client_ca_cert_filename     = nullptr;
};

/////////////////////////////////////////////////////////////
//
// struct HttpConfigParams
//
// configuration parameters as they apear in the global
// configuration file.
/////////////////////////////////////////////////////////////
struct HttpConfigParams : public ConfigInfo {
public:
  HttpConfigParams();
  ~HttpConfigParams() override;

  enum {
    CACHE_REQUIRED_HEADERS_NONE                   = 0,
    CACHE_REQUIRED_HEADERS_AT_LEAST_LAST_MODIFIED = 1,
    CACHE_REQUIRED_HEADERS_CACHE_CONTROL          = 2
  };

  enum {
    SEND_HTTP11_NEVER                    = 0,
    SEND_HTTP11_ALWAYS                   = 1,
    SEND_HTTP11_UPGRADE_HOSTDB           = 2,
    SEND_HTTP11_IF_REQUEST_11_AND_HOSTDB = 3,
  };

public:
  IpAddr inbound_ip4, inbound_ip6;
  IpAddr outbound_ip4, outbound_ip6;
  IpAddr proxy_protocol_ip4, proxy_protocol_ip6;
  IpMap config_proxy_protocol_ipmap;

  MgmtInt server_max_connections    = 0;
  MgmtInt max_websocket_connections = -1;

  char *proxy_request_via_string    = nullptr;
  char *proxy_response_via_string   = nullptr;
  int proxy_request_via_string_len  = 0;
  int proxy_response_via_string_len = 0;

  MgmtInt accept_no_activity_timeout = 120;

  ///////////////////////////////////////////////////////////////////
  // Privacy: fields which are removed from the user agent request //
  ///////////////////////////////////////////////////////////////////
  char *anonymize_other_header_list = nullptr;

  ////////////////////////////////////////////
  // CONNECT ports (used to be == ssl_ports //
  ////////////////////////////////////////////
  char *connect_ports_string         = nullptr;
  HttpConfigPortRange *connect_ports = nullptr;

  char *reverse_proxy_no_host_redirect   = nullptr;
  char *proxy_hostname                   = nullptr;
  int reverse_proxy_no_host_redirect_len = 0;
  int proxy_hostname_len                 = 0;

  MgmtInt post_copy_size = 2048;
  MgmtInt max_post_size  = 0;

  char *redirect_actions_string                        = nullptr;
  IpMap *redirect_actions_map                          = nullptr;
  RedirectEnabled::Action redirect_actions_self_action = RedirectEnabled::Action::INVALID;

  ///////////////////////////////////////////////////////////////////
  // Put all MgmtByte members down here, avoids additional padding //
  ///////////////////////////////////////////////////////////////////
  MgmtByte disable_ssl_parenting = 0;

  MgmtByte no_dns_forward_to_parent = 0;
  MgmtByte no_origin_server_dns     = 0;
  MgmtByte use_client_target_addr   = 0;
  MgmtByte use_client_source_port   = 0;

  MgmtByte enable_http_stats = 1; // Can be "slow"

  MgmtByte cache_post_method = 0;

  MgmtByte push_method_enabled = 0;

  MgmtByte referer_filter_enabled  = 0;
  MgmtByte referer_format_redirect = 0;

  MgmtByte strict_uri_parsing = 0;

  MgmtByte reverse_proxy_enabled = 0;
  MgmtByte url_remap_required    = 1;

  MgmtByte errors_log_error_pages = 1;
  MgmtByte enable_http_info       = 0;

  MgmtByte redirection_host_no_port = 1;

  MgmtByte send_100_continue_response = 0;
  MgmtByte disallow_post_100_continue = 0;
  MgmtByte keepalive_internal_vc      = 0;

  MgmtByte server_session_sharing_pool = TS_SERVER_SESSION_SHARING_POOL_THREAD;

  OutboundConnTrack::GlobalConfig outbound_conntrack;

  // bitset to hold the status codes that will BE cached with negative caching enabled
  HttpStatusBitset negative_caching_list;

  // All the overridable configurations goes into this class member, but they
  // are not copied over until needed ("lazy").
  OverridableHttpConfigParams oride;

  MgmtInt body_factory_response_max_size = 8192;

  MgmtInt http_request_line_max_size = 65535;
  MgmtInt http_hdr_field_max_size    = 131070;

  MgmtByte http_host_sni_policy = 0;

  // noncopyable
  /////////////////////////////////////
  // operator = and copy constructor //
  /////////////////////////////////////
  HttpConfigParams(const HttpConfigParams &) = delete;
  HttpConfigParams &operator=(const HttpConfigParams &) = delete;
};

/////////////////////////////////////////////////////////////
//
// class HttpConfig
//
/////////////////////////////////////////////////////////////
class HttpConfig
{
public:
  static void startup();

  static void reconfigure();

  inkcoreapi static HttpConfigParams *acquire();
  inkcoreapi static void release(HttpConfigParams *params);

  // parse ssl ports configuration string
  static HttpConfigPortRange *parse_ports_list(char *ports_str);

  // parse redirect configuration string
  static IpMap *parse_redirect_actions(char *redirect_actions_string, RedirectEnabled::Action &self_action);

public:
  static int m_id;
  static HttpConfigParams m_master;
};

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
//
//  inline functions
//
/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
inline HttpConfigParams::HttpConfigParams() {}

inline HttpConfigParams::~HttpConfigParams()
{
  ats_free(proxy_hostname);
  ats_free(proxy_request_via_string);
  ats_free(proxy_response_via_string);
  ats_free(anonymize_other_header_list);
  ats_free(oride.body_factory_template_base);
  ats_free(oride.proxy_response_server_string);
  ats_free(oride.global_user_agent_header);
  ats_free(oride.ssl_client_cert_filename);
  ats_free(oride.ssl_client_private_key_filename);
  ats_free(oride.ssl_client_ca_cert_filename);
  ats_free(connect_ports_string);
  ats_free(reverse_proxy_no_host_redirect);
  ats_free(redirect_actions_string);
  ats_free(oride.ssl_client_sni_policy);

  delete connect_ports;
  delete redirect_actions_map;
}

/** Enable a dynamic configuration variable.
 *
 * @param name Configuration var name.
 * @param cb Callback to do the actual update of the master record.
 * @param cookie Extra data for @a cb
 *
 * The purpose of this is to unite the different ways and times a configuration variable needs
 * to be loaded. These are
 * - Process start.
 * - Dynamic update.
 * - Plugin API update.
 *
 * @a cb is expected to perform the update. It must return a @c bool which is
 * - @c true if the value was changed.
 * - @c false if the value was not changed.
 *
 * Based on that, a run time configuration update is triggered or not.
 *
 * In addition, this invokes @a cb and passes it the information in the configuration variable
 * global table in order to perform the initial loading of the value. No update is triggered for
 * that call as it is not needed.
 *
 */
extern void Enable_Config_Var(std::string_view const &name, bool (*cb)(const char *, RecDataT, RecData, void *), void *cookie);
