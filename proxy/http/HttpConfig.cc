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

#include "tscore/ink_config.h"
#include <cctype>
#include <cstring>
#include "HttpConfig.h"
#include "HTTP.h"
#include "ProcessManager.h"
#include "ProxyConfig.h"
#include "P_Net.h"
#include "records/P_RecUtils.h"
#include <records/I_RecHttp.h>

#define HttpEstablishStaticConfigStringAlloc(_ix, _n) \
  REC_EstablishStaticConfigStringAlloc(_ix, _n);      \
  REC_RegisterConfigUpdateFunc(_n, http_config_cb, NULL)

#define HttpEstablishStaticConfigLongLong(_ix, _n) \
  REC_EstablishStaticConfigInteger(_ix, _n);       \
  REC_RegisterConfigUpdateFunc(_n, http_config_cb, NULL)

#define HttpEstablishStaticConfigFloat(_ix, _n) \
  REC_EstablishStaticConfigFloat(_ix, _n);      \
  REC_RegisterConfigUpdateFunc(_n, http_config_cb, NULL)

#define HttpEstablishStaticConfigByte(_ix, _n) \
  REC_EstablishStaticConfigByte(_ix, _n);      \
  REC_RegisterConfigUpdateFunc(_n, http_config_cb, NULL)

RecRawStatBlock *http_rsb;
#define HTTP_CLEAR_DYN_STAT(x)          \
  do {                                  \
    RecSetRawStatSum(http_rsb, x, 0);   \
    RecSetRawStatCount(http_rsb, x, 0); \
  } while (0);

class HttpConfigCont : public Continuation
{
public:
  HttpConfigCont();
  int handle_event(int event, void *edata);
};

/// Data item for enumerated type config value.
template <typename T> struct ConfigEnumPair {
  T _value;
  const char *_key;
};

/// Convert a string to an enumeration value.
/// @a n is the number of entries in the list.
/// @return @c true if the string is found, @c false if not found.
/// If found @a value is set to the corresponding value in @a list.
template <typename T, unsigned N>
static bool
http_config_enum_search(const char *key, const ConfigEnumPair<T> (&list)[N], MgmtByte &value)
{
  // We don't expect any of these lists to be more than 10 long, so a linear search is the best choice.
  for (unsigned i = 0; i < N; ++i) {
    if (0 == strcasecmp(list[i]._key, key)) {
      value = list[i]._value;
      return true;
    }
  }
  return false;
}

/// Read a string from the configuration and convert it to an enumeration value.
/// @a n is the number of entries in the list.
/// @return @c true if the string is found, @c false if not found.
/// If found @a value is set to the corresponding value in @a list.
template <typename T, unsigned N>
static bool
http_config_enum_read(const char *name, const ConfigEnumPair<T> (&list)[N], MgmtByte &value)
{
  char key[512]; // it's just one key - painful UI if keys are longer than this
  if (REC_ERR_OKAY == RecGetRecordString(name, key, sizeof(key))) {
    return http_config_enum_search(key, list, value);
  }
  return false;
}

////////////////////////////////////////////////////////////////
//
//  static variables
//
////////////////////////////////////////////////////////////////
/// Session sharing match types.
static const ConfigEnumPair<TSServerSessionSharingMatchType> SessionSharingMatchStrings[] = {
  {TS_SERVER_SESSION_SHARING_MATCH_NONE, "none"},
  {TS_SERVER_SESSION_SHARING_MATCH_IP, "ip"},
  {TS_SERVER_SESSION_SHARING_MATCH_HOST, "host"},
  {TS_SERVER_SESSION_SHARING_MATCH_BOTH, "both"}};

static const ConfigEnumPair<TSServerSessionSharingPoolType> SessionSharingPoolStrings[] = {
  {TS_SERVER_SESSION_SHARING_POOL_GLOBAL, "global"},
  {TS_SERVER_SESSION_SHARING_POOL_THREAD, "thread"}};

int HttpConfig::m_id = 0;
HttpConfigParams HttpConfig::m_master;

static int http_config_changes          = 1;
static HttpConfigCont *http_config_cont = nullptr;

HttpConfigCont::HttpConfigCont() : Continuation(new_ProxyMutex())
{
  SET_HANDLER(&HttpConfigCont::handle_event);
}

int
HttpConfigCont::handle_event(int /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  if (ink_atomic_increment((int *)&http_config_changes, -1) == 1) {
    HttpConfig::reconfigure();
  }
  return 0;
}

static int
http_config_cb(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData /* data ATS_UNUSED */,
               void * /* cookie ATS_UNUSED */)
{
  ink_atomic_increment((int *)&http_config_changes, 1);

  INK_MEMORY_BARRIER;

  eventProcessor.schedule_in(http_config_cont, HRTIME_SECONDS(1), ET_CALL);
  return 0;
}

// [amc] Not sure which is uglier, this switch or having a micro-function for each var.
// Oh, how I long for when we can use C++eleventy lambdas without compiler problems!
// I think for 5.0 when the BC stuff is yanked, we should probably revert this to independent callbacks.
static int
http_server_session_sharing_cb(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  bool valid_p        = true;
  HttpConfigParams *c = static_cast<HttpConfigParams *>(cookie);

  if (0 == strcasecmp("proxy.config.http.server_session_sharing.match", name)) {
    MgmtByte &match = c->oride.server_session_sharing_match;
    if (RECD_INT == dtype) {
      match = static_cast<TSServerSessionSharingMatchType>(data.rec_int);
    } else if (RECD_STRING == dtype && http_config_enum_search(data.rec_string, SessionSharingMatchStrings, match)) {
      // empty
    } else {
      valid_p = false;
    }
  } else {
    valid_p = false;
  }

  // Signal an update if valid value arrived.
  if (valid_p) {
    http_config_cb(name, dtype, data, cookie);
  }

  return REC_ERR_OKAY;
}

static int
http_insert_forwarded_cb(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  bool valid_p        = false;
  HttpConfigParams *c = static_cast<HttpConfigParams *>(cookie);

  if (0 == strcasecmp("proxy.config.http.insert_forwarded", name)) {
    if (RECD_STRING == dtype) {
      ts::LocalBufferWriter<1024> error;
      HttpForwarded::OptionBitSet bs = HttpForwarded::optStrToBitset(std::string_view(data.rec_string), error);
      if (!error.size()) {
        c->oride.insert_forwarded = bs;
        valid_p                   = true;
      } else {
        Error("HTTP %.*s", static_cast<int>(error.size()), error.data());
      }
    }
  }

  // Signal an update if valid value arrived.
  if (valid_p) {
    http_config_cb(name, dtype, data, cookie);
  }

  return REC_ERR_OKAY;
}

void
register_stat_callbacks()
{
  // Dynamic stats

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.background_fill_current_count", RECD_INT, RECP_NON_PERSISTENT,
                     (int)http_background_fill_current_count_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_background_fill_current_count_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.current_client_connections", RECD_INT, RECP_NON_PERSISTENT,
                     (int)http_current_client_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_client_connections_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.current_active_client_connections", RECD_INT, RECP_NON_PERSISTENT,
                     (int)http_current_active_client_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_active_client_connections_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.websocket.current_active_client_connections", RECD_INT,
                     RECP_NON_PERSISTENT, (int)http_websocket_current_active_client_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_websocket_current_active_client_connections_stat);
  // Current Transaction Stats
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.current_client_transactions", RECD_INT, RECP_NON_PERSISTENT,
                     (int)http_current_client_transactions_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_client_transactions_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.current_server_transactions", RECD_INT, RECP_NON_PERSISTENT,
                     (int)http_current_server_transactions_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_server_transactions_stat);
  // Total connections stats

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.completed_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_completed_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_incoming_connections", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_incoming_connections_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_client_connections", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_client_connections_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_client_connections_ipv4", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_client_connections_ipv4_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_client_connections_ipv6", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_client_connections_ipv6_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_server_connections", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_server_connections_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_parent_proxy_connections", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_parent_proxy_connections_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_parent_retries", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_parent_retries_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_parent_switches", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_parent_switches_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_parent_retries_exhausted", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_parent_retries_exhausted_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_parent_marked_down_count", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_parent_marked_down_count, RecRawStatSyncCount);

  // Upstream current connections stats
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.current_parent_proxy_connections", RECD_INT, RECP_NON_PERSISTENT,
                     (int)http_current_parent_proxy_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_parent_proxy_connections_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.current_server_connections", RECD_INT, RECP_NON_PERSISTENT,
                     (int)http_current_server_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_server_connections_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.current_cache_connections", RECD_INT, RECP_NON_PERSISTENT,
                     (int)http_current_cache_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_cache_connections_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.avg_transactions_per_client_connection", RECD_FLOAT,
                     RECP_PERSISTENT, (int)http_transactions_per_client_con, RecRawStatSyncAvg);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.avg_transactions_per_server_connection", RECD_FLOAT,
                     RECP_PERSISTENT, (int)http_transactions_per_server_con, RecRawStatSyncAvg);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.errors.pre_accept_hangups", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_errors_pre_accept_hangups_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.errors.pre_accept_hangups", RECD_FLOAT,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_errors_pre_accept_hangups_stat,
                     RecRawStatSyncIntMsecsToFloatSeconds);

  // Transactional stats

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.incoming_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_incoming_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.outgoing_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_outgoing_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.incoming_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_incoming_responses_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.invalid_client_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_invalid_client_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.missing_host_hdr", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_missing_host_hdr_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.get_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_get_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.head_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_head_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.trace_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_trace_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.options_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_options_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.post_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_post_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.put_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_put_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.push_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_push_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.delete_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_delete_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.purge_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_purge_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.connect_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_connect_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.extension_method_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_extension_method_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.broken_server_connections", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_broken_server_connections_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_lookups", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_lookups_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_writes", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_writes_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_updates", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_updates_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_deletes", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_deletes_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tunnels", RECD_COUNTER, RECP_PERSISTENT, (int)http_tunnels_stat,
                     RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.throttled_proxy_only", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_throttled_proxy_only_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.parent_proxy_transaction_time", RECD_INT, RECP_PERSISTENT,
                     (int)http_parent_proxy_transaction_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_request_header_total_size", RECD_INT, RECP_PERSISTENT,
                     (int)http_user_agent_request_header_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_response_header_total_size", RECD_INT, RECP_PERSISTENT,
                     (int)http_user_agent_response_header_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_request_document_total_size", RECD_INT, RECP_PERSISTENT,
                     (int)http_user_agent_request_document_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_response_document_total_size", RECD_INT,
                     RECP_PERSISTENT, (int)http_user_agent_response_document_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_request_header_total_size", RECD_INT,
                     RECP_PERSISTENT, (int)http_origin_server_request_header_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_response_header_total_size", RECD_INT,
                     RECP_PERSISTENT, (int)http_origin_server_response_header_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_request_document_total_size", RECD_INT,
                     RECP_PERSISTENT, (int)http_origin_server_request_document_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_response_document_total_size", RECD_INT,
                     RECP_PERSISTENT, (int)http_origin_server_response_document_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.parent_proxy_request_total_bytes", RECD_INT, RECP_PERSISTENT,
                     (int)http_parent_proxy_request_total_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.parent_proxy_response_total_bytes", RECD_INT, RECP_PERSISTENT,
                     (int)http_parent_proxy_response_total_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.pushed_response_header_total_size", RECD_INT, RECP_PERSISTENT,
                     (int)http_pushed_response_header_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.pushed_document_total_size", RECD_INT, RECP_PERSISTENT,
                     (int)http_pushed_document_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.response_document_size_100", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_document_size_100_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.response_document_size_1K", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_document_size_1K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.response_document_size_3K", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_document_size_3K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.response_document_size_5K", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_document_size_5K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.response_document_size_10K", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_document_size_10K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.response_document_size_1M", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_document_size_1M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.response_document_size_inf", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_document_size_inf_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.request_document_size_100", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_request_document_size_100_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.request_document_size_1K", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_request_document_size_1K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.request_document_size_3K", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_request_document_size_3K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.request_document_size_5K", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_request_document_size_5K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.request_document_size_10K", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_request_document_size_10K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.request_document_size_1M", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_request_document_size_1M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.request_document_size_inf", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_request_document_size_inf_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_100", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_user_agent_speed_bytes_per_sec_100_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_1K", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_user_agent_speed_bytes_per_sec_1K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_10K", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_user_agent_speed_bytes_per_sec_10K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_100K", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_user_agent_speed_bytes_per_sec_100K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_1M", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_user_agent_speed_bytes_per_sec_1M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_10M", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_user_agent_speed_bytes_per_sec_10M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_100M", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_user_agent_speed_bytes_per_sec_100M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_100", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_origin_server_speed_bytes_per_sec_100_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_1K", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_origin_server_speed_bytes_per_sec_1K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_10K", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_origin_server_speed_bytes_per_sec_10K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_100K", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_origin_server_speed_bytes_per_sec_100K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_1M", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_origin_server_speed_bytes_per_sec_1M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_10M", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_origin_server_speed_bytes_per_sec_10M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_100M", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_origin_server_speed_bytes_per_sec_100M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_transactions_time", RECD_INT, RECP_PERSISTENT,
                     (int)http_total_transactions_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_hit_fresh", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_hit_fresh_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_hit_mem_fresh", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_hit_mem_fresh_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_hit_revalidated", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_hit_reval_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_hit_ims", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_hit_ims_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_hit_stale_served", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_hit_stale_served_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_miss_cold", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_miss_cold_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_miss_changed", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_miss_changed_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_miss_client_no_cache", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_miss_client_no_cache_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_miss_client_not_cacheable", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_miss_uncacheable_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_miss_ims", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_miss_ims_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_read_error", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_read_error_stat, RecRawStatSyncCount);

  /////////////////////////////////////////
  // Bandwidth Savings Transaction Stats //
  /////////////////////////////////////////

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_hit_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_tcp_hit_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_hit_user_agent_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_hit_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_hit_origin_server_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_hit_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_miss_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_tcp_miss_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_miss_user_agent_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_miss_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_miss_origin_server_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_miss_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_expired_miss_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_tcp_expired_miss_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_expired_miss_user_agent_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_expired_miss_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_expired_miss_origin_server_bytes_stat", RECD_INT,
                     RECP_PERSISTENT, (int)http_tcp_expired_miss_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_refresh_hit_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_tcp_refresh_hit_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_refresh_hit_user_agent_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_refresh_hit_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_refresh_hit_origin_server_bytes_stat", RECD_INT,
                     RECP_PERSISTENT, (int)http_tcp_refresh_hit_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_refresh_miss_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_tcp_refresh_miss_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_refresh_miss_user_agent_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_refresh_miss_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_refresh_miss_origin_server_bytes_stat", RECD_INT,
                     RECP_PERSISTENT, (int)http_tcp_refresh_miss_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_client_refresh_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_tcp_client_refresh_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_client_refresh_user_agent_bytes_stat", RECD_INT,
                     RECP_PERSISTENT, (int)http_tcp_client_refresh_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_client_refresh_origin_server_bytes_stat", RECD_INT,
                     RECP_PERSISTENT, (int)http_tcp_client_refresh_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_ims_hit_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_tcp_ims_hit_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_ims_hit_user_agent_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_ims_hit_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_ims_hit_origin_server_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_ims_hit_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_ims_miss_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_tcp_ims_miss_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_ims_miss_user_agent_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_ims_miss_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.tcp_ims_miss_origin_server_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_tcp_ims_miss_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.err_client_abort_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_err_client_abort_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.err_client_abort_user_agent_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_err_client_abort_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.err_client_abort_origin_server_bytes_stat", RECD_INT,
                     RECP_PERSISTENT, (int)http_err_client_abort_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.err_client_read_error_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_err_client_read_error_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.err_client_read_error_user_agent_bytes_stat", RECD_INT,
                     RECP_PERSISTENT, (int)http_err_client_read_error_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.err_client_read_error_origin_server_bytes_stat", RECD_INT,
                     RECP_PERSISTENT, (int)http_err_client_read_error_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.err_connect_fail_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_err_connect_fail_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.err_connect_fail_user_agent_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_err_connect_fail_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.err_connect_fail_origin_server_bytes_stat", RECD_INT,
                     RECP_PERSISTENT, (int)http_err_connect_fail_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.misc_count_stat", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_misc_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.misc_user_agent_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_misc_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.http_misc_origin_server_bytes_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_misc_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.background_fill_bytes_aborted_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_background_fill_bytes_aborted_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.background_fill_bytes_completed_stat", RECD_INT, RECP_PERSISTENT,
                     (int)http_background_fill_bytes_completed_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_write_errors", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_write_errors, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.cache_read_errors", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_read_errors, RecRawStatSyncSum);

  ////////////////////////////////////////////////////////////////////////////////
  // status code counts
  ////////////////////////////////////////////////////////////////////////////////

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.100_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_100_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.101_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_101_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.1xx_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_1xx_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.200_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_200_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.201_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_201_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.202_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_202_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.203_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_203_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.204_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_204_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.205_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_205_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.206_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_206_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.2xx_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_2xx_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.300_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_300_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.301_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_301_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.302_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_302_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.303_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_303_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.304_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_304_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.305_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_305_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.307_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_307_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.308_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_308_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.3xx_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_3xx_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.400_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_400_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.401_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_401_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.402_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_402_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.403_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_403_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.404_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_404_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.405_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_405_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.406_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_406_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.407_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_407_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.408_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_408_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.409_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_409_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.410_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_410_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.411_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_411_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.412_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_412_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.413_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_413_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.414_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_414_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.415_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_415_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.416_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_416_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.4xx_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_4xx_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.500_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_500_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.501_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_501_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.502_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_502_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.503_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_503_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.504_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_504_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.505_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_505_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.5xx_responses", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_response_status_5xx_count_stat, RecRawStatSyncCount);

  ////////////////////////////////////////////////////////////////////////////////
  // http - time and count of transactions classified by client's point of view //
  //  the internal stat is in msecs, the output time is float seconds           //
  ////////////////////////////////////////////////////////////////////////////////

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.hit_fresh", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_hit_fresh_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.hit_fresh", RECD_FLOAT, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_hit_fresh_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.hit_fresh.process", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_hit_fresh_process_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.hit_fresh.process", RECD_FLOAT,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_hit_fresh_process_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.hit_revalidated", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_hit_reval_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.hit_revalidated", RECD_FLOAT,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_hit_reval_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.miss_cold", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_miss_cold_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.miss_cold", RECD_FLOAT, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_miss_cold_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.miss_not_cacheable", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_miss_uncacheable_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.miss_not_cacheable", RECD_FLOAT,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_miss_uncacheable_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.miss_changed", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_miss_changed_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.miss_changed", RECD_FLOAT, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_miss_changed_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.miss_client_no_cache", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_miss_client_no_cache_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.miss_client_no_cache", RECD_FLOAT,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_miss_client_no_cache_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.errors.aborts", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_errors_aborts_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.errors.aborts", RECD_FLOAT, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_errors_aborts_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.errors.possible_aborts", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_errors_possible_aborts_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.errors.possible_aborts", RECD_FLOAT,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_errors_possible_aborts_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.errors.connect_failed", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_errors_connect_failed_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.errors.connect_failed", RECD_FLOAT,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_errors_connect_failed_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.errors.other", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_errors_other_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.errors.other", RECD_FLOAT, RECP_PERSISTENT,
                     (int)http_ua_msecs_counts_errors_other_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_counts.other.unclassified", RECD_COUNTER,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_other_unclassified_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.transaction_totaltime.other.unclassified", RECD_FLOAT,
                     RECP_PERSISTENT, (int)http_ua_msecs_counts_other_unclassified_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.disallowed_post_100_continue", RECD_COUNTER, RECP_PERSISTENT,
                     (int)disallowed_post_100_continue, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.total_x_redirect_count", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_total_x_redirect_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.https.incoming_requests", RECD_COUNTER, RECP_PERSISTENT,
                     (int)https_incoming_requests_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.https.total_client_connections", RECD_COUNTER, RECP_PERSISTENT,
                     (int)https_total_client_connections_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.origin_connections_throttled_out", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_origin_connections_throttled_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.post_body_too_large", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_post_body_too_large, RecRawStatSyncCount);
  // milestones
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.ua_begin", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_begin_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.ua_first_read", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_first_read_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.ua_read_header_done", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_read_header_done_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.ua_begin_write", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_begin_write_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.ua_close", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_ua_close_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.server_first_connect", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_server_first_connect_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.server_connect", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_server_connect_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.server_connect_end", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_server_connect_end_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.server_begin_write", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_server_begin_write_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.server_first_read", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_server_first_read_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.server_read_header_done", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_server_read_header_done_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.server_close", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_server_close_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.cache_open_read_begin", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_open_read_begin_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.cache_open_read_end", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_open_read_end_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.cache_open_write_begin", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_open_write_begin_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.cache_open_write_end", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_cache_open_write_end_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.dns_lookup_begin", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_dns_lookup_begin_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.dns_lookup_end", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_dns_lookup_end_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.sm_start", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_sm_start_time_stat, RecRawStatSyncSum);
  RecRegisterRawStat(http_rsb, RECT_PROCESS, "proxy.process.http.milestone.sm_finish", RECD_COUNTER, RECP_PERSISTENT,
                     (int)http_sm_finish_time_stat, RecRawStatSyncSum);
}

static bool
set_negative_caching_list(const char *name, RecDataT dtype, RecData data, HttpConfigParams *c, bool update)
{
  bool ret = false;
  HttpStatusBitset set;
  // values from proxy.config.http.negative_caching_list
  if (0 == strcasecmp("proxy.config.http.negative_caching_list", name) && RECD_STRING == dtype && data.rec_string) {
    // parse the list of status code
    ts::TextView status_list(data.rec_string, strlen(data.rec_string));
    auto is_sep{[](char c) { return isspace(c) || ',' == c || ';' == c; }};
    while (!status_list.ltrim_if(is_sep).empty()) {
      ts::TextView span, token{status_list.take_prefix_if(is_sep)};
      auto n = ts::svtoi(token, &span);
      if (span.size() != token.size()) {
        Error("Invalid status code '%.*s' for negative caching: not a number", static_cast<int>(token.size()), token.data());
      } else if (n <= 0 || n >= HTTP_STATUS_NUMBER) {
        Error("Invalid status code '%.*s' for negative caching: out of range", static_cast<int>(token.size()), token.data());
      } else {
        set[n] = 1;
      }
    }
  }
  // set the return value
  if (set != c->negative_caching_list) {
    c->negative_caching_list = set;
    ret                      = ret || update;
  }
  return ret;
}

// Method of getting the status code bitset
static int
negative_caching_list_cb(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  HttpConfigParams *c = static_cast<HttpConfigParams *>(cookie);
  // Signal an update if valid value arrived.
  if (set_negative_caching_list(name, dtype, data, c, true)) {
    http_config_cb(name, dtype, data, cookie);
  }
  return REC_ERR_OKAY;
}

// Method of loading the negative caching config bitset
void
load_negative_caching_var(RecRecord const *r, void *cookie)
{
  HttpConfigParams *c = static_cast<HttpConfigParams *>(cookie);
  set_negative_caching_list(r->name, r->data_type, r->data, c, false);
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::startup()
//
////////////////////////////////////////////////////////////////
void
HttpConfig::startup()
{
  extern void SSLConfigInit(IpMap * map);
  http_rsb = RecAllocateRawStatBlock((int)http_stat_count);
  register_stat_callbacks();

  HttpConfigParams &c = m_master;

  http_config_cont = new HttpConfigCont;

  HttpEstablishStaticConfigStringAlloc(c.proxy_hostname, "proxy.config.proxy_name");
  c.proxy_hostname_len = -1;

  if (c.proxy_hostname == nullptr) {
    c.proxy_hostname    = (char *)ats_malloc(sizeof(char));
    c.proxy_hostname[0] = '\0';
  }

  RecHttpLoadIp("proxy.local.incoming_ip_to_bind", c.inbound_ip4, c.inbound_ip6);
  RecHttpLoadIp("proxy.local.outgoing_ip_to_bind", c.outbound_ip4, c.outbound_ip6);
  RecHttpLoadIpMap("proxy.config.http.proxy_protocol_whitelist", c.config_proxy_protocol_ipmap);
  SSLConfigInit(&c.config_proxy_protocol_ipmap);

  HttpEstablishStaticConfigLongLong(c.server_max_connections, "proxy.config.http.server_max_connections");
  HttpEstablishStaticConfigLongLong(c.max_websocket_connections, "proxy.config.http.websocket.max_number_of_connections");
  HttpEstablishStaticConfigLongLong(c.oride.server_tcp_init_cwnd, "proxy.config.http.server_tcp_init_cwnd");
  HttpEstablishStaticConfigLongLong(c.origin_min_keep_alive_connections, "proxy.config.http.per_server.min_keep_alive");
  HttpEstablishStaticConfigByte(c.oride.attach_server_session_to_client, "proxy.config.http.attach_server_session_to_client");

  HttpEstablishStaticConfigByte(c.disable_ssl_parenting, "proxy.local.http.parent_proxy.disable_connect_tunneling");
  HttpEstablishStaticConfigByte(c.oride.forward_connect_method, "proxy.config.http.forward_connect_method");

  HttpEstablishStaticConfigByte(c.no_dns_forward_to_parent, "proxy.config.http.no_dns_just_forward_to_parent");
  HttpEstablishStaticConfigByte(c.oride.uncacheable_requests_bypass_parent, "proxy.config.http.uncacheable_requests_bypass_parent");
  HttpEstablishStaticConfigByte(c.oride.doc_in_cache_skip_dns, "proxy.config.http.doc_in_cache_skip_dns");

  HttpEstablishStaticConfigByte(c.no_origin_server_dns, "proxy.config.http.no_origin_server_dns");
  HttpEstablishStaticConfigByte(c.use_client_target_addr, "proxy.config.http.use_client_target_addr");
  HttpEstablishStaticConfigByte(c.use_client_source_port, "proxy.config.http.use_client_source_port");
  HttpEstablishStaticConfigByte(c.oride.maintain_pristine_host_hdr, "proxy.config.url_remap.pristine_host_hdr");

  HttpEstablishStaticConfigByte(c.oride.insert_request_via_string, "proxy.config.http.insert_request_via_str");
  HttpEstablishStaticConfigByte(c.oride.insert_response_via_string, "proxy.config.http.insert_response_via_str");
  HttpEstablishStaticConfigLongLong(c.oride.proxy_response_hsts_max_age, "proxy.config.ssl.hsts_max_age");
  HttpEstablishStaticConfigByte(c.oride.proxy_response_hsts_include_subdomains, "proxy.config.ssl.hsts_include_subdomains");

  HttpEstablishStaticConfigStringAlloc(c.proxy_request_via_string, "proxy.config.http.request_via_str");
  c.proxy_request_via_string_len = -1;
  HttpEstablishStaticConfigStringAlloc(c.proxy_response_via_string, "proxy.config.http.response_via_str");
  c.proxy_response_via_string_len = -1;

  HttpEstablishStaticConfigByte(c.oride.keep_alive_enabled_in, "proxy.config.http.keep_alive_enabled_in");
  HttpEstablishStaticConfigByte(c.oride.keep_alive_enabled_out, "proxy.config.http.keep_alive_enabled_out");
  HttpEstablishStaticConfigByte(c.oride.chunking_enabled, "proxy.config.http.chunking_enabled");
  HttpEstablishStaticConfigLongLong(c.oride.http_chunking_size, "proxy.config.http.chunking.size");
  HttpEstablishStaticConfigByte(c.oride.flow_control_enabled, "proxy.config.http.flow_control.enabled");
  HttpEstablishStaticConfigLongLong(c.oride.flow_high_water_mark, "proxy.config.http.flow_control.high_water");
  HttpEstablishStaticConfigLongLong(c.oride.flow_low_water_mark, "proxy.config.http.flow_control.low_water");
  HttpEstablishStaticConfigByte(c.oride.post_check_content_length_enabled, "proxy.config.http.post.check.content_length.enabled");
  HttpEstablishStaticConfigByte(c.oride.request_buffer_enabled, "proxy.config.http.request_buffer_enabled");
  HttpEstablishStaticConfigByte(c.strict_uri_parsing, "proxy.config.http.strict_uri_parsing");

  // [amc] This is a bit of a mess, need to figure out to make this cleaner.
  RecRegisterConfigUpdateCb("proxy.config.http.server_session_sharing.match", &http_server_session_sharing_cb, &c);
  http_config_enum_read("proxy.config.http.server_session_sharing.match", SessionSharingMatchStrings,
                        c.oride.server_session_sharing_match);
  http_config_enum_read("proxy.config.http.server_session_sharing.pool", SessionSharingPoolStrings, c.server_session_sharing_pool);

  RecRegisterConfigUpdateCb("proxy.config.http.insert_forwarded", &http_insert_forwarded_cb, &c);
  {
    char str[512];

    if (REC_ERR_OKAY == RecGetRecordString("proxy.config.http.insert_forwarded", str, sizeof(str))) {
      ts::LocalBufferWriter<1024> error;
      HttpForwarded::OptionBitSet bs = HttpForwarded::optStrToBitset(std::string_view(str), error);
      if (!error.size()) {
        c.oride.insert_forwarded = bs;
      } else {
        Error("HTTP %.*s", static_cast<int>(error.size()), error.data());
      }
    }
  }

  HttpEstablishStaticConfigByte(c.oride.auth_server_session_private, "proxy.config.http.auth_server_session_private");

  HttpEstablishStaticConfigByte(c.oride.keep_alive_post_out, "proxy.config.http.keep_alive_post_out");

  HttpEstablishStaticConfigLongLong(c.oride.keep_alive_no_activity_timeout_in,
                                    "proxy.config.http.keep_alive_no_activity_timeout_in");
  HttpEstablishStaticConfigLongLong(c.oride.keep_alive_no_activity_timeout_out,
                                    "proxy.config.http.keep_alive_no_activity_timeout_out");
  HttpEstablishStaticConfigLongLong(c.oride.transaction_no_activity_timeout_in,
                                    "proxy.config.http.transaction_no_activity_timeout_in");
  HttpEstablishStaticConfigLongLong(c.oride.transaction_no_activity_timeout_out,
                                    "proxy.config.http.transaction_no_activity_timeout_out");
  HttpEstablishStaticConfigLongLong(c.oride.websocket_active_timeout, "proxy.config.websocket.active_timeout");
  HttpEstablishStaticConfigLongLong(c.oride.websocket_inactive_timeout, "proxy.config.websocket.no_activity_timeout");

  HttpEstablishStaticConfigLongLong(c.oride.transaction_active_timeout_in, "proxy.config.http.transaction_active_timeout_in");
  HttpEstablishStaticConfigLongLong(c.oride.transaction_active_timeout_out, "proxy.config.http.transaction_active_timeout_out");
  HttpEstablishStaticConfigLongLong(c.accept_no_activity_timeout, "proxy.config.http.accept_no_activity_timeout");

  HttpEstablishStaticConfigLongLong(c.oride.background_fill_active_timeout, "proxy.config.http.background_fill_active_timeout");
  HttpEstablishStaticConfigFloat(c.oride.background_fill_threshold, "proxy.config.http.background_fill_completed_threshold");

  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_max_retries, "proxy.config.http.connect_attempts_max_retries");
  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_max_retries_dead_server,
                                    "proxy.config.http.connect_attempts_max_retries_dead_server");

  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_rr_retries, "proxy.config.http.connect_attempts_rr_retries");
  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_timeout, "proxy.config.http.connect_attempts_timeout");
  HttpEstablishStaticConfigLongLong(c.oride.post_connect_attempts_timeout, "proxy.config.http.post_connect_attempts_timeout");
  HttpEstablishStaticConfigLongLong(c.oride.parent_connect_attempts, "proxy.config.http.parent_proxy.total_connect_attempts");
  HttpEstablishStaticConfigLongLong(c.oride.parent_retry_time, "proxy.config.http.parent_proxy.retry_time");
  HttpEstablishStaticConfigLongLong(c.oride.parent_fail_threshold, "proxy.config.http.parent_proxy.fail_threshold");
  HttpEstablishStaticConfigLongLong(c.oride.per_parent_connect_attempts,
                                    "proxy.config.http.parent_proxy.per_parent_connect_attempts");
  HttpEstablishStaticConfigLongLong(c.oride.parent_connect_timeout, "proxy.config.http.parent_proxy.connect_attempts_timeout");
  HttpEstablishStaticConfigByte(c.oride.parent_failures_update_hostdb, "proxy.config.http.parent_proxy.mark_down_hostdb");

  HttpEstablishStaticConfigLongLong(c.oride.sock_recv_buffer_size_out, "proxy.config.net.sock_recv_buffer_size_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_send_buffer_size_out, "proxy.config.net.sock_send_buffer_size_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_option_flag_out, "proxy.config.net.sock_option_flag_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_packet_mark_out, "proxy.config.net.sock_packet_mark_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_packet_tos_out, "proxy.config.net.sock_packet_tos_out");

  HttpEstablishStaticConfigByte(c.oride.fwd_proxy_auth_to_parent, "proxy.config.http.forward.proxy_auth_to_parent");

  HttpEstablishStaticConfigByte(c.oride.anonymize_remove_from, "proxy.config.http.anonymize_remove_from");
  HttpEstablishStaticConfigByte(c.oride.anonymize_remove_referer, "proxy.config.http.anonymize_remove_referer");
  HttpEstablishStaticConfigByte(c.oride.anonymize_remove_user_agent, "proxy.config.http.anonymize_remove_user_agent");
  HttpEstablishStaticConfigByte(c.oride.anonymize_remove_cookie, "proxy.config.http.anonymize_remove_cookie");
  HttpEstablishStaticConfigByte(c.oride.anonymize_remove_client_ip, "proxy.config.http.anonymize_remove_client_ip");
  HttpEstablishStaticConfigByte(c.oride.anonymize_insert_client_ip, "proxy.config.http.insert_client_ip");
  HttpEstablishStaticConfigStringAlloc(c.anonymize_other_header_list, "proxy.config.http.anonymize_other_header_list");

  HttpEstablishStaticConfigStringAlloc(c.oride.global_user_agent_header, "proxy.config.http.global_user_agent_header");
  c.oride.global_user_agent_header_size = c.oride.global_user_agent_header ? strlen(c.oride.global_user_agent_header) : 0;

  HttpEstablishStaticConfigByte(c.oride.proxy_response_server_enabled, "proxy.config.http.response_server_enabled");
  HttpEstablishStaticConfigStringAlloc(c.oride.proxy_response_server_string, "proxy.config.http.response_server_str");
  c.oride.proxy_response_server_string_len =
    c.oride.proxy_response_server_string ? strlen(c.oride.proxy_response_server_string) : 0;

  HttpEstablishStaticConfigByte(c.oride.insert_squid_x_forwarded_for, "proxy.config.http.insert_squid_x_forwarded_for");

  HttpEstablishStaticConfigByte(c.oride.insert_age_in_response, "proxy.config.http.insert_age_in_response");
  HttpEstablishStaticConfigByte(c.enable_http_stats, "proxy.config.http.enable_http_stats");
  HttpEstablishStaticConfigByte(c.oride.normalize_ae, "proxy.config.http.normalize_ae");

  HttpEstablishStaticConfigLongLong(c.oride.cache_heuristic_min_lifetime, "proxy.config.http.cache.heuristic_min_lifetime");
  HttpEstablishStaticConfigLongLong(c.oride.cache_heuristic_max_lifetime, "proxy.config.http.cache.heuristic_max_lifetime");
  HttpEstablishStaticConfigFloat(c.oride.cache_heuristic_lm_factor, "proxy.config.http.cache.heuristic_lm_factor");

  HttpEstablishStaticConfigLongLong(c.oride.cache_guaranteed_min_lifetime, "proxy.config.http.cache.guaranteed_min_lifetime");
  HttpEstablishStaticConfigLongLong(c.oride.cache_guaranteed_max_lifetime, "proxy.config.http.cache.guaranteed_max_lifetime");

  HttpEstablishStaticConfigLongLong(c.oride.cache_max_stale_age, "proxy.config.http.cache.max_stale_age");
  HttpEstablishStaticConfigByte(c.oride.srv_enabled, "proxy.config.srv_enabled");

  HttpEstablishStaticConfigByte(c.oride.allow_half_open, "proxy.config.http.allow_half_open");

  HttpEstablishStaticConfigStringAlloc(c.oride.cache_vary_default_text, "proxy.config.http.cache.vary_default_text");
  HttpEstablishStaticConfigStringAlloc(c.oride.cache_vary_default_images, "proxy.config.http.cache.vary_default_images");
  HttpEstablishStaticConfigStringAlloc(c.oride.cache_vary_default_other, "proxy.config.http.cache.vary_default_other");

  // open read failure retries
  HttpEstablishStaticConfigLongLong(c.oride.max_cache_open_read_retries, "proxy.config.http.cache.max_open_read_retries");
  HttpEstablishStaticConfigLongLong(c.oride.cache_open_read_retry_time, "proxy.config.http.cache.open_read_retry_time");
  HttpEstablishStaticConfigLongLong(c.oride.cache_generation_number, "proxy.config.http.cache.generation");

  // open write failure retries
  HttpEstablishStaticConfigLongLong(c.oride.max_cache_open_write_retries, "proxy.config.http.cache.max_open_write_retries");

  HttpEstablishStaticConfigByte(c.oride.cache_http, "proxy.config.http.cache.http");
  HttpEstablishStaticConfigByte(c.oride.cache_ignore_client_no_cache, "proxy.config.http.cache.ignore_client_no_cache");
  HttpEstablishStaticConfigByte(c.oride.cache_ignore_client_cc_max_age, "proxy.config.http.cache.ignore_client_cc_max_age");
  HttpEstablishStaticConfigByte(c.oride.cache_ims_on_client_no_cache, "proxy.config.http.cache.ims_on_client_no_cache");
  HttpEstablishStaticConfigByte(c.oride.cache_ignore_server_no_cache, "proxy.config.http.cache.ignore_server_no_cache");
  HttpEstablishStaticConfigByte(c.oride.cache_responses_to_cookies, "proxy.config.http.cache.cache_responses_to_cookies");

  HttpEstablishStaticConfigByte(c.oride.cache_ignore_auth, "proxy.config.http.cache.ignore_authentication");
  HttpEstablishStaticConfigByte(c.oride.cache_urls_that_look_dynamic, "proxy.config.http.cache.cache_urls_that_look_dynamic");
  HttpEstablishStaticConfigByte(c.oride.cache_enable_default_vary_headers, "proxy.config.http.cache.enable_default_vary_headers");
  HttpEstablishStaticConfigByte(c.cache_post_method, "proxy.config.http.cache.post_method");

  HttpEstablishStaticConfigByte(c.oride.ignore_accept_mismatch, "proxy.config.http.cache.ignore_accept_mismatch");
  HttpEstablishStaticConfigByte(c.oride.ignore_accept_language_mismatch, "proxy.config.http.cache.ignore_accept_language_mismatch");
  HttpEstablishStaticConfigByte(c.oride.ignore_accept_encoding_mismatch, "proxy.config.http.cache.ignore_accept_encoding_mismatch");
  HttpEstablishStaticConfigByte(c.oride.ignore_accept_charset_mismatch, "proxy.config.http.cache.ignore_accept_charset_mismatch");

  HttpEstablishStaticConfigByte(c.send_100_continue_response, "proxy.config.http.send_100_continue_response");
  HttpEstablishStaticConfigByte(c.disallow_post_100_continue, "proxy.config.http.disallow_post_100_continue");

  HttpEstablishStaticConfigByte(c.keepalive_internal_vc, "proxy.config.http.keepalive_internal_vc");

  HttpEstablishStaticConfigByte(c.oride.cache_open_write_fail_action, "proxy.config.http.cache.open_write_fail_action");

  HttpEstablishStaticConfigByte(c.oride.cache_when_to_revalidate, "proxy.config.http.cache.when_to_revalidate");
  HttpEstablishStaticConfigByte(c.oride.cache_required_headers, "proxy.config.http.cache.required_headers");
  HttpEstablishStaticConfigByte(c.oride.cache_range_lookup, "proxy.config.http.cache.range.lookup");
  HttpEstablishStaticConfigByte(c.oride.cache_range_write, "proxy.config.http.cache.range.write");

  HttpEstablishStaticConfigStringAlloc(c.connect_ports_string, "proxy.config.http.connect_ports");

  HttpEstablishStaticConfigLongLong(c.oride.request_hdr_max_size, "proxy.config.http.request_header_max_size");
  HttpEstablishStaticConfigLongLong(c.oride.response_hdr_max_size, "proxy.config.http.response_header_max_size");

  HttpEstablishStaticConfigByte(c.push_method_enabled, "proxy.config.http.push_method_enabled");

  HttpEstablishStaticConfigByte(c.reverse_proxy_enabled, "proxy.config.reverse_proxy.enabled");
  HttpEstablishStaticConfigByte(c.url_remap_required, "proxy.config.url_remap.remap_required");

  HttpEstablishStaticConfigStringAlloc(c.reverse_proxy_no_host_redirect, "proxy.config.header.parse.no_host_url_redirect");
  c.reverse_proxy_no_host_redirect_len = -1;
  HttpEstablishStaticConfigStringAlloc(c.oride.body_factory_template_base, "proxy.config.body_factory.template_base");
  c.oride.body_factory_template_base_len = c.oride.body_factory_template_base ? strlen(c.oride.body_factory_template_base) : 0;
  HttpEstablishStaticConfigLongLong(c.body_factory_response_max_size, "proxy.config.body_factory.response_max_size");
  HttpEstablishStaticConfigByte(c.errors_log_error_pages, "proxy.config.http.errors.log_error_pages");

  HttpEstablishStaticConfigLongLong(c.oride.slow_log_threshold, "proxy.config.http.slow.log.threshold");

  HttpEstablishStaticConfigByte(c.oride.send_http11_requests, "proxy.config.http.send_http11_requests");
  HttpEstablishStaticConfigByte(c.oride.allow_multi_range, "proxy.config.http.allow_multi_range");

  // HTTP Referer Filtering
  HttpEstablishStaticConfigByte(c.referer_filter_enabled, "proxy.config.http.referer_filter");
  HttpEstablishStaticConfigByte(c.referer_format_redirect, "proxy.config.http.referer_format_redirect");

  HttpEstablishStaticConfigLongLong(c.oride.down_server_timeout, "proxy.config.http.down_server.cache_time");
  HttpEstablishStaticConfigLongLong(c.oride.client_abort_threshold, "proxy.config.http.down_server.abort_threshold");

  // Negative caching and revalidation
  HttpEstablishStaticConfigByte(c.oride.negative_caching_enabled, "proxy.config.http.negative_caching_enabled");
  HttpEstablishStaticConfigLongLong(c.oride.negative_caching_lifetime, "proxy.config.http.negative_caching_lifetime");
  HttpEstablishStaticConfigByte(c.oride.negative_revalidating_enabled, "proxy.config.http.negative_revalidating_enabled");
  HttpEstablishStaticConfigLongLong(c.oride.negative_revalidating_lifetime, "proxy.config.http.negative_revalidating_lifetime");
  RecRegisterConfigUpdateCb("proxy.config.http.negative_caching_list", &negative_caching_list_cb, &c);
  RecLookupRecord("proxy.config.http.negative_caching_list", &load_negative_caching_var, &c, true);

  // Buffer size and watermark
  HttpEstablishStaticConfigLongLong(c.oride.default_buffer_size_index, "proxy.config.http.default_buffer_size");
  HttpEstablishStaticConfigLongLong(c.oride.default_buffer_water_mark, "proxy.config.http.default_buffer_water_mark");

  // Stat Page Info
  HttpEstablishStaticConfigByte(c.enable_http_info, "proxy.config.http.enable_http_info");

  HttpEstablishStaticConfigLongLong(c.max_post_size, "proxy.config.http.max_post_size");

  //##############################################################################
  //#
  //# Redirection
  //#
  //# See RecordsConfig definition.
  //#
  //##############################################################################
  HttpEstablishStaticConfigByte(c.oride.redirect_use_orig_cache_key, "proxy.config.http.redirect_use_orig_cache_key");
  HttpEstablishStaticConfigByte(c.redirection_host_no_port, "proxy.config.http.redirect_host_no_port");
  HttpEstablishStaticConfigLongLong(c.oride.number_of_redirections, "proxy.config.http.number_of_redirections");
  HttpEstablishStaticConfigLongLong(c.post_copy_size, "proxy.config.http.post_copy_size");
  HttpEstablishStaticConfigStringAlloc(c.redirect_actions_string, "proxy.config.http.redirect.actions");

  HttpEstablishStaticConfigStringAlloc(c.oride.ssl_client_sni_policy, "proxy.config.ssl.client.sni_policy");

  OutboundConnTrack::config_init(&c.outbound_conntrack, &c.oride.outbound_conntrack);

  MUTEX_TRY_LOCK(lock, http_config_cont->mutex, this_ethread());
  if (!lock.is_locked()) {
    ink_release_assert(0);
  }
  http_config_cont->handleEvent(EVENT_NONE, nullptr);

  return;
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::reconfigure()
//
////////////////////////////////////////////////////////////////
void
HttpConfig::reconfigure()
{
  auto INT_TO_BOOL = [](RecInt i) -> bool { return i != 0; };

  HttpConfigParams *params;

  params = new HttpConfigParams;

  params->inbound_ip4 = m_master.inbound_ip4;
  params->inbound_ip6 = m_master.inbound_ip6;

  params->outbound_ip4 = m_master.outbound_ip4;
  params->outbound_ip6 = m_master.outbound_ip6;

  params->proxy_hostname                           = ats_strdup(m_master.proxy_hostname);
  params->proxy_hostname_len                       = (params->proxy_hostname) ? strlen(params->proxy_hostname) : 0;
  params->no_dns_forward_to_parent                 = INT_TO_BOOL(m_master.no_dns_forward_to_parent);
  params->oride.uncacheable_requests_bypass_parent = INT_TO_BOOL(m_master.oride.uncacheable_requests_bypass_parent);
  params->no_origin_server_dns                     = INT_TO_BOOL(m_master.no_origin_server_dns);
  params->use_client_target_addr                   = m_master.use_client_target_addr;
  params->use_client_source_port                   = INT_TO_BOOL(m_master.use_client_source_port);
  params->oride.maintain_pristine_host_hdr         = INT_TO_BOOL(m_master.oride.maintain_pristine_host_hdr);

  params->disable_ssl_parenting        = INT_TO_BOOL(m_master.disable_ssl_parenting);
  params->oride.forward_connect_method = INT_TO_BOOL(m_master.oride.forward_connect_method);

  params->server_max_connections     = m_master.server_max_connections;
  params->max_websocket_connections  = m_master.max_websocket_connections;
  params->oride.server_tcp_init_cwnd = m_master.oride.server_tcp_init_cwnd;
  params->oride.outbound_conntrack   = m_master.oride.outbound_conntrack;
  // If queuing for outbound connection tracking is enabled without enabling max connections, it is meaningless, so we'll warn
  if (params->outbound_conntrack.queue_size > 0 &&
      !(params->oride.outbound_conntrack.max > 0 || params->origin_min_keep_alive_connections)) {
    Warning("'%s' is set, but neither '%s' nor 'per_server.min_keep_alive_connections' are "
            "set, please correct your records.config",
            OutboundConnTrack::CONFIG_VAR_QUEUE_SIZE.data(), OutboundConnTrack::CONFIG_VAR_MAX.data());
  }
  params->origin_min_keep_alive_connections     = m_master.origin_min_keep_alive_connections;
  params->oride.attach_server_session_to_client = m_master.oride.attach_server_session_to_client;

  if (params->oride.outbound_conntrack.max > 0 &&
      params->oride.outbound_conntrack.max < params->origin_min_keep_alive_connections) {
    Warning("'%s' < origin_min_keep_alive_connections, setting min=max , please correct your records.config",
            OutboundConnTrack::CONFIG_VAR_MAX.data());
    params->origin_min_keep_alive_connections = params->oride.outbound_conntrack.max;
  }

  params->oride.insert_request_via_string   = m_master.oride.insert_request_via_string;
  params->oride.insert_response_via_string  = m_master.oride.insert_response_via_string;
  params->proxy_request_via_string          = ats_strdup(m_master.proxy_request_via_string);
  params->proxy_request_via_string_len      = (params->proxy_request_via_string) ? strlen(params->proxy_request_via_string) : 0;
  params->proxy_response_via_string         = ats_strdup(m_master.proxy_response_via_string);
  params->proxy_response_via_string_len     = (params->proxy_response_via_string) ? strlen(params->proxy_response_via_string) : 0;
  params->oride.proxy_response_hsts_max_age = m_master.oride.proxy_response_hsts_max_age;
  params->oride.proxy_response_hsts_include_subdomains = m_master.oride.proxy_response_hsts_include_subdomains;

  params->oride.keep_alive_enabled_in       = INT_TO_BOOL(m_master.oride.keep_alive_enabled_in);
  params->oride.keep_alive_enabled_out      = INT_TO_BOOL(m_master.oride.keep_alive_enabled_out);
  params->oride.chunking_enabled            = INT_TO_BOOL(m_master.oride.chunking_enabled);
  params->oride.auth_server_session_private = INT_TO_BOOL(m_master.oride.auth_server_session_private);

  params->oride.http_chunking_size = m_master.oride.http_chunking_size;

  params->oride.post_check_content_length_enabled = INT_TO_BOOL(m_master.oride.post_check_content_length_enabled);

  params->oride.request_buffer_enabled = INT_TO_BOOL(m_master.oride.request_buffer_enabled);

  params->oride.flow_control_enabled = INT_TO_BOOL(m_master.oride.flow_control_enabled);
  params->oride.flow_high_water_mark = m_master.oride.flow_high_water_mark;
  params->oride.flow_low_water_mark  = m_master.oride.flow_low_water_mark;
  // If not set (zero) then make values the same.
  if (params->oride.flow_low_water_mark <= 0) {
    params->oride.flow_low_water_mark = params->oride.flow_high_water_mark;
  }
  if (params->oride.flow_high_water_mark <= 0) {
    params->oride.flow_high_water_mark = params->oride.flow_low_water_mark;
  }
  if (params->oride.flow_high_water_mark < params->oride.flow_low_water_mark) {
    Warning("Flow control low water mark is greater than high water mark, flow control disabled");
    params->oride.flow_control_enabled = 0;
    // zero means "hardwired default" when actually used.
    params->oride.flow_high_water_mark = params->oride.flow_low_water_mark = 0;
  }

  params->oride.server_session_sharing_match = m_master.oride.server_session_sharing_match;
  params->server_session_sharing_pool        = m_master.server_session_sharing_pool;
  params->oride.keep_alive_post_out          = m_master.oride.keep_alive_post_out;

  params->oride.keep_alive_no_activity_timeout_in   = m_master.oride.keep_alive_no_activity_timeout_in;
  params->oride.keep_alive_no_activity_timeout_out  = m_master.oride.keep_alive_no_activity_timeout_out;
  params->oride.transaction_no_activity_timeout_in  = m_master.oride.transaction_no_activity_timeout_in;
  params->oride.transaction_no_activity_timeout_out = m_master.oride.transaction_no_activity_timeout_out;
  params->oride.transaction_active_timeout_in       = m_master.oride.transaction_active_timeout_in;
  params->oride.transaction_active_timeout_out      = m_master.oride.transaction_active_timeout_out;
  params->oride.websocket_active_timeout            = m_master.oride.websocket_active_timeout;
  params->oride.websocket_inactive_timeout          = m_master.oride.websocket_inactive_timeout;
  params->accept_no_activity_timeout                = m_master.accept_no_activity_timeout;
  params->oride.background_fill_active_timeout      = m_master.oride.background_fill_active_timeout;
  params->oride.background_fill_threshold           = m_master.oride.background_fill_threshold;

  params->oride.connect_attempts_max_retries             = m_master.oride.connect_attempts_max_retries;
  params->oride.connect_attempts_max_retries_dead_server = m_master.oride.connect_attempts_max_retries_dead_server;
  if (m_master.oride.connect_attempts_rr_retries > params->oride.connect_attempts_max_retries) {
    Warning("connect_attempts_rr_retries (%" PRIu64 ") is greater than "
            "connect_attempts_max_retries (%" PRIu64 "), this means requests "
            "will never redispatch to another server",
            m_master.oride.connect_attempts_rr_retries, params->oride.connect_attempts_max_retries);
  }
  params->oride.connect_attempts_rr_retries   = m_master.oride.connect_attempts_rr_retries;
  params->oride.connect_attempts_timeout      = m_master.oride.connect_attempts_timeout;
  params->oride.post_connect_attempts_timeout = m_master.oride.post_connect_attempts_timeout;
  params->oride.parent_connect_attempts       = m_master.oride.parent_connect_attempts;
  params->oride.parent_retry_time             = m_master.oride.parent_retry_time;
  params->oride.parent_fail_threshold         = m_master.oride.parent_fail_threshold;
  params->oride.per_parent_connect_attempts   = m_master.oride.per_parent_connect_attempts;
  params->oride.parent_connect_timeout        = m_master.oride.parent_connect_timeout;
  params->oride.parent_failures_update_hostdb = m_master.oride.parent_failures_update_hostdb;

  params->oride.sock_recv_buffer_size_out = m_master.oride.sock_recv_buffer_size_out;
  params->oride.sock_send_buffer_size_out = m_master.oride.sock_send_buffer_size_out;
  params->oride.sock_packet_mark_out      = m_master.oride.sock_packet_mark_out;
  params->oride.sock_packet_tos_out       = m_master.oride.sock_packet_tos_out;
  params->oride.sock_option_flag_out      = m_master.oride.sock_option_flag_out;

  // Clear the TCP Fast Open option if it is not supported on this host.
  if ((params->oride.sock_option_flag_out & NetVCOptions::SOCK_OPT_TCP_FAST_OPEN) && !SocketManager::fastopen_supported()) {
    Status("disabling unsupported TCP Fast Open flag on proxy.config.net.sock_option_flag_out");
    params->oride.sock_option_flag_out &= ~NetVCOptions::SOCK_OPT_TCP_FAST_OPEN;
  }

  params->oride.fwd_proxy_auth_to_parent = INT_TO_BOOL(m_master.oride.fwd_proxy_auth_to_parent);

  params->oride.anonymize_remove_from       = INT_TO_BOOL(m_master.oride.anonymize_remove_from);
  params->oride.anonymize_remove_referer    = INT_TO_BOOL(m_master.oride.anonymize_remove_referer);
  params->oride.anonymize_remove_user_agent = INT_TO_BOOL(m_master.oride.anonymize_remove_user_agent);
  params->oride.anonymize_remove_cookie     = INT_TO_BOOL(m_master.oride.anonymize_remove_cookie);
  params->oride.anonymize_remove_client_ip  = INT_TO_BOOL(m_master.oride.anonymize_remove_client_ip);
  params->oride.anonymize_insert_client_ip  = m_master.oride.anonymize_insert_client_ip;
  params->anonymize_other_header_list       = ats_strdup(m_master.anonymize_other_header_list);

  params->oride.global_user_agent_header = ats_strdup(m_master.oride.global_user_agent_header);
  params->oride.global_user_agent_header_size =
    params->oride.global_user_agent_header ? strlen(params->oride.global_user_agent_header) : 0;

  params->oride.proxy_response_server_string = ats_strdup(m_master.oride.proxy_response_server_string);
  params->oride.proxy_response_server_string_len =
    params->oride.proxy_response_server_string ? strlen(params->oride.proxy_response_server_string) : 0;
  params->oride.proxy_response_server_enabled = m_master.oride.proxy_response_server_enabled;

  params->oride.insert_squid_x_forwarded_for = INT_TO_BOOL(m_master.oride.insert_squid_x_forwarded_for);
  params->oride.insert_forwarded             = m_master.oride.insert_forwarded;
  params->oride.insert_age_in_response       = INT_TO_BOOL(m_master.oride.insert_age_in_response);
  params->enable_http_stats                  = INT_TO_BOOL(m_master.enable_http_stats);
  params->oride.normalize_ae                 = m_master.oride.normalize_ae;

  params->oride.cache_heuristic_min_lifetime = m_master.oride.cache_heuristic_min_lifetime;
  params->oride.cache_heuristic_max_lifetime = m_master.oride.cache_heuristic_max_lifetime;
  params->oride.cache_heuristic_lm_factor    = std::min(std::max(m_master.oride.cache_heuristic_lm_factor, 0.0f), 1.0f);

  params->oride.cache_guaranteed_min_lifetime = m_master.oride.cache_guaranteed_min_lifetime;
  params->oride.cache_guaranteed_max_lifetime = m_master.oride.cache_guaranteed_max_lifetime;

  params->oride.cache_max_stale_age = m_master.oride.cache_max_stale_age;

  params->oride.cache_vary_default_text   = ats_strdup(m_master.oride.cache_vary_default_text);
  params->oride.cache_vary_default_images = ats_strdup(m_master.oride.cache_vary_default_images);
  params->oride.cache_vary_default_other  = ats_strdup(m_master.oride.cache_vary_default_other);

  params->oride.srv_enabled = m_master.oride.srv_enabled;

  params->oride.allow_half_open = m_master.oride.allow_half_open;

  // open read failure retries
  params->oride.max_cache_open_read_retries = m_master.oride.max_cache_open_read_retries;
  params->oride.cache_open_read_retry_time  = m_master.oride.cache_open_read_retry_time;
  params->oride.cache_generation_number     = m_master.oride.cache_generation_number;

  // open write failure retries
  params->oride.max_cache_open_write_retries = m_master.oride.max_cache_open_write_retries;

  params->oride.cache_http                        = INT_TO_BOOL(m_master.oride.cache_http);
  params->oride.cache_ignore_client_no_cache      = INT_TO_BOOL(m_master.oride.cache_ignore_client_no_cache);
  params->oride.cache_ignore_client_cc_max_age    = INT_TO_BOOL(m_master.oride.cache_ignore_client_cc_max_age);
  params->oride.cache_ims_on_client_no_cache      = INT_TO_BOOL(m_master.oride.cache_ims_on_client_no_cache);
  params->oride.cache_ignore_server_no_cache      = INT_TO_BOOL(m_master.oride.cache_ignore_server_no_cache);
  params->oride.cache_responses_to_cookies        = m_master.oride.cache_responses_to_cookies;
  params->oride.cache_ignore_auth                 = INT_TO_BOOL(m_master.oride.cache_ignore_auth);
  params->oride.cache_urls_that_look_dynamic      = INT_TO_BOOL(m_master.oride.cache_urls_that_look_dynamic);
  params->oride.cache_enable_default_vary_headers = INT_TO_BOOL(m_master.oride.cache_enable_default_vary_headers);
  params->cache_post_method                       = INT_TO_BOOL(m_master.cache_post_method);

  params->oride.ignore_accept_mismatch          = m_master.oride.ignore_accept_mismatch;
  params->oride.ignore_accept_language_mismatch = m_master.oride.ignore_accept_language_mismatch;
  params->oride.ignore_accept_encoding_mismatch = m_master.oride.ignore_accept_encoding_mismatch;
  params->oride.ignore_accept_charset_mismatch  = m_master.oride.ignore_accept_charset_mismatch;

  params->send_100_continue_response = INT_TO_BOOL(m_master.send_100_continue_response);
  params->disallow_post_100_continue = INT_TO_BOOL(m_master.disallow_post_100_continue);
  params->keepalive_internal_vc      = INT_TO_BOOL(m_master.keepalive_internal_vc);

  params->oride.cache_open_write_fail_action = m_master.oride.cache_open_write_fail_action;

  params->oride.cache_when_to_revalidate = m_master.oride.cache_when_to_revalidate;
  params->max_post_size                  = m_master.max_post_size;

  params->oride.cache_required_headers = m_master.oride.cache_required_headers;
  params->oride.cache_range_lookup     = INT_TO_BOOL(m_master.oride.cache_range_lookup);
  params->oride.cache_range_write      = INT_TO_BOOL(m_master.oride.cache_range_write);
  params->oride.allow_multi_range      = m_master.oride.allow_multi_range;

  params->connect_ports_string = ats_strdup(m_master.connect_ports_string);
  params->connect_ports        = parse_ports_list(params->connect_ports_string);

  params->oride.request_hdr_max_size  = m_master.oride.request_hdr_max_size;
  params->oride.response_hdr_max_size = m_master.oride.response_hdr_max_size;

  params->push_method_enabled = INT_TO_BOOL(m_master.push_method_enabled);

  params->reverse_proxy_enabled            = INT_TO_BOOL(m_master.reverse_proxy_enabled);
  params->url_remap_required               = INT_TO_BOOL(m_master.url_remap_required);
  params->errors_log_error_pages           = INT_TO_BOOL(m_master.errors_log_error_pages);
  params->oride.slow_log_threshold         = m_master.oride.slow_log_threshold;
  params->oride.send_http11_requests       = m_master.oride.send_http11_requests;
  params->oride.doc_in_cache_skip_dns      = INT_TO_BOOL(m_master.oride.doc_in_cache_skip_dns);
  params->oride.default_buffer_size_index  = m_master.oride.default_buffer_size_index;
  params->oride.default_buffer_water_mark  = m_master.oride.default_buffer_water_mark;
  params->enable_http_info                 = INT_TO_BOOL(m_master.enable_http_info);
  params->oride.body_factory_template_base = ats_strdup(m_master.oride.body_factory_template_base);
  params->oride.body_factory_template_base_len =
    params->oride.body_factory_template_base ? strlen(params->oride.body_factory_template_base) : 0;
  params->body_factory_response_max_size = m_master.body_factory_response_max_size;
  params->reverse_proxy_no_host_redirect = ats_strdup(m_master.reverse_proxy_no_host_redirect);
  params->reverse_proxy_no_host_redirect_len =
    params->reverse_proxy_no_host_redirect ? strlen(params->reverse_proxy_no_host_redirect) : 0;

  params->referer_filter_enabled  = INT_TO_BOOL(m_master.referer_filter_enabled);
  params->referer_format_redirect = INT_TO_BOOL(m_master.referer_format_redirect);

  params->strict_uri_parsing = INT_TO_BOOL(m_master.strict_uri_parsing);

  params->oride.down_server_timeout    = m_master.oride.down_server_timeout;
  params->oride.client_abort_threshold = m_master.oride.client_abort_threshold;

  params->oride.negative_caching_enabled       = INT_TO_BOOL(m_master.oride.negative_caching_enabled);
  params->oride.negative_caching_lifetime      = m_master.oride.negative_caching_lifetime;
  params->oride.negative_revalidating_enabled  = INT_TO_BOOL(m_master.oride.negative_revalidating_enabled);
  params->oride.negative_revalidating_lifetime = m_master.oride.negative_revalidating_lifetime;

  params->oride.redirect_use_orig_cache_key = INT_TO_BOOL(m_master.oride.redirect_use_orig_cache_key);
  params->redirection_host_no_port          = INT_TO_BOOL(m_master.redirection_host_no_port);
  params->oride.number_of_redirections      = m_master.oride.number_of_redirections;
  params->post_copy_size                    = m_master.post_copy_size;
  params->redirect_actions_string           = ats_strdup(m_master.redirect_actions_string);
  params->redirect_actions_map = parse_redirect_actions(params->redirect_actions_string, params->redirect_actions_self_action);

  params->oride.ssl_client_sni_policy = ats_strdup(m_master.oride.ssl_client_sni_policy);

  params->negative_caching_list = m_master.negative_caching_list;

  m_id = configProcessor.set(m_id, params);
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::acquire()
//
////////////////////////////////////////////////////////////////
HttpConfigParams *
HttpConfig::acquire()
{
  if (m_id != 0) {
    return (HttpConfigParams *)configProcessor.get(m_id);
  } else {
    return nullptr;
  }
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::release()
//
////////////////////////////////////////////////////////////////
void
HttpConfig::release(HttpConfigParams *params)
{
  configProcessor.release(m_id, params);
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::parse_ports_list()
//
////////////////////////////////////////////////////////////////
HttpConfigPortRange *
HttpConfig::parse_ports_list(char *ports_string)
{
  HttpConfigPortRange *ports_list = nullptr;

  if (!ports_string) {
    return (nullptr);
  }

  if (strchr(ports_string, '*')) {
    ports_list       = new HttpConfigPortRange;
    ports_list->low  = -1;
    ports_list->high = -1;
    ports_list->next = nullptr;
  } else {
    HttpConfigPortRange *pr, *prev;
    char *start;
    char *end;

    pr   = nullptr;
    prev = nullptr;

    start = ports_string;

    while (true) { // eat whitespace
      while ((start[0] != '\0') && ParseRules::is_space(start[0])) {
        start++;
      }

      // locate the end of the next number
      end = start;
      while ((end[0] != '\0') && ParseRules::is_digit(end[0])) {
        end++;
      }

      // if there is no next number we're done
      if (start == end) {
        break;
      }

      pr       = new HttpConfigPortRange;
      pr->low  = atoi(start);
      pr->high = pr->low;
      pr->next = nullptr;

      if (prev) {
        prev->next = pr;
      } else {
        ports_list = pr;
      }
      prev = pr;

      // if the next character after the current port
      //  number is a dash then we are parsing a range
      if (end[0] == '-') {
        start = end + 1;
        while ((start[0] != '\0') && ParseRules::is_space(start[0])) {
          start++;
        }

        end = start;
        while ((end[0] != '\0') && ParseRules::is_digit(end[0])) {
          end++;
        }

        if (start == end) {
          break;
        }

        pr->high = atoi(start);
      }

      start = end;

      ink_release_assert(pr->low <= pr->high);
    }
  }
  return (ports_list);
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::parse_redirect_actions()
//
////////////////////////////////////////////////////////////////
IpMap *
HttpConfig::parse_redirect_actions(char *input_string, RedirectEnabled::Action &self_action)
{
  using RedirectEnabled::Action;
  using RedirectEnabled::AddressClass;
  using RedirectEnabled::action_map;
  using RedirectEnabled::address_class_map;

  if (nullptr == input_string) {
    Emergency("parse_redirect_actions: The configuration value is empty.");
    return nullptr;
  }
  Tokenizer configTokens(", ");
  int n_rules = configTokens.Initialize(input_string);
  std::map<AddressClass, Action> configMapping;
  for (int i = 0; i < n_rules; i++) {
    const char *rule = configTokens[i];
    Tokenizer ruleTokens(":");
    int n_mapping = ruleTokens.Initialize(rule);
    if (2 != n_mapping) {
      Emergency("parse_redirect_actions: Individual rules must be an address class and an action separated by a colon (:)");
      return nullptr;
    }
    std::string c_input(ruleTokens[0]), a_input(ruleTokens[1]);
    AddressClass c =
      address_class_map.find(ruleTokens[0]) != address_class_map.end() ? address_class_map[ruleTokens[0]] : AddressClass::INVALID;
    Action a = action_map.find(ruleTokens[1]) != action_map.end() ? action_map[ruleTokens[1]] : Action::INVALID;

    if (AddressClass::INVALID == c) {
      Emergency("parse_redirect_actions: '%.*s' is not a valid address class", static_cast<int>(c_input.size()), c_input.data());
      return nullptr;
    } else if (Action::INVALID == a) {
      Emergency("parse_redirect_actions: '%.*s' is not a valid action", static_cast<int>(a_input.size()), a_input.data());
      return nullptr;
    }
    configMapping[c] = a;
  }

  // Ensure the default.
  if (configMapping.end() == configMapping.find(AddressClass::DEFAULT)) {
    configMapping[AddressClass::DEFAULT] = Action::RETURN;
  }

  IpMap *ret = new IpMap();
  IpAddr min, max;
  Action action = Action::INVALID;

  // Order Matters. IpAddr::mark uses Painter's Algorithm. Last one wins.

  // PRIVATE
  action = configMapping.find(AddressClass::PRIVATE) != configMapping.end() ? configMapping[AddressClass::PRIVATE] :
                                                                              configMapping[AddressClass::DEFAULT];
  // 10.0.0.0/8
  min.load("10.0.0.0");
  max.load("10.255.255.255");
  ret->mark(min, max, reinterpret_cast<void *>(action));
  // 100.64.0.0/10
  min.load("100.64.0.0");
  max.load("100.127.255.255");
  ret->mark(min, max, reinterpret_cast<void *>(action));
  // 172.16.0.0/12
  min.load("172.16.0.0");
  max.load("172.31.255.255");
  ret->mark(min, max, reinterpret_cast<void *>(action));
  // 192.168.0.0/16
  min.load("192.168.0.0");
  max.load("192.168.255.255");
  ret->mark(min, max, reinterpret_cast<void *>(action));
  // fc00::/7
  min.load("fc00::");
  max.load("feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
  ret->mark(min, max, reinterpret_cast<void *>(action));

  // LOOPBACK
  action = configMapping.find(AddressClass::LOOPBACK) != configMapping.end() ? configMapping[AddressClass::LOOPBACK] :
                                                                               configMapping[AddressClass::DEFAULT];
  min.load("127.0.0.0");
  max.load("127.255.255.255");
  ret->mark(min, max, reinterpret_cast<void *>(action));
  min.load("::1");
  max.load("::1");
  ret->mark(min, max, reinterpret_cast<void *>(action));

  // MULTICAST
  action = configMapping.find(AddressClass::MULTICAST) != configMapping.end() ? configMapping[AddressClass::MULTICAST] :
                                                                                configMapping[AddressClass::DEFAULT];
  // 224.0.0.0/4
  min.load("224.0.0.0");
  max.load("239.255.255.255");
  ret->mark(min, max, reinterpret_cast<void *>(action));
  // ff00::/8
  min.load("ff00::");
  max.load("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
  ret->mark(min, max, reinterpret_cast<void *>(action));

  // LINKLOCAL
  action = configMapping.find(AddressClass::LINKLOCAL) != configMapping.end() ? configMapping[AddressClass::LINKLOCAL] :
                                                                                configMapping[AddressClass::DEFAULT];
  // 169.254.0.0/16
  min.load("169.254.0.0");
  max.load("169.254.255.255");
  ret->mark(min, max, reinterpret_cast<void *>(action));
  // fe80::/10
  min.load("fe80::");
  max.load("febf:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
  ret->mark(min, max, reinterpret_cast<void *>(action));

  // SELF
  // We must store the self address class separately instead of adding the addresses to our map.
  // The addresses Trafficserver will use depend on configurations that are loaded here, so they are not available yet.
  action = configMapping.find(AddressClass::SELF) != configMapping.end() ? configMapping[AddressClass::SELF] :
                                                                           configMapping[AddressClass::DEFAULT];
  self_action = action;

  // IpMap::fill only marks things that are not already marked.

  // ROUTABLE
  action = configMapping.find(AddressClass::ROUTABLE) != configMapping.end() ? configMapping[AddressClass::ROUTABLE] :
                                                                               configMapping[AddressClass::DEFAULT];
  min.load("0.0.0.0");
  max.load("255.255.255.255");
  ret->fill(min, max, reinterpret_cast<void *>(action));
  min.load("::");
  max.load("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
  ret->fill(min, max, reinterpret_cast<void *>(action));

  return ret;
}
