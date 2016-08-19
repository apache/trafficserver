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

#if !defined(_HttpTransact_h_)
#define _HttpTransact_h_

#include "ts/ink_platform.h"
#include "P_HostDB.h"
#include "P_Net.h"
#include "HttpConfig.h"
#include "HTTP.h"
#include "HttpTransactCache.h"
#include "ControlMatcher.h"
#include "CacheControl.h"
#include "ParentSelection.h"
#include "ProxyConfig.h"
#include "Transform.h"
#include "Milestones.h"
//#include "HttpAuthParams.h"
#include "api/ts/remap.h"
#include "RemapPluginInfo.h"
#include "UrlMapping.h"
#include <records/I_RecHttp.h>

#include "congest/Congestion.h"

#define MAX_DNS_LOOKUPS 2

#define HTTP_RELEASE_ASSERT(X) ink_release_assert(X)
// #define ink_cluster_time(X) time(X)

#define ACQUIRE_PRINT_LOCK() // ink_mutex_acquire(&print_lock);
#define RELEASE_PRINT_LOCK() // ink_mutex_release(&print_lock);

#define DUMP_HEADER(T, H, I, S)                                 \
  {                                                             \
    if (diags->on(T)) {                                         \
      ACQUIRE_PRINT_LOCK()                                      \
      fprintf(stderr, "+++++++++ %s +++++++++\n", S);           \
      fprintf(stderr, "-- State Machine Id: %" PRId64 "\n", I); \
      char b[4096];                                             \
      int used, tmp, offset;                                    \
      int done;                                                 \
      offset = 0;                                               \
      if ((H)->valid()) {                                       \
        do {                                                    \
          used = 0;                                             \
          tmp  = offset;                                        \
          done = (H)->print(b, 4095, &used, &tmp);              \
          offset += used;                                       \
          b[used] = '\0';                                       \
          fprintf(stderr, "%s", b);                             \
        } while (!done);                                        \
      }                                                         \
      RELEASE_PRINT_LOCK()                                      \
    }                                                           \
  }

#define TRANSACT_SETUP_RETURN(n, r) \
  s->next_action           = n;     \
  s->transact_return_point = r;     \
  DebugSpecific((s->state_machine && s->state_machine->debug_on), "http_trans", "Next action %s; %s", #n, #r);

#define TRANSACT_RETURN(n, r) \
  TRANSACT_SETUP_RETURN(n, r) \
  return;

#define TRANSACT_RETURN_VAL(n, r, v) \
  TRANSACT_SETUP_RETURN(n, r)        \
  return v;

#define SET_UNPREPARE_CACHE_ACTION(C)                               \
  {                                                                 \
    if (C.action == HttpTransact::CACHE_PREPARE_TO_DELETE) {        \
      C.action = HttpTransact::CACHE_DO_DELETE;                     \
    } else if (C.action == HttpTransact::CACHE_PREPARE_TO_UPDATE) { \
      C.action = HttpTransact::CACHE_DO_UPDATE;                     \
    } else {                                                        \
      C.action = HttpTransact::CACHE_DO_WRITE;                      \
    }                                                               \
  }

typedef time_t ink_time_t;

struct HttpConfigParams;
class HttpSM;

#include "ts/InkErrno.h"
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
  VIA_DETAIL_ICP_DESCRIPTOR,
  VIA_DETAIL_ICP_CONNECT,
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
  VIA_DETAIL_CLUSTER                 = 'L',
  VIA_DETAIL_ICP                     = 'I',
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
  // result of icp suggested host lookup
  VIA_DETAIL_ICP_DESCRIPTOR_STRING = 'i',
  VIA_DETAIL_ICP_SUCCESS           = 'S',
  VIA_DETAIL_ICP_FAILURE           = 'F',
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
  char *parent_proxy_name;
  int parent_proxy_port;
  bool cache_untransformed;
  bool cache_transformed;
  bool logging_enabled;
  bool retry_intercept_failures;

  HttpApiInfo()
    : parent_proxy_name(NULL),
      parent_proxy_port(-1),
      cache_untransformed(false),
      cache_transformed(true),
      logging_enabled(true),
      retry_intercept_failures(false)
  {
  }
};

enum {
  HTTP_UNDEFINED_CL = -1,
};

//////////////////////////////////////////////////////////////////////////////
//
//  HttpTransact
//
//  The HttpTransact class is purely used for scoping and should
//  not be instantiated.
//
//////////////////////////////////////////////////////////////////////////////
#define SET_VIA_STRING(I, S) s->via_string[I] = S;
#define GET_VIA_STRING(I) (s->via_string[I])

class HttpTransact
{
public:
  enum UrlRemapMode_t {
    URL_REMAP_DEFAULT = 0, // which is the same as URL_REMAP_ALL
    URL_REMAP_ALL,
    URL_REMAP_FOR_OS
  };

  enum AbortState_t {
    ABORT_UNDEFINED = 0,
    DIDNOT_ABORT,
    MAYBE_ABORTED,
    ABORTED,
  };

  enum Authentication_t {
    AUTHENTICATION_SUCCESS = 0,
    AUTHENTICATION_MUST_REVALIDATE,
    AUTHENTICATION_MUST_PROXY,
    AUTHENTICATION_CACHE_AUTH
  };

  enum CacheAction_t {
    CACHE_DO_UNDEFINED = 0,
    CACHE_DO_NO_ACTION,
    CACHE_DO_DELETE,
    CACHE_DO_LOOKUP,
    CACHE_DO_REPLACE,
    CACHE_DO_SERVE,
    CACHE_DO_SERVE_AND_DELETE,
    CACHE_DO_SERVE_AND_UPDATE,
    CACHE_DO_UPDATE,
    CACHE_DO_WRITE,
    CACHE_PREPARE_TO_DELETE,
    CACHE_PREPARE_TO_UPDATE,
    CACHE_PREPARE_TO_WRITE,
    TOTAL_CACHE_ACTION_TYPES
  };

  enum CacheOpenWriteFailAction_t {
    CACHE_WL_FAIL_ACTION_DEFAULT                           = 0x00,
    CACHE_WL_FAIL_ACTION_ERROR_ON_MISS                     = 0x01,
    CACHE_WL_FAIL_ACTION_STALE_ON_REVALIDATE               = 0x02,
    CACHE_WL_FAIL_ACTION_ERROR_ON_MISS_STALE_ON_REVALIDATE = 0x03,
    CACHE_WL_FAIL_ACTION_ERROR_ON_MISS_OR_REVALIDATE       = 0x04,
    TOTAL_CACHE_WL_FAIL_ACTION_TYPES
  };

  enum CacheWriteLock_t {
    CACHE_WL_INIT,
    CACHE_WL_SUCCESS,
    CACHE_WL_FAIL,
    CACHE_WL_READ_RETRY,
  };

  enum ClientTransactionResult_t {
    CLIENT_TRANSACTION_RESULT_UNDEFINED,
    CLIENT_TRANSACTION_RESULT_HIT_FRESH,
    CLIENT_TRANSACTION_RESULT_HIT_REVALIDATED,
    CLIENT_TRANSACTION_RESULT_MISS_COLD,
    CLIENT_TRANSACTION_RESULT_MISS_CHANGED,
    CLIENT_TRANSACTION_RESULT_MISS_CLIENT_NO_CACHE,
    CLIENT_TRANSACTION_RESULT_MISS_UNCACHABLE,
    CLIENT_TRANSACTION_RESULT_ERROR_ABORT,
    CLIENT_TRANSACTION_RESULT_ERROR_POSSIBLE_ABORT,
    CLIENT_TRANSACTION_RESULT_ERROR_CONNECT_FAIL,
    CLIENT_TRANSACTION_RESULT_ERROR_OTHER
  };

  enum Freshness_t {
    FRESHNESS_FRESH = 0, // Fresh enough, serve it
    FRESHNESS_WARNING,   // Stale, but client says OK
    FRESHNESS_STALE      // Stale, don't use
  };

  enum HostNameExpansionError_t {
    RETRY_EXPANDED_NAME,
    EXPANSION_FAILED,
    EXPANSION_NOT_ALLOWED,
    DNS_ATTEMPTS_EXHAUSTED,
    NO_PARENT_PROXY_EXPANSION,
    TOTAL_HOST_NAME_EXPANSION_TYPES
  };

  enum HttpTransactMagic_t {
    HTTP_TRANSACT_MAGIC_ALIVE     = 0x00001234,
    HTTP_TRANSACT_MAGIC_DEAD      = 0xDEAD1234,
    HTTP_TRANSACT_MAGIC_SEPARATOR = 0x12345678
  };

  enum LookingUp_t {
    ORIGIN_SERVER,
    UNDEFINED_LOOKUP,
    ICP_SUGGESTED_HOST,
    PARENT_PROXY,
    INCOMING_ROUTER,
    HOST_NONE,
  };

  enum ProxyMode_t {
    UNDEFINED_MODE,
    GENERIC_PROXY,
    TUNNELLING_PROXY,
  };

  enum RequestError_t {
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
    TOTAL_REQUEST_ERROR_TYPES
  };

  enum ResponseError_t {
    NO_RESPONSE_HEADER_ERROR,
    BOGUS_OR_NO_DATE_IN_RESPONSE,
    CONNECTION_OPEN_FAILED,
    MISSING_REASON_PHRASE,
    MISSING_STATUS_CODE,
    NON_EXISTANT_RESPONSE_HEADER,
    NOT_A_RESPONSE_HEADER,
    STATUS_CODE_SERVER_ERROR,
    TOTAL_RESPONSE_ERROR_TYPES
  };

  // Please do not forget to fix TSServerState (ts/ts.h)
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
    CONGEST_CONTROL_CONGESTED_ON_F,
    CONGEST_CONTROL_CONGESTED_ON_M,
    PARENT_ORIGIN_RETRY
  };

  enum CacheWriteStatus_t {
    NO_CACHE_WRITE = 0,
    CACHE_WRITE_LOCK_MISS,
    CACHE_WRITE_IN_PROGRESS,
    CACHE_WRITE_ERROR,
    CACHE_WRITE_COMPLETE
  };

  enum HttpRequestFlavor_t {
    REQ_FLAVOR_INTERCEPTED      = 0,
    REQ_FLAVOR_REVPROXY         = 1,
    REQ_FLAVOR_FWDPROXY         = 2,
    REQ_FLAVOR_SCHEDULED_UPDATE = 3
  };

  ////////////
  // source //
  ////////////
  enum Source_t {
    SOURCE_NONE = 0,
    SOURCE_HTTP_ORIGIN_SERVER,
    SOURCE_RAW_ORIGIN_SERVER,
    SOURCE_CACHE,
    SOURCE_TRANSFORM,
    SOURCE_INTERNAL // generated from text buffer
  };

  ////////////////////////////////////////////////
  // HttpTransact fills a StateMachineAction_t  //
  // to tell the state machine what to do next. //
  ////////////////////////////////////////////////
  enum StateMachineAction_t {
    SM_ACTION_UNDEFINED = 0,

    // SM_ACTION_AUTH_LOOKUP,
    SM_ACTION_DNS_LOOKUP,
    SM_ACTION_DNS_REVERSE_LOOKUP,

    SM_ACTION_CACHE_LOOKUP,
    SM_ACTION_CACHE_ISSUE_WRITE,
    SM_ACTION_CACHE_ISSUE_WRITE_TRANSFORM,
    SM_ACTION_CACHE_PREPARE_UPDATE,
    SM_ACTION_CACHE_ISSUE_UPDATE,

    SM_ACTION_ICP_QUERY,

    SM_ACTION_ORIGIN_SERVER_OPEN,
    SM_ACTION_ORIGIN_SERVER_RAW_OPEN,
    SM_ACTION_ORIGIN_SERVER_RR_MARK_DOWN,

    SM_ACTION_READ_PUSH_HDR,
    SM_ACTION_STORE_PUSH_BODY,

    SM_ACTION_INTERNAL_CACHE_DELETE,
    SM_ACTION_INTERNAL_CACHE_NOOP,
    SM_ACTION_INTERNAL_CACHE_UPDATE_HEADERS,
    SM_ACTION_INTERNAL_CACHE_WRITE,
    SM_ACTION_INTERNAL_100_RESPONSE,
    SM_ACTION_INTERNAL_REQUEST,
    SM_ACTION_SEND_ERROR_CACHE_NOOP,

#ifdef PROXY_DRAIN
    SM_ACTION_DRAIN_REQUEST_BODY,
#endif /* PROXY_DRAIN */

    SM_ACTION_SERVE_FROM_CACHE,
    SM_ACTION_SERVER_READ,
    SM_ACTION_SERVER_PARSE_NEXT_HDR,
    SM_ACTION_TRANSFORM_READ,
    SM_ACTION_SSL_TUNNEL,
    SM_ACTION_CONTINUE,

    SM_ACTION_API_SM_START,
    SM_ACTION_API_READ_REQUEST_HDR,
    SM_ACTION_API_PRE_REMAP,
    SM_ACTION_API_POST_REMAP,
    SM_ACTION_API_OS_DNS,
    SM_ACTION_API_SEND_REQUEST_HDR,
    SM_ACTION_API_READ_CACHE_HDR,
    SM_ACTION_API_CACHE_LOOKUP_COMPLETE,
    SM_ACTION_API_READ_RESPONSE_HDR,
    SM_ACTION_API_SEND_RESPONSE_HDR,
    SM_ACTION_API_SM_SHUTDOWN,

    SM_ACTION_REMAP_REQUEST,
    SM_ACTION_POST_REMAP_SKIP,
    SM_ACTION_REDIRECT_READ
  };

  enum TransferEncoding_t {
    NO_TRANSFER_ENCODING = 0,
    CHUNKED_ENCODING,
    DEFLATE_ENCODING,
  };

  enum Variability_t {
    VARIABILITY_NONE = 0,
    VARIABILITY_SOME,
    VARIABILITY_ALL,
  };

  struct StatRecord_t {
    uint16_t index;
    int64_t increment;
  };

  enum CacheLookupResult_t {
    CACHE_LOOKUP_NONE,
    CACHE_LOOKUP_MISS,
    CACHE_LOOKUP_DOC_BUSY,
    CACHE_LOOKUP_HIT_STALE,
    CACHE_LOOKUP_HIT_WARNING,
    CACHE_LOOKUP_HIT_FRESH,
    CACHE_LOOKUP_SKIPPED
  };

  enum UpdateCachedObject_t {
    UPDATE_CACHED_OBJECT_NONE,
    UPDATE_CACHED_OBJECT_PREPARE,
    UPDATE_CACHED_OBJECT_CONTINUE,
    UPDATE_CACHED_OBJECT_ERROR,
    UPDATE_CACHED_OBJECT_SUCCEED,
    UPDATE_CACHED_OBJECT_FAIL
  };

  enum LockUrl_t {
    LOCK_URL_FIRST = 0,
    LOCK_URL_SECOND,
    LOCK_URL_ORIGINAL,
    LOCK_URL_DONE,
    LOCK_URL_QUIT,
  };

  enum RangeSetup_t {
    RANGE_NONE = 0,
    RANGE_REQUESTED,
    RANGE_NOT_SATISFIABLE,
    RANGE_NOT_HANDLED,
    RANGE_NOT_TRANSFORM_REQUESTED,
  };

  enum CacheAuth_t {
    CACHE_AUTH_NONE = 0,
    // CACHE_AUTH_TRUE,
    CACHE_AUTH_FRESH,
    CACHE_AUTH_STALE,
    CACHE_AUTH_SERVE
  };

  struct State;
  typedef void (*TransactFunc_t)(HttpTransact::State *);

  typedef struct _CacheDirectives {
    bool does_client_permit_lookup;
    bool does_client_permit_storing;
    bool does_client_permit_dns_storing;
    bool does_config_permit_lookup;
    bool does_config_permit_storing;
    bool does_server_permit_lookup;
    bool does_server_permit_storing;

    _CacheDirectives()
      : does_client_permit_lookup(true),
        does_client_permit_storing(true),
        does_client_permit_dns_storing(true),
        does_config_permit_lookup(true),
        does_config_permit_storing(true),
        does_server_permit_lookup(true),
        does_server_permit_storing(true)
    {
    }
  } CacheDirectives;

  typedef struct _CacheLookupInfo {
    HttpTransact::CacheAction_t action;
    HttpTransact::CacheAction_t transform_action;

    HttpTransact::CacheWriteStatus_t write_status;
    HttpTransact::CacheWriteStatus_t transform_write_status;

    URL *lookup_url;
    URL lookup_url_storage;
    URL original_url;
    HTTPInfo *object_read;
    HTTPInfo *second_object_read;
    HTTPInfo object_store;
    HTTPInfo transform_store;
    CacheLookupHttpConfig config;
    CacheDirectives directives;
    int open_read_retries;
    int open_write_retries;
    CacheWriteLock_t write_lock_state;
    int lookup_count;
    SquidHitMissCode hit_miss_code;

    _CacheLookupInfo()
      : action(CACHE_DO_UNDEFINED),
        transform_action(CACHE_DO_UNDEFINED),
        write_status(NO_CACHE_WRITE),
        transform_write_status(NO_CACHE_WRITE),
        lookup_url(NULL),
        lookup_url_storage(),
        original_url(),
        object_read(NULL),
        second_object_read(NULL),
        object_store(),
        transform_store(),
        config(),
        directives(),
        open_read_retries(0),
        open_write_retries(0),
        write_lock_state(CACHE_WL_INIT),
        lookup_count(0),
        hit_miss_code(SQUID_MISS_NONE)
    {
    }
  } CacheLookupInfo;

  typedef struct _RedirectInfo {
    bool redirect_in_process;
    URL original_url;
    URL redirect_url;

    _RedirectInfo() : redirect_in_process(false), original_url(), redirect_url() {}
  } RedirectInfo;

  struct ConnectionAttributes {
    HTTPVersion http_version;
    HTTPKeepAlive keep_alive;

    // The following variable is true if the client expects to
    // received a chunked response.
    bool receive_chunked_response;
    bool pipeline_possible;
    bool proxy_connect_hdr;
    /// @c errno from the most recent attempt to connect.
    /// zero means no failure (not attempted, succeeded).
    int connect_result;
    char *name;
    TransferEncoding_t transfer_encoding;

    /** This is the source address of the connection from the point of view of the transaction.
        It is the address of the source of the request.
    */
    IpEndpoint src_addr;
    /** This is the destination address of the connection from the point of view of the transaction.
        It is the address of the target of the request.
    */
    IpEndpoint dst_addr;

    ServerState_t state;
    AbortState_t abort;
    HttpProxyPort::TransportType port_attribute;

    /// @c true if the connection is transparent.
    bool is_transparent;

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
    void
    set_connect_fail(int e)
    {
      connect_result = e;
    }

    ConnectionAttributes()
      : http_version(),
        keep_alive(HTTP_KEEPALIVE_UNDEFINED),
        receive_chunked_response(false),
        pipeline_possible(false),
        proxy_connect_hdr(false),
        connect_result(0),
        name(NULL),
        transfer_encoding(NO_TRANSFER_ENCODING),
        state(STATE_UNDEFINED),
        abort(ABORT_UNDEFINED),
        port_attribute(HttpProxyPort::TRANSPORT_DEFAULT),
        is_transparent(false)
    {
      memset(&src_addr, 0, sizeof(src_addr));
      memset(&dst_addr, 0, sizeof(dst_addr));
    }
  };

  typedef struct _CurrentInfo {
    ProxyMode_t mode;
    LookingUp_t request_to;
    ConnectionAttributes *server;
    ink_time_t now;
    ServerState_t state;
    unsigned attempts;
    unsigned simple_retry_attempts;
    unsigned unavailable_server_retry_attempts;
    ParentRetry_t retry_type;

    _CurrentInfo()
      : mode(UNDEFINED_MODE),
        request_to(UNDEFINED_LOOKUP),
        server(NULL),
        now(0),
        state(STATE_UNDEFINED),
        attempts(1),
        simple_retry_attempts(0),
        unavailable_server_retry_attempts(0),
        retry_type(PARENT_RETRY_NONE){};
  } CurrentInfo;

  typedef struct _DNSLookupInfo {
    int attempts;
    /** Origin server address source selection.

        If config says to use CTA (client target addr) state is
        OS_ADDR_TRY_CLIENT, otherwise it remains the default. If the
        connect fails then we switch to a USE. We go to USE_HOSTDB if
        (1) the HostDB lookup is successful and (2) some address other
        than the CTA is available to try. Otherwise we keep retrying
        on the CTA (USE_CLIENT) up to the max retry value.  In essence
        we try to treat the CTA as if it were another RR value in the
        HostDB record.
     */
    enum {
      OS_ADDR_TRY_DEFAULT, ///< Initial state, use what config says.
      OS_ADDR_TRY_HOSTDB,  ///< Try HostDB data.
      OS_ADDR_TRY_CLIENT,  ///< Try client target addr.
      OS_ADDR_USE_HOSTDB,  ///< Force use of HostDB target address.
      OS_ADDR_USE_CLIENT   ///< Use client target addr, no fallback.
    } os_addr_style;

    bool lookup_success;
    char *lookup_name;
    char srv_hostname[MAXDNAME];
    LookingUp_t looking_up;
    bool srv_lookup_success;
    short srv_port;
    HostDBApplicationInfo srv_app;
    /*** Set to true by default.  If use_client_target_address is set
     * to 1, this value will be set to false if the client address is
     * not in the DNS pool */
    bool lookup_validated;

    _DNSLookupInfo()
      : attempts(0),
        os_addr_style(OS_ADDR_TRY_DEFAULT),
        lookup_success(false),
        lookup_name(NULL),
        looking_up(UNDEFINED_LOOKUP),
        srv_lookup_success(false),
        srv_port(0),
        lookup_validated(true)
    {
      srv_hostname[0]                = '\0';
      srv_app.allotment.application1 = 0;
      srv_app.allotment.application2 = 0;
    }
  } DNSLookupInfo;

  typedef struct _HeaderInfo {
    HTTPHdr client_request;
    HTTPHdr client_response;
    HTTPHdr server_request;
    HTTPHdr server_response;
    HTTPHdr transform_response;
    HTTPHdr cache_response;
    int64_t request_content_length;
    int64_t response_content_length;
    int64_t transform_request_cl;
    int64_t transform_response_cl;
    bool client_req_is_server_style;
    bool trust_response_cl;
    ResponseError_t response_error;
    bool extension_method;

    _HeaderInfo()
      : client_request(),
        client_response(),
        server_request(),
        server_response(),
        transform_response(),
        cache_response(),
        request_content_length(HTTP_UNDEFINED_CL),
        response_content_length(HTTP_UNDEFINED_CL),
        transform_request_cl(HTTP_UNDEFINED_CL),
        transform_response_cl(HTTP_UNDEFINED_CL),
        client_req_is_server_style(false),
        trust_response_cl(false),
        response_error(NO_RESPONSE_HEADER_ERROR),
        extension_method(false)
    {
    }
  } HeaderInfo;

  typedef struct _SquidLogInfo {
    SquidLogCode log_code;
    SquidHierarchyCode hier_code;
    SquidHitMissCode hit_miss_code;

    _SquidLogInfo() : log_code(SQUID_LOG_ERR_UNKNOWN), hier_code(SQUID_HIER_EMPTY), hit_miss_code(SQUID_MISS_NONE) {}
  } SquidLogInfo;

#define HTTP_TRANSACT_STATE_MAX_XBUF_SIZE (1024 * 2) /* max size of plugin exchange buffer */

  struct State {
    HttpTransactMagic_t m_magic;

    HttpSM *state_machine;

    Arena arena;

    HttpConfigParams *http_config_param;
    CacheLookupInfo cache_info;
    DNSLookupInfo dns_info;
    bool force_dns;
    RedirectInfo redirect_info;
    unsigned int updated_server_version;
    unsigned int cache_open_write_fail_action;
    bool is_revalidation_necessary; // Added to check if revalidation is necessary - YTS Team, yamsat
    bool request_will_not_selfloop; // To determine if process done - YTS Team, yamsat
    ConnectionAttributes client_info;
    ConnectionAttributes icp_info;
    ConnectionAttributes parent_info;
    ConnectionAttributes server_info;
    // ConnectionAttributes     router_info;

    Source_t source;
    Source_t pre_transform_source;
    HttpRequestFlavor_t req_flavor;

    CurrentInfo current;
    HeaderInfo hdr_info;
    SquidLogInfo squid_codes;
    HttpApiInfo api_info;
    // To handle parent proxy case, we need to be
    //  able to defer some work in building the request
    TransactFunc_t pending_work;

    // Sandbox of Variables
    StateMachineAction_t cdn_saved_next_action;
    void (*cdn_saved_transact_return_point)(State *s);
    bool cdn_remap_complete;
    bool first_dns_lookup;

    bool backdoor_request; // internal
    bool cop_test_page;    // internal
    HttpRequestData request_data;
    ParentConfigParams *parent_params;
    ParentResult parent_result;
    CacheControlResult cache_control;
    CacheLookupResult_t cache_lookup_result;
    // FilterResult             content_control;

    StateMachineAction_t next_action;                      // out
    StateMachineAction_t api_next_action;                  // out
    void (*transact_return_point)(HttpTransact::State *s); // out

    // We keep this so we can jump back to the upgrade handler after remap is complete
    void (*post_remap_upgrade_return_point)(HttpTransact::State *s); // out
    const char *upgrade_token_wks;
    bool is_upgrade_request;

    // Some WebSocket state
    bool is_websocket;
    bool did_upgrade_succeed;

    // Some queue info
    bool origin_request_queued;

    char *internal_msg_buffer;        // out
    char *internal_msg_buffer_type;   // out
    int64_t internal_msg_buffer_size; // out
    int64_t internal_msg_buffer_fast_allocator_size;

    struct sockaddr_in icp_ip_result; // in
    bool icp_lookup_success;          // in

    int scheme;          // out
    int next_hop_scheme; // out
    int orig_scheme;     // pre-mapped scheme
    int method;
    int cause_of_death_errno; // in
    HostDBInfo host_db_info;  // in

    ink_time_t client_request_time;    // internal
    ink_time_t request_sent_time;      // internal
    ink_time_t response_received_time; // internal
    ink_time_t plugin_set_expire_time;

    char via_string[MAX_VIA_INDICES + 1];

    int64_t state_machine_id;

    // HttpAuthParams auth_params;

    // new ACL filtering result (calculated immediately after remap)
    bool client_connection_enabled;
    bool acl_filtering_performed;

    // for negative caching
    bool negative_caching;
    // for srv_lookup
    bool srv_lookup;
    // for authenticated content caching
    CacheAuth_t www_auth_content;

    // INK API/Remap API plugin interface
    void *remap_plugin_instance;
    void *user_args[HTTP_SSN_TXN_MAX_USER_ARG];
    remap_plugin_info::_tsremap_os_response *fp_tsremap_os_response;
    HTTPStatus http_return_code;

    int api_txn_active_timeout_value;
    int api_txn_connect_timeout_value;
    int api_txn_dns_timeout_value;
    int api_txn_no_activity_timeout_value;

    // Used by INKHttpTxnCachedReqGet and INKHttpTxnCachedRespGet SDK functions
    // to copy part of HdrHeap (only the writable portion) for cached response headers
    // and request headers
    // These ptrs are deallocate when transaction is over.
    HdrHeapSDKHandle *cache_req_hdr_heap_handle;
    HdrHeapSDKHandle *cache_resp_hdr_heap_handle;
    bool api_cleanup_cache_read;
    bool api_server_response_no_store;
    bool api_server_response_ignore;
    bool api_http_sm_shutdown;
    bool api_modifiable_cached_resp;
    bool api_server_request_body_set;
    bool api_req_cacheable;
    bool api_resp_cacheable;
    bool api_server_addr_set;
    bool stale_icp_lookup;
    UpdateCachedObject_t api_update_cached_object;
    LockUrl_t api_lock_url;
    StateMachineAction_t saved_update_next_action;
    CacheAction_t saved_update_cache_action;

    // Remap plugin processor support
    UrlMappingContainer url_map;
    host_hdr_info hh_info;

    // congestion control
    CongestionEntry *pCongestionEntry;
    StateMachineAction_t congest_saved_next_action;
    int congestion_control_crat; // 'client retry after'
    int congestion_congested_or_failed;
    int congestion_connection_opened;

    unsigned int filter_mask;
    char *remap_redirect;
    bool reverse_proxy;
    bool url_remap_success;

    bool api_skip_all_remapping;

    bool already_downgraded;
    URL pristine_url; // pristine url is the url before remap

    // Http Range: related variables
    RangeSetup_t range_setup;
    int64_t num_range_fields;
    int64_t range_output_cl;
    RangeRecord *ranges;

    OverridableHttpConfigParams *txn_conf;
    OverridableHttpConfigParams my_txn_conf; // Storage for plugins, to avoid malloc

    bool transparent_passthrough;
    bool range_in_cache;

    // Methods
    void
    init()
    {
      parent_params = ParentConfig::acquire();
    }

    // Constructor
    State()
      : m_magic(HTTP_TRANSACT_MAGIC_ALIVE),
        state_machine(NULL),
        http_config_param(NULL),
        force_dns(false),
        updated_server_version(HostDBApplicationInfo::HTTP_VERSION_UNDEFINED),
        cache_open_write_fail_action(0),
        is_revalidation_necessary(false),
        request_will_not_selfloop(false), // YTS Team, yamsat
        source(SOURCE_NONE),
        pre_transform_source(SOURCE_NONE),
        req_flavor(REQ_FLAVOR_FWDPROXY),
        pending_work(NULL),
        cdn_saved_next_action(SM_ACTION_UNDEFINED),
        cdn_saved_transact_return_point(NULL),
        cdn_remap_complete(false),
        first_dns_lookup(true),
        backdoor_request(false),
        cop_test_page(false),
        parent_params(NULL),
        cache_lookup_result(CACHE_LOOKUP_NONE),
        next_action(SM_ACTION_UNDEFINED),
        api_next_action(SM_ACTION_UNDEFINED),
        transact_return_point(NULL),
        post_remap_upgrade_return_point(NULL),
        upgrade_token_wks(NULL),
        is_upgrade_request(false),
        is_websocket(false),
        did_upgrade_succeed(false),
        origin_request_queued(false),
        internal_msg_buffer(NULL),
        internal_msg_buffer_type(NULL),
        internal_msg_buffer_size(0),
        internal_msg_buffer_fast_allocator_size(-1),
        icp_lookup_success(false),
        scheme(-1),
        next_hop_scheme(scheme),
        orig_scheme(scheme),
        method(0),
        cause_of_death_errno(-UNKNOWN_INTERNAL_ERROR),
        client_request_time(UNDEFINED_TIME),
        request_sent_time(UNDEFINED_TIME),
        response_received_time(UNDEFINED_TIME),
        plugin_set_expire_time(UNDEFINED_TIME),
        state_machine_id(0),
        client_connection_enabled(true),
        acl_filtering_performed(false),
        negative_caching(false),
        srv_lookup(false),
        www_auth_content(CACHE_AUTH_NONE),
        remap_plugin_instance(0),
        fp_tsremap_os_response(NULL),
        http_return_code(HTTP_STATUS_NONE),
        api_txn_active_timeout_value(-1),
        api_txn_connect_timeout_value(-1),
        api_txn_dns_timeout_value(-1),
        api_txn_no_activity_timeout_value(-1),
        cache_req_hdr_heap_handle(NULL),
        cache_resp_hdr_heap_handle(NULL),
        api_cleanup_cache_read(false),
        api_server_response_no_store(false),
        api_server_response_ignore(false),
        api_http_sm_shutdown(false),
        api_modifiable_cached_resp(false),
        api_server_request_body_set(false),
        api_req_cacheable(false),
        api_resp_cacheable(false),
        api_server_addr_set(false),
        stale_icp_lookup(false),
        api_update_cached_object(UPDATE_CACHED_OBJECT_NONE),
        api_lock_url(LOCK_URL_FIRST),
        saved_update_next_action(SM_ACTION_UNDEFINED),
        saved_update_cache_action(CACHE_DO_UNDEFINED),
        url_map(),
        pCongestionEntry(NULL),
        congest_saved_next_action(SM_ACTION_UNDEFINED),
        congestion_control_crat(0),
        congestion_congested_or_failed(0),
        congestion_connection_opened(0),
        filter_mask(0),
        remap_redirect(NULL),
        reverse_proxy(false),
        url_remap_success(false),
        api_skip_all_remapping(false),
        already_downgraded(false),
        pristine_url(),
        range_setup(RANGE_NONE),
        num_range_fields(0),
        range_output_cl(0),
        ranges(NULL),
        txn_conf(NULL),
        transparent_passthrough(false),
        range_in_cache(false)
    {
      int i;
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
      via_string[VIA_DETAIL_ICP_DESCRIPTOR]    = VIA_DETAIL_ICP_DESCRIPTOR_STRING;
      via_string[VIA_DETAIL_PP_DESCRIPTOR]     = VIA_DETAIL_PP_DESCRIPTOR_STRING;
      via_string[VIA_DETAIL_SERVER_DESCRIPTOR] = VIA_DETAIL_SERVER_DESCRIPTOR_STRING;
      via_string[MAX_VIA_INDICES]              = '\0';

      memset(user_args, 0, sizeof(user_args));
      memset(&host_db_info, 0, sizeof(host_db_info));
    }

    void
    destroy()
    {
      m_magic = HTTP_TRANSACT_MAGIC_DEAD;

      free_internal_msg_buffer();
      ats_free(internal_msg_buffer_type);

      ParentConfig::release(parent_params);
      parent_params = NULL;

      hdr_info.client_request.destroy();
      hdr_info.client_response.destroy();
      hdr_info.server_request.destroy();
      hdr_info.server_response.destroy();
      hdr_info.transform_response.destroy();
      hdr_info.cache_response.destroy();
      cache_info.lookup_url_storage.destroy();
      cache_info.original_url.destroy();
      cache_info.object_store.destroy();
      cache_info.transform_store.destroy();
      redirect_info.original_url.destroy();
      redirect_info.redirect_url.destroy();

      if (pCongestionEntry) {
        if (congestion_connection_opened == 1) {
          pCongestionEntry->connection_closed();
          congestion_connection_opened = 0;
        }
        pCongestionEntry->put(), pCongestionEntry = NULL;
      }

      url_map.clear();
      arena.reset();
      pristine_url.clear();

      delete[] ranges;
      ranges      = NULL;
      range_setup = RANGE_NONE;
      return;
    }

    // Little helper function to setup the per-transaction configuration copy
    void
    setup_per_txn_configs()
    {
      if (txn_conf != &my_txn_conf) {
        // Make sure we copy it first.
        memcpy(&my_txn_conf, &http_config_param->oride, sizeof(my_txn_conf));
        txn_conf = &my_txn_conf;
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
        internal_msg_buffer = NULL;
      }
      internal_msg_buffer_size = 0;
    }
  }; // End of State struct.

  static void HandleBlindTunnel(State *s);
  static void StartRemapRequest(State *s);
  static void RemapRequest(State *s);
  static void EndRemapRequest(State *s);
  static void PerformRemap(State *s);
  static void ModifyRequest(State *s);
  static void HandleRequest(State *s);
  static bool handleIfRedirect(State *s);

  static void StartAccessControl(State *s);
  static void StartAuth(State *s);
  static void HandleRequestAuthorized(State *s);
  static void BadRequest(State *s);
  static void HandleFiltering(State *s);
  static void DecideCacheLookup(State *s);
  static void LookupSkipOpenServer(State *s);

  static void CallOSDNSLookup(State *s);
  static void OSDNSLookup(State *s);
  static void ReDNSRoundRobin(State *s);
  static void PPDNSLookup(State *s);
  static void HandleAuth(State *s);
  static void HandleAuthFailed(State *s);
  static void OriginServerRawOpen(State *s);
  static void HandleCacheOpenRead(State *s);
  static void HandleCacheOpenReadHitFreshness(State *s);
  static void HandleCacheOpenReadHit(State *s);
  static void HandleCacheOpenReadMiss(State *s);
  static void build_response_from_cache(State *s, HTTPWarningCode warning_code);
  static void handle_cache_write_lock(State *s);
  static void HandleICPLookup(State *s);
  static void HandleResponse(State *s);
  static void HandleUpdateCachedObject(State *s);
  static void HandleUpdateCachedObjectContinue(State *s);
  static void HandleStatPage(State *s);
  static void handle_100_continue_response(State *s);
  static void handle_transform_ready(State *s);
  static void handle_transform_cache_write(State *s);
  static void handle_response_from_icp_suggested_host(State *s);
  static void handle_response_from_parent(State *s);
  static void handle_response_from_server(State *s);
  static void delete_server_rr_entry(State *s, int max_retries);
  static void retry_server_connection_not_open(State *s, ServerState_t conn_state, unsigned max_retries);
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
  static void issue_revalidate(State *s);
  static bool get_ka_info_from_config(State *s, ConnectionAttributes *server_info);
  static void get_ka_info_from_host_db(State *s, ConnectionAttributes *server_info, ConnectionAttributes *client_info,
                                       HostDBInfo *host_db_info);
  static bool service_transaction_in_proxy_only_mode(State *s);
  static void setup_plugin_request_intercept(State *s);
  static void add_client_ip_to_outgoing_request(State *s, HTTPHdr *request);
  static RequestError_t check_request_validity(State *s, HTTPHdr *incoming_hdr);
  static ResponseError_t check_response_validity(State *s, HTTPHdr *incoming_hdr);
  static bool delete_all_document_alternates_and_return(State *s, bool cache_hit);
  static bool did_forward_server_send_0_9_response(State *s);
  static bool does_client_request_permit_cached_response(const OverridableHttpConfigParams *p, CacheControlResult *c, HTTPHdr *h,
                                                         char *via_string);
  static bool does_client_request_permit_dns_caching(CacheControlResult *c, HTTPHdr *h);
  static bool does_client_request_permit_storing(CacheControlResult *c, HTTPHdr *h);
  static bool handle_internal_request(State *s, HTTPHdr *incoming_hdr);
  static bool handle_trace_and_options_requests(State *s, HTTPHdr *incoming_hdr);
  static void bootstrap_state_variables_from_request(State *s, HTTPHdr *incoming_request);
  static void initialize_state_variables_for_origin_server(State *s, HTTPHdr *incoming_request, bool second_time);
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
  static bool perform_accept_encoding_filtering(State *s);

  static HostNameExpansionError_t try_to_expand_host_name(State *s);

  static bool setup_auth_lookup(State *s);
  static bool will_this_request_self_loop(State *s);
  static bool is_request_likely_cacheable(State *s, HTTPHdr *request);

  static void build_request(State *s, HTTPHdr *base_request, HTTPHdr *outgoing_request, HTTPVersion outgoing_version);
  static void build_response(State *s, HTTPHdr *base_response, HTTPHdr *outgoing_response, HTTPVersion outgoing_version,
                             HTTPStatus status_code, const char *reason_phrase = NULL);
  static void build_response(State *s, HTTPHdr *base_response, HTTPHdr *outgoing_response, HTTPVersion outgoing_version);
  static void build_response(State *s, HTTPHdr *outgoing_response, HTTPVersion outgoing_version, HTTPStatus status_code,
                             const char *reason_phrase = NULL);

  static void build_response_copy(State *s, HTTPHdr *base_response, HTTPHdr *outgoing_response, HTTPVersion outgoing_version);
  static void handle_content_length_header(State *s, HTTPHdr *header, HTTPHdr *base);
  static void change_response_header_because_of_range_request(State *s, HTTPHdr *header);

  static void handle_request_keep_alive_headers(State *s, HTTPVersion ver, HTTPHdr *heads);
  static void handle_response_keep_alive_headers(State *s, HTTPVersion ver, HTTPHdr *heads);
  static int calculate_document_freshness_limit(State *s, HTTPHdr *response, time_t response_date, bool *heuristic);
  static int calculate_freshness_fuzz(State *s, int fresh_limit);
  static Freshness_t what_is_document_freshness(State *s, HTTPHdr *client_request, HTTPHdr *cached_obj_response);
  static Authentication_t AuthenticationNeeded(const OverridableHttpConfigParams *p, HTTPHdr *client_request,
                                               HTTPHdr *obj_response);
  static void handle_parent_died(State *s);
  static void handle_server_died(State *s);
  static void build_error_response(State *s, HTTPStatus status_code, const char *reason_phrase_or_null, const char *error_body_type,
                                   const char *format, ...);
  static void build_redirect_response(State *s);
  static void build_upgrade_response(State *s);
  static const char *get_error_string(int erno);

  // the stat functions
  static void update_size_and_time_stats(State *s, ink_hrtime total_time, ink_hrtime user_agent_write_time,
                                         ink_hrtime origin_server_read_time, int user_agent_request_header_size,
                                         int64_t user_agent_request_body_size, int user_agent_response_header_size,
                                         int64_t user_agent_response_body_size, int origin_server_request_header_size,
                                         int64_t origin_server_request_body_size, int origin_server_response_header_size,
                                         int64_t origin_server_response_body_size, int pushed_response_header_size,
                                         int64_t pushed_response_body_size, const TransactionMilestones &milestones);
  static void histogram_request_document_size(State *s, int64_t size);
  static void histogram_response_document_size(State *s, int64_t size);
  static void user_agent_connection_speed(State *s, ink_hrtime transfer_time, int64_t nbytes);
  static void origin_server_connection_speed(State *s, ink_hrtime transfer_time, int64_t nbytes);
  static void client_result_stat(State *s, ink_hrtime total_time, ink_hrtime request_process_time);
  static void delete_warning_value(HTTPHdr *to_warn, HTTPWarningCode warning_code);
  static bool is_connection_collapse_checks_success(State *s); // YTS Team, yamsat
};

typedef void (*TransactEntryFunc_t)(HttpTransact::State *s);

inline bool
is_response_body_precluded(HTTPStatus status_code, int method)
{
  ////////////////////////////////////////////////////////
  // the spec says about message body the following:    //
  // All responses to the HEAD request method MUST NOT  //
  // include a message-body, even though the presence   //
  // of entity-header fields might lead one to believe  //
  // they do. All 1xx (informational), 204 (no content),//
  // and 304 (not modified) responses MUST NOT include  //
  // a message-body.                                    //
  ////////////////////////////////////////////////////////

  if (((status_code != HTTP_STATUS_OK) &&
       ((status_code == HTTP_STATUS_NOT_MODIFIED) || ((status_code < HTTP_STATUS_OK) && (status_code >= HTTP_STATUS_CONTINUE)) ||
        (status_code == 204))) ||
      (method == HTTP_WKSIDX_HEAD)) {
    return true;
  } else {
    return false;
  }
}

inkcoreapi extern ink_time_t ink_cluster_time(void);

#endif
