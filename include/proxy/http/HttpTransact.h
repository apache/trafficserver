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

#include <cstddef>

#include "tsutil/DbgCtl.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_platform.h"
#include "iocore/hostdb/HostDB.h"
#include "proxy/http/HttpConfig.h"
#include "proxy/hdrs/HTTP.h"
#include "iocore/cache/HttpTransactCache.h"
#include "proxy/ControlMatcher.h"
#include "proxy/CacheControl.h"
#include "proxy/ParentSelection.h"
#include "iocore/eventsystem/ConfigProcessor.h"
#include "proxy/Transform.h"
#include "proxy/Milestones.h"
#include "ts/apidefs.h"
#include "ts/remap.h"
#include "proxy/http/remap/RemapPluginInfo.h"
#include "proxy/http/remap/UrlMapping.h"
#include "records/RecHttp.h"
#include "proxy/ProxySession.h"
#include "tscore/MgmtDefs.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#define HTTP_OUR_VIA_MAX_LENGTH 1024 // 512-bytes for hostname+via string, 512-bytes for the debug info

#define HTTP_RELEASE_ASSERT(X) ink_release_assert(X)

inline void
s_dump_header(HTTPHdr const *hdr, std::string &out)
{
  int offset{0};
  int done{0};
  do {
    int  used{0};
    char b[4096];
    // The buffer offset is taken non-const and it is apparently
    // modified in some code path, but in my testing it does
    // not change, it seems. Since we manually bump the offset,
    // the use of tmp is precautionary to make sure our logic
    // doesn't break in case it does change in some circumstance.
    int tmp{offset};
    done    = hdr->print(b, 4096, &used, &tmp);
    offset += used;
    out.append(b, used);
  } while (0 == done);
}

inline void
dump_header(DbgCtl const &ctl, HTTPHdr const *hdr, std::int64_t sm_id, std::string_view description)
{
  if (ctl.on()) {
    std::string output;
    output.append("+++++++++ ");
    output.append(description);
    output.append(" +++++++++\n");
    output.append("-- State Machine Id: ");
    output.append(std::to_string(sm_id));
    output.push_back('\n');
    if (hdr->valid()) {
      s_dump_header(hdr, output);
    } else {
      output.append("Invalid header!\n");
    }
    // We make a single call to fprintf so that the output does not get
    // interleaved with output from other threads performing I/O.
    fprintf(stderr, "%s", output.c_str());
  }
}

using ink_time_t = time_t;

struct HttpConfigParams;
class HttpSM;

#include "iocore/net/ConnectionTracker.h"
#include "tscore/InkErrno.h"

#define UNKNOWN_INTERNAL_ERROR (INK_START_ERRNO - 1)

enum ViaStringIndex_t {
  //
  // General information
  VIA_CLIENT = 0,
  VIA_CLIENT_REQUEST,
  VIA_CACHE,
  VIA_CACHE_RESULT,
  VIA_SERVER,
  VIA_SERVER_RESULT,
  VIA_CACHE_FILL,
  VIA_CACHE_FILL_ACTION,
  VIA_PROXY,
  VIA_PROXY_RESULT,
  VIA_ERROR,
  VIA_ERROR_TYPE,
  //
  // State Machine specific details
  VIA_DETAIL_SEPARATOR,
  VIA_DETAIL_TUNNEL_DESCRIPTOR,
  VIA_DETAIL_TUNNEL,
  VIA_DETAIL_CACHE_DESCRIPTOR,
  VIA_DETAIL_CACHE_TYPE,
  VIA_DETAIL_CACHE_LOOKUP,
  VIA_DETAIL_PP_DESCRIPTOR,
  VIA_DETAIL_PP_CONNECT,
  VIA_DETAIL_SERVER_DESCRIPTOR,
  VIA_DETAIL_SERVER_CONNECT,
  //
  // Total
  MAX_VIA_INDICES
};

enum ViaString_t {
  // client stuff
  VIA_CLIENT_STRING   = 'u',
  VIA_CLIENT_ERROR    = 'E',
  VIA_CLIENT_IMS      = 'I',
  VIA_CLIENT_NO_CACHE = 'N',
  VIA_CLIENT_COOKIE   = 'C',
  VIA_CLIENT_SIMPLE   = 'S',
  // cache lookup stuff
  VIA_CACHE_STRING            = 'c',
  VIA_CACHE_MISS              = 'M',
  VIA_IN_CACHE_NOT_ACCEPTABLE = 'A',
  VIA_IN_CACHE_STALE          = 'S',
  VIA_IN_CACHE_FRESH          = 'H',
  VIA_IN_RAM_CACHE_FRESH      = 'R',
  VIA_IN_CACHE_RWW_HIT        = 'W',
  // server stuff
  VIA_SERVER_STRING       = 's',
  VIA_SERVER_ERROR        = 'E',
  VIA_SERVER_NOT_MODIFIED = 'N',
  VIA_SERVER_SERVED       = 'S',
  // cache fill stuff
  VIA_CACHE_FILL_STRING = 'f',
  VIA_CACHE_DELETED     = 'D',
  VIA_CACHE_WRITTEN     = 'W',
  VIA_CACHE_UPDATED     = 'U',
  // proxy stuff
  VIA_PROXY_STRING             = 'p',
  VIA_PROXY_NOT_MODIFIED       = 'N',
  VIA_PROXY_SERVED             = 'S',
  VIA_PROXY_SERVER_REVALIDATED = 'R',
  // errors
  VIA_ERROR_STRING            = 'e',
  VIA_ERROR_NO_ERROR          = 'N',
  VIA_ERROR_AUTHORIZATION     = 'A',
  VIA_ERROR_CONNECTION        = 'C',
  VIA_ERROR_DNS_FAILURE       = 'D',
  VIA_ERROR_FORBIDDEN         = 'F',
  VIA_ERROR_HEADER_SYNTAX     = 'H',
  VIA_ERROR_SERVER            = 'S',
  VIA_ERROR_TIMEOUT           = 'T',
  VIA_ERROR_CACHE_READ        = 'R',
  VIA_ERROR_MOVED_TEMPORARILY = 'M',
  VIA_ERROR_LOOP_DETECTED     = 'L',
  VIA_ERROR_UNKNOWN           = ' ',
  //
  // Now the detailed stuff
  //
  VIA_DETAIL_SEPARATOR_STRING = ':',
  // tunnelling
  VIA_DETAIL_TUNNEL_DESCRIPTOR_STRING = 't',
  VIA_DETAIL_TUNNEL_HEADER_FIELD      = 'F',
  VIA_DETAIL_TUNNEL_METHOD            = 'M',
  VIA_DETAIL_TUNNEL_CACHE_OFF         = 'O',
  VIA_DETAIL_TUNNEL_URL               = 'U',
  VIA_DETAIL_TUNNEL_NO_FORWARD        = 'N',
  VIA_DETAIL_TUNNEL_AUTHORIZATION     = 'A',
  // cache type
  VIA_DETAIL_CACHE_DESCRIPTOR_STRING = 'c',
  VIA_DETAIL_CACHE                   = 'C',
  VIA_DETAIL_PARENT                  = 'P',
  VIA_DETAIL_SERVER                  = 'S',
  // result of cache lookup
  VIA_DETAIL_HIT_CONDITIONAL  = 'N',
  VIA_DETAIL_HIT_SERVED       = 'H',
  VIA_DETAIL_MISS_CONDITIONAL = 'I',
  VIA_DETAIL_MISS_NOT_CACHED  = 'M',
  VIA_DETAIL_MISS_EXPIRED     = 'S',
  VIA_DETAIL_MISS_CONFIG      = 'C',
  VIA_DETAIL_MISS_CLIENT      = 'U',
  VIA_DETAIL_MISS_METHOD      = 'D',
  VIA_DETAIL_MISS_COOKIE      = 'K',
  // result of pp suggested host lookup
  VIA_DETAIL_PP_DESCRIPTOR_STRING = 'p',
  VIA_DETAIL_PP_SUCCESS           = 'S',
  VIA_DETAIL_PP_FAILURE           = 'F',
  // result of server suggested host lookup
  VIA_DETAIL_SERVER_DESCRIPTOR_STRING = 's',
  VIA_DETAIL_SERVER_SUCCESS           = 'S',
  VIA_DETAIL_SERVER_FAILURE           = 'F'
};

struct HttpApiInfo {
  char *parent_proxy_name        = nullptr;
  int   parent_proxy_port        = -1;
  bool  cache_untransformed      = false;
  bool  cache_transformed        = true;
  bool  logging_enabled          = true;
  bool  retry_intercept_failures = false;

  HttpApiInfo() {}
};

const int32_t HTTP_UNDEFINED_CL = -1;

//////////////////////////////////////////////////////////////////////////////
//
//  HttpTransact
//
//  The HttpTransact class is purely used for scoping and should
//  not be instantiated.
//
//////////////////////////////////////////////////////////////////////////////
#define SET_VIA_STRING(I, S) s->via_string[I] = S;
#define GET_VIA_STRING(I)    (s->via_string[I])

class HttpTransact
{
public:
  enum AbortState_t {
    ABORT_UNDEFINED = 0,
    DIDNOT_ABORT,
    ABORTED,
  };

  enum class Authentication_t { SUCCESS = 0, MUST_REVALIDATE, MUST_PROXY, CACHE_AUTH };

  enum CacheAction_t {
    UNDEFINED = 0,
    NO_ACTION,
    DELETE,
    LOOKUP,
    REPLACE,
    SERVE,
    SERVE_AND_DELETE,
    SERVE_AND_UPDATE,
    UPDATE,
    WRITE,
    PREPARE_TO_DELETE,
    PREPARE_TO_UPDATE,
    PREPARE_TO_WRITE,
    TOTAL_TYPES
  };

  enum class CacheWriteLock_t {
    INIT,
    SUCCESS,
    FAIL,
    READ_RETRY,
  };

  enum class ClientTransactionResult_t {
    UNDEFINED,
    HIT_FRESH,
    HIT_REVALIDATED,
    MISS_COLD,
    MISS_CHANGED,
    MISS_CLIENT_NO_CACHE,
    MISS_UNCACHABLE,
    ERROR_ABORT,
    ERROR_POSSIBLE_ABORT,
    ERROR_CONNECT_FAIL,
    ERROR_OTHER
  };

  enum class Freshness_t {
    FRESH = 0, // Fresh enough, serve it
    WARNING,   // Stale, but client says OK
    STALE      // Stale, don't use
  };

  enum class HttpTransactMagic_t : uint32_t { ALIVE = 0x00001234, DEAD = 0xDEAD1234, SEPARATOR = 0x12345678 };

  enum class ProxyMode_t {
    UNDEFINED,
    GENERIC,
    TUNNELLING,
  };

  enum class RequestError_t {
    NO_REQUEST_HEADER_ERROR,
    BAD_HTTP_HEADER_SYNTAX,
    BAD_CONNECT_PORT,
    FAILED_PROXY_AUTHORIZATION,
    METHOD_NOT_SUPPORTED,
    MISSING_HOST_FIELD,
    NO_POST_CONTENT_LENGTH,
    NO_REQUEST_SCHEME,
    NON_EXISTANT_REQUEST_HEADER,
    SCHEME_NOT_SUPPORTED,
    UNACCEPTABLE_TE_REQUIRED,
    INVALID_POST_CONTENT_LENGTH,
    TOTAL_TYPES
  };

  enum class ResponseError_t {
    NO_RESPONSE_HEADER_ERROR,
    BOGUS_OR_NO_DATE_IN_RESPONSE,
    CONNECTION_OPEN_FAILED,
    MISSING_REASON_PHRASE,
    MISSING_STATUS_CODE,
    NON_EXISTANT_RESPONSE_HEADER,
    NOT_A_RESPONSE_HEADER,
    STATUS_CODE_SERVER_ERROR,
    TOTAL_TYPES
  };

  // Please do not forget to fix TSServerState (ts/apidefs.h.in)
  // in case of any modifications in ServerState_t
  enum ServerState_t {
    STATE_UNDEFINED = 0,
    ACTIVE_TIMEOUT,
    BAD_INCOMING_RESPONSE,
    CONNECTION_ALIVE,
    CONNECTION_CLOSED,
    CONNECTION_ERROR,
    INACTIVE_TIMEOUT,
    OPEN_RAW_ERROR,
    PARSE_ERROR,
    TRANSACTION_COMPLETE,
    PARENT_RETRY,
    OUTBOUND_CONGESTION
  };

  enum class CacheWriteStatus_t { NO_WRITE = 0, LOCK_MISS, IN_PROGRESS, ERROR, COMPLETE };

  enum class HttpRequestFlavor_t { INTERCEPTED = 0, REVPROXY = 1, FWDPROXY = 2, SCHEDULED_UPDATE = 3 };

  ////////////
  // source //
  ////////////
  enum class Source_t {
    NONE = 0,
    HTTP_ORIGIN_SERVER,
    RAW_ORIGIN_SERVER,
    CACHE,
    TRANSFORM,
    INTERNAL // generated from text buffer
  };

  ////////////////////////////////////////////////
  // HttpTransact fills a StateMachineAction_t  //
  // to tell the state machine what to do next. //
  ////////////////////////////////////////////////
  enum class StateMachineAction_t {
    UNDEFINED = 0,

    // AUTH_LOOKUP,
    DNS_LOOKUP,
    DNS_REVERSE_LOOKUP,

    CACHE_LOOKUP,
    CACHE_ISSUE_WRITE,
    CACHE_ISSUE_WRITE_TRANSFORM,
    CACHE_PREPARE_UPDATE,
    CACHE_ISSUE_UPDATE,

    ORIGIN_SERVER_OPEN,
    ORIGIN_SERVER_RAW_OPEN,
    ORIGIN_SERVER_RR_MARK_DOWN,

    READ_PUSH_HDR,
    STORE_PUSH_BODY,

    INTERNAL_CACHE_DELETE,
    INTERNAL_CACHE_NOOP,
    INTERNAL_CACHE_UPDATE_HEADERS,
    INTERNAL_CACHE_WRITE,
    INTERNAL_100_RESPONSE,
    SEND_ERROR_CACHE_NOOP,

    WAIT_FOR_FULL_BODY,
    REQUEST_BUFFER_READ_COMPLETE,
    SERVE_FROM_CACHE,
    SERVER_READ,
    SERVER_PARSE_NEXT_HDR,
    TRANSFORM_READ,
    SSL_TUNNEL,
    CONTINUE,

    API_SM_START,
    API_READ_REQUEST_HDR,
    API_TUNNEL_START,
    API_PRE_REMAP,
    API_POST_REMAP,
    API_OS_DNS,
    API_SEND_REQUEST_HDR,
    API_READ_CACHE_HDR,
    API_CACHE_LOOKUP_COMPLETE,
    API_READ_RESPONSE_HDR,
    API_SEND_RESPONSE_HDR,
    API_SM_SHUTDOWN,

    REMAP_REQUEST,
    POST_REMAP_SKIP,
    REDIRECT_READ
  };

  enum class TransferEncoding_t {
    NONE = 0,
    CHUNKED,
    DEFLATE,
  };

  enum class Variability_t {
    NONE = 0,
    SOME,
    ALL,
  };

  enum class CacheLookupResult_t { NONE, MISS, DOC_BUSY, HIT_STALE, HIT_WARNING, HIT_FRESH, SKIPPED };

  enum class UpdateCachedObject_t { NONE, PREPARE, CONTINUE, ERROR, SUCCEED, FAIL };

  enum class RangeSetup_t {
    NONE = 0,
    REQUESTED,
    NOT_SATISFIABLE,
    NOT_HANDLED,
    NOT_TRANSFORM_REQUESTED,
  };

  enum class CacheAuth_t {
    NONE = 0,
    // TRUE,
    FRESH,
    STALE,
    SERVE
  };

  struct State;
  using TransactFunc_t = void (*)(HttpTransact::State *);

  using CacheDirectives = struct _CacheDirectives {
    bool does_client_permit_lookup      = true;
    bool does_client_permit_storing     = true;
    bool does_client_permit_dns_storing = true;
    bool does_config_permit_lookup      = true;
    bool does_config_permit_storing     = true;
    bool does_server_permit_lookup      = true;
    bool does_server_permit_storing     = true;

    _CacheDirectives() {}
  };

  using CacheLookupInfo = struct _CacheLookupInfo {
    HttpTransact::CacheAction_t action           = CacheAction_t::UNDEFINED;
    HttpTransact::CacheAction_t transform_action = CacheAction_t::UNDEFINED;

    HttpTransact::CacheWriteStatus_t write_status           = CacheWriteStatus_t::NO_WRITE;
    HttpTransact::CacheWriteStatus_t transform_write_status = CacheWriteStatus_t::NO_WRITE;

    URL             *lookup_url = nullptr;
    URL              lookup_url_storage;
    URL              original_url;
    HTTPInfo         object_store;
    HTTPInfo         transform_store;
    CacheDirectives  directives;
    HTTPInfo        *object_read          = nullptr;
    CacheWriteLock_t write_lock_state     = CacheWriteLock_t::INIT;
    int              lookup_count         = 0;
    SquidHitMissCode hit_miss_code        = SQUID_MISS_NONE;
    URL             *parent_selection_url = nullptr;
    URL              parent_selection_url_storage;

    _CacheLookupInfo() {}
  };

  using RedirectInfo = struct _RedirectInfo {
    bool redirect_in_process = false;
    URL  original_url;

    _RedirectInfo() {}
  };

  struct ConnectionAttributes {
    HTTPVersion   http_version;
    HTTPKeepAlive keep_alive = HTTP_KEEPALIVE_UNDEFINED;

    // The following variable is true if the client expects to
    // received a chunked response.
    bool receive_chunked_response = false;
    bool proxy_connect_hdr        = false;
    /// @c errno from the most recent attempt to connect.
    /// zero means no failure (not attempted, succeeded).
    int                connect_result = 0;
    char              *name           = nullptr;
    swoc::IPAddr       name_addr;
    TransferEncoding_t transfer_encoding = TransferEncoding_t::NONE;

    /** This is the source address of the connection from the point of view of the transaction.
        It is the address of the source of the request.
    */
    IpEndpoint src_addr;
    /** This is the destination address of the connection from the point of view of the transaction.
        It is the address of the target of the request.
    */
    IpEndpoint dst_addr;

    ServerState_t                state          = STATE_UNDEFINED;
    AbortState_t                 abort          = ABORT_UNDEFINED;
    HttpProxyPort::TransportType port_attribute = HttpProxyPort::TRANSPORT_DEFAULT;

    /// @c true if the connection is transparent.
    bool       is_transparent = false;
    ProxyError rx_error_code;
    ProxyError tx_error_code;

    bool
    had_connect_fail() const
    {
      return 0 != connect_result;
    }
    void
    clear_connect_fail()
    {
      connect_result = 0;
    }
    ConnectionAttributes() { clear(); }

    void
    clear()
    {
      ink_zero(src_addr);
      ink_zero(dst_addr);
      connect_result = 0;
    }
  };

  using CurrentInfo = struct _CurrentInfo {
    ProxyMode_t                       mode       = ProxyMode_t::UNDEFINED;
    ResolveInfo::UpstreamResolveStyle request_to = ResolveInfo::UNDEFINED_LOOKUP;
    ConnectionAttributes             *server     = nullptr;
    ink_time_t                        now        = 0;
    ServerState_t                     state      = STATE_UNDEFINED;
    class Attempts
    {
    public:
      Attempts()                 = default;
      Attempts(Attempts const &) = delete;

      unsigned
      get() const
      {
        return _v;
      }

      void
      maximize(MgmtInt configured_connect_attempts_max_retries)
      {
        ink_assert(_v <= configured_connect_attempts_max_retries);
        if (_v < configured_connect_attempts_max_retries) {
          ink_assert(0 == _saved_v);
          _saved_v = _v;
          _v       = configured_connect_attempts_max_retries;
        }
      }

      void
      clear()
      {
        _v       = 0;
        _saved_v = 0;
      }

      void
      increment()
      {
        ++_v;
      }

      unsigned
      saved() const
      {
        return _saved_v ? _saved_v : _v;
      }

    private:
      unsigned _v{0}, _saved_v{0};
    };
    Attempts      retry_attempts;
    unsigned      simple_retry_attempts             = 0;
    unsigned      unavailable_server_retry_attempts = 0;
    ParentRetry_t retry_type                        = ParentRetry_t::NONE;

    _CurrentInfo()                     = default;
    _CurrentInfo(_CurrentInfo const &) = delete;
  };

  // Conversion handling for DNS host resolution type.
  static const MgmtConverter HOST_RES_CONV;

  using HeaderInfo = struct _HeaderInfo {
    HTTPHdr         client_request;
    HTTPHdr         client_response;
    HTTPHdr         server_request;
    HTTPHdr         server_response;
    HTTPHdr         transform_response;
    HTTPHdr         cache_request;
    HTTPHdr         cache_response;
    int64_t         request_content_length     = HTTP_UNDEFINED_CL;
    int64_t         response_content_length    = HTTP_UNDEFINED_CL;
    int64_t         transform_request_cl       = HTTP_UNDEFINED_CL;
    int64_t         transform_response_cl      = HTTP_UNDEFINED_CL;
    bool            client_req_is_server_style = false;
    bool            trust_response_cl          = false;
    ResponseError_t response_error             = ResponseError_t::NO_RESPONSE_HEADER_ERROR;
    bool            extension_method           = false;

    _HeaderInfo() {}
  };

  using SquidLogInfo = struct _SquidLogInfo {
    SquidLogCode       log_code      = SQUID_LOG_ERR_UNKNOWN;
    SquidSubcode       subcode       = SQUID_SUBCODE_EMPTY;
    SquidHierarchyCode hier_code     = SQUID_HIER_EMPTY;
    SquidHitMissCode   hit_miss_code = SQUID_MISS_NONE;

    _SquidLogInfo() {}
  };

  using ResponseAction = struct _ResponseAction {
    bool             handled = false;
    TSResponseAction action;

    _ResponseAction() {}
  };

  struct State {
    HttpSM *state_machine = nullptr;

    HttpTransactMagic_t m_magic                = HttpTransactMagic_t::ALIVE;
    HTTPVersion         updated_server_version = HTTP_INVALID;
    CacheLookupResult_t cache_lookup_result    = CacheLookupResult_t::NONE;
    HTTPStatus          http_return_code       = HTTPStatus::NONE;
    CacheAuth_t         www_auth_content       = CacheAuth_t::NONE;

    Arena arena;

    bool force_dns                    = false;
    bool is_upgrade_request           = false;
    bool is_websocket                 = false;
    bool did_upgrade_succeed          = false;
    bool client_connection_allowed    = true;
    bool acl_filtering_performed      = false;
    bool api_cleanup_cache_read       = false;
    bool api_server_response_no_store = false;
    bool api_server_response_ignore   = false;
    bool api_http_sm_shutdown         = false;
    bool api_modifiable_cached_resp   = false;
    bool api_server_request_body_set  = false;
    bool api_req_cacheable            = false;
    bool api_resp_cacheable           = false;
    bool reverse_proxy                = false;
    bool url_remap_success            = false;
    bool api_skip_all_remapping       = false;
    bool already_downgraded           = false;
    bool transparent_passthrough      = false;
    bool range_in_cache               = false;
    bool is_method_stats_incremented  = false;
    bool skip_ip_allow_yaml           = false;

    /// True if the response is cacheable because of negative caching configuration.
    ///
    /// This being true implies the following:
    ///
    /// * The response code was negative.
    /// * Negative caching is enabled.
    /// * The response is considered cacheable because of negative caching
    ///   configuration.
    bool is_cacheable_due_to_negative_caching_configuration = false;

    MgmtByte cache_open_write_fail_action = 0;

    HttpConfigParams           *http_config_param = nullptr;
    CacheLookupInfo             cache_info;
    ResolveInfo                 dns_info;
    RedirectInfo                redirect_info;
    ConnectionTracker::TxnState outbound_conn_track_state;
    ConnectionAttributes        client_info;
    ConnectionAttributes        parent_info;
    ConnectionAttributes        server_info;

    Source_t            source               = Source_t::NONE;
    Source_t            pre_transform_source = Source_t::NONE;
    HttpRequestFlavor_t req_flavor           = HttpRequestFlavor_t::FWDPROXY;

    CurrentInfo  current;
    HeaderInfo   hdr_info;
    SquidLogInfo squid_codes;
    HttpApiInfo  api_info;
    // To handle parent proxy case, we need to be
    //  able to defer some work in building the request
    TransactFunc_t pending_work = nullptr;

    HttpRequestData                           request_data;
    ParentConfigParams                       *parent_params     = nullptr;
    std::shared_ptr<NextHopSelectionStrategy> next_hop_strategy = nullptr;
    ParentResult                              parent_result;
    CacheControlResult                        cache_control;

    StateMachineAction_t next_action                      = StateMachineAction_t::UNDEFINED; // out
    StateMachineAction_t api_next_action                  = StateMachineAction_t::UNDEFINED; // out
    void (*transact_return_point)(HttpTransact::State *s) = nullptr;                         // out

    // We keep this so we can jump back to the upgrade handler after remap is complete
    void (*post_remap_upgrade_return_point)(HttpTransact::State *s) = nullptr; // out
    const char *upgrade_token_wks                                   = nullptr;

    char   *internal_msg_buffer                     = nullptr; // out
    char   *internal_msg_buffer_type                = nullptr; // out
    int64_t internal_msg_buffer_size                = 0;       // out
    int64_t internal_msg_buffer_fast_allocator_size = -1;

    int  scheme                    = -1;     // out
    int  next_hop_scheme           = scheme; // out
    int  orig_scheme               = scheme; // pre-mapped scheme
    int  method                    = 0;
    bool method_metric_incremented = false;

    /// The errno associated with a failed connect attempt.
    ///
    /// This is used for logging and (in some code paths) for determing HTTP
    /// response reason phrases.
    int cause_of_death_errno = -UNKNOWN_INTERNAL_ERROR; // in

    int          api_txn_active_timeout_value      = -1;
    int          api_txn_connect_timeout_value     = -1;
    int          api_txn_dns_timeout_value         = -1;
    int          api_txn_no_activity_timeout_value = -1;
    int          congestion_control_crat           = 0; // Client retry after
    unsigned int filter_mask                       = 0;

    ink_time_t client_request_time    = UNDEFINED_TIME; // internal
    ink_time_t request_sent_time      = UNDEFINED_TIME; // internal
    ink_time_t response_received_time = UNDEFINED_TIME; // internal

    char via_string[MAX_VIA_INDICES + 1];

    RemapPluginInst *os_response_plugin_inst = nullptr;

    // Used by INKHttpTxnCachedReqGet and INKHttpTxnCachedRespGet SDK functions
    // to copy part of HdrHeap (only the writable portion) for cached response headers
    // and request headers
    // These ptrs are deallocate when transaction is over.
    HdrHeapSDKHandle    *cache_req_hdr_heap_handle  = nullptr;
    HdrHeapSDKHandle    *cache_resp_hdr_heap_handle = nullptr;
    UpdateCachedObject_t api_update_cached_object   = UpdateCachedObject_t::NONE;
    StateMachineAction_t saved_update_next_action   = StateMachineAction_t::UNDEFINED;
    CacheAction_t        saved_update_cache_action  = CacheAction_t::UNDEFINED;

    // Remap plugin processor support
    UrlMappingContainer url_map;
    host_hdr_info       hh_info = {nullptr, 0, 0};

    char *remap_redirect = nullptr;
    URL   unmapped_url; // unmapped url is the effective url before remap

    // Http Range: related variables
    RangeSetup_t range_setup      = RangeSetup_t::NONE;
    int64_t      num_range_fields = 0;
    int64_t      range_output_cl  = 0;
    RangeRecord *ranges           = nullptr;

    OverridableHttpConfigParams const *txn_conf = nullptr;
    OverridableHttpConfigParams &
    my_txn_conf() // Storage for plugins, to avoid malloc
    {
      auto p = reinterpret_cast<OverridableHttpConfigParams *>(_my_txn_conf);

      ink_assert(p == txn_conf);

      return *p;
    }

    /** Whether a tunnel is requested to a port which has been dynamically
     * determined by parsing traffic content.
     *
     * Dynamically determined ports require verification against the
     * proxy.config.http.connect_ports.
     */
    bool tunnel_port_is_dynamic = false;

    ResponseAction response_action;

    // Methods
    void
    init()
    {
      parent_params = ParentConfig::acquire();
      new (&dns_info) decltype(dns_info); // reset to default state.
    }

    // Constructor
    State()
    {
      int   i;
      char *via_ptr = via_string;

      for (i = 0; i < MAX_VIA_INDICES; i++) {
        *via_ptr++ = ' ';
      }

      via_string[VIA_CLIENT]                   = VIA_CLIENT_STRING;
      via_string[VIA_CACHE]                    = VIA_CACHE_STRING;
      via_string[VIA_SERVER]                   = VIA_SERVER_STRING;
      via_string[VIA_CACHE_FILL]               = VIA_CACHE_FILL_STRING;
      via_string[VIA_PROXY]                    = VIA_PROXY_STRING;
      via_string[VIA_ERROR]                    = VIA_ERROR_STRING;
      via_string[VIA_ERROR_TYPE]               = VIA_ERROR_NO_ERROR;
      via_string[VIA_DETAIL_SEPARATOR]         = VIA_DETAIL_SEPARATOR_STRING;
      via_string[VIA_DETAIL_TUNNEL_DESCRIPTOR] = VIA_DETAIL_TUNNEL_DESCRIPTOR_STRING;
      via_string[VIA_DETAIL_CACHE_DESCRIPTOR]  = VIA_DETAIL_CACHE_DESCRIPTOR_STRING;
      via_string[VIA_DETAIL_PP_DESCRIPTOR]     = VIA_DETAIL_PP_DESCRIPTOR_STRING;
      via_string[VIA_DETAIL_SERVER_DESCRIPTOR] = VIA_DETAIL_SERVER_DESCRIPTOR_STRING;
      via_string[MAX_VIA_INDICES]              = '\0';

      //      memset(user_args, 0, sizeof(user_args));
      //      memset((void *)&host_db_info, 0, sizeof(host_db_info));
    }

    void
    destroy()
    {
      m_magic = HttpTransactMagic_t::DEAD;

      free_internal_msg_buffer();
      ats_free(internal_msg_buffer_type);

      ParentConfig::release(parent_params);
      parent_params = nullptr;

      hdr_info.client_request.destroy();
      hdr_info.client_response.destroy();
      hdr_info.server_request.destroy();
      hdr_info.server_response.destroy();
      hdr_info.transform_response.destroy();
      hdr_info.cache_request.destroy();
      hdr_info.cache_response.destroy();
      cache_info.lookup_url_storage.destroy();
      cache_info.parent_selection_url_storage.destroy();
      cache_info.original_url.destroy();
      cache_info.object_store.destroy();
      cache_info.transform_store.destroy();
      redirect_info.original_url.destroy();

      url_map.clear();
      arena.reset();
      unmapped_url.clear();
      dns_info.~ResolveInfo();
      outbound_conn_track_state.clear();

      delete[] ranges;
      ranges      = nullptr;
      range_setup = RangeSetup_t::NONE;
      return;
    }

    int64_t state_machine_id() const;

    // Little helper function to setup the per-transaction configuration copy
    void
    setup_per_txn_configs()
    {
      if (txn_conf != reinterpret_cast<OverridableHttpConfigParams *>(_my_txn_conf)) {
        txn_conf = reinterpret_cast<OverridableHttpConfigParams *>(_my_txn_conf);
        memcpy(_my_txn_conf, &http_config_param->oride, sizeof(_my_txn_conf));
      }
    }

    void
    free_internal_msg_buffer()
    {
      if (internal_msg_buffer) {
        if (internal_msg_buffer_fast_allocator_size >= 0) {
          ioBufAllocator[internal_msg_buffer_fast_allocator_size].free_void(internal_msg_buffer);
        } else {
          ats_free(internal_msg_buffer);
        }
        internal_msg_buffer = nullptr;
      }
      internal_msg_buffer_size = 0;
    }

    ProxyProtocol pp_info;

    void
    set_connect_fail(int e)
    {
      int const original_connect_result = this->current.server->connect_result;
      if (e == EUSERS) {
        // EUSERS is used when the number of connections exceeds the configured
        // limit. Since this is not a network connectivity issue with the
        // server, we should not mark it as such. Otherwise we will incorrectly
        // mark the server as down.
        this->current.server->connect_result = 0;
      } else if (e == EIO || this->current.server->connect_result == EIO) {
        this->current.server->connect_result = e;
      }
      if (e != EIO) {
        this->cause_of_death_errno = e;
      }
      Dbg(_dbg_ctl, "Setting upstream connection failure %d to %d", original_connect_result, this->current.server->connect_result);
    }

    MgmtInt
    configured_connect_attempts_max_retries() const
    {
      if (dns_info.looking_up != ResolveInfo::PARENT_PROXY) {
        return txn_conf->connect_attempts_max_retries;
      }
      // For parent proxy, return the maximum retry count for the current parent
      // intead of the max retries for the whole parent group.  The max retry
      // count for the current parent is derived from rounding the current
      // attempt up to next multiple of ppca.
      auto ppca                    = txn_conf->per_parent_connect_attempts;
      auto cur_tries               = current.retry_attempts.get() + 1;
      auto cur_parent_max_attempts = ((cur_tries + ppca - 1) / ppca) * ppca;
      return std::min(cur_parent_max_attempts, txn_conf->parent_connect_attempts) - 1;
    }

  private:
    // Make this a raw byte array, so it will be accessed through the my_txn_conf() member function.
    alignas(OverridableHttpConfigParams) char _my_txn_conf[sizeof(OverridableHttpConfigParams)];

    static DbgCtl _dbg_ctl;

  }; // End of State struct.

  static void HandleBlindTunnel(State *s);
  static void StartRemapRequest(State *s);
  static void EndRemapRequest(State *s);
  static void PerformRemap(State *s);
  static void ModifyRequest(State *s);
  static void HandleRequest(State *s);
  static void HandleRequestBufferDone(State *s);
  static bool handleIfRedirect(State *s);

  static void StartAccessControl(State *s);
  static void HandleRequestAuthorized(State *s);
  static void BadRequest(State *s);
  static void Forbidden(State *s);
  static void SelfLoop(State *s);
  static void TooEarly(State *s);
  static void OriginDown(State *s);
  static void PostActiveTimeoutResponse(State *s);
  static void PostInactiveTimeoutResponse(State *s);
  static void DecideCacheLookup(State *s);
  static void LookupSkipOpenServer(State *s);

  static void CallOSDNSLookup(State *s);
  static void OSDNSLookup(State *s);
  static void PPDNSLookup(State *s);
  static void PPDNSLookupAPICall(State *s);
  static void OriginServerRawOpen(State *s);
  static void HandleCacheOpenRead(State *s);
  static void HandleCacheOpenReadHitFreshness(State *s);
  static void HandleCacheOpenReadHit(State *s);
  static void HandleCacheOpenReadMiss(State *s);
  static void set_cache_prepare_write_action_for_new_request(State *s);
  static void build_response_from_cache(State *s, HTTPWarningCode warning_code);
  static void handle_cache_write_lock(State *s);
  static void HandleResponse(State *s);
  static void HandleUpdateCachedObject(State *s);
  static void HandleUpdateCachedObjectContinue(State *s);
  static void handle_100_continue_response(State *s);
  static void handle_transform_ready(State *s);
  static void handle_transform_cache_write(State *s);
  static void handle_response_from_parent(State *s);
  static void handle_response_from_parent_plugin(State *s);
  static void handle_response_from_server(State *s);
  static void delete_server_rr_entry(State *s, int max_retries);
  static void retry_server_connection_not_open(State *s, ServerState_t conn_state, unsigned max_retries);
  static void error_log_connection_failure(State *s, ServerState_t conn_state);
  static void handle_server_connection_not_open(State *s);
  static void handle_forward_server_connection_open(State *s);
  static void handle_cache_operation_on_forward_server_response(State *s);
  static void handle_no_cache_operation_on_forward_server_response(State *s);
  static void merge_and_update_headers_for_cache_update(State *s);
  static void set_headers_for_cache_write(State *s, HTTPInfo *cache_info, HTTPHdr *request, HTTPHdr *response);
  static void set_header_for_transform(State *s, HTTPHdr *base_header);
  static void merge_response_header_with_cached_header(HTTPHdr *cached_header, HTTPHdr *response_header);
  static void merge_warning_header(HTTPHdr *cached_header, HTTPHdr *response_header);
  static void SetCacheFreshnessLimit(State *s);
  static void HandleApiErrorJump(State *);
  static void handle_websocket_upgrade_pre_remap(State *s);
  static void handle_websocket_upgrade_post_remap(State *s);
  static bool handle_upgrade_request(State *s);
  static void handle_websocket_connection(State *s);

  static void HandleCacheOpenReadPush(State *s, bool read_successful);
  static void HandlePushResponseHdr(State *s);
  static void HandlePushCacheWrite(State *s);
  static void HandlePushTunnelSuccess(State *s);
  static void HandlePushTunnelFailure(State *s);
  static void HandlePushError(State *s, const char *reason);
  static void HandleBadPushRespHdr(State *s);

  // Utility Methods
  static void            issue_revalidate(State *s);
  static bool            get_ka_info_from_config(State *s, ConnectionAttributes *server_info);
  static void            get_ka_info_from_host_db(State *s, ConnectionAttributes *server_info, ConnectionAttributes *client_info,
                                                  HostDBInfo *host_db_info);
  static void            setup_plugin_request_intercept(State *s);
  static void            add_client_ip_to_outgoing_request(State *s, HTTPHdr *request);
  static RequestError_t  check_request_validity(State *s, HTTPHdr *incoming_hdr);
  static ResponseError_t check_response_validity(State *s, HTTPHdr *incoming_hdr);
  static void            set_client_request_state(State *s, HTTPHdr *incoming_hdr);
  static bool            delete_all_document_alternates_and_return(State *s, bool cache_hit);
  static bool does_client_request_permit_cached_response(const OverridableHttpConfigParams *p, CacheControlResult *c, HTTPHdr *h,
                                                         char *via_string);
  static bool does_client_request_permit_dns_caching(CacheControlResult *c, HTTPHdr *h);
  static bool does_client_request_permit_storing(CacheControlResult *c, HTTPHdr *h);
  static bool handle_trace_and_options_requests(State *s, HTTPHdr *incoming_hdr);
  static void bootstrap_state_variables_from_request(State *s, HTTPHdr *incoming_request);

  // WARNING:  this function may be called multiple times for the same transaction.
  //
  static void initialize_state_variables_from_request(State *s, HTTPHdr *obsolete_incoming_request);

  static void initialize_state_variables_from_response(State *s, HTTPHdr *incoming_response);
  static bool is_server_negative_cached(State *s);
  static bool is_cache_response_returnable(State *s);
  static bool is_stale_cache_response_returnable(State *s);
  static bool need_to_revalidate(State *s);
  static bool url_looks_dynamic(URL *url);
  static bool is_request_cache_lookupable(State *s);
  static bool is_request_valid(State *s, HTTPHdr *incoming_request);
  static bool is_request_retryable(State *s);

  static bool is_response_cacheable(State *s, HTTPHdr *request, HTTPHdr *response);
  static bool is_response_valid(State *s, HTTPHdr *incoming_response);

  static void process_quick_http_filter(State *s, int method);
  static bool will_this_request_self_loop(State *s);
  static bool is_request_likely_cacheable(State *s, HTTPHdr *request);
  static bool is_cache_hit(CacheLookupResult_t r);
  static bool is_fresh_cache_hit(CacheLookupResult_t r);

  static void build_request(State *s, HTTPHdr *base_request, HTTPHdr *outgoing_request, HTTPVersion outgoing_version);
  static void build_response(State *s, HTTPHdr *base_response, HTTPHdr *outgoing_response, HTTPVersion outgoing_version,
                             HTTPStatus status_code, const char *reason_phrase = nullptr);
  static void build_response(State *s, HTTPHdr *base_response, HTTPHdr *outgoing_response, HTTPVersion outgoing_version);
  static void build_response(State *s, HTTPHdr *outgoing_response, HTTPVersion outgoing_version, HTTPStatus status_code,
                             const char *reason_phrase = nullptr);

  static void build_response_copy(State *s, HTTPHdr *base_response, HTTPHdr *outgoing_response, HTTPVersion outgoing_version);
  static void handle_content_length_header(State *s, HTTPHdr *header, HTTPHdr *base);
  static void change_response_header_because_of_range_request(State *s, HTTPHdr *header);

  static void             handle_request_keep_alive_headers(State *s, HTTPVersion ver, HTTPHdr *heads);
  static void             handle_response_keep_alive_headers(State *s, HTTPVersion ver, HTTPHdr *heads);
  static int              get_max_age(HTTPHdr *response);
  static int              calculate_document_freshness_limit(State *s, HTTPHdr *response, time_t response_date, bool *heuristic);
  static Freshness_t      what_is_document_freshness(State *s, HTTPHdr *client_request, HTTPHdr *cached_obj_response);
  static Authentication_t AuthenticationNeeded(const OverridableHttpConfigParams *p, HTTPHdr *client_request,
                                               HTTPHdr *obj_response);
  static void             handle_parent_down(State *s);
  static void             handle_server_down(State *s);
  static void             build_error_response(State *s, HTTPStatus status_code, const char *reason_phrase_or_null,
                                               const char *error_body_type);
  static void             build_redirect_response(State *s);
  static const char      *get_error_string(int erno);

  // the stat functions
  static void update_size_and_time_stats(State *s, ink_hrtime total_time, ink_hrtime user_agent_write_time,
                                         ink_hrtime origin_server_read_time, int user_agent_request_header_size,
                                         int64_t user_agent_request_body_size, int user_agent_response_header_size,
                                         int64_t user_agent_response_body_size, int origin_server_request_header_size,
                                         int64_t origin_server_request_body_size, int origin_server_response_header_size,
                                         int64_t origin_server_response_body_size, int pushed_response_header_size,
                                         int64_t pushed_response_body_size, const TransactionMilestones &milestones);
  static void milestone_start_api_time(State *s);
  static void milestone_update_api_time(State *s);
  static void client_result_stat(State *s, ink_hrtime total_time, ink_hrtime request_process_time);
  static void origin_server_connection_speed(ink_hrtime transfer_time, int64_t nbytes);
  static void user_agent_connection_speed(ink_hrtime transfer_time, int64_t nbytes);
  static void delete_warning_value(HTTPHdr *to_warn, HTTPWarningCode warning_code);
  static bool is_connection_collapse_checks_success(State *s); // YTS Team, yamsat
  static void update_method_stat(int method);
};

using TransactEntryFunc_t = void (*)(HttpTransact::State *);

/* The spec says about message body the following:
 *
 * All responses to the HEAD and CONNECT request method
 * MUST NOT include a message-body, even though the presence
 * of entity-header fields might lead one to believe they do.
 *
 * All 1xx (informational), 204 (no content), and 304 (not modified)
 * responses MUST NOT include a message-body.
 *
 * Refer : [https://tools.ietf.org/html/rfc7231#section-4.3.6]
 */
inline bool
is_response_body_precluded(HTTPStatus status_code)
{
  if (((status_code != HTTPStatus::OK) &&
       ((status_code == HTTPStatus::NOT_MODIFIED) || ((status_code < HTTPStatus::OK) && (status_code >= HTTPStatus::CONTINUE)) ||
        (status_code == HTTPStatus::NO_CONTENT)))) {
    return true;
  } else {
    return false;
  }
}

inline bool
is_response_body_precluded(HTTPStatus status_code, int method)
{
  if ((method == HTTP_WKSIDX_HEAD) || (method == HTTP_WKSIDX_CONNECT) || is_response_body_precluded(status_code)) {
    return true;
  } else {
    return false;
  }
}

extern ink_time_t ink_local_time();
