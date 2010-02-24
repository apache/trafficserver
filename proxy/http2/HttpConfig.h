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
#ifndef _HttpConfig_h_
#define _HttpConfig_h_

#include <stdlib.h>
#include <stdio.h>

#ifdef HAVE_REGEX_H
#if (HOST_OS == solaris)
#include <regex.h>
#else
#include "/usr/include/regex.h"
#endif
#endif

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#include "inktomi++.h"
#include "ProxyConfig.h"

#undef MgmtInt
#undef MgmtFloat
#define MgmtInt RecInt
#define MgmtFloat RecFloat

#include "HttpAssert.h"


#include "P_RecProcess.h"

/* Instead of enumerating the stats in DynamicStats.h, each module needs
   to enumerate its stats separately and register them with librecords
   */
enum
{
  http_background_fill_current_count_stat,
  http_current_client_connections_stat,
  http_current_active_client_connections_stat,
  http_current_client_transactions_stat,
  http_total_incoming_connections_stat,
  http_current_parent_proxy_transactions_stat,
  http_current_icp_transactions_stat,
  http_current_server_transactions_stat,
  http_current_parent_proxy_raw_transactions_stat,
  http_current_icp_raw_transactions_stat,
  http_current_server_raw_transactions_stat,

  //  Http Abort information (from HttpNetConnection)
  http_ua_msecs_counts_errors_pre_accept_hangups_stat,
  http_ua_msecs_counts_errors_empty_hangups_stat,
  http_ua_msecs_counts_errors_early_hangups_stat,

  // Http Total Connections Stats
  http_total_client_connections_stat,
  http_total_server_connections_stat,
  http_total_parent_proxy_connections_stat,
  http_current_parent_proxy_connections_stat,
  http_current_server_connections_stat,
  http_current_cache_connections_stat,

  // Http K-A Stats
  http_transactions_per_client_con,
  http_transactions_per_server_con,
  http_transactions_per_parent_con,

  // Http Time Stuff
  http_client_connection_time_stat,
  http_parent_proxy_connection_time_stat,
  http_server_connection_time_stat,
  http_cache_connection_time_stat,


  // Transactional stats (originaly in proxy/HttpTransStats.h)
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

  http_client_no_cache_requests_stat,

  //    http_icp_requests_stat,
  //    http_icp_invalid_responses_stat,
  //    http_parent_proxy_requests_stat,
  //    http_parent_invalid_responses_stat,
  //    http_invalid_responses_stat,
  //    http_retried_requests_stat,

  http_broken_server_connections_stat,

  http_cache_lookups_stat,
  http_cache_misses_stat,
  http_cache_writes_stat,
  http_cache_updates_stat,
  http_cache_deletes_stat,

  http_tunnels_stat,
  http_throttled_proxy_only_stat,

  // HTTP requests classified by IMS/no-cache/MSIE
  http_request_taxonomy_i0_n0_m0_stat,
  http_request_taxonomy_i1_n0_m0_stat,
  http_request_taxonomy_i0_n1_m0_stat,
  http_request_taxonomy_i1_n1_m0_stat,
  http_request_taxonomy_i0_n0_m1_stat,
  http_request_taxonomy_i1_n0_m1_stat,
  http_request_taxonomy_i0_n1_m1_stat,
  http_request_taxonomy_i1_n1_m1_stat,
  http_icp_suggested_lookups_stat,

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
  http_ua_msecs_counts_hit_ims_stat,
  http_ua_msecs_counts_hit_stale_served_stat,
  http_ua_msecs_counts_miss_cold_stat,
  http_ua_msecs_counts_miss_changed_stat,
  http_ua_msecs_counts_miss_client_no_cache_stat,
  http_ua_msecs_counts_miss_uncacheable_stat,
  http_ua_msecs_counts_miss_ims_stat,
  http_ua_msecs_counts_errors_aborts_stat,
  http_ua_msecs_counts_errors_possible_aborts_stat,
  http_ua_msecs_counts_errors_connect_failed_stat,
  http_ua_msecs_counts_errors_other_stat,
  http_ua_msecs_counts_other_unclassified_stat,

  http_total_x_redirect_stat,

  // Times
  http_total_transactions_time_stat,
  http_total_transactions_think_time_stat,

  http_client_transaction_time_stat,

  http_client_write_time_stat,
  http_server_read_time_stat,

  http_icp_transaction_time_stat,
  http_icp_raw_transaction_time_stat,
  http_parent_proxy_transaction_time_stat,
  http_parent_proxy_raw_transaction_time_stat,
  http_server_transaction_time_stat,
  http_server_raw_transaction_time_stat,

  // Ftp stats

  ftp_cache_lookups_stat,
  ftp_cache_hits_stat,
  ftp_cache_misses_stat,

  // Http cache errors
  http_cache_write_errors,
  http_cache_read_errors,

  // jg specific stats
  http_jg_cache_hits_stat,
  http_jg_cache_misses_stat,
  http_jg_client_aborts_stat,
  http_jg_cache_hit_time_stat,
  http_jg_cache_miss_time_stat,


  http_stat_count
};

extern RecRawStatBlock *http_rsb;

/* Stats should only be accessed using these macros */

#define HTTP_SET_DYN_STAT(x,C, S) \
do { \
        RecSetRawStatSum(http_rsb, x, S); \
        RecSetRawStatCount(http_rsb, x, C); \
} while (0);
#define HTTP_INCREMENT_DYN_STAT(x) \
        RecIncrRawStat(http_rsb, mutex->thread_holding, (int) x, 1);
#define HTTP_DECREMENT_DYN_STAT(x) \
        RecIncrRawStat(http_rsb, mutex->thread_holding, (int) x, -1);
#define HTTP_SUM_DYN_STAT(x, y) \
        RecIncrRawStat(http_rsb, mutex->thread_holding, (int) x, (int) y);
#define HTTP_SUM_GLOBAL_DYN_STAT(x, y) \
        RecIncrGlobalRawStatSum(http_rsb,x,y)
#define HTTP_CLEAR_DYN_STAT(x) \
do { \
        RecSetRawStatSum(http_rsb, x, 0); \
        RecSetRawStatCount(http_rsb, x, 0); \
} while (0);
#define HTTP_READ_DYN_STAT(x, C, S)             \
  RecGetRawStatCount(http_rsb, (int) x, &C);    \
  RecGetRawStatSum(http_rsb, (int) x, &S);

#define HTTP_READ_DYN_SUM(x, S)             \
  RecGetRawStatSum(http_rsb, (int) x, &S);

#define HTTP_ConfigReadInteger         REC_ConfigReadInteger
#define HTTP_ConfigReadString          REC_ConfigReadString
#define HTTP_RegisterConfigUpdateFunc  REC_RegisterConfigUpdateFunc


class ostream;

/////////////////////////////////////////////////////////////
//
// struct HttpConfigSSLPortRange
//
// configuration parameters for a range of valid SSL ports
// if "low" == "high" a single port is part of this range
// if "low" == "high" == -1 any port number is allowed
//   (corresponds to a "*" in the config file)
/////////////////////////////////////////////////////////////
struct HttpConfigSSLPortRange
{
  int low;
  int high;
  HttpConfigSSLPortRange *next;

    HttpConfigSSLPortRange()
  : low(0), high(0), next(0)
  {
  }
   ~HttpConfigSSLPortRange()
  {
    if (next)
      delete next;
  }
};

/////////////////////////////////////////////////////////////
//
// struct HttpConfigParams
//
// configuration parameters as they apear in the global
// configuration file.
/////////////////////////////////////////////////////////////
struct HttpConfigParams:public ConfigInfo
{
public:
  HttpConfigParams();
  ~HttpConfigParams();

  enum
  {
    CACHE_REQUIRED_HEADERS_NONE = 0,
    CACHE_REQUIRED_HEADERS_AT_LEAST_LAST_MODIFIED = 1,
    CACHE_REQUIRED_HEADERS_CACHE_CONTROL = 2
  };

  enum
  {
    SEND_HTTP11_NEVER = 0,
    SEND_HTTP11_ALWAYS = 1,
    SEND_HTTP11_UPGRADE_HOSTDB = 2,
    SEND_HTTP11_IF_REQUEST_11_AND_HOSTDB = 3
  };

public:
  char *proxy_hostname;
  int proxy_hostname_len;

  char *incoming_ip_to_bind;
  unsigned int incoming_ip_to_bind_saddr;

  char *outgoing_ip_to_bind;
  unsigned int outgoing_ip_to_bind_saddr;

  MgmtInt server_max_connections;
  MgmtInt origin_max_connections;
  MgmtInt origin_min_keep_alive_connections;

  MgmtInt parent_proxy_routing_enable;
  MgmtInt disable_ssl_parenting;

  MgmtInt enable_url_expandomatic;
  MgmtInt no_dns_forward_to_parent;
  MgmtInt uncacheable_requests_bypass_parent;
  MgmtInt no_origin_server_dns;
  MgmtInt maintain_pristine_host_hdr;

  MgmtInt snarf_username_from_authorization;

  MgmtInt insert_request_via_string;
  MgmtInt insert_response_via_string;
  MgmtInt verbose_via_string;

  char *proxy_request_via_string;
  int proxy_request_via_string_len;
  char *proxy_response_via_string;
  int proxy_response_via_string_len;

  /////////////
  // schemes //
  /////////////
  MgmtInt ftp_enabled;
  //////////////////
  // WUTS headers //
  //////////////////
  MgmtInt wuts_enabled;
  MgmtInt log_spider_codes;

  ///////////////////////////////////
  // URL expansions for DNS lookup //
  ///////////////////////////////////
  char *url_expansions_string;
  char **url_expansions;
  int num_url_expansions;

  ///////////////////////////////////////////////////
  // connection variables. timeouts are in seconds //
  ///////////////////////////////////////////////////
  MgmtInt proxy_server_port;
  char *proxy_server_other_ports;
  MgmtInt keep_alive_enabled;
  MgmtInt chunking_enabled;
  MgmtInt session_auth_cache_keep_alive_enabled;
  MgmtInt origin_server_pipeline;
  MgmtInt user_agent_pipeline;
  MgmtInt share_server_sessions;
  MgmtInt keep_alive_post_out;  // share server sessions for post
  MgmtInt keep_alive_no_activity_timeout_in;
  MgmtInt keep_alive_no_activity_timeout_out;
  MgmtInt transaction_no_activity_timeout_in;
  MgmtInt transaction_no_activity_timeout_out;
  MgmtInt transaction_active_timeout_in;
  MgmtInt transaction_active_timeout_out;
  MgmtInt accept_no_activity_timeout;
  MgmtInt background_fill_active_timeout;
  MgmtFloat background_fill_threshold;

  ////////////////////////////////////
  // origin server connect attempts //
  ////////////////////////////////////
  MgmtInt connect_attempts_max_retries;
  MgmtInt connect_attempts_max_retries_dead_server;
  MgmtInt connect_attempts_rr_retries;
  MgmtInt connect_attempts_timeout;
  MgmtInt streaming_connect_attempts_timeout;
  MgmtInt post_connect_attempts_timeout;
  MgmtInt parent_connect_attempts;
  MgmtInt per_parent_connect_attempts;
  MgmtInt parent_connect_timeout;

  ///////////////////////////////////////
  // origin server connection settings //
  ///////////////////////////////////////
  MgmtInt sock_recv_buffer_size_out;
  MgmtInt sock_send_buffer_size_out;
  MgmtInt sock_option_flag_out;

  ///////////////////////////////////////////////////////////////////
  // Privacy: fields which are removed from the user agent request //
  ///////////////////////////////////////////////////////////////////
  MgmtInt anonymize_remove_from;
  MgmtInt anonymize_remove_referer;
  MgmtInt anonymize_remove_user_agent;
  MgmtInt anonymize_remove_cookie;
  MgmtInt anonymize_remove_client_ip;
  MgmtInt anonymize_insert_client_ip;
  MgmtInt append_xforwards_header;
  char *anonymize_other_header_list;
  bool anonymize_remove_any;

  ///////////////////////////////////////////////////////////////////
  // Global User Agent                                             //
  ///////////////////////////////////////////////////////////////////
  char *global_user_agent_header;
  size_t global_user_agent_header_size;

  ///////////////////////////////////////////////////////////////////
  // Global Server header                                          //
  ///////////////////////////////////////////////////////////////////
  char *proxy_response_server_string;
  size_t proxy_response_server_string_len;
  MgmtInt proxy_response_server_enabled;

  /////////////////////
  // X-Forwarded-For //
  /////////////////////
  MgmtInt insert_squid_x_forwarded_for;

  /////////////////////
  // Benchmark hacks //
  /////////////////////
  MgmtInt insert_age_in_response;       // INKqa09853
  MgmtInt avoid_content_spoofing;       // INKqa10426
  MgmtInt enable_http_stats;    // INKqa09879

  ///////////////////
  // ICP variables //
  ///////////////////
  MgmtInt icp_enabled;
  MgmtInt stale_icp_enabled;

  /////////////////////
  // cache variables //
  /////////////////////
  MgmtInt cache_heuristic_min_lifetime;
  MgmtInt cache_heuristic_max_lifetime;
  float cache_heuristic_lm_factor;

  MgmtInt cache_guaranteed_min_lifetime;
  MgmtInt cache_guaranteed_max_lifetime;

  MgmtInt cache_max_stale_age;

  MgmtInt freshness_fuzz_time;
  MgmtInt freshness_fuzz_min_time;
  float freshness_fuzz_prob;

  char *cache_vary_default_text;
  char *cache_vary_default_images;
  char *cache_vary_default_other;

  // open read failure retries.
  MgmtInt max_cache_open_read_retries;
  MgmtInt cache_open_read_retry_time;   // time is in mseconds

  // open write failure retries.
  MgmtInt max_cache_open_write_retries;
  MgmtInt cache_open_write_retry_time;  // time is in mseconds

  ///////////////////
  // cache control //
  ///////////////////
  MgmtInt cache_http;
  MgmtInt cache_ftp;
  MgmtInt cache_ignore_client_no_cache;
  MgmtInt cache_ignore_client_cc_max_age;
  MgmtInt cache_ims_on_client_no_cache;
  MgmtInt cache_ignore_server_no_cache;
  MgmtInt cache_responses_to_cookies;
  MgmtInt cache_ignore_auth;
  MgmtInt cache_urls_that_look_dynamic;
  MgmtInt cache_enable_default_vary_headers;
  MgmtInt cache_when_to_revalidate;
  MgmtInt cache_when_to_add_no_cache_to_msie_requests;
  MgmtInt cache_required_headers;
  MgmtInt cache_range_lookup;

  /////////
  // SSL //
  /////////
  char *ssl_ports_string;
  HttpConfigSSLPortRange *ssl_ports;

  ///////////////
  // Hdr Limit //
  ///////////////
  MgmtInt request_hdr_max_size;
  MgmtInt response_hdr_max_size;

  //////////
  // Push //
  //////////
  MgmtInt push_method_enabled;

  /////////
  // Ftp //
  /////////
  char *ftp_anonymous_passwd;
  MgmtInt cache_ftp_document_lifetime;
  MgmtInt ftp_binary_transfer_only;


  ////////////////////////////
  // HTTP Referer filtering //
  ////////////////////////////
  MgmtInt referer_filter_enabled;
  MgmtInt referer_format_redirect;

  ////////////////////////////////////////////////////////
  // HTTP Accept-Encoding filtering based on User-Agent //
  ////////////////////////////////////////////////////////
  MgmtInt accept_encoding_filter_enabled;

  //////////////////////////
  // HTTP Quick filtering //
  //////////////////////////
  MgmtInt quick_filter_mask;

  //////////////////
  // Transparency //
  //////////////////
  MgmtInt transparency_enabled;

  ///////////////////
  // reverse proxy //
  ///////////////////
  MgmtInt reverse_proxy_enabled;
  MgmtInt url_remap_required;
  char *reverse_proxy_no_host_redirect;
  int reverse_proxy_no_host_redirect_len;

  ////////////////////////
  //  Negative Caching  //
  ////////////////////////
  MgmtInt down_server_timeout;
  MgmtInt client_abort_threshold;

  ////////////////////////////
  //  Negative Revalidating //
  ////////////////////////////
  MgmtInt negative_revalidating_enabled;
  MgmtInt negative_revalidating_lifetime;

  ////////////////////////////////
  //  Negative Response Caching //
  ////////////////////////////////
  MgmtInt negative_caching_enabled;
  MgmtInt negative_caching_lifetime;

  /////////////////
  // Inktoswitch //
  /////////////////
  MgmtInt inktoswitch_enabled;
  MgmtInt router_ip;
  MgmtInt router_port;

  ///////////////////
  // cop access    //
  ///////////////////
  MgmtInt record_cop_page;

  ////////////////////////
  // record tcp_mem_hit //
  ////////////////////////
  MgmtInt record_tcp_mem_hit;

  //////////////////
  // Traffic Net  //
  //////////////////
  MgmtInt tn_frequency;
  MgmtInt tn_mode;
  char *tn_uid;
  char *tn_lid;
  char *tn_server;
  int tn_server_len;
  MgmtInt tn_port;
  char *tn_path;

  /////////////////////
  // Error Reporting //
  /////////////////////
  MgmtInt errors_log_error_pages;
  MgmtInt slow_log_threshold;

  //////////////////////
  //  Version Hell    //
  //////////////////////
  MgmtInt send_http11_requests;

  //////////////////////
  //  DOC IN CACHE NO DNS//
  //////////////////////
  MgmtInt doc_in_cache_skip_dns;

  MgmtInt default_buffer_size_index;
  MgmtInt default_buffer_water_mark;
  MgmtInt enable_http_info;

  //////////////////////
  //  Breaking Specs  //
  //  mostly for BofA //
  //////////////////////
  MgmtInt fwd_proxy_auth_to_parent;

  // Cluster time delta is not a config variable,
  //  rather it is the time skew which the manager observes
  ink32 cluster_time_delta;

  ///////////////////////////////////////////////////////////////////////////
  // Added by YTS Team, yamsat                                                  //
  //   Connection collapsing Configuration parameters                      //
  // 1. hashtable_enabled: if set to 1, requests will first search the     //
  //    hashtable to see if another similar request is already being served//
  // 2. rww_wait_time: read-while-write wait time: While read while write  //
  //    is enabled, the secondary clients will wait this amount of time    //
  //    after which cache lookup is retried                                //
  // 3. revaildate_window_period: while revaidation of a cached object is  //
  //    being done, the secondary clients for the same url will serve the  //
  //    stale object for this amount of time, after the revalidation had   //
  //    started                                                            //
  ///////////////////////////////////////////////////////////////////////////

  MgmtInt hashtable_enabled;
  MgmtInt rww_wait_time;
  MgmtInt revalidate_window_period;
  MgmtInt srv_enabled;          /* added by: ebalsa */

  //############################################################################## 
//#
  //# Redirection
  //#
  //# 1. redirection_enabled: if set to 1, redirection is enabled.
  //# 2. number_of_redirectionse: The maximum number of redirections YTS permits
  //# 3. post_copy_size: The maximum POST data size YTS permits to copy
  //#
  //##############################################################################

  MgmtInt redirection_enabled;
  MgmtInt number_of_redirections;
  MgmtInt post_copy_size;

  //////////////////////////////////////////////////////////////////
  // Allow special handling of Accept* headers to be disabled to  //
  // avoid unnecessary creation of alternates                     //
  //////////////////////////////////////////////////////////////////
  MgmtInt ignore_accept_mismatch;
  MgmtInt ignore_accept_language_mismatch;
  MgmtInt ignore_accept_encoding_mismatch;
  MgmtInt ignore_accept_charset_mismatch;

  ////////////////////////////////
  // Optimize gzip alternates   //
  ////////////////////////////////
  MgmtInt normalize_ae_gzip;

private:
  /////////////////////////////////////
  // operator = and copy constructor //
  /////////////////////////////////////
    HttpConfigParams(const HttpConfigParams &);
    HttpConfigParams & operator =(const HttpConfigParams &);
};

/////////////////////////////////////////////////////////////
//
// class HttpUserAgent_RegxEntry
//
// configuration entry for specific User-Agent
// Created at startup time only and never changed
// The main purpose of the User-Agent filtering is to find "bad" user agents
// and modify Accept-Encoding to prevent compression for such "bad" guys
/////////////////////////////////////////////////////////////

class HttpUserAgent_RegxEntry
{
public:
  typedef enum
  {                             // for more details, please see comments in "ae_ua.config" file
    STRTYPE_UNKNOWN = 0,
    STRTYPE_SUBSTR_CASE,        /* .substring, .string */
    STRTYPE_SUBSTR_NCASE,       /* .substring_ncase, .string_ncase */
    STRTYPE_REGEXP              /* .regexp POSIX regular expression */
  } StrType;

  HttpUserAgent_RegxEntry *next;
  int user_agent_str_size;
  char *user_agent_str;
  bool regx_valid;
  StrType stype;
  regex_t regx;

    HttpUserAgent_RegxEntry();
   ~HttpUserAgent_RegxEntry();

  bool create(char *refexp_str = NULL, char *errmsgbuf = NULL, int errmsgbuf_size = 0);
};

/////////////////////////////////////////////////////////////
//
// class HttpConfig
//
/////////////////////////////////////////////////////////////
class HttpConfig
{
public:

  static int init_aeua_filter(char *config_fname);

  static void startup();

  static void reconfigure();

  inkcoreapi static HttpConfigParams *acquire();
  inkcoreapi static void release(HttpConfigParams * params);

  // dump
  static void dump_config();

  // parse ssl ports configuration string
  static HttpConfigSSLPortRange *parse_ssl_ports(char *ssl_ports_str);

  // parse DNS URL expansions string
  static char **parse_url_expansions(char *url_expansions_str, int *num_expansions);

  static void *cluster_delta_cb(void *opaque_token, char *data_raw, int data_len);

public:
  static int m_id;
  static HttpConfigParams m_master;
  static HttpUserAgent_RegxEntry *user_agent_list;
};

// DI's request to disable ICP on the fly
extern volatile ink32 icp_dynamic_enabled;

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
//
//  inline functions
//
/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
inline
HttpConfigParams::HttpConfigParams()
  :
proxy_hostname(0),
proxy_hostname_len(0),
incoming_ip_to_bind(0),
incoming_ip_to_bind_saddr(0),
outgoing_ip_to_bind(0),
outgoing_ip_to_bind_saddr(0),
server_max_connections(0),
origin_max_connections(0),
origin_min_keep_alive_connections(0),
parent_proxy_routing_enable(false),
disable_ssl_parenting(0),
enable_url_expandomatic(0),
no_dns_forward_to_parent(0),
uncacheable_requests_bypass_parent(1),
no_origin_server_dns(0),
maintain_pristine_host_hdr(0),
//snarf_username_from_authorization(0),
insert_request_via_string(0),
insert_response_via_string(0),
verbose_via_string(0),
proxy_request_via_string(0),
proxy_request_via_string_len(0),
proxy_response_via_string(0),
proxy_response_via_string_len(0),
ftp_enabled(false),
wuts_enabled(false),
log_spider_codes(false),
url_expansions_string(0),
url_expansions(0),
num_url_expansions(0),
proxy_server_port(0),
proxy_server_other_ports(0),
keep_alive_enabled(0),
chunking_enabled(0),
session_auth_cache_keep_alive_enabled(0),
origin_server_pipeline(0),
user_agent_pipeline(0),
share_server_sessions(0),
keep_alive_post_out(0),
keep_alive_no_activity_timeout_in(0),
keep_alive_no_activity_timeout_out(0),
transaction_no_activity_timeout_in(0),
transaction_no_activity_timeout_out(0),
transaction_active_timeout_in(0),
transaction_active_timeout_out(0),
accept_no_activity_timeout(0),
background_fill_active_timeout(0),
background_fill_threshold(0.0),
connect_attempts_max_retries(0),
connect_attempts_max_retries_dead_server(0),
connect_attempts_rr_retries(0),
connect_attempts_timeout(0),
streaming_connect_attempts_timeout(0),
post_connect_attempts_timeout(0),
parent_connect_attempts(0),
per_parent_connect_attempts(0),
parent_connect_timeout(0),
sock_recv_buffer_size_out(0),
sock_send_buffer_size_out(0),
sock_option_flag_out(0),
anonymize_remove_from(false),
anonymize_remove_referer(false),
anonymize_remove_user_agent(false),
anonymize_remove_cookie(false),
anonymize_remove_client_ip(false),
anonymize_insert_client_ip(true),
append_xforwards_header(false),
anonymize_other_header_list(NULL),
anonymize_remove_any(false),
global_user_agent_header(NULL),
global_user_agent_header_size(0),
proxy_response_server_string(NULL),
proxy_response_server_string_len(0),
proxy_response_server_enabled(0),
insert_squid_x_forwarded_for(0),
insert_age_in_response(1),
avoid_content_spoofing(1),
enable_http_stats(1),
icp_enabled(0),
stale_icp_enabled(0),
cache_heuristic_min_lifetime(0),
cache_heuristic_max_lifetime(0),
cache_heuristic_lm_factor(0),
cache_guaranteed_min_lifetime(0),
cache_guaranteed_max_lifetime(0),
cache_max_stale_age(0),
freshness_fuzz_time(0),
freshness_fuzz_min_time(0),
freshness_fuzz_prob(0.0),
cache_vary_default_text(0),
cache_vary_default_images(0),
cache_vary_default_other(0),
max_cache_open_read_retries(0),
cache_open_read_retry_time(0),
max_cache_open_write_retries(0),
cache_open_write_retry_time(0),
cache_http(false),
cache_ftp(false),
cache_ignore_client_no_cache(false),
cache_ignore_client_cc_max_age(true),
cache_ims_on_client_no_cache(false),
cache_ignore_server_no_cache(false),
cache_responses_to_cookies(0),
cache_ignore_auth(0),
cache_urls_that_look_dynamic(false),
cache_enable_default_vary_headers(false),
cache_when_to_revalidate(0),
cache_when_to_add_no_cache_to_msie_requests(0),
cache_required_headers(CACHE_REQUIRED_HEADERS_NONE),
ssl_ports_string(0),
ssl_ports(0),
request_hdr_max_size(0),
response_hdr_max_size(0),
push_method_enabled(0),
ftp_anonymous_passwd(0),
cache_ftp_document_lifetime(0),
ftp_binary_transfer_only(0),
referer_filter_enabled(0),
referer_format_redirect(0),
accept_encoding_filter_enabled(0),
quick_filter_mask(0),
transparency_enabled(0),
down_server_timeout(0),
client_abort_threshold(0),
negative_revalidating_enabled(0),
negative_revalidating_lifetime(0),
negative_caching_enabled(0),
negative_caching_lifetime(0),
inktoswitch_enabled(0),
router_ip(0),
router_port(0),
record_cop_page(0),
record_tcp_mem_hit(0),
tn_frequency(0),
tn_mode(0),
tn_uid(0),
tn_lid(0),
tn_server(0),
tn_server_len(0),
tn_port(0),
tn_path(0),
errors_log_error_pages(0),
send_http11_requests(SEND_HTTP11_IF_REQUEST_11_AND_HOSTDB),
doc_in_cache_skip_dns(1),       // Added for SKIPPING DNS If DOC IN CACHE
default_buffer_size_index(0),
default_buffer_water_mark(0),
enable_http_info(0),
fwd_proxy_auth_to_parent(0),
cluster_time_delta(0),
        //Added by YTS Team, yamsat
hashtable_enabled(false),
rww_wait_time(0),
revalidate_window_period(0),
srv_enabled(0),
redirection_enabled(true),
number_of_redirections(0),
post_copy_size(2048),
ignore_accept_mismatch(0),
ignore_accept_language_mismatch(0),
ignore_accept_encoding_mismatch(0),
ignore_accept_charset_mismatch(0),
normalize_ae_gzip(1)
{
}

inline
HttpConfigParams::~
HttpConfigParams()
{
  xfree(proxy_hostname);
  xfree(proxy_request_via_string);
  xfree(proxy_response_via_string);
  xfree(url_expansions_string);
  xfree(proxy_server_other_ports);
  xfree(anonymize_other_header_list);
  xfree(global_user_agent_header);
  xfree(proxy_response_server_string);
  xfree(cache_vary_default_text);
  xfree(cache_vary_default_images);
  xfree(cache_vary_default_other);
  xfree(ssl_ports_string);
  xfree(ftp_anonymous_passwd);
  xfree(reverse_proxy_no_host_redirect);

  if (ssl_ports) {
    delete ssl_ports;
  }

  if (url_expansions) {
    xfree(url_expansions);
  }
}
#endif /* #ifndef _HttpConfig_h_ */
