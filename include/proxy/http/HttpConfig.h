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
#include <cctype>
#include <string_view>
#include <chrono>

#include "swoc/swoc_ip.h"

#include "tscore/ink_platform.h"
#include "tscore/ink_inet.h"
#include "tscore/ink_resolver.h"
#include "tscore/Regex.h"
#include "tscpp/util/ts_bw.h"
#include "iocore/eventsystem/ConfigProcessor.h"
#include "iocore/net/ConnectionTracker.h"
#include "iocore/net/SessionSharingAPIEnums.h"
#include "records/RecProcess.h"
#include "tscpp/util/ts_ip.h"
#include "api/Metrics.h"

using ts::Metrics;

static const unsigned HTTP_STATUS_NUMBER = 600;
using HttpStatusBitset                   = std::bitset<HTTP_STATUS_NUMBER>;

struct HttpStatsBlock {
  // Need two stats for these for counts and times
  ts::Metrics::IntType *background_fill_bytes_aborted;
  ts::Metrics::IntType *background_fill_bytes_completed;
  ts::Metrics::IntType *background_fill_current_count;
  ts::Metrics::IntType *background_fill_total_count;
  ts::Metrics::IntType *broken_server_connections;
  ts::Metrics::IntType *cache_deletes;
  ts::Metrics::IntType *cache_hit_fresh;
  ts::Metrics::IntType *cache_hit_ims;
  ts::Metrics::IntType *cache_hit_mem_fresh;
  ts::Metrics::IntType *cache_hit_reval;
  ts::Metrics::IntType *cache_hit_rww;
  ts::Metrics::IntType *cache_hit_stale_served;
  ts::Metrics::IntType *cache_lookups;
  ts::Metrics::IntType *cache_miss_changed;
  ts::Metrics::IntType *cache_miss_client_no_cache;
  ts::Metrics::IntType *cache_miss_cold;
  ts::Metrics::IntType *cache_miss_ims;
  ts::Metrics::IntType *cache_miss_uncacheable;
  ts::Metrics::IntType *cache_open_read_begin_time;
  ts::Metrics::IntType *cache_open_read_end_time;
  ts::Metrics::IntType *cache_open_write_adjust_thread;
  ts::Metrics::IntType *cache_open_write_begin_time;
  ts::Metrics::IntType *cache_open_write_end_time;
  ts::Metrics::IntType *cache_read_error;
  ts::Metrics::IntType *cache_read_errors;
  ts::Metrics::IntType *cache_updates;
  ts::Metrics::IntType *cache_write_errors;
  ts::Metrics::IntType *cache_writes;
  ts::Metrics::IntType *completed_requests;
  ts::Metrics::IntType *connect_requests;
  ts::Metrics::IntType *current_active_client_connections;
  ts::Metrics::IntType *current_cache_connections;
  ts::Metrics::IntType *current_client_connections;
  ts::Metrics::IntType *current_client_transactions;
  ts::Metrics::IntType *current_parent_proxy_connections;
  ts::Metrics::IntType *current_server_connections;
  ts::Metrics::IntType *current_server_transactions;
  ts::Metrics::IntType *delete_requests;
  ts::Metrics::IntType *disallowed_post_100_continue;
  ts::Metrics::IntType *dns_lookup_begin_time;
  ts::Metrics::IntType *dns_lookup_end_time;
  ts::Metrics::IntType *down_server_no_requests;
  ts::Metrics::IntType *err_client_abort_count;
  ts::Metrics::IntType *err_client_abort_origin_server_bytes;
  ts::Metrics::IntType *err_client_abort_user_agent_bytes;
  ts::Metrics::IntType *err_client_read_error_count;
  ts::Metrics::IntType *err_client_read_error_origin_server_bytes;
  ts::Metrics::IntType *err_client_read_error_user_agent_bytes;
  ts::Metrics::IntType *err_connect_fail_count;
  ts::Metrics::IntType *err_connect_fail_origin_server_bytes;
  ts::Metrics::IntType *err_connect_fail_user_agent_bytes;
  ts::Metrics::IntType *extension_method_requests;
  ts::Metrics::IntType *get_requests;
  ts::Metrics::IntType *head_requests;
  ts::Metrics::IntType *https_incoming_requests;
  ts::Metrics::IntType *https_total_client_connections;
  ts::Metrics::IntType *incoming_requests;
  ts::Metrics::IntType *incoming_responses;
  ts::Metrics::IntType *invalid_client_requests;
  ts::Metrics::IntType *misc_count;
  ts::Metrics::IntType *misc_origin_server_bytes;
  ts::Metrics::IntType *misc_user_agent_bytes;
  ts::Metrics::IntType *missing_host_hdr;
  ts::Metrics::IntType *options_requests;
  ts::Metrics::IntType *origin_body;
  ts::Metrics::IntType *origin_close_private;
  ts::Metrics::IntType *origin_connect_adjust_thread;
  ts::Metrics::IntType *origin_connections_throttled;
  ts::Metrics::IntType *origin_make_new;
  ts::Metrics::IntType *origin_no_sharing;
  ts::Metrics::IntType *origin_not_found;
  ts::Metrics::IntType *origin_private;
  ts::Metrics::IntType *origin_raw;
  ts::Metrics::IntType *origin_reuse;
  ts::Metrics::IntType *origin_reuse_fail;
  ts::Metrics::IntType *origin_server_request_document_total_size;
  ts::Metrics::IntType *origin_server_request_header_total_size;
  ts::Metrics::IntType *origin_server_response_document_total_size;
  ts::Metrics::IntType *origin_server_response_header_total_size;
  ts::Metrics::IntType *origin_shutdown_cleanup_entry;
  ts::Metrics::IntType *origin_shutdown_migration_failure;
  ts::Metrics::IntType *origin_shutdown_pool_lock_contention;
  ts::Metrics::IntType *origin_shutdown_release_invalid_request;
  ts::Metrics::IntType *origin_shutdown_release_invalid_response;
  ts::Metrics::IntType *origin_shutdown_release_misc;
  ts::Metrics::IntType *origin_shutdown_release_modified;
  ts::Metrics::IntType *origin_shutdown_release_no_keep_alive;
  ts::Metrics::IntType *origin_shutdown_release_no_server;
  ts::Metrics::IntType *origin_shutdown_release_no_sharing;
  ts::Metrics::IntType *origin_shutdown_tunnel_abort;
  ts::Metrics::IntType *origin_shutdown_tunnel_client;
  ts::Metrics::IntType *origin_shutdown_tunnel_server;
  ts::Metrics::IntType *origin_shutdown_tunnel_server_detach;
  ts::Metrics::IntType *origin_shutdown_tunnel_server_eos;
  ts::Metrics::IntType *origin_shutdown_tunnel_server_no_keep_alive;
  ts::Metrics::IntType *origin_shutdown_tunnel_server_plugin_tunnel;
  ts::Metrics::IntType *origin_shutdown_tunnel_transform_read;
  ts::Metrics::IntType *outgoing_requests;
  ts::Metrics::IntType *parent_count;
  ts::Metrics::IntType *parent_proxy_request_total_bytes;
  ts::Metrics::IntType *parent_proxy_response_total_bytes;
  ts::Metrics::IntType *parent_proxy_transaction_time;
  ts::Metrics::IntType *pooled_server_connections;
  ts::Metrics::IntType *post_body_too_large;
  ts::Metrics::IntType *post_requests;
  ts::Metrics::IntType *proxy_loop_detected;
  ts::Metrics::IntType *proxy_mh_loop_detected;
  ts::Metrics::IntType *purge_requests;
  ts::Metrics::IntType *push_requests;
  ts::Metrics::IntType *pushed_document_total_size;
  ts::Metrics::IntType *pushed_response_header_total_size;
  ts::Metrics::IntType *put_requests;
  ts::Metrics::IntType *response_status_100_count;
  ts::Metrics::IntType *response_status_101_count;
  ts::Metrics::IntType *response_status_1xx_count;
  ts::Metrics::IntType *response_status_200_count;
  ts::Metrics::IntType *response_status_201_count;
  ts::Metrics::IntType *response_status_202_count;
  ts::Metrics::IntType *response_status_203_count;
  ts::Metrics::IntType *response_status_204_count;
  ts::Metrics::IntType *response_status_205_count;
  ts::Metrics::IntType *response_status_206_count;
  ts::Metrics::IntType *response_status_2xx_count;
  ts::Metrics::IntType *response_status_300_count;
  ts::Metrics::IntType *response_status_301_count;
  ts::Metrics::IntType *response_status_302_count;
  ts::Metrics::IntType *response_status_303_count;
  ts::Metrics::IntType *response_status_304_count;
  ts::Metrics::IntType *response_status_305_count;
  ts::Metrics::IntType *response_status_307_count;
  ts::Metrics::IntType *response_status_308_count;
  ts::Metrics::IntType *response_status_3xx_count;
  ts::Metrics::IntType *response_status_400_count;
  ts::Metrics::IntType *response_status_401_count;
  ts::Metrics::IntType *response_status_402_count;
  ts::Metrics::IntType *response_status_403_count;
  ts::Metrics::IntType *response_status_404_count;
  ts::Metrics::IntType *response_status_405_count;
  ts::Metrics::IntType *response_status_406_count;
  ts::Metrics::IntType *response_status_407_count;
  ts::Metrics::IntType *response_status_408_count;
  ts::Metrics::IntType *response_status_409_count;
  ts::Metrics::IntType *response_status_410_count;
  ts::Metrics::IntType *response_status_411_count;
  ts::Metrics::IntType *response_status_412_count;
  ts::Metrics::IntType *response_status_413_count;
  ts::Metrics::IntType *response_status_414_count;
  ts::Metrics::IntType *response_status_415_count;
  ts::Metrics::IntType *response_status_416_count;
  ts::Metrics::IntType *response_status_4xx_count;
  ts::Metrics::IntType *response_status_500_count;
  ts::Metrics::IntType *response_status_501_count;
  ts::Metrics::IntType *response_status_502_count;
  ts::Metrics::IntType *response_status_503_count;
  ts::Metrics::IntType *response_status_504_count;
  ts::Metrics::IntType *response_status_505_count;
  ts::Metrics::IntType *response_status_5xx_count;
  ts::Metrics::IntType *server_begin_write_time;
  ts::Metrics::IntType *server_close_time;
  ts::Metrics::IntType *server_connect_end_time;
  ts::Metrics::IntType *server_connect_time;
  ts::Metrics::IntType *server_first_connect_time;
  ts::Metrics::IntType *server_first_read_time;
  ts::Metrics::IntType *server_read_header_done_time;
  ts::Metrics::IntType *sm_finish_time;
  ts::Metrics::IntType *sm_start_time;
  ts::Metrics::IntType *tcp_client_refresh_count;
  ts::Metrics::IntType *tcp_client_refresh_origin_server_bytes;
  ts::Metrics::IntType *tcp_client_refresh_user_agent_bytes;
  ts::Metrics::IntType *tcp_expired_miss_count;
  ts::Metrics::IntType *tcp_expired_miss_origin_server_bytes;
  ts::Metrics::IntType *tcp_expired_miss_user_agent_bytes;
  ts::Metrics::IntType *tcp_hit_count;
  ts::Metrics::IntType *tcp_hit_origin_server_bytes;
  ts::Metrics::IntType *tcp_hit_user_agent_bytes;
  ts::Metrics::IntType *tcp_ims_hit_count;
  ts::Metrics::IntType *tcp_ims_hit_origin_server_bytes;
  ts::Metrics::IntType *tcp_ims_hit_user_agent_bytes;
  ts::Metrics::IntType *tcp_ims_miss_count;
  ts::Metrics::IntType *tcp_ims_miss_origin_server_bytes;
  ts::Metrics::IntType *tcp_ims_miss_user_agent_bytes;
  ts::Metrics::IntType *tcp_miss_count;
  ts::Metrics::IntType *tcp_miss_origin_server_bytes;
  ts::Metrics::IntType *tcp_miss_user_agent_bytes;
  ts::Metrics::IntType *tcp_refresh_hit_count;
  ts::Metrics::IntType *tcp_refresh_hit_origin_server_bytes;
  ts::Metrics::IntType *tcp_refresh_hit_user_agent_bytes;
  ts::Metrics::IntType *tcp_refresh_miss_count;
  ts::Metrics::IntType *tcp_refresh_miss_origin_server_bytes;
  ts::Metrics::IntType *tcp_refresh_miss_user_agent_bytes;
  ts::Metrics::IntType *total_client_connections;
  ts::Metrics::IntType *total_client_connections_ipv4;
  ts::Metrics::IntType *total_client_connections_ipv6;
  ts::Metrics::IntType *total_incoming_connections;
  ts::Metrics::IntType *total_parent_marked_down_count;
  ts::Metrics::IntType *total_parent_proxy_connections;
  ts::Metrics::IntType *total_parent_retries;
  ts::Metrics::IntType *total_parent_retries_exhausted;
  ts::Metrics::IntType *total_parent_switches;
  ts::Metrics::IntType *total_server_connections;
  ts::Metrics::IntType *total_transactions_time;
  ts::Metrics::IntType *total_x_redirect;
  ts::Metrics::IntType *trace_requests;
  ts::Metrics::IntType *tunnel_current_active_connections;
  ts::Metrics::IntType *tunnels;
  ts::Metrics::IntType *ua_begin_time;
  ts::Metrics::IntType *ua_begin_write_time;
  ts::Metrics::IntType *ua_close_time;
  ts::Metrics::IntType *ua_counts_errors_aborts;
  ts::Metrics::IntType *ua_counts_errors_connect_failed;
  ts::Metrics::IntType *ua_counts_errors_other;
  ts::Metrics::IntType *ua_counts_errors_possible_aborts;
  ts::Metrics::IntType *ua_counts_errors_pre_accept_hangups;
  ts::Metrics::IntType *ua_counts_hit_fresh;
  ts::Metrics::IntType *ua_counts_hit_fresh_process;
  ts::Metrics::IntType *ua_counts_hit_reval;
  ts::Metrics::IntType *ua_counts_miss_changed;
  ts::Metrics::IntType *ua_counts_miss_client_no_cache;
  ts::Metrics::IntType *ua_counts_miss_cold;
  ts::Metrics::IntType *ua_counts_miss_uncacheable;
  ts::Metrics::IntType *ua_counts_other_unclassified;
  ts::Metrics::IntType *ua_first_read_time;
  ts::Metrics::IntType *ua_msecs_errors_aborts;
  ts::Metrics::IntType *ua_msecs_errors_connect_failed;
  ts::Metrics::IntType *ua_msecs_errors_other;
  ts::Metrics::IntType *ua_msecs_errors_possible_aborts;
  ts::Metrics::IntType *ua_msecs_errors_pre_accept_hangups;
  ts::Metrics::IntType *ua_msecs_hit_fresh;
  ts::Metrics::IntType *ua_msecs_hit_fresh_process;
  ts::Metrics::IntType *ua_msecs_hit_reval;
  ts::Metrics::IntType *ua_msecs_miss_changed;
  ts::Metrics::IntType *ua_msecs_miss_client_no_cache;
  ts::Metrics::IntType *ua_msecs_miss_cold;
  ts::Metrics::IntType *ua_msecs_miss_uncacheable;
  ts::Metrics::IntType *ua_msecs_other_unclassified;
  ts::Metrics::IntType *ua_read_header_done_time;
  ts::Metrics::IntType *user_agent_request_document_total_size;
  ts::Metrics::IntType *user_agent_request_header_total_size;
  ts::Metrics::IntType *user_agent_response_document_total_size;
  ts::Metrics::IntType *user_agent_response_header_total_size;
  ts::Metrics::IntType *websocket_current_active_client_connections;
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

extern HttpStatsBlock http_rsb;

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
OptionBitSet optStrToBitset(std::string_view optConfigStr, swoc::FixedBufferWriter &error);

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

using ActionMap = swoc::IPSpace<Action>;

static std::map<std::string, AddressClass> address_class_map = {
  {"default",   AddressClass::DEFAULT  },
  {"private",   AddressClass::PRIVATE  },
  {"loopback",  AddressClass::LOOPBACK },
  {"multicast", AddressClass::MULTICAST},
  {"linklocal", AddressClass::LINKLOCAL},
  {"routable",  AddressClass::ROUTABLE },
  {"self",      AddressClass::SELF     },
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
  MgmtByte server_session_sharing_match       = 0;
  char *server_session_sharing_match_str      = nullptr;
  MgmtByte auth_server_session_private        = 1;
  MgmtByte fwd_proxy_auth_to_parent           = 0;
  MgmtByte uncacheable_requests_bypass_parent = 1;
  MgmtByte attach_server_session_to_client    = 0;
  MgmtInt max_proxy_cycles                    = 0;
  MgmtInt tunnel_activity_check_period        = 0;
  MgmtInt default_inactivity_timeout          = 24 * 60 * 60;

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
  MgmtInt proxy_protocol_out = -1;

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
  MgmtByte cache_ignore_query             = 0;
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

  /////////////////////////
  // DOC IN CACHE NO DNS //
  /////////////////////////
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
  MgmtByte no_dns_forward_to_parent      = 0;

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

  /////////////////////////////////////////////////
  // Body factory
  /////////////////////////////////////////////////
  MgmtByte response_suppression_mode = 0; // proxy.config.body_factory.response_suppression_mode

  //////////////////
  // Redirection  //
  //////////////////
  MgmtByte redirect_use_orig_cache_key = 0;
  MgmtInt number_of_redirections       = 0;

  //////////////////////////////
  // server verification mode //
  //////////////////////////////
  char *ssl_client_verify_server_policy     = nullptr;
  char *ssl_client_verify_server_properties = nullptr;
  char *ssl_client_sni_policy               = nullptr;

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
  MgmtInt sock_packet_notsent_lowat = 0;

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
  MgmtInt connect_attempts_max_retries_down_server = 3;
  MgmtInt connect_attempts_rr_retries              = 3;
  MgmtInt connect_attempts_timeout                 = 30;

  MgmtInt connect_down_policy = 2;

  ////////////////////////////////////
  // parent proxy connect attempts //
  ///////////////////////////////////
  MgmtInt parent_connect_attempts          = 4;
  MgmtInt parent_retry_time                = 300;
  MgmtInt parent_fail_threshold            = 10;
  MgmtInt per_parent_connect_attempts      = 2;
  MgmtByte enable_parent_timeout_markdowns = 0;
  MgmtByte disable_parent_markdowns        = 0;

  ts_seconds down_server_timeout{300};

  // open read failure retries.
  MgmtInt max_cache_open_read_retries = -1;
  MgmtInt cache_open_read_retry_time  = 10; // time in mseconds
  MgmtInt cache_generation_number     = -1;

  // open write failure retries.
  MgmtInt max_cache_open_write_retries       = 1;
  MgmtInt max_cache_open_write_retry_timeout = 0; // time in mseconds

  MgmtInt background_fill_active_timeout = 60;

  MgmtInt http_chunking_size   = 4096; // Maximum chunk size for chunked output.
  MgmtInt flow_high_water_mark = 0;    ///< Flow control high water mark.
  MgmtInt flow_low_water_mark  = 0;    ///< Flow control low water mark.

  MgmtInt default_buffer_size_index = 8;
  MgmtInt default_buffer_water_mark = 32768;
  MgmtInt slow_log_threshold        = 0;

  ConnectionTracker::TxnConfig connection_tracker_config;

  MgmtInt plugin_vc_default_buffer_index      = BUFFER_SIZE_INDEX_32K;
  MgmtInt plugin_vc_default_buffer_water_mark = DEFAULT_PLUGIN_VC_BUFFER_WATER_MARK;

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
  char *ssl_client_alpn_protocols       = nullptr;

  // Host Resolution order
  HostResData host_res_data;
};

/////////////////////////////////////////////////////////////
//
// struct HttpConfigParams
//
// configuration parameters as they appear in the global
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
  ts::IPAddrPair inbound;
  // Initialize to any addr (default constructed) because these must always be set.
  ts::IPAddrPair outbound;
  IpAddr proxy_protocol_ip4, proxy_protocol_ip6;
  swoc::IPRangeSet config_proxy_protocol_ip_addrs;

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

  MgmtInt max_payload_iobuf_index = BUFFER_SIZE_INDEX_32K;
  MgmtInt max_msg_iobuf_index     = BUFFER_SIZE_INDEX_32K;

  char *redirect_actions_string                        = nullptr;
  RedirectEnabled::ActionMap *redirect_actions_map     = nullptr;
  RedirectEnabled::Action redirect_actions_self_action = RedirectEnabled::Action::INVALID;

  ///////////////////////////////////////////////////////////////////
  // Put all MgmtByte members down here, avoids additional padding //
  ///////////////////////////////////////////////////////////////////
  MgmtByte disable_ssl_parenting = 0;

  MgmtByte no_origin_server_dns   = 0;
  MgmtByte use_client_target_addr = 0;
  MgmtByte use_client_source_port = 0;

  MgmtByte enable_http_stats = 1; // Can be "slow"

  MgmtByte cache_post_method = 0;

  MgmtByte push_method_enabled = 0;

  MgmtByte referer_filter_enabled  = 0;
  MgmtByte referer_format_redirect = 0;

  MgmtByte strict_uri_parsing = 2;

  MgmtByte reverse_proxy_enabled = 0;
  MgmtByte url_remap_required    = 1;

  MgmtByte errors_log_error_pages = 1;
  MgmtByte enable_http_info       = 0;

  MgmtByte redirection_host_no_port = 1;

  MgmtByte send_100_continue_response = 0;
  MgmtByte disallow_post_100_continue = 0;

  MgmtByte server_session_sharing_pool = TS_SERVER_SESSION_SHARING_POOL_THREAD;

  ConnectionTracker::GlobalConfig global_connection_tracker_config;

  // bitset to hold the status codes that will BE cached with negative caching enabled
  HttpStatusBitset negative_caching_list;

  // All the overridable configurations goes into this class member, but they
  // are not copied over until needed ("lazy").
  OverridableHttpConfigParams oride;

  MgmtInt body_factory_response_max_size = 8192;

  MgmtInt http_request_line_max_size = 65535;
  MgmtInt http_hdr_field_max_size    = 131070;

  MgmtByte http_host_sni_policy         = 0;
  MgmtByte scheme_proto_mismatch_policy = 2;

  // noncopyable
  /////////////////////////////////////
  // operator = and copy constructor //
  /////////////////////////////////////
  HttpConfigParams(const HttpConfigParams &)            = delete;
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
  using scoped_config = ConfigProcessor::scoped_config<HttpConfig, HttpConfigParams>;

  static void startup();

  static void reconfigure();

  static HttpConfigParams *acquire();
  static void release(HttpConfigParams *params);

  static bool load_server_session_sharing_match(const char *key, MgmtByte &mask);

  // parse ssl ports configuration string
  static HttpConfigPortRange *parse_ports_list(char *ports_str);

  // parse redirect configuration string
  static RedirectEnabled::ActionMap *parse_redirect_actions(char *redirect_actions_string, RedirectEnabled::Action &self_action);

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
  ats_free(oride.server_session_sharing_match_str);
  ats_free(oride.proxy_response_server_string);
  ats_free(oride.global_user_agent_header);
  ats_free(oride.ssl_client_cert_filename);
  ats_free(oride.ssl_client_private_key_filename);
  ats_free(oride.ssl_client_ca_cert_filename);
  ats_free(connect_ports_string);
  ats_free(reverse_proxy_no_host_redirect);
  ats_free(redirect_actions_string);
  ats_free(oride.ssl_client_sni_policy);
  ats_free(oride.ssl_client_alpn_protocols);
  ats_free(oride.host_res_data.conf_value);

  delete connect_ports;
  delete redirect_actions_map;
}
