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

#include "ts/ink_platform.h"

#include <strings.h>
#include <cmath>

#include "HttpTransact.h"
#include "HttpTransactHeaders.h"
#include "HttpSM.h"
#include "HttpCacheSM.h" //Added to get the scope of HttpCacheSM object - YTS Team, yamsat
#include "HttpDebugNames.h"
#include <ctime>
#include "ts/ParseRules.h"
#include "HTTP.h"
#include "HdrUtils.h"
#include "logging/Log.h"
#include "logging/LogUtils.h"
#include "CacheControl.h"
#include "ControlMatcher.h"
#include "ReverseProxy.h"
#include "HttpBodyFactory.h"
#include "StatPages.h"
#include "../IPAllow.h"
#include "I_Machine.h"

static char range_type[] = "multipart/byteranges; boundary=RANGE_SEPARATOR";
#define RANGE_NUMBERS_LENGTH 60

#define TRANSACT_SETUP_RETURN(n, r) \
  s->next_action           = n;     \
  s->transact_return_point = r;     \
  SpecificDebug((s->state_machine && s->state_machine->debug_on), "http_trans", "Next action %s; %s", #n, #r);

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

#define TxnDebug(tag, fmt, ...) \
  SpecificDebug((s->state_machine->debug_on), tag, "[%" PRId64 "] " fmt, s->state_machine->sm_id, ##__VA_ARGS__)

extern HttpBodyFactory *body_factory;

inline static bool
is_localhost(const char *name, int len)
{
  static const char local[] = "127.0.0.1";
  return (len == (sizeof(local) - 1)) && (memcmp(name, local, len) == 0);
}

inline static void
simple_or_unavailable_server_retry(HttpTransact::State *s)
{
  // server response.
  HTTPStatus server_response = http_hdr_status_get(s->hdr_info.server_response.m_http);

  TxnDebug("http_trans", "[simple_or_unavailabe_server_retry] server_response = %d, simple_retry_attempts: %d, numParents:%d ",
           server_response, s->current.simple_retry_attempts, s->parent_params->numParents(&s->parent_result));

  // simple retry is enabled, 0x1
  if ((s->parent_result.retry_type() & PARENT_RETRY_SIMPLE) &&
      s->current.simple_retry_attempts < s->parent_result.max_retries(PARENT_RETRY_SIMPLE) &&
      server_response == HTTP_STATUS_NOT_FOUND) {
    TxnDebug("http_trans", "RECEIVED A SIMPLE RETRY RESPONSE");
    if (s->current.simple_retry_attempts < s->parent_params->numParents(&s->parent_result)) {
      s->current.state      = HttpTransact::PARENT_RETRY;
      s->current.retry_type = PARENT_RETRY_SIMPLE;
      return;
    } else {
      TxnDebug("http_trans", "PARENT_RETRY_SIMPLE: retried all parents, send response to client.");
      return;
    }
  }
  // unavailable server retry is enabled 0x2
  else if ((s->parent_result.retry_type() & PARENT_RETRY_UNAVAILABLE_SERVER) &&
           s->current.unavailable_server_retry_attempts < s->parent_result.max_retries(PARENT_RETRY_UNAVAILABLE_SERVER) &&
           s->parent_result.response_is_retryable(server_response)) {
    TxnDebug("parent_select", "RECEIVED A PARENT_RETRY_UNAVAILABLE_SERVER RESPONSE");
    if (s->current.unavailable_server_retry_attempts < s->parent_params->numParents(&s->parent_result)) {
      s->current.state      = HttpTransact::PARENT_RETRY;
      s->current.retry_type = PARENT_RETRY_UNAVAILABLE_SERVER;
      return;
    } else {
      TxnDebug("http_trans", "PARENT_RETRY_UNAVAILABLE_SERVER: retried all parents, send error to client.");
      return;
    }
  }
}

inline static bool
is_request_conditional(HTTPHdr *header)
{
  uint64_t mask = (MIME_PRESENCE_IF_UNMODIFIED_SINCE | MIME_PRESENCE_IF_MODIFIED_SINCE | MIME_PRESENCE_IF_RANGE |
                   MIME_PRESENCE_IF_MATCH | MIME_PRESENCE_IF_NONE_MATCH);
  return (header->presence(mask) &&
          (header->method_get_wksidx() == HTTP_WKSIDX_GET || header->method_get_wksidx() == HTTP_WKSIDX_HEAD));
}

static inline bool
is_port_in_range(int port, HttpConfigPortRange *pr)
{
  while (pr) {
    if (pr->low == -1) {
      return true;
    } else if ((pr->low <= port) && (pr->high >= port)) {
      return true;
    }

    pr = pr->next;
  }

  return false;
}

inline static void
update_cache_control_information_from_config(HttpTransact::State *s)
{
  getCacheControl(&s->cache_control, &s->request_data, s->txn_conf);

  s->cache_info.directives.does_config_permit_lookup &= (s->cache_control.never_cache == false);
  s->cache_info.directives.does_config_permit_storing &= (s->cache_control.never_cache == false);

  s->cache_info.directives.does_client_permit_storing =
    HttpTransact::does_client_request_permit_storing(&s->cache_control, &s->hdr_info.client_request);

  s->cache_info.directives.does_client_permit_lookup = HttpTransact::does_client_request_permit_cached_response(
    s->txn_conf, &s->cache_control, &s->hdr_info.client_request, s->via_string);

  s->cache_info.directives.does_client_permit_dns_storing =
    HttpTransact::does_client_request_permit_dns_caching(&s->cache_control, &s->hdr_info.client_request);

  if (s->client_info.http_version == HTTPVersion(0, 9)) {
    s->cache_info.directives.does_client_permit_lookup  = false;
    s->cache_info.directives.does_client_permit_storing = false;
  }

  // Less than 0 means it wasn't overridden, so leave it alone.
  if (s->cache_control.cache_responses_to_cookies >= 0) {
    s->txn_conf->cache_responses_to_cookies = s->cache_control.cache_responses_to_cookies;
  }
}

inline bool
HttpTransact::is_server_negative_cached(State *s)
{
  if (s->host_db_info.app.http_data.last_failure != 0 &&
      s->host_db_info.app.http_data.last_failure + s->txn_conf->down_server_timeout > s->client_request_time) {
    return true;
  } else {
    // Make sure some nasty clock skew has not happened
    //  Use the server timeout to set an upperbound as to how far in the
    //   future we should tolerate bogus last failure times.  This sets
    //   the upper bound to the time that we would ever consider a server
    //   down to 2*down_server_timeout
    if (s->client_request_time + s->txn_conf->down_server_timeout < s->host_db_info.app.http_data.last_failure) {
      s->host_db_info.app.http_data.last_failure = 0;
      ink_assert(!"extreme clock skew");
      return true;
    }
    return false;
  }
}

inline static void
update_current_info(HttpTransact::CurrentInfo *into, HttpTransact::ConnectionAttributes *from, HttpTransact::LookingUp_t who,
                    int attempts)
{
  into->request_to = who;
  into->server     = from;
  into->attempts   = attempts;
}

inline static void
update_dns_info(HttpTransact::DNSLookupInfo *dns, HttpTransact::CurrentInfo *from, int attempts, Arena * /* arena ATS_UNUSED */)
{
  dns->looking_up  = from->request_to;
  dns->lookup_name = from->server->name;
  dns->attempts    = attempts;
}

inline static HTTPHdr *
find_appropriate_cached_resp(HttpTransact::State *s)
{
  HTTPHdr *c_resp = nullptr;

  if (s->cache_info.object_store.valid()) {
    c_resp = s->cache_info.object_store.response_get();
    if (c_resp != nullptr && c_resp->valid()) {
      return c_resp;
    }
  }

  ink_assert(s->cache_info.object_read != nullptr);
  return s->cache_info.object_read->response_get();
}

int response_cacheable_indicated_by_cc(HTTPHdr *response);

inline static bool
is_negative_caching_appropriate(HttpTransact::State *s)
{
  if (!s->txn_conf->negative_caching_enabled || !s->hdr_info.server_response.valid()) {
    return false;
  }

  switch (s->hdr_info.server_response.status_get()) {
  case HTTP_STATUS_NO_CONTENT:
  case HTTP_STATUS_USE_PROXY:
  case HTTP_STATUS_BAD_REQUEST:
  case HTTP_STATUS_FORBIDDEN:
  case HTTP_STATUS_NOT_FOUND:
  case HTTP_STATUS_METHOD_NOT_ALLOWED:
  case HTTP_STATUS_REQUEST_URI_TOO_LONG:
  case HTTP_STATUS_INTERNAL_SERVER_ERROR:
  case HTTP_STATUS_NOT_IMPLEMENTED:
  case HTTP_STATUS_BAD_GATEWAY:
  case HTTP_STATUS_SERVICE_UNAVAILABLE:
  case HTTP_STATUS_GATEWAY_TIMEOUT:
    return true;
  default:
    break;
  }

  return false;
}

inline static HttpTransact::LookingUp_t
find_server_and_update_current_info(HttpTransact::State *s)
{
  int host_len;
  const char *host = s->hdr_info.client_request.host_get(&host_len);

  if (is_localhost(host, host_len)) {
    // Do not forward requests to local_host onto a parent.
    // I just wanted to do this for cop heartbeats, someone else
    // wanted it for all requests to local_host.
    s->parent_result.result = PARENT_DIRECT;
  } else if (s->method == HTTP_WKSIDX_CONNECT && s->http_config_param->disable_ssl_parenting) {
    s->parent_params->findParent(&s->request_data, &s->parent_result, s->txn_conf->parent_fail_threshold,
                                 s->txn_conf->parent_retry_time);
    if (!s->parent_result.is_some() || s->parent_result.is_api_result() || s->parent_result.parent_is_proxy()) {
      TxnDebug("http_trans", "request not cacheable, so bypass parent");
      s->parent_result.result = PARENT_DIRECT;
    }
  } else if (s->txn_conf->uncacheable_requests_bypass_parent && s->http_config_param->no_dns_forward_to_parent == 0 &&
             !HttpTransact::is_request_cache_lookupable(s)) {
    // request not lookupable and cacheable, so bypass parent if the parent is not an origin server.
    // Note that the configuration of the proxy as well as the request
    // itself affects the result of is_request_cache_lookupable();
    // we are assuming both child and parent have similar configuration
    // with respect to whether a request is cacheable or not.
    // For example, the cache_urls_that_look_dynamic variable.
    s->parent_params->findParent(&s->request_data, &s->parent_result, s->txn_conf->parent_fail_threshold,
                                 s->txn_conf->parent_retry_time);
    if (!s->parent_result.is_some() || s->parent_result.is_api_result() || s->parent_result.parent_is_proxy()) {
      TxnDebug("http_trans", "request not cacheable, so bypass parent");
      s->parent_result.result = PARENT_DIRECT;
    }
  } else {
    switch (s->parent_result.result) {
    case PARENT_UNDEFINED:
      s->parent_params->findParent(&s->request_data, &s->parent_result, s->txn_conf->parent_fail_threshold,
                                   s->txn_conf->parent_retry_time);
      break;
    case PARENT_SPECIFIED:
      s->parent_params->nextParent(&s->request_data, &s->parent_result, s->txn_conf->parent_fail_threshold,
                                   s->txn_conf->parent_retry_time);

      // Hack!
      // We already have a parent that failed, if we are now told
      //  to go the origin server, we can only obey this if we
      //  dns'ed the origin server
      if (s->parent_result.result == PARENT_DIRECT && s->http_config_param->no_dns_forward_to_parent != 0) {
        ink_assert(!s->server_info.dst_addr.isValid());
        s->parent_result.result = PARENT_FAIL;
      }
      break;
    case PARENT_FAIL:
      // Check to see if should bypass the parent and go direct
      //   We can only do this if
      //   1) the config permitted us to dns the origin server
      //   2) the config permits us
      //   3) the parent was not set from API
      if (s->http_config_param->no_dns_forward_to_parent == 0 && s->parent_result.bypass_ok() &&
          s->parent_result.parent_is_proxy() && !s->parent_params->apiParentExists(&s->request_data)) {
        s->parent_result.result = PARENT_DIRECT;
      }
      break;
    default:
      ink_assert(0);
    // FALL THROUGH
    case PARENT_DIRECT:
      //              // if we have already decided to go direct
      //              // dont bother calling nextParent.
      //              // do nothing here, guy.
      break;
    }
  }

  switch (s->parent_result.result) {
  case PARENT_SPECIFIED:
    s->parent_info.name = s->arena.str_store(s->parent_result.hostname, strlen(s->parent_result.hostname));
    update_current_info(&s->current, &s->parent_info, HttpTransact::PARENT_PROXY, (s->current.attempts)++);
    update_dns_info(&s->dns_info, &s->current, 0, &s->arena);
    ink_assert(s->dns_info.looking_up == HttpTransact::PARENT_PROXY);
    s->next_hop_scheme = URL_WKSIDX_HTTP;

    return HttpTransact::PARENT_PROXY;
  case PARENT_FAIL:
    // No more parents - need to return an error message
    s->current.request_to = HttpTransact::HOST_NONE;
    return HttpTransact::HOST_NONE;

  case PARENT_DIRECT:
  /* fall through */
  default:
    update_current_info(&s->current, &s->server_info, HttpTransact::ORIGIN_SERVER, (s->current.attempts)++);
    update_dns_info(&s->dns_info, &s->current, 0, &s->arena);
    ink_assert(s->dns_info.looking_up == HttpTransact::ORIGIN_SERVER);
    s->next_hop_scheme = s->scheme;
    return HttpTransact::ORIGIN_SERVER;
  }
}

inline static bool
do_cookies_prevent_caching(int cookies_conf, HTTPHdr *request, HTTPHdr *response, HTTPHdr *cached_request = nullptr)
{
  enum CookiesConfig {
    COOKIES_CACHE_NONE             = 0, // do not cache any responses to cookies
    COOKIES_CACHE_ALL              = 1, // cache for any content-type (ignore cookies)
    COOKIES_CACHE_IMAGES           = 2, // cache only for image types
    COOKIES_CACHE_ALL_BUT_TEXT     = 3, // cache for all but text content-types
    COOKIES_CACHE_ALL_BUT_TEXT_EXT = 4  // cache for all but text content-types except with OS response
                                        // without "Set-Cookie" or with "Cache-Control: public"
  };

  const char *content_type = nullptr;
  int str_len;

#ifdef DEBUG
  ink_assert(request->type_get() == HTTP_TYPE_REQUEST);
  ink_assert(response->type_get() == HTTP_TYPE_RESPONSE);
  if (cached_request) {
    ink_assert(cached_request->type_get() == HTTP_TYPE_REQUEST);
  }
#endif

  // Can cache all regardless of cookie header - just ignore all cookie headers
  if ((CookiesConfig)cookies_conf == COOKIES_CACHE_ALL) {
    return false;
  }

  // It is considered that Set-Cookie headers can be safely ignored
  // for non text content types if Cache-Control private is not set.
  // This enables a bigger hit rate, which currently outweighs the risk of
  // breaking origin servers that truly intend to set a cookie with other
  // objects such as images.
  // At this time, it is believed that only advertisers do this, and that
  // customers won't care about it.

  // If the response does not have a Set-Cookie header and
  // the response does not have a Cookie header and
  // the object is not cached or the request does not have a Cookie header
  // then cookies do not prevent caching.
  if (!response->presence(MIME_PRESENCE_SET_COOKIE) && !request->presence(MIME_PRESENCE_COOKIE) &&
      (cached_request == nullptr || !cached_request->presence(MIME_PRESENCE_COOKIE))) {
    return false;
  }

  // Do not cache if cookies option is COOKIES_CACHE_NONE
  // and a Cookie is detected
  if ((CookiesConfig)cookies_conf == COOKIES_CACHE_NONE) {
    return true;
  }
  // All other options depend on the Content-Type
  content_type = response->value_get(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, &str_len);

  if ((CookiesConfig)cookies_conf == COOKIES_CACHE_IMAGES) {
    if (content_type && str_len >= 5 && memcmp(content_type, "image", 5) == 0) {
      // Images can be cached
      return false;
    }
    return true; // do not cache if  COOKIES_CACHE_IMAGES && content_type != "image"
  }
  // COOKIES_CACHE_ALL_BUT_TEXT || COOKIES_CACHE_ALL_BUT_TEXT_EXT
  // Note: if the configuration is bad, we consider
  // COOKIES_CACHE_ALL_BUT_TEXT to be the default

  if (content_type && str_len >= 4 && memcmp(content_type, "text", 4) == 0) { // content type  - "text"
    // Text objects cannot be cached unless the option is
    // COOKIES_CACHE_ALL_BUT_TEXT_EXT.
    // Furthermore, if there is a Set-Cookie header, then
    // Cache-Control must be set.
    if ((CookiesConfig)cookies_conf == COOKIES_CACHE_ALL_BUT_TEXT_EXT &&
        ((!response->presence(MIME_PRESENCE_SET_COOKIE)) || response->is_cache_control_set(HTTP_VALUE_PUBLIC))) {
      return false;
    }
    return true;
  }
  return false; // Non text objects can be cached
}

inline static bool
does_method_require_cache_copy_deletion(const HttpConfigParams *http_config_param, const int method)
{
  return ((method != HTTP_WKSIDX_GET) &&
          (method == HTTP_WKSIDX_DELETE || method == HTTP_WKSIDX_PURGE || method == HTTP_WKSIDX_PUT ||
           (http_config_param->cache_post_method != 1 && method == HTTP_WKSIDX_POST)));
}

inline static bool
does_method_effect_cache(int method)
{
  return ((method == HTTP_WKSIDX_GET || method == HTTP_WKSIDX_DELETE || method == HTTP_WKSIDX_PURGE || method == HTTP_WKSIDX_PUT ||
           method == HTTP_WKSIDX_POST));
}

inline static HttpTransact::StateMachineAction_t
how_to_open_connection(HttpTransact::State *s)
{
  ink_assert((s->pending_work == nullptr) || (s->current.request_to == HttpTransact::PARENT_PROXY));

  // Originally we returned which type of server to open
  // Now, however, we may want to issue a cache
  // operation first in order to lock the cache
  // entry to prevent multiple origin server requests
  // for the same document.
  // The cache operation that we actually issue, of
  // course, depends on the specified "cache_action".
  // If there is no cache-action to be issued, just
  // connect to the server.
  switch (s->cache_info.action) {
  case HttpTransact::CACHE_PREPARE_TO_DELETE:
  case HttpTransact::CACHE_PREPARE_TO_UPDATE:
  case HttpTransact::CACHE_PREPARE_TO_WRITE:
    s->transact_return_point = HttpTransact::handle_cache_write_lock;
    return HttpTransact::SM_ACTION_CACHE_ISSUE_WRITE;
  default:
    // This covers:
    // CACHE_DO_UNDEFINED, CACHE_DO_NO_ACTION, CACHE_DO_DELETE,
    // CACHE_DO_LOOKUP, CACHE_DO_REPLACE, CACHE_DO_SERVE,
    // CACHE_DO_SERVE_AND_DELETE, CACHE_DO_SERVE_AND_UPDATE,
    // CACHE_DO_UPDATE, CACHE_DO_WRITE, TOTAL_CACHE_ACTION_TYPES
    break;
  }

  s->cdn_saved_next_action = HttpTransact::SM_ACTION_ORIGIN_SERVER_OPEN;

  // Setting up a direct CONNECT tunnel enters OriginServerRawOpen. We always do that if we
  // are not forwarding CONNECT and are not going to a parent proxy.
  if (s->method == HTTP_WKSIDX_CONNECT) {
    if (s->txn_conf->forward_connect_method == 1 || s->parent_result.result == PARENT_SPECIFIED) {
      s->cdn_saved_next_action = HttpTransact::SM_ACTION_ORIGIN_SERVER_OPEN;
    } else {
      s->cdn_saved_next_action = HttpTransact::SM_ACTION_ORIGIN_SERVER_RAW_OPEN;
    }
  }

  if (!s->already_downgraded) { // false unless downgraded previously (possibly due to HTTP 505)
    (&s->hdr_info.server_request)->version_set(HTTPVersion(1, 1));
    HttpTransactHeaders::convert_request(s->current.server->http_version, &s->hdr_info.server_request);
  }

  ink_assert(s->cdn_saved_next_action == HttpTransact::SM_ACTION_ORIGIN_SERVER_OPEN ||
             s->cdn_saved_next_action == HttpTransact::SM_ACTION_ORIGIN_SERVER_RAW_OPEN);
  return s->cdn_saved_next_action;
}

/*****************************************************************************
 *****************************************************************************
 ****                                                                     ****
 ****                 HttpTransact State Machine Handlers                 ****
 ****                                                                     ****
 **** What follow from here on are the state machine handlers - the code  ****
 **** which is called from HttpSM::set_next_state to specify              ****
 **** what action the state machine needs to execute next. These ftns     ****
 **** take as input just the state and set the next_action variable.      ****
 *****************************************************************************
 *****************************************************************************/
void
HttpTransact::BadRequest(State *s)
{
  TxnDebug("http_trans", "[BadRequest]"
                         "parser marked request bad");
  bootstrap_state_variables_from_request(s, &s->hdr_info.client_request);
  build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Invalid HTTP Request", "request#syntax_error");
  TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
}

void
HttpTransact::PostActiveTimeoutResponse(State *s)
{
  TxnDebug("http_trans", "[PostActiveTimeoutResponse]"
                         "post active timeout");
  bootstrap_state_variables_from_request(s, &s->hdr_info.client_request);
  build_error_response(s, HTTP_STATUS_REQUEST_TIMEOUT, "Active Timeout", "timeout#activity");
  TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
}

void
HttpTransact::PostInactiveTimeoutResponse(State *s)
{
  TxnDebug("http_trans", "[PostInactiveTimeoutResponse]"
                         "post inactive timeout");
  bootstrap_state_variables_from_request(s, &s->hdr_info.client_request);
  build_error_response(s, HTTP_STATUS_REQUEST_TIMEOUT, "Inactive Timeout", "timeout#inactivity");
  TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
}

void
HttpTransact::Forbidden(State *s)
{
  TxnDebug("http_trans", "[Forbidden]"
                         "IpAllow marked request forbidden");
  bootstrap_state_variables_from_request(s, &s->hdr_info.client_request);
  build_error_response(s, HTTP_STATUS_FORBIDDEN, "Access Denied", "access#denied");
  TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
}

void
HttpTransact::HandleBlindTunnel(State *s)
{
  URL u;
  // IpEndpoint dest_addr;
  // ip_text_buffer new_host;

  TxnDebug("http_trans", "[HttpTransact::HandleBlindTunnel]");

  // We set the version to 0.9 because once we know where we are going
  //   this blind ssl tunnel is indistinguishable from a "CONNECT 0.9"
  //   except for the need to suppression error messages
  HTTPVersion ver(0, 9);
  s->hdr_info.client_request.version_set(ver);

  // Initialize the state vars necessary to sending error responses
  bootstrap_state_variables_from_request(s, &s->hdr_info.client_request);

  if (is_debug_tag_set("http_trans")) {
    int host_len;
    const char *host = s->hdr_info.client_request.url_get()->host_get(&host_len);
    TxnDebug("http_trans", "[HandleBlindTunnel] destination set to %.*s:%d", host_len, host,
             s->hdr_info.client_request.url_get()->port_get());
  }

  // Set the mode to tunnel so that we don't lookup the cache
  s->current.mode = TUNNELLING_PROXY;

  // Let the request work it's way through the code and
  //  we grab it again after the raw connection has been opened
  HandleRequest(s);
}

void
HttpTransact::StartRemapRequest(State *s)
{
  if (s->api_skip_all_remapping) {
    TxnDebug("http_trans", "API request to skip remapping");

    s->hdr_info.client_request.set_url_target_from_host_field();

    // Since we're not doing remap, we still have to allow for these overridable
    // configurations to modify follow-redirect behavior. Someone could for example
    // have set them in a plugin other than conf_remap running in a prior hook.
    s->state_machine->enable_redirection = (s->txn_conf->number_of_redirections > 0);

    if (s->is_upgrade_request && s->post_remap_upgrade_return_point) {
      TRANSACT_RETURN(SM_ACTION_POST_REMAP_SKIP, s->post_remap_upgrade_return_point);
    }

    TRANSACT_RETURN(SM_ACTION_POST_REMAP_SKIP, HttpTransact::HandleRequest);
  }

  TxnDebug("http_trans", "START HttpTransact::StartRemapRequest");

  //////////////////////////////////////////////////////////////////
  // FIX: this logic seems awfully convoluted and hard to follow; //
  //      seems like we could come up with a more elegant and     //
  //      comprehensible design that generalized things           //
  //////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////
  // run the remap url-rewriting engine:                         //
  //                                                             //
  // * the variable <url_remap_success> is set true if           //
  //   the url was rewritten                                     //
  //                                                             //
  // * the variable <remap_redirect> is set to non-NULL if there //
  //   is a URL provided that the proxy is supposed to redirect  //
  //   requesters of a particular URL to.                        //
  /////////////////////////////////////////////////////////////////

  if (is_debug_tag_set("http_chdr_describe") || is_debug_tag_set("http_trans")) {
    TxnDebug("http_trans", "Before Remapping:");
    obj_describe(s->hdr_info.client_request.m_http, true);
  }

  if (s->http_config_param->referer_filter_enabled) {
    s->filter_mask = URL_REMAP_FILTER_REFERER;
    if (s->http_config_param->referer_format_redirect) {
      s->filter_mask |= URL_REMAP_FILTER_REDIRECT_FMT;
    }
  }

  TxnDebug("http_trans", "END HttpTransact::StartRemapRequest");
  TRANSACT_RETURN(SM_ACTION_API_PRE_REMAP, HttpTransact::PerformRemap);
}

void
HttpTransact::PerformRemap(State *s)
{
  TxnDebug("http_trans", "Inside PerformRemap");
  TRANSACT_RETURN(SM_ACTION_REMAP_REQUEST, HttpTransact::EndRemapRequest);
}

void
HttpTransact::EndRemapRequest(State *s)
{
  TxnDebug("http_trans", "START HttpTransact::EndRemapRequest");

  HTTPHdr *incoming_request = &s->hdr_info.client_request;
  int method                = incoming_request->method_get_wksidx();
  int host_len;
  const char *host = incoming_request->host_get(&host_len);
  TxnDebug("http_trans", "EndRemapRequest host is %.*s", host_len, host);

  // Setting enable_redirection according to HttpConfig (master or overridable). We
  // defer this as late as possible, to allow plugins to modify the overridable
  // configurations (e.g. conf_remap.so). We intentionally only modify this if
  // the configuration says so.
  s->state_machine->enable_redirection = (s->txn_conf->number_of_redirections > 0);

  ////////////////////////////////////////////////////////////////
  // if we got back a URL to redirect to, vector the user there //
  ////////////////////////////////////////////////////////////////
  if (s->remap_redirect != nullptr) {
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    const char *error_body_type;
    switch (s->http_return_code) {
    case HTTP_STATUS_MOVED_PERMANENTLY:
    case HTTP_STATUS_PERMANENT_REDIRECT:
    case HTTP_STATUS_SEE_OTHER:
    case HTTP_STATUS_USE_PROXY:
      error_body_type = "redirect#moved_permanently";
      break;
    case HTTP_STATUS_MOVED_TEMPORARILY:
    case HTTP_STATUS_TEMPORARY_REDIRECT:
      error_body_type = "redirect#moved_temporarily";
      break;
    default:
      if (HTTP_STATUS_NONE == s->http_return_code) {
        s->http_return_code = HTTP_STATUS_MOVED_TEMPORARILY;
        Warning("Changed status code from '0' to '%d'.", s->http_return_code);
      } else {
        Warning("Using invalid status code for redirect '%d'. Building a response for a temporary redirect.", s->http_return_code);
      }
      error_body_type = "redirect#moved_temporarily";
    }
    build_error_response(s, s->http_return_code, "Redirect", error_body_type);
    ats_free(s->remap_redirect);
    s->reverse_proxy = false;
    goto done;
  }
  /////////////////////////////////////////////////////
  // Quick HTTP filtering (primary key: http method) //
  /////////////////////////////////////////////////////
  process_quick_http_filter(s, method);
  /////////////////////////////////////////////////////////////////////////
  // We must close this connection if client_connection_enabled == false //
  /////////////////////////////////////////////////////////////////////////
  if (!s->client_connection_enabled) {
    build_error_response(s, HTTP_STATUS_FORBIDDEN, "Access Denied", "access#denied");
    s->reverse_proxy = false;
    goto done;
  }
  /////////////////////////////////////////////////////////////////
  // Check if remap plugin set HTTP return code and return body  //
  /////////////////////////////////////////////////////////////////
  if (s->http_return_code != HTTP_STATUS_NONE) {
    build_error_response(s, s->http_return_code, nullptr, nullptr);
    s->reverse_proxy = false;
    goto done;
  }

  ///////////////////////////////////////////////////////////////
  // if no mapping was found, handle the cases where:          //
  //                                                           //
  // (1) reverse proxy is on, and no URL host (server request) //
  // (2) no mappings are found, but mappings strictly required //
  ///////////////////////////////////////////////////////////////

  if (!s->url_remap_success) {
    /**
     * It's better to test redirect rules just after url_remap failed
     * Or those successfully remapped rules might be redirected
     **/
    if (handleIfRedirect(s)) {
      TxnDebug("http_trans", "END HttpTransact::RemapRequest");
      TRANSACT_RETURN(SM_ACTION_INTERNAL_CACHE_NOOP, nullptr);
    }

    if (!s->http_config_param->url_remap_required && !incoming_request->is_target_in_url()) {
      s->hdr_info.client_request.set_url_target_from_host_field();
    }

    /////////////////////////////////////////////////////////
    // check for: (1) reverse proxy is on, and no URL host //
    /////////////////////////////////////////////////////////
    if (s->http_config_param->reverse_proxy_enabled && !s->client_info.is_transparent && !incoming_request->is_target_in_url()) {
      /////////////////////////////////////////////////////////
      // the url mapping failed, reverse proxy was enabled,
      // and the request contains no host:
      //
      // * if there is an explanatory redirect, send there.
      // * if there was no host, send "no host" error.
      // * if there was a host, say "not found".
      /////////////////////////////////////////////////////////

      char *redirect_url   = s->http_config_param->reverse_proxy_no_host_redirect;
      int redirect_url_len = s->http_config_param->reverse_proxy_no_host_redirect_len;

      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      if (redirect_url) { /* there is a redirect url */
        build_error_response(s, HTTP_STATUS_MOVED_TEMPORARILY, "Redirect For Explanation", "request#no_host");
        s->hdr_info.client_response.value_set(MIME_FIELD_LOCATION, MIME_LEN_LOCATION, redirect_url, redirect_url_len);
        // socket when there is no host. Need to handle DNS failure elsewhere.
      } else if (host == nullptr) { /* no host */
        build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Host Header Required", "request#no_host");
        s->squid_codes.log_code = SQUID_LOG_ERR_INVALID_URL;
      } else {
        build_error_response(s, HTTP_STATUS_NOT_FOUND, "Not Found on Accelerator", "urlrouting#no_mapping");
        s->squid_codes.log_code = SQUID_LOG_ERR_INVALID_URL;
      }
      s->reverse_proxy = false;
      goto done;
    } else if (s->http_config_param->url_remap_required) {
      ///////////////////////////////////////////////////////
      // the url mapping failed, but mappings are strictly //
      // required so return an error message.              //
      ///////////////////////////////////////////////////////
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      build_error_response(s, HTTP_STATUS_NOT_FOUND, "Not Found", "urlrouting#no_mapping");
      s->squid_codes.log_code = SQUID_LOG_ERR_INVALID_URL;

      s->reverse_proxy = false;
      goto done;
    }
  } else {
    if (s->http_config_param->reverse_proxy_enabled) {
      s->req_flavor = REQ_FLAVOR_REVPROXY;
    }
  }
  s->reverse_proxy              = true;
  s->server_info.is_transparent = s->state_machine->ua_txn ? s->state_machine->ua_txn->is_outbound_transparent() : false;

done:
  // We now set the active-timeout again, since it might have been changed as part of the remap rules.
  if (s->state_machine->ua_txn) {
    s->state_machine->ua_txn->set_active_timeout(HRTIME_SECONDS(s->txn_conf->transaction_active_timeout_in));
  }

  if (is_debug_tag_set("http_chdr_describe") || is_debug_tag_set("http_trans") || is_debug_tag_set("url_rewrite")) {
    TxnDebug("http_trans", "After Remapping:");
    obj_describe(s->hdr_info.client_request.m_http, true);
  }

  /*
    if s->reverse_proxy == false, we can assume remapping failed in some way
      -however-
    If an API setup a tunnel to fake the origin or proxy's response we will
    continue to handle the request (as this was likely the plugin author's intent)

    otherwise, 502/404 the request right now. /eric
  */
  if (!s->reverse_proxy && s->state_machine->plugin_tunnel_type == HTTP_NO_PLUGIN_TUNNEL) {
    TxnDebug("http_trans", "END HttpTransact::EndRemapRequest");
    HTTP_INCREMENT_DYN_STAT(http_invalid_client_requests_stat);
    TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
  } else {
    s->hdr_info.client_response.destroy(); // release the underlying memory.
    s->hdr_info.client_response.clear();   // clear the pointers.
    TxnDebug("http_trans", "END HttpTransact::EndRemapRequest");

    if (s->is_upgrade_request && s->post_remap_upgrade_return_point) {
      TRANSACT_RETURN(SM_ACTION_API_POST_REMAP, s->post_remap_upgrade_return_point);
    }

    TRANSACT_RETURN(SM_ACTION_API_POST_REMAP, HttpTransact::HandleRequest);
  }

  ink_assert(!"not reached");
}

bool
HttpTransact::handle_upgrade_request(State *s)
{
  // Quickest way to determine that this is defintely not an upgrade.
  /* RFC 6455 The method of the request MUST be GET, and the HTTP version MUST
        be at least 1.1. */
  if (!s->hdr_info.client_request.presence(MIME_PRESENCE_UPGRADE) ||
      !s->hdr_info.client_request.presence(MIME_PRESENCE_CONNECTION) || s->method != HTTP_WKSIDX_GET ||
      s->hdr_info.client_request.version_get() < HTTPVersion(1, 1)) {
    return false;
  }

  MIMEField *upgrade_hdr    = s->hdr_info.client_request.field_find(MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE);
  MIMEField *connection_hdr = s->hdr_info.client_request.field_find(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);

  StrList connection_hdr_vals;
  const char *upgrade_hdr_val = nullptr;
  int upgrade_hdr_val_len     = 0;

  if (!upgrade_hdr || !connection_hdr || connection_hdr->value_get_comma_list(&connection_hdr_vals) == 0 ||
      (upgrade_hdr_val = upgrade_hdr->value_get(&upgrade_hdr_val_len)) == nullptr) {
    TxnDebug("http_trans_upgrade", "Transaction wasn't a valid upgrade request, proceeding as a normal HTTP request.");
    return false;
  }

  /*
   * In order for this request to be treated as a normal upgrade request we must have a Connection: Upgrade header
   * and a Upgrade: header, with a non-empty value, otherwise we just assume it's not an Upgrade Request, after
   * we've verified that, we will try to match this upgrade to a known upgrade type such as Websockets.
   */
  bool connection_contains_upgrade = false;
  // Next, let's validate that the Connection header contains an Upgrade key
  for (int i = 0; i < connection_hdr_vals.count; ++i) {
    Str *val = connection_hdr_vals.get_idx(i);
    if (ptr_len_casecmp(val->str, val->len, MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE) == 0) {
      connection_contains_upgrade = true;
      break;
    }
  }

  if (!connection_contains_upgrade) {
    TxnDebug("http_trans_upgrade",
             "Transaction wasn't a valid upgrade request, proceeding as a normal HTTP request, missing Connection upgrade header.");
    return false;
  }

  // Mark this request as an upgrade request.
  s->is_upgrade_request = true;

  /*
     RFC 6455
     The request MUST contain an |Upgrade| header field whose value
        MUST include the "websocket" keyword.
     The request MUST contain a |Connection| header field whose value
        MUST include the "Upgrade" token. // Checked Above
     The request MUST include a header field with the name
        |Sec-WebSocket-Key|.
     The request MUST include a header field with the name
        |Sec-WebSocket-Version|.  The value of this header field MUST be
        13.
   */
  if (hdrtoken_tokenize(upgrade_hdr_val, upgrade_hdr_val_len, &s->upgrade_token_wks) >= 0) {
    if (s->upgrade_token_wks == MIME_VALUE_WEBSOCKET) {
      MIMEField *sec_websocket_key =
        s->hdr_info.client_request.field_find(MIME_FIELD_SEC_WEBSOCKET_KEY, MIME_LEN_SEC_WEBSOCKET_KEY);
      MIMEField *sec_websocket_ver =
        s->hdr_info.client_request.field_find(MIME_FIELD_SEC_WEBSOCKET_VERSION, MIME_LEN_SEC_WEBSOCKET_VERSION);

      if (sec_websocket_key && sec_websocket_ver && sec_websocket_ver->value_get_int() == 13) {
        TxnDebug("http_trans_upgrade", "Transaction wants upgrade to websockets");
        handle_websocket_upgrade_pre_remap(s);
        return true;
      } else {
        TxnDebug("http_trans_upgrade", "Unable to upgrade connection to websockets, invalid headers (RFC 6455).");
      }
    }

    // TODO accept h2c token to start HTTP/2 session after TS-3498 is fixed
  } else {
    TxnDebug("http_trans_upgrade", "Transaction requested upgrade for unknown protocol: %s", upgrade_hdr_val);
  }

  build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Invalid Upgrade Request", "request#syntax_error");

  // we want our modify_request method to just return while we fail out from here.
  // this seems like the preferred option because the user wanted to do an upgrade but sent a bad protocol.
  TRANSACT_RETURN_VAL(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr, true);
}

void
HttpTransact::handle_websocket_upgrade_pre_remap(State *s)
{
  TxnDebug("http_trans_websocket_upgrade_pre_remap", "Prepping transaction before remap.");

  /*
   * We will use this opportunity to set everything up so that during the remap stage we can deal with
   * ws:// and wss:// remap rules, and then we will take over again post remap.
   */
  s->is_websocket                    = true;
  s->post_remap_upgrade_return_point = HttpTransact::handle_websocket_upgrade_post_remap;

  /* let's modify the url scheme to be wss or ws, so remapping will happen as expected */
  URL *url = s->hdr_info.client_request.url_get();
  if (url->scheme_get_wksidx() == URL_WKSIDX_HTTP) {
    TxnDebug("http_trans_websocket_upgrade_pre_remap", "Changing scheme to WS for remapping.");
    url->scheme_set(URL_SCHEME_WS, URL_LEN_WS);
  } else if (url->scheme_get_wksidx() == URL_WKSIDX_HTTPS) {
    TxnDebug("http_trans_websocket_upgrade_pre_remap", "Changing scheme to WSS for remapping.");
    url->scheme_set(URL_SCHEME_WSS, URL_LEN_WSS);
  } else {
    TxnDebug("http_trans_websocket_upgrade_pre_remap", "Invalid scheme for websocket upgrade");
    build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Invalid Upgrade Request", "request#syntax_error");
    TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
  }

  TRANSACT_RETURN(SM_ACTION_API_READ_REQUEST_HDR, HttpTransact::StartRemapRequest);
}

void
HttpTransact::handle_websocket_upgrade_post_remap(State *s)
{
  TxnDebug("http_trans_websocket_upgrade_post_remap", "Remap is complete, start websocket upgrade");

  TRANSACT_RETURN(SM_ACTION_API_POST_REMAP, HttpTransact::handle_websocket_connection);
}

void
HttpTransact::handle_websocket_connection(State *s)
{
  TxnDebug("http_trans_websocket", "START handle_websocket_connection");

  HandleRequest(s);
}

static bool
mimefield_value_equal(MIMEField *field, const char *value, const int value_len)
{
  int field_value_len     = 0;
  const char *field_value = field->value_get(&field_value_len);

  if (field_value != nullptr && field_value_len == value_len) {
    return !strncasecmp(field_value, value, value_len);
  }

  return false;
}

void
HttpTransact::ModifyRequest(State *s)
{
  int scheme, hostname_len;
  HTTPHdr &request              = s->hdr_info.client_request;
  static const int PORT_PADDING = 8;

  TxnDebug("http_trans", "START HttpTransact::ModifyRequest");

  // Initialize the state vars necessary to sending error responses
  bootstrap_state_variables_from_request(s, &request);

  ////////////////////////////////////////////////
  // If there is no scheme default to http      //
  ////////////////////////////////////////////////
  URL *url = request.url_get();

  s->orig_scheme = (scheme = url->scheme_get_wksidx());

  s->method = request.method_get_wksidx();
  if (scheme < 0 && s->method != HTTP_WKSIDX_CONNECT) {
    if (s->client_info.port_attribute == HttpProxyPort::TRANSPORT_SSL) {
      url->scheme_set(URL_SCHEME_HTTPS, URL_LEN_HTTPS);
      s->orig_scheme = URL_WKSIDX_HTTPS;
    } else {
      url->scheme_set(URL_SCHEME_HTTP, URL_LEN_HTTP);
      s->orig_scheme = URL_WKSIDX_HTTP;
    }
  }

  if (s->method == HTTP_WKSIDX_CONNECT && !request.is_port_in_header()) {
    url->port_set(80);
  }

  // Ugly - this must come after the call to url->scheme_set or
  // it can't get the scheme properly and the wrong data is cached.
  // The solution should be to move the scheme detecting logic in to
  // the header class, rather than doing it in a random bit of
  // external code.
  const char *buf = request.host_get(&hostname_len);
  if (!request.is_target_in_url()) {
    s->hdr_info.client_req_is_server_style = true;
  }
  // Copy out buf to a hostname just in case its heap header memory is freed during coalescing
  // due to later HdrHeap operations
  char *hostname = (char *)alloca(hostname_len + PORT_PADDING);
  memcpy(hostname, buf, hostname_len);

  // Make clang analyzer happy. hostname is non-null iff request.is_target_in_url().
  ink_assert(hostname || s->hdr_info.client_req_is_server_style);

  // If the incoming request is proxy-style make sure the Host: header
  // matches the incoming request URL. The exception is if we have
  // Max-Forwards set to 0 in the request
  int max_forwards = -1; // -1 is a valid value meaning that it didn't find the header
  if (request.presence(MIME_PRESENCE_MAX_FORWARDS)) {
    max_forwards = request.get_max_forwards();
  }

  if ((max_forwards != 0) && !s->hdr_info.client_req_is_server_style && s->method != HTTP_WKSIDX_CONNECT) {
    MIMEField *host_field = request.field_find(MIME_FIELD_HOST, MIME_LEN_HOST);
    in_port_t port        = url->port_get_raw();

    // Form the host:port string if not a default port (e.g. 80)
    // We allocated extra space for the port above
    if (port > 0) {
      hostname_len += snprintf(hostname + hostname_len, PORT_PADDING, ":%u", port);
    }

    // No host_field means not equal to host and will need to be set, so create it now.
    if (!host_field) {
      host_field = request.field_create(MIME_FIELD_HOST, MIME_LEN_HOST);
      request.field_attach(host_field);
    }

    if (mimefield_value_equal(host_field, hostname, hostname_len) == false) {
      request.field_value_set(host_field, hostname, hostname_len);
      request.mark_target_dirty();
    }
  }

  TxnDebug("http_trans", "END HttpTransact::ModifyRequest");
  TxnDebug("http_trans", "Checking if transaction wants to upgrade");

  if (handle_upgrade_request(s)) {
    // everything should be handled by the upgrade handler.
    TxnDebug("http_trans", "Transaction will be upgraded by the appropriate upgrade handler.");
    return;
  }

  TRANSACT_RETURN(SM_ACTION_API_READ_REQUEST_HDR, HttpTransact::StartRemapRequest);
}

// This function is supposed to figure out if this transaction is
// susceptible to a redirection as specified by remap.config
bool
HttpTransact::handleIfRedirect(State *s)
{
  int answer;
  URL redirect_url;

  answer = request_url_remap_redirect(&s->hdr_info.client_request, &redirect_url, s->state_machine->m_remap);
  if ((answer == PERMANENT_REDIRECT) || (answer == TEMPORARY_REDIRECT)) {
    int remap_redirect_len;

    s->remap_redirect = redirect_url.string_get(&s->arena, &remap_redirect_len);
    redirect_url.destroy();
    if (answer == TEMPORARY_REDIRECT) {
      if ((s->client_info).http_version.m_version == HTTP_VERSION(1, 1)) {
        build_error_response(s, HTTP_STATUS_TEMPORARY_REDIRECT, "Redirect", "redirect#moved_temporarily");
      } else {
        build_error_response(s, HTTP_STATUS_MOVED_TEMPORARILY, "Redirect", "redirect#moved_temporarily");
      }
    } else {
      build_error_response(s, HTTP_STATUS_MOVED_PERMANENTLY, "Redirect", "redirect#moved_permanently");
    }
    s->arena.str_free(s->remap_redirect);
    s->remap_redirect = nullptr;
    return true;
  }

  return false;
}

void
HttpTransact::HandleRequest(State *s)
{
  TxnDebug("http_trans", "START HttpTransact::HandleRequest");

  if (!s->state_machine->is_waiting_for_full_body) {
    ink_assert(!s->hdr_info.server_request.valid());

    HTTP_INCREMENT_DYN_STAT(http_incoming_requests_stat);

    if (s->client_info.port_attribute == HttpProxyPort::TRANSPORT_SSL) {
      HTTP_INCREMENT_DYN_STAT(https_incoming_requests_stat);
    }

    ///////////////////////////////////////////////
    // if request is bad, return error response  //
    ///////////////////////////////////////////////

    if (!(is_request_valid(s, &s->hdr_info.client_request))) {
      HTTP_INCREMENT_DYN_STAT(http_invalid_client_requests_stat);
      TxnDebug("http_seq", "[HttpTransact::HandleRequest] request invalid.");
      s->next_action = SM_ACTION_SEND_ERROR_CACHE_NOOP;
      //  s->next_action = HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
      return;
    }
    TxnDebug("http_seq", "[HttpTransact::HandleRequest] request valid.");

    if (is_debug_tag_set("http_chdr_describe")) {
      obj_describe(s->hdr_info.client_request.m_http, true);
    }
    // at this point we are guaranteed that the request is good and acceptable.
    // initialize some state variables from the request (client version,
    // client keep-alive, cache action, etc.
    initialize_state_variables_from_request(s, &s->hdr_info.client_request);
    // The following chunk of code will limit the maximum number of websocket connections (TS-3659)
    if (s->is_upgrade_request && s->is_websocket && s->http_config_param->max_websocket_connections >= 0) {
      int64_t val = 0;
      HTTP_READ_DYN_SUM(http_websocket_current_active_client_connections_stat, val);
      if (val >= s->http_config_param->max_websocket_connections) {
        s->is_websocket = false; // unset to avoid screwing up stats.
        TxnDebug("http_trans", "Rejecting websocket connection because the limit has been exceeded");
        bootstrap_state_variables_from_request(s, &s->hdr_info.client_request);
        build_error_response(s, HTTP_STATUS_SERVICE_UNAVAILABLE, "WebSocket Connection Limit Exceeded", nullptr);
        TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
      }
    }

    // The following code is configurable to allow a user to control the max post size (TS-3631)
    if (s->http_config_param->max_post_size > 0 && s->hdr_info.request_content_length > 0 &&
        s->hdr_info.request_content_length > s->http_config_param->max_post_size) {
      TxnDebug("http_trans", "Max post size %" PRId64 " Client tried to post a body that was too large.",
               s->http_config_param->max_post_size);
      HTTP_INCREMENT_DYN_STAT(http_post_body_too_large);
      bootstrap_state_variables_from_request(s, &s->hdr_info.client_request);
      build_error_response(s, HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE, "Request Entity Too Large", "request#entity_too_large");
      s->squid_codes.log_code = SQUID_LOG_ERR_POST_ENTITY_TOO_LARGE;
      TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
    }

    // The following chunk of code allows you to disallow post w/ expect 100-continue (TS-3459)
    if (s->hdr_info.request_content_length && s->http_config_param->disallow_post_100_continue) {
      MIMEField *expect = s->hdr_info.client_request.field_find(MIME_FIELD_EXPECT, MIME_LEN_EXPECT);

      if (expect != nullptr) {
        const char *expect_hdr_val = nullptr;
        int expect_hdr_val_len     = 0;
        expect_hdr_val             = expect->value_get(&expect_hdr_val_len);
        if (ptr_len_casecmp(expect_hdr_val, expect_hdr_val_len, HTTP_VALUE_100_CONTINUE, HTTP_LEN_100_CONTINUE) == 0) {
          // Let's error out this request.
          TxnDebug("http_trans", "Client sent a post expect: 100-continue, sending 405.");
          HTTP_INCREMENT_DYN_STAT(disallowed_post_100_continue);
          build_error_response(s, HTTP_STATUS_METHOD_NOT_ALLOWED, "Method Not Allowed", "request#method_unsupported");
          TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
        }
      }
    }
    if (s->txn_conf->request_buffer_enabled &&
        (s->hdr_info.request_content_length > 0 || s->client_info.transfer_encoding == CHUNKED_ENCODING)) {
      TRANSACT_RETURN(SM_ACTION_WAIT_FOR_FULL_BODY, nullptr);
    }
  }

  // Cache lookup or not will be decided later at DecideCacheLookup().
  // Before it's decided to do a cache lookup,
  // assume no cache lookup and using proxy (not tunneling)
  s->cache_info.action = CACHE_DO_NO_ACTION;
  s->current.mode      = GENERIC_PROXY;

  // initialize the cache_control structure read from cache.config
  update_cache_control_information_from_config(s);

  // We still need to decide whether or not to do a cache lookup since
  // the scheduled update code depends on this info.
  if (is_request_cache_lookupable(s)) {
    s->cache_info.action = CACHE_DO_LOOKUP;
  }

  // If the hostname is "$internal$" then this is a request for
  // internal proxy information.
  if (handle_internal_request(s, &s->hdr_info.client_request)) {
    TRANSACT_RETURN(SM_ACTION_INTERNAL_REQUEST, nullptr);
  }

  // this needs to be called after initializing state variables from request
  // it adds the client-ip to the incoming client request.

  DUMP_HEADER("http_hdrs", &s->hdr_info.client_request, s->state_machine_id, "Incoming Request");

  if (s->state_machine->plugin_tunnel_type == HTTP_PLUGIN_AS_INTERCEPT) {
    setup_plugin_request_intercept(s);
    return;
  }

  // if ip in url or cop test page, not do srv lookup.
  if (s->txn_conf->srv_enabled) {
    IpEndpoint addr;
    ats_ip_pton(s->server_info.name, &addr);
    s->txn_conf->srv_enabled = !ats_is_ip(&addr);
  }

  // if the request is a trace or options request, decrement the
  // max-forwards value. if the incoming max-forwards value was 0,
  // then we have to return a response to the client with the
  // appropriate action for trace/option. in this case this routine
  // is responsible for building the response.
  if (handle_trace_and_options_requests(s, &s->hdr_info.client_request)) {
    TRANSACT_RETURN(SM_ACTION_INTERNAL_CACHE_NOOP, nullptr);
  }

  if (s->http_config_param->no_dns_forward_to_parent && s->scheme != URL_WKSIDX_HTTPS &&
      strcmp(s->server_info.name, "127.0.0.1") != 0) {
    // for HTTPS requests, we must go directly to the
    // origin server. Ignore the no_dns_just_forward_to_parent setting.
    // we need to see if the hostname is an
    //   ip address since the parent selection code result
    //   could change as a result of this ip address
    IpEndpoint addr;
    ats_ip_pton(s->server_info.name, &addr);
    if (ats_is_ip(&addr)) {
      ats_ip_copy(&s->request_data.dest_ip, &addr);
    }

    if (s->parent_params->parentExists(&s->request_data)) {
      // If the proxy is behind and firewall and there is no
      //  DNS service available, we just want to forward the request
      //  the parent proxy.  In this case, we never find out the
      //  origin server's ip.  So just skip past OSDNS
      ats_ip_invalidate(&s->server_info.dst_addr);
      StartAccessControl(s);
      return;
    } else if (s->http_config_param->no_origin_server_dns) {
      build_error_response(s, HTTP_STATUS_BAD_GATEWAY, "Next Hop Connection Failed", "connect#failed_connect");

      TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
    }
  }

  // Added to skip the dns if the document is in the cache.
  // DNS is requested before cache lookup only if there are rules in cache.config , parent.config or
  // if the newly added varible doc_in_cache_skip_dns is not enabled
  if (s->dns_info.lookup_name[0] <= '9' && s->dns_info.lookup_name[0] >= '0' &&
      (!s->state_machine->enable_redirection || !s->redirect_info.redirect_in_process) &&
      s->parent_params->parent_table->hostMatch) {
    s->force_dns = true;
  }
  /* A redirect means we need to check some things again.
     If the cache is enabled then we need to check the new (redirected) request against the cache.
     If not, then we need to at least do DNS again to guarantee we are using the correct IP address
     (if the host changed). Note DNS comes after cache lookup so in both cases we do the DNS.
  */
  if (s->redirect_info.redirect_in_process && s->state_machine->enable_redirection) {
    if (s->txn_conf->cache_http) {
      TRANSACT_RETURN(SM_ACTION_CACHE_LOOKUP, nullptr);
    } else {
      TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, OSDNSLookup); // effectively s->force_dns
    }
  }

  if (s->force_dns) {
    TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, OSDNSLookup); // After handling the request, DNS is done.
  } else {
    // After the requested is properly handled No need of requesting the DNS directly check the ACLs
    // if the request is authorized
    StartAccessControl(s);
  }
}

void
HttpTransact::HandleRequestBufferDone(State *s)
{
  TRANSACT_RETURN(SM_ACTION_REQUEST_BUFFER_READ_COMPLETE, HttpTransact::HandleRequest);
}

void
HttpTransact::setup_plugin_request_intercept(State *s)
{
  ink_assert(s->state_machine->plugin_tunnel != nullptr);

  // Plugin is intercepting the request which means
  //  that we don't do dns, cache read or cache write
  //
  // We just want to write the request straight to the plugin
  if (s->cache_info.action != HttpTransact::CACHE_DO_NO_ACTION) {
    s->cache_info.action = HttpTransact::CACHE_DO_NO_ACTION;
    s->current.mode      = TUNNELLING_PROXY;
    HTTP_INCREMENT_DYN_STAT(http_tunnels_stat);
  }
  // Regardless of the protocol we're gatewaying to
  //   we see the scheme as http
  s->scheme = s->next_hop_scheme = URL_WKSIDX_HTTP;

  // Set up a "fake" server server entry
  update_current_info(&s->current, &s->server_info, HttpTransact::ORIGIN_SERVER, 0);

  // Also "fake" the info we'd normally get from
  //   hostDB
  s->server_info.http_version.set(1, 0);
  s->server_info.keep_alive                  = HTTP_NO_KEEPALIVE;
  s->host_db_info.app.http_data.http_version = HostDBApplicationInfo::HTTP_VERSION_10;
  s->host_db_info.app.http_data.pipeline_max = 1;
  s->server_info.dst_addr.setToAnyAddr(AF_INET);                                 // must set an address or we can't set the port.
  s->server_info.dst_addr.port() = htons(s->hdr_info.client_request.port_get()); // this is the info that matters.

  // Build the request to the server
  build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->client_info.http_version);

  // We don't do keep alive over these impersonated
  //  NetVCs so nuke the connection header
  s->hdr_info.server_request.field_delete(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);

  TRANSACT_RETURN(SM_ACTION_ORIGIN_SERVER_OPEN, nullptr);
}

////////////////////////////////////////////////////////////////////////
// void HttpTransact::HandleApiErrorJump(State* s)
//
//   Called after an API function indicates it wished to send an
//     error to the user agent
////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleApiErrorJump(State *s)
{
  TxnDebug("http_trans", "[HttpTransact::HandleApiErrorJump]");

  // since the READ_REQUEST_HDR_HOOK is processed before
  //   we examine the request, returning TS_EVENT_ERROR will cause
  //   the protocol in the via string to be "?"  Set it here
  //   since we know it has to be http
  // For CONNECT method, next_hop_scheme is NULL
  if (s->next_hop_scheme < 0) {
    s->next_hop_scheme = URL_WKSIDX_HTTP;
  }
  // The client response may not be empty in the
  // case the txn was reenabled in error by a plugin from hook SEND_RESPONSE_HDR.
  // build_response doesn't clean the header. So clean it up before.
  // Do fields_clear() instead of clear() to prevent memory leak
  if (s->hdr_info.client_response.valid()) {
    s->hdr_info.client_response.fields_clear();
  }

  // Set the source to internal so chunking is handled correctly
  s->source = SOURCE_INTERNAL;

  /**
    The API indicated an error. Lets use a >=400 error from the state (if one's set) or fallback to a
    generic HTTP/1.X 500 INKApi Error
  **/
  if (s->http_return_code && s->http_return_code >= HTTP_STATUS_BAD_REQUEST) {
    const char *reason = http_hdr_reason_lookup(s->http_return_code);
    ;
    build_response(s, &s->hdr_info.client_response, s->client_info.http_version, s->http_return_code, reason ? reason : "Error");
  } else {
    build_response(s, &s->hdr_info.client_response, s->client_info.http_version, HTTP_STATUS_INTERNAL_SERVER_ERROR, "INKApi Error");
  }

  TRANSACT_RETURN(SM_ACTION_INTERNAL_CACHE_NOOP, nullptr);
  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : PPDNSLookup
// Description: called after DNS lookup of parent proxy name
//
// Details    :
//
// the configuration information gave us the name of the parent proxy
// to send the request to. this function is called after the dns lookup
// for that name. it may fail, in which case we look for the next parent
// proxy to try and if none exist, then go to the origin server.
// if the lookup succeeds, we open a connection to the parent proxy.
//
//
// Possible Next States From Here:

// - HttpTransact::SM_ACTION_DNS_LOOKUP;
// - HttpTransact::ORIGIN_SERVER_RAW_OPEN;
// - HttpTransact::ORIGIN_SERVER_OPEN;
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::PPDNSLookup(State *s)
{
  ++s->dns_info.attempts;

  TxnDebug("http_trans", "[HttpTransact::PPDNSLookup] This was attempt %d", s->dns_info.attempts);

  ink_assert(s->dns_info.looking_up == PARENT_PROXY);
  if (!s->dns_info.lookup_success) {
    // Mark parent as down due to resolving failure
    HTTP_INCREMENT_DYN_STAT(http_total_parent_marked_down_count);
    s->parent_params->markParentDown(&s->parent_result, s->txn_conf->parent_fail_threshold, s->txn_conf->parent_retry_time);
    // DNS lookup of parent failed, find next parent or o.s.
    if (find_server_and_update_current_info(s) == HttpTransact::HOST_NONE) {
      ink_assert(s->current.request_to == HOST_NONE);
      handle_parent_died(s);
      return;
    }

    if (!s->current.server->dst_addr.isValid()) {
      if (s->current.request_to == PARENT_PROXY) {
        TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, PPDNSLookup);
      } else if (s->parent_result.result == PARENT_DIRECT && s->http_config_param->no_dns_forward_to_parent != 1) {
        // We ran out of parents but parent configuration allows us to go to Origin Server directly
        TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, OSDNSLookup);
      } else {
        // We could be out of parents here if all the parents failed DNS lookup
        ink_assert(s->current.request_to == HOST_NONE);
        handle_parent_died(s);
      }
      return;
    }
  } else {
    // lookup succeeded, open connection to p.p.
    ats_ip_copy(&s->parent_info.dst_addr, s->host_db_info.ip());
    s->parent_info.dst_addr.port() = htons(s->parent_result.port);
    get_ka_info_from_host_db(s, &s->parent_info, &s->client_info, &s->host_db_info);

    char addrbuf[INET6_ADDRSTRLEN];
    TxnDebug("http_trans", "[PPDNSLookup] DNS lookup for sm_id[%" PRId64 "] successful IP: %s", s->state_machine->sm_id,
             ats_ip_ntop(&s->parent_info.dst_addr.sa, addrbuf, sizeof(addrbuf)));
  }

  // Since this function can be called several times while retrying
  //  parents, check to see if we've already built our request
  if (!s->hdr_info.server_request.valid()) {
    build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->current.server->http_version);

    // Take care of deferred (issue revalidate) work in building
    //   the request
    if (s->pending_work != nullptr) {
      ink_assert(s->pending_work == issue_revalidate);
      (*s->pending_work)(s);
      s->pending_work = nullptr;
    }
  }
  // what kind of a connection (raw, simple)
  s->next_action = how_to_open_connection(s);
}

///////////////////////////////////////////////////////////////////////////////
//
// Name       : ReDNSRoundRobin
// Description: Called after we fail to contact part of a round-robin
//              robin server set and we found a another ip address.
//
// Details    :
//
//
//
// Possible Next States From Here:
// - HttpTransact::ORIGIN_SERVER_RAW_OPEN;
// - HttpTransact::ORIGIN_SERVER_OPEN;
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::ReDNSRoundRobin(State *s)
{
  ink_assert(s->current.server == &s->server_info);
  ink_assert(s->current.server->had_connect_fail());

  if (s->dns_info.lookup_success) {
    // We using a new server now so clear the connection
    //  failure mark
    s->current.server->clear_connect_fail();

    // Our ReDNS of the server succeeded so update the necessary
    //  information and try again. Need to preserve the current port value if possible.
    in_port_t server_port = s->current.server->dst_addr.host_order_port();
    // Temporary check to make sure the port preservation can be depended upon. That should be the case
    // because we get here only after trying a connection. Remove for 6.2.
    ink_assert(s->current.server->dst_addr.isValid() && 0 != server_port);

    ats_ip_copy(&s->server_info.dst_addr, s->host_db_info.ip());
    s->server_info.dst_addr.port() = htons(server_port);
    ats_ip_copy(&s->request_data.dest_ip, &s->server_info.dst_addr);
    get_ka_info_from_host_db(s, &s->server_info, &s->client_info, &s->host_db_info);

    char addrbuf[INET6_ADDRSTRLEN];
    TxnDebug("http_trans", "[ReDNSRoundRobin] DNS lookup for O.S. successful IP: %s",
             ats_ip_ntop(&s->server_info.dst_addr.sa, addrbuf, sizeof(addrbuf)));

    s->next_action = how_to_open_connection(s);
  } else {
    // Our ReDNS failed so output the DNS failure error message
    build_error_response(s, HTTP_STATUS_BAD_GATEWAY, "Cannot find server.", "connect#dns_failed");
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->next_action       = SM_ACTION_SEND_ERROR_CACHE_NOOP;
    //  s->next_action = PROXY_INTERNAL_CACHE_NOOP;
  }

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : OSDNSLookup
// Description: called after the DNS lookup of origin server name
//
// Details    :
//
// normally called after Start. may be called more than once, however,
// if the dns lookup fails. this may be if the client does not specify
// the full hostname (e.g. just cnn, instead of www.cnn.com), or because
// it was not possible to resolve the name after several attempts.
//
// the next action depends. since this function is normally called after
// a request has come in, which is valid and does not require an immediate
// response, the next action may just be to open a connection to the
// origin server, or a parent proxy, or the next action may be to do a
// cache lookup, or in the event of an error, the next action may be to
// send a response back to the client.
//
//
// Possible Next States From Here:
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
// - HttpTransact::CACHE_LOOKUP;
// - HttpTransact::SM_ACTION_DNS_LOOKUP;
// - HttpTransact::ORIGIN_SERVER_RAW_OPEN;
// - HttpTransact::ORIGIN_SERVER_OPEN;
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::OSDNSLookup(State *s)
{
  static const int max_dns_lookups = 3;

  ink_assert(s->dns_info.looking_up == ORIGIN_SERVER);

  TxnDebug("http_trans", "[HttpTransact::OSDNSLookup] This was attempt %d", s->dns_info.attempts);
  ++s->dns_info.attempts;

  // It's never valid to connect *to* INADDR_ANY, so let's reject the request now.
  if (ats_is_ip_any(s->host_db_info.ip())) {
    TxnDebug("http_trans", "[OSDNSLookup] Invalid request IP: INADDR_ANY");
    build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Bad Destination Address", "request#syntax_error");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
  }

  // detect whether we are about to self loop. the client may have
  // specified the proxy as the origin server (badness).
  // Check if this procedure is already done - YTS Team, yamsat
  if (!s->request_will_not_selfloop) {
    if (will_this_request_self_loop(s)) {
      TxnDebug("http_trans", "[OSDNSLookup] request will selfloop - bailing out");
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
    }
  }

  if (!s->dns_info.lookup_success) {
    // maybe the name can be expanded (e.g cnn -> www.cnn.com)
    HostNameExpansionError_t host_name_expansion = try_to_expand_host_name(s);

    switch (host_name_expansion) {
    case RETRY_EXPANDED_NAME:
      // expansion successful, do a dns lookup on expanded name
      HTTP_RELEASE_ASSERT(s->dns_info.attempts < max_dns_lookups);
      TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, OSDNSLookup);
      break;
    case EXPANSION_NOT_ALLOWED:
    case EXPANSION_FAILED:
    case DNS_ATTEMPTS_EXHAUSTED:
      if (DNSLookupInfo::OS_Addr::OS_ADDR_TRY_HOSTDB == s->dns_info.os_addr_style) {
        /*
         *  We tried to connect to client target address, failed and tried to use a different addr
         *  No HostDB data, just keep on with the CTA.
         */
        s->dns_info.lookup_success = true;
        s->dns_info.os_addr_style  = DNSLookupInfo::OS_Addr::OS_ADDR_USE_CLIENT;
        TxnDebug("http_seq", "[HttpTransact::OSDNSLookup] DNS lookup unsuccessful, using client target address");
      } else {
        if (host_name_expansion == EXPANSION_NOT_ALLOWED) {
          // config file doesn't allow automatic expansion of host names
          TxnDebug("http_seq", "[HttpTransact::OSDNSLookup] DNS Lookup unsuccessful");
        } else if (host_name_expansion == EXPANSION_FAILED) {
          // not able to expand the hostname. dns lookup failed
          TxnDebug("http_seq", "[HttpTransact::OSDNSLookup] DNS Lookup unsuccessful");
        } else if (host_name_expansion == DNS_ATTEMPTS_EXHAUSTED) {
          // retry attempts exhausted --- can't find dns entry for this host name
          HTTP_RELEASE_ASSERT(s->dns_info.attempts >= max_dns_lookups);
          TxnDebug("http_seq", "[HttpTransact::OSDNSLookup] DNS Lookup unsuccessful");
        }
        // output the DNS failure error message
        SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
        build_error_response(s, HTTP_STATUS_BAD_GATEWAY, "Cannot find server.", "connect#dns_failed");
        // s->cache_info.action = CACHE_DO_NO_ACTION;
        TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
      }
      break;
    default:
      ink_assert(!("try_to_expand_hostname returned an unsupported code"));
      break;
    }
    return;
  }
  // ok, so the dns lookup succeeded
  ink_assert(s->dns_info.lookup_success);
  TxnDebug("http_seq", "[HttpTransact::OSDNSLookup] DNS Lookup successful");

  if (DNSLookupInfo::OS_Addr::OS_ADDR_TRY_HOSTDB == s->dns_info.os_addr_style) {
    // We've backed off from a client supplied address and found some
    // HostDB addresses. We use those if they're different from the CTA.
    // In all cases we now commit to client or HostDB for our source.
    if (s->host_db_info.round_robin) {
      HostDBInfo *cta = s->host_db_info.rr()->select_next(&s->current.server->dst_addr.sa);
      if (cta) {
        // found another addr, lock in host DB.
        s->host_db_info           = *cta;
        s->dns_info.os_addr_style = DNSLookupInfo::OS_Addr::OS_ADDR_USE_HOSTDB;
      } else {
        // nothing else there, continue with CTA.
        s->dns_info.os_addr_style = DNSLookupInfo::OS_Addr::OS_ADDR_USE_CLIENT;
      }
    } else if (ats_ip_addr_eq(s->host_db_info.ip(), &s->server_info.dst_addr.sa)) {
      s->dns_info.os_addr_style = DNSLookupInfo::OS_Addr::OS_ADDR_USE_CLIENT;
    } else {
      s->dns_info.os_addr_style = DNSLookupInfo::OS_Addr::OS_ADDR_USE_HOSTDB;
    }
  }

  // Check to see if can fullfill expect requests based on the cached
  // update some state variables with hostdb information that has
  // been provided.
  ats_ip_copy(&s->server_info.dst_addr, s->host_db_info.ip());
  // If the SRV response has a port number, we should honor it. Otherwise we do the port defined in remap
  if (s->dns_info.srv_lookup_success) {
    s->server_info.dst_addr.port() = htons(s->dns_info.srv_port);
  } else if (!s->api_server_addr_set) {
    s->server_info.dst_addr.port() = htons(s->hdr_info.client_request.port_get()); // now we can set the port.
  }
  ats_ip_copy(&s->request_data.dest_ip, &s->server_info.dst_addr);
  get_ka_info_from_host_db(s, &s->server_info, &s->client_info, &s->host_db_info);

  char addrbuf[INET6_ADDRSTRLEN];
  TxnDebug("http_trans",
           "[OSDNSLookup] DNS lookup for O.S. successful "
           "IP: %s",
           ats_ip_ntop(&s->server_info.dst_addr.sa, addrbuf, sizeof(addrbuf)));

  // so the dns lookup was a success, but the lookup succeeded on
  // a hostname which was expanded by the traffic server. we should
  // not automatically forward the request to this expanded hostname.
  // return a response to the client with the expanded host name
  // and a tasty little blurb explaining what happened.

  // if a DNS lookup succeeded on a user-defined
  // hostname expansion, forward the request to the expanded hostname.
  // On the other hand, if the lookup succeeded on a www.<hostname>.com
  // expansion, return a 302 response.
  // [amc] Also don't redirect if we backed off using HostDB instead of CTA.
  if (s->dns_info.attempts == max_dns_lookups && s->dns_info.looking_up == ORIGIN_SERVER &&
      DNSLookupInfo::OS_Addr::OS_ADDR_USE_CLIENT != s->dns_info.os_addr_style) {
    TxnDebug("http_trans", "[OSDNSLookup] DNS name resolution on expansion");
    TxnDebug("http_seq", "[OSDNSLookup] DNS name resolution on expansion - returning");
    build_redirect_response(s);
    // s->cache_info.action = CACHE_DO_NO_ACTION;
    TRANSACT_RETURN(SM_ACTION_INTERNAL_CACHE_NOOP, nullptr);
  }
  // everything succeeded with the DNS lookup so do an API callout
  //   that allows for filtering.  We'll do traffic_server internal
  //   filtering after API filtering

  // After SM_ACTION_DNS_LOOKUP, goto the saved action/state ORIGIN_SERVER_(RAW_)OPEN.
  // Should we skip the StartAccessControl()? why?

  if (s->cdn_remap_complete) {
    TxnDebug("cdn", "This is a late DNS lookup.  We are going to the OS, "
                    "not to HandleFiltering.");
    ink_assert(s->cdn_saved_next_action == SM_ACTION_ORIGIN_SERVER_OPEN ||
               s->cdn_saved_next_action == SM_ACTION_ORIGIN_SERVER_RAW_OPEN);
    TxnDebug("cdn", "outgoing version -- (pre  conversion) %d", s->hdr_info.server_request.m_http->m_version);
    (&s->hdr_info.server_request)->version_set(HTTPVersion(1, 1));
    HttpTransactHeaders::convert_request(s->current.server->http_version, &s->hdr_info.server_request);
    TxnDebug("cdn", "outgoing version -- (post conversion) %d", s->hdr_info.server_request.m_http->m_version);
    TRANSACT_RETURN(s->cdn_saved_next_action, nullptr);
  } else if (DNSLookupInfo::OS_Addr::OS_ADDR_USE_CLIENT == s->dns_info.os_addr_style ||
             DNSLookupInfo::OS_Addr::OS_ADDR_USE_HOSTDB == s->dns_info.os_addr_style) {
    // we've come back after already trying the server to get a better address
    // and finished with all backtracking - return to trying the server.
    TRANSACT_RETURN(how_to_open_connection(s), HttpTransact::HandleResponse);
  } else if (s->dns_info.lookup_name[0] <= '9' && s->dns_info.lookup_name[0] >= '0' && s->parent_params->parent_table->hostMatch &&
             !s->http_config_param->no_dns_forward_to_parent) {
    // note, broken logic: ACC fudges the OR stmt to always be true,
    // 'AuthHttpAdapter' should do the rev-dns if needed, not here .
    TRANSACT_RETURN(SM_ACTION_DNS_REVERSE_LOOKUP, HttpTransact::StartAccessControl);
  } else {
    //(s->state_machine->authAdapter).StartLookup (s);
    // TRANSACT_RETURN(SM_ACTION_AUTH_LOOKUP, NULL);

    if (s->force_dns) {
      StartAccessControl(s); // If skip_dns is enabled and no ip based rules in cache.config and parent.config
      // Access Control is called after DNS response
    } else {
      if ((s->cache_info.action == CACHE_DO_NO_ACTION) &&
          (((s->hdr_info.client_request.presence(MIME_PRESENCE_RANGE) && !s->txn_conf->cache_range_write) ||
            s->range_setup == RANGE_NOT_SATISFIABLE || s->range_setup == RANGE_NOT_HANDLED))) {
        TRANSACT_RETURN(SM_ACTION_API_OS_DNS, HandleCacheOpenReadMiss);
      } else if (!s->txn_conf->cache_http || s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_SKIPPED) {
        TRANSACT_RETURN(SM_ACTION_API_OS_DNS, LookupSkipOpenServer);
        // DNS Lookup is done after LOOKUP Skipped  and after we get response
        // from the DNS we need to call LookupSkipOpenServer
      } else if (s->cache_lookup_result == CACHE_LOOKUP_HIT_FRESH || s->cache_lookup_result == CACHE_LOOKUP_HIT_WARNING ||
                 s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE) {
        // DNS lookup is done if the content is state need to call handle cache open read hit
        TRANSACT_RETURN(SM_ACTION_API_OS_DNS, HandleCacheOpenReadHit);
      } else if (s->cache_lookup_result == CACHE_LOOKUP_MISS || s->cache_info.action == CACHE_DO_NO_ACTION) {
        TRANSACT_RETURN(SM_ACTION_API_OS_DNS, HandleCacheOpenReadMiss);
        // DNS lookup is done if the lookup failed and need to call Handle Cache Open Read Miss
      } else {
        build_error_response(s, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Invalid Cache Lookup result", "default");
        Log::error("HTTP: Invalid CACHE LOOKUP RESULT : %d", s->cache_lookup_result);
        TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
      }
    }
  }
}

void
HttpTransact::StartAccessControl(State *s)
{
  // if (s->cop_test_page  || (s->state_machine->authAdapter.disabled() == true)) {
  // Heartbeats should always be allowed.
  // s->content_control.access = ACCESS_ALLOW;
  HandleRequestAuthorized(s);
  //  return;
  // }
  // ua_txn is NULL for scheduled updates.
  // Don't use req_flavor to do the test because if updated
  // urls are remapped, the req_flavor is changed to REV_PROXY.
  // if (s->state_machine->ua_txn == NULL) {
  // Scheduled updates should always be allowed
  // return;
  //}
  // pass the access control logic to the ACC module.
  //(s->state_machine->authAdapter).StartLookup(s);
}

void
HttpTransact::HandleRequestAuthorized(State *s)
{
  //(s->state_machine->authAdapter).SetState(s);
  //(s->state_machine->authAdapter).UserAuthorized(NULL);
  // TRANSACT_RETURN(HTTP_API_OS_DNS, HandleFiltering);
  if (s->force_dns) {
    TRANSACT_RETURN(SM_ACTION_API_OS_DNS, HttpTransact::DecideCacheLookup);
  } else {
    HttpTransact::DecideCacheLookup(s);
  }
}

void
HttpTransact::HandleFiltering(State *s)
{
  ink_release_assert(!"Fix-Me AUTH MERGE");

  if (s->method == HTTP_WKSIDX_PUSH && s->http_config_param->push_method_enabled == 0) {
    // config file says this request is not authorized.
    // send back error response to client.
    TxnDebug("http_trans", "[HandleFiltering] access denied.");
    TxnDebug("http_seq", "[HttpTransact::HandleFiltering] Access Denied.");

    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    // adding a comment so that cvs recognizes that I added a space in the text below
    build_error_response(s, HTTP_STATUS_FORBIDDEN, "Access Denied", "access#denied");
    // s->cache_info.action = CACHE_DO_NO_ACTION;
    TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
  }

  TxnDebug("http_seq", "[HttpTransact::HandleFiltering] Request Authorized.");
  //////////////////////////////////////////////////////////////
  // ok, the config file says that the request is authorized. //
  //////////////////////////////////////////////////////////////

  // request is not black listed so now decided if we ought to
  //  lookup the cache
  DecideCacheLookup(s);
}

void
HttpTransact::DecideCacheLookup(State *s)
{
  // Check if a client request is lookupable.
  if (s->redirect_info.redirect_in_process) {
    // for redirect, we want to skip cache lookup and write into
    // the cache directly with the URL before the redirect
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->current.mode      = GENERIC_PROXY;
  } else {
    if (is_request_cache_lookupable(s) && !s->is_upgrade_request) {
      s->cache_info.action = CACHE_DO_LOOKUP;
      s->current.mode      = GENERIC_PROXY;
    } else {
      s->cache_info.action = CACHE_DO_NO_ACTION;
      s->current.mode      = TUNNELLING_PROXY;
      HTTP_INCREMENT_DYN_STAT(http_tunnels_stat);
    }
  }

  if (service_transaction_in_proxy_only_mode(s)) {
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->current.mode      = TUNNELLING_PROXY;
    HTTP_INCREMENT_DYN_STAT(http_throttled_proxy_only_stat);
  }
  // at this point the request is ready to continue down the
  // traffic server path.

  // now decide whether the cache can even be looked up.
  if (s->cache_info.action == CACHE_DO_LOOKUP) {
    TxnDebug("http_trans", "[DecideCacheLookup] Will do cache lookup.");
    TxnDebug("http_seq", "[DecideCacheLookup] Will do cache lookup");
    ink_assert(s->current.mode != TUNNELLING_PROXY);

    if (s->cache_info.lookup_url == nullptr) {
      HTTPHdr *incoming_request = &s->hdr_info.client_request;

      if (s->txn_conf->maintain_pristine_host_hdr) {
        s->cache_info.lookup_url_storage.create(nullptr);
        s->cache_info.lookup_url_storage.copy(incoming_request->url_get());
        s->cache_info.lookup_url = &s->cache_info.lookup_url_storage;
        // if the target isn't in the URL, put it in the copy for
        // cache lookup.
        incoming_request->set_url_target_from_host_field(s->cache_info.lookup_url);
      } else {
        // make sure the target is in the URL.
        incoming_request->set_url_target_from_host_field();
        s->cache_info.lookup_url = incoming_request->url_get();
      }

      // *somebody* wants us to not hack the host header in a reverse proxy setup.
      // In addition, they want us to reverse proxy for 6000 servers, which vary
      // the stupid content on the Host header!!!!
      // We could a) have 6000 alts (barf, puke, vomit) or b) use the original
      // host header in the url before doing all cache actions (lookups, writes, etc.)
      if (s->txn_conf->maintain_pristine_host_hdr) {
        const char *host_hdr;
        const char *port_hdr;
        int host_len, port_len;
        // So, the host header will have the original host header.
        if (incoming_request->get_host_port_values(&host_hdr, &host_len, &port_hdr, &port_len)) {
          int port = 0;
          if (port_hdr) {
            s->cache_info.lookup_url->host_set(host_hdr, host_len);
            port = ink_atoi(port_hdr, port_len);
          } else {
            s->cache_info.lookup_url->host_set(host_hdr, host_len);
          }
          s->cache_info.lookup_url->port_set(port);
        }
      }
      ink_assert(s->cache_info.lookup_url->valid() == true);
    }

    TRANSACT_RETURN(SM_ACTION_CACHE_LOOKUP, nullptr);
  } else {
    ink_assert(s->cache_info.action != CACHE_DO_LOOKUP && s->cache_info.action != CACHE_DO_SERVE);

    TxnDebug("http_trans", "[DecideCacheLookup] Will NOT do cache lookup.");
    TxnDebug("http_seq", "[DecideCacheLookup] Will NOT do cache lookup");
    // If this is a push request, we need send an error because
    //   since what ever was sent is not cachable
    if (s->method == HTTP_WKSIDX_PUSH) {
      HandlePushError(s, "Request Not Cachable");
      return;
    }
    // for redirect, we skipped cache lookup to do the automatic redirection
    if (s->redirect_info.redirect_in_process) {
      // without calling out the CACHE_LOOKUP_COMPLETE_HOOK
      if (s->txn_conf->cache_http) {
        s->cache_info.action = CACHE_DO_WRITE;
      }
      LookupSkipOpenServer(s);
    } else {
      // calling out CACHE_LOOKUP_COMPLETE_HOOK even when the cache
      // lookup is skipped
      s->cache_lookup_result = HttpTransact::CACHE_LOOKUP_SKIPPED;
      if (s->force_dns) {
        TRANSACT_RETURN(SM_ACTION_API_CACHE_LOOKUP_COMPLETE, LookupSkipOpenServer);
      } else {
        // Returning to dns lookup as cache lookup is skipped
        TRANSACT_RETURN(SM_ACTION_API_CACHE_LOOKUP_COMPLETE, CallOSDNSLookup);
      }
    }
  }

  return;
}

void
HttpTransact::LookupSkipOpenServer(State *s)
{
  // cache will not be looked up. open a connection
  // to a parent proxy or to the origin server.
  find_server_and_update_current_info(s);

  if (s->current.request_to == PARENT_PROXY) {
    TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, PPDNSLookup);
  } else if (s->parent_result.result == PARENT_FAIL) {
    handle_parent_died(s);
    return;
  }

  ink_assert(s->current.request_to == ORIGIN_SERVER);
  // ink_assert(s->current.server->ip != 0);

  build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->current.server->http_version);

  StateMachineAction_t next = how_to_open_connection(s);
  s->next_action            = next;
  if (next == SM_ACTION_ORIGIN_SERVER_OPEN || next == SM_ACTION_ORIGIN_SERVER_RAW_OPEN) {
    TRANSACT_RETURN(next, HttpTransact::HandleResponse);
  }
}

//////////////////////////////////////////////////////////////////////////////
// Name       : HandleCacheOpenReadPush
// Description:
//
// Details    :
//
// Called on PUSH requests from HandleCacheOpenRead
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleCacheOpenReadPush(State *s, bool read_successful)
{
  if (read_successful) {
    s->cache_info.action = CACHE_PREPARE_TO_UPDATE;
  } else {
    s->cache_info.action = CACHE_PREPARE_TO_WRITE;
  }

  TRANSACT_RETURN(SM_ACTION_READ_PUSH_HDR, HandlePushResponseHdr);
}

//////////////////////////////////////////////////////////////////////////////
// Name       : HandlePushResponseHdr
// Description:
//
// Details    :
//
// Called after reading the response header on PUSH request
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandlePushResponseHdr(State *s)
{
  // Verify the pushed header wasn't longer than the content length
  int64_t body_bytes = s->hdr_info.request_content_length - s->state_machine->pushed_response_hdr_bytes;
  if (body_bytes < 0) {
    HandlePushError(s, "Bad Content Length");
    return;
  }
  // We need to create the request header storing in the cache
  s->hdr_info.server_request.create(HTTP_TYPE_REQUEST);
  s->hdr_info.server_request.copy(&s->hdr_info.client_request);
  s->hdr_info.server_request.method_set(HTTP_METHOD_GET, HTTP_LEN_GET);
  s->hdr_info.server_request.value_set("X-Inktomi-Source", 16, "http PUSH", 9);

  DUMP_HEADER("http_hdrs", &s->hdr_info.server_response, s->state_machine_id, "Pushed Response Header");

  DUMP_HEADER("http_hdrs", &s->hdr_info.server_request, s->state_machine_id, "Generated Request Header");

  s->response_received_time = s->request_sent_time = ink_local_time();

  if (is_response_cacheable(s, &s->hdr_info.server_request, &s->hdr_info.server_response)) {
    ink_assert(s->cache_info.action == CACHE_PREPARE_TO_WRITE || s->cache_info.action == CACHE_PREPARE_TO_UPDATE);

    TRANSACT_RETURN(SM_ACTION_CACHE_ISSUE_WRITE, HandlePushCacheWrite);
  } else {
    HandlePushError(s, "Response Not Cachable");
  }
}

//////////////////////////////////////////////////////////////////////////////
// Name       : HandlePushCacheWrite
// Description:
//
// Details    :
//
// Called after performing the cache write on a push request
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandlePushCacheWrite(State *s)
{
  switch (s->cache_info.write_lock_state) {
  case CACHE_WL_SUCCESS:
    // We were able to get the lock for the URL vector in the cache
    if (s->cache_info.action == CACHE_PREPARE_TO_WRITE) {
      s->cache_info.action = CACHE_DO_WRITE;
    } else if (s->cache_info.action == CACHE_PREPARE_TO_UPDATE) {
      s->cache_info.action = CACHE_DO_REPLACE;
    } else {
      ink_release_assert(0);
    }
    set_headers_for_cache_write(s, &s->cache_info.object_store, &s->hdr_info.server_request, &s->hdr_info.server_response);

    TRANSACT_RETURN(SM_ACTION_STORE_PUSH_BODY, nullptr);
    break;

  case CACHE_WL_FAIL:
  case CACHE_WL_READ_RETRY:
    // No write lock, can not complete request so bail
    HandlePushError(s, "Cache Write Failed");
    break;
  case CACHE_WL_INIT:
  default:
    ink_release_assert(0);
  }
}

void
HttpTransact::HandlePushTunnelSuccess(State *s)
{
  ink_assert(s->cache_info.action == CACHE_DO_WRITE || s->cache_info.action == CACHE_DO_REPLACE);

  // FIX ME: check PUSH spec for status codes
  HTTPStatus resp_status = (s->cache_info.action == CACHE_DO_WRITE) ? HTTP_STATUS_CREATED : HTTP_STATUS_OK;

  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, resp_status);

  TRANSACT_RETURN(SM_ACTION_INTERNAL_CACHE_NOOP, nullptr);
}

void
HttpTransact::HandlePushTunnelFailure(State *s)
{
  HandlePushError(s, "Cache Error");
}

void
HttpTransact::HandleBadPushRespHdr(State *s)
{
  HandlePushError(s, "Malformed Pushed Response Header");
}

void
HttpTransact::HandlePushError(State *s, const char *reason)
{
  s->client_info.keep_alive = HTTP_NO_KEEPALIVE;

  // Set half close flag to prevent TCP
  //   reset from the body still being transfered
  s->state_machine->set_ua_half_close_flag();

  build_error_response(s, HTTP_STATUS_BAD_REQUEST, reason, "default");
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleCacheOpenRead
// Description: the cache lookup succeeded - may have been a hit or a miss
//
// Details    :
//
// the cache lookup succeeded. first check if the lookup resulted in
// a hit or a miss, if the lookup was for an http request.
// This function just funnels the result into the appropriate
// functions which handle these different cases.
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleCacheOpenRead(State *s)
{
  TxnDebug("http_trans", "[HttpTransact::HandleCacheOpenRead]");

  SET_VIA_STRING(VIA_DETAIL_CACHE_TYPE, VIA_DETAIL_CACHE);

  bool read_successful = true;

  if (s->cache_info.object_read == nullptr) {
    read_successful = false;
    //
    // If somebody else was writing the document, proceed just like it was
    // a normal cache miss, except don't try to write to the cache
    //
    if (s->cache_lookup_result == CACHE_LOOKUP_DOC_BUSY) {
      s->cache_lookup_result = CACHE_LOOKUP_MISS;
      s->cache_info.action   = CACHE_DO_NO_ACTION;
    }
  } else {
    CacheHTTPInfo *obj = s->cache_info.object_read;
    if (obj->response_get()->type_get() == HTTP_TYPE_UNKNOWN) {
      read_successful = false;
    }
    if (obj->request_get()->type_get() == HTTP_TYPE_UNKNOWN) {
      read_successful = false;
    }
  }

  if (s->method == HTTP_WKSIDX_PUSH) {
    HandleCacheOpenReadPush(s, read_successful);
  } else if (read_successful == false) {
    // cache miss
    TxnDebug("http_trans", "CacheOpenRead -- miss");
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_NOT_CACHED);
    // Perform DNS for the origin when it is required.
    // 1. If parent configuration does not allow to go to origin there is no need of performing DNS
    // 2. If parent satisfies the request there is no need to go to origin to perfrom DNS
    HandleCacheOpenReadMiss(s);
  } else {
    // cache hit
    TxnDebug("http_trans", "CacheOpenRead -- hit");
    TRANSACT_RETURN(SM_ACTION_API_READ_CACHE_HDR, HandleCacheOpenReadHitFreshness);
  }

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : issue_revalidate
// Description:   Sets cache action and does various bookkeeping
//
// Details    :
//
// The Cache Lookup was hit but the document was stale so after
//   calling build_request, we need setup up the cache action,
//   set the via code, and possibly conditionalize the request
// The paths that we take to get this code are:
//   Directly from HandleOpenReadHit if we are going to the origin server
//   After PPDNS if we are going to a parent proxy
//
//
// Possible Next States From Here:
// -
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::issue_revalidate(State *s)
{
  HTTPHdr *c_resp = find_appropriate_cached_resp(s);
  SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_STALE);
  ink_assert(GET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP) != ' ');

  if (s->www_auth_content == CACHE_AUTH_FRESH) {
    s->hdr_info.server_request.method_set(HTTP_METHOD_HEAD, HTTP_LEN_HEAD);
    // The document is fresh in cache and we just want to see if the
    // the client has the right credentials
    // this cache action is just to get us into the hcoofsr function
    s->cache_info.action = CACHE_DO_UPDATE;
    DUMP_HEADER("http_hdrs", &s->hdr_info.server_request, s->state_machine_id, "Proxy's Request (Conditionalized)");
    return;
  }

  if (s->cache_info.write_lock_state == CACHE_WL_INIT) {
    // We do a cache lookup for DELETE, PUT and POST requests as well.
    // We must, however, delete the cached copy after forwarding the
    // request to the server. is_cache_response_returnable will ensure
    // that we forward the request. We now specify what the cache
    // action should be when the response is received.
    if (does_method_require_cache_copy_deletion(s->http_config_param, s->method)) {
      s->cache_info.action = CACHE_PREPARE_TO_DELETE;
      TxnDebug("http_seq", "[HttpTransact::issue_revalidate] cache action: DELETE");
    } else {
      s->cache_info.action = CACHE_PREPARE_TO_UPDATE;
      TxnDebug("http_seq", "[HttpTransact::issue_revalidate] cache action: UPDATE");
    }
  } else {
    // We've looped back around due to missing the write lock
    //  for the cache.  At this point we want to forget about the cache
    ink_assert(s->cache_info.write_lock_state == CACHE_WL_READ_RETRY);
    s->cache_info.action = CACHE_DO_NO_ACTION;
    return;
  }

  // if the document is cached, just send a conditional request to the server

  // So the request does not have preconditions. It can, however
  // be a simple GET request with a Pragma:no-cache. As on 8/28/98
  // we have fixed the whole Reload/Shift-Reload cached copy
  // corruption problem. This means that we can issue a conditional
  // request to the server only if the incoming request has a conditional
  // or the incoming request does NOT have a no-cache header.
  // In other words, if the incoming request is not conditional
  // but has a no-cache header we can not issue an IMS. check for
  // that case here.
  bool no_cache_in_request = false;

  if (s->hdr_info.client_request.is_pragma_no_cache_set() || s->hdr_info.client_request.is_cache_control_set(HTTP_VALUE_NO_CACHE)) {
    TxnDebug("http_trans", "[issue_revalidate] no-cache header directive in request, folks");
    no_cache_in_request = true;
  }

  if ((!(s->hdr_info.client_request.presence(MIME_PRESENCE_IF_MODIFIED_SINCE))) &&
      (!(s->hdr_info.client_request.presence(MIME_PRESENCE_IF_NONE_MATCH))) && (no_cache_in_request == true) &&
      (!s->txn_conf->cache_ims_on_client_no_cache) && (s->www_auth_content == CACHE_AUTH_NONE)) {
    TxnDebug("http_trans",
             "[issue_revalidate] Can not make this a conditional request. This is the force update of the cached copy case");
    // set cache action to update. response will be a 200 or error,
    // causing cached copy to be replaced (if 200).
    s->cache_info.action = CACHE_PREPARE_TO_UPDATE;
    return;
  }
  // do not conditionalize if the cached response is not a 200
  switch (c_resp->status_get()) {
  case HTTP_STATUS_OK: // 200
    // don't conditionalize if we are configured to repeat the clients
    //   conditionals
    if (s->txn_conf->cache_when_to_revalidate == 4) {
      break;
    }
    // ok, request is either a conditional or does not have a no-cache.
    //   (or is method that we don't conditionalize but lookup the
    //    cache on like DELETE)
    if (c_resp->get_last_modified() > 0 &&
        (s->hdr_info.server_request.method_get_wksidx() == HTTP_WKSIDX_GET ||
         s->hdr_info.server_request.method_get_wksidx() == HTTP_WKSIDX_HEAD) &&
        s->range_setup == RANGE_NONE) {
      // make this a conditional request
      int length;
      const char *str = c_resp->value_get(MIME_FIELD_LAST_MODIFIED, MIME_LEN_LAST_MODIFIED, &length);
      if (str) {
        s->hdr_info.server_request.value_set(MIME_FIELD_IF_MODIFIED_SINCE, MIME_LEN_IF_MODIFIED_SINCE, str, length);
      }
      DUMP_HEADER("http_hdrs", &s->hdr_info.server_request, s->state_machine_id, "Proxy's Request (Conditionalized)");
    }
    // if Etag exists, also add if-non-match header
    if (c_resp->presence(MIME_PRESENCE_ETAG) && (s->hdr_info.server_request.method_get_wksidx() == HTTP_WKSIDX_GET ||
                                                 s->hdr_info.server_request.method_get_wksidx() == HTTP_WKSIDX_HEAD)) {
      int length       = 0;
      const char *etag = c_resp->value_get(MIME_FIELD_ETAG, MIME_LEN_ETAG, &length);
      if (nullptr != etag) {
        if ((length >= 2) && (etag[0] == 'W') && (etag[1] == '/')) {
          etag += 2;
          length -= 2;
        }
        s->hdr_info.server_request.value_set(MIME_FIELD_IF_NONE_MATCH, MIME_LEN_IF_NONE_MATCH, etag, length);
      }
      DUMP_HEADER("http_hdrs", &s->hdr_info.server_request, s->state_machine_id, "Proxy's Request (Conditionalized)");
    }
    break;
  case HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION: // 203
  /* fall through */
  case HTTP_STATUS_MULTIPLE_CHOICES: // 300
  /* fall through */
  case HTTP_STATUS_MOVED_PERMANENTLY: // 301
  /* fall through */
  case HTTP_STATUS_GONE: // 410
  /* fall through */
  default:
    TxnDebug("http_trans", "[issue_revalidate] cached response is"
                           "not a 200 response so no conditionalization.");
    s->cache_info.action = CACHE_PREPARE_TO_UPDATE;
    break;
  case HTTP_STATUS_PARTIAL_CONTENT:
    ink_assert(!"unexpected status code");
    break;
  }
}

void
HttpTransact::HandleCacheOpenReadHitFreshness(State *s)
{
  CacheHTTPInfo *&obj = s->cache_info.object_read;

  ink_release_assert((s->request_sent_time == UNDEFINED_TIME) && (s->response_received_time == UNDEFINED_TIME));
  TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHitFreshness] Hit in cache");

  if (delete_all_document_alternates_and_return(s, true)) {
    TxnDebug("http_trans", "[HandleCacheOpenReadHitFreshness] Delete and return");
    s->cache_info.action = CACHE_DO_DELETE;
    s->next_action       = HttpTransact::SM_ACTION_INTERNAL_CACHE_DELETE;
    return;
  }

  s->request_sent_time      = obj->request_sent_time_get();
  s->response_received_time = obj->response_received_time_get();

  // There may be clock skew if one of the machines
  // went down and we do not have the correct delta
  // for it. this is just to deal with the effects
  // of the skew by setting minimum and maximum times
  // so that ages are not negative, etc.
  s->request_sent_time      = std::min(s->client_request_time, s->request_sent_time);
  s->response_received_time = std::min(s->client_request_time, s->response_received_time);

  ink_assert(s->request_sent_time <= s->response_received_time);

  TxnDebug("http_trans", "[HandleCacheOpenReadHitFreshness] request_sent_time      : %" PRId64, (int64_t)s->request_sent_time);
  TxnDebug("http_trans", "[HandleCacheOpenReadHitFreshness] response_received_time : %" PRId64, (int64_t)s->response_received_time);
  // if the plugin has already decided the freshness, we don't need to
  // do it again
  if (s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_NONE) {
    // is the document still fresh enough to be served back to
    // the client without revalidation?
    Freshness_t freshness = what_is_document_freshness(s, &s->hdr_info.client_request, obj->response_get());
    switch (freshness) {
    case FRESHNESS_FRESH:
      TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHitFreshness] "
                           "Fresh copy");
      s->cache_lookup_result = HttpTransact::CACHE_LOOKUP_HIT_FRESH;
      break;
    case FRESHNESS_WARNING:
      TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHitFreshness] "
                           "Heuristic-based Fresh copy");
      s->cache_lookup_result = HttpTransact::CACHE_LOOKUP_HIT_WARNING;
      break;
    case FRESHNESS_STALE:
      TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHitFreshness] "
                           "Stale in cache");
      s->cache_lookup_result       = HttpTransact::CACHE_LOOKUP_HIT_STALE;
      s->is_revalidation_necessary = true; // to identify a revalidation occurrence
      break;
    default:
      ink_assert(!("what_is_document_freshness has returned unsupported code."));
      break;
    }
  }

  ink_assert(s->cache_lookup_result != HttpTransact::CACHE_LOOKUP_MISS);
  if (s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_HIT_STALE) {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_EXPIRED);
    SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_STALE);
  }

  if (!s->force_dns) { // If DNS is not performed before
    if (need_to_revalidate(s)) {
      TRANSACT_RETURN(SM_ACTION_API_CACHE_LOOKUP_COMPLETE,
                      CallOSDNSLookup); // content needs to be revalidated and we did not perform a dns ....calling DNS lookup
    } else {                            // document can be served can cache
      TRANSACT_RETURN(SM_ACTION_API_CACHE_LOOKUP_COMPLETE, HttpTransact::HandleCacheOpenReadHit);
    }
  } else { // we have done dns . Its up to HandleCacheOpenReadHit to decide to go OS or serve from cache
    TRANSACT_RETURN(SM_ACTION_API_CACHE_LOOKUP_COMPLETE, HttpTransact::HandleCacheOpenReadHit);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : CallOSDNSLookup
// Description: Moves in SM_ACTION_DNS_LOOKUP state and sets the transact return to OSDNSLookup
//
// Details    :
/////////////////////////////////////////////////////////////////////////////
void
HttpTransact::CallOSDNSLookup(State *s)
{
  TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, OSDNSLookup);
}

///////////////////////////////////////////////////////////////////////////////
// Name       : need_to_revalidate
// Description: Checks if a document which is in the cache needs to be revalidates
//
// Details    : Function calls AuthenticationNeeded and is_cache_response_returnable to determine
//              if the cached document can be served
/////////////////////////////////////////////////////////////////////////////
bool
HttpTransact::need_to_revalidate(State *s)
{
  bool needs_revalidate, needs_authenticate = false;
  bool needs_cache_auth = false;
  CacheHTTPInfo *obj;

  if (s->api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    obj = &s->cache_info.object_store;
    ink_assert(obj->valid());
    if (!obj->valid()) {
      return true;
    }
  } else {
    obj = s->cache_info.object_read;
  }

  // do we have to authenticate with the server before
  // sending back the cached response to the client?
  Authentication_t authentication_needed = AuthenticationNeeded(s->txn_conf, &s->hdr_info.client_request, obj->response_get());

  switch (authentication_needed) {
  case AUTHENTICATION_SUCCESS:
    TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] "
                         "Authentication not needed");
    needs_authenticate = false;
    break;
  case AUTHENTICATION_MUST_REVALIDATE:
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_METHOD);
    TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] "
                         "Authentication needed");
    needs_authenticate = true;
    break;
  case AUTHENTICATION_MUST_PROXY:
    TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] "
                         "Authentication needed");
    needs_authenticate = true;
    break;
  case AUTHENTICATION_CACHE_AUTH:
    TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] "
                         "Authentication needed for cache_auth_content");
    needs_authenticate = false;
    needs_cache_auth   = true;
    break;
  default:
    ink_assert(!("AuthenticationNeeded has returned unsupported code."));
    return true;
    break;
  }

  ink_assert(s->cache_lookup_result == CACHE_LOOKUP_HIT_FRESH || s->cache_lookup_result == CACHE_LOOKUP_HIT_WARNING ||
             s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE);
  if (s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE &&
      s->api_update_cached_object != HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    needs_revalidate = true;
  } else {
    needs_revalidate = false;
  }

  bool send_revalidate = ((needs_authenticate == true) || (needs_revalidate == true) || (is_cache_response_returnable(s) == false));
  if (needs_cache_auth == true) {
    s->www_auth_content = send_revalidate ? CACHE_AUTH_STALE : CACHE_AUTH_FRESH;
    send_revalidate     = true;
  }
  return send_revalidate;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleCacheOpenReadHit
// Description: handle result of a cache hit
//
// Details    :
//
// Cache lookup succeeded and resulted in a cache hit. This means
// that the Accept* and Etags fields also matched. The cache lookup
// may have resulted in a vector of alternates (since lookup may
// be based on a url). A different function (SelectFromAlternates)
// goes through the alternates and finds the best match. That is
// then returned to this function. The result may not be sent back
// to the client, still, if the document is not fresh enough, or
// does not have enough authorization, or if the client wants a
// reload, etc. that decision will be made in this routine.
//
//
// Possible Next States From Here:
// - HttpTransact::PROXY_INTERNAL_CACHE_DELETE;
// - HttpTransact::SM_ACTION_DNS_LOOKUP;
// - HttpTransact::ORIGIN_SERVER_OPEN;
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
// - HttpTransact::SERVE_FROM_CACHE;
// - result of how_to_open_connection()
//
//
// For Range requests, we will decide to do simple tunneling if one of the
// following conditions hold:
// - document stale
// - cached response doesn't have Accept-Ranges and Content-Length
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleCacheOpenReadHit(State *s)
{
  bool needs_revalidate   = false;
  bool needs_authenticate = false;
  bool needs_cache_auth   = false;
  bool server_up          = true;
  CacheHTTPInfo *obj;

  if (s->api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    obj = &s->cache_info.object_store;
    ink_assert(obj->valid());
  } else {
    obj = s->cache_info.object_read;
  }

  // do we have to authenticate with the server before
  // sending back the cached response to the client?
  Authentication_t authentication_needed = AuthenticationNeeded(s->txn_conf, &s->hdr_info.client_request, obj->response_get());

  switch (authentication_needed) {
  case AUTHENTICATION_SUCCESS:
    TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] "
                         "Authentication not needed");
    needs_authenticate = false;
    break;
  case AUTHENTICATION_MUST_REVALIDATE:
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_METHOD);
    TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] "
                         "Authentication needed");
    needs_authenticate = true;
    break;
  case AUTHENTICATION_MUST_PROXY:
    TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] "
                         "Authentication needed");
    HandleCacheOpenReadMiss(s);
    return;
  case AUTHENTICATION_CACHE_AUTH:
    TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] "
                         "Authentication needed for cache_auth_content");
    needs_authenticate = false;
    needs_cache_auth   = true;
    break;
  default:
    ink_assert(!("AuthenticationNeeded has returned unsupported code."));
    break;
  }

  ink_assert(s->cache_lookup_result == CACHE_LOOKUP_HIT_FRESH || s->cache_lookup_result == CACHE_LOOKUP_HIT_WARNING ||
             s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE);

  // We'll request a revalidation under one of these conditions:
  //
  // 1. Cache lookup is a hit, but the response is stale
  // 2. The cached object has a "Cache-Control: no-cache" header
  //       *and*
  //    proxy.config.http.cache.ignore_server_no_cache is set to 0 (i.e don't ignore no cache -- the default setting)
  //
  // But, we only do this if we're not in an API updating the cached object (see TSHttpTxnUpdateCachedObject)
  if ((((s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE) ||
        ((obj->response_get()->get_cooked_cc_mask() & MIME_COOKED_MASK_CC_NO_CACHE) && !s->cache_control.ignore_server_no_cache)) &&
       (s->api_update_cached_object != HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE))) {
    needs_revalidate = true;
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_EXPIRED);
  }

  // the response may not be directly returnable to the client. there
  // are several reasons for this: config may force revalidation or
  // client may have forced a refresh by sending a Pragma:no-cache
  // or a Cache-Control:no-cache, or the client may have sent a
  // non-GET/HEAD request for a document that is cached. an example
  // of a situation for this is when a client sends a DELETE, PUT
  // or POST request for a url that is cached. except for DELETE,
  // we may actually want to update the cached copy with the contents
  // of the PUT/POST, but the easiest, safest and most robust solution
  // is to simply delete the cached copy (in order to maintain cache
  // consistency). this is particularly true if the server does not
  // accept or conditionally accepts the PUT/POST requests.
  // anyhow, this is an overloaded function and will return false
  // if the origin server still has to be looked up.
  bool response_returnable = is_cache_response_returnable(s);

  // do we need to revalidate. in other words if the response
  // has to be authorized, is stale or can not be returned, do
  // a revalidate.
  bool send_revalidate = (needs_authenticate || needs_revalidate || !response_returnable);

  if (needs_cache_auth == true) {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_EXPIRED);
    s->www_auth_content = send_revalidate ? CACHE_AUTH_STALE : CACHE_AUTH_FRESH;
    send_revalidate     = true;
  }

  TxnDebug("http_trans", "CacheOpenRead --- needs_auth          = %d", needs_authenticate);
  TxnDebug("http_trans", "CacheOpenRead --- needs_revalidate    = %d", needs_revalidate);
  TxnDebug("http_trans", "CacheOpenRead --- response_returnable = %d", response_returnable);
  TxnDebug("http_trans", "CacheOpenRead --- needs_cache_auth    = %d", needs_cache_auth);
  TxnDebug("http_trans", "CacheOpenRead --- send_revalidate     = %d", send_revalidate);

  if (send_revalidate) {
    TxnDebug("http_trans", "CacheOpenRead --- HIT-STALE");
    s->dns_info.attempts = 0;

    TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] "
                         "Revalidate document with server");

    find_server_and_update_current_info(s);

    // We do not want to try to revalidate documents if we think
    //  the server is down due to the something report problem
    //
    // Note: we only want to skip origin servers because 1)
    //  parent proxies have their own negative caching
    //  scheme & 2) If we skip down parents, every page
    //  we serve is potentially stale
    //
    if (s->current.request_to == ORIGIN_SERVER && is_server_negative_cached(s) && response_returnable == true &&
        is_stale_cache_response_returnable(s) == true) {
      server_up = false;
      update_current_info(&s->current, nullptr, UNDEFINED_LOOKUP, 0);
      TxnDebug("http_trans", "CacheOpenReadHit - server_down, returning stale document");
    }
    // a parent lookup could come back as PARENT_FAIL if in parent.config, go_direct == false and
    // there are no available parents (all down).
    else if (s->current.request_to == HOST_NONE && s->parent_result.result == PARENT_FAIL) {
      if (is_server_negative_cached(s) && response_returnable == true && is_stale_cache_response_returnable(s) == true) {
        server_up = false;
        update_current_info(&s->current, nullptr, UNDEFINED_LOOKUP, 0);
        TxnDebug("http_trans", "CacheOpenReadHit - server_down, returning stale document");
      } else {
        handle_parent_died(s);
        return;
      }
    }

    if (server_up) {
      // set a default version for the outgoing request
      HTTPVersion http_version;

      if (s->current.server != nullptr) {
        bool check_hostdb = get_ka_info_from_config(s, s->current.server);
        TxnDebug("http_trans", "CacheOpenReadHit - check_hostdb %d", check_hostdb);
        if (check_hostdb || !s->current.server->dst_addr.isValid()) {
          // We must be going a PARENT PROXY since so did
          //  origin server DNS lookup right after state Start
          //
          // If we end up here in the release case just fall
          //  through.  The request will fail because of the
          //  missing ip but we won't take down the system
          //
          if (s->current.request_to == PARENT_PROXY) {
            // Set ourselves up to handle pending revalidate issues
            //  after the PP DNS lookup
            ink_assert(s->pending_work == nullptr);
            s->pending_work = issue_revalidate;

            TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, PPDNSLookup);
          } else if (s->current.request_to == ORIGIN_SERVER) {
            TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, OSDNSLookup);
          } else {
            handle_parent_died(s);
            return;
          }
        }
        // override the default version with what the server has
        http_version = s->current.server->http_version;
      }

      TxnDebug("http_trans", "CacheOpenReadHit - version %d", http_version.m_version);
      build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, http_version);

      issue_revalidate(s);

      // this can not be anything but a simple origin server connection.
      // in other words, we would not have looked up the cache for a
      // connect request, so the next action can not be origin_server_raw_open.
      s->next_action = how_to_open_connection(s);

      ink_release_assert(s->next_action != SM_ACTION_ORIGIN_SERVER_RAW_OPEN);
      return;
    } else { // server is down but stale response is returnable
      SET_VIA_STRING(VIA_DETAIL_CACHE_TYPE, VIA_DETAIL_CACHE);
    }
  }
  // cache hit, document is fresh, does not authorization,
  // is valid, etc. etc. send it back to the client.
  //
  // the important thing to keep in mind is that if we are
  // here then we found a match in the cache and the document
  // is fresh and we have enough authorization for it to send
  // it back to the client without revalidating first with the
  // origin server. we are, therefore, allowed to behave as the
  // origin server. we can, therefore, make the claim that the
  // document has not been modified since or has not been unmodified
  // since the time requested by the client. this may not be
  // the case in reality, but since the document is fresh in
  // the cache, we can make the claim that this is the truth.
  //
  // so, any decision we make at this point can be made with authority.
  // realistically, if we can not make this claim, then there
  // is no reason to cache anything.
  //
  ink_assert((send_revalidate == true && server_up == false) || (send_revalidate == false && server_up == true));

  TxnDebug("http_trans", "CacheOpenRead --- HIT-FRESH");
  TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] "
                       "Serve from cache");

  // ToDo: Should support other levels of cache hits here, but the cache does not support it (yet)
  if (SQUID_HIT_RAM == s->cache_info.hit_miss_code) {
    SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_RAM_CACHE_FRESH);
  } else {
    SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_FRESH);
  }

  if (s->cache_lookup_result == CACHE_LOOKUP_HIT_WARNING) {
    build_response_from_cache(s, HTTP_WARNING_CODE_HERUISTIC_EXPIRATION);
  } else if (s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE) {
    ink_assert(server_up == false);
    build_response_from_cache(s, HTTP_WARNING_CODE_REVALIDATION_FAILED);
  } else {
    build_response_from_cache(s, HTTP_WARNING_CODE_NONE);
  }

  if (s->api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    s->saved_update_next_action  = s->next_action;
    s->saved_update_cache_action = s->cache_info.action;
    s->next_action               = SM_ACTION_CACHE_PREPARE_UPDATE;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : build_response_from_cache()
// Description: build a client response from cached response and client request
//
// Input      : State, warning code to be inserted into the response header
// Output     :
//
// Details    : This function is called if we decided to serve a client request
//              using a cached response.
//              It is called by handle_server_connection_not_open()
//              and HandleCacheOpenReadHit().
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::build_response_from_cache(State *s, HTTPWarningCode warning_code)
{
  HTTPHdr *client_request  = &s->hdr_info.client_request;
  HTTPHdr *cached_response = nullptr;
  HTTPHdr *to_warn         = &s->hdr_info.client_response;
  CacheHTTPInfo *obj;

  if (s->api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    obj = &s->cache_info.object_store;
    ink_assert(obj->valid());
  } else {
    obj = s->cache_info.object_read;
  }
  cached_response = obj->response_get();

  // If the client request is conditional, and the cached copy meets
  // the conditions, do not need to send back the full document,
  // just a NOT_MODIFIED response.
  // If the request is not conditional,
  // the function match_response_to_request_conditionals() returns
  // the code of the cached response, which means that we should send
  // back the full document.
  HTTPStatus client_response_code =
    HttpTransactCache::match_response_to_request_conditionals(client_request, cached_response, s->response_received_time);

  switch (client_response_code) {
  case HTTP_STATUS_NOT_MODIFIED:
    // A IMS or INM GET client request with conditions being met
    // by the cached response.  Send back a NOT MODIFIED response.
    TxnDebug("http_trans", "[build_response_from_cache] Not modified");
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_HIT_CONDITIONAL);

    build_response(s, cached_response, &s->hdr_info.client_response, s->client_info.http_version, client_response_code);
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->next_action       = SM_ACTION_INTERNAL_CACHE_NOOP;
    break;

  case HTTP_STATUS_PRECONDITION_FAILED:
    // A conditional request with conditions not being met by the cached
    // response.  Send back a PRECONDITION FAILED response.
    TxnDebug("http_trans", "[build_response_from_cache] Precondition Failed");
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_CONDITIONAL);

    build_response(s, &s->hdr_info.client_response, s->client_info.http_version, client_response_code);
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->next_action       = SM_ACTION_INTERNAL_CACHE_NOOP;
    break;

  case HTTP_STATUS_RANGE_NOT_SATISFIABLE:
  // Check if cached response supports Range. If it does, append
  // Range transformation plugin
  // A little misnomer. HTTP_STATUS_RANGE_NOT_SATISFIABLE
  // acutally means If-Range match fails here.
  // fall through
  default:
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_HIT_SERVED);
    if (s->method == HTTP_WKSIDX_GET || s->api_resp_cacheable == true) {
      // send back the full document to the client.
      TxnDebug("http_trans", "[build_response_from_cache] Match! Serving full document.");
      s->cache_info.action = CACHE_DO_SERVE;

      // Check if cached response supports Range. If it does, append
      // Range transformation plugin
      // only if the cached response is a 200 OK
      if (client_response_code == HTTP_STATUS_OK && client_request->presence(MIME_PRESENCE_RANGE)) {
        s->state_machine->do_range_setup_if_necessary();
        if (s->range_setup == RANGE_NOT_SATISFIABLE) {
          build_error_response(s, HTTP_STATUS_RANGE_NOT_SATISFIABLE, "Requested Range Not Satisfiable", "default");
          s->cache_info.action = CACHE_DO_NO_ACTION;
          s->next_action       = SM_ACTION_INTERNAL_CACHE_NOOP;
          break;
        } else if ((s->range_setup == RANGE_NOT_HANDLED) || !s->range_in_cache) {
          // we switch to tunneling for Range requests if it is out of order.
          // or if the range can't be satisfied from the cache
          // In that case we fetch the entire source so it's OK to switch
          // this late.
          TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] Out-of-order Range request - tunneling");
          s->cache_info.action = CACHE_DO_NO_ACTION;
          if (s->force_dns) {
            HandleCacheOpenReadMiss(s); // DNS is already completed no need of doing DNS
          } else {
            TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP,
                            OSDNSLookup); // DNS not done before need to be done now as we are connecting to OS
          }
          return;
        }
      }

      if (s->state_machine->do_transform_open()) {
        set_header_for_transform(s, cached_response);
        to_warn = &s->hdr_info.transform_response;
      } else {
        build_response(s, cached_response, &s->hdr_info.client_response, s->client_info.http_version);
      }
      s->next_action = SM_ACTION_SERVE_FROM_CACHE;
    }
    // If the client request is a HEAD, then serve the header from cache.
    else if (s->method == HTTP_WKSIDX_HEAD) {
      TxnDebug("http_trans", "[build_response_from_cache] Match! Serving header only.");

      build_response(s, cached_response, &s->hdr_info.client_response, s->client_info.http_version);
      s->cache_info.action = CACHE_DO_NO_ACTION;
      s->next_action       = SM_ACTION_INTERNAL_CACHE_NOOP;
    } else {
      // We handled the request but it's not GET or HEAD (eg. DELETE),
      // and server is not reacheable: 502
      //
      TxnDebug("http_trans", "[build_response_from_cache] No match! Connection failed.");
      build_error_response(s, HTTP_STATUS_BAD_GATEWAY, "Connection Failed", "connect#failed_connect");
      s->cache_info.action = CACHE_DO_NO_ACTION;
      s->next_action       = SM_ACTION_INTERNAL_CACHE_NOOP;
      warning_code         = HTTP_WARNING_CODE_NONE;
    }
    break;
  }

  // After building the client response, add the given warning if provided.
  if (warning_code != HTTP_WARNING_CODE_NONE) {
    delete_warning_value(to_warn, warning_code);
    HttpTransactHeaders::insert_warning_header(s->http_config_param, to_warn, warning_code);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : handle_cache_write_lock
// Description:
//
// Details    :
//
//
//
// Possible Next States From Here:
// - result of how_to_open_connection
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_cache_write_lock(State *s)
{
  bool remove_ims = false;

  ink_assert(s->cache_info.action == CACHE_PREPARE_TO_DELETE || s->cache_info.action == CACHE_PREPARE_TO_UPDATE ||
             s->cache_info.action == CACHE_PREPARE_TO_WRITE);

  switch (s->cache_info.write_lock_state) {
  case CACHE_WL_SUCCESS:
    // We were able to get the lock for the URL vector in the cache
    SET_UNPREPARE_CACHE_ACTION(s->cache_info);
    break;
  case CACHE_WL_FAIL:
    // No write lock, ignore the cache and proxy only;
    // FIX: Should just serve from cache if this is a revalidate
    s->cache_info.action = CACHE_DO_NO_ACTION;
    switch (s->cache_open_write_fail_action) {
    case CACHE_WL_FAIL_ACTION_ERROR_ON_MISS:
    case CACHE_WL_FAIL_ACTION_ERROR_ON_MISS_STALE_ON_REVALIDATE:
    case CACHE_WL_FAIL_ACTION_ERROR_ON_MISS_OR_REVALIDATE:
      TxnDebug("http_error", "cache_open_write_fail_action %d, cache miss, return error", s->cache_open_write_fail_action);
      s->cache_info.write_status = CACHE_WRITE_ERROR;
      build_error_response(s, HTTP_STATUS_BAD_GATEWAY, "Connection Failed", "connect#failed_connect");
      MIMEField *ats_field;
      HTTPHdr *header;
      header = &(s->hdr_info.client_response);
      if ((ats_field = header->field_find(MIME_FIELD_ATS_INTERNAL, MIME_LEN_ATS_INTERNAL)) == nullptr) {
        if (likely((ats_field = header->field_create(MIME_FIELD_ATS_INTERNAL, MIME_LEN_ATS_INTERNAL)) != nullptr)) {
          header->field_attach(ats_field);
        }
      }
      if (likely(ats_field)) {
        int value = (s->cache_info.object_read) ? 1 : 0;
        TxnDebug("http_error", "Adding Ats-Internal-Messages: %d", value);
        header->field_value_set_int(ats_field, value);
      } else {
        TxnDebug("http_error", "failed to add Ats-Internal-Messages");
      }

      TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
      return;
    default:
      s->cache_info.write_status = CACHE_WRITE_LOCK_MISS;
      remove_ims                 = true;
      break;
    }
    break;
  case CACHE_WL_READ_RETRY:
    //  Write failed but retried and got a vector to read
    //  We need to clean up our state so that transact does
    //  not assert later on.  Then handle the open read hit
    //
    s->request_sent_time      = UNDEFINED_TIME;
    s->response_received_time = UNDEFINED_TIME;
    s->cache_info.action      = CACHE_DO_LOOKUP;
    remove_ims                = true;
    SET_VIA_STRING(VIA_DETAIL_CACHE_TYPE, VIA_DETAIL_CACHE);
    break;
  case CACHE_WL_INIT:
  default:
    ink_release_assert(0);
    break;
  }

  // Since we've already built the server request and we can't get the write
  //  lock we need to remove the ims field from the request since we're
  //  ignoring the cache.  If their is a client ims field, copy that since
  //  we're tunneling response anyway
  if (remove_ims) {
    s->hdr_info.server_request.field_delete(MIME_FIELD_IF_MODIFIED_SINCE, MIME_LEN_IF_MODIFIED_SINCE);
    s->hdr_info.server_request.field_delete(MIME_FIELD_IF_NONE_MATCH, MIME_LEN_IF_NONE_MATCH);
    MIMEField *c_ims = s->hdr_info.client_request.field_find(MIME_FIELD_IF_MODIFIED_SINCE, MIME_LEN_IF_MODIFIED_SINCE);
    MIMEField *c_inm = s->hdr_info.client_request.field_find(MIME_FIELD_IF_NONE_MATCH, MIME_LEN_IF_NONE_MATCH);

    if (c_ims) {
      int len;
      const char *value = c_ims->value_get(&len);
      s->hdr_info.server_request.value_set(MIME_FIELD_IF_MODIFIED_SINCE, MIME_LEN_IF_MODIFIED_SINCE, value, len);
    }
    if (c_inm) {
      int len;
      const char *value = c_inm->value_get(&len);
      s->hdr_info.server_request.value_set(MIME_FIELD_IF_NONE_MATCH, MIME_LEN_IF_NONE_MATCH, value, len);
    }
  }

  if (s->cache_info.write_lock_state == CACHE_WL_READ_RETRY) {
    TxnDebug("http_error", "calling hdr_info.server_request.destroy");
    s->hdr_info.server_request.destroy();
    HandleCacheOpenReadHitFreshness(s);
  } else {
    StateMachineAction_t next;
    next = how_to_open_connection(s);
    if (next == SM_ACTION_ORIGIN_SERVER_OPEN || next == SM_ACTION_ORIGIN_SERVER_RAW_OPEN) {
      s->next_action = next;
      TRANSACT_RETURN(next, nullptr);
    } else {
      // hehe!
      s->next_action = next;
      ink_assert(s->next_action == SM_ACTION_DNS_LOOKUP);
      return;
    }

    TRANSACT_RETURN(next, nullptr);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleCacheOpenReadMiss
// Description: cache looked up, miss or hit, but needs authorization
//
// Details    :
//
//
//
// Possible Next States From Here:
// - HttpTransact::SM_ACTION_DNS_LOOKUP;
// - HttpTransact::ORIGIN_SERVER_OPEN;
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
// - result of how_to_open_connection()
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleCacheOpenReadMiss(State *s)
{
  TxnDebug("http_trans", "[HandleCacheOpenReadMiss] --- MISS");
  TxnDebug("http_seq", "[HttpTransact::HandleCacheOpenReadMiss] "
                       "Miss in cache");

  if (delete_all_document_alternates_and_return(s, false)) {
    TxnDebug("http_trans", "[HandleCacheOpenReadMiss] Delete and return");
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->next_action       = SM_ACTION_INTERNAL_CACHE_NOOP;
    return;
  }
  // reinitialize some variables to reflect cache miss state.
  s->cache_info.object_read = nullptr;
  s->request_sent_time      = UNDEFINED_TIME;
  s->response_received_time = UNDEFINED_TIME;
  SET_VIA_STRING(VIA_CACHE_RESULT, VIA_CACHE_MISS);
  if (GET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP) == ' ') {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_NOT_CACHED);
  }
  // We do a cache lookup for DELETE and PUT requests as well.
  // We must, however, not cache the responses to these requests.
  if (does_method_require_cache_copy_deletion(s->http_config_param, s->method) && s->api_req_cacheable == false) {
    s->cache_info.action = CACHE_DO_NO_ACTION;
  } else if ((s->hdr_info.client_request.presence(MIME_PRESENCE_RANGE) && !s->txn_conf->cache_range_write) ||
             does_method_effect_cache(s->method) == false || s->range_setup == RANGE_NOT_SATISFIABLE ||
             s->range_setup == RANGE_NOT_HANDLED) {
    s->cache_info.action = CACHE_DO_NO_ACTION;
  } else {
    s->cache_info.action = CACHE_PREPARE_TO_WRITE;
  }

  ///////////////////////////////////////////////////////////////
  // a normal miss would try to fetch the document from the    //
  // origin server, unless the origin server isn't resolvable, //
  // but if "CacheControl: only-if-cached" is set, then we are //
  // supposed to send a 504 (GATEWAY TIMEOUT) response.        //
  ///////////////////////////////////////////////////////////////

  HTTPHdr *h = &s->hdr_info.client_request;

  if (!h->is_cache_control_set(HTTP_VALUE_ONLY_IF_CACHED)) {
    // Initialize the server_info structure if we haven't been through DNS
    // Otherwise, the http_version will not be initialized
    if (!s->current.server || !s->current.server->dst_addr.isValid()) {
      // Short term hack.  get_ka_info_from_config assumes if http_version is > 0,9 it has already been
      // set and skips the rest of the function.  The default functor sets it to 1,0
      s->server_info.http_version = HTTPVersion(0, 9);
      get_ka_info_from_config(s, &s->server_info);
    }
    find_server_and_update_current_info(s);
    // a parent lookup could come back as PARENT_FAIL if in parent.config go_direct == false and
    // there are no available parents (all down).
    if (s->parent_result.result == PARENT_FAIL) {
      handle_parent_died(s);
      return;
    }
    if (!s->current.server->dst_addr.isValid()) {
      ink_release_assert(s->parent_result.result == PARENT_DIRECT || s->current.request_to == PARENT_PROXY ||
                         s->http_config_param->no_dns_forward_to_parent != 0);
      if (s->parent_result.result == PARENT_DIRECT && s->http_config_param->no_dns_forward_to_parent != 1) {
        TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, OSDNSLookup);
        return;
      }
      if (s->current.request_to == PARENT_PROXY) {
        TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, HttpTransact::PPDNSLookup);
      } else {
        handle_parent_died(s);
        return;
      }
    }
    build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->current.server->http_version);
    s->current.attempts = 0;
    s->next_action      = how_to_open_connection(s);
    if (s->current.server == &s->server_info && s->next_hop_scheme == URL_WKSIDX_HTTP) {
      HttpTransactHeaders::remove_host_name_from_url(&s->hdr_info.server_request);
    }
  } else { // miss, but only-if-cached is set
    build_error_response(s, HTTP_STATUS_GATEWAY_TIMEOUT, "Not Cached", "cache#not_in_cache");
    s->next_action = SM_ACTION_SEND_ERROR_CACHE_NOOP;
  }

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : OriginServerRawOpen
// Description: called for ssl tunneling
//
// Details    :
//
// when the method is CONNECT, we open a raw connection to the origin
// server. if the open succeeds, then do ssl tunneling from the client
// to the host.
//
//
// Possible Next States From Here:
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
// - HttpTransact::SSL_TUNNEL;
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::OriginServerRawOpen(State *s)
{
  TxnDebug("http_trans", "[HttpTransact::OriginServerRawOpen]");

  switch (s->current.state) {
  case STATE_UNDEFINED:
  /* fall through */
  case OPEN_RAW_ERROR:
  /* fall through */
  case CONNECTION_ERROR:
  /* fall through */
  case CONNECTION_CLOSED:
    /* fall through */
    handle_server_died(s);

    ink_assert(s->cache_info.action == CACHE_DO_NO_ACTION);
    s->next_action = SM_ACTION_INTERNAL_CACHE_NOOP;
    break;
  case CONNECTION_ALIVE:
    build_response(s, &s->hdr_info.client_response, s->client_info.http_version, HTTP_STATUS_OK);

    TxnDebug("http_trans", "[OriginServerRawOpen] connection alive. next action is ssl_tunnel");
    s->next_action = SM_ACTION_SSL_TUNNEL;
    break;
  default:
    ink_assert(!("s->current.state is set to something unsupported"));
    break;
  }

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleResponse
// Description: called from the state machine when a response is received
//
// Details    :
//
//   This is the entry into a coin-sorting machine. There are many different
//   bins that the response can fall into. First, the response can be invalid
//   if for example it is not a response, or not complete, or the connection
//   was closed, etc. Then, the response can be from  parent proxy or from
//   the origin server. The next action to take differs for all three of these
//   cases. Finally, good responses can either require a cache action,
//   be it deletion, update, or writing or may just need to be tunnelled
//   to the client. This latter case should be handled with as little processing
//   as possible, since it should represent a fast path.
//
//
// Possible Next States From Here:
//
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleResponse(State *s)
{
  TxnDebug("http_trans", "[HttpTransact::HandleResponse]");
  TxnDebug("http_seq", "[HttpTransact::HandleResponse] Response received");

  s->source                 = SOURCE_HTTP_ORIGIN_SERVER;
  s->response_received_time = ink_local_time();
  ink_assert(s->response_received_time >= s->request_sent_time);
  s->current.now = s->response_received_time;

  TxnDebug("http_trans", "[HandleResponse] response_received_time: %" PRId64, (int64_t)s->response_received_time);
  DUMP_HEADER("http_hdrs", &s->hdr_info.server_response, s->state_machine_id, "Incoming O.S. Response");

  HTTP_INCREMENT_DYN_STAT(http_incoming_responses_stat);

  ink_release_assert(s->current.request_to != UNDEFINED_LOOKUP);
  if (s->cache_info.action != CACHE_DO_WRITE) {
    ink_release_assert(s->cache_info.action != CACHE_DO_LOOKUP);
    ink_release_assert(s->cache_info.action != CACHE_DO_SERVE);
    ink_release_assert(s->cache_info.action != CACHE_PREPARE_TO_DELETE);
    ink_release_assert(s->cache_info.action != CACHE_PREPARE_TO_UPDATE);
    ink_release_assert(s->cache_info.action != CACHE_PREPARE_TO_WRITE);
  }

  if (!is_response_valid(s, &s->hdr_info.server_response)) {
    TxnDebug("http_seq", "[HttpTransact::HandleResponse] Response not valid");
  } else {
    TxnDebug("http_seq", "[HttpTransact::HandleResponse] Response valid");
    initialize_state_variables_from_response(s, &s->hdr_info.server_response);
  }

  switch (s->current.request_to) {
  case PARENT_PROXY:
    handle_response_from_parent(s);
    break;
  case ORIGIN_SERVER:
    handle_response_from_server(s);
    break;
  default:
    ink_assert(!("s->current.request_to is not P.P. or O.S. - hmmm."));
    break;
  }

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleUpdateCachedObject
// Description: called from the state machine when we are going to modify
//              headers without any server contact.
//
// Details    : this function does very little. mainly to satisfy
//              the call_transact_and_set_next format and not affect
//              the performance of the non-invalidate operations, which
//              are the majority
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleUpdateCachedObject(State *s)
{
  if (s->cache_info.write_lock_state == HttpTransact::CACHE_WL_SUCCESS) {
    ink_assert(s->cache_info.object_store.valid());
    ink_assert(s->cache_info.object_store.response_get() != nullptr);
    ink_assert(s->cache_info.object_read != nullptr);
    ink_assert(s->cache_info.object_read->valid());

    if (!s->cache_info.object_store.request_get()) {
      s->cache_info.object_store.request_set(s->cache_info.object_read->request_get());
    }
    s->request_sent_time      = s->cache_info.object_read->request_sent_time_get();
    s->response_received_time = s->cache_info.object_read->response_received_time_get();
    if (s->api_update_cached_object == UPDATE_CACHED_OBJECT_CONTINUE) {
      TRANSACT_RETURN(SM_ACTION_CACHE_ISSUE_UPDATE, HttpTransact::HandleUpdateCachedObjectContinue);
    } else {
      TRANSACT_RETURN(SM_ACTION_CACHE_ISSUE_UPDATE, HttpTransact::HandleApiErrorJump);
    }
  } else if (s->api_update_cached_object == UPDATE_CACHED_OBJECT_CONTINUE) {
    // even failed to update, continue to serve from cache
    HandleUpdateCachedObjectContinue(s);
  } else {
    s->api_update_cached_object = UPDATE_CACHED_OBJECT_FAIL;
    HandleApiErrorJump(s);
  }
}

void
HttpTransact::HandleUpdateCachedObjectContinue(State *s)
{
  ink_assert(s->api_update_cached_object == UPDATE_CACHED_OBJECT_CONTINUE);
  s->cache_info.action = s->saved_update_cache_action;
  s->next_action       = s->saved_update_next_action;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleStatPage
// Description: called from the state machine when a response is received
//
// Details    :
//
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleStatPage(State *s)
{
  HTTPStatus status;

  if (s->internal_msg_buffer) {
    status = HTTP_STATUS_OK;
  } else {
    status = HTTP_STATUS_NOT_FOUND;
  }

  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, status);

  ///////////////////////////
  // insert content-length //
  ///////////////////////////
  s->hdr_info.client_response.set_content_length(s->internal_msg_buffer_size);

  if (s->internal_msg_buffer_type) {
    int len = strlen(s->internal_msg_buffer_type);

    if (len > 0) {
      s->hdr_info.client_response.value_set(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, s->internal_msg_buffer_type, len);
    }
  } else {
    s->hdr_info.client_response.value_set(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, "text/plain", 10);
  }

  s->cache_info.action = CACHE_DO_NO_ACTION;
  s->next_action       = SM_ACTION_INTERNAL_CACHE_NOOP;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : handle_response_from_parent
// Description: response came from a parent proxy
//
// Details    :
//
//   The configuration file can be used to specify more than one parent
//   proxy. If a connection to one fails, another can be looked up. This
//   function handles responses from parent proxies. If the response is
//   bad the next parent proxy (if any) is looked up. If there are no more
//   parent proxies that can be looked up, the response is sent to the
//   origin server. If the response is good handle_forward_server_connection_open
//   is called.
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_response_from_parent(State *s)
{
  TxnDebug("http_trans", "[handle_response_from_parent] (hrfp)");
  HTTP_RELEASE_ASSERT(s->current.server == &s->parent_info);

  // response is from a parent origin server.
  if (is_response_valid(s, &s->hdr_info.server_response) && s->current.request_to == HttpTransact::PARENT_PROXY) {
    // check for a retryable response if simple or unavailable server retry are enabled.
    if (s->parent_result.retry_type() & (PARENT_RETRY_SIMPLE | PARENT_RETRY_UNAVAILABLE_SERVER)) {
      simple_or_unavailable_server_retry(s);
    }
  }

  s->parent_info.state = s->current.state;
  switch (s->current.state) {
  case CONNECTION_ALIVE:
    TxnDebug("http_trans", "[hrfp] connection alive");
    s->current.server->connect_result = 0;
    SET_VIA_STRING(VIA_DETAIL_PP_CONNECT, VIA_DETAIL_PP_SUCCESS);
    if (s->parent_result.retry) {
      s->parent_params->markParentUp(&s->parent_result);
    }
    handle_forward_server_connection_open(s);
    break;
  default: {
    LookingUp_t next_lookup = UNDEFINED_LOOKUP;

    if (s->current.state == PARENT_RETRY) {
      if (s->current.retry_type == PARENT_RETRY_SIMPLE) {
        if (s->current.simple_retry_attempts >= s->parent_result.max_retries(PARENT_RETRY_SIMPLE)) {
          TxnDebug("http_trans", "PARENT_RETRY_SIMPLE: retried all parents, send error to client.");
          s->current.retry_type = PARENT_RETRY_NONE;
        } else {
          s->current.simple_retry_attempts++;
          TxnDebug("http_trans", "PARENT_RETRY_SIMPLE: try another parent.");
          s->current.retry_type = PARENT_RETRY_NONE;
          next_lookup           = find_server_and_update_current_info(s);
        }
      } else if (s->current.retry_type == PARENT_RETRY_UNAVAILABLE_SERVER) {
        if (s->current.unavailable_server_retry_attempts >= s->parent_result.max_retries(PARENT_RETRY_UNAVAILABLE_SERVER)) {
          TxnDebug("http_trans", "PARENT_RETRY_UNAVAILABLE_SERVER: retried all parents, send error to client.");
          s->current.retry_type = PARENT_RETRY_NONE;
        } else {
          s->current.unavailable_server_retry_attempts++;
          TxnDebug("http_trans", "PARENT_RETRY_UNAVAILABLE_SERVER: marking parent down and trying another.");
          s->current.retry_type = PARENT_RETRY_NONE;
          HTTP_INCREMENT_DYN_STAT(http_total_parent_marked_down_count);
          s->parent_params->markParentDown(&s->parent_result, s->txn_conf->parent_fail_threshold, s->txn_conf->parent_retry_time);
          next_lookup = find_server_and_update_current_info(s);
        }
      }
    } else {
      TxnDebug("http_trans", "[hrfp] connection not alive");
      SET_VIA_STRING(VIA_DETAIL_PP_CONNECT, VIA_DETAIL_PP_FAILURE);

      ink_assert(s->hdr_info.server_request.valid());

      s->current.server->connect_result = ENOTCONN;
      // only mark the parent down in hostdb if the configuration allows it,
      // see proxy.config.http.parent_proxy.mark_down_hostdb in records.config.
      if (s->txn_conf->parent_failures_update_hostdb) {
        s->state_machine->do_hostdb_update_if_necessary();
      }

      char addrbuf[INET6_ADDRSTRLEN];
      TxnDebug("http_trans", "[%d] failed to connect to parent %s", s->current.attempts,
               ats_ip_ntop(&s->current.server->dst_addr.sa, addrbuf, sizeof(addrbuf)));

      // If the request is not retryable, just give up!
      if (!is_request_retryable(s)) {
        HTTP_INCREMENT_DYN_STAT(http_total_parent_marked_down_count);
        s->parent_params->markParentDown(&s->parent_result, s->txn_conf->parent_fail_threshold, s->txn_conf->parent_retry_time);
        s->parent_result.result = PARENT_FAIL;
        handle_parent_died(s);
        return;
      }

      if (s->current.attempts < s->txn_conf->parent_connect_attempts) {
        HTTP_INCREMENT_DYN_STAT(http_total_parent_retries_stat);
        s->current.attempts++;

        // Are we done with this particular parent?
        if ((s->current.attempts - 1) % s->txn_conf->per_parent_connect_attempts != 0) {
          // No we are not done with this parent so retry
          HTTP_INCREMENT_DYN_STAT(http_total_parent_switches_stat);
          s->next_action = how_to_open_connection(s);
          TxnDebug("http_trans", "%s Retrying parent for attempt %d, max %" PRId64, "[handle_response_from_parent]",
                   s->current.attempts, s->txn_conf->per_parent_connect_attempts);
          return;
        } else {
          TxnDebug("http_trans", "%s %d per parent attempts exhausted", "[handle_response_from_parent]", s->current.attempts);
          HTTP_INCREMENT_DYN_STAT(http_total_parent_retries_exhausted_stat);

          // Only mark the parent down if we failed to connect
          //  to the parent otherwise slow origin servers cause
          //  us to mark the parent down
          if (s->current.state == CONNECTION_ERROR) {
            HTTP_INCREMENT_DYN_STAT(http_total_parent_marked_down_count);
            s->parent_params->markParentDown(&s->parent_result, s->txn_conf->parent_fail_threshold, s->txn_conf->parent_retry_time);
          }
          // We are done so look for another parent if any
          next_lookup = find_server_and_update_current_info(s);
        }
      } else {
        // Done trying parents... fail over to origin server if that is
        //   appropriate
        HTTP_INCREMENT_DYN_STAT(http_total_parent_retries_exhausted_stat);
        TxnDebug("http_trans", "[handle_response_from_parent] Error. No more retries.");
        if (s->current.state == CONNECTION_ERROR) {
          HTTP_INCREMENT_DYN_STAT(http_total_parent_marked_down_count);
          s->parent_params->markParentDown(&s->parent_result, s->txn_conf->parent_fail_threshold, s->txn_conf->parent_retry_time);
        }
        s->parent_result.result = PARENT_FAIL;
        next_lookup             = find_server_and_update_current_info(s);
      }
    }

    // We have either tried to find a new parent or failed over to the
    //   origin server
    switch (next_lookup) {
    case PARENT_PROXY:
      ink_assert(s->current.request_to == PARENT_PROXY);
      TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, PPDNSLookup);
      break;
    case ORIGIN_SERVER:
      // Next lookup is Origin Server, try DNS for Origin Server
      TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, OSDNSLookup);
      break;
    case HOST_NONE:
      handle_parent_died(s);
      break;
    default:
      // This handles:
      // UNDEFINED_LOOKUP
      // INCOMING_ROUTER
      break;
    }

    break;
  }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : handle_response_from_server
// Description: response is from the origin server
//
// Details    :
//
//   response from the origin server. one of three things can happen now.
//   if the response is bad, then we can either retry (by first downgrading
//   the request, maybe making it non-keepalive, etc.), or we can give up.
//   the latter case is handled by handle_server_connection_not_open and
//   sends an error response back to the client. if the response is good
//   handle_forward_server_connection_open is called.
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_response_from_server(State *s)
{
  TxnDebug("http_trans", "[handle_response_from_server] (hrfs)");
  HTTP_RELEASE_ASSERT(s->current.server == &s->server_info);
  unsigned max_connect_retries = 0;

  // plugin call
  s->server_info.state = s->current.state;
  if (s->fp_tsremap_os_response) {
    s->fp_tsremap_os_response(s->remap_plugin_instance, reinterpret_cast<TSHttpTxn>(s->state_machine), s->current.state);
  }

  switch (s->current.state) {
  case CONNECTION_ALIVE:
    TxnDebug("http_trans", "[hrfs] connection alive");
    SET_VIA_STRING(VIA_DETAIL_SERVER_CONNECT, VIA_DETAIL_SERVER_SUCCESS);
    s->current.server->clear_connect_fail();
    handle_forward_server_connection_open(s);
    break;
  case OPEN_RAW_ERROR:
  /* fall through */
  case CONNECTION_ERROR:
  /* fall through */
  case STATE_UNDEFINED:
  /* fall through */
  case INACTIVE_TIMEOUT:
  /* fall through */
  case PARSE_ERROR:
  /* fall through */
  case CONNECTION_CLOSED:
  /* fall through */
  case BAD_INCOMING_RESPONSE:
    // Set to generic I/O error if not already set specifically.
    if (!s->current.server->had_connect_fail()) {
      s->current.server->set_connect_fail(EIO);
    }

    if (is_server_negative_cached(s)) {
      max_connect_retries = s->txn_conf->connect_attempts_max_retries_dead_server;
    } else {
      // server not yet negative cached - use default number of retries
      max_connect_retries = s->txn_conf->connect_attempts_max_retries;
    }

    if (is_request_retryable(s) && s->current.attempts < max_connect_retries) {
      // If this is a round robin DNS entry & we're tried configured
      //    number of times, we should try another node
      if (DNSLookupInfo::OS_Addr::OS_ADDR_TRY_CLIENT == s->dns_info.os_addr_style) {
        // attempt was based on client supplied server address. Try again
        // using HostDB.
        // Allow DNS attempt
        s->dns_info.lookup_success = false;
        // See if we can get data from HostDB for this.
        s->dns_info.os_addr_style = DNSLookupInfo::OS_Addr::OS_ADDR_TRY_HOSTDB;
        // Force host resolution to have the same family as the client.
        // Because this is a transparent connection, we can't switch address
        // families - that is locked in by the client source address.
        s->state_machine->ua_txn->set_host_res_style(ats_host_res_match(&s->current.server->dst_addr.sa));
        TRANSACT_RETURN(SM_ACTION_DNS_LOOKUP, OSDNSLookup);
      } else if ((s->dns_info.srv_lookup_success || s->host_db_info.is_rr_elt()) &&
                 (s->txn_conf->connect_attempts_rr_retries > 0) &&
                 (s->current.attempts % s->txn_conf->connect_attempts_rr_retries == 0)) {
        delete_server_rr_entry(s, max_connect_retries);
        return;
      } else {
        retry_server_connection_not_open(s, s->current.state, max_connect_retries);
        TxnDebug("http_trans", "[handle_response_from_server] Error. Retrying...");
        s->next_action = how_to_open_connection(s);

        if (s->api_server_addr_set) {
          // If the plugin set a server address, back up to the OS_DNS hook
          // to let it try another one. Force OS_ADDR_USE_CLIENT so that
          // in OSDNSLoopkup, we back up to how_to_open_connections which
          // will tell HttpSM to connect the origin server.

          s->dns_info.os_addr_style = DNSLookupInfo::OS_Addr::OS_ADDR_USE_CLIENT;
          TRANSACT_RETURN(SM_ACTION_API_OS_DNS, OSDNSLookup);
        }
        return;
      }
    } else {
      TxnDebug("http_trans", "[handle_response_from_server] Error. No more retries.");
      SET_VIA_STRING(VIA_DETAIL_SERVER_CONNECT, VIA_DETAIL_SERVER_FAILURE);
      handle_server_connection_not_open(s);
    }
    break;
  case ACTIVE_TIMEOUT:
    TxnDebug("http_trans", "[hrfs] connection not alive");
    SET_VIA_STRING(VIA_DETAIL_SERVER_CONNECT, VIA_DETAIL_SERVER_FAILURE);
    s->current.server->set_connect_fail(ETIMEDOUT);
    handle_server_connection_not_open(s);
    break;
  default:
    ink_assert(!("s->current.state is set to something unsupported"));
    break;
  }

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : delete_server_rr_entry
// Description:
//
// Details    :
//
//   connection to server failed mark down the server round robin entry
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::delete_server_rr_entry(State *s, int max_retries)
{
  char addrbuf[INET6_ADDRSTRLEN];

  TxnDebug("http_trans", "[%d] failed to connect to %s", s->current.attempts,
           ats_ip_ntop(&s->current.server->dst_addr.sa, addrbuf, sizeof(addrbuf)));
  TxnDebug("http_trans", "[delete_server_rr_entry] marking rr entry "
                         "down and finding next one");
  ink_assert(s->current.server->had_connect_fail());
  ink_assert(s->current.request_to == ORIGIN_SERVER);
  ink_assert(s->current.server == &s->server_info);
  update_dns_info(&s->dns_info, &s->current, 0, &s->arena);
  s->current.attempts++;
  TxnDebug("http_trans", "[delete_server_rr_entry] attempts now: %d, max: %d", s->current.attempts, max_retries);
  TRANSACT_RETURN(SM_ACTION_ORIGIN_SERVER_RR_MARK_DOWN, ReDNSRoundRobin);
}

///////////////////////////////////////////////////////////////////////////////
// Name       : retry_server_connection_not_open
// Description:
//
// Details    :
//
//   connection to server failed. retry.
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::retry_server_connection_not_open(State *s, ServerState_t conn_state, unsigned max_retries)
{
  ink_assert(s->current.state != CONNECTION_ALIVE);
  ink_assert(s->current.state != ACTIVE_TIMEOUT);
  ink_assert(s->current.attempts <= max_retries);
  ink_assert(s->current.server->had_connect_fail());
  char addrbuf[INET6_ADDRSTRLEN];

  char *url_string = s->hdr_info.client_request.url_string_get(&s->arena);

  TxnDebug("http_trans", "[%d] failed to connect [%d] to %s", s->current.attempts, conn_state,
           ats_ip_ntop(&s->current.server->dst_addr.sa, addrbuf, sizeof(addrbuf)));

  //////////////////////////////////////////
  // on the first connect attempt failure //
  // record the failue                   //
  //////////////////////////////////////////
  if (0 == s->current.attempts) {
    Log::error("CONNECT:[%d] could not connect [%s] to %s for '%s'", s->current.attempts,
               HttpDebugNames::get_server_state_name(conn_state),
               ats_ip_ntop(&s->current.server->dst_addr.sa, addrbuf, sizeof(addrbuf)), url_string ? url_string : "<none>");
  }

  if (url_string) {
    s->arena.str_free(url_string);
  }
  //////////////////////////////////////////////
  // disable keep-alive for request and retry //
  //////////////////////////////////////////////
  s->current.server->keep_alive = HTTP_NO_KEEPALIVE;
  s->current.attempts++;

  TxnDebug("http_trans", "[retry_server_connection_not_open] attempts now: %d, max: %d", s->current.attempts, max_retries);

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : handle_server_connection_not_open
// Description:
//
// Details    :
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_server_connection_not_open(State *s)
{
  bool serve_from_cache = false;

  TxnDebug("http_trans", "[handle_server_connection_not_open] (hscno)");
  TxnDebug("http_seq", "[HttpTransact::handle_server_connection_not_open] ");
  ink_assert(s->current.state != CONNECTION_ALIVE);

  SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_ERROR);
  HTTP_INCREMENT_DYN_STAT(http_broken_server_connections_stat);

  // Fire off a hostdb update to mark the server as down
  s->state_machine->do_hostdb_update_if_necessary();

  switch (s->cache_info.action) {
  case CACHE_DO_UPDATE:
    serve_from_cache = is_stale_cache_response_returnable(s);
    break;

  case CACHE_PREPARE_TO_DELETE:
  /* fall through */
  case CACHE_PREPARE_TO_UPDATE:
  /* fall through */
  case CACHE_PREPARE_TO_WRITE:
    ink_release_assert(!"Why still preparing for cache action - "
                        "we skipped a step somehow.");
    break;

  case CACHE_DO_LOOKUP:
  /* fall through */
  case CACHE_DO_SERVE:
    ink_assert(!("Why server response? Should have been a cache operation"));
    break;

  case CACHE_DO_DELETE:
  // decisions, decisions. what should we do here?
  // we could theoretically still delete the cached
  // copy or serve it back with a warning, or easier
  // just punt and biff the user. i say: biff the user.
  /* fall through */
  case CACHE_DO_UNDEFINED:
  /* fall through */
  case CACHE_DO_NO_ACTION:
  /* fall through */
  case CACHE_DO_WRITE:
  /* fall through */
  default:
    serve_from_cache = false;
    break;
  }

  if (serve_from_cache) {
    ink_assert(s->cache_info.object_read != nullptr);
    ink_assert(s->cache_info.action == CACHE_DO_UPDATE);
    ink_assert(s->internal_msg_buffer == nullptr);

    TxnDebug("http_trans", "[hscno] serving stale doc to client");
    build_response_from_cache(s, HTTP_WARNING_CODE_REVALIDATION_FAILED);
  } else {
    handle_server_died(s);
    s->next_action = SM_ACTION_SEND_ERROR_CACHE_NOOP;
  }

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : handle_forward_server_connection_open
// Description: connection to a forward server is open and good
//
// Details    :
//
//   "Forward server" includes the parent proxy
//   or the origin server. This function first determines if the forward
//   server uses HTTP 0.9, in which case it simply tunnels the response
//   to the client. Else, it updates
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_forward_server_connection_open(State *s)
{
  TxnDebug("http_trans", "[handle_forward_server_connection_open] (hfsco)");
  TxnDebug("http_seq", "[HttpTransact::handle_server_connection_open] ");
  ink_release_assert(s->current.state == CONNECTION_ALIVE);

  if (s->hdr_info.server_response.version_get() == HTTPVersion(0, 9)) {
    TxnDebug("http_trans", "[hfsco] server sent 0.9 response, reading...");
    build_response(s, &s->hdr_info.client_response, s->client_info.http_version, HTTP_STATUS_OK, "Connection Established");

    s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
    s->cache_info.action      = CACHE_DO_NO_ACTION;
    s->next_action            = SM_ACTION_SERVER_READ;
    return;

  } else if (s->hdr_info.server_response.version_get() == HTTPVersion(1, 0)) {
    if (s->current.server->http_version == HTTPVersion(0, 9)) {
      // update_hostdb_to_indicate_server_version_is_1_0
      s->updated_server_version = HostDBApplicationInfo::HTTP_VERSION_10;
    } else if (s->current.server->http_version == HTTPVersion(1, 1)) {
      // update_hostdb_to_indicate_server_version_is_1_0
      s->updated_server_version = HostDBApplicationInfo::HTTP_VERSION_10;
    } else {
      // dont update the hostdb. let us try again with what we currently think.
    }
  } else if (s->hdr_info.server_response.version_get() == HTTPVersion(1, 1)) {
    if (s->current.server->http_version == HTTPVersion(0, 9)) {
      // update_hostdb_to_indicate_server_version_is_1_1
      s->updated_server_version = HostDBApplicationInfo::HTTP_VERSION_11;
    } else if (s->current.server->http_version == HTTPVersion(1, 0)) {
      // update_hostdb_to_indicate_server_version_is_1_1
      s->updated_server_version = HostDBApplicationInfo::HTTP_VERSION_11;
    } else {
      // dont update the hostdb. let us try again with what we currently think.
    }
  } else {
    // dont update the hostdb. let us try again with what we currently think.
  }

  if (s->hdr_info.server_response.status_get() == HTTP_STATUS_CONTINUE ||
      s->hdr_info.server_response.status_get() == HTTP_STATUS_EARLY_HINTS) {
    handle_100_continue_response(s);
    return;
  }

  s->state_machine->do_hostdb_update_if_necessary();

  if (s->www_auth_content == CACHE_AUTH_FRESH) {
    // no update is needed - either to serve from cache if authorized,
    // or tunnnel the server response
    if (s->hdr_info.server_response.status_get() == HTTP_STATUS_OK) {
      // borrow a state variable used by the API function
      // this enable us to serve from cache without doing any updating
      s->api_server_response_ignore = true;
    }
    // s->cache_info.action = CACHE_PREPARE_TO_SERVE;
    // xing xing in the tunneling case, need to check when the cache_read_vc is closed, make sure the cache_read_vc is closed
    // right
    // away
  }

  CacheVConnection *cw_vc = s->state_machine->get_cache_sm().cache_write_vc;

  if (s->redirect_info.redirect_in_process && s->state_machine->enable_redirection) {
    if (s->cache_info.action == CACHE_DO_NO_ACTION) {
      switch (s->hdr_info.server_response.status_get()) {
      case HTTP_STATUS_MULTIPLE_CHOICES:   // 300
      case HTTP_STATUS_MOVED_PERMANENTLY:  // 301
      case HTTP_STATUS_MOVED_TEMPORARILY:  // 302
      case HTTP_STATUS_SEE_OTHER:          // 303
      case HTTP_STATUS_USE_PROXY:          // 305
      case HTTP_STATUS_TEMPORARY_REDIRECT: // 307
        break;
      default:
        TxnDebug("http_trans", "[hfsco] redirect in progress, non-3xx response, setting cache_do_write");
        if (cw_vc && s->txn_conf->cache_http) {
          s->cache_info.action = CACHE_DO_WRITE;
        }
        break;
      }
    }
  }

  switch (s->cache_info.action) {
  case CACHE_DO_WRITE:
  /* fall through */
  case CACHE_DO_UPDATE:
  /* fall through */
  case CACHE_DO_DELETE:
    TxnDebug("http_trans", "[hfsco] cache action: %s", HttpDebugNames::get_cache_action_name(s->cache_info.action));
    handle_cache_operation_on_forward_server_response(s);
    break;
  case CACHE_PREPARE_TO_DELETE:
  /* fall through */
  case CACHE_PREPARE_TO_UPDATE:
  /* fall through */
  case CACHE_PREPARE_TO_WRITE:
    ink_release_assert(!"Why still preparing for cache action - we skipped a step somehow.");
    break;
  case CACHE_DO_LOOKUP:
  /* fall through */
  case CACHE_DO_SERVE:
    ink_assert(!("Why server response? Should have been a cache operation"));
    break;
  case CACHE_DO_UNDEFINED:
  /* fall through */
  case CACHE_DO_NO_ACTION:
  /* fall through */
  default:
    // Just tunnel?
    TxnDebug("http_trans", "[hfsco] cache action: %s", HttpDebugNames::get_cache_action_name(s->cache_info.action));
    handle_no_cache_operation_on_forward_server_response(s);
    break;
  }

  return;
}

// void HttpTransact::handle_100_continue_response(State* s)
//
//   We've received a 100 continue response.  Determine if
//     we should just swallow the response 100 or forward it
//     the client.  http-1.1-spec-rev-06 section 8.2.3
//
void
HttpTransact::handle_100_continue_response(State *s)
{
  bool forward_100 = false;

  HTTPVersion ver = s->hdr_info.client_request.version_get();
  if (ver == HTTPVersion(1, 1)) {
    forward_100 = true;
  } else if (ver == HTTPVersion(1, 0)) {
    if (s->hdr_info.client_request.value_get_int(MIME_FIELD_EXPECT, MIME_LEN_EXPECT) == 100) {
      forward_100 = true;
    }
  }

  if (forward_100) {
    // We just want to copy the server's response.  All
    //   the other build response functions insist on
    //   adding stuff
    build_response_copy(s, &s->hdr_info.server_response, &s->hdr_info.client_response, s->client_info.http_version);
    TRANSACT_RETURN(SM_ACTION_INTERNAL_100_RESPONSE, HandleResponse);
  } else {
    TRANSACT_RETURN(SM_ACTION_SERVER_PARSE_NEXT_HDR, HandleResponse);
  }
}

// void HttpTransact::build_response_copy
//
//   Build a response with minimal changes from the base response
//
void
HttpTransact::build_response_copy(State *s, HTTPHdr *base_response, HTTPHdr *outgoing_response, HTTPVersion outgoing_version)
{
  HttpTransactHeaders::copy_header_fields(base_response, outgoing_response, s->txn_conf->fwd_proxy_auth_to_parent, s->current.now);
  HttpTransactHeaders::convert_response(outgoing_version, outgoing_response); // http version conversion
  HttpTransactHeaders::add_server_header_to_response(s->txn_conf, outgoing_response);

  DUMP_HEADER("http_hdrs", outgoing_response, s->state_machine_id, "Proxy's Response");
}

//////////////////////////////////////////////////////////////////////////
//   IMS handling table                                                 //
//       OS = Origin Server                                             //
//       IMS = A GET request w/ an If-Modified-Since header             //
//       LMs = Last modified state returned by server                   //
//       D, D' are Last modified dates returned by the origin server    //
//          and are later used for IMS                                  //
//       D < D'                                                         //
//                                                                      //
//  +----------+-----------+----------+-----------+--------------+      //
//  | Client's | Cached    | Proxy's  |   Response to client     |      //
//  | Request  | State     | Request  +-----------+--------------+      //
//  |          |           |          | OS 200    |  OS 304      |      //
//  +==========+===========+==========+===========+==============+      //
//  |  GET     | Fresh     | N/A      |  N/A      |  N/A         |      //
//  +----------+-----------+----------+-----------+--------------+      //
//  |  GET     | Stale, D' | IMS  D'  | 200, new  | 200, cached  |      //
//  +----------+-----------+----------+-----------+--------------+      //
//  |  GET     | Stale, E  | INM  E   | 200, new  | 200, cached  |      //
//  +----------+-----------+----------+-----------+--------------+      //
//  |  INM E   | Stale, E  | INM  E   | 304       | 304          |      //
//  +----------+-----------+----------+-----------+--------------+      //
//  |  INM E + | Stale,    | INM E    | 200, new *| 304          |      //
//  |  IMS D'  | E + D'    | IMS D'   |           |              |      //
//  +----------+-----------+----------+-----------+--------------+      //
//  |  IMS D   | None      | GET      | 200, new *|  N/A         |      //
//  +----------+-----------+----------+-----------+--------------+      //
//  |  INM E   | None      | GET      | 200, new *|  N/A         |      //
//  +----------+-----------+----------+-----------+--------------+      //
//  |  IMS D   | Stale, D' | IMS D'   | 200, new  | Compare      |      //
//  |---------------------------------------------| LMs & D'     |      //
//  |  IMS D'  | Stale, D' | IMS D'   | 200, new  | If match, 304|      //
//  |---------------------------------------------| If no match, |      //
//  |  IMS D'  | Stale D   | IMS D    | 200, new *|  200, cached |      //
//  +------------------------------------------------------------+      //
//                                                                      //
//  Note: * indicates a case that could be optimized to return          //
//     304 to the client but currently is not                           //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Name       : handle_cache_operation_on_forward_server_response
// Description:
//
// Details    :
//
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_cache_operation_on_forward_server_response(State *s)
{
  TxnDebug("http_trans", "[handle_cache_operation_on_forward_server_response] (hcoofsr)");
  TxnDebug("http_seq", "[handle_cache_operation_on_forward_server_response]");

  HTTPHdr *base_response          = nullptr;
  HTTPStatus server_response_code = HTTP_STATUS_NONE;
  HTTPStatus client_response_code = HTTP_STATUS_NONE;
  const char *warn_text           = nullptr;
  bool cacheable                  = false;

  cacheable = is_response_cacheable(s, &s->hdr_info.client_request, &s->hdr_info.server_response);
  TxnDebug("http_trans", "[hcoofsr] response %s cacheable", cacheable ? "is" : "is not");

  // set the correct next action, cache action, response code, and base response

  server_response_code = s->hdr_info.server_response.status_get();
  switch (server_response_code) {
  case HTTP_STATUS_NOT_MODIFIED: // 304
    SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_NOT_MODIFIED);

    // determine the correct cache action, next state, and response
    // precondition: s->cache_info.action should be one of the following
    // CACHE_DO_DELETE, or CACHE_DO_UPDATE; otherwise, it's an error.
    if (s->api_server_response_ignore && s->cache_info.action == CACHE_DO_UPDATE) {
      s->api_server_response_ignore = false;
      ink_assert(s->cache_info.object_read);
      base_response        = s->cache_info.object_read->response_get();
      s->cache_info.action = CACHE_DO_SERVE;
      TxnDebug("http_trans", "[hcoofsr] not merging, cache action changed to: %s",
               HttpDebugNames::get_cache_action_name(s->cache_info.action));
      s->next_action       = SM_ACTION_SERVE_FROM_CACHE;
      client_response_code = base_response->status_get();
    } else if ((s->cache_info.action == CACHE_DO_DELETE) || ((s->cache_info.action == CACHE_DO_UPDATE) && !cacheable)) {
      if (is_request_conditional(&s->hdr_info.client_request)) {
        client_response_code = HttpTransactCache::match_response_to_request_conditionals(
          &s->hdr_info.client_request, s->cache_info.object_read->response_get(), s->response_received_time);
      } else {
        client_response_code = HTTP_STATUS_OK;
      }

      if (client_response_code != HTTP_STATUS_OK) {
        // we can just forward the not modified response
        // from the server and delete the cached copy
        base_response        = &s->hdr_info.server_response;
        client_response_code = base_response->status_get();
        s->cache_info.action = CACHE_DO_DELETE;
        s->next_action       = SM_ACTION_INTERNAL_CACHE_DELETE;
      } else {
        // We got screwed. The client did not send a conditional request,
        // but we had a cached copy which we revalidated. The server has
        // now told us to delete the cached copy and sent back a 304.
        // We need to send the cached copy to the client, then delete it.
        if (s->method == HTTP_WKSIDX_HEAD) {
          s->cache_info.action = CACHE_DO_DELETE;
          s->next_action       = SM_ACTION_SERVER_READ;
        } else {
          s->cache_info.action = CACHE_DO_SERVE_AND_DELETE;
          s->next_action       = SM_ACTION_SERVE_FROM_CACHE;
        }
        base_response        = s->cache_info.object_read->response_get();
        client_response_code = base_response->status_get();
      }

    } else if (s->cache_info.action == CACHE_DO_UPDATE && is_request_conditional(&s->hdr_info.server_request)) {
      // CACHE_DO_UPDATE and server response is cacheable
      if (is_request_conditional(&s->hdr_info.client_request)) {
        if (s->txn_conf->cache_when_to_revalidate != 4) {
          client_response_code = HttpTransactCache::match_response_to_request_conditionals(
            &s->hdr_info.client_request, s->cache_info.object_read->response_get(), s->response_received_time);
        } else {
          client_response_code = server_response_code;
        }
      } else {
        client_response_code = HTTP_STATUS_OK;
      }

      if (client_response_code != HTTP_STATUS_OK) {
        // delete the cached copy unless configured to always verify IMS
        if (s->txn_conf->cache_when_to_revalidate != 4) {
          s->cache_info.action = CACHE_DO_UPDATE;
          s->next_action       = SM_ACTION_INTERNAL_CACHE_UPDATE_HEADERS;
          /* base_response will be set after updating headers below */
        } else {
          s->cache_info.action = CACHE_DO_NO_ACTION;
          s->next_action       = SM_ACTION_INTERNAL_CACHE_NOOP;
          base_response        = &s->hdr_info.server_response;
        }
      } else {
        if (s->method == HTTP_WKSIDX_HEAD) {
          s->cache_info.action = CACHE_DO_UPDATE;
          s->next_action       = SM_ACTION_SERVER_READ;
        } else {
          if (s->hdr_info.client_request.presence(MIME_PRESENCE_RANGE)) {
            s->state_machine->do_range_setup_if_necessary();
            // Note that even if the Range request is not satisfiable, we
            // update and serve this cache. This will give a 200 response to
            // a bad client, but allows us to avoid pegging the origin (e.g. abuse).
          }
          s->cache_info.action = CACHE_DO_SERVE_AND_UPDATE;
          s->next_action       = SM_ACTION_SERVE_FROM_CACHE;
        }
        /* base_response will be set after updating headers below */
      }

    } else { // cache action != CACHE_DO_DELETE and != CACHE_DO_UPDATE

      // bogus response from server. deal by tunnelling to client.
      // server should not have sent back a 304 because our request
      // should not have been an conditional.
      TxnDebug("http_trans", "[hcoofsr] 304 for non-conditional request");
      s->cache_info.action = CACHE_DO_NO_ACTION;
      s->next_action       = SM_ACTION_INTERNAL_CACHE_NOOP;
      client_response_code = s->hdr_info.server_response.status_get();
      base_response        = &s->hdr_info.server_response;

      // since this is bad, insert warning header into client response
      // The only exception case is conditional client request,
      // cache miss, and client request being unlikely cacheable.
      // In this case, the server request is given the same
      // conditional headers as client request (see build_request()).
      // So an unexpected 304 might be received.
      // FIXME: check this case
      if (is_request_likely_cacheable(s, &s->hdr_info.client_request)) {
        warn_text = "Proxy received unexpected 304 response; "
                    "content may be stale";
      }
    }

    break;

  case HTTP_STATUS_HTTPVER_NOT_SUPPORTED: // 505
  {
    bool keep_alive = (s->current.server->keep_alive == HTTP_KEEPALIVE);

    s->next_action = how_to_open_connection(s);

    /* Downgrade the request level and retry */
    if (!HttpTransactHeaders::downgrade_request(&keep_alive, &s->hdr_info.server_request)) {
      build_error_response(s, HTTP_STATUS_HTTPVER_NOT_SUPPORTED, "HTTP Version Not Supported", "response#bad_version");
      s->next_action        = SM_ACTION_SEND_ERROR_CACHE_NOOP;
      s->already_downgraded = true;
    } else {
      if (!keep_alive) {
        /* START Hack */
        (s->hdr_info.server_request).field_delete(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION);
        /* END   Hack */
      }
      s->already_downgraded = true;
      s->next_action        = how_to_open_connection(s);
    }
  }
    return;

  default:
    TxnDebug("http_trans", "[hcoofsr] response code: %d", server_response_code);
    SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_SERVED);
    SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_SERVED);

    /* if we receive a 500, 502, 503 or 504 while revalidating
       a document, treat the response as a 304 and in effect revalidate the document for
       negative_revalidating_lifetime. (negative revalidating)
     */

    if ((server_response_code == HTTP_STATUS_INTERNAL_SERVER_ERROR || server_response_code == HTTP_STATUS_GATEWAY_TIMEOUT ||
         server_response_code == HTTP_STATUS_BAD_GATEWAY || server_response_code == HTTP_STATUS_SERVICE_UNAVAILABLE) &&
        s->cache_info.action == CACHE_DO_UPDATE && s->txn_conf->negative_revalidating_enabled &&
        is_stale_cache_response_returnable(s)) {
      TxnDebug("http_trans", "[hcoofsr] negative revalidating: revalidate stale object and serve from cache");

      s->cache_info.object_store.create();
      s->cache_info.object_store.request_set(&s->hdr_info.client_request);
      s->cache_info.object_store.response_set(s->cache_info.object_read->response_get());
      base_response   = s->cache_info.object_store.response_get();
      time_t exp_time = s->txn_conf->negative_revalidating_lifetime + ink_local_time();
      base_response->set_expires(exp_time);

      SET_VIA_STRING(VIA_CACHE_FILL_ACTION, VIA_CACHE_UPDATED);
      HTTP_INCREMENT_DYN_STAT(http_cache_updates_stat);

      // unset Cache-control: "need-revalidate-once" (if it's set)
      // This directive is used internally by T.S. to invalidate
      // documents so that an invalidated document needs to be
      // revalidated again.
      base_response->unset_cooked_cc_need_revalidate_once();

      if (is_request_conditional(&s->hdr_info.client_request) &&
          HttpTransactCache::match_response_to_request_conditionals(&s->hdr_info.client_request,
                                                                    s->cache_info.object_read->response_get(),
                                                                    s->response_received_time) == HTTP_STATUS_NOT_MODIFIED) {
        s->next_action       = SM_ACTION_INTERNAL_CACHE_UPDATE_HEADERS;
        client_response_code = HTTP_STATUS_NOT_MODIFIED;
      } else {
        if (s->method == HTTP_WKSIDX_HEAD) {
          s->cache_info.action = CACHE_DO_UPDATE;
          s->next_action       = SM_ACTION_INTERNAL_CACHE_NOOP;
        } else {
          s->cache_info.action = CACHE_DO_SERVE_AND_UPDATE;
          s->next_action       = SM_ACTION_SERVE_FROM_CACHE;
        }

        client_response_code = s->cache_info.object_read->response_get()->status_get();
      }

      ink_assert(base_response->valid());

      if (client_response_code == HTTP_STATUS_NOT_MODIFIED) {
        ink_assert(GET_VIA_STRING(VIA_CLIENT_REQUEST) != VIA_CLIENT_SIMPLE);
        SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_IMS);
        SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_NOT_MODIFIED);
      } else {
        SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_SERVED);
      }

      ink_assert(client_response_code != HTTP_STATUS_NONE);

      if (s->next_action == SM_ACTION_SERVE_FROM_CACHE && s->state_machine->do_transform_open()) {
        set_header_for_transform(s, base_response);
      } else {
        build_response(s, base_response, &s->hdr_info.client_response, s->client_info.http_version, client_response_code);
      }

      return;
    }

    s->next_action       = SM_ACTION_SERVER_READ;
    client_response_code = server_response_code;
    base_response        = &s->hdr_info.server_response;

    s->negative_caching = is_negative_caching_appropriate(s) && cacheable;

    // determine the correct cache action given the original cache action,
    // cacheability of server response, and request method
    // precondition: s->cache_info.action is one of the following
    // CACHE_DO_UPDATE, CACHE_DO_WRITE, or CACHE_DO_DELETE
    if (s->api_server_response_no_store) {
      s->cache_info.action = CACHE_DO_NO_ACTION;
    } else if (s->api_server_response_ignore && server_response_code == HTTP_STATUS_OK &&
               s->hdr_info.server_request.method_get_wksidx() == HTTP_WKSIDX_HEAD) {
      s->api_server_response_ignore = false;
      ink_assert(s->cache_info.object_read);
      base_response        = s->cache_info.object_read->response_get();
      s->cache_info.action = CACHE_DO_SERVE;
      TxnDebug("http_trans",
               "[hcoofsr] ignoring server response, "
               "cache action changed to: %s",
               HttpDebugNames::get_cache_action_name(s->cache_info.action));
      s->next_action       = SM_ACTION_SERVE_FROM_CACHE;
      client_response_code = base_response->status_get();
    } else if (s->cache_info.action == CACHE_DO_UPDATE) {
      if (s->www_auth_content == CACHE_AUTH_FRESH || s->api_server_response_ignore) {
        s->cache_info.action = CACHE_DO_NO_ACTION;
      } else if (s->www_auth_content == CACHE_AUTH_STALE && server_response_code == HTTP_STATUS_UNAUTHORIZED) {
        s->cache_info.action = CACHE_DO_NO_ACTION;
      } else if (!cacheable) {
        s->cache_info.action = CACHE_DO_DELETE;
      } else if (s->method == HTTP_WKSIDX_HEAD) {
        s->cache_info.action = CACHE_DO_DELETE;
      } else {
        ink_assert(s->cache_info.object_read != nullptr);
        s->cache_info.action = CACHE_DO_REPLACE;
      }

    } else if (s->cache_info.action == CACHE_DO_WRITE) {
      if (!cacheable && !s->negative_caching) {
        s->cache_info.action = CACHE_DO_NO_ACTION;
      } else if (s->method == HTTP_WKSIDX_HEAD) {
        s->cache_info.action = CACHE_DO_NO_ACTION;
      } else {
        s->cache_info.action = CACHE_DO_WRITE;
      }

    } else if (s->cache_info.action == CACHE_DO_DELETE) {
      // do nothing

    } else {
      ink_assert(!("cache action inconsistent with current state"));
    }
    // postcondition: s->cache_info.action is one of the following
    // CACHE_DO_REPLACE, CACHE_DO_WRITE, CACHE_DO_DELETE, or
    // CACHE_DO_NO_ACTION

    // Check see if we ought to serve the client a 304 based on
    //   it's IMS date.  We may gotten a 200 back from the origin
    //   server if our (the proxies's) cached copy was out of date
    //   but the client's wasn't.  However, if the response is
    //   not cacheable we ought not issue a 304 to the client so
    //   make sure we are writing the document to the cache if
    //   before issuing a 304
    if (s->cache_info.action == CACHE_DO_WRITE || s->cache_info.action == CACHE_DO_NO_ACTION ||
        s->cache_info.action == CACHE_DO_REPLACE) {
      if (s->negative_caching) {
        HTTPHdr *resp;
        s->cache_info.object_store.create();
        s->cache_info.object_store.request_set(&s->hdr_info.client_request);
        s->cache_info.object_store.response_set(&s->hdr_info.server_response);
        resp = s->cache_info.object_store.response_get();
        if (!resp->presence(MIME_PRESENCE_EXPIRES)) {
          time_t exp_time = s->txn_conf->negative_caching_lifetime + ink_local_time();

          resp->set_expires(exp_time);
        }
      } else if (is_request_conditional(&s->hdr_info.client_request) && server_response_code == HTTP_STATUS_OK) {
        client_response_code = HttpTransactCache::match_response_to_request_conditionals(
          &s->hdr_info.client_request, &s->hdr_info.server_response, s->response_received_time);

        TxnDebug("http_trans",
                 "[hcoofsr] conditional request, 200 "
                 "response, send back 304 if possible [crc=%d]",
                 client_response_code);
        if ((client_response_code == HTTP_STATUS_NOT_MODIFIED) || (client_response_code == HTTP_STATUS_PRECONDITION_FAILED)) {
          switch (s->cache_info.action) {
          case CACHE_DO_WRITE:
          case CACHE_DO_REPLACE:
            s->next_action = SM_ACTION_INTERNAL_CACHE_WRITE;
            break;
          case CACHE_DO_DELETE:
            s->next_action = SM_ACTION_INTERNAL_CACHE_DELETE;
            break;
          default:
            s->next_action = SM_ACTION_INTERNAL_CACHE_NOOP;
            break;
          }
        } else {
          SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_SERVER_REVALIDATED);
        }
      }
    } else if (s->negative_caching) {
      s->negative_caching = false;
    }

    break;
  }

  // update stat, set via string, etc

  switch (s->cache_info.action) {
  case CACHE_DO_SERVE_AND_DELETE:
  // fall through
  case CACHE_DO_DELETE:
    TxnDebug("http_trans", "[hcoofsr] delete cached copy");
    SET_VIA_STRING(VIA_CACHE_FILL_ACTION, VIA_CACHE_DELETED);
    HTTP_INCREMENT_DYN_STAT(http_cache_deletes_stat);
    break;
  case CACHE_DO_WRITE:
    TxnDebug("http_trans", "[hcoofsr] cache write");
    SET_VIA_STRING(VIA_CACHE_FILL_ACTION, VIA_CACHE_WRITTEN);
    HTTP_INCREMENT_DYN_STAT(http_cache_writes_stat);
    break;
  case CACHE_DO_SERVE_AND_UPDATE:
  // fall through
  case CACHE_DO_UPDATE:
  // fall through
  case CACHE_DO_REPLACE:
    TxnDebug("http_trans", "[hcoofsr] cache update/replace");
    SET_VIA_STRING(VIA_CACHE_FILL_ACTION, VIA_CACHE_UPDATED);
    HTTP_INCREMENT_DYN_STAT(http_cache_updates_stat);
    break;
  default:
    break;
  }

  if ((client_response_code == HTTP_STATUS_NOT_MODIFIED) && (s->cache_info.action != CACHE_DO_NO_ACTION)) {
    /* ink_assert(GET_VIA_STRING(VIA_CLIENT_REQUEST)
       != VIA_CLIENT_SIMPLE); */
    TxnDebug("http_trans", "[hcoofsr] Client request was conditional");
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_IMS);
    SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_NOT_MODIFIED);
  } else {
    SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_SERVED);
  }

  ink_assert(client_response_code != HTTP_STATUS_NONE);

  // The correct cache action, next action, and response code are set.
  // Do the real work below.

  // first update the cached object
  if ((s->cache_info.action == CACHE_DO_UPDATE) || (s->cache_info.action == CACHE_DO_SERVE_AND_UPDATE)) {
    TxnDebug("http_trans", "[hcoofsr] merge and update cached copy");
    merge_and_update_headers_for_cache_update(s);
    base_response = s->cache_info.object_store.response_get();
    // unset Cache-control: "need-revalidate-once" (if it's set)
    // This directive is used internally by T.S. to invalidate documents
    // so that an invalidated document needs to be revalidated again.
    base_response->unset_cooked_cc_need_revalidate_once();
    // unset warning revalidation failed header if it set
    // (potentially added by negative revalidating)
    delete_warning_value(base_response, HTTP_WARNING_CODE_REVALIDATION_FAILED);
  }
  ink_assert(base_response->valid());

  if ((s->cache_info.action == CACHE_DO_WRITE) || (s->cache_info.action == CACHE_DO_REPLACE)) {
    set_headers_for_cache_write(s, &s->cache_info.object_store, &s->hdr_info.server_request, &s->hdr_info.server_response);
  }
  // 304, 412, and 416 responses are handled here
  if ((client_response_code == HTTP_STATUS_NOT_MODIFIED) || (client_response_code == HTTP_STATUS_PRECONDITION_FAILED)) {
    // Because we are decoupling User-Agent validation from
    //  Traffic Server validation just build a regular 304
    //  if the exception of adding prepending the VIA
    //  header to show the revalidation path
    build_response(s, base_response, &s->hdr_info.client_response, s->client_info.http_version, client_response_code);

    // Copy over the response via field (if any) preserving
    //  the order of the fields
    MIMEField *resp_via = s->hdr_info.server_response.field_find(MIME_FIELD_VIA, MIME_LEN_VIA);
    if (resp_via) {
      MIMEField *our_via;
      our_via = s->hdr_info.client_response.field_find(MIME_FIELD_VIA, MIME_LEN_VIA);
      if (our_via == nullptr) {
        our_via = s->hdr_info.client_response.field_create(MIME_FIELD_VIA, MIME_LEN_VIA);
        s->hdr_info.client_response.field_attach(our_via);
      }
      // HDR FIX ME - Mulitple appends are VERY slow
      while (resp_via) {
        int clen;
        const char *cfield = resp_via->value_get(&clen);
        s->hdr_info.client_response.field_value_append(our_via, cfield, clen, true);
        resp_via = resp_via->m_next_dup;
      }
    }
    // a warning text is added only in the case of a NOT MODIFIED response
    if (warn_text) {
      HttpTransactHeaders::insert_warning_header(s->http_config_param, &s->hdr_info.client_response, HTTP_WARNING_CODE_MISC_WARNING,
                                                 warn_text, strlen(warn_text));
    }

    DUMP_HEADER("http_hdrs", &s->hdr_info.client_response, s->state_machine_id, "Proxy's Response (Client Conditionals)");
    return;
  }
  // all other responses (not 304, 412, 416) are handled here
  else {
    if (((s->next_action == SM_ACTION_SERVE_FROM_CACHE) || (s->next_action == SM_ACTION_SERVER_READ)) &&
        s->state_machine->do_transform_open()) {
      set_header_for_transform(s, base_response);
    } else {
      build_response(s, base_response, &s->hdr_info.client_response, s->client_info.http_version, client_response_code);
    }
  }

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : handle_no_cache_operation_on_forward_server_response
// Description:
//
// Details    :
//
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_no_cache_operation_on_forward_server_response(State *s)
{
  TxnDebug("http_trans", "[handle_no_cache_operation_on_forward_server_response] (hncoofsr)");
  TxnDebug("http_seq", "[handle_no_cache_operation_on_forward_server_response]");

  bool keep_alive       = s->current.server->keep_alive == HTTP_KEEPALIVE;
  const char *warn_text = nullptr;

  switch (s->hdr_info.server_response.status_get()) {
  case HTTP_STATUS_OK:
    TxnDebug("http_trans", "[hncoofsr] server sent back 200");
    SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_SERVED);
    SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_SERVED);
    if (s->method == HTTP_WKSIDX_CONNECT) {
      TxnDebug("http_trans", "[hncoofsr] next action is SSL_TUNNEL");
      s->next_action = SM_ACTION_SSL_TUNNEL;
    } else {
      TxnDebug("http_trans", "[hncoofsr] next action will be OS_READ_CACHE_NOOP");

      ink_assert(s->cache_info.action == CACHE_DO_NO_ACTION);
      s->next_action = SM_ACTION_SERVER_READ;
    }
    if (s->state_machine->redirect_url == nullptr) {
      s->state_machine->enable_redirection = false;
    }
    break;
  case HTTP_STATUS_NOT_MODIFIED:
    TxnDebug("http_trans", "[hncoofsr] server sent back 304. IMS from client?");
    SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_NOT_MODIFIED);
    SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_NOT_MODIFIED);

    if (!is_request_conditional(&s->hdr_info.client_request)) {
      // bogus server response. not a conditional request
      // from the client and probably not a conditional
      // request from the proxy.

      // since this is bad, insert warning header into client response
      warn_text = "Proxy received unexpected 304 response; content may be stale";
    }

    ink_assert(s->cache_info.action == CACHE_DO_NO_ACTION);
    s->next_action = SM_ACTION_INTERNAL_CACHE_NOOP;
    break;
  case HTTP_STATUS_HTTPVER_NOT_SUPPORTED:
    s->next_action = how_to_open_connection(s);

    /* Downgrade the request level and retry */
    if (!HttpTransactHeaders::downgrade_request(&keep_alive, &s->hdr_info.server_request)) {
      s->already_downgraded = true;
      build_error_response(s, HTTP_STATUS_HTTPVER_NOT_SUPPORTED, "HTTP Version Not Supported", "response#bad_version");
      s->next_action = SM_ACTION_SEND_ERROR_CACHE_NOOP;
    } else {
      s->already_downgraded = true;
      s->next_action        = how_to_open_connection(s);
    }
    return;
  case HTTP_STATUS_PARTIAL_CONTENT:
    // If we get this back we should be just passing it through.
    ink_assert(s->cache_info.action == CACHE_DO_NO_ACTION);
    s->next_action = SM_ACTION_SERVER_READ;
    break;
  default:
    TxnDebug("http_trans", "[hncoofsr] server sent back something other than 100,304,200");
    /* Default behavior is to pass-through response to the client */

    ink_assert(s->cache_info.action == CACHE_DO_NO_ACTION);
    s->next_action = SM_ACTION_SERVER_READ;
    break;
  }

  HTTPHdr *to_warn;
  if (s->next_action == SM_ACTION_SERVER_READ && s->state_machine->do_transform_open()) {
    set_header_for_transform(s, &s->hdr_info.server_response);
    to_warn = &s->hdr_info.transform_response;
  } else {
    build_response(s, &s->hdr_info.server_response, &s->hdr_info.client_response, s->client_info.http_version);
    to_warn = &s->hdr_info.server_response;
  }

  if (warn_text) {
    HttpTransactHeaders::insert_warning_header(s->http_config_param, to_warn, HTTP_WARNING_CODE_MISC_WARNING, warn_text,
                                               strlen(warn_text));
  }

  return;
}

void
HttpTransact::merge_and_update_headers_for_cache_update(State *s)
{
  URL *s_url          = nullptr;
  HTTPHdr *cached_hdr = nullptr;

  if (!s->cache_info.object_store.valid()) {
    s->cache_info.object_store.create();
  }

  s->cache_info.object_store.request_set(&s->hdr_info.server_request);
  cached_hdr = s->cache_info.object_store.response_get();

  if (s->redirect_info.redirect_in_process) {
    s_url = &s->redirect_info.original_url;
  } else {
    s_url = &s->cache_info.original_url;
  }
  ink_assert(s_url != nullptr);

  s->cache_info.object_store.request_get()->url_set(s_url->valid() ? s_url : s->hdr_info.client_request.url_get());

  if (s->cache_info.object_store.request_get()->method_get_wksidx() == HTTP_WKSIDX_HEAD) {
    s->cache_info.object_store.request_get()->method_set(HTTP_METHOD_GET, HTTP_LEN_GET);
  }

  if (s->api_modifiable_cached_resp) {
    ink_assert(cached_hdr != nullptr && cached_hdr->valid());
    s->api_modifiable_cached_resp = false;
  } else {
    s->cache_info.object_store.response_set(s->cache_info.object_read->response_get());
  }

  // Delete caching headers from the cached response. If these are
  // still being served by the origin we will copy new versions in
  // from the server response. RFC 2616 says that a 304 response may
  // omit some headers if they were sent in a 200 response (see section
  // 10.3.5), but RFC 7232) is clear that the 304 and 200 responses
  // must be identical (see section 4.1). This code attempts to strike
  // a balance between the two.
  cached_hdr->field_delete(MIME_FIELD_AGE, MIME_LEN_AGE);
  cached_hdr->field_delete(MIME_FIELD_ETAG, MIME_LEN_ETAG);
  cached_hdr->field_delete(MIME_FIELD_EXPIRES, MIME_LEN_EXPIRES);

  merge_response_header_with_cached_header(cached_hdr, &s->hdr_info.server_response);

  // Some special processing for 304
  if (s->hdr_info.server_response.status_get() == HTTP_STATUS_NOT_MODIFIED) {
    // Hack fix. If the server sends back
    // a 304 without a Date Header, use the current time
    // as the new Date value in the header to be cached.
    time_t date_value = s->hdr_info.server_response.get_date();

    if (date_value <= 0) {
      cached_hdr->set_date(s->request_sent_time);
      date_value = s->request_sent_time;
    }

    // If the cached response has an Age: we should update it
    // We could use calculate_document_age but my guess is it's overkill
    // Just use 'now' - 304's Date: + Age: (response's Age: if there)
    date_value = std::max(s->current.now - date_value, (ink_time_t)0);
    if (s->hdr_info.server_response.presence(MIME_PRESENCE_AGE)) {
      time_t new_age = s->hdr_info.server_response.get_age();

      if (new_age >= 0) {
        cached_hdr->set_age(date_value + new_age);
      } else {
        cached_hdr->set_age(-1); // Overflow
      }
    }

    delete_warning_value(cached_hdr, HTTP_WARNING_CODE_REVALIDATION_FAILED);
  }

  s->cache_info.object_store.request_get()->field_delete(MIME_FIELD_VIA, MIME_LEN_VIA);
}

void
HttpTransact::handle_transform_cache_write(State *s)
{
  ink_assert(s->cache_info.transform_action == CACHE_PREPARE_TO_WRITE);

  switch (s->cache_info.write_lock_state) {
  case CACHE_WL_SUCCESS:
    // We were able to get the lock for the URL vector in the cache
    s->cache_info.transform_action = CACHE_DO_WRITE;
    break;
  case CACHE_WL_FAIL:
    // No write lock, ignore the cache
    s->cache_info.transform_action       = CACHE_DO_NO_ACTION;
    s->cache_info.transform_write_status = CACHE_WRITE_LOCK_MISS;
    break;
  default:
    ink_release_assert(0);
  }

  TRANSACT_RETURN(SM_ACTION_TRANSFORM_READ, nullptr);
}

void
HttpTransact::handle_transform_ready(State *s)
{
  ink_assert(s->hdr_info.transform_response.valid() == true);

  s->pre_transform_source = s->source;
  s->source               = SOURCE_TRANSFORM;

  DUMP_HEADER("http_hdrs", &s->hdr_info.transform_response, s->state_machine_id, "Header From Transform");

  build_response(s, &s->hdr_info.transform_response, &s->hdr_info.client_response, s->client_info.http_version);

  if (s->cache_info.action != CACHE_DO_NO_ACTION && s->cache_info.action != CACHE_DO_DELETE && s->api_info.cache_transformed &&
      !s->range_setup) {
    HTTPHdr *transform_store_request = nullptr;
    switch (s->pre_transform_source) {
    case SOURCE_CACHE:
      // If we are transforming from the cache, treat
      //  the transform as if it were virtual server
      //  use in the incoming request
      transform_store_request = &s->hdr_info.client_request;
      break;
    case SOURCE_HTTP_ORIGIN_SERVER:
      transform_store_request = &s->hdr_info.server_request;
      break;
    default:
      ink_release_assert(0);
    }
    ink_assert(transform_store_request->valid() == true);
    set_headers_for_cache_write(s, &s->cache_info.transform_store, transform_store_request, &s->hdr_info.transform_response);

    // For debugging
    if (is_action_tag_set("http_nullt")) {
      s->cache_info.transform_store.request_get()->value_set("InkXform", 8, "nullt", 5);
      s->cache_info.transform_store.response_get()->value_set("InkXform", 8, "nullt", 5);
    }

    s->cache_info.transform_action = CACHE_PREPARE_TO_WRITE;
    TRANSACT_RETURN(SM_ACTION_CACHE_ISSUE_WRITE_TRANSFORM, handle_transform_cache_write);
  } else {
    s->cache_info.transform_action = CACHE_DO_NO_ACTION;
    TRANSACT_RETURN(SM_ACTION_TRANSFORM_READ, nullptr);
  }
}

void
HttpTransact::set_header_for_transform(State *s, HTTPHdr *base_header)
{
  s->hdr_info.transform_response.create(HTTP_TYPE_RESPONSE);
  s->hdr_info.transform_response.copy(base_header);

  // Nuke the content length since 1) the transform will probably
  //   change it.  2) it would only be valid for the first transform
  //   in the chain
  s->hdr_info.transform_response.field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);

  DUMP_HEADER("http_hdrs", &s->hdr_info.transform_response, s->state_machine_id, "Header To Transform");
}

void
HttpTransact::set_headers_for_cache_write(State *s, HTTPInfo *cache_info, HTTPHdr *request, HTTPHdr *response)
{
  URL *temp_url;
  ink_assert(request->type_get() == HTTP_TYPE_REQUEST);
  ink_assert(response->type_get() == HTTP_TYPE_RESPONSE);

  if (!cache_info->valid()) {
    cache_info->create();
  }

  /* Store the requested URI */
  //  Nasty hack. The set calls for
  //  marshalled types current do handle something being
  //  set to itself.  Make the check here for that case.
  //  Why the request url is set before a copy made is
  //  quite beyond me.  Seems like a unsafe practice so
  //  FIX ME!

  // Logic added to restore the orignal URL for multiple cache lookup
  // and automatic redirection
  if (s->redirect_info.redirect_in_process) {
    temp_url = &s->redirect_info.original_url;
    ink_assert(temp_url->valid());
    request->url_set(temp_url);
  } else if ((temp_url = &(s->cache_info.original_url))->valid()) {
    request->url_set(temp_url);
  } else if (request != &s->hdr_info.client_request) {
    request->url_set(s->hdr_info.client_request.url_get());
  }
  cache_info->request_set(request);
  /* Why do we check the negative caching case? No one knows. This used to assert if the cache_info
     response wasn't already valid, which broke negative caching when a transform is active. Why it
     wasn't OK to pull in the @a response explicitly passed in isn't clear and looking at the call
     sites yields no insight. So the assert is removed and we keep the behavior that if the response
     in @a cache_info is already set, we don't override it.
  */
  if (!s->negative_caching || !cache_info->response_get()->valid()) {
    cache_info->response_set(response);
  }

  if (s->api_server_request_body_set) {
    cache_info->request_get()->method_set(HTTP_METHOD_GET, HTTP_LEN_GET);
  }

  // Set-Cookie should not be put in the cache to prevent
  //  sending person A's cookie to person B
  cache_info->response_get()->field_delete(MIME_FIELD_SET_COOKIE, MIME_LEN_SET_COOKIE);
  cache_info->request_get()->field_delete(MIME_FIELD_VIA, MIME_LEN_VIA);
  // server 200 Ok for Range request
  cache_info->request_get()->field_delete(MIME_FIELD_RANGE, MIME_LEN_RANGE);

  // If we're ignoring auth, then we don't want to cache WWW-Auth
  //  headers
  if (s->txn_conf->cache_ignore_auth) {
    cache_info->response_get()->field_delete(MIME_FIELD_WWW_AUTHENTICATE, MIME_LEN_WWW_AUTHENTICATE);
  }

  // if (s->cache_control.cache_auth_content && s->www_auth_content != CACHE_AUTH_NONE) {
  // decided to cache authenticated content because of cache.config
  // add one marker to the content in cache
  // cache_info->response_get()->value_set("@WWW-Auth", 9, "true", 4);
  //}
  DUMP_HEADER("http_hdrs", cache_info->request_get(), s->state_machine_id, "Cached Request Hdr");
}

void
HttpTransact::merge_response_header_with_cached_header(HTTPHdr *cached_header, HTTPHdr *response_header)
{
  MIMEField *field;
  MIMEField *new_field;
  MIMEFieldIter fiter;
  const char *name;
  bool dups_seen = false;

  field = response_header->iter_get_first(&fiter);

  for (; field != nullptr; field = response_header->iter_get_next(&fiter)) {
    int name_len;
    name = field->name_get(&name_len);

    ///////////////////////////
    // is hop-by-hop header? //
    ///////////////////////////
    if (HttpTransactHeaders::is_this_a_hop_by_hop_header(name)) {
      continue;
    }
    /////////////////////////////////////
    // dont cache content-length field //
    /////////////////////////////////////
    if (name == MIME_FIELD_CONTENT_LENGTH) {
      continue;
    }
    /////////////////////////////////////
    // dont cache Set-Cookie headers   //
    /////////////////////////////////////
    if (name == MIME_FIELD_SET_COOKIE) {
      continue;
    }
    /////////////////////////////////////////
    // dont overwrite the cached content   //
    //   type as this wreaks havoc with    //
    //   transformed content               //
    /////////////////////////////////////////
    if (name == MIME_FIELD_CONTENT_TYPE) {
      continue;
    }
    /////////////////////////////////////
    // dont delete warning.  a separate//
    //  functions merges the two in a  //
    //  complex manner                 //
    /////////////////////////////////////
    if (name == MIME_FIELD_WARNING) {
      continue;
    }
    // Copy all remaining headers with replacement

    // Duplicate header fields cause a bug problem
    //   since we need to duplicate with replacement.
    //   Without dups, we can just nuke what is already
    //   there in the cached header.  With dups, we
    //   can't do this because what is already there
    //   may be a dup we've already copied in.  If
    //   dups show up we look through the remaining
    //   header fields in the new reponse, nuke
    //   them in the cached response and then add in
    //   the remaining fields one by one from the
    //   response header
    //
    if (field->m_next_dup) {
      if (dups_seen == false) {
        MIMEField *dfield;
        // use a second iterator to delete the
        // remaining response headers in the cached response,
        // so that they will be added in the next iterations.
        MIMEFieldIter fiter2 = fiter;
        const char *dname    = name;
        int dlen             = name_len;

        while (dname) {
          cached_header->field_delete(dname, dlen);
          dfield = response_header->iter_get_next(&fiter2);
          if (dfield) {
            dname = dfield->name_get(&dlen);
          } else {
            dname = nullptr;
          }
        }
        dups_seen = true;
      }
    }

    int value_len;
    const char *value = field->value_get(&value_len);

    if (dups_seen == false) {
      cached_header->value_set(name, name_len, value, value_len);
    } else {
      new_field = cached_header->field_create(name, name_len);
      cached_header->field_attach(new_field);
      cached_header->field_value_set(new_field, value, value_len);
    }
  }

  merge_warning_header(cached_header, response_header);

  Debug("http_hdr_space", "Merged response header with %d dead bytes", cached_header->m_heap->m_lost_string_space);
}

void
HttpTransact::merge_warning_header(HTTPHdr *cached_header, HTTPHdr *response_header)
{
  //  The plan:
  //
  //    1) The cached header has it's warning codes untouched
  //         since merge_response_header_with_cached_header()
  //         doesn't deal with warning headers.
  //    2) If there are 1xx warning codes in the cached
  //         header, they need to be removed.  Removal
  //         is difficult since the hdrs don't comma
  //         separate values, so build up a new header
  //         piecemal.  Very slow but shouldn't happen
  //         very often
  //    3) Since we keep the all the warning codes from
  //         the response header, append if to
  //         the cached header
  //
  MIMEField *c_warn    = cached_header->field_find(MIME_FIELD_WARNING, MIME_LEN_WARNING);
  MIMEField *r_warn    = response_header->field_find(MIME_FIELD_WARNING, MIME_LEN_WARNING);
  MIMEField *new_cwarn = nullptr;
  int move_warn_len;
  const char *move_warn;

  // Loop over the cached warning header and transfer all non 1xx
  //   warning values to a new header
  if (c_warn) {
    HdrCsvIter csv;

    move_warn = csv.get_first(c_warn, &move_warn_len);
    while (move_warn) {
      int code = ink_atoi(move_warn, move_warn_len);
      if (code < 100 || code > 199) {
        bool first_move;
        if (!new_cwarn) {
          new_cwarn  = cached_header->field_create();
          first_move = true;
        } else {
          first_move = false;
        }
        cached_header->field_value_append(new_cwarn, move_warn, move_warn_len, !first_move);
      }

      move_warn = csv.get_next(&move_warn_len);
    }

    // At this point we can nuke the old warning headers
    cached_header->field_delete(MIME_FIELD_WARNING, MIME_LEN_WARNING);

    // Add in the new header if it has anything in it
    if (new_cwarn) {
      new_cwarn->name_set(cached_header->m_heap, cached_header->m_mime, MIME_FIELD_WARNING, MIME_LEN_WARNING);
      cached_header->field_attach(new_cwarn);
    }
  }
  // Loop over all the dups in the response warning header and append
  //  them one by one on to the cached warning header
  while (r_warn) {
    move_warn = r_warn->value_get(&move_warn_len);

    if (new_cwarn) {
      cached_header->field_value_append(new_cwarn, move_warn, move_warn_len, true);
    } else {
      new_cwarn = cached_header->field_create(MIME_FIELD_WARNING, MIME_LEN_WARNING);
      cached_header->field_attach(new_cwarn);
      cached_header->field_value_set(new_cwarn, move_warn, move_warn_len);
    }

    r_warn = r_warn->m_next_dup;
  }
}

////////////////////////////////////////////////////////
// Set the keep-alive and version flags for later use //
// in request construction                            //
// this is also used when opening a connection to     //
// the origin server, and search_keepalive_to().      //
////////////////////////////////////////////////////////
bool
HttpTransact::get_ka_info_from_config(State *s, ConnectionAttributes *server_info)
{
  bool check_hostdb = false;

  if (server_info->http_version > HTTPVersion(0, 9)) {
    TxnDebug("http_trans", "get_ka_info_from_config, version already set server_info->http_version %d",
             server_info->http_version.m_version);
    return false;
  }
  switch (s->txn_conf->send_http11_requests) {
  case HttpConfigParams::SEND_HTTP11_NEVER:
    server_info->http_version = HTTPVersion(1, 0);
    break;
  case HttpConfigParams::SEND_HTTP11_UPGRADE_HOSTDB:
    server_info->http_version = HTTPVersion(1, 0);
    check_hostdb              = true;
    break;
  case HttpConfigParams::SEND_HTTP11_IF_REQUEST_11_AND_HOSTDB:
    server_info->http_version = HTTPVersion(1, 0);
    if (s->hdr_info.client_request.version_get() == HTTPVersion(1, 1)) {
      // check hostdb only if client req is http/1.1
      check_hostdb = true;
    }
    break;
  default:
    // The default is the "1" config, SEND_HTTP11_ALWAYS, but assert in debug builds since we shouldn't be here
    ink_assert(0);
  // fallthrough
  case HttpConfigParams::SEND_HTTP11_ALWAYS:
    server_info->http_version = HTTPVersion(1, 1);
    break;
  }
  TxnDebug("http_trans", "get_ka_info_from_config, server_info->http_version %d, check_hostdb %d",
           server_info->http_version.m_version, check_hostdb);

  // Set keep_alive info based on the records.config setting
  server_info->keep_alive = s->txn_conf->keep_alive_enabled_out ? HTTP_KEEPALIVE : HTTP_NO_KEEPALIVE;

  return check_hostdb;
}

////////////////////////////////////////////////////////
// Set the keep-alive and version flags for later use //
// in request construction                            //
// this is also used when opening a connection to     //
// the origin server, and search_keepalive_to().      //
////////////////////////////////////////////////////////
void
HttpTransact::get_ka_info_from_host_db(State *s, ConnectionAttributes *server_info,
                                       ConnectionAttributes * /* client_info ATS_UNUSED */, HostDBInfo *host_db_info)
{
  bool force_http11     = false;
  bool http11_if_hostdb = false;

  switch (s->txn_conf->send_http11_requests) {
  case HttpConfigParams::SEND_HTTP11_NEVER:
    // No need to do anything since above vars
    //   are defaulted false
    break;
  case HttpConfigParams::SEND_HTTP11_UPGRADE_HOSTDB:
    http11_if_hostdb = true;
    break;
  case HttpConfigParams::SEND_HTTP11_IF_REQUEST_11_AND_HOSTDB:
    if (s->hdr_info.client_request.version_get() == HTTPVersion(1, 1)) {
      http11_if_hostdb = true;
    }
    break;
  default:
    // The default is the "1" config, SEND_HTTP11_ALWAYS, but assert in debug builds since we shouldn't be here
    ink_assert(0);
  // fallthrough
  case HttpConfigParams::SEND_HTTP11_ALWAYS:
    force_http11 = true;
    break;
  }

  if (force_http11 == true ||
      (http11_if_hostdb == true && host_db_info->app.http_data.http_version == HostDBApplicationInfo::HTTP_VERSION_11)) {
    server_info->http_version.set(1, 1);
    server_info->keep_alive = HTTP_KEEPALIVE;
  } else if (host_db_info->app.http_data.http_version == HostDBApplicationInfo::HTTP_VERSION_10) {
    server_info->http_version.set(1, 0);
    server_info->keep_alive = HTTP_KEEPALIVE;
  } else if (host_db_info->app.http_data.http_version == HostDBApplicationInfo::HTTP_VERSION_09) {
    server_info->http_version.set(0, 9);
    server_info->keep_alive = HTTP_NO_KEEPALIVE;
  } else {
    //////////////////////////////////////////////
    // not set yet for this host. set defaults. //
    //////////////////////////////////////////////
    server_info->http_version.set(1, 0);
    server_info->keep_alive                  = HTTP_KEEPALIVE;
    host_db_info->app.http_data.http_version = HostDBApplicationInfo::HTTP_VERSION_10;
  }

  /////////////////////////////
  // origin server keep_alive //
  /////////////////////////////
  if (!s->txn_conf->keep_alive_enabled_out) {
    server_info->keep_alive = HTTP_NO_KEEPALIVE;
  }

  return;
}

void
HttpTransact::add_client_ip_to_outgoing_request(State *s, HTTPHdr *request)
{
  char ip_string[INET6_ADDRSTRLEN + 1] = {'\0'};
  size_t ip_string_size                = 0;

  if (!ats_is_ip(&s->client_info.src_addr.sa)) {
    return;
  }

  // Always prepare the IP string.
  if (ats_ip_ntop(&s->client_info.src_addr.sa, ip_string, sizeof(ip_string)) != nullptr) {
    ip_string_size += strlen(ip_string);
  } else {
    // Failure, omg
    ip_string_size = 0;
    ip_string[0]   = 0;
  }

  // Check to see if the ip_string has been set
  if (ip_string_size == 0) {
    return;
  }

  // if we want client-ip headers, and there isn't one, add one
  if (!s->txn_conf->anonymize_remove_client_ip) {
    switch (s->txn_conf->anonymize_insert_client_ip) {
    case 1: { // Insert the client-ip, but only if the UA did not send one
      bool client_ip_set = request->presence(MIME_PRESENCE_CLIENT_IP);
      TxnDebug("http_trans", "client_ip_set = %d", client_ip_set);

      if (client_ip_set == true) {
        break;
      }
    }

    // FALL-THROUGH
    case 2: // Always insert the client-ip
      request->value_set(MIME_FIELD_CLIENT_IP, MIME_LEN_CLIENT_IP, ip_string, ip_string_size);
      TxnDebug("http_trans", "inserted request header 'Client-ip: %s'", ip_string);
      break;

    default: // don't insert client-ip
      break;
    }
  }

  // Add or append to the X-Forwarded-For header
  if (s->txn_conf->insert_squid_x_forwarded_for) {
    request->value_append_or_set(MIME_FIELD_X_FORWARDED_FOR, MIME_LEN_X_FORWARDED_FOR, ip_string, ip_string_size);
    TxnDebug("http_trans",
             "[add_client_ip_to_outgoing_request] Appended connecting client's "
             "(%s) to the X-Forwards header",
             ip_string);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : check_request_validity()
// Description: checks to see if incoming request has necessary fields
//
// Input      : State, header (can we do this without the state?)
// Output     : enum RequestError_t of the error type, if any
//
// Details    :
//
//
///////////////////////////////////////////////////////////////////////////////
HttpTransact::RequestError_t
HttpTransact::check_request_validity(State *s, HTTPHdr *incoming_hdr)
{
  if (incoming_hdr == nullptr) {
    return NON_EXISTANT_REQUEST_HEADER;
  }

  if (!(HttpTransactHeaders::is_request_proxy_authorized(incoming_hdr))) {
    return FAILED_PROXY_AUTHORIZATION;
  }

  URL *incoming_url = incoming_hdr->url_get();
  int hostname_len;
  const char *hostname = incoming_hdr->host_get(&hostname_len);

  if (hostname == nullptr) {
    return MISSING_HOST_FIELD;
  }

  if (hostname_len >= MAXDNAME || hostname_len <= 0 || memchr(hostname, '\0', hostname_len)) {
    return BAD_HTTP_HEADER_SYNTAX;
  }

  int scheme = incoming_url->scheme_get_wksidx();
  int method = incoming_hdr->method_get_wksidx();

  // Check for chunked encoding
  if (incoming_hdr->presence(MIME_PRESENCE_TRANSFER_ENCODING)) {
    MIMEField *field = incoming_hdr->field_find(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING);
    HdrCsvIter enc_val_iter;
    int enc_val_len;
    const char *enc_value = enc_val_iter.get_first(field, &enc_val_len);

    while (enc_value) {
      const char *wks_value = hdrtoken_string_to_wks(enc_value, enc_val_len);
      if (wks_value == HTTP_VALUE_CHUNKED) {
        s->client_info.transfer_encoding = CHUNKED_ENCODING;
        break;
      }
      enc_value = enc_val_iter.get_next(&enc_val_len);
    }
  }

  /////////////////////////////////////////////////////
  // get request content length                      //
  // To avoid parsing content-length twice, we set   //
  // s->hdr_info.request_content_length here rather  //
  // than in initialize_state_variables_from_request //
  /////////////////////////////////////////////////////
  if (method != HTTP_WKSIDX_TRACE) {
    int64_t length                     = incoming_hdr->get_content_length();
    s->hdr_info.request_content_length = (length >= 0) ? length : HTTP_UNDEFINED_CL; // content length less than zero is invalid

    TxnDebug("http_trans", "[init_stat_vars_from_req] set req cont length to %" PRId64, s->hdr_info.request_content_length);

  } else {
    s->hdr_info.request_content_length = 0;
  }

  if (!((scheme == URL_WKSIDX_HTTP) && (method == HTTP_WKSIDX_GET))) {
    if (scheme != URL_WKSIDX_HTTP && scheme != URL_WKSIDX_HTTPS && method != HTTP_WKSIDX_CONNECT &&
        !((scheme == URL_WKSIDX_WS || scheme == URL_WKSIDX_WSS) && s->is_websocket)) {
      if (scheme < 0) {
        return NO_REQUEST_SCHEME;
      } else {
        return SCHEME_NOT_SUPPORTED;
      }
    }

    if (!HttpTransactHeaders::is_this_method_supported(scheme, method)) {
      return METHOD_NOT_SUPPORTED;
    }
    if ((method == HTTP_WKSIDX_CONNECT) && !s->transparent_passthrough &&
        (!is_port_in_range(incoming_hdr->url_get()->port_get(), s->http_config_param->connect_ports))) {
      return BAD_CONNECT_PORT;
    }

    // Require Content-Length/Transfer-Encoding for POST/PUSH/PUT
    if ((scheme == URL_WKSIDX_HTTP || scheme == URL_WKSIDX_HTTPS) &&
        (method == HTTP_WKSIDX_POST || method == HTTP_WKSIDX_PUSH || method == HTTP_WKSIDX_PUT) &&
        s->client_info.transfer_encoding != CHUNKED_ENCODING) {
      if (!incoming_hdr->presence(MIME_PRESENCE_CONTENT_LENGTH)) {
        // In normal operation there will always be a ua_txn at this point, but in one of the -R1  regression tests a request is
        // created
        // independent of a transaction and this method is called, so we must null check
        if (s->txn_conf->post_check_content_length_enabled &&
            (!s->state_machine->ua_txn || s->state_machine->ua_txn->is_chunked_encoding_supported())) {
          return NO_POST_CONTENT_LENGTH;
        } else {
          // Stuff in a TE setting so we treat this as chunked, sort of.
          s->client_info.transfer_encoding = HttpTransact::CHUNKED_ENCODING;
          incoming_hdr->value_append(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING, HTTP_VALUE_CHUNKED, HTTP_LEN_CHUNKED,
                                     true);
        }
      }
      if (HTTP_UNDEFINED_CL == s->hdr_info.request_content_length) {
        return INVALID_POST_CONTENT_LENGTH;
      }
    }
  }
  // Check whether a Host header field is missing in the request.
  if (!incoming_hdr->presence(MIME_PRESENCE_HOST) && incoming_hdr->version_get() != HTTPVersion(0, 9)) {
    // Update the number of incoming 1.0 or 1.1 requests that do
    // not contain Host header fields.
    HTTP_INCREMENT_DYN_STAT(http_missing_host_hdr_stat);
  }
  // Did the client send a "TE: identity;q=0"? We have to respond
  // with an error message because we only support identity
  // Transfer Encoding.

  if (incoming_hdr->presence(MIME_PRESENCE_TE)) {
    MIMEField *te_field = incoming_hdr->field_find(MIME_FIELD_TE, MIME_LEN_TE);
    HTTPValTE *te_val;

    if (te_field) {
      HdrCsvIter csv_iter;
      int te_raw_len;
      const char *te_raw = csv_iter.get_first(te_field, &te_raw_len);

      while (te_raw) {
        te_val = http_parse_te(te_raw, te_raw_len, &s->arena);
        if (te_val->encoding == HTTP_VALUE_IDENTITY) {
          if (te_val->qvalue <= 0.0) {
            s->arena.free(te_val, sizeof(HTTPValTE));
            return UNACCEPTABLE_TE_REQUIRED;
          }
        }
        s->arena.free(te_val, sizeof(HTTPValTE));
        te_raw = csv_iter.get_next(&te_raw_len);
      }
    }
  }

  return NO_REQUEST_HEADER_ERROR;
}

HttpTransact::ResponseError_t
HttpTransact::check_response_validity(State *s, HTTPHdr *incoming_hdr)
{
  ink_assert(s->next_hop_scheme == URL_WKSIDX_HTTP || s->next_hop_scheme == URL_WKSIDX_HTTPS);

  if (incoming_hdr == nullptr) {
    return NON_EXISTANT_RESPONSE_HEADER;
  }

  if (incoming_hdr->type_get() != HTTP_TYPE_RESPONSE) {
    return NOT_A_RESPONSE_HEADER;
  }
  // If the response is 0.9 then there is no status
  //   code or date
  if (did_forward_server_send_0_9_response(s) == true) {
    return NO_RESPONSE_HEADER_ERROR;
  }

  HTTPStatus incoming_status = incoming_hdr->status_get();
  if (!incoming_status) {
    return MISSING_STATUS_CODE;
  }

  if (incoming_status == HTTP_STATUS_INTERNAL_SERVER_ERROR) {
    return STATUS_CODE_SERVER_ERROR;
  }

  if (!incoming_hdr->presence(MIME_PRESENCE_DATE)) {
    incoming_hdr->set_date(s->current.now);
  }
  //     if (! incoming_hdr->get_reason_phrase()) {
  //      return MISSING_REASON_PHRASE;
  //     }

#ifdef REALLY_NEED_TO_CHECK_DATE_VALIDITY

  if (incoming_hdr->presence(MIME_PRESENCE_DATE)) {
    time_t date_value = incoming_hdr->get_date();
    if (date_value <= 0) {
      // following lines commented out because of performance
      // concerns
      //          if (s->http_config_param->errors_log_error_pages) {
      //              const char *date_string =
      //                    incoming_hdr->value_get(MIME_FIELD_DATE);
      //              Log::error ("Incoming response has bogus date value: %d: %s",
      //                          date_value, date_string ? date_string : "(null)");
      //          }

      TxnDebug("http_trans", "[check_response_validity] Bogus date in response");
      return BOGUS_OR_NO_DATE_IN_RESPONSE;
    }
  } else {
    TxnDebug("http_trans", "[check_response_validity] No date in response");
    return BOGUS_OR_NO_DATE_IN_RESPONSE;
  }
#endif

  return NO_RESPONSE_HEADER_ERROR;
}

bool
HttpTransact::did_forward_server_send_0_9_response(State *s)
{
  if (s->hdr_info.server_response.version_get() == HTTPVersion(0, 9)) {
    s->current.server->http_version.set(0, 9);
    return true;
  }
  return false;
}

bool
HttpTransact::handle_internal_request(State * /* s ATS_UNUSED */, HTTPHdr *incoming_hdr)
{
  URL *url;

  ink_assert(incoming_hdr->type_get() == HTTP_TYPE_REQUEST);

  if (incoming_hdr->method_get_wksidx() != HTTP_WKSIDX_GET) {
    return false;
  }

  url = incoming_hdr->url_get();

  int scheme = url->scheme_get_wksidx();
  if (scheme != URL_WKSIDX_HTTP && scheme != URL_WKSIDX_HTTPS) {
    return false;
  }

  if (!statPagesManager.is_stat_page(url)) {
    return false;
  }

  return true;
}

bool
HttpTransact::handle_trace_and_options_requests(State *s, HTTPHdr *incoming_hdr)
{
  ink_assert(incoming_hdr->type_get() == HTTP_TYPE_REQUEST);

  // This only applies to TRACE and OPTIONS
  if ((s->method != HTTP_WKSIDX_TRACE) && (s->method != HTTP_WKSIDX_OPTIONS)) {
    return false;
  }

  // If there is no Max-Forwards request header, just return false.
  if (!incoming_hdr->presence(MIME_PRESENCE_MAX_FORWARDS)) {
    // Trace and Options requests should not be looked up in cache.
    // s->cache_info.action = CACHE_DO_NO_ACTION;
    s->current.mode = TUNNELLING_PROXY;
    HTTP_INCREMENT_DYN_STAT(http_tunnels_stat);
    return false;
  }

  int max_forwards = incoming_hdr->get_max_forwards();
  if (max_forwards <= 0) {
    //////////////////////////////////////////////
    // if max-forward is 0 the request must not //
    // be forwarded to the origin server.       //
    //////////////////////////////////////////////
    TxnDebug("http_trans", "[handle_trace] max-forwards: 0, building response...");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_response(s, &s->hdr_info.client_response, s->client_info.http_version, HTTP_STATUS_OK);

    ////////////////////////////////////////
    // if method is trace we should write //
    // the request header as the body.    //
    ////////////////////////////////////////
    if (s->method == HTTP_WKSIDX_TRACE) {
      TxnDebug("http_trans", "[handle_trace] inserting request in body.");
      int req_length = incoming_hdr->length_get();
      HTTP_RELEASE_ASSERT(req_length > 0);

      s->free_internal_msg_buffer();
      s->internal_msg_buffer_size = req_length * 2;

      if (s->internal_msg_buffer_size <= max_iobuffer_size) {
        s->internal_msg_buffer_fast_allocator_size = buffer_size_to_index(s->internal_msg_buffer_size);
        s->internal_msg_buffer = (char *)ioBufAllocator[s->internal_msg_buffer_fast_allocator_size].alloc_void();
      } else {
        s->internal_msg_buffer_fast_allocator_size = -1;
        s->internal_msg_buffer                     = (char *)ats_malloc(s->internal_msg_buffer_size);
      }

      // clear the stupid buffer
      memset(s->internal_msg_buffer, '\0', s->internal_msg_buffer_size);

      int offset = 0;
      int used   = 0;
      int done;
      done = incoming_hdr->print(s->internal_msg_buffer, s->internal_msg_buffer_size, &used, &offset);
      HTTP_RELEASE_ASSERT(done);
      s->internal_msg_buffer_size = used;
      s->internal_msg_buffer_type = ats_strdup("message/http");

      s->hdr_info.client_response.set_content_length(used);
    } else {
      // For OPTIONS request insert supported methods in ALLOW field
      TxnDebug("http_trans", "[handle_options] inserting methods in Allow.");
      HttpTransactHeaders::insert_supported_methods_in_response(&s->hdr_info.client_response, s->scheme);
    }
    return true;
  } else { /* max-forwards != 0 */

    // Logically want to make sure max_forwards is a legitimate non-zero non-negative integer
    // Since max_fowards is a signed integer, no sense making sure it is less than INT_MAX.
    // Would be negative in that case.  Already checked negative in the other case.  Noted by coverity

    --max_forwards;
    TxnDebug("http_trans", "[handle_trace_options] Decrementing max_forwards to %d", max_forwards);
    incoming_hdr->set_max_forwards(max_forwards);

    // Trace and Options requests should not be looked up in cache.
    // s->cache_info.action = CACHE_DO_NO_ACTION;
    s->current.mode = TUNNELLING_PROXY;
    HTTP_INCREMENT_DYN_STAT(http_tunnels_stat);
  }

  return false;
}

void
HttpTransact::initialize_state_variables_for_origin_server(State *s, HTTPHdr *incoming_request, bool second_time)
{
  if (s->server_info.name && !second_time) {
    ink_assert(s->server_info.dst_addr.port() != 0);
  }

  int host_len;
  const char *host    = incoming_request->host_get(&host_len);
  s->server_info.name = s->arena.str_store(host, host_len);

  if (second_time) {
    s->dns_info.attempts    = 0;
    s->dns_info.lookup_name = s->server_info.name;
  }
}

void
HttpTransact::bootstrap_state_variables_from_request(State *s, HTTPHdr *incoming_request)
{
  s->current.now = s->client_request_time = ink_local_time();
  s->client_info.http_version             = incoming_request->version_get();
}

void
HttpTransact::initialize_state_variables_from_request(State *s, HTTPHdr *obsolete_incoming_request)
{
  HTTPHdr *incoming_request = &s->hdr_info.client_request;

  // Temporary, until we're confident that the second argument is redundant.
  ink_assert(incoming_request == obsolete_incoming_request);

  int host_len;
  const char *host_name = incoming_request->host_get(&host_len);

  // check if the request is conditional (IMS or INM)
  if (incoming_request->presence(MIME_PRESENCE_IF_MODIFIED_SINCE | MIME_PRESENCE_IF_NONE_MATCH)) {
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_IMS);
  } else {
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_SIMPLE);
  }

  // Is the user agent Keep-Alive?
  //  If we are transparent or if the user-agent is following
  //  the 1.1 spec, we will see a "Connection" header to
  //  indicate a keep-alive.  However most user-agents including
  //  MSIE3.0, Netscape4.04 and Netscape3.01 send Proxy-Connection
  //  when they are configured to use a proxy.  Proxy-Connection
  //  is not in the spec but was added to prevent problems
  //  with a dumb proxy forwarding all headers (including "Connection")
  //  to the origin server and confusing it.  In cases of transparent
  //  deployments we use the Proxy-Connect hdr (to be as transparent
  //  as possible).
  MIMEField *pc = incoming_request->field_find(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION);

  // If we need to send a close header later check to see if it should be "Proxy-Connection"
  if (pc != nullptr) {
    s->client_info.proxy_connect_hdr = true;
  }

  NetVConnection *vc = nullptr;
  if (s->state_machine->ua_txn) {
    vc = s->state_machine->ua_txn->get_netvc();
  }

  if (vc) {
    s->request_data.incoming_port = vc->get_local_port();
    s->request_data.internal_txn  = vc->get_is_internal_request();
  }

  // If this is an internal request, never keep alive
  if (!s->txn_conf->keep_alive_enabled_in || (s->state_machine->ua_txn && s->state_machine->ua_txn->ignore_keep_alive())) {
    s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
  } else if (vc && vc->get_is_internal_request()) {
    // Following the trail of JIRAs back from TS-4960, there can be issues with
    // EOS event delivery when using keepalive on internal PluginVC session. As
    // an interim measure, if proxy.config.http.keepalive_internal_vc is set,
    // we will obey the incoming transaction's keepalive request.
    s->client_info.keep_alive =
      s->http_config_param->keepalive_internal_vc ? incoming_request->keep_alive_get() : HTTP_NO_KEEPALIVE;
  } else {
    s->client_info.keep_alive = incoming_request->keep_alive_get();
  }

  if (s->client_info.keep_alive == HTTP_KEEPALIVE && s->client_info.http_version == HTTPVersion(1, 1)) {
    s->client_info.pipeline_possible = true;
  }

  if (!s->server_info.name || s->redirect_info.redirect_in_process) {
    s->server_info.name = s->arena.str_store(host_name, host_len);
  }

  s->next_hop_scheme = s->scheme = incoming_request->url_get()->scheme_get_wksidx();

  // With websockets we need to make an outgoing request
  // as http or https.
  // We switch back to HTTP or HTTPS for the next hop
  // I think this is required to properly establish outbound WSS connections,
  // you'll need to force the next hop to be https.
  if (s->is_websocket) {
    if (s->next_hop_scheme == URL_WKSIDX_WS) {
      TxnDebug("http_trans", "Switching WS next hop scheme to http.");
      s->next_hop_scheme = URL_WKSIDX_HTTP;
      s->scheme          = URL_WKSIDX_HTTP;
      // s->request_data.hdr->url_get()->scheme_set(URL_SCHEME_HTTP, URL_LEN_HTTP);
    } else if (s->next_hop_scheme == URL_WKSIDX_WSS) {
      TxnDebug("http_trans", "Switching WSS next hop scheme to https.");
      s->next_hop_scheme = URL_WKSIDX_HTTPS;
      s->scheme          = URL_WKSIDX_HTTPS;
      // s->request_data.hdr->url_get()->scheme_set(URL_SCHEME_HTTPS, URL_LEN_HTTPS);
    } else {
      Error("Scheme doesn't match websocket...!");
    }

    s->current.mode      = GENERIC_PROXY;
    s->cache_info.action = CACHE_DO_NO_ACTION;
  }

  s->method = incoming_request->method_get_wksidx();

  if (s->method == HTTP_WKSIDX_GET) {
    HTTP_INCREMENT_DYN_STAT(http_get_requests_stat);
  } else if (s->method == HTTP_WKSIDX_HEAD) {
    HTTP_INCREMENT_DYN_STAT(http_head_requests_stat);
  } else if (s->method == HTTP_WKSIDX_POST) {
    HTTP_INCREMENT_DYN_STAT(http_post_requests_stat);
  } else if (s->method == HTTP_WKSIDX_PUT) {
    HTTP_INCREMENT_DYN_STAT(http_put_requests_stat);
  } else if (s->method == HTTP_WKSIDX_CONNECT) {
    HTTP_INCREMENT_DYN_STAT(http_connect_requests_stat);
  } else if (s->method == HTTP_WKSIDX_DELETE) {
    HTTP_INCREMENT_DYN_STAT(http_delete_requests_stat);
  } else if (s->method == HTTP_WKSIDX_PURGE) {
    HTTP_INCREMENT_DYN_STAT(http_purge_requests_stat);
  } else if (s->method == HTTP_WKSIDX_TRACE) {
    HTTP_INCREMENT_DYN_STAT(http_trace_requests_stat);
  } else if (s->method == HTTP_WKSIDX_PUSH) {
    HTTP_INCREMENT_DYN_STAT(http_push_requests_stat);
  } else if (s->method == HTTP_WKSIDX_OPTIONS) {
    HTTP_INCREMENT_DYN_STAT(http_options_requests_stat);
  } else if (s->method == HTTP_WKSIDX_TRACE) {
    HTTP_INCREMENT_DYN_STAT(http_trace_requests_stat);
  } else {
    HTTP_INCREMENT_DYN_STAT(http_extension_method_requests_stat);
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_METHOD);
    s->squid_codes.log_code      = SQUID_LOG_TCP_MISS;
    s->hdr_info.extension_method = true;
  }

  // if transfer encoding is chunked content length is undefined
  if (s->client_info.transfer_encoding == CHUNKED_ENCODING) {
    s->hdr_info.request_content_length = HTTP_UNDEFINED_CL;
  }
  s->request_data.hdr = &s->hdr_info.client_request;

  s->request_data.hostname_str = s->arena.str_store(host_name, host_len);
  ats_ip_copy(&s->request_data.src_ip, &s->client_info.src_addr);
  memset(&s->request_data.dest_ip, 0, sizeof(s->request_data.dest_ip));
  if (vc) {
    s->request_data.incoming_port = vc->get_local_port();
  }
  s->request_data.xact_start                      = s->client_request_time;
  s->request_data.api_info                        = &s->api_info;
  s->request_data.cache_info_lookup_url           = &s->cache_info.lookup_url;
  s->request_data.cache_info_parent_selection_url = &s->cache_info.parent_selection_url;

  /////////////////////////////////////////////
  // Do dns lookup for the host. We need     //
  // the expanded host for cache lookup, and //
  // the host ip for reverse proxy.          //
  /////////////////////////////////////////////
  s->dns_info.looking_up  = ORIGIN_SERVER;
  s->dns_info.attempts    = 0;
  s->dns_info.lookup_name = s->server_info.name;
}

void
HttpTransact::initialize_state_variables_from_response(State *s, HTTPHdr *incoming_response)
{
  /* check if the server permits caching */
  s->cache_info.directives.does_server_permit_storing =
    HttpTransactHeaders::does_server_allow_response_to_be_stored(&s->hdr_info.server_response);

  /*
   * A stupid moronic broken pathetic excuse
   *   for a server may send us a keep alive response even
   *   if we sent "Connection: close"  We need check the response
   *   header regardless of what we sent to the server
   */
  s->current.server->keep_alive = s->hdr_info.server_response.keep_alive_get();

  // Don't allow an upgrade request to Keep Alive
  if (s->is_upgrade_request) {
    s->current.server->keep_alive = HTTP_NO_KEEPALIVE;
  }

  if (s->current.server->keep_alive == HTTP_KEEPALIVE) {
    TxnDebug("http_hdrs", "[initialize_state_variables_from_response]"
                          "Server is keep-alive.");
  } else if (s->state_machine->ua_txn && s->state_machine->ua_txn->is_outbound_transparent() &&
             s->state_machine->t_state.http_config_param->use_client_source_port) {
    /* If we are reusing the client<->ATS 4-tuple for ATS<->server then if the server side is closed, we can't
       re-open it because the 4-tuple may still be in the processing of shutting down. So if the server isn't
       keep alive we must turn that off for the client as well.
    */
    s->state_machine->t_state.client_info.keep_alive = HTTP_NO_KEEPALIVE;
  }

  HTTPStatus status_code = incoming_response->status_get();
  if (is_response_body_precluded(status_code, s->method)) {
    s->hdr_info.response_content_length = 0;
    s->hdr_info.trust_response_cl       = true;
  } else {
    // This code used to discriminate CL: headers when the origin disabled keep-alive.
    if (incoming_response->presence(MIME_PRESENCE_CONTENT_LENGTH)) {
      int64_t cl = incoming_response->get_content_length();

      s->hdr_info.response_content_length = (cl >= 0) ? cl : HTTP_UNDEFINED_CL;
      s->hdr_info.trust_response_cl       = true;
    } else {
      s->hdr_info.response_content_length = HTTP_UNDEFINED_CL;
      s->hdr_info.trust_response_cl       = false;
    }
  }

  if (incoming_response->presence(MIME_PRESENCE_TRANSFER_ENCODING)) {
    MIMEField *field = incoming_response->field_find(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING);
    ink_assert(field != nullptr);

    HdrCsvIter enc_val_iter;
    int enc_val_len;
    const char *enc_value = enc_val_iter.get_first(field, &enc_val_len);

    while (enc_value) {
      const char *wks_value = hdrtoken_string_to_wks(enc_value, enc_val_len);

      if (wks_value == HTTP_VALUE_CHUNKED && !is_response_body_precluded(status_code, s->method)) {
        TxnDebug("http_hdrs", "[init_state_vars_from_resp] transfer encoding: chunked!");
        s->current.server->transfer_encoding = CHUNKED_ENCODING;

        s->hdr_info.response_content_length = HTTP_UNDEFINED_CL;
        s->hdr_info.trust_response_cl       = false;

        // OBJECTIVE: Since we are dechunking the request remove the
        //   chunked value If this is the only value, we need to remove
        //    the whole field.
        MIMEField *new_enc_field = nullptr;
        HdrCsvIter new_enc_iter;
        int new_enc_len;
        const char *new_enc_val = new_enc_iter.get_first(field, &new_enc_len);

        // Loop over the all the values in existing Trans-enc header and
        //   copy the ones that aren't our chunked value to a new field
        while (new_enc_val) {
          const char *new_wks_value = hdrtoken_string_to_wks(new_enc_val, new_enc_len);
          if (new_wks_value != wks_value) {
            if (new_enc_field) {
              new_enc_field->value_append(incoming_response->m_heap, incoming_response->m_mime, new_enc_val, new_enc_len, true);
            } else {
              new_enc_field = incoming_response->field_create();
              incoming_response->field_value_set(new_enc_field, new_enc_val, new_enc_len);
            }
          }

          new_enc_val = new_enc_iter.get_next(&new_enc_len);
        }

        // We're done with the old field since we copied out everything
        //   we needed
        incoming_response->field_delete(field);

        // If there is a new field (ie: there was more than one
        //   transfer-encoding), insert it to the list
        if (new_enc_field) {
          new_enc_field->name_set(incoming_response->m_heap, incoming_response->m_mime, MIME_FIELD_TRANSFER_ENCODING,
                                  MIME_LEN_TRANSFER_ENCODING);
          incoming_response->field_attach(new_enc_field);
        }

        return;
      } //  if (enc_value == CHUNKED)

      enc_value = enc_val_iter.get_next(&enc_val_len);
    }
  }

  s->current.server->transfer_encoding = NO_TRANSFER_ENCODING;
}

bool
HttpTransact::is_cache_response_returnable(State *s)
{
  if (s->cache_control.never_cache) {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_CONFIG);
    return false;
  }

  if (!s->cache_info.directives.does_client_permit_lookup) {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_CLIENT);
    return false;
  }

  if (!HttpTransactHeaders::is_method_cacheable(s->http_config_param, s->method) && s->api_resp_cacheable == false) {
    SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_NOT_ACCEPTABLE);
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_METHOD);
    return false;
  }
  // If cookies in response and no TTL set, we do not cache the doc
  if ((s->cache_control.ttl_in_cache <= 0) &&
      do_cookies_prevent_caching((int)s->txn_conf->cache_responses_to_cookies, &s->hdr_info.client_request,
                                 s->cache_info.object_read->response_get(), s->cache_info.object_read->request_get())) {
    SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_NOT_ACCEPTABLE);
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_COOKIE);
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : is_stale_cache_response_returnable()
// Description: check if a stale cached response is returnable to a client
//
// Input      : State
// Output     : true or false
//
// Details    :
//
///////////////////////////////////////////////////////////////////////////////
bool
HttpTransact::is_stale_cache_response_returnable(State *s)
{
  HTTPHdr *cached_response = s->cache_info.object_read->response_get();

  // First check if client allows cached response
  // Note does_client_permit_lookup was set to
  // does_client_Request_permit_cached_response()
  // in update_cache_control_information_from_config().
  if (!s->cache_info.directives.does_client_permit_lookup) {
    return false;
  }
  // Spec says that we can not serve a stale document with a
  //   "must-revalidate header"
  // How about "s-maxage" and "no-cache" directives?
  uint32_t cc_mask;
  cc_mask = (MIME_COOKED_MASK_CC_MUST_REVALIDATE | MIME_COOKED_MASK_CC_PROXY_REVALIDATE | MIME_COOKED_MASK_CC_NEED_REVALIDATE_ONCE |
             MIME_COOKED_MASK_CC_NO_CACHE | MIME_COOKED_MASK_CC_NO_STORE | MIME_COOKED_MASK_CC_S_MAXAGE);
  if ((cached_response->get_cooked_cc_mask() & cc_mask) || cached_response->is_pragma_no_cache_set()) {
    TxnDebug("http_trans", "[is_stale_cache_response_returnable] "
                           "document headers prevent serving stale");
    return false;
  }
  // See how old the document really is.  We don't want create a
  //   stale content museum of documents that are no longer available
  time_t current_age = HttpTransactHeaders::calculate_document_age(s->cache_info.object_read->request_sent_time_get(),
                                                                   s->cache_info.object_read->response_received_time_get(),
                                                                   cached_response, cached_response->get_date(), s->current.now);
  // Negative age is overflow
  if ((current_age < 0) || (current_age > s->txn_conf->cache_max_stale_age)) {
    TxnDebug("http_trans",
             "[is_stale_cache_response_returnable] "
             "document age is too large %" PRId64,
             (int64_t)current_age);
    return false;
  }
  // If the stale document requires authorization, we can't return it either.
  Authentication_t auth_needed = AuthenticationNeeded(s->txn_conf, &s->hdr_info.client_request, cached_response);

  if (auth_needed != AUTHENTICATION_SUCCESS) {
    TxnDebug("http_trans", "[is_stale_cache_response_returnable] "
                           "authorization prevent serving stale");
    return false;
  }

  TxnDebug("http_trans", "[is_stale_cache_response_returnable] can serve stale");
  return true;
}

bool
HttpTransact::url_looks_dynamic(URL *url)
{
  const char *p_start, *p, *t;
  static const char *asp = ".asp";
  const char *part;
  int part_length;

  if (url->scheme_get_wksidx() != URL_WKSIDX_HTTP && url->scheme_get_wksidx() != URL_WKSIDX_HTTPS) {
    return false;
  }
  ////////////////////////////////////////////////////////////
  // (1) If URL contains query stuff in it, call it dynamic //
  ////////////////////////////////////////////////////////////

  part = url->params_get(&part_length);
  if (part != nullptr) {
    return true;
  }
  part = url->query_get(&part_length);
  if (part != nullptr) {
    return true;
  }
  ///////////////////////////////////////////////
  // (2) If path ends in "asp" call it dynamic //
  ///////////////////////////////////////////////

  part = url->path_get(&part_length);
  if (part) {
    p = &part[part_length - 1];
    t = &asp[3];

    while (p != part) {
      if (ParseRules::ink_tolower(*p) == ParseRules::ink_tolower(*t)) {
        p -= 1;
        t -= 1;
        if (t == asp) {
          return true;
        }
      } else {
        break;
      }
    }
  }
  /////////////////////////////////////////////////////////////////
  // (3) If the path of the url contains "cgi", call it dynamic. //
  /////////////////////////////////////////////////////////////////

  if (part && part_length >= 3) {
    for (p_start = part; p_start <= &part[part_length - 3]; p_start++) {
      if (((p_start[0] == 'c') || (p_start[0] == 'C')) && ((p_start[1] == 'g') || (p_start[1] == 'G')) &&
          ((p_start[2] == 'i') || (p_start[2] == 'I'))) {
        return (true);
      }
    }
  }

  return (false);
}

///////////////////////////////////////////////////////////////////////////////
// Name       : is_request_cache_lookupable()
// Description: check if a request should be looked up in cache
//
// Input      : State, request header
// Output     : true or false
//
// Details    :
//
//
///////////////////////////////////////////////////////////////////////////////
bool
HttpTransact::is_request_cache_lookupable(State *s)
{
  // ummm, someone has already decided that proxy should tunnel
  if (s->current.mode == TUNNELLING_PROXY) {
    return false;
  }
  // don't bother with remaining checks if we already did a cache lookup
  if (s->cache_info.lookup_count > 0) {
    return true;
  }
  // is cache turned on?
  if (!s->txn_conf->cache_http) {
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_CACHE_OFF);
    return false;
  }
  // GET, HEAD, POST, DELETE, and PUT are all cache lookupable
  if (!HttpTransactHeaders::is_method_cache_lookupable(s->method) && s->api_req_cacheable == false) {
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_METHOD);
    return false;
  }
  // don't cache page if URL "looks dynamic" and this filter is enabled
  // We can do the check in is_response_cacheable() or here.
  // It may be more efficient if we are not going to cache dynamic looking urls
  // (the default config?) since we don't even need to do cache lookup.
  // So for the time being, it'll be left here.

  // If url looks dynamic but a ttl is set, request is cache lookupable
  if ((!s->txn_conf->cache_urls_that_look_dynamic) && url_looks_dynamic(s->hdr_info.client_request.url_get()) &&
      (s->cache_control.ttl_in_cache <= 0)) {
    // We do not want to forward the request for a dynamic URL onto the
    // origin server if the value of the Max-Forwards header is zero.
    int max_forwards = -1;
    if (s->hdr_info.client_request.presence(MIME_PRESENCE_MAX_FORWARDS)) {
      MIMEField *max_forwards_f = s->hdr_info.client_request.field_find(MIME_FIELD_MAX_FORWARDS, MIME_LEN_MAX_FORWARDS);

      if (max_forwards_f) {
        max_forwards = max_forwards_f->value_get_int();
      }
    }

    if (max_forwards != 0) {
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_URL);
      return false;
    }
  }

  // Don't look in cache if it's a RANGE request but the cache is not enabled for RANGE.
  if (!s->txn_conf->cache_range_lookup && s->hdr_info.client_request.presence(MIME_PRESENCE_RANGE)) {
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_HEADER_FIELD);
    return false;
  }

  // Even with "no-cache" directive, we want to do a cache lookup
  // because we need to update our cached copy.
  // Client request "no-cache" directive is handle elsewhere:
  // update_cache_control_information_from_config()

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : response_cacheable_indicated_by_cc()
// Description: check if a response is cacheable as indicated by Cache-Control
//
// Input      : Response header
// Output     : -1, 0, or +1
//
// Details    :
// (1) return -1 if cache control indicates response not cacheable,
//     ie, with no-store, or private directives;
// (2) return +1 if cache control indicates response cacheable
//     ie, with public, max-age, s-maxage, must-revalidate, or proxy-revalidate;
// (3) otherwise, return 0 if cache control does not indicate.
//
///////////////////////////////////////////////////////////////////////////////
int
response_cacheable_indicated_by_cc(HTTPHdr *response)
{
  uint32_t cc_mask;
  // the following directives imply not cacheable
  cc_mask = (MIME_COOKED_MASK_CC_NO_STORE | MIME_COOKED_MASK_CC_PRIVATE);
  if (response->get_cooked_cc_mask() & cc_mask) {
    return -1;
  }
  // the following directives imply cacheable
  cc_mask = (MIME_COOKED_MASK_CC_PUBLIC | MIME_COOKED_MASK_CC_MAX_AGE | MIME_COOKED_MASK_CC_S_MAXAGE |
             MIME_COOKED_MASK_CC_MUST_REVALIDATE | MIME_COOKED_MASK_CC_PROXY_REVALIDATE);
  if (response->get_cooked_cc_mask() & cc_mask) {
    return 1;
  }
  // otherwise, no indication
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : is_response_cacheable()
// Description: check if a response is cacheable
//
// Input      : State, request header, response header
// Output     : true or false
//
// Details    :
//
///////////////////////////////////////////////////////////////////////////////
bool
HttpTransact::is_response_cacheable(State *s, HTTPHdr *request, HTTPHdr *response)
{
  // If the use_client_target_addr is specified but the client
  // specified OS addr does not match any of trafficserver's looked up
  // host addresses, do not allow cache.  This may cause DNS cache poisoning
  // of other trafficserver clients. The flag is set in the
  // process_host_db_info method
  if (!s->dns_info.lookup_validated && s->client_info.is_transparent) {
    TxnDebug("http_trans", "[is_response_cacheable] "
                           "Lookup not validated.  Possible DNS cache poison.  Don't cache");
    return false;
  }

  // the plugin may decide we don't want to cache the response
  if (s->api_server_response_no_store) {
    return false;
  }
  // if method is not GET or HEAD, do not cache.
  // Note: POST is also cacheable with Expires or Cache-control.
  // but due to INKqa11567, we are not caching POST responses.
  // Basically, the problem is the resp for POST url1 req should not
  // be served to a GET url1 request, but we just match URL not method.
  int req_method = request->method_get_wksidx();
  if (!(HttpTransactHeaders::is_method_cacheable(s->http_config_param, req_method)) && s->api_req_cacheable == false) {
    TxnDebug("http_trans", "[is_response_cacheable] "
                           "only GET, and some HEAD and POST are cachable");
    return false;
  }
  // TxnDebug("http_trans", "[is_response_cacheable] method is cacheable");
  // If the request was not looked up in the cache, the response
  // should not be cached (same subsequent requests will not be
  // looked up, either, so why cache this).
  if (!(is_request_cache_lookupable(s))) {
    TxnDebug("http_trans", "[is_response_cacheable] "
                           "request is not cache lookupable, response is not cachable");
    return false;
  }
  // already has a fresh copy in the cache
  if (s->range_setup == RANGE_NOT_HANDLED) {
    return false;
  }

  // Check whether the response is cachable based on its cookie
  // If there are cookies in response but a ttl is set, allow caching
  if ((s->cache_control.ttl_in_cache <= 0) &&
      do_cookies_prevent_caching((int)s->txn_conf->cache_responses_to_cookies, request, response)) {
    TxnDebug("http_trans", "[is_response_cacheable] "
                           "response has uncachable cookies, response is not cachable");
    return false;
  }
  // if server spits back a WWW-Authenticate
  if ((s->txn_conf->cache_ignore_auth) == 0 && response->presence(MIME_PRESENCE_WWW_AUTHENTICATE)) {
    TxnDebug("http_trans", "[is_response_cacheable] "
                           "response has WWW-Authenticate, response is not cachable");
    return false;
  }
  // does server explicitly forbid storing?
  // If OS forbids storing but a ttl is set, allow caching
  if (!s->cache_info.directives.does_server_permit_storing && (s->cache_control.ttl_in_cache <= 0)) {
    TxnDebug("http_trans", "[is_response_cacheable] server does not permit storing and config file does not "
                           "indicate that server directive should be ignored");
    return false;
  }
  // TxnDebug("http_trans", "[is_response_cacheable] server permits storing");

  // does config explicitly forbid storing?
  // ttl overides other config parameters
  if ((!s->cache_info.directives.does_config_permit_storing && (s->cache_control.ttl_in_cache <= 0)) ||
      (s->cache_control.never_cache)) {
    TxnDebug("http_trans", "[is_response_cacheable] config doesn't allow storing, and cache control does not "
                           "say to ignore no-cache and does not specify never-cache or a ttl");
    return false;
  }
  // TxnDebug("http_trans", "[is_response_cacheable] config permits storing");

  // does client explicitly forbid storing?
  if (!s->cache_info.directives.does_client_permit_storing && !s->cache_control.ignore_client_no_cache) {
    TxnDebug("http_trans", "[is_response_cacheable] client does not permit storing, "
                           "and cache control does not say to ignore client no-cache");
    return false;
  }
  TxnDebug("http_trans", "[is_response_cacheable] client permits storing");

  HTTPStatus response_code = response->status_get();

  // caching/not-caching based on required headers
  // only makes sense when the server sends back a
  // 200 and a document.
  if (response_code == HTTP_STATUS_OK) {
    // If a ttl is set: no header required for caching
    // otherwise: follow parameter http.cache.required_headers
    if (s->cache_control.ttl_in_cache <= 0) {
      uint32_t cc_mask = (MIME_COOKED_MASK_CC_MAX_AGE | MIME_COOKED_MASK_CC_S_MAXAGE);
      // server did not send expires header or last modified
      // and we are configured to not cache without them.
      switch (s->txn_conf->cache_required_headers) {
      case HttpConfigParams::CACHE_REQUIRED_HEADERS_NONE:
        TxnDebug("http_trans", "[is_response_cacheable] "
                               "no response headers required");
        break;

      case HttpConfigParams::CACHE_REQUIRED_HEADERS_AT_LEAST_LAST_MODIFIED:
        if (!response->presence(MIME_PRESENCE_EXPIRES) && !(response->get_cooked_cc_mask() & cc_mask) &&
            !response->get_last_modified()) {
          TxnDebug("http_trans", "[is_response_cacheable] "
                                 "last_modified, expires, or max-age is required");

          s->squid_codes.hit_miss_code = ((response->get_date() == 0) ? (SQUID_MISS_HTTP_NO_DLE) : (SQUID_MISS_HTTP_NO_LE));
          return false;
        }
        break;

      case HttpConfigParams::CACHE_REQUIRED_HEADERS_CACHE_CONTROL:
        if (!response->presence(MIME_PRESENCE_EXPIRES) && !(response->get_cooked_cc_mask() & cc_mask)) {
          TxnDebug("http_trans", "[is_response_cacheable] "
                                 "expires header or max-age is required");
          return false;
        }
        break;

      default:
        break;
      }
    }
  }
  // do not cache partial content - Range response
  if (response_code == HTTP_STATUS_PARTIAL_CONTENT || response_code == HTTP_STATUS_RANGE_NOT_SATISFIABLE) {
    TxnDebug("http_trans",
             "[is_response_cacheable] "
             "response code %d - don't cache",
             response_code);
    return false;
  }

  // check if cache control overrides default cacheability
  int indicator;
  indicator = response_cacheable_indicated_by_cc(response);
  if (indicator > 0) { // cacheable indicated by cache control header
    TxnDebug("http_trans", "[is_response_cacheable] YES by response cache control");
    // even if it is authenticated, this is cacheable based on regular rules
    s->www_auth_content = CACHE_AUTH_NONE;
    return true;
  } else if (indicator < 0) { // not cacheable indicated by cache control header

    // If a ttl is set, allow caching even if response contains
    // Cache-Control headers to prevent caching
    if (s->cache_control.ttl_in_cache > 0) {
      TxnDebug("http_trans",
               "[is_response_cacheable] Cache-control header directives in response overridden by ttl in cache.config");
    } else {
      TxnDebug("http_trans", "[is_response_cacheable] NO by response cache control");
      return false;
    }
  }
  // else no indication by cache control header
  // continue to determine cacheability

  // if client contains Authorization header,
  // only cache if response has proper Cache-Control
  // if (s->www_auth_content == CACHE_AUTH_FRESH) {
  // response to the HEAD request
  //  return false;
  //} else if (s->www_auth_content == CACHE_AUTH_TRUE ||
  //          (s->www_auth_content == CACHE_AUTH_NONE && request->presence(MIME_PRESENCE_AUTHORIZATION))) {
  // if (!s->cache_control.cache_auth_content || response_code != HTTP_STATUS_OK || req_method != HTTP_WKSIDX_GET)
  //  return false;
  //}
  // s->www_auth_content == CACHE_AUTH_STALE silently continues

  if (response->presence(MIME_PRESENCE_EXPIRES)) {
    TxnDebug("http_trans", "[is_response_cacheable] YES response w/ Expires");
    return true;
  }
  // if it's a 302 or 307 and no positive indicator from cache-control, reject
  if (response_code == HTTP_STATUS_MOVED_TEMPORARILY || response_code == HTTP_STATUS_TEMPORARY_REDIRECT) {
    TxnDebug("http_trans", "[is_response_cacheable] cache-control or expires header is required for 302");
    return false;
  }
  // if it's a POST request and no positive indicator from cache-control
  if (req_method == HTTP_WKSIDX_POST) {
    // allow caching for a POST requests w/o Expires but with a ttl
    if (s->cache_control.ttl_in_cache > 0) {
      TxnDebug("http_trans", "[is_response_cacheable] POST method with a TTL");
    } else {
      TxnDebug("http_trans", "[is_response_cacheable] NO POST w/o Expires or CC");
      return false;
    }
  }

  // default cacheability
  if (!s->txn_conf->negative_caching_enabled) {
    if ((response_code == HTTP_STATUS_OK) || (response_code == HTTP_STATUS_NOT_MODIFIED) ||
        (response_code == HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION) || (response_code == HTTP_STATUS_MOVED_PERMANENTLY) ||
        (response_code == HTTP_STATUS_MULTIPLE_CHOICES) || (response_code == HTTP_STATUS_GONE)) {
      TxnDebug("http_trans", "[is_response_cacheable] YES by default ");
      return true;
    } else {
      TxnDebug("http_trans", "[is_response_cacheable] NO by default");
      return false;
    }
  }
  if (response_code == HTTP_STATUS_SEE_OTHER || response_code == HTTP_STATUS_UNAUTHORIZED ||
      response_code == HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED) {
    return false;
  }
  // let is_negative_caching_approriate decide what to do
  return true;
  /* Since we weren't caching response obtained with
     Authorization (the cache control stuff was commented out previously)
     I've moved this check to is_request_cache_lookupable().
     We should consider this matter further.  It is unclear
     how many sites actually add Cache-Control headers for Authorized content.

      // if client contains Authorization header, only cache if response
      // has proper Cache-Control flags, as in RFC2068, section 14.8.
      if (request->field_presence(MIME_PRESENCE_AUTHORIZATION)) {
  //         if (! (response->is_cache_control_set(HTTP_VALUE_MUST_REVALIDATE)) &&
  //             ! (response->is_cache_control_set(HTTP_VALUE_PROXY_REVALIDATE)) &&
  //             ! (response->is_cache_control_set(HTTP_VALUE_PUBLIC))) {

              TxnDebug("http_trans", "[is_response_cacheable] request has AUTHORIZATION - not cacheable");
              return(false);
  //         }
  //      else {
  //          TxnDebug("http_trans","[is_response_cacheable] request has AUTHORIZATION, "
  //                "but response has a cache-control that allows caching");
  //      }
      }
  */
}

bool
HttpTransact::is_request_valid(State *s, HTTPHdr *incoming_request)
{
  RequestError_t incoming_error;
  URL *url = nullptr;

  if (incoming_request) {
    url = incoming_request->url_get();
  }

  incoming_error = check_request_validity(s, incoming_request);
  switch (incoming_error) {
  case NO_REQUEST_HEADER_ERROR:
    TxnDebug("http_trans", "[is_request_valid] no request header errors");
    break;
  case FAILED_PROXY_AUTHORIZATION:
    TxnDebug("http_trans", "[is_request_valid] failed proxy authorization");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_error_response(s, HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED, "Proxy Authentication Required",
                         "access#proxy_auth_required");
    return false;
  case NON_EXISTANT_REQUEST_HEADER:
  /* fall through */
  case BAD_HTTP_HEADER_SYNTAX: {
    TxnDebug("http_trans", "[is_request_valid] non-existant/bad header");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Invalid HTTP Request", "request#syntax_error");
    return false;
  }

  case MISSING_HOST_FIELD:

    ////////////////////////////////////////////////////////////////////
    // FIX: are we sure the following logic is right?  it seems that  //
    //      we shouldn't complain about the missing host header until //
    //      we know we really need one --- are we sure we need a host //
    //      header at this point?                                     //
    //                                                                //
    // FIX: also, let's clean up the transparency code to remove the  //
    //      SunOS conditionals --- we will be transparent on all      //
    //      platforms soon!  in fact, I really want a method that i   //
    //      can call for each transaction to say if the transaction   //
    //      is a forward proxy request, a transparent request, a      //
    //      reverse proxy request, etc --- the detail of how we       //
    //      determine the cases should be hidden behind the method.   //
    ////////////////////////////////////////////////////////////////////

    TxnDebug("http_trans", "[is_request_valid] missing host field");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    if (s->http_config_param->reverse_proxy_enabled) { // host header missing and reverse proxy on
      build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Host Header Required", "request#no_host");
    } else {
      // host header missing and reverse proxy off
      build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Host Required In Request", "request#no_host");
    }

    return false;
  case SCHEME_NOT_SUPPORTED:
  case NO_REQUEST_SCHEME: {
    TxnDebug("http_trans", "[is_request_valid] unsupported or missing request scheme");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Unsupported URL Scheme", "request#scheme_unsupported");
    return false;
  }
  /* fall through */
  case METHOD_NOT_SUPPORTED:
    TxnDebug("http_trans", "[is_request_valid] unsupported method");
    s->current.mode = TUNNELLING_PROXY;
    return true;
  case BAD_CONNECT_PORT:
    int port;
    port = url ? url->port_get() : 0;
    TxnDebug("http_trans", "[is_request_valid] %d is an invalid connect port", port);
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_error_response(s, HTTP_STATUS_FORBIDDEN, "Tunnel Forbidden", "access#connect_forbidden");
    return false;
  case NO_POST_CONTENT_LENGTH: {
    TxnDebug("http_trans", "[is_request_valid] post request without content length");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_error_response(s, HTTP_STATUS_LENGTH_REQUIRED, "Content Length Required", "request#no_content_length");
    return false;
  }
  case UNACCEPTABLE_TE_REQUIRED: {
    TxnDebug("http_trans", "[is_request_valid] TE required is unacceptable.");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_error_response(s, HTTP_STATUS_NOT_ACCEPTABLE, "Transcoding Not Available", "transcoding#unsupported");
    return false;
  }
  case INVALID_POST_CONTENT_LENGTH: {
    TxnDebug("http_trans", "[is_request_valid] post request with negative content length value");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Invalid Content Length", "request#invalid_content_length");
    return false;
  }
  default:
    return true;
  }

  return true;
}

// bool HttpTransact::is_request_retryable
//
// In the general case once bytes have been sent on the wire the request cannot be retried.
// The reason we cannot retry is that the rfc2616 does not make any guarantees about the
// retry-ability of a request. In fact in the reverse proxy case it is quite common for GET
// requests on the origin to fire tracking events etc. So, as a proxy, once bytes have been ACKd
// by the server we cannot guarantee that the request is safe to retry or redispatch to another server.
// This is distinction of "ACKd" vs "sent" is intended, and has reason. In the case of a
// new origin connection there is little difference, as the chance of a RST between setup
// and the first set of bytes is relatively small. This distinction is more apparent in the
// case where the origin connection is a KA session. In this case, the session may not have
// been used for a long time. In that case, we'll immediately queue up session to send to the
// origin, without any idea of the state of the connection. If the origin is dead (or the connection
// is broken for some other reason) we'll immediately get a RST back. In that case-- since no
// bytes where ACKd by the remote end, we can retry/redispatch the request.
//
bool
HttpTransact::is_request_retryable(State *s)
{
  // If safe requests are  retryable, it should be safe to retry safe requests irrespective of bytes sent or connection state
  // according to RFC the following methods are safe (https://tools.ietf.org/html/rfc7231#section-4.2.1)
  // Otherwise, if there was no error establishing the connection (and we sent bytes)-- we cannot retry
  if (!HttpTransactHeaders::is_method_safe(s->method) && s->current.state != CONNECTION_ERROR &&
      s->state_machine->server_request_hdr_bytes > 0) {
    return false;
  }

  // FIXME: disable the post transform retry currently.
  if (s->state_machine->is_post_transform_request()) {
    return false;
  }

  if (s->state_machine->plugin_tunnel_type != HTTP_NO_PLUGIN_TUNNEL) {
    // API can override
    if (s->state_machine->plugin_tunnel_type == HTTP_PLUGIN_AS_SERVER && s->api_info.retry_intercept_failures == true) {
      // This used to be an == comparison, which made no sense. Changed
      // to be an assignment, hoping the state is correct.
      s->state_machine->plugin_tunnel_type = HTTP_NO_PLUGIN_TUNNEL;
    } else {
      return false;
    }
  }

  return true;
}

bool
HttpTransact::is_response_valid(State *s, HTTPHdr *incoming_response)
{
  if (s->current.state != CONNECTION_ALIVE) {
    ink_assert((s->current.state == CONNECTION_ERROR) || (s->current.state == OPEN_RAW_ERROR) ||
               (s->current.state == PARSE_ERROR) || (s->current.state == CONNECTION_CLOSED) ||
               (s->current.state == INACTIVE_TIMEOUT) || (s->current.state == ACTIVE_TIMEOUT));

    s->hdr_info.response_error = CONNECTION_OPEN_FAILED;
    return false;
  }

  s->hdr_info.response_error = check_response_validity(s, incoming_response);

  switch (s->hdr_info.response_error) {
#ifdef REALLY_NEED_TO_CHECK_DATE_VALIDITY
  case BOGUS_OR_NO_DATE_IN_RESPONSE:
    // We could modify the response to add the date, if need be.
    //          incoming_response->set_date(s->request_sent_time);
    return true;
#endif
  case NO_RESPONSE_HEADER_ERROR:
    TxnDebug("http_trans", "[is_response_valid] No errors in response");
    return true;

  case MISSING_REASON_PHRASE:
    TxnDebug("http_trans", "[is_response_valid] Response Error: Missing reason phrase - allowing");
    return true;

  case STATUS_CODE_SERVER_ERROR:
    TxnDebug("http_trans", "[is_response_valid] Response Error: Origin Server returned 500 - allowing");
    return true;

  case CONNECTION_OPEN_FAILED:
    TxnDebug("http_trans", "[is_response_valid] Response Error: connection open failed");
    s->current.state = CONNECTION_ERROR;
    return false;

  case NON_EXISTANT_RESPONSE_HEADER:
    TxnDebug("http_trans", "[is_response_valid] Response Error: No response header");
    s->current.state = BAD_INCOMING_RESPONSE;
    return false;

  case NOT_A_RESPONSE_HEADER:
    TxnDebug("http_trans", "[is_response_valid] Response Error: Not a response header");
    s->current.state = BAD_INCOMING_RESPONSE;
    return false;

  case MISSING_STATUS_CODE:
    TxnDebug("http_trans", "[is_response_valid] Response Error: Missing status code");
    s->current.state = BAD_INCOMING_RESPONSE;
    return false;

  default:
    TxnDebug("http_trans", "[is_response_valid] Errors in response");
    s->current.state = BAD_INCOMING_RESPONSE;
    return false;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : service_transaction_in_proxy_only_mode
// Description: uses some metric to force this transaction to be proxy-only
//
// Details    :
//
// Some metric may be employed to force the traffic server to enter
// a proxy-only mode temporarily. This function is called to determine
// if the current transaction should be proxy-only. The function is
// called from initialize_state_variables_from_request and is used to
// set s->current.mode to TUNNELLING_PROXY and just for safety to set
// s->cache_info.action to CACHE_DO_NO_ACTION.
//
// Currently the function is just a placeholder and always returns false.
//
///////////////////////////////////////////////////////////////////////////////
bool
HttpTransact::service_transaction_in_proxy_only_mode(State * /* s ATS_UNUSED */)
{
  return false;
}

void
HttpTransact::process_quick_http_filter(State *s, int method)
{
  // connection already disabled by previous ACL filtering, don't modify it.
  if (!s->client_connection_enabled) {
    return;
  }

  // if ipallow rules are disabled by remap then don't modify anything
  url_mapping *mp = s->url_map.getMapping();
  if (mp && !mp->ip_allow_check_enabled_p) {
    return;
  }

  if (s->state_machine->ua_txn) {
    const AclRecord *acl_record = s->state_machine->ua_txn->get_acl_record();
    bool deny_request           = (acl_record == nullptr);
    if (acl_record && (acl_record->_method_mask != AclRecord::ALL_METHOD_MASK)) {
      if (method != -1) {
        deny_request = !acl_record->isMethodAllowed(method);
      } else {
        int method_str_len;
        const char *method_str = s->hdr_info.client_request.method_get(&method_str_len);
        deny_request           = !acl_record->isNonstandardMethodAllowed(std::string(method_str, method_str_len));
      }
    }
    if (deny_request) {
      if (is_debug_tag_set("ip-allow")) {
        ip_text_buffer ipb;
        TxnDebug("ip-allow", "Quick filter denial on %s:%s with mask %x",
                 ats_ip_ntop(&s->client_info.src_addr.sa, ipb, sizeof(ipb)), hdrtoken_index_to_wks(method),
                 acl_record ? acl_record->_method_mask : 0x0);
      }
      s->client_connection_enabled = false;
    }
  }
}

HttpTransact::HostNameExpansionError_t
HttpTransact::try_to_expand_host_name(State *s)
{
  HTTP_RELEASE_ASSERT(!s->dns_info.lookup_success);

  if (s->dns_info.looking_up == ORIGIN_SERVER) {
    return EXPANSION_NOT_ALLOWED;
  } else {
    //////////////////////////////////////////////////////
    // we looked up dns of parent proxy, but it failed, //
    // try lookup of origin server name.                //
    //////////////////////////////////////////////////////
    ink_assert(s->dns_info.looking_up == PARENT_PROXY);

    s->dns_info.lookup_name = s->server_info.name;
    s->dns_info.looking_up  = ORIGIN_SERVER;
    s->dns_info.attempts    = 0;

    return RETRY_EXPANDED_NAME;
  }
}

bool
HttpTransact::will_this_request_self_loop(State *s)
{
  ////////////////////////////////////////
  // check if we are about to self loop //
  ////////////////////////////////////////
  if (s->dns_info.lookup_success) {
    if (ats_ip_addr_eq(s->host_db_info.ip(), &Machine::instance()->ip.sa)) {
      in_port_t host_port  = s->hdr_info.client_request.url_get()->port_get();
      in_port_t local_port = s->client_info.src_addr.host_order_port();
      if (host_port == local_port) {
        switch (s->dns_info.looking_up) {
        case ORIGIN_SERVER:
          TxnDebug("http_transact", "[will_this_request_self_loop] host ip and port same as local ip and port - bailing");
          break;
        case PARENT_PROXY:
          TxnDebug("http_transact", "[will_this_request_self_loop] "
                                    "parent proxy ip and port same as local ip and port - bailing");
          break;
        default:
          TxnDebug("http_transact", "[will_this_request_self_loop] "
                                    "unknown's ip and port same as local ip and port - bailing");
          break;
        }
        build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Cycle Detected", "request#cycle_detected");
        return true;
      }
    }

    // Now check for a loop using the Via string.
    const char *uuid     = Machine::instance()->uuid.getString();
    MIMEField *via_field = s->hdr_info.client_request.field_find(MIME_FIELD_VIA, MIME_LEN_VIA);

    while (via_field) {
      // No need to waste cycles comma separating the via values since we want to do a match anywhere in the
      // in the string.  We can just loop over the dup hdr fields
      int via_len;
      const char *via_string = via_field->value_get(&via_len);

      if (via_string && ptr_len_str(via_string, via_len, uuid)) {
        TxnDebug("http_transact", "[will_this_request_self_loop] Incoming via: %.*s has (%s[%s] (%s))", via_len, via_string,
                 s->http_config_param->proxy_hostname, uuid, s->http_config_param->proxy_request_via_string);
        build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Multi-Hop Cycle Detected", "request#cycle_detected");
        return true;
      }

      via_field = via_field->m_next_dup;
    }
  }
  s->request_will_not_selfloop = true;
  return false;
}

/*
 * handle_content_length_header(...)
 *  Function handles the insertion of content length headers into
 * header. header CAN equal base.
 */
void
HttpTransact::handle_content_length_header(State *s, HTTPHdr *header, HTTPHdr *base)
{
  int64_t cl = HTTP_UNDEFINED_CL;
  ink_assert(header->type_get() == HTTP_TYPE_RESPONSE);
  if (base->presence(MIME_PRESENCE_CONTENT_LENGTH)) {
    cl = base->get_content_length();
    if (cl >= 0) {
      // header->set_content_length(cl);
      ink_assert(header->get_content_length() == cl);

      switch (s->source) {
      case SOURCE_HTTP_ORIGIN_SERVER:
        // We made our decision about whether to trust the
        //   response content length in init_state_vars_from_response()
        if (s->range_setup != HttpTransact::RANGE_NOT_TRANSFORM_REQUESTED) {
          break;
        }
        // fallthrough

      case SOURCE_CACHE:
        // if we are doing a single Range: request, calculate the new
        // C-L: header
        if (s->range_setup == HttpTransact::RANGE_NOT_TRANSFORM_REQUESTED) {
          change_response_header_because_of_range_request(s, header);
          s->hdr_info.trust_response_cl = true;
        }
        ////////////////////////////////////////////////
        //  Make sure that the cache's object size    //
        //   agrees with the Content-Length           //
        //   Otherwise, set the state's machine view  //
        //   of c-l to undefined to turn off K-A      //
        ////////////////////////////////////////////////
        else if ((int64_t)s->cache_info.object_read->object_size_get() == cl) {
          s->hdr_info.trust_response_cl = true;
        } else {
          TxnDebug("http_trans", "Content Length header and cache object size mismatch."
                                 "Disabling keep-alive");
          s->hdr_info.trust_response_cl = false;
        }
        break;

      case SOURCE_TRANSFORM:
        if (s->range_setup == HttpTransact::RANGE_REQUESTED) {
          header->set_content_length(s->range_output_cl);
          s->hdr_info.trust_response_cl = true;
        } else if (s->hdr_info.transform_response_cl == HTTP_UNDEFINED_CL) {
          s->hdr_info.trust_response_cl = false;
        } else {
          s->hdr_info.trust_response_cl = true;
        }
        break;

      default:
        ink_release_assert(0);
        break;
      }
    } else {
      header->field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
      s->hdr_info.trust_response_cl = false;
    }
    TxnDebug("http_trans", "[handle_content_length_header] RESPONSE cont len in hdr is %" PRId64, header->get_content_length());
  } else {
    // No content length header.
    if (s->source == SOURCE_CACHE) {
      // If there is no content-length header, we can
      //   insert one since the cache knows definately
      //   how long the object is unless we're in a
      //   read-while-write mode and object hasn't been
      //   written into a cache completely.
      cl = s->cache_info.object_read->object_size_get();
      if (cl == INT64_MAX) { // INT64_MAX cl in cache indicates rww in progress
        header->field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
        s->hdr_info.trust_response_cl      = false;
        s->hdr_info.request_content_length = HTTP_UNDEFINED_CL;
        ink_assert(s->range_setup == RANGE_NONE);
      } else if (s->range_setup == RANGE_NOT_TRANSFORM_REQUESTED) {
        // if we are doing a single Range: request, calculate the new
        // C-L: header
        change_response_header_because_of_range_request(s, header);
        s->hdr_info.trust_response_cl = true;
      } else {
        header->set_content_length(cl);
        s->hdr_info.trust_response_cl = true;
      }
    } else if (s->source == SOURCE_HTTP_ORIGIN_SERVER && s->hdr_info.server_response.status_get() == HTTP_STATUS_NOT_MODIFIED &&
               s->range_setup == RANGE_NOT_TRANSFORM_REQUESTED) {
      // In this case, we had a cached object, possibly chunked encoded (so we don't have a CL: header), but the origin did a
      // 304 Not Modified response. We can still turn this into a proper Range response from the cached object.
      change_response_header_because_of_range_request(s, header);
      s->hdr_info.trust_response_cl = true;
    } else {
      // Check to see if there is no content length
      //  header because the response precludes a
      // body
      if (is_response_body_precluded(header->status_get(), s->method)) {
        // We want to be able to do keep-alive here since
        //   there can't be body so we don't have any
        //   issues about trusting the body length
        s->hdr_info.trust_response_cl = true;
      } else {
        s->hdr_info.trust_response_cl = false;
      }
      header->field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
      ink_assert(s->range_setup != RANGE_NOT_TRANSFORM_REQUESTED);
    }
  }
  return;
} /* End HttpTransact::handle_content_length_header */

//////////////////////////////////////////////////////////////////////////////
//
//      void HttpTransact::handle_request_keep_alive_headers(
//          State* s, bool ka_on, HTTPVersion ver, HTTPHdr *heads)
//
//      Removes keep alive headers from user-agent from <heads>
//
//      Adds the appropriate keep alive headers (if any) to <heads>
//      for keep-alive state <ka_on>, and HTTP version <ver>.
//
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_request_keep_alive_headers(State *s, HTTPVersion ver, HTTPHdr *heads)
{
  enum KA_Action_t {
    KA_UNKNOWN,
    KA_DISABLED,
    KA_CLOSE,
    KA_CONNECTION,
  };

  KA_Action_t ka_action = KA_UNKNOWN;
  bool upstream_ka      = (s->current.server->keep_alive == HTTP_KEEPALIVE);

  ink_assert(heads->type_get() == HTTP_TYPE_REQUEST);

  // Check preconditions for Keep-Alive
  if (!upstream_ka) {
    ka_action = KA_DISABLED;
  } else if (HTTP_MAJOR(ver.m_version) == 0) { /* No K-A for 0.9 apps */
    ka_action = KA_DISABLED;
  }
  // If preconditions are met, figure out what action to take
  if (ka_action == KA_UNKNOWN) {
    int method = heads->method_get_wksidx();
    if (method == HTTP_WKSIDX_GET || method == HTTP_WKSIDX_HEAD || method == HTTP_WKSIDX_OPTIONS || method == HTTP_WKSIDX_PURGE ||
        method == HTTP_WKSIDX_DELETE || method == HTTP_WKSIDX_TRACE) {
      // These methods do not need a content-length header
      ka_action = KA_CONNECTION;
    } else {
      // All remaining methods require a content length header
      if (heads->get_content_length() == -1) {
        ka_action = KA_CLOSE;
      } else {
        ka_action = KA_CONNECTION;
      }
    }
  }

  ink_assert(ka_action != KA_UNKNOWN);

  // Since connection headers are hop-to-hop, strip the
  //  the ones we received from the user-agent
  heads->field_delete(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION);
  heads->field_delete(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);

  if (!s->is_upgrade_request) {
    // Insert K-A headers as necessary
    switch (ka_action) {
    case KA_CONNECTION:
      ink_assert(s->current.server->keep_alive != HTTP_NO_KEEPALIVE);
      if (ver == HTTPVersion(1, 0)) {
        if (s->current.request_to == PARENT_PROXY) {
          heads->value_set(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION, "keep-alive", 10);
        } else {
          heads->value_set(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION, "keep-alive", 10);
        }
      }
      // NOTE: if the version is 1.1 we don't need to do
      //  anything since keep-alive is assumed
      break;
    case KA_DISABLED:
    case KA_CLOSE:
      if (s->current.server->keep_alive != HTTP_NO_KEEPALIVE || (ver == HTTPVersion(1, 1))) {
        /* Had keep-alive */
        s->current.server->keep_alive = HTTP_NO_KEEPALIVE;
        if (s->current.request_to == PARENT_PROXY) {
          heads->value_set(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION, "close", 5);
        } else {
          heads->value_set(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION, "close", 5);
        }
      }
      // Note: if we are 1.1, we always need to send the close
      //  header since persistant connnections are the default
      break;
    default:
      ink_assert(0);
      break;
    }
  } else { /* websocket connection */
    s->current.server->keep_alive = HTTP_NO_KEEPALIVE;
    s->client_info.keep_alive     = HTTP_NO_KEEPALIVE;
    heads->value_set(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION, MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE);

    if (s->is_websocket) {
      heads->value_set(MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE, "websocket", 9);
    }
  }
} /* End HttpTransact::handle_request_keep_alive_headers */

//////////////////////////////////////////////////////////////////////////////
//
//      void HttpTransact::handle_response_keep_alive_headers(
//          State* s, bool ka_on, HTTPVersion ver, HTTPHdr *heads)
//
//      Removes keep alive headers from origin server from <heads>
//
//      Adds the appropriate Transfer-Encoding: chunked header.
//
//      Adds the appropriate keep alive headers (if any) to <heads>
//      for keep-alive state <ka_on>, and HTTP version <ver>.
//
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_response_keep_alive_headers(State *s, HTTPVersion ver, HTTPHdr *heads)
{
  enum KA_Action_t {
    KA_UNKNOWN,
    KA_DISABLED,
    KA_CLOSE,
    KA_CONNECTION,
  };
  KA_Action_t ka_action = KA_UNKNOWN;

  ink_assert(heads->type_get() == HTTP_TYPE_RESPONSE);

  // Since connection headers are hop-to-hop, strip the
  //  the ones we received from upstream
  heads->field_delete(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);
  heads->field_delete(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION);

  // Handle the upgrade cases
  if (s->is_upgrade_request && heads->status_get() == HTTP_STATUS_SWITCHING_PROTOCOL && s->source == SOURCE_HTTP_ORIGIN_SERVER) {
    s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
    if (s->is_websocket) {
      TxnDebug("http_trans", "transaction successfully upgraded to websockets.");
      // s->transparent_passthrough = true;
      heads->value_set(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION, MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE);
      heads->value_set(MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE, "websocket", 9);
    }

    // We set this state so that we can jump to our blind forwarding state once
    // the response is sent to the client.
    s->did_upgrade_succeed = true;
    return;
  }

  int c_hdr_field_len;
  const char *c_hdr_field_str;
  if (s->client_info.proxy_connect_hdr) {
    c_hdr_field_str = MIME_FIELD_PROXY_CONNECTION;
    c_hdr_field_len = MIME_LEN_PROXY_CONNECTION;
  } else {
    c_hdr_field_str = MIME_FIELD_CONNECTION;
    c_hdr_field_len = MIME_LEN_CONNECTION;
  }

  // Check pre-conditions for keep-alive
  if (HTTP_MAJOR(ver.m_version) == 0) { /* No K-A for 0.9 apps */
    ka_action = KA_DISABLED;
  } else if (heads->status_get() == HTTP_STATUS_NO_CONTENT &&
             ((s->source == SOURCE_HTTP_ORIGIN_SERVER && s->current.server->transfer_encoding != NO_TRANSFER_ENCODING) ||
              heads->get_content_length() != 0)) {
    // some systems hang until the connection closes when receiving a 204 regardless of the K-A headers
    // close if there is any body response from the origin
    ka_action = KA_CLOSE;
  } else {
    // Determine if we are going to send either a server-generated or
    // proxy-generated chunked response to the client. If we cannot
    // trust the content-length, we may be able to chunk the response
    // to the client to keep the connection alive.
    // Insert a Transfer-Encoding header in the response if necessary.

    // check that the client is HTTP 1.1 and the conf allows chunking or the client
    // protocol unchunks before returning to the user agent (i.e. is http/2)
    if (s->client_info.http_version == HTTPVersion(1, 1) &&
        (s->txn_conf->chunking_enabled == 1 ||
         (s->state_machine->plugin_tag && (!strncmp(s->state_machine->plugin_tag, "http/2", 6)))) &&
        // if we're not sending a body, don't set a chunked header regardless of server response
        !is_response_body_precluded(s->hdr_info.client_response.status_get(), s->method) &&
        // we do not need chunked encoding for internal error messages
        // that are sent to the client if the server response is not valid.
        (((s->source == SOURCE_HTTP_ORIGIN_SERVER || s->source == SOURCE_TRANSFORM) && s->hdr_info.server_response.valid() &&
          // if we receive a 304, we will serve the client from the
          // cache and thus do not need chunked encoding.
          s->hdr_info.server_response.status_get() != HTTP_STATUS_NOT_MODIFIED &&
          (s->current.server->transfer_encoding == HttpTransact::CHUNKED_ENCODING ||
           // we can use chunked encoding if we cannot trust the content
           // length (e.g. no Content-Length and Connection:close in HTTP/1.1 responses)
           s->hdr_info.trust_response_cl == false)) ||
         // handle serve from cache (read-while-write) case
         (s->source == SOURCE_CACHE && s->hdr_info.trust_response_cl == false) ||
         // any transform will potentially alter the content length. try chunking if possible
         (s->source == SOURCE_TRANSFORM && s->hdr_info.trust_response_cl == false))) {
      s->client_info.receive_chunked_response = true;
      heads->value_append(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING, HTTP_VALUE_CHUNKED, HTTP_LEN_CHUNKED, true);
    } else {
      s->client_info.receive_chunked_response = false;
    }

    // make sure no content length header is send when transfer encoding is chunked
    if (s->client_info.receive_chunked_response) {
      s->hdr_info.trust_response_cl = false;

      // And delete the header if it's already been added...
      heads->field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
    }

    // Close the connection if client_info is not keep-alive.
    // Otherwise, if we cannot trust the content length, we will close the connection
    // unless we are going to use chunked encoding or the client issued
    // a PUSH request
    if (s->client_info.keep_alive != HTTP_KEEPALIVE) {
      ka_action = KA_DISABLED;
    } else if (s->hdr_info.trust_response_cl == false &&
               !(s->client_info.receive_chunked_response == true ||
                 (s->method == HTTP_WKSIDX_PUSH && s->client_info.keep_alive == HTTP_KEEPALIVE))) {
      ka_action = KA_CLOSE;
    } else {
      ka_action = KA_CONNECTION;
    }
  }

  ink_assert(ka_action != KA_UNKNOWN);

  // Insert K-A headers as necessary
  switch (ka_action) {
  case KA_CONNECTION:
    ink_assert(s->client_info.keep_alive != HTTP_NO_KEEPALIVE);
    // This is a hack, we send the keep-alive header for both 1.0
    // and 1.1, to be "compatible" with Akamai.
    // if (ver == HTTPVersion (1, 0)) {
    heads->value_set(c_hdr_field_str, c_hdr_field_len, "keep-alive", 10);
    // NOTE: if the version is 1.1 we don't need to do
    //  anything since keep-alive is assumed
    break;
  case KA_CLOSE:
  case KA_DISABLED:
    if (s->client_info.keep_alive != HTTP_NO_KEEPALIVE || (ver == HTTPVersion(1, 1))) {
      heads->value_set(c_hdr_field_str, c_hdr_field_len, "close", 5);
      s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
    }
    // Note: if we are 1.1, we always need to send the close
    //  header since persistant connnections are the default
    break;
  default:
    ink_assert(0);
    break;
  }
} /* End HttpTransact::handle_response_keep_alive_headers */

bool
HttpTransact::delete_all_document_alternates_and_return(State *s, bool cache_hit)
{
  if (cache_hit == true) {
    // ToDo: Should support other levels of cache hits here, but the cache does not support it (yet)
    if (SQUID_HIT_RAM == s->cache_info.hit_miss_code) {
      SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_RAM_CACHE_FRESH);
    } else {
      SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_FRESH);
    }
  } else {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_NOT_CACHED);
  }

  if ((s->method != HTTP_WKSIDX_GET) && (s->method == HTTP_WKSIDX_DELETE || s->method == HTTP_WKSIDX_PURGE)) {
    bool valid_max_forwards;
    int max_forwards          = -1;
    MIMEField *max_forwards_f = s->hdr_info.client_request.field_find(MIME_FIELD_MAX_FORWARDS, MIME_LEN_MAX_FORWARDS);

    // Check the max forwards value for DELETE
    if (max_forwards_f) {
      valid_max_forwards = true;
      max_forwards       = max_forwards_f->value_get_int();
    } else {
      valid_max_forwards = false;
    }

    if (s->method == HTTP_WKSIDX_PURGE || (valid_max_forwards && max_forwards <= 0)) {
      TxnDebug("http_trans",
               "[delete_all_document_alternates_and_return] "
               "DELETE with Max-Forwards: %d",
               max_forwards);

      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);

      // allow deletes to be pipelined
      //   We want to allow keep-alive so trust the response content
      //    length.  There really isn't one and the SM will add the
      //    zero content length when setting up the transfer
      s->hdr_info.trust_response_cl = true;
      build_response(s, &s->hdr_info.client_response, s->client_info.http_version,
                     (cache_hit == true) ? HTTP_STATUS_OK : HTTP_STATUS_NOT_FOUND);

      return true;
    } else {
      if (valid_max_forwards) {
        --max_forwards;
        TxnDebug("http_trans",
                 "[delete_all_document_alternates_and_return] "
                 "Decrementing max_forwards to %d",
                 max_forwards);
        s->hdr_info.client_request.value_set_int(MIME_FIELD_MAX_FORWARDS, MIME_LEN_MAX_FORWARDS, max_forwards);
      }
    }
  }

  return false;
}

bool
HttpTransact::does_client_request_permit_cached_response(const OverridableHttpConfigParams *p, CacheControlResult *c, HTTPHdr *h,
                                                         char *via_string)
{
  ////////////////////////////////////////////////////////////////////////
  // If aren't ignoring client's cache directives, meet client's wishes //
  ////////////////////////////////////////////////////////////////////////

  if (!c->ignore_client_no_cache) {
    if (h->is_cache_control_set(HTTP_VALUE_NO_CACHE)) {
      return (false);
    }
    if (h->is_pragma_no_cache_set()) {
      // if we are going to send out an ims anyway,
      // no need to flag this as a no-cache.
      if (!p->cache_ims_on_client_no_cache) {
        via_string[VIA_CLIENT_REQUEST] = VIA_CLIENT_NO_CACHE;
      }
      return (false);
    }
  }

  return (true);
}

bool
HttpTransact::does_client_request_permit_dns_caching(CacheControlResult *c, HTTPHdr *h)
{
  if (h->is_pragma_no_cache_set() && h->is_cache_control_set(HTTP_VALUE_NO_CACHE) && (!c->ignore_client_no_cache)) {
    return (false);
  }
  return (true);
}

bool
HttpTransact::does_client_request_permit_storing(CacheControlResult *c, HTTPHdr *h)
{
  ////////////////////////////////////////////////////////////////////////
  // If aren't ignoring client's cache directives, meet client's wishes //
  ////////////////////////////////////////////////////////////////////////
  if (!c->ignore_client_no_cache) {
    if (h->is_cache_control_set(HTTP_VALUE_NO_STORE)) {
      return (false);
    }
  }

  return (true);
}

int
HttpTransact::calculate_document_freshness_limit(State *s, HTTPHdr *response, time_t response_date, bool *heuristic)
{
  bool expires_set, date_set, last_modified_set;
  time_t date_value, expires_value, last_modified_value;
  MgmtInt min_freshness_bounds, max_freshness_bounds;
  int freshness_limit = 0;
  uint32_t cc_mask    = response->get_cooked_cc_mask();

  *heuristic = false;

  if (cc_mask & (MIME_COOKED_MASK_CC_S_MAXAGE | MIME_COOKED_MASK_CC_MAX_AGE)) {
    if (cc_mask & MIME_COOKED_MASK_CC_S_MAXAGE) {
      freshness_limit = (int)response->get_cooked_cc_s_maxage();
      TxnDebug("http_match", "calculate_document_freshness_limit --- s_max_age set, freshness_limit = %d", freshness_limit);
    } else if (cc_mask & MIME_COOKED_MASK_CC_MAX_AGE) {
      freshness_limit = (int)response->get_cooked_cc_max_age();
      TxnDebug("http_match", "calculate_document_freshness_limit --- max_age set, freshness_limit = %d", freshness_limit);
    }
    freshness_limit = std::min(std::max(0, freshness_limit), (int)s->txn_conf->cache_guaranteed_max_lifetime);
  } else {
    date_set = last_modified_set = false;

    if (s->plugin_set_expire_time != UNDEFINED_TIME) {
      expires_set   = true;
      expires_value = s->plugin_set_expire_time;
    } else {
      expires_set   = (response->presence(MIME_PRESENCE_EXPIRES) != 0);
      expires_value = response->get_expires();
    }

    date_value = response_date;
    if (date_value > 0) {
      date_set = true;
    } else {
      date_value = s->request_sent_time;
      TxnDebug("http_match",
               "calculate_document_freshness_limit --- Expires header = %" PRId64 " no date, using sent time %" PRId64,
               (int64_t)expires_value, (int64_t)date_value);
    }
    ink_assert(date_value > 0);

    // Getting the cache_sm object
    HttpCacheSM &cache_sm = s->state_machine->get_cache_sm();

    // Bypassing if loop to set freshness_limit to heuristic value
    if (expires_set && !cache_sm.is_readwhilewrite_inprogress()) {
      if (expires_value == UNDEFINED_TIME || expires_value <= date_value) {
        expires_value = date_value;
        TxnDebug("http_match", "calculate_document_freshness_limit --- no expires, using date %" PRId64, (int64_t)expires_value);
      }
      freshness_limit = (int)(expires_value - date_value);

      TxnDebug("http_match", "calculate_document_freshness_limit --- Expires: %" PRId64 ", Date: %" PRId64 ", freshness_limit = %d",
               (int64_t)expires_value, (int64_t)date_value, freshness_limit);

      freshness_limit = std::min(std::max(0, freshness_limit), (int)s->txn_conf->cache_guaranteed_max_lifetime);
    } else {
      last_modified_value = 0;
      if (response->presence(MIME_PRESENCE_LAST_MODIFIED)) {
        last_modified_set   = true;
        last_modified_value = response->get_last_modified();
        TxnDebug("http_match", "calculate_document_freshness_limit --- Last Modified header = %" PRId64,
                 (int64_t)last_modified_value);

        if (last_modified_value == UNDEFINED_TIME) {
          last_modified_set = false;
        } else if (last_modified_value > date_value) {
          last_modified_value = date_value;
          TxnDebug("http_match", "calculate_document_freshness_limit --- no last-modified, using sent time %" PRId64,
                   (int64_t)last_modified_value);
        }
      }

      *heuristic = true;
      if (date_set && last_modified_set) {
        MgmtFloat f = s->txn_conf->cache_heuristic_lm_factor;
        ink_assert((f >= 0.0) && (f <= 1.0));
        ink_time_t time_since_last_modify = date_value - last_modified_value;
        int h_freshness                   = (int)(time_since_last_modify * f);
        freshness_limit                   = std::max(h_freshness, 0);
        TxnDebug("http_match",
                 "calculate_document_freshness_limit --- heuristic: date=%" PRId64 ", lm=%" PRId64
                 ", time_since_last_modify=%" PRId64 ", f=%g, freshness_limit = %d",
                 (int64_t)date_value, (int64_t)last_modified_value, (int64_t)time_since_last_modify, f, freshness_limit);
      } else {
        freshness_limit = s->txn_conf->cache_heuristic_min_lifetime;
        TxnDebug("http_match", "calculate_document_freshness_limit --- heuristic: freshness_limit = %d", freshness_limit);
      }
    }
  }

  // The freshness limit must always fall within the min and max guaranteed bounds.
  min_freshness_bounds = std::max((MgmtInt)0, s->txn_conf->cache_guaranteed_min_lifetime);
  max_freshness_bounds = s->txn_conf->cache_guaranteed_max_lifetime;

  // Heuristic freshness can be more strict.
  if (*heuristic) {
    min_freshness_bounds = std::max(min_freshness_bounds, s->txn_conf->cache_heuristic_min_lifetime);
    max_freshness_bounds = std::min(max_freshness_bounds, s->txn_conf->cache_heuristic_max_lifetime);
  }
  // Now clip the freshness limit.
  if (freshness_limit > max_freshness_bounds) {
    freshness_limit = max_freshness_bounds;
  }
  if (freshness_limit < min_freshness_bounds) {
    freshness_limit = min_freshness_bounds;
  }

  TxnDebug("http_match", "calculate_document_freshness_limit --- final freshness_limit = %d", freshness_limit);

  return (freshness_limit);
}

//////////////////////////////////////////////////////////////////////////////
//
//
//      This function takes the request and response headers for a cached
//      object, and the current HTTP parameters, and decides if the object
//      is still "fresh enough" to serve.  One of the following values
//      is returned:
//
//          FRESHNESS_FRESH             Fresh enough, serve it
//          FRESHNESS_WARNING           Stale but client says it's okay
//          FRESHNESS_STALE             Too stale, don't use
//
//////////////////////////////////////////////////////////////////////////////
HttpTransact::Freshness_t
HttpTransact::what_is_document_freshness(State *s, HTTPHdr *client_request, HTTPHdr *cached_obj_response)
{
  bool heuristic, do_revalidate = false;
  int age_limit;
  int fresh_limit;
  ink_time_t current_age, response_date;
  uint32_t cc_mask, cooked_cc_mask;
  uint32_t os_specifies_revalidate;

  if (s->cache_open_write_fail_action & CACHE_WL_FAIL_ACTION_STALE_ON_REVALIDATE) {
    if (is_stale_cache_response_returnable(s)) {
      TxnDebug("http_match", "[what_is_document_freshness] cache_serve_stale_on_write_lock_fail, return FRESH");
      return (FRESHNESS_FRESH);
    }
  }

  //////////////////////////////////////////////////////
  // If config file has a ttl-in-cache field set,     //
  // it has priority over any other http headers and  //
  // other configuration parameters.                  //
  //////////////////////////////////////////////////////
  if (s->cache_control.ttl_in_cache > 0) {
    // what matters if ttl is set is not the age of the document
    // but for how long it has been stored in the cache (resident time)
    int resident_time = s->current.now - s->response_received_time;

    TxnDebug("http_match", "[..._document_freshness] ttl-in-cache = %d, resident time = %d", s->cache_control.ttl_in_cache,
             resident_time);
    if (resident_time > s->cache_control.ttl_in_cache) {
      return (FRESHNESS_STALE);
    } else {
      return (FRESHNESS_FRESH);
    }
  }

  cooked_cc_mask          = cached_obj_response->get_cooked_cc_mask();
  os_specifies_revalidate = cooked_cc_mask & (MIME_COOKED_MASK_CC_MUST_REVALIDATE | MIME_COOKED_MASK_CC_PROXY_REVALIDATE);
  cc_mask                 = MIME_COOKED_MASK_CC_NEED_REVALIDATE_ONCE;

  // Check to see if the server forces revalidation

  if ((cooked_cc_mask & cc_mask) && s->cache_control.revalidate_after <= 0) {
    TxnDebug("http_match", "[what_is_document_freshness] document stale due to "
                           "server must-revalidate");
    return FRESHNESS_STALE;
  }

  response_date = cached_obj_response->get_date();
  fresh_limit   = calculate_document_freshness_limit(s, cached_obj_response, response_date, &heuristic);
  ink_assert(fresh_limit >= 0);

  current_age = HttpTransactHeaders::calculate_document_age(s->request_sent_time, s->response_received_time, cached_obj_response,
                                                            response_date, s->current.now);

  // Overflow ?
  if (current_age < 0) {
    current_age = s->txn_conf->cache_guaranteed_max_lifetime;
  } else {
    current_age = std::min((time_t)s->txn_conf->cache_guaranteed_max_lifetime, current_age);
  }

  TxnDebug("http_match", "[what_is_document_freshness] fresh_limit:  %d  current_age: %" PRId64, fresh_limit, (int64_t)current_age);

  /////////////////////////////////////////////////////////
  // did the admin override the expiration calculations? //
  // (used only for http).                               //
  /////////////////////////////////////////////////////////
  ink_assert(client_request == &s->hdr_info.client_request);

  if (s->txn_conf->cache_when_to_revalidate == 0) {
    ;
    // Compute how fresh below
  } else if (client_request->url_get()->scheme_get_wksidx() == URL_WKSIDX_HTTP) {
    switch (s->txn_conf->cache_when_to_revalidate) {
    case 1: // Stale if heuristic
      if (heuristic) {
        TxnDebug("http_match", "[what_is_document_freshness] config requires FRESHNESS_STALE because heuristic calculation");
        return (FRESHNESS_STALE);
      }
      break;
    case 2: // Always stale
      TxnDebug("http_match", "[what_is_document_freshness] config "
                             "specifies always FRESHNESS_STALE");
      return (FRESHNESS_STALE);
    case 3: // Never stale
      TxnDebug("http_match", "[what_is_document_freshness] config "
                             "specifies always FRESHNESS_FRESH");
      return (FRESHNESS_FRESH);
    case 4: // Stale if IMS
      if (client_request->presence(MIME_PRESENCE_IF_MODIFIED_SINCE)) {
        TxnDebug("http_match", "[what_is_document_freshness] config "
                               "specifies FRESHNESS_STALE if IMS present");
        return (FRESHNESS_STALE);
      }
    default: // Bad config, ignore
      break;
    }
  }
  //////////////////////////////////////////////////////////////////////
  // the normal expiration policy allows serving a doc from cache if: //
  //     basic:          (current_age <= fresh_limit)                 //
  //                                                                  //
  // this can be modified by client Cache-Control headers:            //
  //     max-age:        (current_age <= max_age)                     //
  //     min-fresh:      (current_age <= fresh_limit - min_fresh)     //
  //     max-stale:      (current_age <= fresh_limit + max_stale)     //
  //////////////////////////////////////////////////////////////////////
  age_limit = fresh_limit; // basic constraint
  TxnDebug("http_match", "[..._document_freshness] initial age limit: %d", age_limit);

  cooked_cc_mask = client_request->get_cooked_cc_mask();
  cc_mask        = (MIME_COOKED_MASK_CC_MAX_STALE | MIME_COOKED_MASK_CC_MIN_FRESH | MIME_COOKED_MASK_CC_MAX_AGE);
  if (cooked_cc_mask & cc_mask) {
    /////////////////////////////////////////////////
    // if max-stale set, relax the freshness limit //
    /////////////////////////////////////////////////
    if (cooked_cc_mask & MIME_COOKED_MASK_CC_MAX_STALE) {
      if (os_specifies_revalidate) {
        TxnDebug("http_match", "[...document_freshness] OS specifies revalidation; "
                               "ignoring client's max-stale request...");
      } else {
        int max_stale_val = client_request->get_cooked_cc_max_stale();

        if (max_stale_val != INT_MAX) {
          age_limit += max_stale_val;
        } else {
          age_limit = max_stale_val;
        }
        TxnDebug("http_match", "[..._document_freshness] max-stale set, age limit: %d", age_limit);
      }
    }
    /////////////////////////////////////////////////////
    // if min-fresh set, constrain the freshness limit //
    /////////////////////////////////////////////////////
    if (cooked_cc_mask & MIME_COOKED_MASK_CC_MIN_FRESH) {
      age_limit = std::min(age_limit, fresh_limit - client_request->get_cooked_cc_min_fresh());
      TxnDebug("http_match", "[..._document_freshness] min_fresh set, age limit: %d", age_limit);
    }
    ///////////////////////////////////////////////////
    // if max-age set, constrain the freshness limit //
    ///////////////////////////////////////////////////
    if (!s->cache_control.ignore_client_cc_max_age && (cooked_cc_mask & MIME_COOKED_MASK_CC_MAX_AGE)) {
      int age_val = client_request->get_cooked_cc_max_age();
      if (age_val == 0) {
        do_revalidate = true;
      }
      age_limit = std::min(age_limit, age_val);
      TxnDebug("http_match", "[..._document_freshness] min_fresh set, age limit: %d", age_limit);
    }
  }
  /////////////////////////////////////////////////////////
  // config file may have a "revalidate_after" field set //
  /////////////////////////////////////////////////////////
  // bug fix changed ">0" to ">=0"
  if (s->cache_control.revalidate_after >= 0) {
    // if we want the minimum of the already-computed age_limit and revalidate_after
    //      age_limit = mine(age_limit, s->cache_control.revalidate_after);

    // if instead the revalidate_after overrides all other variables
    age_limit = s->cache_control.revalidate_after;

    TxnDebug("http_match", "[..._document_freshness] revalidate_after set, age limit: %d", age_limit);
  }

  TxnDebug("http_match", "document_freshness --- current_age = %" PRId64, (int64_t)current_age);
  TxnDebug("http_match", "document_freshness --- age_limit   = %d", age_limit);
  TxnDebug("http_match", "document_freshness --- fresh_limit = %d", fresh_limit);
  TxnDebug("http_seq", "document_freshness --- current_age = %" PRId64, (int64_t)current_age);
  TxnDebug("http_seq", "document_freshness --- age_limit   = %d", age_limit);
  TxnDebug("http_seq", "document_freshness --- fresh_limit = %d", fresh_limit);
  ///////////////////////////////////////////
  // now, see if the age is "fresh enough" //
  ///////////////////////////////////////////

  if (do_revalidate || current_age > age_limit) { // client-modified limit
    TxnDebug("http_match", "[..._document_freshness] document needs revalidate/too old; "
                           "returning FRESHNESS_STALE");
    return (FRESHNESS_STALE);
  } else if (current_age > fresh_limit) { // original limit
    if (os_specifies_revalidate) {
      TxnDebug("http_match", "[..._document_freshness] document is stale and OS specifies revalidation; "
                             "returning FRESHNESS_STALE");
      return (FRESHNESS_STALE);
    }
    TxnDebug("http_match", "[..._document_freshness] document is stale but no revalidation explicitly required; "
                           "returning FRESHNESS_WARNING");
    return (FRESHNESS_WARNING);
  } else {
    TxnDebug("http_match", "[..._document_freshness] document is fresh; returning FRESHNESS_FRESH");
    return (FRESHNESS_FRESH);
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//      HttpTransact::Authentication_t HttpTransact::AuthenticationNeeded(
//          const OverridableHttpConfigParams *p,
//          HTTPHdr *client_request,
//          HTTPHdr *obj_response)
//
//      This function takes the current client request, and the headers
//      from a potential response (e.g. from cache or proxy), and decides
//      if the object needs to be authenticated with the origin server,
//      before it can be sent to the client.
//
//      The return value describes the authentication process needed.  In
//      this function, three results are possible:
//
//          AUTHENTICATION_SUCCESS              Can serve object directly
//          AUTHENTICATION_MUST_REVALIDATE      Must revalidate with server
//          AUTHENTICATION_MUST_PROXY           Must not serve object
//
//////////////////////////////////////////////////////////////////////////////

HttpTransact::Authentication_t
HttpTransact::AuthenticationNeeded(const OverridableHttpConfigParams *p, HTTPHdr *client_request, HTTPHdr *obj_response)
{
  ///////////////////////////////////////////////////////////////////////
  // from RFC2068, sec 14.8, if a client request has the Authorization //
  // header set, we can't serve it unless the response is public, or   //
  // if it has a Cache-Control revalidate flag, and we do revalidate.  //
  ///////////////////////////////////////////////////////////////////////

  if ((p->cache_ignore_auth == 0) && client_request->presence(MIME_PRESENCE_AUTHORIZATION)) {
    if (obj_response->is_cache_control_set(HTTP_VALUE_MUST_REVALIDATE) ||
        obj_response->is_cache_control_set(HTTP_VALUE_PROXY_REVALIDATE)) {
      return AUTHENTICATION_MUST_REVALIDATE;
    } else if (obj_response->is_cache_control_set(HTTP_VALUE_PROXY_REVALIDATE)) {
      return AUTHENTICATION_MUST_REVALIDATE;
    } else if (obj_response->is_cache_control_set(HTTP_VALUE_PUBLIC)) {
      return AUTHENTICATION_SUCCESS;
    } else {
      if (obj_response->field_find("@WWW-Auth", 9) && client_request->method_get_wksidx() == HTTP_WKSIDX_GET) {
        return AUTHENTICATION_CACHE_AUTH;
      }
      return AUTHENTICATION_MUST_PROXY;
    }
  }

  if (obj_response->field_find("@WWW-Auth", 9) && client_request->method_get_wksidx() == HTTP_WKSIDX_GET) {
    return AUTHENTICATION_CACHE_AUTH;
  }

  return (AUTHENTICATION_SUCCESS);
}

void
HttpTransact::handle_parent_died(State *s)
{
  ink_assert(s->parent_result.result == PARENT_FAIL);

  build_error_response(s, HTTP_STATUS_BAD_GATEWAY, "Next Hop Connection Failed", "connect#failed_connect");
  TRANSACT_RETURN(SM_ACTION_SEND_ERROR_CACHE_NOOP, nullptr);
}

void
HttpTransact::handle_server_died(State *s)
{
  const char *reason    = nullptr;
  const char *body_type = "UNKNOWN";
  HTTPStatus status     = HTTP_STATUS_BAD_GATEWAY;

  ////////////////////////////////////////////////////////
  // FIX: all the body types below need to be filled in //
  ////////////////////////////////////////////////////////

  switch (s->current.state) {
  case CONNECTION_ALIVE: /* died while alive for unknown reason */
    ink_release_assert(s->hdr_info.response_error != NO_RESPONSE_HEADER_ERROR);
    status    = HTTP_STATUS_BAD_GATEWAY;
    reason    = "Unknown Error";
    body_type = "response#bad_response";
    break;
  case CONNECTION_ERROR:
    status    = HTTP_STATUS_BAD_GATEWAY;
    reason    = get_error_string(s->cause_of_death_errno == 0 ? -ENET_CONNECT_FAILED : s->cause_of_death_errno);
    body_type = "connect#failed_connect";
    break;
  case OPEN_RAW_ERROR:
    status    = HTTP_STATUS_BAD_GATEWAY;
    reason    = "Tunnel Connection Failed";
    body_type = "connect#failed_connect";
    break;
  case CONNECTION_CLOSED:
    status    = HTTP_STATUS_BAD_GATEWAY;
    reason    = "Server Hangup";
    body_type = "connect#hangup";
    break;
  case ACTIVE_TIMEOUT:
    if (s->api_txn_active_timeout_value != -1) {
      TxnDebug("http_timeout", "Maximum active time of %d msec exceeded", s->api_txn_active_timeout_value);
    }
    status    = HTTP_STATUS_GATEWAY_TIMEOUT;
    reason    = "Maximum Transaction Time Exceeded";
    body_type = "timeout#activity";
    break;
  case INACTIVE_TIMEOUT:
    if (s->api_txn_connect_timeout_value != -1) {
      TxnDebug("http_timeout", "Maximum connect time of %d msec exceeded", s->api_txn_connect_timeout_value);
    }
    status    = HTTP_STATUS_GATEWAY_TIMEOUT;
    reason    = "Connection Timed Out";
    body_type = "timeout#inactivity";
    break;
  case PARSE_ERROR:
  case BAD_INCOMING_RESPONSE:
    status    = HTTP_STATUS_BAD_GATEWAY;
    reason    = "Invalid HTTP Response";
    body_type = "response#bad_response";
    break;
  case STATE_UNDEFINED:
  case TRANSACTION_COMPLETE:
  default: /* unknown death */
    ink_release_assert(!"[handle_server_died] Unreasonable state - not dead, shouldn't be here");
    status    = HTTP_STATUS_BAD_GATEWAY;
    reason    = nullptr;
    body_type = "response#bad_response";
    break;
  }

  ////////////////////////////////////////////////////////
  // FIX: comment stuff above and below here, not clear //
  ////////////////////////////////////////////////////////

  switch (s->hdr_info.response_error) {
  case NON_EXISTANT_RESPONSE_HEADER:
    status    = HTTP_STATUS_BAD_GATEWAY;
    reason    = "No Response Header From Server";
    body_type = "response#bad_response";
    break;
  case MISSING_REASON_PHRASE:
  case NO_RESPONSE_HEADER_ERROR:
  case NOT_A_RESPONSE_HEADER:
#ifdef REALLY_NEED_TO_CHECK_DATE_VALIDITY
  case BOGUS_OR_NO_DATE_IN_RESPONSE:
#endif
    status    = HTTP_STATUS_BAD_GATEWAY;
    reason    = "Malformed Server Response";
    body_type = "response#bad_response";
    break;
  case MISSING_STATUS_CODE:
    status    = HTTP_STATUS_BAD_GATEWAY;
    reason    = "Malformed Server Response Status";
    body_type = "response#bad_response";
    break;
  default:
    break;
  }

  if (reason == nullptr) {
    status    = HTTP_STATUS_BAD_GATEWAY;
    reason    = "Server Connection Failed";
    body_type = "connect#failed_connect";
  }

  build_error_response(s, status, reason, body_type);

  return;
}

// return true if the response to the given request is likely cacheable
// This function is called by build_request() to determine if the conditional
// headers should be removed from server request.
bool
HttpTransact::is_request_likely_cacheable(State *s, HTTPHdr *request)
{
  if ((s->method == HTTP_WKSIDX_GET || s->api_req_cacheable) && !s->api_server_response_no_store &&
      !request->presence(MIME_PRESENCE_AUTHORIZATION) &&
      (!request->presence(MIME_PRESENCE_RANGE) || s->txn_conf->cache_range_write)) {
    return true;
  }
  return false;
}

void
HttpTransact::build_request(State *s, HTTPHdr *base_request, HTTPHdr *outgoing_request, HTTPVersion outgoing_version)
{
  // this part is to restore the original URL in case, multiple cache
  // lookups have happened - client request has been changed as the result
  //
  // notice that currently, based_request IS client_request
  if (base_request == &s->hdr_info.client_request) {
    if (s->redirect_info.redirect_in_process) {
      // this is for auto redirect
      URL *r_url = &s->redirect_info.redirect_url;

      ink_assert(r_url->valid());
      base_request->url_get()->copy(r_url);
    } else {
      // this is for multiple cache lookup
      URL *o_url = &s->cache_info.original_url;

      if (o_url->valid()) {
        base_request->url_get()->copy(o_url);
      }
    }

    // Peform any configured normalization (including per-remap-rule configuration overrides) of the Accept-Encoding header
    // field (if any).  This has to be done in the request from the client, for the benefit of the gzip plugin.
    //
    HttpTransactHeaders::normalize_accept_encoding(s->txn_conf, base_request);
  }

  HttpTransactHeaders::copy_header_fields(base_request, outgoing_request, s->txn_conf->fwd_proxy_auth_to_parent);
  add_client_ip_to_outgoing_request(s, outgoing_request);
  HttpTransactHeaders::add_forwarded_field_to_request(s, outgoing_request);
  HttpTransactHeaders::remove_privacy_headers_from_request(s->http_config_param, s->txn_conf, outgoing_request);
  HttpTransactHeaders::add_global_user_agent_header_to_request(s->txn_conf, outgoing_request);
  handle_request_keep_alive_headers(s, outgoing_version, outgoing_request);

  // handle_conditional_headers appears to be obsolete.  Nothing happens
  // unelss s->cache_info.action == HttpTransact::CACHE_DO_UPDATE.  In that
  // case an assert will go off.  The functionality of this method
  // (e.g., setting the if-modfied-since header occurs in issue_revalidate
  // HttpTransactHeaders::handle_conditional_headers(&s->cache_info, outgoing_request);

  if (s->next_hop_scheme < 0) {
    s->next_hop_scheme = URL_WKSIDX_HTTP;
  }
  if (s->orig_scheme < 0) {
    s->orig_scheme = URL_WKSIDX_HTTP;
  }

  if (s->txn_conf->insert_request_via_string) {
    HttpTransactHeaders::insert_via_header_in_request(s, outgoing_request);
  }

  // We build 1.1 request header and then convert as necessary to
  //  the appropriate version in HttpTransact::build_request
  outgoing_request->version_set(HTTPVersion(1, 1));

  // Make sure our request version is defined
  ink_assert(outgoing_version != HTTPVersion(0, 0));

  // HttpTransactHeaders::convert_request(outgoing_version, outgoing_request); // commented out this idea

  // Check whether a Host header field is missing from a 1.0 or 1.1 request.
  if (outgoing_version != HTTPVersion(0, 9) && !outgoing_request->presence(MIME_PRESENCE_HOST)) {
    URL *url = outgoing_request->url_get();
    int host_len;
    const char *host = url->host_get(&host_len);

    // Add a ':port' to the HOST header if the request is not going
    // to the default port.
    int port = url->port_get();
    if (port != url_canonicalize_port(URL_TYPE_HTTP, 0)) {
      char *buf = (char *)alloca(host_len + 15);
      memcpy(buf, host, host_len);
      host_len += snprintf(buf + host_len, 15, ":%d", port);
      outgoing_request->value_set(MIME_FIELD_HOST, MIME_LEN_HOST, buf, host_len);
    } else {
      outgoing_request->value_set(MIME_FIELD_HOST, MIME_LEN_HOST, host, host_len);
    }
  }

  // Figure out whether to force the outgoing request URL into absolute or relative styles.
  if (outgoing_request->method_get_wksidx() == HTTP_WKSIDX_CONNECT) {
    // CONNECT method requires a target in the URL, so always force it from the Host header.
    outgoing_request->set_url_target_from_host_field();
  } else if (s->current.request_to == PARENT_PROXY && s->parent_result.parent_is_proxy()) {
    // If we have a parent proxy set the URL target field.
    if (!outgoing_request->is_target_in_url()) {
      TxnDebug("http_trans", "[build_request] adding target to URL for parent proxy");
      outgoing_request->set_url_target_from_host_field();
    }
  } else if (s->next_hop_scheme == URL_WKSIDX_HTTP || s->next_hop_scheme == URL_WKSIDX_HTTPS ||
             s->next_hop_scheme == URL_WKSIDX_WS || s->next_hop_scheme == URL_WKSIDX_WSS) {
    // Otherwise, remove the URL target from HTTP and Websocket URLs since certain origins
    // cannot deal with absolute URLs.
    TxnDebug("http_trans", "[build_request] removing host name from url");
    HttpTransactHeaders::remove_host_name_from_url(outgoing_request);
  }

  // If the response is most likely not cacheable, eg, request with Authorization,
  // do we really want to remove conditional headers to get large 200 response?
  // Answer: NO.  Since if the response is most likely not cacheable,
  // we don't remove conditional headers so that for a non-200 response
  // from the O.S., we will save bandwidth between proxy and O.S.
  if (s->current.mode == GENERIC_PROXY) {
    if (is_request_likely_cacheable(s, base_request)) {
      if (s->txn_conf->cache_when_to_revalidate != 4) {
        TxnDebug("http_trans", "[build_request] "
                               "request like cacheable and conditional headers removed");
        HttpTransactHeaders::remove_conditional_headers(outgoing_request);
      } else {
        TxnDebug("http_trans", "[build_request] "
                               "request like cacheable but keep conditional headers");
      }
    } else {
      // In this case, we send a conditional request
      // instead of the normal non-conditional request.
      TxnDebug("http_trans", "[build_request] "
                             "request not like cacheable and conditional headers not removed");
    }
  }

  if (s->http_config_param->send_100_continue_response) {
    HttpTransactHeaders::remove_100_continue_headers(s, outgoing_request);
    TxnDebug("http_trans", "[build_request] request expect 100-continue headers removed");
  }

  s->request_sent_time = ink_local_time();
  s->current.now       = s->request_sent_time;

  // The assert is backwards in this case because request is being (re)sent.
  ink_assert(s->request_sent_time >= s->response_received_time);

  TxnDebug("http_trans", "[build_request] request_sent_time: %" PRId64, (int64_t)s->request_sent_time);
  DUMP_HEADER("http_hdrs", outgoing_request, s->state_machine_id, "Proxy's Request");

  HTTP_INCREMENT_DYN_STAT(http_outgoing_requests_stat);
}

// build a (status_code) response based upon the given info

void
HttpTransact::build_response(State *s, HTTPHdr *base_response, HTTPHdr *outgoing_response, HTTPVersion outgoing_version)
{
  build_response(s, base_response, outgoing_response, outgoing_version, HTTP_STATUS_NONE, nullptr);
  return;
}

void
HttpTransact::build_response(State *s, HTTPHdr *outgoing_response, HTTPVersion outgoing_version, HTTPStatus status_code,
                             const char *reason_phrase)
{
  build_response(s, nullptr, outgoing_response, outgoing_version, status_code, reason_phrase);
  return;
}

void
HttpTransact::build_response(State *s, HTTPHdr *base_response, HTTPHdr *outgoing_response, HTTPVersion outgoing_version,
                             HTTPStatus status_code, const char *reason_phrase)
{
  if (reason_phrase == nullptr) {
    reason_phrase = http_hdr_reason_lookup(status_code);
  }

  if (base_response == nullptr) {
    HttpTransactHeaders::build_base_response(outgoing_response, status_code, reason_phrase, strlen(reason_phrase), s->current.now);
  } else {
    if ((status_code == HTTP_STATUS_NONE) || (status_code == base_response->status_get())) {
      HttpTransactHeaders::copy_header_fields(base_response, outgoing_response, s->txn_conf->fwd_proxy_auth_to_parent);

      if (s->txn_conf->insert_age_in_response) {
        HttpTransactHeaders::insert_time_and_age_headers_in_response(s->request_sent_time, s->response_received_time,
                                                                     s->current.now, base_response, outgoing_response);
      }

      // Note: We need to handle the "Content-Length" header first here
      //  since handle_content_length_header()
      //  determines whether we accept origin server's content-length.
      //  We need to have made a decision regard the content-length
      //  before processing the keep_alive headers
      //
      handle_content_length_header(s, outgoing_response, base_response);
    } else {
      switch (status_code) {
      case HTTP_STATUS_NOT_MODIFIED:
        HttpTransactHeaders::build_base_response(outgoing_response, status_code, reason_phrase, strlen(reason_phrase),
                                                 s->current.now);

        // According to RFC 2616, Section 10.3.5,
        // a 304 response MUST contain Date header,
        // Etag and/or Content-location header,
        // and Expires, Cache-control, and Vary
        // (if they might be changed).
        // Since a proxy doesn't know if a header differs from
        // a user agent's cached document or not, all are sent.
        {
          static const struct {
            const char *name;
            int len;
            uint64_t presence;
          } fields[] = {
            {MIME_FIELD_ETAG, MIME_LEN_ETAG, MIME_PRESENCE_ETAG},
            {MIME_FIELD_CONTENT_LOCATION, MIME_LEN_CONTENT_LOCATION, MIME_PRESENCE_CONTENT_LOCATION},
            {MIME_FIELD_EXPIRES, MIME_LEN_EXPIRES, MIME_PRESENCE_EXPIRES},
            {MIME_FIELD_CACHE_CONTROL, MIME_LEN_CACHE_CONTROL, MIME_PRESENCE_CACHE_CONTROL},
            {MIME_FIELD_VARY, MIME_LEN_VARY, MIME_PRESENCE_VARY},
          };

          for (size_t i = 0; i < countof(fields); i++) {
            if (base_response->presence(fields[i].presence)) {
              MIMEField *field;
              int len;
              const char *value;

              field = base_response->field_find(fields[i].name, fields[i].len);
              ink_assert(field != nullptr);
              value = field->value_get(&len);
              outgoing_response->value_append(fields[i].name, fields[i].len, value, len, false);
            }
          }
        }
        break;

      case HTTP_STATUS_PRECONDITION_FAILED:
      // fall through
      case HTTP_STATUS_RANGE_NOT_SATISFIABLE:
        HttpTransactHeaders::build_base_response(outgoing_response, status_code, reason_phrase, strlen(reason_phrase),
                                                 s->current.now);
        break;
      default:
        // ink_assert(!"unexpected status code in build_response()");
        break;
      }
    }
  }

  // the following is done whether base_response == NULL or not

  // If the response is prohibited from containing a body,
  //  we know the content length is trustable for keep-alive
  if (is_response_body_precluded(status_code, s->method)) {
    s->hdr_info.trust_response_cl = true;
  }

  handle_response_keep_alive_headers(s, outgoing_version, outgoing_response);

  if (s->next_hop_scheme < 0) {
    s->next_hop_scheme = URL_WKSIDX_HTTP;
  }

  // Add HSTS header (Strict-Transport-Security) if max-age is set and the request was https
  // and the incoming request was remapped correctly
  if (s->orig_scheme == URL_WKSIDX_HTTPS && s->txn_conf->proxy_response_hsts_max_age >= 0 && s->url_remap_success == true) {
    TxnDebug("http_hdrs", "hsts max-age=%" PRId64, s->txn_conf->proxy_response_hsts_max_age);
    HttpTransactHeaders::insert_hsts_header_in_response(s, outgoing_response);
  }

  if (s->txn_conf->insert_response_via_string) {
    HttpTransactHeaders::insert_via_header_in_response(s, outgoing_response);
  }

  HttpTransactHeaders::convert_response(outgoing_version, outgoing_response);

  // process reverse mappings on the location header
  // TS-1364: do this regardless of response code
  response_url_remap(outgoing_response, s->state_machine->m_remap);

  if (s->http_config_param->enable_http_stats) {
    HttpTransactHeaders::generate_and_set_squid_codes(outgoing_response, s->via_string, &s->squid_codes);
  }

  HttpTransactHeaders::add_server_header_to_response(s->txn_conf, outgoing_response);

  if (s->state_machine->ua_txn && s->state_machine->ua_txn->get_parent()->is_draining()) {
    HttpTransactHeaders::add_connection_close(outgoing_response);
  }

  if (is_debug_tag_set("http_hdrs")) {
    if (base_response) {
      DUMP_HEADER("http_hdrs", base_response, s->state_machine_id, "Base Header for Building Response");
    }

    DUMP_HEADER("http_hdrs", outgoing_response, s->state_machine_id, "Proxy's Response 2");
  }

  return;
}

//////////////////////////////////////////////////////////////////////////////
//
//      void HttpTransact::build_error_response(
//          State *s,
//          HTTPStatus status_code,
//          char *reason_phrase_or_null,
//          char *error_body_type,
//          char *format, ...)
//
//      This method sets the requires state for an error reply, including
//      the error text, status code, reason phrase, and reply headers.  The
//      caller calls the method with the HttpTransact::State <s>, the
//      HTTP status code <status_code>, a user-specified reason phrase
//      string (or NULL) <reason_phrase_or_null>, and a printf-like
//      text format and arguments which are appended to the error text.
//
//      The <error_body_type> is the error message type, as specified by
//      the HttpBodyFactory customized error page system.
//
//      If the descriptive text <format> is not NULL or "", it is also
//      added to the error text body as descriptive text in the error body.
//      If <reason_phrase_or_null> is NULL, the default HTTP reason phrase
//      is used.  This routine DOES NOT check for buffer overflows.  The
//      caller should keep the messages small to be sure the error text
//      fits in the error buffer (ok, it's nasty, but at least I admit it!).
//
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::build_error_response(State *s, HTTPStatus status_code, const char *reason_phrase_or_null, const char *error_body_type)
{
  char body_language[256], body_type[256];

  if (nullptr == error_body_type) {
    error_body_type = "default";
  }

  // Make sure that if this error occured before we initailzied the state variables that we do now.
  initialize_state_variables_from_request(s, &s->hdr_info.client_request);

  //////////////////////////////////////////////////////
  //  If there is a request body, we must disable     //
  //  keep-alive to prevent the body being read as    //
  //  the next header (unless we've already drained   //
  //  which we do for NTLM auth)                      //
  //////////////////////////////////////////////////////
  if (status_code == HTTP_STATUS_REQUEST_TIMEOUT || s->hdr_info.client_request.get_content_length() != 0 ||
      s->client_info.transfer_encoding == HttpTransact::CHUNKED_ENCODING) {
    s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
  } else {
    // We don't have a request body.  Since we are
    //  generating the error, we know we can trust
    //  the content-length
    s->hdr_info.trust_response_cl = true;
  }
  // If transparent and the forward server connection looks unhappy don't
  // keep alive the ua connection.
  if ((s->state_machine->ua_txn && s->state_machine->ua_txn->is_outbound_transparent()) &&
      (status_code == HTTP_STATUS_INTERNAL_SERVER_ERROR || status_code == HTTP_STATUS_GATEWAY_TIMEOUT ||
       status_code == HTTP_STATUS_BAD_GATEWAY || status_code == HTTP_STATUS_SERVICE_UNAVAILABLE)) {
    s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
  }

  // If there is a parse error on reading the request it can leave reading the request stream in an undetermined state
  if (status_code == HTTP_STATUS_BAD_REQUEST) {
    s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
  }

  switch (status_code) {
  case HTTP_STATUS_BAD_REQUEST:
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_ERROR);
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_HEADER_SYNTAX);
    break;
  case HTTP_STATUS_BAD_GATEWAY:
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_CONNECTION);
    break;
  case HTTP_STATUS_GATEWAY_TIMEOUT:
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_TIMEOUT);
    break;
  case HTTP_STATUS_NOT_FOUND:
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_SERVER);
    break;
  case HTTP_STATUS_FORBIDDEN:
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_ERROR);
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_FORBIDDEN);
    break;
  case HTTP_STATUS_HTTPVER_NOT_SUPPORTED:
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_ERROR);
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_SERVER);
    break;
  case HTTP_STATUS_INTERNAL_SERVER_ERROR:
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_DNS_FAILURE);
    break;
  case HTTP_STATUS_MOVED_TEMPORARILY:
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_MOVED_TEMPORARILY);
    break;
  case HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED:
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_ERROR);
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_AUTHORIZATION);
    break;
  case HTTP_STATUS_UNAUTHORIZED:
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_ERROR);
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_AUTHORIZATION);
    break;
  default:
    break;
  }

  const char *reason_phrase = (reason_phrase_or_null ? reason_phrase_or_null : (char *)(http_hdr_reason_lookup(status_code)));
  if (unlikely(!reason_phrase)) {
    reason_phrase = "Unknown HTTP Status";

    // set the source to internal so that chunking is handled correctly
  }
  s->source = SOURCE_INTERNAL;
  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, status_code, reason_phrase);

  if (status_code == HTTP_STATUS_SERVICE_UNAVAILABLE) {
    int ret_tmp;
    int retry_after = 0;

    if (s->hdr_info.client_response.value_get(MIME_FIELD_RETRY_AFTER, MIME_LEN_RETRY_AFTER, &ret_tmp) != nullptr) {
      retry_after = ret_tmp;
    }
    s->congestion_control_crat = retry_after;
  }

  // Add a bunch of headers to make sure that caches between
  // the Traffic Server and the client do not cache the error
  // page.
  s->hdr_info.client_response.value_set(MIME_FIELD_CACHE_CONTROL, MIME_LEN_CACHE_CONTROL, "no-store", 8);
  // Make sure there are no Expires and Last-Modified headers.
  s->hdr_info.client_response.field_delete(MIME_FIELD_EXPIRES, MIME_LEN_EXPIRES);
  s->hdr_info.client_response.field_delete(MIME_FIELD_LAST_MODIFIED, MIME_LEN_LAST_MODIFIED);

  if ((status_code == HTTP_STATUS_PERMANENT_REDIRECT || status_code == HTTP_STATUS_TEMPORARY_REDIRECT ||
       status_code == HTTP_STATUS_MOVED_TEMPORARILY || status_code == HTTP_STATUS_MOVED_PERMANENTLY) &&
      s->remap_redirect) {
    s->hdr_info.client_response.value_set(MIME_FIELD_LOCATION, MIME_LEN_LOCATION, s->remap_redirect, strlen(s->remap_redirect));
  }

  ////////////////////////////////////////////////////////////////////
  // create the error message using the "body factory", which will  //
  // build a customized error message if available, or generate the //
  // old style internal defaults otherwise --- the body factory     //
  // supports language targeting using the Accept-Language header   //
  ////////////////////////////////////////////////////////////////////

  int64_t len;
  char *new_msg;

  new_msg = body_factory->fabricate_with_old_api(error_body_type, s, 8192, &len, body_language, sizeof(body_language), body_type,
                                                 sizeof(body_type), s->internal_msg_buffer_size,
                                                 s->internal_msg_buffer_size ? s->internal_msg_buffer : nullptr);

  // After the body factory is called, a new "body" is allocated, and we must replace it. It is
  // unfortunate that there's no way to avoid this fabrication even when there is no substitutions...
  s->free_internal_msg_buffer();
  if (len == 0) {
    // If the file is empty, we may have a malloc(1) buffer. Release it.
    new_msg = (char *)ats_free_null(new_msg);
  }
  s->internal_msg_buffer                     = new_msg;
  s->internal_msg_buffer_size                = len;
  s->internal_msg_buffer_fast_allocator_size = -1;

  if (len > 0) {
    s->hdr_info.client_response.value_set(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, body_type, strlen(body_type));
    s->hdr_info.client_response.value_set(MIME_FIELD_CONTENT_LANGUAGE, MIME_LEN_CONTENT_LANGUAGE, body_language,
                                          strlen(body_language));
  } else {
    s->hdr_info.client_response.field_delete(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);
    s->hdr_info.client_response.field_delete(MIME_FIELD_CONTENT_LANGUAGE, MIME_LEN_CONTENT_LANGUAGE);
  }

  s->next_action = SM_ACTION_SEND_ERROR_CACHE_NOOP;
  return;
}

void
HttpTransact::build_redirect_response(State *s)
{
  TxnDebug("http_redirect", "[HttpTransact::build_redirect_response]");
  URL *u;
  const char *old_host;
  int old_host_len;
  const char *new_url = nullptr;
  int new_url_len;
  char *to_free = nullptr;

  HTTPStatus status_code = HTTP_STATUS_MOVED_TEMPORARILY;
  char *reason_phrase    = (char *)(http_hdr_reason_lookup(status_code));

  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, status_code, reason_phrase);

  //////////////////////////////////////////////////////////
  // figure out what new url should be.  this little hack //
  // inserts expanded hostname into old url in order to   //
  // get scheme information, then puts the old url back.  //
  //////////////////////////////////////////////////////////
  u        = s->hdr_info.client_request.url_get();
  old_host = u->host_get(&old_host_len);
  u->host_set(s->dns_info.lookup_name, strlen(s->dns_info.lookup_name));
  new_url = to_free = u->string_get(&s->arena, &new_url_len);
  if (new_url == nullptr) {
    new_url = "";
  }
  u->host_set(old_host, old_host_len);

  //////////////////////////
  // set redirect headers //
  //////////////////////////
  HTTPHdr *h = &s->hdr_info.client_response;
  if (s->txn_conf->insert_response_via_string) {
    const char pa[] = "Proxy-agent";

    h->value_append(pa, sizeof(pa) - 1, s->http_config_param->proxy_response_via_string,
                    s->http_config_param->proxy_response_via_string_len);
  }
  h->value_set(MIME_FIELD_LOCATION, MIME_LEN_LOCATION, new_url, new_url_len);

  //////////////////////////
  // set descriptive text //
  //////////////////////////
  s->free_internal_msg_buffer();
  s->internal_msg_buffer_fast_allocator_size = -1;
  // template redirect#temporarily can not be used here since there is no way to pass the computed url to the template.
  s->internal_msg_buffer = body_factory->getFormat(8192, &s->internal_msg_buffer_size, "%s <a href=\"%s\">%s</a>.  %s.",
                                                   "The document you requested is now", new_url, new_url,
                                                   "Please update your documents and bookmarks accordingly", nullptr);

  h->set_content_length(s->internal_msg_buffer_size);
  h->value_set(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, "text/html", 9);

  s->arena.str_free(to_free);
}

void
HttpTransact::build_upgrade_response(State *s)
{
  TxnDebug("http_upgrade", "[HttpTransact::build_upgrade_response]");

  // 101 Switching Protocols
  HTTPStatus status_code    = HTTP_STATUS_SWITCHING_PROTOCOL;
  const char *reason_phrase = http_hdr_reason_lookup(status_code);
  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, status_code, reason_phrase);

  //////////////////////////
  // set upgrade headers  //
  //////////////////////////
  HTTPHdr *h = &s->hdr_info.client_response;
  h->value_set(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION, "Upgrade", strlen("Upgrade"));
  h->value_set(MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE, MIME_UPGRADE_H2C_TOKEN, strlen(MIME_UPGRADE_H2C_TOKEN));
}

const char *
HttpTransact::get_error_string(int erno)
{
  if (erno >= 0) {
    return (strerror(erno));
  } else {
    switch (-erno) {
    case ENET_THROTTLING:
      return ("throttling");
    case ESOCK_DENIED:
      return ("socks error - denied");
    case ESOCK_TIMEOUT:
      return ("socks error - timeout");
    case ESOCK_NO_SOCK_SERVER_CONN:
      return ("socks error - no server connection");
    //              this assumes that the following case occurs
    //              when HttpSM.cc::state_origin_server_read_response
    //                 receives an HTTP_EVENT_EOS. (line 1729 in HttpSM.cc,
    //                 version 1.145.2.13.2.57)
    case ENET_CONNECT_FAILED:
      return ("connect failed");
    case UNKNOWN_INTERNAL_ERROR:
      return ("internal error - server connection terminated");
    default:
      return ("");
    }
  }
}

ink_time_t
ink_local_time()
{
  return Thread::get_hrtime() / HRTIME_SECOND;
}

//
// The stat functions
//
void
HttpTransact::histogram_response_document_size(State *s, int64_t doc_size)
{
  if (doc_size >= 0 && doc_size <= 100) {
    HTTP_INCREMENT_DYN_STAT(http_response_document_size_100_stat);
  } else if (doc_size <= 1024) {
    HTTP_INCREMENT_DYN_STAT(http_response_document_size_1K_stat);
  } else if (doc_size <= 3072) {
    HTTP_INCREMENT_DYN_STAT(http_response_document_size_3K_stat);
  } else if (doc_size <= 5120) {
    HTTP_INCREMENT_DYN_STAT(http_response_document_size_5K_stat);
  } else if (doc_size <= 10240) {
    HTTP_INCREMENT_DYN_STAT(http_response_document_size_10K_stat);
  } else if (doc_size <= 1048576) {
    HTTP_INCREMENT_DYN_STAT(http_response_document_size_1M_stat);
  } else {
    HTTP_INCREMENT_DYN_STAT(http_response_document_size_inf_stat);
  }
  return;
}

void
HttpTransact::histogram_request_document_size(State *s, int64_t doc_size)
{
  if (doc_size >= 0 && doc_size <= 100) {
    HTTP_INCREMENT_DYN_STAT(http_request_document_size_100_stat);
  } else if (doc_size <= 1024) {
    HTTP_INCREMENT_DYN_STAT(http_request_document_size_1K_stat);
  } else if (doc_size <= 3072) {
    HTTP_INCREMENT_DYN_STAT(http_request_document_size_3K_stat);
  } else if (doc_size <= 5120) {
    HTTP_INCREMENT_DYN_STAT(http_request_document_size_5K_stat);
  } else if (doc_size <= 10240) {
    HTTP_INCREMENT_DYN_STAT(http_request_document_size_10K_stat);
  } else if (doc_size <= 1048576) {
    HTTP_INCREMENT_DYN_STAT(http_request_document_size_1M_stat);
  } else {
    HTTP_INCREMENT_DYN_STAT(http_request_document_size_inf_stat);
  }
  return;
}

void
HttpTransact::user_agent_connection_speed(State *s, ink_hrtime transfer_time, int64_t nbytes)
{
  float bytes_per_hrtime = (transfer_time == 0) ? (nbytes) : ((float)nbytes / (float)(int64_t)transfer_time);
  int bytes_per_sec      = (int)(bytes_per_hrtime * HRTIME_SECOND);

  if (bytes_per_sec <= 100) {
    HTTP_INCREMENT_DYN_STAT(http_user_agent_speed_bytes_per_sec_100_stat);
  } else if (bytes_per_sec <= 1024) {
    HTTP_INCREMENT_DYN_STAT(http_user_agent_speed_bytes_per_sec_1K_stat);
  } else if (bytes_per_sec <= 10240) {
    HTTP_INCREMENT_DYN_STAT(http_user_agent_speed_bytes_per_sec_10K_stat);
  } else if (bytes_per_sec <= 102400) {
    HTTP_INCREMENT_DYN_STAT(http_user_agent_speed_bytes_per_sec_100K_stat);
  } else if (bytes_per_sec <= 1048576) {
    HTTP_INCREMENT_DYN_STAT(http_user_agent_speed_bytes_per_sec_1M_stat);
  } else if (bytes_per_sec <= 10485760) {
    HTTP_INCREMENT_DYN_STAT(http_user_agent_speed_bytes_per_sec_10M_stat);
  } else {
    HTTP_INCREMENT_DYN_STAT(http_user_agent_speed_bytes_per_sec_100M_stat);
  }

  return;
}

/*
 * added request_process_time stat for loadshedding foo
 */
void
HttpTransact::client_result_stat(State *s, ink_hrtime total_time, ink_hrtime request_process_time)
{
  ClientTransactionResult_t client_transaction_result = CLIENT_TRANSACTION_RESULT_UNDEFINED;

  ///////////////////////////////////////////////////////
  // don't count errors we generated as hits or misses //
  ///////////////////////////////////////////////////////
  if ((s->source == SOURCE_INTERNAL) && (s->hdr_info.client_response.status_get() >= 400)) {
    client_transaction_result = CLIENT_TRANSACTION_RESULT_ERROR_OTHER;
  }

  switch (s->squid_codes.log_code) {
  case SQUID_LOG_ERR_CONNECT_FAIL:
    HTTP_INCREMENT_DYN_STAT(http_cache_miss_cold_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_ERROR_CONNECT_FAIL;
    break;

  case SQUID_LOG_TCP_MEM_HIT:
    HTTP_INCREMENT_DYN_STAT(http_cache_hit_mem_fresh_stat);
    // fallthrough

  case SQUID_LOG_TCP_HIT:
    // It's possible to have two stat's instead of one, if needed.
    HTTP_INCREMENT_DYN_STAT(http_cache_hit_fresh_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_HIT_FRESH;
    break;

  case SQUID_LOG_TCP_REFRESH_HIT:
    HTTP_INCREMENT_DYN_STAT(http_cache_hit_reval_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_HIT_REVALIDATED;
    break;

  case SQUID_LOG_TCP_IMS_HIT:
    HTTP_INCREMENT_DYN_STAT(http_cache_hit_ims_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_HIT_FRESH;
    break;

  case SQUID_LOG_TCP_REF_FAIL_HIT:
    HTTP_INCREMENT_DYN_STAT(http_cache_hit_stale_served_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_HIT_FRESH;
    break;

  case SQUID_LOG_TCP_MISS:
    if ((GET_VIA_STRING(VIA_CACHE_RESULT) == VIA_IN_CACHE_NOT_ACCEPTABLE) || (GET_VIA_STRING(VIA_CACHE_RESULT) == VIA_CACHE_MISS)) {
      HTTP_INCREMENT_DYN_STAT(http_cache_miss_cold_stat);
      client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_COLD;
    } else {
      // FIX: what case is this for?  can it ever happen?
      HTTP_INCREMENT_DYN_STAT(http_cache_miss_uncacheable_stat);
      client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_UNCACHABLE;
    }
    break;

  case SQUID_LOG_TCP_REFRESH_MISS:
    HTTP_INCREMENT_DYN_STAT(http_cache_miss_changed_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_CHANGED;
    break;

  case SQUID_LOG_TCP_CLIENT_REFRESH:
    HTTP_INCREMENT_DYN_STAT(http_cache_miss_client_no_cache_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_CLIENT_NO_CACHE;
    break;

  case SQUID_LOG_TCP_IMS_MISS:
    HTTP_INCREMENT_DYN_STAT(http_cache_miss_ims_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_COLD;
    break;

  case SQUID_LOG_TCP_SWAPFAIL:
    HTTP_INCREMENT_DYN_STAT(http_cache_read_error_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_HIT_FRESH;
    break;

  case SQUID_LOG_ERR_READ_TIMEOUT:
  case SQUID_LOG_TCP_DENIED:
    // No cache result due to error
    client_transaction_result = CLIENT_TRANSACTION_RESULT_ERROR_OTHER;
    break;

  default:
    // FIX: What is the conditional below doing?
    //          if (s->local_trans_stats[http_cache_lookups_stat].count == 1L)
    //              HTTP_INCREMENT_DYN_STAT(http_cache_miss_cold_stat);

    // FIX: I suspect the following line should not be set here,
    //      because it overrides the error classification above.
    //      Commenting out.
    // client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_COLD;

    break;
  }

  //////////////////////////////////////////
  // don't count aborts as hits or misses //
  //////////////////////////////////////////
  if (s->client_info.abort == ABORTED) {
    client_transaction_result = CLIENT_TRANSACTION_RESULT_ERROR_ABORT;
  } else if (s->client_info.abort == MAYBE_ABORTED) {
    client_transaction_result = CLIENT_TRANSACTION_RESULT_ERROR_POSSIBLE_ABORT;
  }
  // Count the status codes, assuming the client didn't abort (i.e. there is an m_http)
  if ((s->source != SOURCE_NONE) && (s->client_info.abort == DIDNOT_ABORT)) {
    int status_code = s->hdr_info.client_response.status_get();

    switch (status_code) {
    case 100:
      HTTP_INCREMENT_DYN_STAT(http_response_status_100_count_stat);
      break;
    case 101:
      HTTP_INCREMENT_DYN_STAT(http_response_status_101_count_stat);
      break;
    case 200:
      HTTP_INCREMENT_DYN_STAT(http_response_status_200_count_stat);
      break;
    case 201:
      HTTP_INCREMENT_DYN_STAT(http_response_status_201_count_stat);
      break;
    case 202:
      HTTP_INCREMENT_DYN_STAT(http_response_status_202_count_stat);
      break;
    case 203:
      HTTP_INCREMENT_DYN_STAT(http_response_status_203_count_stat);
      break;
    case 204:
      HTTP_INCREMENT_DYN_STAT(http_response_status_204_count_stat);
      break;
    case 205:
      HTTP_INCREMENT_DYN_STAT(http_response_status_205_count_stat);
      break;
    case 206:
      HTTP_INCREMENT_DYN_STAT(http_response_status_206_count_stat);
      break;
    case 300:
      HTTP_INCREMENT_DYN_STAT(http_response_status_300_count_stat);
      break;
    case 301:
      HTTP_INCREMENT_DYN_STAT(http_response_status_301_count_stat);
      break;
    case 302:
      HTTP_INCREMENT_DYN_STAT(http_response_status_302_count_stat);
      break;
    case 303:
      HTTP_INCREMENT_DYN_STAT(http_response_status_303_count_stat);
      break;
    case 304:
      HTTP_INCREMENT_DYN_STAT(http_response_status_304_count_stat);
      break;
    case 305:
      HTTP_INCREMENT_DYN_STAT(http_response_status_305_count_stat);
      break;
    case 307:
      HTTP_INCREMENT_DYN_STAT(http_response_status_307_count_stat);
      break;
    case 400:
      HTTP_INCREMENT_DYN_STAT(http_response_status_400_count_stat);
      break;
    case 401:
      HTTP_INCREMENT_DYN_STAT(http_response_status_401_count_stat);
      break;
    case 402:
      HTTP_INCREMENT_DYN_STAT(http_response_status_402_count_stat);
      break;
    case 403:
      HTTP_INCREMENT_DYN_STAT(http_response_status_403_count_stat);
      break;
    case 404:
      HTTP_INCREMENT_DYN_STAT(http_response_status_404_count_stat);
      break;
    case 405:
      HTTP_INCREMENT_DYN_STAT(http_response_status_405_count_stat);
      break;
    case 406:
      HTTP_INCREMENT_DYN_STAT(http_response_status_406_count_stat);
      break;
    case 407:
      HTTP_INCREMENT_DYN_STAT(http_response_status_407_count_stat);
      break;
    case 408:
      HTTP_INCREMENT_DYN_STAT(http_response_status_408_count_stat);
      break;
    case 409:
      HTTP_INCREMENT_DYN_STAT(http_response_status_409_count_stat);
      break;
    case 410:
      HTTP_INCREMENT_DYN_STAT(http_response_status_410_count_stat);
      break;
    case 411:
      HTTP_INCREMENT_DYN_STAT(http_response_status_411_count_stat);
      break;
    case 412:
      HTTP_INCREMENT_DYN_STAT(http_response_status_412_count_stat);
      break;
    case 413:
      HTTP_INCREMENT_DYN_STAT(http_response_status_413_count_stat);
      break;
    case 414:
      HTTP_INCREMENT_DYN_STAT(http_response_status_414_count_stat);
      break;
    case 415:
      HTTP_INCREMENT_DYN_STAT(http_response_status_415_count_stat);
      break;
    case 416:
      HTTP_INCREMENT_DYN_STAT(http_response_status_416_count_stat);
      break;
    case 500:
      HTTP_INCREMENT_DYN_STAT(http_response_status_500_count_stat);
      break;
    case 501:
      HTTP_INCREMENT_DYN_STAT(http_response_status_501_count_stat);
      break;
    case 502:
      HTTP_INCREMENT_DYN_STAT(http_response_status_502_count_stat);
      break;
    case 503:
      HTTP_INCREMENT_DYN_STAT(http_response_status_503_count_stat);
      break;
    case 504:
      HTTP_INCREMENT_DYN_STAT(http_response_status_504_count_stat);
      break;
    case 505:
      HTTP_INCREMENT_DYN_STAT(http_response_status_505_count_stat);
      break;
    default:
      break;
    }
    switch (status_code / 100) {
    case 1:
      HTTP_INCREMENT_DYN_STAT(http_response_status_1xx_count_stat);
      break;
    case 2:
      HTTP_INCREMENT_DYN_STAT(http_response_status_2xx_count_stat);
      break;
    case 3:
      HTTP_INCREMENT_DYN_STAT(http_response_status_3xx_count_stat);
      break;
    case 4:
      HTTP_INCREMENT_DYN_STAT(http_response_status_4xx_count_stat);
      break;
    case 5:
      HTTP_INCREMENT_DYN_STAT(http_response_status_5xx_count_stat);
      break;
    default:
      break;
    }
  }

  // Increment the completed connection count
  HTTP_INCREMENT_DYN_STAT(http_completed_requests_stat);

  // Set the stat now that we know what happend
  ink_hrtime total_msec   = ink_hrtime_to_msec(total_time);
  ink_hrtime process_msec = ink_hrtime_to_msec(request_process_time);
  switch (client_transaction_result) {
  case CLIENT_TRANSACTION_RESULT_HIT_FRESH:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_hit_fresh_stat, total_msec);
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_hit_fresh_process_stat, process_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_HIT_REVALIDATED:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_hit_reval_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_MISS_COLD:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_miss_cold_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_MISS_CHANGED:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_miss_changed_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_MISS_CLIENT_NO_CACHE:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_miss_client_no_cache_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_MISS_UNCACHABLE:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_miss_uncacheable_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_ERROR_ABORT:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_errors_aborts_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_ERROR_POSSIBLE_ABORT:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_errors_possible_aborts_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_ERROR_CONNECT_FAIL:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_errors_connect_failed_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_ERROR_OTHER:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_errors_other_stat, total_msec);
    break;
  default:
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_other_unclassified_stat, total_msec);
    // This can happen if a plugin manually sets the status code after an error.
    TxnDebug("http", "Unclassified statistic");
    break;
  }
}

void
HttpTransact::origin_server_connection_speed(State *s, ink_hrtime transfer_time, int64_t nbytes)
{
  float bytes_per_hrtime = (transfer_time == 0) ? (nbytes) : ((float)nbytes / (float)(int64_t)transfer_time);
  int bytes_per_sec      = (int)(bytes_per_hrtime * HRTIME_SECOND);

  if (bytes_per_sec <= 100) {
    HTTP_INCREMENT_DYN_STAT(http_origin_server_speed_bytes_per_sec_100_stat);
  } else if (bytes_per_sec <= 1024) {
    HTTP_INCREMENT_DYN_STAT(http_origin_server_speed_bytes_per_sec_1K_stat);
  } else if (bytes_per_sec <= 10240) {
    HTTP_INCREMENT_DYN_STAT(http_origin_server_speed_bytes_per_sec_10K_stat);
  } else if (bytes_per_sec <= 102400) {
    HTTP_INCREMENT_DYN_STAT(http_origin_server_speed_bytes_per_sec_100K_stat);
  } else if (bytes_per_sec <= 1048576) {
    HTTP_INCREMENT_DYN_STAT(http_origin_server_speed_bytes_per_sec_1M_stat);
  } else if (bytes_per_sec <= 10485760) {
    HTTP_INCREMENT_DYN_STAT(http_origin_server_speed_bytes_per_sec_10M_stat);
  } else {
    HTTP_INCREMENT_DYN_STAT(http_origin_server_speed_bytes_per_sec_100M_stat);
  }

  return;
}

void
HttpTransact::update_size_and_time_stats(State *s, ink_hrtime total_time, ink_hrtime user_agent_write_time,
                                         ink_hrtime origin_server_read_time, int user_agent_request_header_size,
                                         int64_t user_agent_request_body_size, int user_agent_response_header_size,
                                         int64_t user_agent_response_body_size, int origin_server_request_header_size,
                                         int64_t origin_server_request_body_size, int origin_server_response_header_size,
                                         int64_t origin_server_response_body_size, int pushed_response_header_size,
                                         int64_t pushed_response_body_size, const TransactionMilestones &milestones)
{
  int64_t user_agent_request_size  = user_agent_request_header_size + user_agent_request_body_size;
  int64_t user_agent_response_size = user_agent_response_header_size + user_agent_response_body_size;
  int64_t user_agent_bytes         = user_agent_request_size + user_agent_response_size;

  int64_t origin_server_request_size  = origin_server_request_header_size + origin_server_request_body_size;
  int64_t origin_server_response_size = origin_server_response_header_size + origin_server_response_body_size;
  int64_t origin_server_bytes         = origin_server_request_size + origin_server_response_size;

  // Background fill stats
  switch (s->state_machine->background_fill) {
  case BACKGROUND_FILL_COMPLETED: {
    int64_t bg_size = origin_server_response_body_size - user_agent_response_body_size;
    bg_size         = std::max((int64_t)0, bg_size);
    HTTP_SUM_DYN_STAT(http_background_fill_bytes_completed_stat, bg_size);
    break;
  }
  case BACKGROUND_FILL_ABORTED: {
    int64_t bg_size = origin_server_response_body_size - user_agent_response_body_size;

    if (bg_size < 0) {
      bg_size = 0;
    }
    HTTP_SUM_DYN_STAT(http_background_fill_bytes_aborted_stat, bg_size);
    break;
  }
  case BACKGROUND_FILL_NONE:
    break;
  case BACKGROUND_FILL_STARTED:
  default:
    ink_assert(0);
  }

  // Bandwidth Savings
  switch (s->squid_codes.log_code) {
  case SQUID_LOG_TCP_HIT:
  case SQUID_LOG_TCP_MEM_HIT:
    // It's possible to have two stat's instead of one, if needed.
    HTTP_INCREMENT_DYN_STAT(http_tcp_hit_count_stat);
    HTTP_SUM_DYN_STAT(http_tcp_hit_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_tcp_hit_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_MISS:
    HTTP_INCREMENT_DYN_STAT(http_tcp_miss_count_stat);
    HTTP_SUM_DYN_STAT(http_tcp_miss_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_tcp_miss_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_EXPIRED_MISS:
    HTTP_INCREMENT_DYN_STAT(http_tcp_expired_miss_count_stat);
    HTTP_SUM_DYN_STAT(http_tcp_expired_miss_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_tcp_expired_miss_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_REFRESH_HIT:
    HTTP_INCREMENT_DYN_STAT(http_tcp_refresh_hit_count_stat);
    HTTP_SUM_DYN_STAT(http_tcp_refresh_hit_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_tcp_refresh_hit_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_REFRESH_MISS:
    HTTP_INCREMENT_DYN_STAT(http_tcp_refresh_miss_count_stat);
    HTTP_SUM_DYN_STAT(http_tcp_refresh_miss_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_tcp_refresh_miss_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_CLIENT_REFRESH:
    HTTP_INCREMENT_DYN_STAT(http_tcp_client_refresh_count_stat);
    HTTP_SUM_DYN_STAT(http_tcp_client_refresh_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_tcp_client_refresh_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_IMS_HIT:
    HTTP_INCREMENT_DYN_STAT(http_tcp_ims_hit_count_stat);
    HTTP_SUM_DYN_STAT(http_tcp_ims_hit_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_tcp_ims_hit_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_IMS_MISS:
    HTTP_INCREMENT_DYN_STAT(http_tcp_ims_miss_count_stat);
    HTTP_SUM_DYN_STAT(http_tcp_ims_miss_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_tcp_ims_miss_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_ERR_CLIENT_ABORT:
    HTTP_INCREMENT_DYN_STAT(http_err_client_abort_count_stat);
    HTTP_SUM_DYN_STAT(http_err_client_abort_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_err_client_abort_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_ERR_CLIENT_READ_ERROR:
    HTTP_INCREMENT_DYN_STAT(http_err_client_read_error_count_stat);
    HTTP_SUM_DYN_STAT(http_err_client_read_error_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_err_client_read_error_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_ERR_CONNECT_FAIL:
    HTTP_INCREMENT_DYN_STAT(http_err_connect_fail_count_stat);
    HTTP_SUM_DYN_STAT(http_err_connect_fail_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_err_connect_fail_origin_server_bytes_stat, origin_server_bytes);
    break;
  default:
    HTTP_INCREMENT_DYN_STAT(http_misc_count_stat);
    HTTP_SUM_DYN_STAT(http_misc_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_DYN_STAT(http_misc_origin_server_bytes_stat, origin_server_bytes);
    break;
  }

  // times
  HTTP_SUM_DYN_STAT(http_total_transactions_time_stat, total_time);

  // sizes
  HTTP_SUM_DYN_STAT(http_user_agent_request_header_total_size_stat, user_agent_request_header_size);
  HTTP_SUM_DYN_STAT(http_user_agent_response_header_total_size_stat, user_agent_response_header_size);
  HTTP_SUM_DYN_STAT(http_user_agent_request_document_total_size_stat, user_agent_request_body_size);
  HTTP_SUM_DYN_STAT(http_user_agent_response_document_total_size_stat, user_agent_response_body_size);

  // proxy stats
  if (s->current.request_to == HttpTransact::PARENT_PROXY) {
    HTTP_SUM_DYN_STAT(http_parent_proxy_request_total_bytes_stat,
                      origin_server_request_header_size + origin_server_request_body_size);
    HTTP_SUM_DYN_STAT(http_parent_proxy_response_total_bytes_stat,
                      origin_server_response_header_size + origin_server_response_body_size);
    HTTP_SUM_DYN_STAT(http_parent_proxy_transaction_time_stat, total_time);
  }
  // request header zero means the document was cached.
  // do not add to stats.
  if (origin_server_request_header_size > 0) {
    HTTP_SUM_DYN_STAT(http_origin_server_request_header_total_size_stat, origin_server_request_header_size);
    HTTP_SUM_DYN_STAT(http_origin_server_response_header_total_size_stat, origin_server_response_header_size);
    HTTP_SUM_DYN_STAT(http_origin_server_request_document_total_size_stat, origin_server_request_body_size);
    HTTP_SUM_DYN_STAT(http_origin_server_response_document_total_size_stat, origin_server_response_body_size);
  }

  if (s->method == HTTP_WKSIDX_PUSH) {
    HTTP_SUM_DYN_STAT(http_pushed_response_header_total_size_stat, pushed_response_header_size);
    HTTP_SUM_DYN_STAT(http_pushed_document_total_size_stat, pushed_response_body_size);
  }

  histogram_request_document_size(s, user_agent_request_body_size);
  histogram_response_document_size(s, user_agent_response_body_size);

  if (user_agent_write_time >= 0) {
    user_agent_connection_speed(s, user_agent_write_time, user_agent_response_size);
  }

  if (origin_server_request_header_size > 0 && origin_server_read_time > 0) {
    origin_server_connection_speed(s, origin_server_read_time, origin_server_response_size);
  }

  // update milestones stats
  HTTP_SUM_DYN_STAT(http_ua_begin_time_stat, milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_BEGIN));
  HTTP_SUM_DYN_STAT(http_ua_first_read_time_stat, milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_FIRST_READ));
  HTTP_SUM_DYN_STAT(http_ua_read_header_done_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_READ_HEADER_DONE));
  HTTP_SUM_DYN_STAT(http_ua_begin_write_time_stat, milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_BEGIN_WRITE));
  HTTP_SUM_DYN_STAT(http_ua_close_time_stat, milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_CLOSE));
  HTTP_SUM_DYN_STAT(http_server_first_connect_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_FIRST_CONNECT));
  HTTP_SUM_DYN_STAT(http_server_connect_time_stat, milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_CONNECT));
  HTTP_SUM_DYN_STAT(http_server_connect_end_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_CONNECT_END));
  HTTP_SUM_DYN_STAT(http_server_begin_write_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_BEGIN_WRITE));
  HTTP_SUM_DYN_STAT(http_server_first_read_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_FIRST_READ));
  HTTP_SUM_DYN_STAT(http_server_read_header_done_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_READ_HEADER_DONE));
  HTTP_SUM_DYN_STAT(http_server_close_time_stat, milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_CLOSE));
  HTTP_SUM_DYN_STAT(http_cache_open_read_begin_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_CACHE_OPEN_READ_BEGIN));
  HTTP_SUM_DYN_STAT(http_cache_open_read_end_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_CACHE_OPEN_READ_END));
  HTTP_SUM_DYN_STAT(http_cache_open_write_begin_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN));
  HTTP_SUM_DYN_STAT(http_cache_open_write_end_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_CACHE_OPEN_WRITE_END));
  HTTP_SUM_DYN_STAT(http_dns_lookup_begin_time_stat,
                    milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_DNS_LOOKUP_BEGIN));
  HTTP_SUM_DYN_STAT(http_dns_lookup_end_time_stat, milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_DNS_LOOKUP_END));
  HTTP_SUM_DYN_STAT(http_sm_start_time_stat, milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_SM_START));
  HTTP_SUM_DYN_STAT(http_sm_finish_time_stat, milestones.difference_msec(TS_MILESTONE_SM_START, TS_MILESTONE_SM_FINISH));
}

void
HttpTransact::delete_warning_value(HTTPHdr *to_warn, HTTPWarningCode warning_code)
{
  int w_code       = (int)warning_code;
  MIMEField *field = to_warn->field_find(MIME_FIELD_WARNING, MIME_LEN_WARNING);
  ;

  // Loop over the values to see if we need to do anything
  if (field) {
    HdrCsvIter iter;

    int valid;
    int val_code;

    const char *value_str;
    int value_len;

    MIMEField *new_field = nullptr;
    val_code             = iter.get_first_int(field, &valid);

    while (valid) {
      if (val_code == w_code) {
        // Ok, found the value we're look to delete
        //  Look over and create a new field
        //  appending all elements that are not this
        //  value
        val_code = iter.get_first_int(field, &valid);

        while (valid) {
          if (val_code != warning_code) {
            value_str = iter.get_current(&value_len);
            if (new_field) {
              new_field->value_append(to_warn->m_heap, to_warn->m_mime, value_str, value_len, true);
            } else {
              new_field = to_warn->field_create();
              to_warn->field_value_set(new_field, value_str, value_len);
            }
          }
          val_code = iter.get_next_int(&valid);
        }

        to_warn->field_delete(MIME_FIELD_WARNING, MIME_LEN_WARNING);
        if (new_field) {
          new_field->name_set(to_warn->m_heap, to_warn->m_mime, MIME_FIELD_WARNING, MIME_LEN_WARNING);
          to_warn->field_attach(new_field);
        }

        return;
      }

      val_code = iter.get_next_int(&valid);
    }
  }
}

void
HttpTransact::change_response_header_because_of_range_request(State *s, HTTPHdr *header)
{
  MIMEField *field;
  char *reason_phrase;

  TxnDebug("http_trans", "Partial content requested, re-calculating content-length");

  header->status_set(HTTP_STATUS_PARTIAL_CONTENT);
  reason_phrase = (char *)(http_hdr_reason_lookup(HTTP_STATUS_PARTIAL_CONTENT));
  header->reason_set(reason_phrase, strlen(reason_phrase));

  // set the right Content-Type for multiple entry Range
  if (s->num_range_fields > 1) {
    field = header->field_find(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);

    if (field != nullptr) {
      header->field_delete(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);
    }

    field = header->field_create(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);
    field->value_append(header->m_heap, header->m_mime, range_type, sizeof(range_type) - 1);

    header->field_attach(field);
    // TODO: There's a known bug here where the Content-Length is not correct for multi-part
    // Range: requests.
    header->set_content_length(s->range_output_cl);
  } else {
    if (s->cache_info.object_read && s->cache_info.object_read->valid()) {
      // TODO: It's unclear under which conditions we need to update the Content-Range: header,
      // many times it's already set correctly before calling this. For now, always try do it
      // when we have the information for it available.
      // TODO: Also, it's unclear as to why object_read->valid() is not always true here.
      char numbers[RANGE_NUMBERS_LENGTH];
      header->field_delete(MIME_FIELD_CONTENT_RANGE, MIME_LEN_CONTENT_RANGE);
      field = header->field_create(MIME_FIELD_CONTENT_RANGE, MIME_LEN_CONTENT_RANGE);
      snprintf(numbers, sizeof(numbers), "bytes %" PRId64 "-%" PRId64 "/%" PRId64, s->ranges[0]._start, s->ranges[0]._end,
               s->cache_info.object_read->object_size_get());
      field->value_set(header->m_heap, header->m_mime, numbers, strlen(numbers));
      header->field_attach(field);
    }
    // Always update the Content-Length: header.
    header->set_content_length(s->range_output_cl);
  }
}

#if TS_HAS_TESTS
void forceLinkRegressionHttpTransact();
void
forceLinkRegressionHttpTransactCaller()
{
  forceLinkRegressionHttpTransact();
}
#endif
