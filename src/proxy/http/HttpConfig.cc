/** @file
 *

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

#include <deque>

#include "tscore/ink_config.h"
#include "tscore/Filenames.h"
#include "tscore/Tokenizer.h"
#include <cctype>
#include <cstring>
#include "proxy/http/HttpConfig.h"
#include "proxy/hdrs/HTTP.h"
#include "iocore/eventsystem/ConfigProcessor.h"
#include "iocore/eventsystem/UnixSocket.h"
#include "../../iocore/net/P_Net.h"
#include "../../records/P_RecUtils.h"
#include "records/RecHttp.h"
#include "proxy/http/HttpSessionManager.h"

#define HttpEstablishStaticConfigStringAlloc(_ix, _n) \
  RecEstablishStaticConfigString(_ix, _n);            \
  RecRegisterConfigUpdateCb(_n, http_config_cb, NULL)

#define HttpEstablishStaticConfigLongLong(_ix, _n) \
  RecEstablishStaticConfigInt(_ix, _n);            \
  RecRegisterConfigUpdateCb(_n, http_config_cb, NULL)

#define HttpEstablishStaticConfigFloat(_ix, _n) \
  RecEstablishStaticConfigFloat(_ix, _n);       \
  RecRegisterConfigUpdateCb(_n, http_config_cb, NULL)

#define HttpEstablishStaticConfigByte(_ix, _n) \
  RecEstablishStaticConfigByte(_ix, _n);       \
  RecRegisterConfigUpdateCb(_n, http_config_cb, NULL)

namespace
{
DbgCtl dbg_ctl_http_config{"http_config"};

} // end anonymous namespace

class HttpConfigCont : public Continuation
{
public:
  HttpConfigCont();
  int handle_event(int event, void *edata);
};

/// Data item for enumerated type config value.
template <typename T> struct ConfigEnumPair {
  T           _value;
  const char *_key;
};

/// Convert a string to an enumeration value.
/// @a n is the number of entries in the list.
/// @return @c true if the string is found, @c false if not found.
/// If found @a value is set to the corresponding value in @a list.
template <typename T, unsigned N>
static bool
http_config_enum_search(std::string_view key, const ConfigEnumPair<T> (&list)[N], MgmtByte &value)
{
  Dbg(dbg_ctl_http_config, "enum element %.*s", static_cast<int>(key.size()), key.data());
  // We don't expect any of these lists to be more than 10 long, so a linear search is the best choice.
  for (unsigned i = 0; i < N; ++i) {
    if (key.compare(list[i]._key) == 0) {
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
  char key_buf[512]; // it's just one key - painful UI if keys are longer than this
  if (auto key{RecGetRecordString(name, key_buf, sizeof(key_buf))}; key) {
    return http_config_enum_search(key.value(), list, value);
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
  {TS_SERVER_SESSION_SHARING_MATCH_NONE,     "none"    },
  {TS_SERVER_SESSION_SHARING_MATCH_IP,       "ip"      },
  {TS_SERVER_SESSION_SHARING_MATCH_HOST,     "host"    },
  {TS_SERVER_SESSION_SHARING_MATCH_HOST,     "hostsni" },
  {TS_SERVER_SESSION_SHARING_MATCH_BOTH,     "both"    },
  {TS_SERVER_SESSION_SHARING_MATCH_HOSTONLY, "hostonly"},
  {TS_SERVER_SESSION_SHARING_MATCH_SNI,      "sni"     },
  {TS_SERVER_SESSION_SHARING_MATCH_CERT,     "cert"    }
};

bool
HttpConfig::load_server_session_sharing_match(std::string_view key, MgmtByte &mask)
{
  MgmtByte value;
  mask = 0;
  // Parse through and build up mask
  size_t start  = 0;
  size_t offset = 0;
  Dbg(dbg_ctl_http_config, "enum mask value %.*s", static_cast<int>(key.length()), key.data());
  do {
    offset = key.find(',', start);
    if (offset == std::string_view::npos) {
      std::string_view one_key = key.substr(start);
      if (!http_config_enum_search(one_key, SessionSharingMatchStrings, value)) {
        return false;
      }
    } else {
      std::string_view one_key = key.substr(start, offset - start);
      if (!http_config_enum_search(one_key, SessionSharingMatchStrings, value)) {
        return false;
      }
      start = offset + 1;
    }
    if (value < TS_SERVER_SESSION_SHARING_MATCH_NONE) {
      mask |= (1 << value);
    } else if (value == TS_SERVER_SESSION_SHARING_MATCH_BOTH) {
      mask |= TS_SERVER_SESSION_SHARING_MATCH_MASK_IP | TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY |
              TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTSNISYNC;
    } else if (value == TS_SERVER_SESSION_SHARING_MATCH_HOST) {
      mask |= TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY | TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTSNISYNC;
    }
  } while (offset != std::string_view::npos);
  return true;
}

static bool
http_config_enum_mask_read(const char *name, MgmtByte &value)
{
  char key_buf[512]; // it's just one key - painful UI if keys are longer than this
  if (auto key{RecGetRecordString(name, key_buf, sizeof(key_buf))}; key) {
    return HttpConfig::load_server_session_sharing_match(key.value(), value);
  }
  return false;
}

static const ConfigEnumPair<TSServerSessionSharingPoolType> SessionSharingPoolStrings[] = {
  {TS_SERVER_SESSION_SHARING_POOL_GLOBAL,        "global"       },
  {TS_SERVER_SESSION_SHARING_POOL_THREAD,        "thread"       },
  {TS_SERVER_SESSION_SHARING_POOL_HYBRID,        "hybrid"       },
  {TS_SERVER_SESSION_SHARING_POOL_GLOBAL_LOCKED, "global_locked"},
};

int              HttpConfig::m_id = 0;
HttpConfigParams HttpConfig::m_master;

static int             http_config_changes = 1;
static HttpConfigCont *http_config_cont    = nullptr;

HttpConfigCont::HttpConfigCont() : Continuation(new_ProxyMutex())
{
  SET_HANDLER(&HttpConfigCont::handle_event);
}

int
HttpConfigCont::handle_event(int /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  if (ink_atomic_increment(&http_config_changes, -1) == 1) {
    HttpConfig::reconfigure();
  }
  return 0;
}

int
http_config_cb(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData /* data ATS_UNUSED */,
               void * /* cookie ATS_UNUSED */)
{
  ink_atomic_increment(&http_config_changes, 1);

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
  bool              valid_p = true;
  HttpConfigParams *c       = static_cast<HttpConfigParams *>(cookie);

  if (0 == strcasecmp("proxy.config.http.server_session_sharing.match", name)) {
    MgmtByte &match = c->oride.server_session_sharing_match;
    if (RECD_INT == dtype) {
      match = static_cast<TSServerSessionSharingMatchType>(data.rec_int);
    } else if (RECD_STRING == dtype && HttpConfig::load_server_session_sharing_match(data.rec_string, match)) {
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
  bool              valid_p = false;
  HttpConfigParams *c       = static_cast<HttpConfigParams *>(cookie);

  if (0 == strcasecmp("proxy.config.http.insert_forwarded", name)) {
    if (RECD_STRING == dtype) {
      swoc::LocalBufferWriter<1024> error;
      HttpForwarded::OptionBitSet   bs = HttpForwarded::optStrToBitset(std::string_view(data.rec_string), error);
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
  http_rsb.background_fill_bytes_aborted     = Metrics::Counter::createPtr("proxy.process.http.background_fill_bytes_aborted");
  http_rsb.background_fill_bytes_completed   = Metrics::Counter::createPtr("proxy.process.http.background_fill_bytes_completed");
  http_rsb.background_fill_current_count     = Metrics::Gauge::createPtr("proxy.process.http.background_fill_current_count");
  http_rsb.background_fill_total_count       = Metrics::Counter::createPtr("proxy.process.http.background_fill_total_count");
  http_rsb.broken_server_connections         = Metrics::Counter::createPtr("proxy.process.http.broken_server_connections");
  http_rsb.cache_deletes                     = Metrics::Counter::createPtr("proxy.process.http.cache_deletes");
  http_rsb.cache_hit_fresh                   = Metrics::Counter::createPtr("proxy.process.http.cache_hit_fresh");
  http_rsb.cache_hit_ims                     = Metrics::Counter::createPtr("proxy.process.http.cache_hit_ims");
  http_rsb.cache_hit_mem_fresh               = Metrics::Counter::createPtr("proxy.process.http.cache_hit_mem_fresh");
  http_rsb.cache_hit_reval                   = Metrics::Counter::createPtr("proxy.process.http.cache_hit_revalidated");
  http_rsb.cache_hit_rww                     = Metrics::Counter::createPtr("proxy.process.http.cache_hit_rww");
  http_rsb.cache_hit_stale_served            = Metrics::Counter::createPtr("proxy.process.http.cache_hit_stale_served");
  http_rsb.cache_lookups                     = Metrics::Counter::createPtr("proxy.process.http.cache_lookups");
  http_rsb.cache_miss_changed                = Metrics::Counter::createPtr("proxy.process.http.cache_miss_changed");
  http_rsb.cache_miss_client_no_cache        = Metrics::Counter::createPtr("proxy.process.http.cache_miss_client_no_cache");
  http_rsb.cache_miss_cold                   = Metrics::Counter::createPtr("proxy.process.http.cache_miss_cold");
  http_rsb.cache_miss_ims                    = Metrics::Counter::createPtr("proxy.process.http.cache_miss_ims");
  http_rsb.cache_miss_uncacheable            = Metrics::Counter::createPtr("proxy.process.http.cache_miss_client_not_cacheable");
  http_rsb.cache_open_read_begin_time        = Metrics::Counter::createPtr("proxy.process.http.milestone.cache_open_read_begin");
  http_rsb.cache_open_read_end_time          = Metrics::Counter::createPtr("proxy.process.http.milestone.cache_open_read_end");
  http_rsb.cache_open_write_adjust_thread    = Metrics::Counter::createPtr("proxy.process.http.cache.open_write.adjust_thread");
  http_rsb.cache_open_write_begin_time       = Metrics::Counter::createPtr("proxy.process.http.milestone.cache_open_write_begin");
  http_rsb.cache_open_write_end_time         = Metrics::Counter::createPtr("proxy.process.http.milestone.cache_open_write_end");
  http_rsb.cache_open_write_fail_count       = Metrics::Counter::createPtr("proxy.process.http.cache_open_write_fail_count");
  http_rsb.cache_read_error                  = Metrics::Counter::createPtr("proxy.process.http.cache_read_error");
  http_rsb.cache_read_errors                 = Metrics::Counter::createPtr("proxy.process.http.cache_read_errors");
  http_rsb.cache_updates                     = Metrics::Counter::createPtr("proxy.process.http.cache_updates");
  http_rsb.cache_write_errors                = Metrics::Counter::createPtr("proxy.process.http.cache_write_errors");
  http_rsb.cache_writes                      = Metrics::Counter::createPtr("proxy.process.http.cache_writes");
  http_rsb.completed_requests                = Metrics::Counter::createPtr("proxy.process.http.completed_requests");
  http_rsb.connect_requests                  = Metrics::Counter::createPtr("proxy.process.http.connect_requests");
  http_rsb.current_active_client_connections = Metrics::Gauge::createPtr("proxy.process.http.current_active_client_connections");
  http_rsb.current_cache_connections         = Metrics::Gauge::createPtr("proxy.process.http.current_cache_connections");
  http_rsb.current_client_connections        = Metrics::Gauge::createPtr("proxy.process.http.current_client_connections");
  http_rsb.current_client_transactions       = Metrics::Gauge::createPtr("proxy.process.http.current_client_transactions");
  http_rsb.current_parent_proxy_connections  = Metrics::Gauge::createPtr("proxy.process.http.current_parent_proxy_connections");
  http_rsb.current_server_connections        = Metrics::Gauge::createPtr("proxy.process.http.current_server_connections");
  http_rsb.current_server_transactions       = Metrics::Gauge::createPtr("proxy.process.http.current_server_transactions");
  http_rsb.delete_requests                   = Metrics::Counter::createPtr("proxy.process.http.delete_requests");
  http_rsb.disallowed_post_100_continue      = Metrics::Counter::createPtr("proxy.process.http.disallowed_post_100_continue");
  http_rsb.dns_lookup_begin_time             = Metrics::Counter::createPtr("proxy.process.http.milestone.dns_lookup_begin");
  http_rsb.dns_lookup_end_time               = Metrics::Counter::createPtr("proxy.process.http.milestone.dns_lookup_end");
  http_rsb.down_server_no_requests           = Metrics::Counter::createPtr("proxy.process.http.down_server.no_requests");
  http_rsb.err_client_abort_count            = Metrics::Counter::createPtr("proxy.process.http.err_client_abort_count");
  http_rsb.err_client_abort_origin_server_bytes =
    Metrics::Counter::createPtr("proxy.process.http.err_client_abort_origin_server_bytes");
  http_rsb.err_client_abort_user_agent_bytes = Metrics::Counter::createPtr("proxy.process.http.err_client_abort_user_agent_bytes");
  http_rsb.err_client_read_error_count       = Metrics::Counter::createPtr("proxy.process.http.err_client_read_error_count");
  http_rsb.err_client_read_error_origin_server_bytes =
    Metrics::Counter::createPtr("proxy.process.http.err_client_read_error_origin_server_bytes");
  http_rsb.err_client_read_error_user_agent_bytes =
    Metrics::Counter::createPtr("proxy.process.http.err_client_read_error_user_agent_bytes");
  http_rsb.err_connect_fail_count = Metrics::Counter::createPtr("proxy.process.http.err_connect_fail_count");
  http_rsb.err_connect_fail_origin_server_bytes =
    Metrics::Counter::createPtr("proxy.process.http.err_connect_fail_origin_server_bytes");
  http_rsb.err_connect_fail_user_agent_bytes = Metrics::Counter::createPtr("proxy.process.http.err_connect_fail_user_agent_bytes");
  http_rsb.extension_method_requests         = Metrics::Counter::createPtr("proxy.process.http.extension_method_requests");
  http_rsb.get_requests                      = Metrics::Counter::createPtr("proxy.process.http.get_requests");
  http_rsb.head_requests                     = Metrics::Counter::createPtr("proxy.process.http.head_requests");
  http_rsb.https_incoming_requests           = Metrics::Counter::createPtr("proxy.process.https.incoming_requests");
  http_rsb.https_total_client_connections    = Metrics::Counter::createPtr("proxy.process.https.total_client_connections");
  http_rsb.incoming_requests                 = Metrics::Counter::createPtr("proxy.process.http.incoming_requests");
  http_rsb.incoming_responses                = Metrics::Counter::createPtr("proxy.process.http.incoming_responses");
  http_rsb.invalid_client_requests           = Metrics::Counter::createPtr("proxy.process.http.invalid_client_requests");
  http_rsb.misc_count                        = Metrics::Counter::createPtr("proxy.process.http.misc_count");
  http_rsb.misc_origin_server_bytes          = Metrics::Counter::createPtr("proxy.process.http.http_misc_origin_server_bytes");
  http_rsb.misc_user_agent_bytes             = Metrics::Counter::createPtr("proxy.process.http.misc_user_agent_bytes");
  http_rsb.missing_host_hdr                  = Metrics::Counter::createPtr("proxy.process.http.missing_host_hdr");
  http_rsb.no_remap_matched                  = Metrics::Counter::createPtr("proxy.process.http.no_remap_matched");
  http_rsb.options_requests                  = Metrics::Counter::createPtr("proxy.process.http.options_requests");
  http_rsb.origin_body                       = Metrics::Counter::createPtr("proxy.process.http.origin.body");
  http_rsb.origin_close_private              = Metrics::Counter::createPtr("proxy.process.http.origin.close_private");
  http_rsb.origin_connect_adjust_thread      = Metrics::Counter::createPtr("proxy.process.http.origin.connect.adjust_thread");
  http_rsb.origin_connections_throttled      = Metrics::Counter::createPtr("proxy.process.http.origin_connections_throttled_out");
  http_rsb.origin_make_new                   = Metrics::Counter::createPtr("proxy.process.http.origin.make_new");
  http_rsb.origin_no_sharing                 = Metrics::Counter::createPtr("proxy.process.http.origin.no_sharing");
  http_rsb.origin_not_found                  = Metrics::Counter::createPtr("proxy.process.http.origin.not_found");
  http_rsb.origin_private                    = Metrics::Counter::createPtr("proxy.process.http.origin.private");
  http_rsb.origin_raw                        = Metrics::Counter::createPtr("proxy.process.http.origin.raw");
  http_rsb.origin_reuse                      = Metrics::Counter::createPtr("proxy.process.http.origin.reuse");
  http_rsb.origin_reuse_fail                 = Metrics::Counter::createPtr("proxy.process.http.origin.reuse_fail");
  http_rsb.origin_server_request_document_total_size =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_request_document_total_size");
  http_rsb.origin_server_request_header_total_size =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_request_header_total_size");
  http_rsb.origin_server_response_document_total_size =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_response_document_total_size");
  http_rsb.origin_server_response_header_total_size =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_response_header_total_size");
  http_rsb.origin_shutdown_cleanup_entry     = Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.cleanup_entry");
  http_rsb.origin_shutdown_migration_failure = Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.migration_failure");
  http_rsb.origin_shutdown_pool_lock_contention =
    Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.pool_lock_contention");
  http_rsb.origin_shutdown_release_invalid_request =
    Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.release_invalid_request");
  http_rsb.origin_shutdown_release_invalid_response =
    Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.release_invalid_response");
  http_rsb.origin_shutdown_release_misc     = Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.release_misc");
  http_rsb.origin_shutdown_release_modified = Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.release_modified");
  http_rsb.origin_shutdown_release_no_keep_alive =
    Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.release_no_keep_alive");
  http_rsb.origin_shutdown_release_no_server = Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.release_no_server");
  http_rsb.origin_shutdown_release_no_sharing =
    Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.release_no_sharing");
  http_rsb.origin_shutdown_tunnel_abort  = Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.tunnel_abort");
  http_rsb.origin_shutdown_tunnel_client = Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.tunnel_client");
  http_rsb.origin_shutdown_tunnel_server = Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.tunnel_server");
  http_rsb.origin_shutdown_tunnel_server_detach =
    Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.tunnel_server_detach");
  http_rsb.origin_shutdown_tunnel_server_eos = Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.tunnel_server_eos");
  http_rsb.origin_shutdown_tunnel_server_no_keep_alive =
    Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.tunnel_server_no_keep_alive");
  http_rsb.origin_shutdown_tunnel_server_plugin_tunnel =
    Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.tunnel_server_plugin_tunnel");
  http_rsb.origin_shutdown_tunnel_transform_read =
    Metrics::Counter::createPtr("proxy.process.http.origin_shutdown.tunnel_transform_read");
  http_rsb.outgoing_requests                 = Metrics::Counter::createPtr("proxy.process.http.outgoing_requests");
  http_rsb.parent_count                      = Metrics::Counter::createPtr("proxy.process.http_parent_count");
  http_rsb.parent_proxy_request_total_bytes  = Metrics::Counter::createPtr("proxy.process.http.parent_proxy_request_total_bytes");
  http_rsb.parent_proxy_response_total_bytes = Metrics::Counter::createPtr("proxy.process.http.parent_proxy_response_total_bytes");
  http_rsb.parent_proxy_transaction_time     = Metrics::Counter::createPtr("proxy.process.http.parent_proxy_transaction_time");
  http_rsb.pooled_server_connections         = Metrics::Gauge::createPtr("proxy.process.http.pooled_server_connections");
  http_rsb.post_body_too_large               = Metrics::Counter::createPtr("proxy.process.http.post_body_too_large");
  http_rsb.post_requests                     = Metrics::Counter::createPtr("proxy.process.http.post_requests");
  http_rsb.proxy_loop_detected               = Metrics::Counter::createPtr("proxy.process.http.http_proxy_loop_detected");
  http_rsb.proxy_mh_loop_detected            = Metrics::Counter::createPtr("proxy.process.http.http_proxy_mh_loop_detected");
  http_rsb.purge_requests                    = Metrics::Counter::createPtr("proxy.process.http.purge_requests");
  http_rsb.push_requests                     = Metrics::Counter::createPtr("proxy.process.http.push_requests");
  http_rsb.pushed_document_total_size        = Metrics::Counter::createPtr("proxy.process.http.pushed_document_total_size");
  http_rsb.pushed_response_header_total_size = Metrics::Counter::createPtr("proxy.process.http.pushed_response_header_total_size");
  http_rsb.put_requests                      = Metrics::Counter::createPtr("proxy.process.http.put_requests");
  http_rsb.response_status_100_count         = Metrics::Counter::createPtr("proxy.process.http.100_responses");
  http_rsb.response_status_101_count         = Metrics::Counter::createPtr("proxy.process.http.101_responses");
  http_rsb.response_status_1xx_count         = Metrics::Counter::createPtr("proxy.process.http.1xx_responses");
  http_rsb.response_status_200_count         = Metrics::Counter::createPtr("proxy.process.http.200_responses");
  http_rsb.response_status_201_count         = Metrics::Counter::createPtr("proxy.process.http.201_responses");
  http_rsb.response_status_202_count         = Metrics::Counter::createPtr("proxy.process.http.202_responses");
  http_rsb.response_status_203_count         = Metrics::Counter::createPtr("proxy.process.http.203_responses");
  http_rsb.response_status_204_count         = Metrics::Counter::createPtr("proxy.process.http.204_responses");
  http_rsb.response_status_205_count         = Metrics::Counter::createPtr("proxy.process.http.205_responses");
  http_rsb.response_status_206_count         = Metrics::Counter::createPtr("proxy.process.http.206_responses");
  http_rsb.response_status_2xx_count         = Metrics::Counter::createPtr("proxy.process.http.2xx_responses");
  http_rsb.response_status_300_count         = Metrics::Counter::createPtr("proxy.process.http.300_responses");
  http_rsb.response_status_301_count         = Metrics::Counter::createPtr("proxy.process.http.301_responses");
  http_rsb.response_status_302_count         = Metrics::Counter::createPtr("proxy.process.http.302_responses");
  http_rsb.response_status_303_count         = Metrics::Counter::createPtr("proxy.process.http.303_responses");
  http_rsb.response_status_304_count         = Metrics::Counter::createPtr("proxy.process.http.304_responses");
  http_rsb.response_status_305_count         = Metrics::Counter::createPtr("proxy.process.http.305_responses");
  http_rsb.response_status_307_count         = Metrics::Counter::createPtr("proxy.process.http.307_responses");
  http_rsb.response_status_308_count         = Metrics::Counter::createPtr("proxy.process.http.308_responses");
  http_rsb.response_status_3xx_count         = Metrics::Counter::createPtr("proxy.process.http.3xx_responses");
  http_rsb.response_status_400_count         = Metrics::Counter::createPtr("proxy.process.http.400_responses");
  http_rsb.response_status_401_count         = Metrics::Counter::createPtr("proxy.process.http.401_responses");
  http_rsb.response_status_402_count         = Metrics::Counter::createPtr("proxy.process.http.402_responses");
  http_rsb.response_status_403_count         = Metrics::Counter::createPtr("proxy.process.http.403_responses");
  http_rsb.response_status_404_count         = Metrics::Counter::createPtr("proxy.process.http.404_responses");
  http_rsb.response_status_405_count         = Metrics::Counter::createPtr("proxy.process.http.405_responses");
  http_rsb.response_status_406_count         = Metrics::Counter::createPtr("proxy.process.http.406_responses");
  http_rsb.response_status_407_count         = Metrics::Counter::createPtr("proxy.process.http.407_responses");
  http_rsb.response_status_408_count         = Metrics::Counter::createPtr("proxy.process.http.408_responses");
  http_rsb.response_status_409_count         = Metrics::Counter::createPtr("proxy.process.http.409_responses");
  http_rsb.response_status_410_count         = Metrics::Counter::createPtr("proxy.process.http.410_responses");
  http_rsb.response_status_411_count         = Metrics::Counter::createPtr("proxy.process.http.411_responses");
  http_rsb.response_status_412_count         = Metrics::Counter::createPtr("proxy.process.http.412_responses");
  http_rsb.response_status_413_count         = Metrics::Counter::createPtr("proxy.process.http.413_responses");
  http_rsb.response_status_414_count         = Metrics::Counter::createPtr("proxy.process.http.414_responses");
  http_rsb.response_status_415_count         = Metrics::Counter::createPtr("proxy.process.http.415_responses");
  http_rsb.response_status_416_count         = Metrics::Counter::createPtr("proxy.process.http.416_responses");
  http_rsb.response_status_4xx_count         = Metrics::Counter::createPtr("proxy.process.http.4xx_responses");
  http_rsb.response_status_500_count         = Metrics::Counter::createPtr("proxy.process.http.500_responses");
  http_rsb.response_status_501_count         = Metrics::Counter::createPtr("proxy.process.http.501_responses");
  http_rsb.response_status_502_count         = Metrics::Counter::createPtr("proxy.process.http.502_responses");
  http_rsb.response_status_503_count         = Metrics::Counter::createPtr("proxy.process.http.503_responses");
  http_rsb.response_status_504_count         = Metrics::Counter::createPtr("proxy.process.http.504_responses");
  http_rsb.response_status_505_count         = Metrics::Counter::createPtr("proxy.process.http.505_responses");
  http_rsb.response_status_5xx_count         = Metrics::Counter::createPtr("proxy.process.http.5xx_responses");
  http_rsb.server_begin_write_time           = Metrics::Counter::createPtr("proxy.process.http.milestone.server_begin_write");
  http_rsb.server_close_time                 = Metrics::Counter::createPtr("proxy.process.http.milestone.server_close");
  http_rsb.server_connect_end_time           = Metrics::Counter::createPtr("proxy.process.http.milestone.server_connect_end");
  http_rsb.server_connect_time               = Metrics::Counter::createPtr("proxy.process.http.milestone.server_connect");
  http_rsb.server_first_connect_time         = Metrics::Counter::createPtr("proxy.process.http.milestone.server_first_connect");
  http_rsb.server_first_read_time            = Metrics::Counter::createPtr("proxy.process.http.milestone.server_first_read");
  http_rsb.server_read_header_done_time      = Metrics::Counter::createPtr("proxy.process.http.milestone.server_read_header_done");
  http_rsb.sm_finish_time                    = Metrics::Counter::createPtr("proxy.process.http.milestone.sm_finish");
  http_rsb.sm_start_time                     = Metrics::Counter::createPtr("proxy.process.http.milestone.sm_start");
  http_rsb.tcp_client_refresh_count          = Metrics::Counter::createPtr("proxy.process.http.tcp_client_refresh_count");
  http_rsb.tcp_client_refresh_origin_server_bytes =
    Metrics::Counter::createPtr("proxy.process.http.tcp_client_refresh_origin_server_bytes");
  http_rsb.tcp_client_refresh_user_agent_bytes =
    Metrics::Counter::createPtr("proxy.process.http.tcp_client_refresh_user_agent_bytes");
  http_rsb.tcp_expired_miss_count = Metrics::Counter::createPtr("proxy.process.http.tcp_expired_miss_count");
  http_rsb.tcp_expired_miss_origin_server_bytes =
    Metrics::Counter::createPtr("proxy.process.http.tcp_expired_miss_origin_server_bytes");
  http_rsb.tcp_expired_miss_user_agent_bytes = Metrics::Counter::createPtr("proxy.process.http.tcp_expired_miss_user_agent_bytes");
  http_rsb.tcp_hit_count                     = Metrics::Counter::createPtr("proxy.process.http.tcp_hit_count");
  http_rsb.tcp_hit_origin_server_bytes       = Metrics::Counter::createPtr("proxy.process.http.tcp_hit_origin_server_bytes");
  http_rsb.tcp_hit_user_agent_bytes          = Metrics::Counter::createPtr("proxy.process.http.tcp_hit_user_agent_bytes");
  http_rsb.tcp_ims_hit_count                 = Metrics::Counter::createPtr("proxy.process.http.tcp_ims_hit_count");
  http_rsb.tcp_ims_hit_origin_server_bytes   = Metrics::Counter::createPtr("proxy.process.http.tcp_ims_hit_origin_server_bytes");
  http_rsb.tcp_ims_hit_user_agent_bytes      = Metrics::Counter::createPtr("proxy.process.http.tcp_ims_hit_user_agent_bytes");
  http_rsb.tcp_ims_miss_count                = Metrics::Counter::createPtr("proxy.process.http.tcp_ims_miss_count");
  http_rsb.tcp_ims_miss_origin_server_bytes  = Metrics::Counter::createPtr("proxy.process.http.tcp_ims_miss_origin_server_bytes");
  http_rsb.tcp_ims_miss_user_agent_bytes     = Metrics::Counter::createPtr("proxy.process.http.tcp_ims_miss_user_agent_bytes");
  http_rsb.tcp_miss_count                    = Metrics::Counter::createPtr("proxy.process.http.tcp_miss_count");
  http_rsb.tcp_miss_origin_server_bytes      = Metrics::Counter::createPtr("proxy.process.http.tcp_miss_origin_server_bytes");
  http_rsb.tcp_miss_user_agent_bytes         = Metrics::Counter::createPtr("proxy.process.http.tcp_miss_user_agent_bytes");
  http_rsb.tcp_refresh_hit_count             = Metrics::Counter::createPtr("proxy.process.http.tcp_refresh_hit_count");
  http_rsb.tcp_refresh_hit_origin_server_bytes =
    Metrics::Counter::createPtr("proxy.process.http.tcp_refresh_hit_origin_server_bytes");
  http_rsb.tcp_refresh_hit_user_agent_bytes = Metrics::Counter::createPtr("proxy.process.http.tcp_refresh_hit_user_agent_bytes");
  http_rsb.tcp_refresh_miss_count           = Metrics::Counter::createPtr("proxy.process.http.tcp_refresh_miss_count");
  http_rsb.tcp_refresh_miss_origin_server_bytes =
    Metrics::Counter::createPtr("proxy.process.http.tcp_refresh_miss_origin_server_bytes");
  http_rsb.tcp_refresh_miss_user_agent_bytes = Metrics::Counter::createPtr("proxy.process.http.tcp_refresh_miss_user_agent_bytes");
  http_rsb.total_client_connections          = Metrics::Counter::createPtr("proxy.process.http.total_client_connections");
  http_rsb.total_client_connections_ipv4     = Metrics::Counter::createPtr("proxy.process.http.total_client_connections_ipv4");
  http_rsb.total_client_connections_ipv6     = Metrics::Counter::createPtr("proxy.process.http.total_client_connections_ipv6");
  http_rsb.total_client_connections_uds      = Metrics::Counter::createPtr("proxy.process.http.total_client_connections_uds");
  http_rsb.total_incoming_connections        = Metrics::Counter::createPtr("proxy.process.http.total_incoming_connections");
  http_rsb.total_parent_marked_down_count    = Metrics::Counter::createPtr("proxy.process.http.total_parent_marked_down_count");
  http_rsb.total_parent_proxy_connections    = Metrics::Counter::createPtr("proxy.process.http.total_parent_proxy_connections");
  http_rsb.total_parent_retries              = Metrics::Counter::createPtr("proxy.process.http.total_parent_retries");
  http_rsb.total_parent_retries_exhausted    = Metrics::Counter::createPtr("proxy.process.http.total_parent_retries_exhausted");
  http_rsb.total_parent_switches             = Metrics::Counter::createPtr("proxy.process.http.total_parent_switches");
  http_rsb.total_server_connections          = Metrics::Counter::createPtr("proxy.process.http.total_server_connections");
  http_rsb.total_transactions_time           = Metrics::Counter::createPtr("proxy.process.http.total_transactions_time");
  http_rsb.total_x_redirect                  = Metrics::Counter::createPtr("proxy.process.http.total_x_redirect_count");
  http_rsb.trace_requests                    = Metrics::Counter::createPtr("proxy.process.http.trace_requests");
  http_rsb.tunnel_current_active_connections = Metrics::Gauge::createPtr("proxy.process.tunnel.current_active_connections");
  http_rsb.tunnels                           = Metrics::Counter::createPtr("proxy.process.http.tunnels");
  http_rsb.ua_begin_time                     = Metrics::Counter::createPtr("proxy.process.http.milestone.ua_begin");
  http_rsb.ua_begin_write_time               = Metrics::Counter::createPtr("proxy.process.http.milestone.ua_begin_write");
  http_rsb.ua_close_time                     = Metrics::Counter::createPtr("proxy.process.http.milestone.ua_close");
  http_rsb.ua_counts_errors_aborts           = Metrics::Counter::createPtr("proxy.process.http.transaction_counts.errors.aborts");
  http_rsb.ua_counts_errors_connect_failed =
    Metrics::Counter::createPtr("proxy.process.http.transaction_counts.errors.connect_failed");
  http_rsb.ua_counts_errors_other = Metrics::Counter::createPtr("proxy.process.http.transaction_counts.errors.other");
  http_rsb.ua_counts_errors_possible_aborts =
    Metrics::Counter::createPtr("proxy.process.http.transaction_counts.errors.possible_aborts");
  http_rsb.ua_counts_errors_pre_accept_hangups =
    Metrics::Counter::createPtr("proxy.process.http.transaction_counts.errors.pre_accept_hangups");
  http_rsb.ua_counts_hit_fresh         = Metrics::Counter::createPtr("proxy.process.http.transaction_counts.hit_fresh");
  http_rsb.ua_counts_hit_fresh_process = Metrics::Counter::createPtr("proxy.process.http.transaction_counts.hit_fresh.process");
  http_rsb.ua_counts_hit_reval         = Metrics::Counter::createPtr("proxy.process.http.transaction_counts.hit_revalidated");
  http_rsb.ua_counts_miss_changed      = Metrics::Counter::createPtr("proxy.process.http.transaction_counts.miss_changed");
  http_rsb.ua_counts_miss_client_no_cache =
    Metrics::Counter::createPtr("proxy.process.http.transaction_counts.miss_client_no_cache");
  http_rsb.ua_counts_miss_cold          = Metrics::Counter::createPtr("proxy.process.http.transaction_counts.miss_cold");
  http_rsb.ua_counts_miss_uncacheable   = Metrics::Counter::createPtr("proxy.process.http.transaction_counts.miss_not_cacheable");
  http_rsb.ua_counts_other_unclassified = Metrics::Counter::createPtr("proxy.process.http.transaction_counts.other.unclassified");
  http_rsb.ua_first_read_time           = Metrics::Counter::createPtr("proxy.process.http.milestone.ua_first_read");
  http_rsb.ua_msecs_errors_aborts       = Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.errors.aborts");
  http_rsb.ua_msecs_errors_connect_failed =
    Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.errors.connect_failed");
  http_rsb.ua_msecs_errors_other = Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.errors.other");
  http_rsb.ua_msecs_errors_possible_aborts =
    Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.errors.possible_aborts");
  http_rsb.ua_msecs_errors_pre_accept_hangups =
    Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.errors.pre_accept_hangups");
  http_rsb.ua_msecs_hit_fresh         = Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.hit_fresh");
  http_rsb.ua_msecs_hit_fresh_process = Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.hit_fresh.process");
  http_rsb.ua_msecs_hit_reval         = Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.hit_revalidated");
  http_rsb.ua_msecs_miss_changed      = Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.miss_changed");
  http_rsb.ua_msecs_miss_client_no_cache =
    Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.miss_client_no_cache");
  http_rsb.ua_msecs_miss_cold          = Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.miss_cold");
  http_rsb.ua_msecs_miss_uncacheable   = Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.miss_not_cacheable");
  http_rsb.ua_msecs_other_unclassified = Metrics::Counter::createPtr("proxy.process.http.transaction_totaltime.other.unclassified");
  http_rsb.ua_read_header_done_time    = Metrics::Counter::createPtr("proxy.process.http.milestone.ua_read_header_done");
  http_rsb.user_agent_request_document_total_size =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_request_document_total_size");
  http_rsb.user_agent_request_header_total_size =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_request_header_total_size");
  http_rsb.user_agent_response_document_total_size =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_response_document_total_size");
  http_rsb.user_agent_response_header_total_size =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_response_header_total_size");
  http_rsb.websocket_current_active_client_connections =
    Metrics::Gauge::createPtr("proxy.process.http.websocket.current_active_client_connections");

  // Speed bucket stats for client and origin
  http_rsb.user_agent_speed_bytes_per_sec_100 =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_100");
  http_rsb.user_agent_speed_bytes_per_sec_1k = Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_1K");
  http_rsb.user_agent_speed_bytes_per_sec_10k =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_10K");
  http_rsb.user_agent_speed_bytes_per_sec_100k =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_100K");
  http_rsb.user_agent_speed_bytes_per_sec_1M = Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_1M");
  http_rsb.user_agent_speed_bytes_per_sec_10M =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_10M");
  http_rsb.user_agent_speed_bytes_per_sec_100M =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_100M");
  http_rsb.user_agent_speed_bytes_per_sec_200M =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_200M");
  http_rsb.user_agent_speed_bytes_per_sec_400M =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_400M");
  http_rsb.user_agent_speed_bytes_per_sec_800M =
    Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_800M");
  http_rsb.user_agent_speed_bytes_per_sec_1G = Metrics::Counter::createPtr("proxy.process.http.user_agent_speed_bytes_per_sec_1G");
  http_rsb.origin_server_speed_bytes_per_sec_100 =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_100");
  http_rsb.origin_server_speed_bytes_per_sec_1k =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_1K");
  http_rsb.origin_server_speed_bytes_per_sec_10k =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_10K");
  http_rsb.origin_server_speed_bytes_per_sec_100k =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_100K");
  http_rsb.origin_server_speed_bytes_per_sec_1M =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_1M");
  http_rsb.origin_server_speed_bytes_per_sec_10M =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_10M");
  http_rsb.origin_server_speed_bytes_per_sec_100M =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_100M");
  http_rsb.origin_server_speed_bytes_per_sec_200M =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_200M");
  http_rsb.origin_server_speed_bytes_per_sec_400M =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_400M");
  http_rsb.origin_server_speed_bytes_per_sec_800M =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_800M");
  http_rsb.origin_server_speed_bytes_per_sec_1G =
    Metrics::Counter::createPtr("proxy.process.http.origin_server_speed_bytes_per_sec_1G");

  Metrics::Derived::derive({
    // Total bytes of client request body + headers
    {"proxy.process.http.user_agent_total_request_bytes",
     {http_rsb.user_agent_request_document_total_size, http_rsb.user_agent_request_header_total_size}                                           },
    // Total bytes of client response body + headers
    {"proxy.process.http.user_agent_total_response_bytes",
     {http_rsb.user_agent_response_document_total_size, http_rsb.user_agent_response_header_total_size}                                         },
    // Total bytes of origin server request body + headers
    {"proxy.process.http.origin_server_total_request_bytes",
     {http_rsb.origin_server_request_document_total_size, http_rsb.origin_server_request_header_total_size}                                     },
    // Total bytes of origin server response body + headers
    {"proxy.process.http.origin_server_total_response_bytes",
     {http_rsb.origin_server_response_document_total_size, http_rsb.origin_server_response_header_total_size}                                   },
    // Total bytes of client request and response (total traffic to and from clients)
    {"proxy.process.user_agent_total_bytes",
     {"proxy.process.http.user_agent_total_request_bytes", "proxy.process.http.user_agent_total_response_bytes"}                                },
    // Total bytes of origin/parent request and response
    {"proxy.process.origin_server_total_bytes",
     {"proxy.process.http.origin_server_total_request_bytes", "proxy.process.http.origin_server_total_response_bytes",
      http_rsb.parent_proxy_request_total_bytes, http_rsb.parent_proxy_response_total_bytes}                                                    },
    // Total requests which are cache hits
    {"proxy.process.cache_total_hits",
     {http_rsb.cache_hit_fresh, http_rsb.cache_hit_reval, http_rsb.cache_hit_ims, http_rsb.cache_hit_stale_served}                              },
    // Total requests which are cache misses
    {"proxy.process.cache_total_misses",
     {http_rsb.cache_miss_cold, http_rsb.cache_miss_changed, http_rsb.cache_miss_client_no_cache, http_rsb.cache_miss_ims,
      http_rsb.cache_miss_uncacheable}                                                                                                          },
    // Total of all server connections (sum of origins and parent connections)
    {"proxy.process.current_server_connections",              {http_rsb.current_server_connections, http_rsb.current_parent_proxy_connections}  },
    // Total requests, both hits and misses (this is slightly superfluous, but assures correct percentage calculations)
    {"proxy.process.cache_total_requests",                    {"proxy.process.cache_total_hits", "proxy.process.cache_total_misses"}            },
    // Total cache requests bytes which are cache hits
    {"proxy.process.cache_total_hits_bytes",
     {http_rsb.tcp_hit_user_agent_bytes, http_rsb.tcp_refresh_hit_user_agent_bytes, http_rsb.tcp_ims_hit_user_agent_bytes}                      },
    // Total cache requests bytes which are cache misses
    {"proxy.process.cache_total_misses_bytes",
     {http_rsb.tcp_miss_user_agent_bytes, http_rsb.tcp_expired_miss_user_agent_bytes, http_rsb.tcp_refresh_miss_user_agent_bytes,
      http_rsb.tcp_ims_miss_user_agent_bytes}                                                                                                   },
    // Total request bytes, both hits and misses
    {"proxy.process.cache_total_bytes",                       {"proxy.process.cache_total_hits_bytes", "proxy.process.cache_total_misses_bytes"}}
  });
}

/**
  Parse list of HTTP status code and return HttpStatusBitset
  - e.g. "204 305 403 404 414 500 501 502 503 504"
 */
static HttpStatusBitset
parse_http_status_code_list(swoc::TextView status_list)
{
  HttpStatusBitset set;

  auto is_sep{[](char c) { return isspace(c) || ',' == c || ';' == c; }};

  while (!status_list.ltrim_if(is_sep).empty()) {
    swoc::TextView span;
    swoc::TextView token{status_list.take_prefix_if(is_sep)};
    auto           n = swoc::svtoi(token, &span);
    if (span.size() != token.size()) {
      Error("Invalid status code '%.*s': not a number", static_cast<int>(token.size()), token.data());
    } else if (n <= 0 || n >= HTTP_STATUS_NUMBER) {
      Error("Invalid status code '%.*s': out of range", static_cast<int>(token.size()), token.data());
    } else {
      set[n] = true;
    }
  }

  return set;
}

static bool
set_negative_caching_list(const char *name, RecDataT dtype, RecData data, HttpConfigParams *c, bool update)
{
  bool             ret = false;
  HttpStatusBitset set;

  // values from proxy.config.http.negative_caching_list
  if (0 == strcasecmp("proxy.config.http.negative_caching_list", name) && RECD_STRING == dtype && data.rec_string) {
    // parse the list of status codes
    set = parse_http_status_code_list({data.rec_string, strlen(data.rec_string)});
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

static bool
set_negative_revalidating_list(const char *name, RecDataT dtype, RecData data, HttpConfigParams *c, bool update)
{
  bool             ret = false;
  HttpStatusBitset set;

  // values from proxy.config.http.negative_revalidating_list
  if (0 == strcasecmp("proxy.config.http.negative_revalidating_list", name) && RECD_STRING == dtype && data.rec_string) {
    // parse the list of status codes
    set = parse_http_status_code_list({data.rec_string, strlen(data.rec_string)});
  }

  // set the return value
  if (set != c->negative_revalidating_list) {
    c->negative_revalidating_list = set;
    ret                           = ret || update;
  }

  return ret;
}

// Method of getting the status code bitset
static int
negative_revalidating_list_cb(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  HttpConfigParams *c = static_cast<HttpConfigParams *>(cookie);
  // Signal an update if valid value arrived.
  if (set_negative_revalidating_list(name, dtype, data, c, true)) {
    http_config_cb(name, dtype, data, cookie);
  }
  return REC_ERR_OKAY;
}

// Method of loading the negative caching config bitset
void
load_negative_revalidating_var(RecRecord const *r, void *cookie)
{
  HttpConfigParams *c = static_cast<HttpConfigParams *>(cookie);
  set_negative_revalidating_list(r->name, r->data_type, r->data, c, false);
}

/** Template for creating conversions and initialization for @c std::chrono based configuration variables.
 *
 * @tparam V The exact type of the configuration variable.
 *
 * The tricky template code is to enable having a class instance for each configuration variable, instead of for each _type_ of
 * configuration variable. This is required because the callback interface requires functions and so the actual storage must be
 * accessible from that function. *
 */
template <typename V> struct ConfigDuration {
  using self_type = ConfigDuration;
  V *_var; ///< Pointer to the variable to control.

  /** Constructor.
   *
   * @param v The variable to update.
   */
  ConfigDuration(V &v) : _var(&v) {}

  /// Convert to the mgmt (configuration) type.
  static MgmtInt
  to_mgmt(void const *data)
  {
    return static_cast<MgmtInt>(static_cast<V const *>(data)->count());
  }

  /// Convert from the mgmt (configuration) type.
  static void
  from_mgmt(void *data, MgmtInt i)
  {
    *static_cast<V *>(data) = V{i};
  }

  /// The conversion structure, which handles @c MgmtInt.
  static inline const MgmtConverter Conversions{&to_mgmt, &from_mgmt};

  /** Process start up conversion from configuration.
   *
   * @param type The data type in the configuration.
   * @param data The data in the configuration.
   * @param var Pointer to the variable to update.
   * @return @c true if @a data was successfully converted and stored, @c false if not.
   *
   * @note @a var is the target variable because it was explicitly set to be the value of @a _var in @c Enable.
   */
  static bool
  callback(char const *, RecDataT type, RecData data, void *var)
  {
    if (RECD_INT == type) {
      (*self_type::Conversions.store_int)(var, data.rec_int);
      return true;
    }
    return false;
  }

  /** Enable.
   *
   * @param name Name of the configuration variable.
   *
   * This enables both reading from the configuration and handling the callback for dynamic
   * updates of the variable.
   */
  void
  Enable(std::string_view name)
  {
    Enable_Config_Var(name, &self_type::callback, http_config_cb, _var);
  }
};

ConfigDuration HttpDownServerCacheTimeVar{HttpConfig::m_master.oride.down_server_timeout};
// Make the conversions visible to the plugin API. This allows exporting just the conversions
// without having to export the class definition. Again, the compiler doesn't allow doing this
// in one line.
extern MgmtConverter const &HttpDownServerCacheTimeConv;
MgmtConverter const        &HttpDownServerCacheTimeConv = HttpDownServerCacheTimeVar.Conversions;

////////////////////////////////////////////////////////////////
//
//  HttpConfig::startup()
//
////////////////////////////////////////////////////////////////
HttpStatsBlock http_rsb;

void
HttpConfig::startup()
{
  extern void SSLConfigInit(swoc::IPRangeSet * addrs);
  register_stat_callbacks();

  HttpConfigParams &c = m_master;

  http_config_cont = new HttpConfigCont;

  HttpEstablishStaticConfigStringAlloc(c.proxy_hostname, "proxy.config.proxy_name");
  c.proxy_hostname_len = -1;

  if (c.proxy_hostname == nullptr) {
    c.proxy_hostname    = static_cast<char *>(ats_malloc(sizeof(char)));
    c.proxy_hostname[0] = '\0';
  }

  c.inbound  += RecHttpLoadIp("proxy.config.incoming_ip_to_bind");
  c.outbound += RecHttpLoadIp("proxy.config.outgoing_ip_to_bind");
  RecHttpLoadIpAddrsFromConfVar("proxy.config.http.proxy_protocol_allowlist", c.config_proxy_protocol_ip_addrs);
  SSLConfigInit(&c.config_proxy_protocol_ip_addrs);

  HttpEstablishStaticConfigLongLong(c.server_max_connections, "proxy.config.http.server_max_connections");
  HttpEstablishStaticConfigLongLong(c.max_websocket_connections, "proxy.config.http.websocket.max_number_of_connections");
  HttpEstablishStaticConfigByte(c.oride.attach_server_session_to_client, "proxy.config.http.attach_server_session_to_client");
  HttpEstablishStaticConfigLongLong(c.oride.max_proxy_cycles, "proxy.config.http.max_proxy_cycles");

  HttpEstablishStaticConfigLongLong(c.oride.tunnel_activity_check_period, "proxy.config.tunnel.activity_check_period");

  HttpEstablishStaticConfigLongLong(c.oride.default_inactivity_timeout, "proxy.config.net.default_inactivity_timeout");

  HttpEstablishStaticConfigLongLong(c.http_request_line_max_size, "proxy.config.http.request_line_max_size");
  HttpEstablishStaticConfigLongLong(c.http_hdr_field_max_size, "proxy.config.http.header_field_max_size");

  HttpEstablishStaticConfigByte(c.disable_ssl_parenting, "proxy.config.http.parent_proxy.disable_connect_tunneling");
  HttpEstablishStaticConfigByte(c.oride.forward_connect_method, "proxy.config.http.forward_connect_method");

  HttpEstablishStaticConfigByte(c.oride.no_dns_forward_to_parent, "proxy.config.http.no_dns_just_forward_to_parent");
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
  HttpEstablishStaticConfigByte(c.oride.http_drop_chunked_trailers, "proxy.config.http.drop_chunked_trailers");
  HttpEstablishStaticConfigByte(c.oride.http_strict_chunk_parsing, "proxy.config.http.strict_chunk_parsing");
  HttpEstablishStaticConfigByte(c.oride.flow_control_enabled, "proxy.config.http.flow_control.enabled");
  HttpEstablishStaticConfigLongLong(c.oride.flow_high_water_mark, "proxy.config.http.flow_control.high_water");
  HttpEstablishStaticConfigLongLong(c.oride.flow_low_water_mark, "proxy.config.http.flow_control.low_water");
  HttpEstablishStaticConfigByte(c.oride.post_check_content_length_enabled, "proxy.config.http.post.check.content_length.enabled");
  HttpEstablishStaticConfigByte(c.oride.cache_post_method, "proxy.config.http.cache.post_method");
  HttpEstablishStaticConfigByte(c.oride.request_buffer_enabled, "proxy.config.http.request_buffer_enabled");
  HttpEstablishStaticConfigByte(c.strict_uri_parsing, "proxy.config.http.strict_uri_parsing");

  // [amc] This is a bit of a mess, need to figure out to make this cleaner.
  RecRegisterConfigUpdateCb("proxy.config.http.server_session_sharing.match", &http_server_session_sharing_cb, &c);
  http_config_enum_mask_read("proxy.config.http.server_session_sharing.match", c.oride.server_session_sharing_match);
  HttpEstablishStaticConfigStringAlloc(c.oride.server_session_sharing_match_str, "proxy.config.http.server_session_sharing.match");
  http_config_enum_read("proxy.config.http.server_session_sharing.pool", SessionSharingPoolStrings, c.server_session_sharing_pool);
  httpSessionManager.set_pool_type(c.server_session_sharing_pool);

  RecRegisterConfigUpdateCb("proxy.config.http.insert_forwarded", &http_insert_forwarded_cb, &c);
  {
    char str[512];

    if (auto sv{RecGetRecordString("proxy.config.http.insert_forwarded", str, sizeof(str))}; sv) {
      swoc::LocalBufferWriter<1024> error;
      HttpForwarded::OptionBitSet   bs = HttpForwarded::optStrToBitset(sv.value(), error);
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
  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_max_retries_down_server,
                                    "proxy.config.http.connect_attempts_max_retries_down_server");

  HttpEstablishStaticConfigLongLong(c.oride.connect_down_policy, "proxy.config.http.connect.down.policy");

  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_rr_retries, "proxy.config.http.connect_attempts_rr_retries");
  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_timeout, "proxy.config.http.connect_attempts_timeout");
  HttpEstablishStaticConfigLongLong(c.oride.parent_connect_attempts, "proxy.config.http.parent_proxy.total_connect_attempts");
  HttpEstablishStaticConfigLongLong(c.oride.parent_retry_time, "proxy.config.http.parent_proxy.retry_time");
  HttpEstablishStaticConfigLongLong(c.oride.parent_fail_threshold, "proxy.config.http.parent_proxy.fail_threshold");
  HttpEstablishStaticConfigLongLong(c.oride.per_parent_connect_attempts,
                                    "proxy.config.http.parent_proxy.per_parent_connect_attempts");
  HttpEstablishStaticConfigByte(c.oride.parent_failures_update_hostdb, "proxy.config.http.parent_proxy.mark_down_hostdb");
  HttpEstablishStaticConfigByte(c.oride.enable_parent_timeout_markdowns,
                                "proxy.config.http.parent_proxy.enable_parent_timeout_markdowns");
  HttpEstablishStaticConfigByte(c.oride.disable_parent_markdowns, "proxy.config.http.parent_proxy.disable_parent_markdowns");

  HttpEstablishStaticConfigLongLong(c.oride.sock_recv_buffer_size_out, "proxy.config.net.sock_recv_buffer_size_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_send_buffer_size_out, "proxy.config.net.sock_send_buffer_size_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_option_flag_out, "proxy.config.net.sock_option_flag_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_packet_mark_out, "proxy.config.net.sock_packet_mark_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_packet_tos_out, "proxy.config.net.sock_packet_tos_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_packet_notsent_lowat, "proxy.config.net.sock_notsent_lowat");

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

  HttpEstablishStaticConfigLongLong(c.oride.proxy_protocol_out, "proxy.config.http.proxy_protocol_out");

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

  // Body factory
  HttpEstablishStaticConfigByte(c.oride.response_suppression_mode, "proxy.config.body_factory.response_suppression_mode");

  // open read failure retries
  HttpEstablishStaticConfigLongLong(c.oride.max_cache_open_read_retries, "proxy.config.http.cache.max_open_read_retries");
  HttpEstablishStaticConfigLongLong(c.oride.cache_open_read_retry_time, "proxy.config.http.cache.open_read_retry_time");
  HttpEstablishStaticConfigLongLong(c.oride.cache_generation_number, "proxy.config.http.cache.generation");

  // open write failure retries
  HttpEstablishStaticConfigLongLong(c.oride.max_cache_open_write_retries, "proxy.config.http.cache.max_open_write_retries");
  HttpEstablishStaticConfigLongLong(c.oride.max_cache_open_write_retry_timeout,
                                    "proxy.config.http.cache.max_open_write_retry_timeout");

  HttpEstablishStaticConfigByte(c.oride.cache_http, "proxy.config.http.cache.http");
  HttpEstablishStaticConfigByte(c.oride.cache_ignore_client_no_cache, "proxy.config.http.cache.ignore_client_no_cache");
  HttpEstablishStaticConfigByte(c.oride.cache_ignore_client_cc_max_age, "proxy.config.http.cache.ignore_client_cc_max_age");
  HttpEstablishStaticConfigByte(c.oride.cache_ims_on_client_no_cache, "proxy.config.http.cache.ims_on_client_no_cache");
  HttpEstablishStaticConfigByte(c.oride.cache_ignore_server_no_cache, "proxy.config.http.cache.ignore_server_no_cache");
  HttpEstablishStaticConfigByte(c.oride.cache_responses_to_cookies, "proxy.config.http.cache.cache_responses_to_cookies");

  HttpEstablishStaticConfigByte(c.oride.cache_ignore_auth, "proxy.config.http.cache.ignore_authentication");
  HttpEstablishStaticConfigByte(c.oride.cache_urls_that_look_dynamic, "proxy.config.http.cache.cache_urls_that_look_dynamic");
  HttpEstablishStaticConfigByte(c.oride.cache_ignore_query, "proxy.config.http.cache.ignore_query");

  HttpEstablishStaticConfigByte(c.oride.ignore_accept_mismatch, "proxy.config.http.cache.ignore_accept_mismatch");
  HttpEstablishStaticConfigByte(c.oride.ignore_accept_language_mismatch, "proxy.config.http.cache.ignore_accept_language_mismatch");
  HttpEstablishStaticConfigByte(c.oride.ignore_accept_encoding_mismatch, "proxy.config.http.cache.ignore_accept_encoding_mismatch");
  HttpEstablishStaticConfigByte(c.oride.ignore_accept_charset_mismatch, "proxy.config.http.cache.ignore_accept_charset_mismatch");

  HttpEstablishStaticConfigByte(c.send_100_continue_response, "proxy.config.http.send_100_continue_response");
  HttpEstablishStaticConfigByte(c.disallow_post_100_continue, "proxy.config.http.disallow_post_100_continue");

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

  HttpDownServerCacheTimeVar.Enable("proxy.config.http.down_server.cache_time");

  // Negative caching and revalidation
  HttpEstablishStaticConfigByte(c.oride.negative_caching_enabled, "proxy.config.http.negative_caching_enabled");
  HttpEstablishStaticConfigLongLong(c.oride.negative_caching_lifetime, "proxy.config.http.negative_caching_lifetime");
  HttpEstablishStaticConfigByte(c.oride.negative_revalidating_enabled, "proxy.config.http.negative_revalidating_enabled");
  HttpEstablishStaticConfigLongLong(c.oride.negative_revalidating_lifetime, "proxy.config.http.negative_revalidating_lifetime");
  RecRegisterConfigUpdateCb("proxy.config.http.negative_caching_list", &negative_caching_list_cb, &c);
  RecLookupRecord("proxy.config.http.negative_caching_list", &load_negative_caching_var, &c, true);
  RecRegisterConfigUpdateCb("proxy.config.http.negative_revalidating_list", &negative_revalidating_list_cb, &c);
  RecLookupRecord("proxy.config.http.negative_revalidating_list", &load_negative_revalidating_var, &c, true);

  // Buffer size and watermark
  HttpEstablishStaticConfigLongLong(c.oride.default_buffer_size_index, "proxy.config.http.default_buffer_size");
  HttpEstablishStaticConfigLongLong(c.oride.default_buffer_water_mark, "proxy.config.http.default_buffer_water_mark");

  // Plugin VC buffer size and watermark
  HttpEstablishStaticConfigLongLong(c.oride.plugin_vc_default_buffer_index, "proxy.config.plugin.vc.default_buffer_index");
  HttpEstablishStaticConfigLongLong(c.oride.plugin_vc_default_buffer_water_mark,
                                    "proxy.config.plugin.vc.default_buffer_water_mark");

  HttpEstablishStaticConfigLongLong(c.max_post_size, "proxy.config.http.max_post_size");
  HttpEstablishStaticConfigLongLong(c.max_payload_iobuf_index, "proxy.config.payload.io.max_buffer_index");
  HttpEstablishStaticConfigLongLong(c.max_msg_iobuf_index, "proxy.config.msg.io.max_buffer_index");

  // ##############################################################################
  // #
  // # Redirection
  // #
  // # See RecordsConfig definition.
  // #
  // ##############################################################################
  HttpEstablishStaticConfigByte(c.oride.redirect_use_orig_cache_key, "proxy.config.http.redirect_use_orig_cache_key");
  HttpEstablishStaticConfigByte(c.redirection_host_no_port, "proxy.config.http.redirect_host_no_port");
  HttpEstablishStaticConfigLongLong(c.oride.number_of_redirections, "proxy.config.http.number_of_redirections");
  HttpEstablishStaticConfigLongLong(c.post_copy_size, "proxy.config.http.post_copy_size");
  HttpEstablishStaticConfigStringAlloc(c.redirect_actions_string, "proxy.config.http.redirect.actions");
  HttpEstablishStaticConfigByte(c.http_host_sni_policy, "proxy.config.http.host_sni_policy");

  HttpEstablishStaticConfigStringAlloc(c.oride.ssl_client_sni_policy, "proxy.config.ssl.client.sni_policy");
  HttpEstablishStaticConfigStringAlloc(c.oride.ssl_client_alpn_protocols, "proxy.config.ssl.client.alpn_protocols");
  HttpEstablishStaticConfigByte(c.scheme_proto_mismatch_policy, "proxy.config.ssl.client.scheme_proto_mismatch_policy");

  ConnectionTracker::config_init(&c.global_connection_tracker_config, &c.oride.connection_tracker_config, http_config_cb);

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

  params->inbound = m_master.inbound;

  params->outbound = m_master.outbound;

  params->proxy_hostname                           = ats_strdup(m_master.proxy_hostname);
  params->proxy_hostname_len                       = (params->proxy_hostname) ? strlen(params->proxy_hostname) : 0;
  params->oride.no_dns_forward_to_parent           = INT_TO_BOOL(m_master.oride.no_dns_forward_to_parent);
  params->oride.uncacheable_requests_bypass_parent = INT_TO_BOOL(m_master.oride.uncacheable_requests_bypass_parent);
  params->no_origin_server_dns                     = INT_TO_BOOL(m_master.no_origin_server_dns);
  params->use_client_target_addr                   = m_master.use_client_target_addr;
  params->use_client_source_port                   = INT_TO_BOOL(m_master.use_client_source_port);
  params->oride.maintain_pristine_host_hdr         = INT_TO_BOOL(m_master.oride.maintain_pristine_host_hdr);

  params->disable_ssl_parenting        = INT_TO_BOOL(m_master.disable_ssl_parenting);
  params->oride.forward_connect_method = INT_TO_BOOL(m_master.oride.forward_connect_method);

  params->server_max_connections                = m_master.server_max_connections;
  params->max_websocket_connections             = m_master.max_websocket_connections;
  params->oride.connection_tracker_config       = m_master.oride.connection_tracker_config;
  params->global_connection_tracker_config      = m_master.global_connection_tracker_config;
  params->oride.attach_server_session_to_client = m_master.oride.attach_server_session_to_client;
  params->oride.max_proxy_cycles                = m_master.oride.max_proxy_cycles;
  params->oride.tunnel_activity_check_period    = m_master.oride.tunnel_activity_check_period;

  params->oride.default_inactivity_timeout = m_master.oride.default_inactivity_timeout;

  params->http_request_line_max_size = m_master.http_request_line_max_size;
  params->http_hdr_field_max_size    = m_master.http_hdr_field_max_size;

  if (params->oride.connection_tracker_config.server_max > 0 &&
      params->oride.connection_tracker_config.server_max < params->oride.connection_tracker_config.server_min) {
    Warning("'%s' < per_server.min_keep_alive_connections, setting min=max , please correct your %s",
            ConnectionTracker::CONFIG_SERVER_VAR_MAX.data(), ts::filename::RECORDS);
    params->oride.connection_tracker_config.server_min = params->oride.connection_tracker_config.server_max;
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
  params->oride.http_drop_chunked_trailers  = m_master.oride.http_drop_chunked_trailers;
  params->oride.http_strict_chunk_parsing   = m_master.oride.http_strict_chunk_parsing;
  params->oride.auth_server_session_private = m_master.oride.auth_server_session_private;

  params->oride.http_chunking_size = m_master.oride.http_chunking_size;

  params->oride.post_check_content_length_enabled = INT_TO_BOOL(m_master.oride.post_check_content_length_enabled);
  params->oride.cache_post_method                 = INT_TO_BOOL(m_master.oride.cache_post_method);

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

  params->oride.server_session_sharing_match     = m_master.oride.server_session_sharing_match;
  params->oride.server_session_sharing_match_str = ats_strdup(m_master.oride.server_session_sharing_match_str);
  params->oride.server_min_keep_alive_conns      = m_master.oride.server_min_keep_alive_conns;
  params->server_session_sharing_pool            = m_master.server_session_sharing_pool;
  params->oride.keep_alive_post_out              = m_master.oride.keep_alive_post_out;

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
  params->oride.connect_attempts_max_retries_down_server = m_master.oride.connect_attempts_max_retries_down_server;
  if (m_master.oride.connect_attempts_rr_retries > params->oride.connect_attempts_max_retries) {
    Warning("connect_attempts_rr_retries (%" PRIu64 ") is greater than "
            "connect_attempts_max_retries (%" PRIu64 "), this means requests "
            "will never redispatch to another server",
            m_master.oride.connect_attempts_rr_retries, params->oride.connect_attempts_max_retries);
  }
  params->oride.connect_attempts_rr_retries     = m_master.oride.connect_attempts_rr_retries;
  params->oride.connect_attempts_timeout        = m_master.oride.connect_attempts_timeout;
  params->oride.connect_down_policy             = m_master.oride.connect_down_policy;
  params->oride.parent_connect_attempts         = m_master.oride.parent_connect_attempts;
  params->oride.parent_retry_time               = m_master.oride.parent_retry_time;
  params->oride.parent_fail_threshold           = m_master.oride.parent_fail_threshold;
  params->oride.per_parent_connect_attempts     = m_master.oride.per_parent_connect_attempts;
  params->oride.parent_failures_update_hostdb   = m_master.oride.parent_failures_update_hostdb;
  params->oride.no_dns_forward_to_parent        = m_master.oride.no_dns_forward_to_parent;
  params->oride.enable_parent_timeout_markdowns = m_master.oride.enable_parent_timeout_markdowns;
  params->oride.disable_parent_markdowns        = m_master.oride.disable_parent_markdowns;

  params->oride.sock_recv_buffer_size_out = m_master.oride.sock_recv_buffer_size_out;
  params->oride.sock_send_buffer_size_out = m_master.oride.sock_send_buffer_size_out;
  params->oride.sock_packet_mark_out      = m_master.oride.sock_packet_mark_out;
  params->oride.sock_packet_tos_out       = m_master.oride.sock_packet_tos_out;
  params->oride.sock_option_flag_out      = m_master.oride.sock_option_flag_out;
  params->oride.sock_packet_notsent_lowat = m_master.oride.sock_packet_notsent_lowat;

  // Clear the TCP Fast Open option if it is not supported on this host.
  if ((params->oride.sock_option_flag_out & NetVCOptions::SOCK_OPT_TCP_FAST_OPEN) && !UnixSocket::client_fastopen_supported()) {
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
  params->oride.proxy_protocol_out           = m_master.oride.proxy_protocol_out;

  params->oride.cache_heuristic_min_lifetime = m_master.oride.cache_heuristic_min_lifetime;
  params->oride.cache_heuristic_max_lifetime = m_master.oride.cache_heuristic_max_lifetime;
  params->oride.cache_heuristic_lm_factor    = std::min(std::max(m_master.oride.cache_heuristic_lm_factor, 0.0f), 1.0f);

  params->oride.cache_guaranteed_min_lifetime = m_master.oride.cache_guaranteed_min_lifetime;
  params->oride.cache_guaranteed_max_lifetime = m_master.oride.cache_guaranteed_max_lifetime;

  params->oride.cache_max_stale_age = m_master.oride.cache_max_stale_age;

  params->oride.srv_enabled = m_master.oride.srv_enabled;

  params->oride.allow_half_open = m_master.oride.allow_half_open;

  params->oride.response_suppression_mode = m_master.oride.response_suppression_mode;

  // open read failure retries
  params->oride.max_cache_open_read_retries = m_master.oride.max_cache_open_read_retries;
  params->oride.cache_open_read_retry_time  = m_master.oride.cache_open_read_retry_time;
  params->oride.cache_generation_number     = m_master.oride.cache_generation_number;

  // open write failure retries
  params->oride.max_cache_open_write_retries = m_master.oride.max_cache_open_write_retries;

  params->oride.cache_http                     = INT_TO_BOOL(m_master.oride.cache_http);
  params->oride.cache_ignore_client_no_cache   = INT_TO_BOOL(m_master.oride.cache_ignore_client_no_cache);
  params->oride.cache_ignore_client_cc_max_age = INT_TO_BOOL(m_master.oride.cache_ignore_client_cc_max_age);
  params->oride.cache_ims_on_client_no_cache   = INT_TO_BOOL(m_master.oride.cache_ims_on_client_no_cache);
  params->oride.cache_ignore_server_no_cache   = INT_TO_BOOL(m_master.oride.cache_ignore_server_no_cache);
  params->oride.cache_responses_to_cookies     = m_master.oride.cache_responses_to_cookies;
  params->oride.cache_ignore_auth              = INT_TO_BOOL(m_master.oride.cache_ignore_auth);
  params->oride.cache_urls_that_look_dynamic   = INT_TO_BOOL(m_master.oride.cache_urls_that_look_dynamic);
  params->oride.cache_ignore_query             = INT_TO_BOOL(m_master.oride.cache_ignore_query);

  params->oride.ignore_accept_mismatch          = m_master.oride.ignore_accept_mismatch;
  params->oride.ignore_accept_language_mismatch = m_master.oride.ignore_accept_language_mismatch;
  params->oride.ignore_accept_encoding_mismatch = m_master.oride.ignore_accept_encoding_mismatch;
  params->oride.ignore_accept_charset_mismatch  = m_master.oride.ignore_accept_charset_mismatch;

  params->send_100_continue_response = INT_TO_BOOL(m_master.send_100_continue_response);
  params->disallow_post_100_continue = INT_TO_BOOL(m_master.disallow_post_100_continue);

  params->oride.cache_open_write_fail_action = m_master.oride.cache_open_write_fail_action;
  if (params->oride.cache_open_write_fail_action == static_cast<MgmtByte>(CacheOpenWriteFailAction_t::READ_RETRY)) {
    if (params->oride.max_cache_open_read_retries <= 0 || params->oride.max_cache_open_write_retries <= 0) {
      Warning("Invalid config, cache_open_write_fail_action (%d), max_cache_open_read_retries (%" PRIu64 "), "
              "max_cache_open_write_retries (%" PRIu64 ")",
              params->oride.cache_open_write_fail_action, params->oride.max_cache_open_read_retries,
              params->oride.max_cache_open_write_retries);
    }
  }

  params->oride.cache_when_to_revalidate = m_master.oride.cache_when_to_revalidate;
  params->max_post_size                  = m_master.max_post_size;
  params->max_payload_iobuf_index        = m_master.max_payload_iobuf_index;
  params->max_msg_iobuf_index            = m_master.max_msg_iobuf_index;

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
  params->oride.body_factory_template_base = ats_strdup(m_master.oride.body_factory_template_base);
  params->oride.body_factory_template_base_len =
    params->oride.body_factory_template_base ? strlen(params->oride.body_factory_template_base) : 0;
  params->body_factory_response_max_size = m_master.body_factory_response_max_size;
  params->reverse_proxy_no_host_redirect = ats_strdup(m_master.reverse_proxy_no_host_redirect);
  params->reverse_proxy_no_host_redirect_len =
    params->reverse_proxy_no_host_redirect ? strlen(params->reverse_proxy_no_host_redirect) : 0;

  params->referer_filter_enabled  = INT_TO_BOOL(m_master.referer_filter_enabled);
  params->referer_format_redirect = INT_TO_BOOL(m_master.referer_format_redirect);

  params->strict_uri_parsing = m_master.strict_uri_parsing;

  params->oride.down_server_timeout = m_master.oride.down_server_timeout;

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
  params->http_host_sni_policy = m_master.http_host_sni_policy;
  params->scheme_proto_mismatch_policy = m_master.scheme_proto_mismatch_policy;

  params->oride.ssl_client_sni_policy     = ats_strdup(m_master.oride.ssl_client_sni_policy);
  params->oride.ssl_client_alpn_protocols = ats_strdup(m_master.oride.ssl_client_alpn_protocols);

  params->negative_caching_list      = m_master.negative_caching_list;
  params->negative_revalidating_list = m_master.negative_revalidating_list;

  params->oride.host_res_data            = m_master.oride.host_res_data;
  params->oride.host_res_data.conf_value = ats_strdup(m_master.oride.host_res_data.conf_value);

  params->oride.plugin_vc_default_buffer_index      = m_master.oride.plugin_vc_default_buffer_index;
  params->oride.plugin_vc_default_buffer_water_mark = m_master.oride.plugin_vc_default_buffer_water_mark;

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
    char                *start;
    char                *end;

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
RedirectEnabled::ActionMap *
HttpConfig::parse_redirect_actions(char *input_string, RedirectEnabled::Action &self_action)
{
  using RedirectEnabled::Action;
  using RedirectEnabled::action_map;
  using RedirectEnabled::address_class_map;
  using RedirectEnabled::AddressClass;

  if (nullptr == input_string) {
    Error("parse_redirect_actions: The configuration value is empty.");
    return nullptr;
  }
  Tokenizer                      configTokens(", ");
  int                            n_rules = configTokens.Initialize(input_string);
  std::map<AddressClass, Action> configMapping;
  for (int i = 0; i < n_rules; i++) {
    const char *rule = configTokens[i];
    Tokenizer   ruleTokens(":");
    int         n_mapping = ruleTokens.Initialize(rule);
    if (2 != n_mapping) {
      Error("parse_redirect_actions: Individual rules must be an address class and an action separated by a colon (:)");
      return nullptr;
    }
    std::string  c_input(ruleTokens[0]), a_input(ruleTokens[1]);
    AddressClass c =
      address_class_map.find(ruleTokens[0]) != address_class_map.end() ? address_class_map[ruleTokens[0]] : AddressClass::INVALID;
    Action a = action_map.find(ruleTokens[1]) != action_map.end() ? action_map[ruleTokens[1]] : Action::INVALID;

    if (AddressClass::INVALID == c) {
      Error("parse_redirect_actions: '%.*s' is not a valid address class", static_cast<int>(c_input.size()), c_input.data());
      return nullptr;
    } else if (Action::INVALID == a) {
      Error("parse_redirect_actions: '%.*s' is not a valid action", static_cast<int>(a_input.size()), a_input.data());
      return nullptr;
    }
    configMapping[c] = a;
  }

  // Ensure the default.
  if (configMapping.end() == configMapping.find(AddressClass::DEFAULT)) {
    configMapping[AddressClass::DEFAULT] = Action::RETURN;
  }

  auto  *ret    = new RedirectEnabled::ActionMap;
  Action action = Action::INVALID;

  // PRIVATE
  action = configMapping.find(AddressClass::PRIVATE) != configMapping.end() ? configMapping[AddressClass::PRIVATE] :
                                                                              configMapping[AddressClass::DEFAULT];
  ret->mark(swoc::IP4Range("10.0.0.0/8"), action);
  ret->mark(swoc::IP4Range("100.64.0.0/10"), action);
  ret->mark(swoc::IP4Range("172.16.0.0/12"), action);
  ret->mark(swoc::IP4Range("192.168.0.0/16"), action);
  ret->mark(swoc::IP6Range("fc00::/7"), action);

  // LOOPBACK
  action = configMapping.find(AddressClass::LOOPBACK) != configMapping.end() ? configMapping[AddressClass::LOOPBACK] :
                                                                               configMapping[AddressClass::DEFAULT];

  ret->mark(swoc::IP4Range("127.0.0.0/8"), action);
  ret->mark(swoc::IP6Addr("::1"), action);

  // MULTICAST
  action = configMapping.find(AddressClass::MULTICAST) != configMapping.end() ? configMapping[AddressClass::MULTICAST] :
                                                                                configMapping[AddressClass::DEFAULT];
  ret->mark(swoc::IP4Range("224.0.0.0/4"), action);
  ret->mark(swoc::IP6Range("ff00::/8"), action);

  // LINKLOCAL
  action = configMapping.find(AddressClass::LINKLOCAL) != configMapping.end() ? configMapping[AddressClass::LINKLOCAL] :
                                                                                configMapping[AddressClass::DEFAULT];
  ret->mark(swoc::IP4Range("169.254.0.0/16"), action);
  ret->mark(swoc::IP6Range("fe80::/10"), action);

  // SELF
  // We must store the self address class separately instead of adding the addresses to our map.
  // The addresses Trafficserver will use depend on configurations that are loaded here, so they are not available yet.
  action      = configMapping.find(AddressClass::SELF) != configMapping.end() ? configMapping[AddressClass::SELF] :
                                                                                configMapping[AddressClass::DEFAULT];
  self_action = action;

  // ROUTABLE
  action = configMapping.find(AddressClass::ROUTABLE) != configMapping.end() ? configMapping[AddressClass::ROUTABLE] :
                                                                               configMapping[AddressClass::DEFAULT];
  // @c fille doesn't change any existing mapping.
  ret->fill(swoc::IP4Range("0/0"), action);
  ret->fill(swoc::IP6Range("::/0"), action);

  return ret;
}
