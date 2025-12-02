/** @file

  Definitions via the X Macro pattern for overridable configuration variables.

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

/*
  This file provides a single source of truth for all overridable configuration
  variables. By defining configs here once, we can auto-generate:
  - The Lua plugin enum and variable array (ts_lua_http_config.cc)
  - The overridable_txn_vars.cc string-to-enum mapping
  - The _conf_to_memberp switch statement (InkAPI.cc)
  - The SDK_Overridable_Configs test array (InkAPITest.cc)

  @section adding_new_config Adding a New Overridable Config

  To make a configuration variable overridable, follow these steps:

  1. Add the config member to OverridableHttpConfigParams in HttpConfig.h
     (move it from HttpConfigParams if it already exists there as a
     non-overridable config).

  2. Add an entry to the OVERRIDABLE_CONFIGS macro below with:
     - CONFIG_KEY: The enum suffix (becomes TS_CONFIG_<CONFIG_KEY>).
     - MEMBER: The struct member name in OverridableHttpConfigParams.
     - RECORD_NAME: The proxy.config.* record name string.
     - DATA_TYPE: INT, FLOAT, or STRING.
     - CONV: GENERIC (for standard types), a custom converter name, or NONE.

  Note on CONV=NONE: Some SSL string configs use NONE because they bypass
  _conf_to_memberp and are handled directly in TSHttpTxnConfigStringSet().
  For NONE entries, the MEMBER field is ignored but must still be provided
  to satisfy the macro format.

  3. Add the enum value to TSOverridableConfigKey in apidefs.h.in
     (must be added at the end just before TS_CONFIG_LAST_ENTRY to preserve
     ABI).
     IMPORTANT: The X-macro order MUST match the enum order.
     overridable_txn_vars.cc contains compile-time validation that the order
     matches.

  4. Update documentation in doc/developer-guide/api/
*/

#pragma once

// clang-format off
#define OVERRIDABLE_CONFIGS(X) \
  X(URL_REMAP_PRISTINE_HOST_HDR,                    maintain_pristine_host_hdr,                 "proxy.config.url_remap.pristine_host_hdr",                       INT,    GENERIC) \
  X(HTTP_CHUNKING_ENABLED,                          chunking_enabled,                           "proxy.config.http.chunking_enabled",                             INT,    GENERIC) \
  X(HTTP_NEGATIVE_CACHING_ENABLED,                  negative_caching_enabled,                   "proxy.config.http.negative_caching_enabled",                     INT,    GENERIC) \
  X(HTTP_NEGATIVE_CACHING_LIFETIME,                 negative_caching_lifetime,                  "proxy.config.http.negative_caching_lifetime",                    INT,    GENERIC) \
  X(HTTP_CACHE_WHEN_TO_REVALIDATE,                  cache_when_to_revalidate,                   "proxy.config.http.cache.when_to_revalidate",                     INT,    GENERIC) \
  X(HTTP_KEEP_ALIVE_ENABLED_IN,                     keep_alive_enabled_in,                      "proxy.config.http.keep_alive_enabled_in",                        INT,    GENERIC) \
  X(HTTP_KEEP_ALIVE_ENABLED_OUT,                    keep_alive_enabled_out,                     "proxy.config.http.keep_alive_enabled_out",                       INT,    GENERIC) \
  X(HTTP_KEEP_ALIVE_POST_OUT,                       keep_alive_post_out,                        "proxy.config.http.keep_alive_post_out",                          INT,    GENERIC) \
  X(HTTP_SERVER_SESSION_SHARING_MATCH,              server_session_sharing_match,               "proxy.config.http.server_session_sharing.match",                 STRING, GENERIC) \
  X(NET_SOCK_RECV_BUFFER_SIZE_OUT,                  sock_recv_buffer_size_out,                  "proxy.config.net.sock_recv_buffer_size_out",                     INT,    GENERIC) \
  X(NET_SOCK_SEND_BUFFER_SIZE_OUT,                  sock_send_buffer_size_out,                  "proxy.config.net.sock_send_buffer_size_out",                     INT,    GENERIC) \
  X(NET_SOCK_OPTION_FLAG_OUT,                       sock_option_flag_out,                       "proxy.config.net.sock_option_flag_out",                          INT,    GENERIC) \
  X(HTTP_FORWARD_PROXY_AUTH_TO_PARENT,              fwd_proxy_auth_to_parent,                   "proxy.config.http.forward.proxy_auth_to_parent",                 INT,    GENERIC) \
  X(HTTP_ANONYMIZE_REMOVE_FROM,                     anonymize_remove_from,                      "proxy.config.http.anonymize_remove_from",                        INT,    GENERIC) \
  X(HTTP_ANONYMIZE_REMOVE_REFERER,                  anonymize_remove_referer,                   "proxy.config.http.anonymize_remove_referer",                     INT,    GENERIC) \
  X(HTTP_ANONYMIZE_REMOVE_USER_AGENT,               anonymize_remove_user_agent,                "proxy.config.http.anonymize_remove_user_agent",                  INT,    GENERIC) \
  X(HTTP_ANONYMIZE_REMOVE_COOKIE,                   anonymize_remove_cookie,                    "proxy.config.http.anonymize_remove_cookie",                      INT,    GENERIC) \
  X(HTTP_ANONYMIZE_REMOVE_CLIENT_IP,                anonymize_remove_client_ip,                 "proxy.config.http.anonymize_remove_client_ip",                   INT,    GENERIC) \
  X(HTTP_ANONYMIZE_INSERT_CLIENT_IP,                anonymize_insert_client_ip,                 "proxy.config.http.insert_client_ip",                             INT,    GENERIC) \
  X(HTTP_RESPONSE_SERVER_ENABLED,                   proxy_response_server_enabled,              "proxy.config.http.response_server_enabled",                      INT,    GENERIC) \
  X(HTTP_INSERT_SQUID_X_FORWARDED_FOR,              insert_squid_x_forwarded_for,               "proxy.config.http.insert_squid_x_forwarded_for",                 INT,    GENERIC) \
  X(HTTP_SEND_HTTP11_REQUESTS,                      send_http11_requests,                       "proxy.config.http.send_http11_requests",                         INT,    GENERIC) \
  X(HTTP_CACHE_HTTP,                                cache_http,                                 "proxy.config.http.cache.http",                                   INT,    GENERIC) \
  X(HTTP_CACHE_IGNORE_CLIENT_NO_CACHE,              cache_ignore_client_no_cache,               "proxy.config.http.cache.ignore_client_no_cache",                 INT,    GENERIC) \
  X(HTTP_CACHE_IGNORE_CLIENT_CC_MAX_AGE,            cache_ignore_client_cc_max_age,             "proxy.config.http.cache.ignore_client_cc_max_age",               INT,    GENERIC) \
  X(HTTP_CACHE_IMS_ON_CLIENT_NO_CACHE,              cache_ims_on_client_no_cache,               "proxy.config.http.cache.ims_on_client_no_cache",                 INT,    GENERIC) \
  X(HTTP_CACHE_IGNORE_SERVER_NO_CACHE,              cache_ignore_server_no_cache,               "proxy.config.http.cache.ignore_server_no_cache",                 INT,    GENERIC) \
  X(HTTP_CACHE_CACHE_RESPONSES_TO_COOKIES,          cache_responses_to_cookies,                 "proxy.config.http.cache.cache_responses_to_cookies",             INT,    GENERIC) \
  X(HTTP_CACHE_IGNORE_AUTHENTICATION,               cache_ignore_auth,                          "proxy.config.http.cache.ignore_authentication",                  INT,    GENERIC) \
  X(HTTP_CACHE_CACHE_URLS_THAT_LOOK_DYNAMIC,        cache_urls_that_look_dynamic,               "proxy.config.http.cache.cache_urls_that_look_dynamic",           INT,    GENERIC) \
  X(HTTP_CACHE_REQUIRED_HEADERS,                    cache_required_headers,                     "proxy.config.http.cache.required_headers",                       INT,    GENERIC) \
  X(HTTP_INSERT_REQUEST_VIA_STR,                    insert_request_via_string,                  "proxy.config.http.insert_request_via_str",                       INT,    GENERIC) \
  X(HTTP_INSERT_RESPONSE_VIA_STR,                   insert_response_via_string,                 "proxy.config.http.insert_response_via_str",                      INT,    GENERIC) \
  X(HTTP_CACHE_HEURISTIC_MIN_LIFETIME,              cache_heuristic_min_lifetime,               "proxy.config.http.cache.heuristic_min_lifetime",                 INT,    GENERIC) \
  X(HTTP_CACHE_HEURISTIC_MAX_LIFETIME,              cache_heuristic_max_lifetime,               "proxy.config.http.cache.heuristic_max_lifetime",                 INT,    GENERIC) \
  X(HTTP_CACHE_GUARANTEED_MIN_LIFETIME,             cache_guaranteed_min_lifetime,              "proxy.config.http.cache.guaranteed_min_lifetime",                INT,    GENERIC) \
  X(HTTP_CACHE_GUARANTEED_MAX_LIFETIME,             cache_guaranteed_max_lifetime,              "proxy.config.http.cache.guaranteed_max_lifetime",                INT,    GENERIC) \
  X(HTTP_CACHE_MAX_STALE_AGE,                       cache_max_stale_age,                        "proxy.config.http.cache.max_stale_age",                          INT,    GENERIC) \
  X(HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_IN,         keep_alive_no_activity_timeout_in,          "proxy.config.http.keep_alive_no_activity_timeout_in",            INT,    GENERIC) \
  X(HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_OUT,        keep_alive_no_activity_timeout_out,         "proxy.config.http.keep_alive_no_activity_timeout_out",           INT,    GENERIC) \
  X(HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_IN,        transaction_no_activity_timeout_in,         "proxy.config.http.transaction_no_activity_timeout_in",           INT,    GENERIC) \
  X(HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_OUT,       transaction_no_activity_timeout_out,        "proxy.config.http.transaction_no_activity_timeout_out",          INT,    GENERIC) \
  X(HTTP_TRANSACTION_ACTIVE_TIMEOUT_OUT,            transaction_active_timeout_out,             "proxy.config.http.transaction_active_timeout_out",               INT,    GENERIC) \
  X(HTTP_CONNECT_ATTEMPTS_MAX_RETRIES,              connect_attempts_max_retries,               "proxy.config.http.connect_attempts_max_retries",                 INT,    GENERIC) \
  X(HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DOWN_SERVER,  connect_attempts_max_retries_down_server,   "proxy.config.http.connect_attempts_max_retries_down_server",     INT,    GENERIC) \
  X(HTTP_CONNECT_ATTEMPTS_RR_RETRIES,               connect_attempts_rr_retries,                "proxy.config.http.connect_attempts_rr_retries",                  INT,    GENERIC) \
  X(HTTP_CONNECT_ATTEMPTS_TIMEOUT,                  connect_attempts_timeout,                   "proxy.config.http.connect_attempts_timeout",                     INT,    GENERIC) \
  X(HTTP_DOWN_SERVER_CACHE_TIME,                    down_server_timeout,                        "proxy.config.http.down_server.cache_time",                       INT,    HttpDownServerCacheTimeConv) \
  X(HTTP_DOC_IN_CACHE_SKIP_DNS,                     doc_in_cache_skip_dns,                      "proxy.config.http.doc_in_cache_skip_dns",                        INT,    GENERIC) \
  X(HTTP_BACKGROUND_FILL_ACTIVE_TIMEOUT,            background_fill_active_timeout,             "proxy.config.http.background_fill_active_timeout",               INT,    GENERIC) \
  X(HTTP_RESPONSE_SERVER_STR,                       proxy_response_server_string,               "proxy.config.http.response_server_str",                          STRING, GENERIC) \
  X(HTTP_CACHE_HEURISTIC_LM_FACTOR,                 cache_heuristic_lm_factor,                  "proxy.config.http.cache.heuristic_lm_factor",                    FLOAT,  GENERIC) \
  X(HTTP_BACKGROUND_FILL_COMPLETED_THRESHOLD,       background_fill_threshold,                  "proxy.config.http.background_fill_completed_threshold",          FLOAT,  GENERIC) \
  X(NET_SOCK_PACKET_MARK_OUT,                       sock_packet_mark_out,                       "proxy.config.net.sock_packet_mark_out",                          INT,    GENERIC) \
  X(NET_SOCK_PACKET_TOS_OUT,                        sock_packet_tos_out,                        "proxy.config.net.sock_packet_tos_out",                           INT,    GENERIC) \
  X(HTTP_INSERT_AGE_IN_RESPONSE,                    insert_age_in_response,                     "proxy.config.http.insert_age_in_response",                       INT,    GENERIC) \
  X(HTTP_CHUNKING_SIZE,                             http_chunking_size,                         "proxy.config.http.chunking.size",                                INT,    GENERIC) \
  X(HTTP_FLOW_CONTROL_ENABLED,                      flow_control_enabled,                       "proxy.config.http.flow_control.enabled",                         INT,    GENERIC) \
  X(HTTP_FLOW_CONTROL_LOW_WATER_MARK,               flow_low_water_mark,                        "proxy.config.http.flow_control.low_water",                       INT,    GENERIC) \
  X(HTTP_FLOW_CONTROL_HIGH_WATER_MARK,              flow_high_water_mark,                       "proxy.config.http.flow_control.high_water",                      INT,    GENERIC) \
  X(HTTP_CACHE_RANGE_LOOKUP,                        cache_range_lookup,                         "proxy.config.http.cache.range.lookup",                           INT,    GENERIC) \
  X(HTTP_DEFAULT_BUFFER_SIZE,                       default_buffer_size_index,                  "proxy.config.http.default_buffer_size",                          INT,    GENERIC) \
  X(HTTP_DEFAULT_BUFFER_WATER_MARK,                 default_buffer_water_mark,                  "proxy.config.http.default_buffer_water_mark",                    INT,    GENERIC) \
  X(HTTP_REQUEST_HEADER_MAX_SIZE,                   request_hdr_max_size,                       "proxy.config.http.request_header_max_size",                      INT,    GENERIC) \
  X(HTTP_RESPONSE_HEADER_MAX_SIZE,                  response_hdr_max_size,                      "proxy.config.http.response_header_max_size",                     INT,    GENERIC) \
  X(HTTP_NEGATIVE_REVALIDATING_ENABLED,             negative_revalidating_enabled,              "proxy.config.http.negative_revalidating_enabled",                INT,    GENERIC) \
  X(HTTP_NEGATIVE_REVALIDATING_LIFETIME,            negative_revalidating_lifetime,             "proxy.config.http.negative_revalidating_lifetime",               INT,    GENERIC) \
  X(SSL_HSTS_MAX_AGE,                               proxy_response_hsts_max_age,                "proxy.config.ssl.hsts_max_age",                                  INT,    GENERIC) \
  X(SSL_HSTS_INCLUDE_SUBDOMAINS,                    proxy_response_hsts_include_subdomains,     "proxy.config.ssl.hsts_include_subdomains",                       INT,    GENERIC) \
  X(HTTP_CACHE_OPEN_READ_RETRY_TIME,                cache_open_read_retry_time,                 "proxy.config.http.cache.open_read_retry_time",                   INT,    GENERIC) \
  X(HTTP_CACHE_MAX_OPEN_READ_RETRIES,               max_cache_open_read_retries,                "proxy.config.http.cache.max_open_read_retries",                  INT,    GENERIC) \
  X(HTTP_CACHE_RANGE_WRITE,                         cache_range_write,                          "proxy.config.http.cache.range.write",                            INT,    GENERIC) \
  X(HTTP_POST_CHECK_CONTENT_LENGTH_ENABLED,         post_check_content_length_enabled,          "proxy.config.http.post.check.content_length.enabled",            INT,    GENERIC) \
  X(HTTP_GLOBAL_USER_AGENT_HEADER,                  global_user_agent_header,                   "proxy.config.http.global_user_agent_header",                     STRING, GENERIC) \
  X(HTTP_AUTH_SERVER_SESSION_PRIVATE,               auth_server_session_private,                "proxy.config.http.auth_server_session_private",                  INT,    GENERIC) \
  X(HTTP_SLOW_LOG_THRESHOLD,                        slow_log_threshold,                         "proxy.config.http.slow.log.threshold",                           INT,    GENERIC) \
  X(HTTP_CACHE_GENERATION,                          cache_generation_number,                    "proxy.config.http.cache.generation",                             INT,    GENERIC) \
  X(BODY_FACTORY_TEMPLATE_BASE,                     body_factory_template_base,                 "proxy.config.body_factory.template_base",                        STRING, GENERIC) \
  X(HTTP_CACHE_OPEN_WRITE_FAIL_ACTION,              cache_open_write_fail_action,               "proxy.config.http.cache.open_write_fail_action",                 INT,    GENERIC) \
  X(HTTP_NUMBER_OF_REDIRECTIONS,                    number_of_redirections,                     "proxy.config.http.number_of_redirections",                       INT,    GENERIC) \
  X(HTTP_CACHE_MAX_OPEN_WRITE_RETRIES,              max_cache_open_write_retries,               "proxy.config.http.cache.max_open_write_retries",                 INT,    GENERIC) \
  X(HTTP_CACHE_MAX_OPEN_WRITE_RETRY_TIMEOUT,        max_cache_open_write_retry_timeout,         "proxy.config.http.cache.max_open_write_retry_timeout",           INT,    GENERIC) \
  X(HTTP_REDIRECT_USE_ORIG_CACHE_KEY,               redirect_use_orig_cache_key,                "proxy.config.http.redirect_use_orig_cache_key",                  INT,    GENERIC) \
  X(HTTP_ATTACH_SERVER_SESSION_TO_CLIENT,           attach_server_session_to_client,            "proxy.config.http.attach_server_session_to_client",              INT,    GENERIC) \
  X(WEBSOCKET_NO_ACTIVITY_TIMEOUT,                  websocket_inactive_timeout,                 "proxy.config.websocket.no_activity_timeout",                     INT,    GENERIC) \
  X(WEBSOCKET_ACTIVE_TIMEOUT,                       websocket_active_timeout,                   "proxy.config.websocket.active_timeout",                          INT,    GENERIC) \
  X(HTTP_UNCACHEABLE_REQUESTS_BYPASS_PARENT,        uncacheable_requests_bypass_parent,         "proxy.config.http.uncacheable_requests_bypass_parent",           INT,    GENERIC) \
  X(HTTP_PARENT_PROXY_TOTAL_CONNECT_ATTEMPTS,       parent_connect_attempts,                    "proxy.config.http.parent_proxy.total_connect_attempts",          INT,    GENERIC) \
  X(HTTP_TRANSACTION_ACTIVE_TIMEOUT_IN,             transaction_active_timeout_in,              "proxy.config.http.transaction_active_timeout_in",                INT,    GENERIC) \
  X(SRV_ENABLED,                                    srv_enabled,                                "proxy.config.srv_enabled",                                       INT,    GENERIC) \
  X(HTTP_FORWARD_CONNECT_METHOD,                    forward_connect_method,                     "proxy.config.http.forward_connect_method",                       INT,    GENERIC) \
  X(SSL_CERT_FILENAME,                              ssl_client_cert_filename,                   "proxy.config.ssl.client.cert.filename",                          STRING, NONE) \
  X(SSL_CERT_FILEPATH,                              ssl_client_cert_filename,                   "proxy.config.ssl.client.cert.path",                              STRING, NONE) \
  X(PARENT_FAILURES_UPDATE_HOSTDB,                  parent_failures_update_hostdb,              "proxy.config.http.parent_proxy.mark_down_hostdb",                INT,    GENERIC) \
  X(HTTP_CACHE_IGNORE_ACCEPT_MISMATCH,              ignore_accept_mismatch,                     "proxy.config.http.cache.ignore_accept_mismatch",                 INT,    GENERIC) \
  X(HTTP_CACHE_IGNORE_ACCEPT_LANGUAGE_MISMATCH,     ignore_accept_language_mismatch,            "proxy.config.http.cache.ignore_accept_language_mismatch",        INT,    GENERIC) \
  X(HTTP_CACHE_IGNORE_ACCEPT_ENCODING_MISMATCH,     ignore_accept_encoding_mismatch,            "proxy.config.http.cache.ignore_accept_encoding_mismatch",        INT,    GENERIC) \
  X(HTTP_CACHE_IGNORE_ACCEPT_CHARSET_MISMATCH,      ignore_accept_charset_mismatch,             "proxy.config.http.cache.ignore_accept_charset_mismatch",         INT,    GENERIC) \
  X(HTTP_PARENT_PROXY_FAIL_THRESHOLD,               parent_fail_threshold,                      "proxy.config.http.parent_proxy.fail_threshold",                  INT,    GENERIC) \
  X(HTTP_PARENT_PROXY_RETRY_TIME,                   parent_retry_time,                          "proxy.config.http.parent_proxy.retry_time",                      INT,    GENERIC) \
  X(HTTP_PER_PARENT_CONNECT_ATTEMPTS,               per_parent_connect_attempts,                "proxy.config.http.parent_proxy.per_parent_connect_attempts",     INT,    GENERIC) \
  X(HTTP_NORMALIZE_AE,                              normalize_ae,                               "proxy.config.http.normalize_ae",                                 INT,    GENERIC) \
  X(HTTP_INSERT_FORWARDED,                          insert_forwarded,                           "proxy.config.http.insert_forwarded",                             STRING, GENERIC) \
  X(HTTP_PROXY_PROTOCOL_OUT,                        proxy_protocol_out,                         "proxy.config.http.proxy_protocol_out",                           INT,    GENERIC) \
  X(HTTP_ALLOW_MULTI_RANGE,                         allow_multi_range,                          "proxy.config.http.allow_multi_range",                            INT,    GENERIC) \
  X(HTTP_REQUEST_BUFFER_ENABLED,                    request_buffer_enabled,                     "proxy.config.http.request_buffer_enabled",                       INT,    GENERIC) \
  X(HTTP_ALLOW_HALF_OPEN,                           allow_half_open,                            "proxy.config.http.allow_half_open",                              INT,    GENERIC) \
  X(HTTP_SERVER_MIN_KEEP_ALIVE_CONNS,               connection_tracker_config.server_min,       ConnectionTracker::CONFIG_SERVER_VAR_MIN,                         INT,    ConnectionTracker_MIN_SERVER_CONV) \
  X(HTTP_PER_SERVER_CONNECTION_MAX,                 connection_tracker_config.server_max,       ConnectionTracker::CONFIG_SERVER_VAR_MAX,                         INT,    ConnectionTracker_MAX_SERVER_CONV) \
  X(HTTP_PER_SERVER_CONNECTION_MATCH,               connection_tracker_config.server_match,     ConnectionTracker::CONFIG_SERVER_VAR_MATCH,                       INT,    ConnectionTracker_SERVER_MATCH_CONV) \
  X(SSL_CLIENT_VERIFY_SERVER_POLICY,                ssl_client_verify_server_policy,            "proxy.config.ssl.client.verify.server.policy",                   STRING, NONE) \
  X(SSL_CLIENT_VERIFY_SERVER_PROPERTIES,            ssl_client_verify_server_properties,        "proxy.config.ssl.client.verify.server.properties",               STRING, NONE) \
  X(SSL_CLIENT_SNI_POLICY,                          ssl_client_sni_policy,                      "proxy.config.ssl.client.sni_policy",                             STRING, NONE) \
  X(SSL_CLIENT_PRIVATE_KEY_FILENAME,                ssl_client_private_key_filename,            "proxy.config.ssl.client.private_key.filename",                   STRING, NONE) \
  X(SSL_CLIENT_CA_CERT_FILENAME,                    ssl_client_ca_cert_filename,                "proxy.config.ssl.client.CA.cert.filename",                       STRING, NONE) \
  X(SSL_CLIENT_ALPN_PROTOCOLS,                      ssl_client_alpn_protocols,                  "proxy.config.ssl.client.alpn_protocols",                         STRING, NONE) \
  X(HTTP_HOST_RESOLUTION_PREFERENCE,                host_res_data,                              "proxy.config.hostdb.ip_resolve",                                 STRING, HttpTransact_HOST_RES_CONV) \
  X(HTTP_CONNECT_DOWN_POLICY,                       connect_down_policy,                        "proxy.config.http.connect.down.policy",                          INT,    GENERIC) \
  X(HTTP_MAX_PROXY_CYCLES,                          max_proxy_cycles,                           "proxy.config.http.max_proxy_cycles",                             INT,    GENERIC) \
  X(PLUGIN_VC_DEFAULT_BUFFER_INDEX,                 plugin_vc_default_buffer_index,             "proxy.config.plugin.vc.default_buffer_index",                    INT,    GENERIC) \
  X(PLUGIN_VC_DEFAULT_BUFFER_WATER_MARK,            plugin_vc_default_buffer_water_mark,        "proxy.config.plugin.vc.default_buffer_water_mark",               INT,    GENERIC) \
  X(NET_SOCK_NOTSENT_LOWAT,                         sock_packet_notsent_lowat,                  "proxy.config.net.sock_notsent_lowat",                            INT,    GENERIC) \
  X(BODY_FACTORY_RESPONSE_SUPPRESSION_MODE,         response_suppression_mode,                  "proxy.config.body_factory.response_suppression_mode",            INT,    GENERIC) \
  X(HTTP_ENABLE_PARENT_TIMEOUT_MARKDOWNS,           enable_parent_timeout_markdowns,            "proxy.config.http.parent_proxy.enable_parent_timeout_markdowns", INT,    GENERIC) \
  X(HTTP_DISABLE_PARENT_MARKDOWNS,                  disable_parent_markdowns,                   "proxy.config.http.parent_proxy.disable_parent_markdowns",        INT,    GENERIC) \
  X(NET_DEFAULT_INACTIVITY_TIMEOUT,                 default_inactivity_timeout,                 "proxy.config.net.default_inactivity_timeout",                    INT,    GENERIC) \
  X(HTTP_NO_DNS_JUST_FORWARD_TO_PARENT,             no_dns_forward_to_parent,                   "proxy.config.http.no_dns_just_forward_to_parent",                INT,    GENERIC) \
  X(HTTP_CACHE_IGNORE_QUERY,                        cache_ignore_query,                         "proxy.config.http.cache.ignore_query",                           INT,    GENERIC) \
  X(HTTP_DROP_CHUNKED_TRAILERS,                     http_drop_chunked_trailers,                 "proxy.config.http.drop_chunked_trailers",                        INT,    GENERIC) \
  X(HTTP_STRICT_CHUNK_PARSING,                      http_strict_chunk_parsing,                  "proxy.config.http.strict_chunk_parsing",                         INT,    GENERIC) \
  X(HTTP_NEGATIVE_CACHING_LIST,                     negative_caching_list,                      "proxy.config.http.negative_caching_list",                        STRING, HttpStatusCodeList_Conv) \
  X(HTTP_CONNECT_ATTEMPTS_RETRY_BACKOFF_BASE,       connect_attempts_retry_backoff_base,        "proxy.config.http.connect_attempts_retry_backoff_base",          INT,    GENERIC) \
  X(HTTP_NEGATIVE_REVALIDATING_LIST,                negative_revalidating_list,                 "proxy.config.http.negative_revalidating_list",                   STRING, HttpStatusCodeList_Conv) \
  X(HTTP_CACHE_POST_METHOD,                         cache_post_method,                          "proxy.config.http.cache.post_method",                            INT,    GENERIC)

// clang-format on
