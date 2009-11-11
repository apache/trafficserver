/** @file

  Implements callin functions for plugins

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

#ifndef INK_NO_API

#include "inktomi++.h"

#include "InkAPIInternal.h"
#include <stdio.h>
#include <string>
#include "Log.h"
#include "URL.h"
#include "MIME.h"
#include "HTTP.h"
#include "HttpClientSession.h"
#include "HttpSM.h"
#include "HttpConfig.h"
#include "P_Net.h"
#include "P_HostDB.h"
#include "StatSystem.h"
#include "P_Cache.h"
#include "I_RecCore.h"
#include "I_RecSignals.h"
#include "Config.h"
#include "stats/CoupledStats.h"
#include "stats/Stats.h"
#include "InkAPIInternal.h"
#include "Plugin.h"
#include "LogObject.h"
#include "LogConfig.h"
//#include "UserNameCache.h"
#include "PluginVC.h"
#include "api/include/InkAPIPrivate.h"
#include "ICP.h"
#include "HttpAccept.h"
#include "PluginVC.h"
//#include "MixtAPIInternal.h"
#include "api/include/InkAPIaaa.h"

/****************************************************************
 *  IMPORTANT - READ ME
 * Any plugin using the IO Core must enter
 *   with a held mutex.  SDK 1.0, 1.1 & 2.0 did not
 *   have this restriction so we need to add a mutex
 *   to Plugin's Continuation if it trys to use the IOCore
 * Not only does the plugin have to have a mutex
 *   before entering the IO Core.  The mutex needs to be held.
 *   We now take out the mutex on each call to ensure it is
 *   held for the entire duration of the IOCore call
 ***************************************************************/

//
// FORCE_PLUGIN_MUTEX -- define 'UNSAFE_FORCE_MUTEX' if you
// do *not* want the locking macro to be thread safe.
// Otherwise, access during 'null-mutex' case will be serialized
// in a locking manner (too bad for the net threads).
//

#define UNSAFE_FORCE_MUTEX

#ifdef UNSAFE_FORCE_MUTEX
#define LOCK_MONGO_MUTEX
#define UNLOCK_MONGO_MUTEX
#define MUX_WARNING(p) \
INKDebug ("sdk","(SDK) null mutex detected in critical region (mutex created)"); \
INKDebug ("sdk","(SDK) please create continuation [%p] with mutex", (p));
#else
static ink_mutex big_mux;

#define MUX_WARNING(p) 1
#define LOCK_MONGO_MUTEX   ink_mutex_acquire (&big_mux)
#define UNLOCK_MONGO_MUTEX ink_mutex_release (&big_mux)
#endif

#define FORCE_PLUGIN_MUTEX(_c) \
  MutexLock ml; \
  LOCK_MONGO_MUTEX; \
  if (( (INKContInternal*)_c)->mutex == NULL) { \
      ( (INKContInternal*)_c)->mutex = new_ProxyMutex(); \
      UNLOCK_MONGO_MUTEX; \
	  MUX_WARNING(_c); \
      MUTEX_SET_AND_TAKE_LOCK(ml, ((INKContInternal*)_c)->mutex, this_ethread()); \
  } else { \
      UNLOCK_MONGO_MUTEX; \
      MUTEX_SET_AND_TAKE_LOCK(ml, ((INKContInternal*)_c)->mutex, this_ethread()); \
  }

// helper macro for setting HTTPHdr data
#define SET_HTTP_HDR(_HDR, _BUF_PTR, _OBJ_PTR) \
    _HDR.m_heap = ((HdrHeapSDKHandle*) _BUF_PTR)->m_heap; \
    _HDR.m_http = (HTTPHdrImpl*) _OBJ_PTR; \
    _HDR.m_mime = _HDR.m_http->m_fields_impl;

/* URL schemes */
inkapi const char *INK_URL_SCHEME_FILE;
inkapi const char *INK_URL_SCHEME_FTP;
inkapi const char *INK_URL_SCHEME_GOPHER;
inkapi const char *INK_URL_SCHEME_HTTP;
inkapi const char *INK_URL_SCHEME_HTTPS;
inkapi const char *INK_URL_SCHEME_MAILTO;
inkapi const char *INK_URL_SCHEME_NEWS;
inkapi const char *INK_URL_SCHEME_NNTP;
inkapi const char *INK_URL_SCHEME_PROSPERO;
inkapi const char *INK_URL_SCHEME_RTSP;
inkapi const char *INK_URL_SCHEME_RTSPU;
inkapi const char *INK_URL_SCHEME_TELNET;
inkapi const char *INK_URL_SCHEME_WAIS;

/* URL schemes string lengths */
inkapi int INK_URL_LEN_FILE;
inkapi int INK_URL_LEN_FTP;
inkapi int INK_URL_LEN_GOPHER;
inkapi int INK_URL_LEN_HTTP;
inkapi int INK_URL_LEN_HTTPS;
inkapi int INK_URL_LEN_MAILTO;
inkapi int INK_URL_LEN_NEWS;
inkapi int INK_URL_LEN_NNTP;
inkapi int INK_URL_LEN_PROSPERO;
inkapi int INK_URL_LEN_TELNET;
inkapi int INK_URL_LEN_WAIS;

/* MIME fields */
inkapi const char *INK_MIME_FIELD_ACCEPT;
inkapi const char *INK_MIME_FIELD_ACCEPT_CHARSET;
inkapi const char *INK_MIME_FIELD_ACCEPT_ENCODING;
inkapi const char *INK_MIME_FIELD_ACCEPT_LANGUAGE;
inkapi const char *INK_MIME_FIELD_ACCEPT_RANGES;
inkapi const char *INK_MIME_FIELD_AGE;
inkapi const char *INK_MIME_FIELD_ALLOW;
inkapi const char *INK_MIME_FIELD_APPROVED;
inkapi const char *INK_MIME_FIELD_AUTHORIZATION;
inkapi const char *INK_MIME_FIELD_BYTES;
inkapi const char *INK_MIME_FIELD_CACHE_CONTROL;
inkapi const char *INK_MIME_FIELD_CLIENT_IP;
inkapi const char *INK_MIME_FIELD_CONNECTION;
inkapi const char *INK_MIME_FIELD_CONTENT_BASE;
inkapi const char *INK_MIME_FIELD_CONTENT_ENCODING;
inkapi const char *INK_MIME_FIELD_CONTENT_LANGUAGE;
inkapi const char *INK_MIME_FIELD_CONTENT_LENGTH;
inkapi const char *INK_MIME_FIELD_CONTENT_LOCATION;
inkapi const char *INK_MIME_FIELD_CONTENT_MD5;
inkapi const char *INK_MIME_FIELD_CONTENT_RANGE;
inkapi const char *INK_MIME_FIELD_CONTENT_TYPE;
inkapi const char *INK_MIME_FIELD_CONTROL;
inkapi const char *INK_MIME_FIELD_COOKIE;
inkapi const char *INK_MIME_FIELD_DATE;
inkapi const char *INK_MIME_FIELD_DISTRIBUTION;
inkapi const char *INK_MIME_FIELD_ETAG;
inkapi const char *INK_MIME_FIELD_EXPECT;
inkapi const char *INK_MIME_FIELD_EXPIRES;
inkapi const char *INK_MIME_FIELD_FOLLOWUP_TO;
inkapi const char *INK_MIME_FIELD_FROM;
inkapi const char *INK_MIME_FIELD_HOST;
inkapi const char *INK_MIME_FIELD_IF_MATCH;
inkapi const char *INK_MIME_FIELD_IF_MODIFIED_SINCE;
inkapi const char *INK_MIME_FIELD_IF_NONE_MATCH;
inkapi const char *INK_MIME_FIELD_IF_RANGE;
inkapi const char *INK_MIME_FIELD_IF_UNMODIFIED_SINCE;
inkapi const char *INK_MIME_FIELD_KEEP_ALIVE;
inkapi const char *INK_MIME_FIELD_KEYWORDS;
inkapi const char *INK_MIME_FIELD_LAST_MODIFIED;
inkapi const char *INK_MIME_FIELD_LINES;
inkapi const char *INK_MIME_FIELD_LOCATION;
inkapi const char *INK_MIME_FIELD_MAX_FORWARDS;
inkapi const char *INK_MIME_FIELD_MESSAGE_ID;
inkapi const char *INK_MIME_FIELD_NEWSGROUPS;
inkapi const char *INK_MIME_FIELD_ORGANIZATION;
inkapi const char *INK_MIME_FIELD_PATH;
inkapi const char *INK_MIME_FIELD_PRAGMA;
inkapi const char *INK_MIME_FIELD_PROXY_AUTHENTICATE;
inkapi const char *INK_MIME_FIELD_PROXY_AUTHORIZATION;
inkapi const char *INK_MIME_FIELD_PROXY_CONNECTION;
inkapi const char *INK_MIME_FIELD_PUBLIC;
inkapi const char *INK_MIME_FIELD_RANGE;
inkapi const char *INK_MIME_FIELD_REFERENCES;
inkapi const char *INK_MIME_FIELD_REFERER;
inkapi const char *INK_MIME_FIELD_REPLY_TO;
inkapi const char *INK_MIME_FIELD_RETRY_AFTER;
inkapi const char *INK_MIME_FIELD_SENDER;
inkapi const char *INK_MIME_FIELD_SERVER;
inkapi const char *INK_MIME_FIELD_SET_COOKIE;
inkapi const char *INK_MIME_FIELD_SUBJECT;
inkapi const char *INK_MIME_FIELD_SUMMARY;
inkapi const char *INK_MIME_FIELD_TE;
inkapi const char *INK_MIME_FIELD_TRANSFER_ENCODING;
inkapi const char *INK_MIME_FIELD_UPGRADE;
inkapi const char *INK_MIME_FIELD_USER_AGENT;
inkapi const char *INK_MIME_FIELD_VARY;
inkapi const char *INK_MIME_FIELD_VIA;
inkapi const char *INK_MIME_FIELD_WARNING;
inkapi const char *INK_MIME_FIELD_WWW_AUTHENTICATE;
inkapi const char *INK_MIME_FIELD_XREF;
inkapi const char *INK_MIME_FIELD_X_FORWARDED_FOR;

/* MIME fields string lengths */
inkapi int INK_MIME_LEN_ACCEPT;
inkapi int INK_MIME_LEN_ACCEPT_CHARSET;
inkapi int INK_MIME_LEN_ACCEPT_ENCODING;
inkapi int INK_MIME_LEN_ACCEPT_LANGUAGE;
inkapi int INK_MIME_LEN_ACCEPT_RANGES;
inkapi int INK_MIME_LEN_AGE;
inkapi int INK_MIME_LEN_ALLOW;
inkapi int INK_MIME_LEN_APPROVED;
inkapi int INK_MIME_LEN_AUTHORIZATION;
inkapi int INK_MIME_LEN_BYTES;
inkapi int INK_MIME_LEN_CACHE_CONTROL;
inkapi int INK_MIME_LEN_CLIENT_IP;
inkapi int INK_MIME_LEN_CONNECTION;
inkapi int INK_MIME_LEN_CONTENT_BASE;
inkapi int INK_MIME_LEN_CONTENT_ENCODING;
inkapi int INK_MIME_LEN_CONTENT_LANGUAGE;
inkapi int INK_MIME_LEN_CONTENT_LENGTH;
inkapi int INK_MIME_LEN_CONTENT_LOCATION;
inkapi int INK_MIME_LEN_CONTENT_MD5;
inkapi int INK_MIME_LEN_CONTENT_RANGE;
inkapi int INK_MIME_LEN_CONTENT_TYPE;
inkapi int INK_MIME_LEN_CONTROL;
inkapi int INK_MIME_LEN_COOKIE;
inkapi int INK_MIME_LEN_DATE;
inkapi int INK_MIME_LEN_DISTRIBUTION;
inkapi int INK_MIME_LEN_ETAG;
inkapi int INK_MIME_LEN_EXPECT;
inkapi int INK_MIME_LEN_EXPIRES;
inkapi int INK_MIME_LEN_FOLLOWUP_TO;
inkapi int INK_MIME_LEN_FROM;
inkapi int INK_MIME_LEN_HOST;
inkapi int INK_MIME_LEN_IF_MATCH;
inkapi int INK_MIME_LEN_IF_MODIFIED_SINCE;
inkapi int INK_MIME_LEN_IF_NONE_MATCH;
inkapi int INK_MIME_LEN_IF_RANGE;
inkapi int INK_MIME_LEN_IF_UNMODIFIED_SINCE;
inkapi int INK_MIME_LEN_KEEP_ALIVE;
inkapi int INK_MIME_LEN_KEYWORDS;
inkapi int INK_MIME_LEN_LAST_MODIFIED;
inkapi int INK_MIME_LEN_LINES;
inkapi int INK_MIME_LEN_LOCATION;
inkapi int INK_MIME_LEN_MAX_FORWARDS;
inkapi int INK_MIME_LEN_MESSAGE_ID;
inkapi int INK_MIME_LEN_NEWSGROUPS;
inkapi int INK_MIME_LEN_ORGANIZATION;
inkapi int INK_MIME_LEN_PATH;
inkapi int INK_MIME_LEN_PRAGMA;
inkapi int INK_MIME_LEN_PROXY_AUTHENTICATE;
inkapi int INK_MIME_LEN_PROXY_AUTHORIZATION;
inkapi int INK_MIME_LEN_PROXY_CONNECTION;
inkapi int INK_MIME_LEN_PUBLIC;
inkapi int INK_MIME_LEN_RANGE;
inkapi int INK_MIME_LEN_REFERENCES;
inkapi int INK_MIME_LEN_REFERER;
inkapi int INK_MIME_LEN_REPLY_TO;
inkapi int INK_MIME_LEN_RETRY_AFTER;
inkapi int INK_MIME_LEN_SENDER;
inkapi int INK_MIME_LEN_SERVER;
inkapi int INK_MIME_LEN_SET_COOKIE;
inkapi int INK_MIME_LEN_SUBJECT;
inkapi int INK_MIME_LEN_SUMMARY;
inkapi int INK_MIME_LEN_TE;
inkapi int INK_MIME_LEN_TRANSFER_ENCODING;
inkapi int INK_MIME_LEN_UPGRADE;
inkapi int INK_MIME_LEN_USER_AGENT;
inkapi int INK_MIME_LEN_VARY;
inkapi int INK_MIME_LEN_VIA;
inkapi int INK_MIME_LEN_WARNING;
inkapi int INK_MIME_LEN_WWW_AUTHENTICATE;
inkapi int INK_MIME_LEN_XREF;
inkapi int INK_MIME_LEN_X_FORWARDED_FOR;


/* HTTP miscellaneous values */
inkapi const char *INK_HTTP_VALUE_BYTES;
inkapi const char *INK_HTTP_VALUE_CHUNKED;
inkapi const char *INK_HTTP_VALUE_CLOSE;
inkapi const char *INK_HTTP_VALUE_COMPRESS;
inkapi const char *INK_HTTP_VALUE_DEFLATE;
inkapi const char *INK_HTTP_VALUE_GZIP;
inkapi const char *INK_HTTP_VALUE_IDENTITY;
inkapi const char *INK_HTTP_VALUE_KEEP_ALIVE;
inkapi const char *INK_HTTP_VALUE_MAX_AGE;
inkapi const char *INK_HTTP_VALUE_MAX_STALE;
inkapi const char *INK_HTTP_VALUE_MIN_FRESH;
inkapi const char *INK_HTTP_VALUE_MUST_REVALIDATE;
inkapi const char *INK_HTTP_VALUE_NONE;
inkapi const char *INK_HTTP_VALUE_NO_CACHE;
inkapi const char *INK_HTTP_VALUE_NO_STORE;
inkapi const char *INK_HTTP_VALUE_NO_TRANSFORM;
inkapi const char *INK_HTTP_VALUE_ONLY_IF_CACHED;
inkapi const char *INK_HTTP_VALUE_PRIVATE;
inkapi const char *INK_HTTP_VALUE_PROXY_REVALIDATE;
inkapi const char *INK_HTTP_VALUE_PUBLIC;
inkapi const char *INK_HTTP_VALUE_SMAX_AGE;
inkapi const char *INK_HTTP_VALUE_S_MAXAGE;

/* HTTP miscellaneous values string lengths */
inkapi int INK_HTTP_LEN_BYTES;
inkapi int INK_HTTP_LEN_CHUNKED;
inkapi int INK_HTTP_LEN_CLOSE;
inkapi int INK_HTTP_LEN_COMPRESS;
inkapi int INK_HTTP_LEN_DEFLATE;
inkapi int INK_HTTP_LEN_GZIP;
inkapi int INK_HTTP_LEN_IDENTITY;
inkapi int INK_HTTP_LEN_KEEP_ALIVE;
inkapi int INK_HTTP_LEN_MAX_AGE;
inkapi int INK_HTTP_LEN_MAX_STALE;
inkapi int INK_HTTP_LEN_MIN_FRESH;
inkapi int INK_HTTP_LEN_MUST_REVALIDATE;
inkapi int INK_HTTP_LEN_NONE;
inkapi int INK_HTTP_LEN_NO_CACHE;
inkapi int INK_HTTP_LEN_NO_STORE;
inkapi int INK_HTTP_LEN_NO_TRANSFORM;
inkapi int INK_HTTP_LEN_ONLY_IF_CACHED;
inkapi int INK_HTTP_LEN_PRIVATE;
inkapi int INK_HTTP_LEN_PROXY_REVALIDATE;
inkapi int INK_HTTP_LEN_PUBLIC;
inkapi int INK_HTTP_LEN_SMAX_AGE;
inkapi int INK_HTTP_LEN_S_MAXAGE;

/* HTTP methods */
inkapi const char *INK_HTTP_METHOD_CONNECT;
inkapi const char *INK_HTTP_METHOD_DELETE;
inkapi const char *INK_HTTP_METHOD_GET;
inkapi const char *INK_HTTP_METHOD_HEAD;
inkapi const char *INK_HTTP_METHOD_ICP_QUERY;
inkapi const char *INK_HTTP_METHOD_OPTIONS;
inkapi const char *INK_HTTP_METHOD_POST;
inkapi const char *INK_HTTP_METHOD_PURGE;
inkapi const char *INK_HTTP_METHOD_PUT;
inkapi const char *INK_HTTP_METHOD_TRACE;

/* HTTP methods string lengths */
inkapi int INK_HTTP_LEN_CONNECT;
inkapi int INK_HTTP_LEN_DELETE;
inkapi int INK_HTTP_LEN_GET;
inkapi int INK_HTTP_LEN_HEAD;
inkapi int INK_HTTP_LEN_ICP_QUERY;
inkapi int INK_HTTP_LEN_OPTIONS;
inkapi int INK_HTTP_LEN_POST;
inkapi int INK_HTTP_LEN_PURGE;
inkapi int INK_HTTP_LEN_PUT;
inkapi int INK_HTTP_LEN_TRACE;

/* MLoc Constants */
inkapi const INKMLoc INK_NULL_MLOC = (INKMLoc) NULL;

HttpAPIHooks *http_global_hooks = NULL;
CacheAPIHooks *cache_global_hooks = NULL;
ConfigUpdateCbTable *global_config_cbs = NULL;

static char traffic_server_version[128] = "";

static ClassAllocator<APIHook> apiHookAllocator("apiHookAllocator");
static ClassAllocator<INKContInternal> INKContAllocator("INKContAllocator");
static ClassAllocator<INKVConnInternal> INKVConnAllocator("INKVConnAllocator");

// Error Ptr.
inkapi const void *INK_ERROR_PTR = (const void *) 0x00000bad;

////////////////////////////////////////////////////////////////////
//
// API error logging
//
////////////////////////////////////////////////////////////////////

void
INKError(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  if (is_action_tag_set("deft") || is_action_tag_set("sdk_vbos_errors")) {
    diags->print_va(NULL, DL_Error, NULL, NULL, fmt, args);
  }
  Log::va_error((char *) fmt, args);
  va_end(args);
}

// Assert in debug AND optim
int
_INKReleaseAssert(const char *text, const char *file, int line)
{
  _ink_assert(text, file, line);
  return (0);
}

// Assert only in debug
int
_INKAssert(const char *text, const char *file, int line)
{
#ifdef DEBUG
  _ink_assert(text, file, line);
#endif
  return (0);
}

////////////////////////////////////////////////////////////////////
//
// SDK Interoperability Support
//
// ----------------------------------------------------------------
//
// Standalone Fields (SDK Version-Interoperability Hack)
//
//
// A "standalone" field is an ugly hack for portability with old
// versions of the SDK that mirrored the old header system.  In
// the old system, you could create arbitrary tiny little field
// objects, distinct from MIME header objects, and link them
// together.  In the new header system, all fields are internal
// constituents of the MIME header.  To preserve the semantics of
// the old SDK, we need to maintain the concept of fields that
// are created outside of a MIME header.  Whenever a field is
// "attached" to a MIME header, it is copied into the MIME header
// field's slot, and the handle to the field is updated to refer
// to the new field.
//
// Hopefully, we can eliminate this old compatibility interface and
// migrate users to the newer semantics quickly.
//
// ----------------------------------------------------------------
//
// MIMEField SDK Handles (SDK Version-Interoperability Hack)
//
// MIMEField "handles" are used by the SDK as an indirect reference
// to the MIMEField.  Because versions 1 & 2 of the SDK allowed
// standalone fields that existed without associated MIME headers,
// and because the version 3 SDK requires an associated MIME header
// for all field mutation operations (for presence bits, etc.) we
// need a data structure that:
//
//   * identifies standalone fields and stores field name/value
//     information for fields that are not yet in a header
//   * redirects the field to a real header field when the field
//     is inserted into a header
//   * maintains the associated MIMEHdrImpl when returning field
//     slots from lookup and create functions
//
// If the MIMEHdrImpl pointer is NULL, then the handle points
// to a standalone field, otherwise the handle points to a field
// within the MIME header.
//
////////////////////////////////////////////////////////////////////


/*****************************************************************/
/* Handles to headers are impls, but need to handle MIME or HTTP */
/*****************************************************************/

inline MIMEHdrImpl *
_hdr_obj_to_mime_hdr_impl(HdrHeapObjImpl * obj)
{
  MIMEHdrImpl *impl;
  if (obj->m_type == HDR_HEAP_OBJ_HTTP_HEADER)
    impl = ((HTTPHdrImpl *) obj)->m_fields_impl;
  else if (obj->m_type == HDR_HEAP_OBJ_MIME_HEADER)
    impl = (MIMEHdrImpl *) obj;
  else {
    ink_release_assert(!"mloc not a header type");
    impl = NULL;                /* gcc does not know about 'ink_release_assert' - make him happy */
  }
  return (impl);
}

inline MIMEHdrImpl *
_hdr_mloc_to_mime_hdr_impl(INKMLoc mloc)
{
  return (_hdr_obj_to_mime_hdr_impl((HdrHeapObjImpl *) mloc));
}

inline INKReturnCode
sdk_sanity_check_field_handle(INKMLoc field, INKMLoc parent_hdr = NULL)
{
#ifdef DEBUG
  if ((field == INK_NULL_MLOC) || (field == INK_ERROR_PTR)) {
    return INK_ERROR;
  }
  MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field;
  if (field_handle->m_type != HDR_HEAP_OBJ_FIELD_SDK_HANDLE) {
    return INK_ERROR;
  }
  if (parent_hdr != NULL) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(parent_hdr);
    if (field_handle->mh != mh) {
      return INK_ERROR;
    }
  }
  return INK_SUCCESS;
#else
  return INK_SUCCESS;
#endif
}

inline INKReturnCode
sdk_sanity_check_mbuffer(INKMBuffer bufp)
{
#ifdef DEBUG
  HdrHeapSDKHandle *handle = (HdrHeapSDKHandle *) bufp;
  if ((handle == NULL) ||
      (handle == INK_ERROR_PTR) || (handle->m_heap == NULL) || (handle->m_heap->m_magic != HDR_BUF_MAGIC_ALIVE)
    ) {
    return INK_ERROR;
  }
  return INK_SUCCESS;
#else
  return INK_SUCCESS;
#endif
}

INKReturnCode
sdk_sanity_check_mime_hdr_handle(INKMLoc field)
{
#ifdef DEBUG
  if ((field == INK_NULL_MLOC) || (field == INK_ERROR_PTR)) {
    return INK_ERROR;
  }
  MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field;
  if (field_handle->m_type != HDR_HEAP_OBJ_MIME_HEADER) {
    return INK_ERROR;
  }
  return INK_SUCCESS;
#else
  return INK_SUCCESS;
#endif
}

INKReturnCode
sdk_sanity_check_url_handle(INKMLoc field)
{
#ifdef DEBUG
  if ((field == INK_NULL_MLOC) || (field == INK_ERROR_PTR)) {
    return INK_ERROR;
  }
  MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field;
  if (field_handle->m_type != HDR_HEAP_OBJ_URL) {
    return INK_ERROR;
  }
  return INK_SUCCESS;
#else
  return INK_SUCCESS;
#endif
}

inline INKReturnCode
sdk_sanity_check_http_hdr_handle(INKMLoc field)
{
#ifdef DEBUG
  if ((field == INK_NULL_MLOC) || (field == INK_ERROR_PTR)) {
    return INK_ERROR;
  }
  HTTPHdrImpl *field_handle = (HTTPHdrImpl *) field;
  if (field_handle->m_type != HDR_HEAP_OBJ_HTTP_HEADER) {
    return INK_ERROR;
  }
  return INK_SUCCESS;
#else
  return INK_SUCCESS;
#endif
}

inline INKReturnCode
sdk_sanity_check_continuation(INKCont cont)
{
#ifdef DEBUG
  if ((cont != NULL) && (cont != INK_ERROR_PTR) &&
      (((INKContInternal *) cont)->m_free_magic != INKCONT_INTERN_MAGIC_DEAD)) {
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
#else
  return INK_SUCCESS;
#endif
}

inline INKReturnCode
sdk_sanity_check_http_ssn(INKHttpSsn ssnp)
{
#ifdef DEBUG
  if ((ssnp != NULL) && (ssnp != INK_ERROR_PTR)) {
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
#else
  return INK_SUCCESS;
#endif
}

inline INKReturnCode
sdk_sanity_check_txn(INKHttpTxn txnp)
{
#ifdef DEBUG
  if ((txnp != NULL) && (txnp != INK_ERROR_PTR) && (((HttpSM *) txnp)->magic == HTTP_SM_MAGIC_ALIVE)
    ) {
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
#else
  return INK_SUCCESS;
#endif
}

inline INKReturnCode
sdk_sanity_check_mime_parser(INKMimeParser parser)
{
#ifdef DEBUG
  if ((parser != NULL) && (parser != INK_ERROR_PTR)) {
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
#endif
  return INK_SUCCESS;
}

inline INKReturnCode
sdk_sanity_check_http_parser(INKHttpParser parser)
{
#ifdef DEBUG
  if ((parser != NULL) && (parser != INK_ERROR_PTR)) {
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
#endif
  return INK_SUCCESS;
}

inline INKReturnCode
sdk_sanity_check_alt_info(INKHttpAltInfo info)
{
#ifdef DEBUG
  if ((info != NULL) && (info != INK_ERROR_PTR)) {
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
#endif
  return INK_SUCCESS;
}

inline INKReturnCode
sdk_sanity_check_hook_id(INKHttpHookID id)
{
#ifdef DEBUG
  if (id<INK_HTTP_READ_REQUEST_HDR_HOOK || id> INK_HTTP_LAST_HOOK)
    return INK_ERROR;
  return INK_SUCCESS;
#else
  return INK_SUCCESS;
#endif
}


inline INKReturnCode
sdk_sanity_check_null_ptr(void *ptr)
{
#ifdef DEBUG
  if (ptr == NULL)
    return INK_ERROR;
  return INK_SUCCESS;
#else
  return INK_SUCCESS;
#endif
}

/**
  The function checks if the buffer is Modifiable and returns true if
  it is modifiable, else returns false.

*/
bool
isWriteable(INKMBuffer bufp)
{
  if (bufp != NULL) {
    return (((HdrHeapSDKHandle *) bufp)->m_heap->m_writeable);
  } else {
    return false;
  }
}


/******************************************************/
/* Allocators for field handles and standalone fields */
/******************************************************/

static MIMEFieldSDKHandle *
sdk_alloc_field_handle(INKMBuffer bufp, MIMEHdrImpl * mh)
{
  sdk_sanity_check_mbuffer(bufp);
  HdrHeapSDKHandle *sdk_heap = (HdrHeapSDKHandle *) bufp;

  MIMEFieldSDKHandle *handle = sdk_heap->m_sdk_alloc.allocate_mhandle();
  obj_init_header(handle, HDR_HEAP_OBJ_FIELD_SDK_HANDLE, sizeof(MIMEFieldSDKHandle), 0);
  handle->mh = mh;
  return (handle);
}

static void
sdk_free_field_handle(INKMBuffer bufp, MIMEFieldSDKHandle * field_handle)
{
  sdk_sanity_check_mbuffer(bufp);
  HdrHeapSDKHandle *sdk_heap = (HdrHeapSDKHandle *) bufp;
  field_handle->m_type = HDR_HEAP_OBJ_EMPTY;
  field_handle->mh = NULL;
  field_handle->field_ptr = NULL;

  sdk_heap->m_sdk_alloc.free_mhandle(field_handle);
}

static MIMEField *
sdk_alloc_standalone_field(INKMBuffer bufp)
{
  sdk_sanity_check_mbuffer(bufp);
  HdrHeapSDKHandle *sdk_heap = (HdrHeapSDKHandle *) bufp;

  MIMEField *sa_field = sdk_heap->m_sdk_alloc.allocate_mfield();
  return (sa_field);
}

static void
sdk_free_standalone_field(INKMBuffer bufp, MIMEField * sa_field)
{
  sdk_sanity_check_mbuffer(bufp);
  HdrHeapSDKHandle *sdk_heap = (HdrHeapSDKHandle *) bufp;

  // FIX: can remove memset once debugged --- only here to help catch bugs
  memset(sa_field, NUL, sizeof(MIMEField));
  sa_field->m_readiness = MIME_FIELD_SLOT_READINESS_DELETED;

  sdk_heap->m_sdk_alloc.free_mfield(sa_field);
}

////////////////////////////////////////////////////////////////////
//
// FileImpl
//
////////////////////////////////////////////////////////////////////

FileImpl::FileImpl()
:m_fd(-1), m_mode(CLOSED), m_buf(NULL), m_bufsize(0), m_bufpos(0)
{
}

FileImpl::~FileImpl()
{
  fclose();
}

int
FileImpl::fopen(const char *filename, const char *mode)
{
  if (mode[0] == '\0') {
    return 0;
  } else if (mode[0] == 'r') {
    if (mode[1] != '\0') {
      return 0;
    }
    m_mode = READ;
    m_fd = ink_open(filename, O_RDONLY | _O_ATTRIB_NORMAL);
  } else if (mode[0] == 'w') {
    if (mode[1] != '\0') {
      return 0;
    }
    m_mode = WRITE;
    m_fd = ink_open(filename, O_WRONLY | O_CREAT | _O_ATTRIB_NORMAL, 0644);
  } else if (mode[0] == 'a') {
    if (mode[1] != '\0') {
      return 0;
    }
    m_mode = WRITE;
    m_fd = ink_open(filename, O_WRONLY | O_CREAT | O_APPEND | _O_ATTRIB_NORMAL, 0644);
  }

  if (m_fd < 0) {
    m_mode = CLOSED;
    return 0;
  } else {
    return 1;
  }
}

void
FileImpl::fclose()
{
  if (m_fd != -1) {
    fflush();

    ink_close(m_fd);
    m_fd = -1;
    m_mode = CLOSED;
  }

  if (m_buf) {
    xfree(m_buf);
    m_buf = NULL;
    m_bufsize = 0;
    m_bufpos = 0;
  }
}

int
FileImpl::fread(void *buf, int length)
{
  int amount;
  int err;

  if ((m_mode != READ) || (m_fd == -1)) {
    return -1;
  }

  if (!m_buf) {
    m_bufpos = 0;
    m_bufsize = 1024;
    m_buf = (char *) xmalloc(m_bufsize);
  }

  if (m_bufpos < length) {
    amount = length;
    if (amount < 1024) {
      amount = 1024;
    }
    if (amount > (m_bufsize - m_bufpos)) {
      while (amount > (m_bufsize - m_bufpos)) {
        m_bufsize *= 2;
      }
      m_buf = (char *) xrealloc(m_buf, m_bufsize);
    }

    do {
      err = ink_read(m_fd, &m_buf[m_bufpos], amount);
    } while ((err < 0) && (errno == EINTR));

    if (err < 0) {
      return -1;
    }

    m_bufpos += err;
  }

  if (buf) {
    amount = length;
    if (amount > m_bufpos) {
      amount = m_bufpos;
    }
    memcpy(buf, m_buf, amount);
    memmove(m_buf, &m_buf[amount], m_bufpos - amount);
    m_bufpos -= amount;
    return amount;
  } else {
    return m_bufpos;
  }
}

int
FileImpl::fwrite(const void *buf, int length)
{
  const char *p, *e;
  int avail;

  if ((m_mode != WRITE) || (m_fd == -1)) {
    return -1;
  }

  if (!m_buf) {
    m_bufpos = 0;
    m_bufsize = 1024;
    m_buf = (char *) xmalloc(m_bufsize);
  }

  p = (const char *) buf;
  e = p + length;

  while (p != e) {
    avail = m_bufsize - m_bufpos;
    if (avail > length) {
      avail = length;
    }
    memcpy(&m_buf[m_bufpos], p, avail);

    m_bufpos += avail;
    p += avail;
    length -= avail;

    if ((length > 0) && (m_bufpos > 0)) {
      if (fflush() <= 0) {
        break;
      }
    }
  }

  return (p - (const char *) buf);
}

int
FileImpl::fflush()
{
  char *p, *e;
  int err = 0;

  if ((m_mode != WRITE) || (m_fd == -1)) {
    return -1;
  }

  if (m_buf) {
    p = m_buf;
    e = &m_buf[m_bufpos];

    while (p != e) {
      do {
        err = ink_write(m_fd, p, e - p);
      } while ((err < 0) && (errno == EINTR));

      if (err < 0) {
        break;
      }

      p += err;
    }

    err = p - m_buf;
    memmove(m_buf, &m_buf[err], m_bufpos - err);
    m_bufpos -= err;
  }

  return err;
}

char *
FileImpl::fgets(char *buf, int length)
{
  char *e;
  int pos;

  if (length == 0) {
    return NULL;
  }

  if (!m_buf || (m_bufpos < (length - 1))) {
    pos = m_bufpos;

    fread(NULL, length - 1);

    if (!m_bufpos && (pos == m_bufpos)) {
      return NULL;
    }
  }

  e = (char *) memchr(m_buf, '\n', m_bufpos);
  if (e) {
    e += 1;
    if (length > (e - m_buf + 1)) {
      length = e - m_buf + 1;
    }
  }

  pos = fread(buf, length - 1);
  buf[pos] = '\0';

  return buf;
}

////////////////////////////////////////////////////////////////////
//
// INKContInternal
//
////////////////////////////////////////////////////////////////////

INKContInternal::INKContInternal()
:DummyVConnection(NULL), mdata(NULL), m_event_func(NULL), m_event_count(0), m_closed(1), m_deletable(0), m_deleted(0),
  m_free_magic(INKCONT_INTERN_MAGIC_ALIVE)
{
}

INKContInternal::INKContInternal(INKEventFunc funcp, INKMutex mutexp)
:DummyVConnection((ProxyMutex *) mutexp),
mdata(NULL), m_event_func(funcp), m_event_count(0), m_closed(1), m_deletable(0), m_deleted(0),
  m_free_magic(INKCONT_INTERN_MAGIC_ALIVE)
{
  SET_HANDLER(&INKContInternal::handle_event);
}

void
INKContInternal::init(INKEventFunc funcp, INKMutex mutexp)
{
  SET_HANDLER(&INKContInternal::handle_event);

  mutex = (ProxyMutex *) mutexp;
  m_event_func = funcp;
}

void
INKContInternal::destroy()
{
  if (m_free_magic == INKCONT_INTERN_MAGIC_DEAD) {
    ink_release_assert(!"Plugin tries to use a continuation which is deleted");
  }
  m_deleted = 1;
  if (m_deletable) {
    this->mutex = NULL;
    m_free_magic = INKCONT_INTERN_MAGIC_DEAD;
    INKContAllocator.free(this);
  } else {
    INKContSchedule(this, 0);
  }
}

void
INKContInternal::handle_event_count(int event)
{
  if ((event == EVENT_IMMEDIATE) || (event == EVENT_INTERVAL)) {
    int val;

    m_deletable = (m_closed != 0);

    val = ink_atomic_increment((int *) &m_event_count, -1);
    if (val <= 0) {
      ink_assert(!"not reached");
    }

    m_deletable = m_deletable && (val == 1);
  }
}

int
INKContInternal::handle_event(int event, void *edata)
{
  if (m_free_magic == INKCONT_INTERN_MAGIC_DEAD) {
    ink_release_assert(!"Plugin tries to use a continuation which is deleted");
  }
  handle_event_count(event);
  if (m_deleted) {
    if (m_deletable) {
      this->mutex = NULL;
      m_free_magic = INKCONT_INTERN_MAGIC_DEAD;
      INKContAllocator.free(this);
    }
  } else {
    return m_event_func((INKCont) this, (INKEvent) event, edata);
  }
  return EVENT_DONE;
}


////////////////////////////////////////////////////////////////////
//
// INKVConnInternal
//
////////////////////////////////////////////////////////////////////

INKVConnInternal::INKVConnInternal()
:INKContInternal(), m_read_vio(), m_write_vio(), m_output_vc(NULL)
{
  m_closed = 0;
}

INKVConnInternal::INKVConnInternal(INKEventFunc funcp, INKMutex mutexp)
:INKContInternal(funcp, mutexp), m_read_vio(), m_write_vio(), m_output_vc(NULL)
{
  m_closed = 0;
  SET_HANDLER(&INKVConnInternal::handle_event);
}

void
INKVConnInternal::init(INKEventFunc funcp, INKMutex mutexp)
{
  INKContInternal::init(funcp, mutexp);
  SET_HANDLER(&INKVConnInternal::handle_event);
}

void
INKVConnInternal::destroy()
{
  m_deleted = 1;
  if (m_deletable) {
    this->mutex = NULL;
    m_read_vio.set_continuation(NULL);
    m_write_vio.set_continuation(NULL);
    INKVConnAllocator.free(this);
  }
}

int
INKVConnInternal::handle_event(int event, void *edata)
{
  handle_event_count(event);
  if (m_deleted) {
    if (m_deletable) {
      this->mutex = NULL;
      m_read_vio.set_continuation(NULL);
      m_write_vio.set_continuation(NULL);
      INKVConnAllocator.free(this);
    }
  } else {
    return m_event_func((INKCont) this, (INKEvent) event, edata);
  }
  return EVENT_DONE;
}

VIO *
INKVConnInternal::do_io_read(Continuation * c, int nbytes, MIOBuffer * buf)
{
  m_read_vio.buffer.writer_for(buf);
  m_read_vio.op = VIO::READ;
  m_read_vio.set_continuation(c);
  m_read_vio.nbytes = nbytes;
  m_read_vio.data = 0;
  m_read_vio.ndone = 0;
  m_read_vio.vc_server = this;

  if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  eventProcessor.schedule_imm(this, ET_NET);

  return &m_read_vio;
}

VIO *
INKVConnInternal::do_io_write(Continuation * c, int nbytes, IOBufferReader * buf, bool owner)
{
  ink_assert(!owner);
  m_write_vio.buffer.reader_for(buf);
  m_write_vio.op = VIO::WRITE;
  m_write_vio.set_continuation(c);
  m_write_vio.nbytes = nbytes;
  m_write_vio.data = 0;
  m_write_vio.ndone = 0;
  m_write_vio.vc_server = this;

  if (m_write_vio.buffer.reader()->read_avail() > 0) {
    if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
      ink_assert(!"not reached");
    }
    eventProcessor.schedule_imm(this, ET_NET);
  }

  return &m_write_vio;
}

void
INKVConnInternal::do_io_transform(VConnection * vc)
{
  m_output_vc = vc;
}

void
INKVConnInternal::do_io_close(int error)
{
  if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }

  INK_WRITE_MEMORY_BARRIER;

  if (error != -1) {
    lerrno = error;
    m_closed = INK_VC_CLOSE_ABORT;
  } else {
    m_closed = INK_VC_CLOSE_NORMAL;
  }

  m_read_vio.op = VIO::NONE;
  m_read_vio.buffer.clear();

  m_write_vio.op = VIO::NONE;
  m_write_vio.buffer.clear();

  if (m_output_vc) {
    m_output_vc->do_io_close(error);
  }

  eventProcessor.schedule_imm(this, ET_NET);
}

void
INKVConnInternal::do_io_shutdown(ShutdownHowTo_t howto)
{
  if ((howto == IO_SHUTDOWN_READ) || (howto == IO_SHUTDOWN_READWRITE)) {
    m_read_vio.op = VIO::NONE;
    m_read_vio.buffer.clear();
  }

  if ((howto == IO_SHUTDOWN_WRITE) || (howto == IO_SHUTDOWN_READWRITE)) {
    m_write_vio.op = VIO::NONE;
    m_write_vio.buffer.clear();
  }

  if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  eventProcessor.schedule_imm(this, ET_NET);
}

void
INKVConnInternal::reenable(VIO * vio)
{
  if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  eventProcessor.schedule_imm(this, ET_NET);
}

void
INKVConnInternal::retry(unsigned int delay)
{
  if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(delay));
}

bool INKVConnInternal::get_data(int id, void *data)
{
  switch (id) {
  case INK_API_DATA_READ_VIO:
    *((INKVIO *) data) = &m_read_vio;
    return true;
  case INK_API_DATA_WRITE_VIO:
    *((INKVIO *) data) = &m_write_vio;
    return true;
  case INK_API_DATA_OUTPUT_VC:
    *((INKVConn *) data) = m_output_vc;
    return true;
  case INK_API_DATA_CLOSED:
    *((int *) data) = m_closed;
    return true;
  default:
    return INKContInternal::get_data(id, data);
  }
}

bool INKVConnInternal::set_data(int id, void *data)
{
  switch (id) {
  case INK_API_DATA_OUTPUT_VC:
    m_output_vc = (VConnection *) data;
    return true;
  default:
    return INKContInternal::set_data(id, data);
  }
}

////////////////////////////////////////////////////////////////////
//
// APIHook, APIHooks, HttpAPIHooks
//
////////////////////////////////////////////////////////////////////

int
APIHook::invoke(int event, void *edata)
{
  if ((event == EVENT_IMMEDIATE) || (event == EVENT_INTERVAL)) {
    if (ink_atomic_increment((int *) &m_cont->m_event_count, 1) < 0) {
      ink_assert(!"not reached");
    }
  }
  return m_cont->handleEvent(event, edata);
}

APIHook *
APIHook::next() const
{
  return m_link.next;
}


void
APIHooks::prepend(INKContInternal * cont)
{
  APIHook *api_hook;

  api_hook = apiHookAllocator.alloc();
  api_hook->m_cont = cont;

  m_hooks.push(api_hook, api_hook->m_link);
}

void
APIHooks::append(INKContInternal * cont)
{
  APIHook *api_hook;

  api_hook = apiHookAllocator.alloc();
  api_hook->m_cont = cont;

  m_hooks.enqueue(api_hook, api_hook->m_link);
}

APIHook *
APIHooks::get()
{
  return m_hooks.head;
}


HttpAPIHooks::HttpAPIHooks():
hooks_set(0)
{
}

HttpAPIHooks::~HttpAPIHooks()
{
  clear();
}



void
HttpAPIHooks::clear()
{
  APIHook *api_hook;
  APIHook *next_hook;
  int i;

  for (i = 0; i < INK_HTTP_LAST_HOOK; i++) {
    api_hook = m_hooks[i].get();
    while (api_hook) {
      next_hook = api_hook->m_link.next;
      apiHookAllocator.free(api_hook);
      api_hook = next_hook;
    }
  }
  hooks_set = 0;
}

void
HttpAPIHooks::prepend(INKHttpHookID id, INKContInternal * cont)
{
  hooks_set = 1;
  m_hooks[id].prepend(cont);
}

void
HttpAPIHooks::append(INKHttpHookID id, INKContInternal * cont)
{
  hooks_set = 1;
  m_hooks[id].append(cont);
}

APIHook *
HttpAPIHooks::get(INKHttpHookID id)
{
  return m_hooks[id].get();
}


//Cache API

CacheAPIHooks::CacheAPIHooks():
hooks_set(0)
{
}

CacheAPIHooks::~CacheAPIHooks()
{
  clear();
}

void
CacheAPIHooks::clear()
{
  APIHook *api_hook;
  APIHook *next_hook;
  int i;

  for (i = 0; i < INK_CACHE_LAST_HOOK; i++) {
    api_hook = m_hooks[i].get();
    while (api_hook) {
      next_hook = api_hook->m_link.next;
      apiHookAllocator.free(api_hook);
      api_hook = next_hook;
    }
  }
  hooks_set = 0;
}

void
CacheAPIHooks::append(INKCacheHookID id, INKContInternal * cont)
{
  hooks_set = 1;
  m_hooks[id].append(cont);
}

APIHook *
CacheAPIHooks::get(INKCacheHookID id)
{
  return m_hooks[id].get();
}

void
CacheAPIHooks::prepend(INKCacheHookID id, INKContInternal * cont)
{
  hooks_set = 1;
  m_hooks[id].prepend(cont);
}


////////////////////////////////////////////////////////////////////
//
// ConfigUpdateCbTable
//
////////////////////////////////////////////////////////////////////

ConfigUpdateCbTable::ConfigUpdateCbTable()
{
  cb_table = ink_hash_table_create(InkHashTableKeyType_String);
}

ConfigUpdateCbTable::~ConfigUpdateCbTable()
{
  ink_assert(cb_table != NULL);

  ink_hash_table_destroy(cb_table);
}

void
ConfigUpdateCbTable::insert(INKContInternal * contp, const char *name, const char *config_path)
{
  ink_assert(cb_table != NULL);

  if (contp != NULL) {
    if (name != NULL) {
      ink_hash_table_insert(cb_table, (InkHashTableKey) name, (InkHashTableValue) contp);
      if (config_path != NULL) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "%s\t%s", name, config_path);
        RecSignalManager(MGMT_SIGNAL_PLUGIN_CONFIG_REG, buffer);
      }
    }
  }
}

void
ConfigUpdateCbTable::invoke(const char *name)
{
  ink_assert(cb_table != NULL);

  InkHashTableIteratorState ht_iter;
  InkHashTableEntry *ht_entry;
  INKContInternal *contp;

  if (name != NULL) {
    if (strcmp(name, "*") == 0) {
      ht_entry = ink_hash_table_iterator_first(cb_table, &ht_iter);
      while (ht_entry != NULL) {
        contp = (INKContInternal *) ink_hash_table_entry_value(cb_table, ht_entry);
        ink_assert(contp != NULL);
        invoke(contp);
        ht_entry = ink_hash_table_iterator_next(cb_table, &ht_iter);
      }
    } else {
      ht_entry = ink_hash_table_lookup_entry(cb_table, (InkHashTableKey) name);
      if (ht_entry != NULL) {
        contp = (INKContInternal *) ink_hash_table_entry_value(cb_table, ht_entry);
        ink_assert(contp != NULL);
        invoke(contp);
      }
    }
  }
}

void
ConfigUpdateCbTable::invoke(INKContInternal * contp)
{
  eventProcessor.schedule_imm(NEW(new ConfigUpdateCallback(contp)), ET_NET);
}

////////////////////////////////////////////////////////////////////
//
// api_init
//
////////////////////////////////////////////////////////////////////

void
api_init()
{
  // HDR FIX ME

  static int init = 1;

  if (init) {
    init = 0;

#ifndef UNSAFE_FORCE_MUTEX
    ink_mutex_init(&big_mux, "APIMongoMutex");
#endif

    /* URL schemes */
    INK_URL_SCHEME_FILE = URL_SCHEME_FILE;
    INK_URL_SCHEME_FTP = URL_SCHEME_FTP;
    INK_URL_SCHEME_GOPHER = URL_SCHEME_GOPHER;
    INK_URL_SCHEME_HTTP = URL_SCHEME_HTTP;
    INK_URL_SCHEME_HTTPS = URL_SCHEME_HTTPS;
    INK_URL_SCHEME_MAILTO = URL_SCHEME_MAILTO;
    INK_URL_SCHEME_NEWS = URL_SCHEME_NEWS;
    INK_URL_SCHEME_NNTP = URL_SCHEME_NNTP;
    INK_URL_SCHEME_PROSPERO = URL_SCHEME_PROSPERO;
    INK_URL_SCHEME_TELNET = URL_SCHEME_TELNET;
    INK_URL_SCHEME_WAIS = URL_SCHEME_WAIS;

    INK_URL_LEN_FILE = URL_LEN_FILE;
    INK_URL_LEN_FTP = URL_LEN_FTP;
    INK_URL_LEN_GOPHER = URL_LEN_GOPHER;
    INK_URL_LEN_HTTP = URL_LEN_HTTP;
    INK_URL_LEN_HTTPS = URL_LEN_HTTPS;
    INK_URL_LEN_MAILTO = URL_LEN_MAILTO;
    INK_URL_LEN_NEWS = URL_LEN_NEWS;
    INK_URL_LEN_NNTP = URL_LEN_NNTP;
    INK_URL_LEN_PROSPERO = URL_LEN_PROSPERO;
    INK_URL_LEN_TELNET = URL_LEN_TELNET;
    INK_URL_LEN_WAIS = URL_LEN_WAIS;

    /* MIME fields */
    INK_MIME_FIELD_ACCEPT = MIME_FIELD_ACCEPT;
    INK_MIME_FIELD_ACCEPT_CHARSET = MIME_FIELD_ACCEPT_CHARSET;
    INK_MIME_FIELD_ACCEPT_ENCODING = MIME_FIELD_ACCEPT_ENCODING;
    INK_MIME_FIELD_ACCEPT_LANGUAGE = MIME_FIELD_ACCEPT_LANGUAGE;
    INK_MIME_FIELD_ACCEPT_RANGES = MIME_FIELD_ACCEPT_RANGES;
    INK_MIME_FIELD_AGE = MIME_FIELD_AGE;
    INK_MIME_FIELD_ALLOW = MIME_FIELD_ALLOW;
    INK_MIME_FIELD_APPROVED = MIME_FIELD_APPROVED;
    INK_MIME_FIELD_AUTHORIZATION = MIME_FIELD_AUTHORIZATION;
    INK_MIME_FIELD_BYTES = MIME_FIELD_BYTES;
    INK_MIME_FIELD_CACHE_CONTROL = MIME_FIELD_CACHE_CONTROL;
    INK_MIME_FIELD_CLIENT_IP = MIME_FIELD_CLIENT_IP;
    INK_MIME_FIELD_CONNECTION = MIME_FIELD_CONNECTION;
    INK_MIME_FIELD_CONTENT_BASE = MIME_FIELD_CONTENT_BASE;
    INK_MIME_FIELD_CONTENT_ENCODING = MIME_FIELD_CONTENT_ENCODING;
    INK_MIME_FIELD_CONTENT_LANGUAGE = MIME_FIELD_CONTENT_LANGUAGE;
    INK_MIME_FIELD_CONTENT_LENGTH = MIME_FIELD_CONTENT_LENGTH;
    INK_MIME_FIELD_CONTENT_LOCATION = MIME_FIELD_CONTENT_LOCATION;
    INK_MIME_FIELD_CONTENT_MD5 = MIME_FIELD_CONTENT_MD5;
    INK_MIME_FIELD_CONTENT_RANGE = MIME_FIELD_CONTENT_RANGE;
    INK_MIME_FIELD_CONTENT_TYPE = MIME_FIELD_CONTENT_TYPE;
    INK_MIME_FIELD_CONTROL = MIME_FIELD_CONTROL;
    INK_MIME_FIELD_COOKIE = MIME_FIELD_COOKIE;
    INK_MIME_FIELD_DATE = MIME_FIELD_DATE;
    INK_MIME_FIELD_DISTRIBUTION = MIME_FIELD_DISTRIBUTION;
    INK_MIME_FIELD_ETAG = MIME_FIELD_ETAG;
    INK_MIME_FIELD_EXPECT = MIME_FIELD_EXPECT;
    INK_MIME_FIELD_EXPIRES = MIME_FIELD_EXPIRES;
    INK_MIME_FIELD_FOLLOWUP_TO = MIME_FIELD_FOLLOWUP_TO;
    INK_MIME_FIELD_FROM = MIME_FIELD_FROM;
    INK_MIME_FIELD_HOST = MIME_FIELD_HOST;
    INK_MIME_FIELD_IF_MATCH = MIME_FIELD_IF_MATCH;
    INK_MIME_FIELD_IF_MODIFIED_SINCE = MIME_FIELD_IF_MODIFIED_SINCE;
    INK_MIME_FIELD_IF_NONE_MATCH = MIME_FIELD_IF_NONE_MATCH;
    INK_MIME_FIELD_IF_RANGE = MIME_FIELD_IF_RANGE;
    INK_MIME_FIELD_IF_UNMODIFIED_SINCE = MIME_FIELD_IF_UNMODIFIED_SINCE;
    INK_MIME_FIELD_KEEP_ALIVE = MIME_FIELD_KEEP_ALIVE;
    INK_MIME_FIELD_KEYWORDS = MIME_FIELD_KEYWORDS;
    INK_MIME_FIELD_LAST_MODIFIED = MIME_FIELD_LAST_MODIFIED;
    INK_MIME_FIELD_LINES = MIME_FIELD_LINES;
    INK_MIME_FIELD_LOCATION = MIME_FIELD_LOCATION;
    INK_MIME_FIELD_MAX_FORWARDS = MIME_FIELD_MAX_FORWARDS;
    INK_MIME_FIELD_MESSAGE_ID = MIME_FIELD_MESSAGE_ID;
    INK_MIME_FIELD_NEWSGROUPS = MIME_FIELD_NEWSGROUPS;
    INK_MIME_FIELD_ORGANIZATION = MIME_FIELD_ORGANIZATION;
    INK_MIME_FIELD_PATH = MIME_FIELD_PATH;
    INK_MIME_FIELD_PRAGMA = MIME_FIELD_PRAGMA;
    INK_MIME_FIELD_PROXY_AUTHENTICATE = MIME_FIELD_PROXY_AUTHENTICATE;
    INK_MIME_FIELD_PROXY_AUTHORIZATION = MIME_FIELD_PROXY_AUTHORIZATION;
    INK_MIME_FIELD_PROXY_CONNECTION = MIME_FIELD_PROXY_CONNECTION;
    INK_MIME_FIELD_PUBLIC = MIME_FIELD_PUBLIC;
    INK_MIME_FIELD_RANGE = MIME_FIELD_RANGE;
    INK_MIME_FIELD_REFERENCES = MIME_FIELD_REFERENCES;
    INK_MIME_FIELD_REFERER = MIME_FIELD_REFERER;
    INK_MIME_FIELD_REPLY_TO = MIME_FIELD_REPLY_TO;
    INK_MIME_FIELD_RETRY_AFTER = MIME_FIELD_RETRY_AFTER;
    INK_MIME_FIELD_SENDER = MIME_FIELD_SENDER;
    INK_MIME_FIELD_SERVER = MIME_FIELD_SERVER;
    INK_MIME_FIELD_SET_COOKIE = MIME_FIELD_SET_COOKIE;
    INK_MIME_FIELD_SUBJECT = MIME_FIELD_SUBJECT;
    INK_MIME_FIELD_SUMMARY = MIME_FIELD_SUMMARY;
    INK_MIME_FIELD_TE = MIME_FIELD_TE;
    INK_MIME_FIELD_TRANSFER_ENCODING = MIME_FIELD_TRANSFER_ENCODING;
    INK_MIME_FIELD_UPGRADE = MIME_FIELD_UPGRADE;
    INK_MIME_FIELD_USER_AGENT = MIME_FIELD_USER_AGENT;
    INK_MIME_FIELD_VARY = MIME_FIELD_VARY;
    INK_MIME_FIELD_VIA = MIME_FIELD_VIA;
    INK_MIME_FIELD_WARNING = MIME_FIELD_WARNING;
    INK_MIME_FIELD_WWW_AUTHENTICATE = MIME_FIELD_WWW_AUTHENTICATE;
    INK_MIME_FIELD_XREF = MIME_FIELD_XREF;
    INK_MIME_FIELD_X_FORWARDED_FOR = MIME_FIELD_X_FORWARDED_FOR;


    INK_MIME_LEN_ACCEPT = MIME_LEN_ACCEPT;
    INK_MIME_LEN_ACCEPT_CHARSET = MIME_LEN_ACCEPT_CHARSET;
    INK_MIME_LEN_ACCEPT_ENCODING = MIME_LEN_ACCEPT_ENCODING;
    INK_MIME_LEN_ACCEPT_LANGUAGE = MIME_LEN_ACCEPT_LANGUAGE;
    INK_MIME_LEN_ACCEPT_RANGES = MIME_LEN_ACCEPT_RANGES;
    INK_MIME_LEN_AGE = MIME_LEN_AGE;
    INK_MIME_LEN_ALLOW = MIME_LEN_ALLOW;
    INK_MIME_LEN_APPROVED = MIME_LEN_APPROVED;
    INK_MIME_LEN_AUTHORIZATION = MIME_LEN_AUTHORIZATION;
    INK_MIME_LEN_BYTES = MIME_LEN_BYTES;
    INK_MIME_LEN_CACHE_CONTROL = MIME_LEN_CACHE_CONTROL;
    INK_MIME_LEN_CLIENT_IP = MIME_LEN_CLIENT_IP;
    INK_MIME_LEN_CONNECTION = MIME_LEN_CONNECTION;
    INK_MIME_LEN_CONTENT_BASE = MIME_LEN_CONTENT_BASE;
    INK_MIME_LEN_CONTENT_ENCODING = MIME_LEN_CONTENT_ENCODING;
    INK_MIME_LEN_CONTENT_LANGUAGE = MIME_LEN_CONTENT_LANGUAGE;
    INK_MIME_LEN_CONTENT_LENGTH = MIME_LEN_CONTENT_LENGTH;
    INK_MIME_LEN_CONTENT_LOCATION = MIME_LEN_CONTENT_LOCATION;
    INK_MIME_LEN_CONTENT_MD5 = MIME_LEN_CONTENT_MD5;
    INK_MIME_LEN_CONTENT_RANGE = MIME_LEN_CONTENT_RANGE;
    INK_MIME_LEN_CONTENT_TYPE = MIME_LEN_CONTENT_TYPE;
    INK_MIME_LEN_CONTROL = MIME_LEN_CONTROL;
    INK_MIME_LEN_COOKIE = MIME_LEN_COOKIE;
    INK_MIME_LEN_DATE = MIME_LEN_DATE;
    INK_MIME_LEN_DISTRIBUTION = MIME_LEN_DISTRIBUTION;
    INK_MIME_LEN_ETAG = MIME_LEN_ETAG;
    INK_MIME_LEN_EXPECT = MIME_LEN_EXPECT;
    INK_MIME_LEN_EXPIRES = MIME_LEN_EXPIRES;
    INK_MIME_LEN_FOLLOWUP_TO = MIME_LEN_FOLLOWUP_TO;
    INK_MIME_LEN_FROM = MIME_LEN_FROM;
    INK_MIME_LEN_HOST = MIME_LEN_HOST;
    INK_MIME_LEN_IF_MATCH = MIME_LEN_IF_MATCH;
    INK_MIME_LEN_IF_MODIFIED_SINCE = MIME_LEN_IF_MODIFIED_SINCE;
    INK_MIME_LEN_IF_NONE_MATCH = MIME_LEN_IF_NONE_MATCH;
    INK_MIME_LEN_IF_RANGE = MIME_LEN_IF_RANGE;
    INK_MIME_LEN_IF_UNMODIFIED_SINCE = MIME_LEN_IF_UNMODIFIED_SINCE;
    INK_MIME_LEN_KEEP_ALIVE = MIME_LEN_KEEP_ALIVE;
    INK_MIME_LEN_KEYWORDS = MIME_LEN_KEYWORDS;
    INK_MIME_LEN_LAST_MODIFIED = MIME_LEN_LAST_MODIFIED;
    INK_MIME_LEN_LINES = MIME_LEN_LINES;
    INK_MIME_LEN_LOCATION = MIME_LEN_LOCATION;
    INK_MIME_LEN_MAX_FORWARDS = MIME_LEN_MAX_FORWARDS;
    INK_MIME_LEN_MESSAGE_ID = MIME_LEN_MESSAGE_ID;
    INK_MIME_LEN_NEWSGROUPS = MIME_LEN_NEWSGROUPS;
    INK_MIME_LEN_ORGANIZATION = MIME_LEN_ORGANIZATION;
    INK_MIME_LEN_PATH = MIME_LEN_PATH;
    INK_MIME_LEN_PRAGMA = MIME_LEN_PRAGMA;
    INK_MIME_LEN_PROXY_AUTHENTICATE = MIME_LEN_PROXY_AUTHENTICATE;
    INK_MIME_LEN_PROXY_AUTHORIZATION = MIME_LEN_PROXY_AUTHORIZATION;
    INK_MIME_LEN_PROXY_CONNECTION = MIME_LEN_PROXY_CONNECTION;
    INK_MIME_LEN_PUBLIC = MIME_LEN_PUBLIC;
    INK_MIME_LEN_RANGE = MIME_LEN_RANGE;
    INK_MIME_LEN_REFERENCES = MIME_LEN_REFERENCES;
    INK_MIME_LEN_REFERER = MIME_LEN_REFERER;
    INK_MIME_LEN_REPLY_TO = MIME_LEN_REPLY_TO;
    INK_MIME_LEN_RETRY_AFTER = MIME_LEN_RETRY_AFTER;
    INK_MIME_LEN_SENDER = MIME_LEN_SENDER;
    INK_MIME_LEN_SERVER = MIME_LEN_SERVER;
    INK_MIME_LEN_SET_COOKIE = MIME_LEN_SET_COOKIE;
    INK_MIME_LEN_SUBJECT = MIME_LEN_SUBJECT;
    INK_MIME_LEN_SUMMARY = MIME_LEN_SUMMARY;
    INK_MIME_LEN_TE = MIME_LEN_TE;
    INK_MIME_LEN_TRANSFER_ENCODING = MIME_LEN_TRANSFER_ENCODING;
    INK_MIME_LEN_UPGRADE = MIME_LEN_UPGRADE;
    INK_MIME_LEN_USER_AGENT = MIME_LEN_USER_AGENT;
    INK_MIME_LEN_VARY = MIME_LEN_VARY;
    INK_MIME_LEN_VIA = MIME_LEN_VIA;
    INK_MIME_LEN_WARNING = MIME_LEN_WARNING;
    INK_MIME_LEN_WWW_AUTHENTICATE = MIME_LEN_WWW_AUTHENTICATE;
    INK_MIME_LEN_XREF = MIME_LEN_XREF;
    INK_MIME_LEN_X_FORWARDED_FOR = MIME_LEN_X_FORWARDED_FOR;


    /* HTTP methods */
    INK_HTTP_METHOD_CONNECT = HTTP_METHOD_CONNECT;
    INK_HTTP_METHOD_DELETE = HTTP_METHOD_DELETE;
    INK_HTTP_METHOD_GET = HTTP_METHOD_GET;
    INK_HTTP_METHOD_HEAD = HTTP_METHOD_HEAD;
    INK_HTTP_METHOD_ICP_QUERY = HTTP_METHOD_ICP_QUERY;
    INK_HTTP_METHOD_OPTIONS = HTTP_METHOD_OPTIONS;
    INK_HTTP_METHOD_POST = HTTP_METHOD_POST;
    INK_HTTP_METHOD_PURGE = HTTP_METHOD_PURGE;
    INK_HTTP_METHOD_PUT = HTTP_METHOD_PUT;
    INK_HTTP_METHOD_TRACE = HTTP_METHOD_TRACE;

    INK_HTTP_LEN_CONNECT = HTTP_LEN_CONNECT;
    INK_HTTP_LEN_DELETE = HTTP_LEN_DELETE;
    INK_HTTP_LEN_GET = HTTP_LEN_GET;
    INK_HTTP_LEN_HEAD = HTTP_LEN_HEAD;
    INK_HTTP_LEN_ICP_QUERY = HTTP_LEN_ICP_QUERY;
    INK_HTTP_LEN_OPTIONS = HTTP_LEN_OPTIONS;
    INK_HTTP_LEN_POST = HTTP_LEN_POST;
    INK_HTTP_LEN_PURGE = HTTP_LEN_PURGE;
    INK_HTTP_LEN_PUT = HTTP_LEN_PUT;
    INK_HTTP_LEN_TRACE = HTTP_LEN_TRACE;

    /* HTTP miscellaneous values */
    INK_HTTP_VALUE_BYTES = HTTP_VALUE_BYTES;
    INK_HTTP_VALUE_CHUNKED = HTTP_VALUE_CHUNKED;
    INK_HTTP_VALUE_CLOSE = HTTP_VALUE_CLOSE;
    INK_HTTP_VALUE_COMPRESS = HTTP_VALUE_COMPRESS;
    INK_HTTP_VALUE_DEFLATE = HTTP_VALUE_DEFLATE;
    INK_HTTP_VALUE_GZIP = HTTP_VALUE_GZIP;
    INK_HTTP_VALUE_IDENTITY = HTTP_VALUE_IDENTITY;
    INK_HTTP_VALUE_KEEP_ALIVE = HTTP_VALUE_KEEP_ALIVE;
    INK_HTTP_VALUE_MAX_AGE = HTTP_VALUE_MAX_AGE;
    INK_HTTP_VALUE_MAX_STALE = HTTP_VALUE_MAX_STALE;
    INK_HTTP_VALUE_MIN_FRESH = HTTP_VALUE_MIN_FRESH;
    INK_HTTP_VALUE_MUST_REVALIDATE = HTTP_VALUE_MUST_REVALIDATE;
    INK_HTTP_VALUE_NONE = HTTP_VALUE_NONE;
    INK_HTTP_VALUE_NO_CACHE = HTTP_VALUE_NO_CACHE;
    INK_HTTP_VALUE_NO_STORE = HTTP_VALUE_NO_STORE;
    INK_HTTP_VALUE_NO_TRANSFORM = HTTP_VALUE_NO_TRANSFORM;
    INK_HTTP_VALUE_ONLY_IF_CACHED = HTTP_VALUE_ONLY_IF_CACHED;
    INK_HTTP_VALUE_PRIVATE = HTTP_VALUE_PRIVATE;
    INK_HTTP_VALUE_PROXY_REVALIDATE = HTTP_VALUE_PROXY_REVALIDATE;
    INK_HTTP_VALUE_PUBLIC = HTTP_VALUE_PUBLIC;
    INK_HTTP_VALUE_S_MAXAGE = HTTP_VALUE_S_MAXAGE;
    INK_HTTP_VALUE_SMAX_AGE = HTTP_VALUE_S_MAXAGE;      // deprecated

    INK_HTTP_LEN_BYTES = HTTP_LEN_BYTES;
    INK_HTTP_LEN_CHUNKED = HTTP_LEN_CHUNKED;
    INK_HTTP_LEN_CLOSE = HTTP_LEN_CLOSE;
    INK_HTTP_LEN_COMPRESS = HTTP_LEN_COMPRESS;
    INK_HTTP_LEN_DEFLATE = HTTP_LEN_DEFLATE;
    INK_HTTP_LEN_GZIP = HTTP_LEN_GZIP;
    INK_HTTP_LEN_IDENTITY = HTTP_LEN_IDENTITY;
    INK_HTTP_LEN_KEEP_ALIVE = HTTP_LEN_KEEP_ALIVE;
    INK_HTTP_LEN_MAX_AGE = HTTP_LEN_MAX_AGE;
    INK_HTTP_LEN_MAX_STALE = HTTP_LEN_MAX_STALE;
    INK_HTTP_LEN_MIN_FRESH = HTTP_LEN_MIN_FRESH;
    INK_HTTP_LEN_MUST_REVALIDATE = HTTP_LEN_MUST_REVALIDATE;
    INK_HTTP_LEN_NONE = HTTP_LEN_NONE;
    INK_HTTP_LEN_NO_CACHE = HTTP_LEN_NO_CACHE;
    INK_HTTP_LEN_NO_STORE = HTTP_LEN_NO_STORE;
    INK_HTTP_LEN_NO_TRANSFORM = HTTP_LEN_NO_TRANSFORM;
    INK_HTTP_LEN_ONLY_IF_CACHED = HTTP_LEN_ONLY_IF_CACHED;
    INK_HTTP_LEN_PRIVATE = HTTP_LEN_PRIVATE;
    INK_HTTP_LEN_PROXY_REVALIDATE = HTTP_LEN_PROXY_REVALIDATE;
    INK_HTTP_LEN_PUBLIC = HTTP_LEN_PUBLIC;
    INK_HTTP_LEN_S_MAXAGE = HTTP_LEN_S_MAXAGE;
    INK_HTTP_LEN_SMAX_AGE = HTTP_LEN_S_MAXAGE;

    http_global_hooks = NEW(new HttpAPIHooks);
    cache_global_hooks = NEW(new CacheAPIHooks);
    global_config_cbs = NEW(new ConfigUpdateCbTable);

    // Setup the version string for returning to plugins
    ink_strncpy(traffic_server_version, appVersionInfo.VersionStr, sizeof(traffic_server_version));
  }
}

////////////////////////////////////////////////////////////////////
//
// API memory management
//
////////////////////////////////////////////////////////////////////

void *
_INKmalloc(unsigned int size, const char *path)
{
  return _xmalloc(size, path);
}

void *
_INKrealloc(void *ptr, unsigned int size, const char *path)
{
  return _xrealloc(ptr, size, path);
}

char *
_INKstrdup(const char *str, int length, const char *path)
{
  return _xstrdup(str, length, path);
}

void
_INKfree(void *ptr)
{
  _xfree(ptr);
}

////////////////////////////////////////////////////////////////////
//
// API utility routines
//
////////////////////////////////////////////////////////////////////

unsigned int
INKrandom()
{
  return this_ethread()->generator.random();
}

double
INKdrandom()
{
  return this_ethread()->generator.drandom();
}

INK64
INKhrtime()
{
  return ink_get_based_hrtime();
}

////////////////////////////////////////////////////////////////////
//
// API install and plugin locations
//
////////////////////////////////////////////////////////////////////

const char *
INKInstallDirGet(void)
{
  return system_base_install;
}

const char *
INKTrafficServerVersionGet(void)
{
  return traffic_server_version;
}

const char *
INKPluginDirGet(void)
{
  const char *CFG_NM = "proxy.config.plugin.plugin_dir";
  static char path[PATH_NAME_MAX];
  static char *plugin_dir = ".";

  if (*path == '\0') {

    RecGetRecordString_Xmalloc((char *) CFG_NM, &plugin_dir);
    if (!plugin_dir) {
      Error("Unable to read %s", CFG_NM);
      return NULL;
    }

    if (*plugin_dir == '/') {
      ink_strncpy(path, plugin_dir, sizeof(path));
    } else {
      snprintf(path, sizeof(path), "%s%s%s", system_base_install, DIR_SEP, plugin_dir);
    }

  }

  return path;
}

////////////////////////////////////////////////////////////////////
//
// Plugin registration
//
////////////////////////////////////////////////////////////////////

int
INKPluginRegister(INKSDKVersion sdk_version, INKPluginRegistrationInfo * plugin_info)
{

  ink_assert(plugin_reg_current != NULL);
  if (!plugin_reg_current)
    return 0;

  if (sdk_sanity_check_null_ptr((void *) plugin_info) != INK_SUCCESS) {
    return 0;
  }

  plugin_reg_current->plugin_registered = true;

  if (sdk_version >= INK_SDK_VERSION_1_0 && sdk_version <= INK_SDK_VERSION_5_2) {
    plugin_reg_current->sdk_version = (PluginSDKVersion) sdk_version;
  } else {
    plugin_reg_current->sdk_version = PLUGIN_SDK_VERSION_UNKNOWN;
  }

  if (plugin_info->plugin_name) {
    plugin_reg_current->plugin_name = xstrdup(plugin_info->plugin_name);
  }

  if (plugin_info->vendor_name) {
    plugin_reg_current->vendor_name = xstrdup(plugin_info->vendor_name);
  }

  if (plugin_info->support_email) {
    plugin_reg_current->support_email = xstrdup(plugin_info->support_email);
  }

  return (1);
}

////////////////////////////////////////////////////////////////////
//
// Plugin info registration - coded in 5.2, but not documented and
//                                                        not supported in 5.2
//
////////////////////////////////////////////////////////////////////

INKReturnCode
INKPluginInfoRegister(INKPluginRegistrationInfo * plugin_info)
{
  if (sdk_sanity_check_null_ptr((void *) plugin_info) == INK_ERROR) {
    return INK_ERROR;
  }

  ink_assert(plugin_reg_current != NULL);
  if (!plugin_reg_current)
    return INK_ERROR;

  plugin_reg_current->plugin_registered = true;

  /* version is not used. kept a value for backward compatibility */
  plugin_reg_current->sdk_version = PLUGIN_SDK_VERSION_UNKNOWN;

  if (plugin_info->plugin_name) {
    plugin_reg_current->plugin_name = xstrdup(plugin_info->plugin_name);
  }

  if (plugin_info->vendor_name) {
    plugin_reg_current->vendor_name = xstrdup(plugin_info->vendor_name);
  }

  if (plugin_info->support_email) {
    plugin_reg_current->support_email = xstrdup(plugin_info->support_email);
  }

  return INK_SUCCESS;
}

////////////////////////////////////////////////////////////////////
//
// API file management
//
////////////////////////////////////////////////////////////////////

INKFile
INKfopen(const char *filename, const char *mode)
{
  FileImpl *file;

  file = NEW(new FileImpl);
  if (!file->fopen(filename, mode)) {
    delete file;
    return NULL;
  }

  return (INKFile) file;
}

void
INKfclose(INKFile filep)
{
  FileImpl *file = (FileImpl *) filep;
  file->fclose();
  delete file;
}

int
INKfread(INKFile filep, void *buf, int length)
{
  FileImpl *file = (FileImpl *) filep;
  return file->fread(buf, length);
}

int
INKfwrite(INKFile filep, const void *buf, int length)
{
  FileImpl *file = (FileImpl *) filep;
  return file->fwrite(buf, length);
}

void
INKfflush(INKFile filep)
{
  FileImpl *file = (FileImpl *) filep;
  file->fflush();
}

char *
INKfgets(INKFile filep, char *buf, int length)
{
  FileImpl *file = (FileImpl *) filep;
  return file->fgets(buf, length);
}

////////////////////////////////////////////////////////////////////
//
// Header component object handles
//
////////////////////////////////////////////////////////////////////

INKReturnCode
INKHandleMLocRelease(INKMBuffer bufp, INKMLoc parent, INKMLoc mloc)
{
  MIMEFieldSDKHandle *field_handle;
  HdrHeapObjImpl *obj = (HdrHeapObjImpl *) mloc;

  if (mloc == INK_NULL_MLOC)
    return (INK_SUCCESS);

  if (sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) {
    return INK_ERROR;
  }

  switch (obj->m_type) {
  case HDR_HEAP_OBJ_URL:
  case HDR_HEAP_OBJ_HTTP_HEADER:
  case HDR_HEAP_OBJ_MIME_HEADER:
    return (INK_SUCCESS);

  case HDR_HEAP_OBJ_FIELD_SDK_HANDLE:
    field_handle = (MIMEFieldSDKHandle *) obj;
    if (sdk_sanity_check_field_handle(mloc, parent) != INK_SUCCESS) {
      return INK_ERROR;
    }
    sdk_free_field_handle(bufp, field_handle);
    return (INK_SUCCESS);

  default:
    ink_release_assert(!"invalid mloc");
    return (INK_ERROR);
  }
}

INKReturnCode
INKHandleStringRelease(INKMBuffer bufp, INKMLoc parent, const char *str)
{
  if (str == NULL)
    return (INK_SUCCESS);
  if (bufp == NULL)
    return (INK_ERROR);

  if (hdrtoken_is_wks(str))
    return (INK_SUCCESS);

  HdrHeapSDKHandle *sdk_h = (HdrHeapSDKHandle *) bufp;
  int r = sdk_h->destroy_sdk_string((char *) str);

  return ((r == 0) ? INK_ERROR : INK_SUCCESS);
}

////////////////////////////////////////////////////////////////////
//
// HdrHeaps (previously known as "Marshal Buffers")
//
////////////////////////////////////////////////////////////////////

// INKMBuffer: pointers to HdrHeapSDKHandle objects

INKMBuffer
INKMBufferCreate()
{
  INKMBuffer bufp;
  HdrHeapSDKHandle *new_heap = NEW(new HdrHeapSDKHandle);
  new_heap->m_heap = new_HdrHeap();
  bufp = (INKMBuffer) new_heap;
  if (sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) {
    delete new_heap;
    return (INKMBuffer) INK_ERROR_PTR;
  }
  return (bufp);
}

INKReturnCode
INKMBufferDestroy(INKMBuffer bufp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if (isWriteable(bufp)) {
    sdk_sanity_check_mbuffer(bufp);
    HdrHeapSDKHandle *sdk_heap = (HdrHeapSDKHandle *) bufp;
    sdk_heap->m_heap->destroy();
    delete sdk_heap;
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

// DEPRECATED
int
INKMBufferDataSet(INKMBuffer bufp, void *data)
{
  sdk_sanity_check_mbuffer(bufp);
  return 0;
}

// DEPRECATED
void *
INKMBufferDataGet(INKMBuffer bufp, int *length)
{
  sdk_sanity_check_mbuffer(bufp);
  if (length)
    *length = 0;
  return NULL;
}

// DEPRECATED
int
INKMBufferLengthGet(INKMBuffer bufp)
{
  sdk_sanity_check_mbuffer(bufp);
  return 0;
}

// DEPRECATED
void
INKMBufferRef(INKMBuffer bufp)
{
  sdk_sanity_check_mbuffer(bufp);
}

// DEPRECATED
void
INKMBufferUnref(INKMBuffer bufp)
{
  sdk_sanity_check_mbuffer(bufp);
}

// DEPRECATED
void
INKMBufferCompress(INKMBuffer bufp)
{
  sdk_sanity_check_mbuffer(bufp);
}

////////////////////////////////////////////////////////////////////
//
// URLs
//
////////////////////////////////////////////////////////////////////

// INKMBuffer: pointers to HdrHeapSDKHandle objects
// INKMLoc:    pointers to URLImpl objects

INKMLoc
INKUrlCreate(INKMBuffer bufp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If not allowed, return INK_ERROR_PTR.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) && isWriteable(bufp)) {
    HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    return ((INKMLoc) (url_create(heap)));
  } else {
    return (INKMLoc) INK_ERROR_PTR;
  }
}

INKReturnCode
INKUrlDestroy(INKMBuffer bufp, INKMLoc url_loc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_url_handle(url_loc) == INK_SUCCESS) && isWriteable(bufp)) {
    // No more objects counts in heap or deallocation so do nothing!
    // FIX ME - Did this free the MBuffer in Pete's old system?
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKMLoc
INKUrlClone(INKMBuffer dest_bufp, INKMBuffer src_bufp, INKMLoc src_url)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If not allowed, return NULL.
  if ((sdk_sanity_check_mbuffer(src_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_url_handle(src_url) == INK_SUCCESS) && isWriteable(dest_bufp)) {

    HdrHeap *s_heap, *d_heap;
    URLImpl *s_url, *d_url;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_url = (URLImpl *) src_url;

    d_url = url_copy(s_url, s_heap, d_heap, (s_heap != d_heap));
    return ((INKMLoc) d_url);
  } else {
    return (INKMLoc) INK_ERROR_PTR;
  }
}

INKReturnCode
INKUrlCopy(INKMBuffer dest_bufp, INKMLoc dest_obj, INKMBuffer src_bufp, INKMLoc src_obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(src_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_url_handle(src_obj) == INK_SUCCESS) &&
      (sdk_sanity_check_url_handle(dest_obj) == INK_SUCCESS) && isWriteable(dest_bufp)) {

    HdrHeap *s_heap, *d_heap;
    URLImpl *s_url, *d_url;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_url = (URLImpl *) src_obj;
    d_url = (URLImpl *) dest_obj;

    url_copy_onto(s_url, s_heap, d_url, d_heap, (s_heap != d_heap));
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKUrlPrint(INKMBuffer bufp, INKMLoc obj, INKIOBuffer iobufp)
{
  MIOBuffer *b = (MIOBuffer *) iobufp;
  IOBufferBlock *blk;
  int bufindex;
  int tmp, dumpoffset;
  int done;

  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      (sdk_sanity_check_url_handle(obj) != INK_SUCCESS) || (sdk_sanity_check_iocore_structure(iobufp) != INK_SUCCESS)
    ) {
    return INK_ERROR;
  }

  URL u;
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;

  dumpoffset = 0;

  do {
    blk = b->get_current_block();
    if (!blk || blk->write_avail() == 0) {
      b->add_block();
      blk = b->get_current_block();
    }

    bufindex = 0;
    tmp = dumpoffset;

    done = u.print(blk->end(), blk->write_avail(), &bufindex, &tmp);

    dumpoffset += bufindex;
    b->fill(bufindex);
  } while (!done);
  return INK_SUCCESS;
}

int
INKUrlParse(INKMBuffer bufp, INKMLoc obj, const char **start, const char *end)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      (sdk_sanity_check_url_handle(obj) != INK_SUCCESS) ||
      (start == NULL) || (*start == NULL) ||
      sdk_sanity_check_null_ptr((void *) end) != INK_SUCCESS || (!isWriteable(bufp))
    ) {
    return INK_PARSE_ERROR;
  }

  URL u;
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;
  url_clear(u.m_url_impl);
  return u.parse(start, end);
}

int
INKUrlLengthGet(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_url_handle(obj) != INK_SUCCESS)
    ) {
    return INK_ERROR;
  }
  URLImpl *url_impl = (URLImpl *) obj;
  return (url_length_get(url_impl));
}

char *
INKUrlStringGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_url_handle(obj) != INK_SUCCESS)) {
    return (char *) INK_ERROR_PTR;
  }
  URLImpl *url_impl = (URLImpl *) obj;
  return (url_string_get(url_impl, NULL, length, NULL));

}

typedef const char *(URL::*URLPartGetF) (int *length);
typedef void (URL::*URLPartSetF) (const char *value, int length);

static const char *
URLPartGet(INKMBuffer bufp, INKMLoc obj, int *length, URLPartGetF url_f)
{

  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      (sdk_sanity_check_url_handle(obj) != INK_SUCCESS) || (length == NULL)
    ) {
    return (const char *) INK_ERROR_PTR;
  }

  URL u;
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;

  int str_len;
  const char *str_ptr = (u.*url_f) (&str_len);

  if (length)
    *length = str_len;
  if (str_ptr == NULL)
    return NULL;

  return ((HdrHeapSDKHandle *) bufp)->make_sdk_string(str_ptr, str_len);
}

static const INKReturnCode
URLPartSet(INKMBuffer bufp, INKMLoc obj, const char *value, int length, URLPartSetF url_f)
{

  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      (sdk_sanity_check_url_handle(obj) != INK_SUCCESS) ||
      (sdk_sanity_check_null_ptr((void *) value) != INK_SUCCESS) || (!isWriteable(bufp))
    ) {
    return INK_ERROR;
  }

  URL u;
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;

  if (length < 0)
    length = strlen(value);

  (u.*url_f) (value, length);
  return INK_SUCCESS;
}

const char *
INKUrlSchemeGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::scheme_get);
}

INKReturnCode
INKUrlSchemeSet(INKMBuffer bufp, INKMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::scheme_set);
  }

  return INK_ERROR;
}

/* Internet specific URLs */

const char *
INKUrlUserGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::user_get);
}

INKReturnCode
INKUrlUserSet(INKMBuffer bufp, INKMLoc obj, const char *value, int length)
{
  return URLPartSet(bufp, obj, value, length, &URL::user_set);
}

const char *
INKUrlPasswordGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::password_get);
}

INKReturnCode
INKUrlPasswordSet(INKMBuffer bufp, INKMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::password_set);
  }

  return INK_ERROR;

}

const char *
INKUrlHostGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::host_get);
}

INKReturnCode
INKUrlHostSet(INKMBuffer bufp, INKMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::host_set);
  }

  return INK_ERROR;

}

int
INKUrlPortGet(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_url_handle(obj) != INK_SUCCESS)
    ) {
    return INK_ERROR;
  }
  URL u;
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;

  return u.port_get();
}

INKReturnCode
INKUrlPortSet(INKMBuffer bufp, INKMLoc obj, int port)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_url_handle(obj) == INK_SUCCESS) && isWriteable(bufp) && (port > 0)
    ) {
    URL u;
    u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    u.m_url_impl = (URLImpl *) obj;
    u.port_set(port);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

/* FTP and HTTP specific URLs  */

const char *
INKUrlPathGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::path_get);
}

INKReturnCode
INKUrlPathSet(INKMBuffer bufp, INKMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::path_set);
  }

  return INK_ERROR;
}

/* FTP specific URLs */

int
INKUrlFtpTypeGet(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_url_handle(obj) != INK_SUCCESS)
    ) {
    return INK_ERROR;
  }
  URL u;
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;

  return u.type_get();
}

INKReturnCode
INKUrlFtpTypeSet(INKMBuffer bufp, INKMLoc obj, int type)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.

  //The valid values are : 0, 65('A'), 97('a'),
  //69('E'), 101('e'), 73 ('I') and 105('i').

  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_url_handle(obj) == INK_SUCCESS) &&
      (type == 0 || type == 'A' || type == 'E' || type == 'I' ||
       type == 'a' || type == 'i' || type == 'e') && isWriteable(bufp)
    ) {
    URL u;
    u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    u.m_url_impl = (URLImpl *) obj;
    u.type_set(type);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

/* HTTP specific URLs */

const char *
INKUrlHttpParamsGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::params_get);
}

INKReturnCode
INKUrlHttpParamsSet(INKMBuffer bufp, INKMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::params_set);
  }

  return INK_ERROR;

}

const char *
INKUrlHttpQueryGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::query_get);
}

INKReturnCode
INKUrlHttpQuerySet(INKMBuffer bufp, INKMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::query_set);
  }

  return INK_ERROR;
}

const char *
INKUrlHttpFragmentGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::fragment_get);
}

INKReturnCode
INKUrlHttpFragmentSet(INKMBuffer bufp, INKMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::fragment_set);
  }

  return INK_ERROR;

}

////////////////////////////////////////////////////////////////////
//
// MIME Headers
//
////////////////////////////////////////////////////////////////////

/**************/
/* MimeParser */
/**************/

INKMimeParser
INKMimeParserCreate()
{
  INKMimeParser parser;

  parser = INKmalloc(sizeof(MIMEParser));
  if (sdk_sanity_check_mime_parser(parser) != INK_SUCCESS) {
    INKfree(parser);
    return (INKMimeParser) INK_ERROR_PTR;
  }
  mime_parser_init((MIMEParser *) parser);

  return parser;
}

INKReturnCode
INKMimeParserClear(INKMimeParser parser)
{
  if (sdk_sanity_check_mime_parser(parser) != INK_SUCCESS) {
    return INK_ERROR;
  }
  mime_parser_clear((MIMEParser *) parser);
  return INK_SUCCESS;
}

INKReturnCode
INKMimeParserDestroy(INKMimeParser parser)
{
  if (sdk_sanity_check_mime_parser(parser) != INK_SUCCESS) {
    return INK_ERROR;
  }
  mime_parser_clear((MIMEParser *) parser);
  INKfree(parser);
  return INK_SUCCESS;
}

/***********/
/* MimeHdr */
/***********/

// INKMBuffer: pointers to HdrHeapSDKHandle objects
// INKMLoc:    pointers to MIMEFieldSDKHandle objects

INKMLoc
INKMimeHdrCreate(INKMBuffer bufp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If not allowed, return NULL.
  // Changed the return value for SDK3.0 from NULL to INK_ERROR_PTR.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) && isWriteable(bufp)) {
    return (INKMLoc) mime_hdr_create(((HdrHeapSDKHandle *) bufp)->m_heap);
  } else {
    return (INKMLoc) INK_ERROR_PTR;
  }
}

INKReturnCode
INKMimeHdrDestroy(INKMBuffer bufp, INKMLoc obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(obj) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS))
      && isWriteable(bufp)) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
    mime_hdr_destroy(((HdrHeapSDKHandle *) bufp)->m_heap, mh);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKMLoc
INKMimeHdrClone(INKMBuffer dest_bufp, INKMBuffer src_bufp, INKMLoc src_hdr)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If not allowed, return NULL.
  if ((sdk_sanity_check_mbuffer(dest_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_mbuffer(src_bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(src_hdr) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(src_hdr) == INK_SUCCESS)) && isWriteable(dest_bufp)
    ) {
    HdrHeap *s_heap, *d_heap;
    MIMEHdrImpl *s_mh, *d_mh;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_mh = _hdr_mloc_to_mime_hdr_impl(src_hdr);

    d_mh = mime_hdr_clone(s_mh, s_heap, d_heap, (s_heap != d_heap));
    return ((INKMLoc) d_mh);
  } else {
    return (INKMLoc) INK_ERROR_PTR;
  }
}

INKReturnCode
INKMimeHdrCopy(INKMBuffer dest_bufp, INKMLoc dest_obj, INKMBuffer src_bufp, INKMLoc src_obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(src_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(src_obj) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(src_obj) == INK_SUCCESS)) &&
      ((sdk_sanity_check_mime_hdr_handle(dest_obj) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(dest_obj) == INK_SUCCESS)) && isWriteable(dest_bufp)) {
    HdrHeap *s_heap, *d_heap;
    MIMEHdrImpl *s_mh, *d_mh;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_mh = _hdr_mloc_to_mime_hdr_impl(src_obj);
    d_mh = _hdr_mloc_to_mime_hdr_impl(dest_obj);

    mime_hdr_fields_clear(d_heap, d_mh);
    mime_hdr_copy_onto(s_mh, s_heap, d_mh, d_heap, (s_heap != d_heap));
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrPrint(INKMBuffer bufp, INKMLoc obj, INKIOBuffer iobufp)
{
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(obj) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS))
      && (sdk_sanity_check_iocore_structure(iobufp) == INK_SUCCESS)
    ) {
    HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
    MIOBuffer *b = (MIOBuffer *) iobufp;
    IOBufferBlock *blk;
    int bufindex;
    int tmp, dumpoffset;
    int done;

    dumpoffset = 0;

    do {
      blk = b->get_current_block();
      if (!blk || blk->write_avail() == 0) {
        b->add_block();
        blk = b->get_current_block();
      }

      bufindex = 0;
      tmp = dumpoffset;
      done = mime_hdr_print(heap, mh, blk->end(), blk->write_avail(), &bufindex, &tmp);

      dumpoffset += bufindex;
      b->fill(bufindex);
    } while (!done);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

int
INKMimeHdrParse(INKMimeParser parser, INKMBuffer bufp, INKMLoc obj, const char **start, const char *end)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      ((sdk_sanity_check_mime_hdr_handle(obj) != INK_SUCCESS) && (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS))
      || (start == NULL) || (*start == NULL) || (!isWriteable(bufp))
    ) {
    return INK_PARSE_ERROR;
  }
  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
  return (mime_parser_parse((MIMEParser *) parser, ((HdrHeapSDKHandle *) bufp)->m_heap, mh, start, end, false, false));
}

int
INKMimeHdrLengthGet(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(obj) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS))
    ) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
    return (mime_hdr_length_get(mh));
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldsClear(INKMBuffer bufp, INKMLoc obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(obj) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS))
      && isWriteable(bufp)) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
    mime_hdr_fields_clear(((HdrHeapSDKHandle *) bufp)->m_heap, mh);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

int
INKMimeHdrFieldsCount(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(obj) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS))
    ) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
    return (mime_hdr_fields_count(mh));
  } else {
    return INK_ERROR;
  }
}

/*************/
/* MimeField */
/*************/

// NOTE: The INKMimeFieldCreate interface is being replaced by
// INKMimeHdrFieldCreate.  The implementation below is tortuous, to
// mimic the behavior of an SDK with stand-alone fields.  The new
// header system does not support standalone fields, thus mimicry.

INKMLoc
INKMimeFieldCreate(INKMBuffer bufp)
{
  sdk_sanity_check_mbuffer(bufp);

  MIMEField *sa_field;
  HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  NOWARN_UNUSED(heap);

  // (1) create a standalone field object in the heap
  sa_field = sdk_alloc_standalone_field(bufp);
  mime_field_init(sa_field);

  // (2) create a field handle
  MIMEFieldSDKHandle *field_handle = sdk_alloc_field_handle(bufp, NULL);
  field_handle->field_ptr = sa_field;

  return (field_handle);
}

void
INKMimeFieldDestroy(INKMBuffer bufp, INKMLoc field_or_sa)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_or_sa);

  MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field_or_sa;

  if (field_handle->mh == NULL) // standalone field
    sdk_free_standalone_field(bufp, field_handle->field_ptr);
  else
    mime_field_destroy(field_handle->mh, field_handle->field_ptr);

  // for consistence, the handle will not be released here.
  // users will be required to do it.

  //sdk_free_field_handle(bufp, field_handle);
}

void
INKMimeFieldCopy(INKMBuffer dest_bufp, INKMLoc dest_obj, INKMBuffer src_bufp, INKMLoc src_obj)
{
  int src_attached, dest_attached;

  sdk_sanity_check_mbuffer(src_bufp);
  sdk_sanity_check_mbuffer(dest_bufp);
  sdk_sanity_check_field_handle(src_obj);
  sdk_sanity_check_field_handle(dest_obj);

  MIMEFieldSDKHandle *s_handle = (MIMEFieldSDKHandle *) src_obj;
  MIMEFieldSDKHandle *d_handle = (MIMEFieldSDKHandle *) dest_obj;
  HdrHeap *d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;

  // FIX: This tortuous detach/change/attach algorithm is due to the
  //      fact that we can't change the name of an attached header (assertion)

  src_attached = (s_handle->mh && s_handle->field_ptr->is_live());
  dest_attached = (d_handle->mh && d_handle->field_ptr->is_live());

  if (dest_attached)
    mime_hdr_field_detach(d_handle->mh, d_handle->field_ptr, false);

  mime_field_name_value_set(d_heap, d_handle->mh, d_handle->field_ptr,
                            s_handle->field_ptr->m_wks_idx,
                            s_handle->field_ptr->m_ptr_name,
                            s_handle->field_ptr->m_len_name,
                            s_handle->field_ptr->m_ptr_value, s_handle->field_ptr->m_len_value, 0, 0, true);

  if (dest_attached)
    mime_hdr_field_attach(d_handle->mh, d_handle->field_ptr, 1, NULL);
}

void
INKMimeFieldCopyValues(INKMBuffer dest_bufp, INKMLoc dest_obj, INKMBuffer src_bufp, INKMLoc src_obj)
{
  int dest_attached;
  NOWARN_UNUSED(dest_attached);

  sdk_sanity_check_mbuffer(src_bufp);
  sdk_sanity_check_mbuffer(dest_bufp);
  sdk_sanity_check_field_handle(src_obj);
  sdk_sanity_check_field_handle(dest_obj);

  MIMEFieldSDKHandle *s_handle = (MIMEFieldSDKHandle *) src_obj;
  MIMEFieldSDKHandle *d_handle = (MIMEFieldSDKHandle *) dest_obj;
  HdrHeap *d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
  MIMEField *s_field, *d_field;

  s_field = s_handle->field_ptr;
  d_field = d_handle->field_ptr;
  mime_field_value_set(d_heap, d_handle->mh, d_field, s_field->m_ptr_value, s_field->m_len_value, true);
}

// FIX: This is implemented horribly slowly, but who's using it anyway?
//      If we threaded all the MIMEFields, this function could be easier,
//      but we'd have to print dups in order and we'd need a flag saying
//      end of dup list or dup follows.

INKMLoc
INKMimeFieldNext(INKMBuffer bufp, INKMLoc field_obj)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;
  if (handle->mh == NULL)
    return (NULL);

  int slotnum = mime_hdr_field_slotnum(handle->mh, handle->field_ptr);
  if (slotnum == -1)
    return (NULL);

  while (1) {
    ++slotnum;
    MIMEField *f = mime_hdr_field_get_slotnum(handle->mh, slotnum);

    if (f == NULL)
      return (NULL);
    if (f->is_live()) {
      MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, handle->mh);
      h->field_ptr = f;
      return (h);
    }
  }
}

int
INKMimeFieldLengthGet(INKMBuffer bufp, INKMLoc field_obj)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;
  return mime_field_length_get(handle->field_ptr);
}

const char *
INKMimeFieldNameGet(INKMBuffer bufp, INKMLoc field_obj, int *length)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  int name_len;
  const char *name_ptr;
  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;

  name_ptr = mime_field_name_get(handle->field_ptr, &name_len);
  if (length)
    *length = name_len;
  return (((HdrHeapSDKHandle *) bufp)->make_sdk_string(name_ptr, name_len));
}

void
INKMimeFieldNameSet(INKMBuffer bufp, INKMLoc field_obj, const char *name, int length)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  if (length == -1)
    length = strlen(name);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;
  HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;

  int attached = (handle->mh && handle->field_ptr->is_live());
  if (attached)
    mime_hdr_field_detach(handle->mh, handle->field_ptr, false);

  handle->field_ptr->name_set(heap, handle->mh, name, length);

  if (attached)
    mime_hdr_field_attach(handle->mh, handle->field_ptr, 1, NULL);
}

void
INKMimeFieldValuesClear(INKMBuffer bufp, INKMLoc field_obj)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;
  HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    /**
     * Modified the string value passed from an empty string ("") to NULL.
     * An empty string is also considered to be a token. The correct value of
     * the field after this function should be NULL.
     */
  mime_field_value_set(heap, handle->mh, handle->field_ptr, NULL, 0, 1);
}

int
INKMimeFieldValuesCount(INKMBuffer bufp, INKMLoc field_obj)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;
  return (mime_field_value_get_comma_val_count(handle->field_ptr));
}

const char *
INKMimeFieldValueGet(INKMBuffer bufp, INKMLoc field_obj, int idx, int *value_len_ptr)
{
  const char *value_str;
  int compat_length = 0;

  if (value_len_ptr == NULL) {
    value_len_ptr = &compat_length;
  }

  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;

  if (idx >= 0) {
    value_str = mime_field_value_get_comma_val(handle->field_ptr, value_len_ptr, idx);
  } else {
    value_str = mime_field_value_get(handle->field_ptr, value_len_ptr);
    if (value_str == NULL)
      value_str = "";           // don't return NULL for whole value
  }

  return (((HdrHeapSDKHandle *) bufp)->make_sdk_string(value_str, *value_len_ptr));
}

int
INKMimeFieldValueGetInt(INKMBuffer bufp, INKMLoc field_obj, int idx)
{
  int value;
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  int value_len;
  const char *value_str = INKMimeFieldValueGet(bufp, field_obj, idx, &value_len);
  if (value_str == NULL)
    return (0);
  value = mime_parse_int(value_str, value_str + value_len);
  ((HdrHeapSDKHandle *) bufp)->destroy_sdk_string((char *) value_str);
  return value;
}

unsigned int
INKMimeFieldValueGetUint(INKMBuffer bufp, INKMLoc field_obj, int idx)
{
  unsigned int value;
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  int value_len;
  const char *value_str = INKMimeFieldValueGet(bufp, field_obj, idx, &value_len);
  if (value_str == NULL)
    return (0);
  value = mime_parse_uint(value_str, value_str + value_len);
  ((HdrHeapSDKHandle *) bufp)->destroy_sdk_string((char *) value_str);
  return value;
}

time_t
INKMimeFieldValueGetDate(INKMBuffer bufp, INKMLoc field_obj, int idx)
{
  time_t value;
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  int value_len;
  // idx is ignored for GetDate
  //const char *value_str = INKMimeFieldValueGet(bufp, field_obj, idx, &value_len);
  const char *value_str = INKMimeFieldValueGet(bufp, field_obj, -1, &value_len);
  if (value_str == NULL)
    return ((time_t) 0);
  value = mime_parse_date(value_str, value_str + value_len);
  ((HdrHeapSDKHandle *) bufp)->destroy_sdk_string((char *) value_str);
  return value;
}

void
INKMimeFieldValueSet(INKMBuffer bufp, INKMLoc field_obj, int idx, const char *value, int length)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  if (value == NULL) {
    value = "";
    length = 0;
  }
  if (length == -1)
    length = strlen(value);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;
  HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;

  if (idx >= 0)
    mime_field_value_set_comma_val(heap, handle->mh, handle->field_ptr, idx, value, length);
  else
    mime_field_value_set(heap, handle->mh, handle->field_ptr, value, length, true);
}

void
INKMimeFieldValueSetInt(INKMBuffer bufp, INKMLoc field_obj, int idx, int value)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  char tmp[16];
  int len = mime_format_int(tmp, value, sizeof(tmp));
  INKMimeFieldValueSet(bufp, field_obj, idx, tmp, len);
}

void
INKMimeFieldValueSetUint(INKMBuffer bufp, INKMLoc field_obj, int idx, unsigned int value)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  char tmp[16];
  int len = mime_format_uint(tmp, value, sizeof(tmp));
  INKMimeFieldValueSet(bufp, field_obj, idx, tmp, len);
}

void
INKMimeFieldValueSetDate(INKMBuffer bufp, INKMLoc field_obj, int idx, time_t value)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  char tmp[33];
  int len = mime_format_date(tmp, value);
  // idx is ignored and we overwrite all existing values
  // INKMimeFieldValueSet(bufp, field_obj, idx, tmp, len);
  INKMimeFieldValueSet(bufp, field_obj, -1, tmp, len);
}

void
INKMimeFieldValueAppend(INKMBuffer bufp, INKMLoc field_obj, int idx, const char *value, int length)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  if (length == -1)
    length = strlen(value);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;
  HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  mime_field_value_extend_comma_val(heap, handle->mh, handle->field_ptr, idx, value, length);
}

INKMLoc
INKMimeFieldValueInsert(INKMBuffer bufp, INKMLoc field_obj, const char *value, int length, int idx)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  if (length == -1)
    length = strlen(value);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;
  HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  mime_field_value_insert_comma_val(heap, handle->mh, handle->field_ptr, idx, value, length);
  return (INK_NULL_MLOC);
}

INKMLoc
INKMimeFieldValueInsertInt(INKMBuffer bufp, INKMLoc field_obj, int value, int idx)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  char tmp[16];
  int len = mime_format_int(tmp, value, sizeof(tmp));
  (void) INKMimeFieldValueInsert(bufp, field_obj, tmp, len, idx);
  return (INK_NULL_MLOC);
}

INKMLoc
INKMimeFieldValueInsertUint(INKMBuffer bufp, INKMLoc field_obj, unsigned int value, int idx)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  char tmp[16];
  int len = mime_format_uint(tmp, value, sizeof(tmp));
  (void) INKMimeFieldValueInsert(bufp, field_obj, tmp, len, idx);
  return (INK_NULL_MLOC);
}

INKMLoc
INKMimeFieldValueInsertDate(INKMBuffer bufp, INKMLoc field_obj, time_t value, int idx)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  char tmp[33];
  int len = mime_format_date(tmp, value);
  // idx ignored, overwrite all exisiting values
  // (void)INKMimeFieldValueInsert(bufp, field_obj, tmp, len, idx);
  (void) INKMimeFieldValueSet(bufp, field_obj, -1, tmp, len);

  return (INK_NULL_MLOC);
}

void
INKMimeFieldValueDelete(INKMBuffer bufp, INKMLoc field_obj, int idx)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;
  HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  mime_field_value_delete_comma_val(heap, handle->mh, handle->field_ptr, idx);
}

/****************/
/* MimeHdrField */
/****************/

// INKMBuffer: pointers to HdrHeapSDKHandle objects
// INKMLoc:    pointers to MIMEFieldSDKHandle objects

int
INKMimeHdrFieldEqual(INKMBuffer bufp, INKMLoc hdr_obj, INKMLoc field1_obj, INKMLoc field2_obj)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field1_obj, hdr_obj);
  sdk_sanity_check_field_handle(field2_obj, hdr_obj);

  MIMEFieldSDKHandle *field1_handle = (MIMEFieldSDKHandle *) field1_obj;
  MIMEFieldSDKHandle *field2_handle = (MIMEFieldSDKHandle *) field2_obj;

  if ((field1_handle == NULL) || (field2_handle == NULL))
    return (field1_handle == field2_handle);

  return (field1_handle->field_ptr == field2_handle->field_ptr);
}

INKMLoc
INKMimeHdrFieldGet(INKMBuffer bufp, INKMLoc hdr_obj, int idx)
{

  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr_obj) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(hdr_obj) == INK_SUCCESS)) && (idx >= 0)
    ) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(hdr_obj);
    MIMEField *f = mime_hdr_field_get(mh, idx);
    if (f == NULL)
      return ((INKMLoc) NULL);

    MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, mh);
    h->field_ptr = f;
    return (h);
  } else {
    return (INKMLoc) INK_ERROR_PTR;
  }
}

INKMLoc
INKMimeHdrFieldFind(INKMBuffer bufp, INKMLoc hdr_obj, const char *name, int length)
{
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr_obj) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(hdr_obj) == INK_SUCCESS)) && (name != NULL)
    ) {

    if (length == -1)
      length = strlen(name);

    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(hdr_obj);
    MIMEField *f = mime_hdr_field_find(mh, name, length);
    if (f == NULL)
      return ((INKMLoc) NULL);

    MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, mh);
    h->field_ptr = f;
    return (h);
  } else {
    return (INKMLoc) INK_ERROR_PTR;
  }
}

// DEPRECATED
INKMLoc
INKMimeHdrFieldRetrieve(INKMBuffer bufp, INKMLoc hdr_obj, const char *name)
{
  int length;

  sdk_sanity_check_mbuffer(bufp);
  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(hdr_obj);

  if (hdrtoken_is_wks(name))
    length = hdrtoken_wks_to_length(name);
  else
    length = strlen(name);

  MIMEField *f = mime_hdr_field_find(mh, name, length);
  if (f == NULL)
    return ((INKMLoc) NULL);

  MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, mh);
  h->field_ptr = mime_hdr_field_find(mh, name, length);
  return (h);
}

INKReturnCode
INKMimeHdrFieldAppend(INKMBuffer bufp, INKMLoc mh_mloc, INKMLoc field_mloc)
{
  return INKMimeHdrFieldInsert(bufp, mh_mloc, field_mloc, -1);
}

// DEPRECATED
INKReturnCode
INKMimeHdrFieldInsert(INKMBuffer bufp, INKMLoc mh_mloc, INKMLoc field_mloc, int idx)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(mh_mloc) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(mh_mloc) == INK_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field_mloc) == INK_SUCCESS) && isWriteable(bufp)
    ) {

    MIMEField *mh_field;
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
    MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field_mloc;


    //////////////////////////////////////////////////////////////////////
    // The field passed in field_mloc might have been allocated from    //
    // inside a MIME header (the correct way), or it might have been    //
    // created in isolation as a "standalone field" (the old way).      //
    //                                                                  //
    // If it's a standalone field (the associated mime header is NULL), //
    // then we need to now allocate a real field inside the header,     //
    // copy over the data, and convert the standalone field into a      //
    // forwarding pointer to the real field, in case it's used again    //
    //////////////////////////////////////////////////////////////////////

    if (field_handle->mh == NULL) {
      HdrHeap *heap = (HdrHeap *) (((HdrHeapSDKHandle *) bufp)->m_heap);

      // allocate a new hdr field and copy any pre-set info
      mh_field = mime_field_create(heap, mh);

      // FIX: is it safe to copy everything over?
      memcpy(mh_field, field_handle->field_ptr, sizeof(MIMEField));

      // now set up the forwarding ptr from standalone field to hdr field
      field_handle->mh = mh;
      field_handle->field_ptr = mh_field;
    }

    INKAssert(field_handle->mh == mh);

    /////////////////////////////////////////////////////////////////////
    // The underlying header system doesn't let you insert unnamed     //
    // headers, but the SDK examples show you doing exactly that.  So, //
    // we need to mimic this case by creating a fake field name.       //
    /////////////////////////////////////////////////////////////////////

    if (field_handle->field_ptr->m_ptr_name == NULL) {
      char noname[20];
      intptr_t addr = (intptr_t) (field_handle->field_ptr);
      snprintf(noname, sizeof(noname), "@X-Noname-%016llX", (ink64)addr);
      INKMimeFieldNameSet(bufp, field_mloc, noname, 26);
    }

    mime_hdr_field_attach(mh, field_handle->field_ptr, 1, NULL);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldRemove(INKMBuffer bufp, INKMLoc mh_mloc, INKMLoc field_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(mh_mloc) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(mh_mloc) == INK_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field_mloc, mh_mloc) == INK_SUCCESS) && isWriteable(bufp)) {

    MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field_mloc;

    if (field_handle->mh != NULL) {
      MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
      INKAssert(mh == field_handle->mh);
      sdk_sanity_check_field_handle(field_mloc, mh_mloc);
      mime_hdr_field_detach(mh, field_handle->field_ptr, false);        // only detach this dup
    }
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

// DEPRECATED

INKReturnCode
INKMimeHdrFieldDelete(INKMBuffer bufp, INKMLoc mh_mloc, INKMLoc field_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(mh_mloc) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(mh_mloc) == INK_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field_mloc, mh_mloc) == INK_SUCCESS) && isWriteable(bufp)) {

    MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field_mloc;

    if (field_handle->mh == NULL)       // standalone field
    {
      MIMEField *field_ptr = field_handle->field_ptr;
      ink_assert(field_ptr->m_readiness != MIME_FIELD_SLOT_READINESS_DELETED);
      sdk_free_standalone_field(bufp, field_ptr);
    } else if (field_handle->mh != NULL) {
      MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
      HdrHeap *heap = (HdrHeap *) (((HdrHeapSDKHandle *) bufp)->m_heap);
      INKAssert(mh == field_handle->mh);
      sdk_sanity_check_field_handle(field_mloc, mh_mloc);

      // detach and delete this field, but not all dups
      mime_hdr_field_delete(heap, mh, field_handle->field_ptr, false);
    }
    // for consistence, the handle will not be released here.
    // users will be required to do it.

    //sdk_free_field_handle(bufp, field_handle);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldDestroy(INKMBuffer bufp, INKMLoc mh_mloc, INKMLoc field_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  // Since INKMimeHdrFieldDelete does this check, it is not done here.
  return (INKMimeHdrFieldDelete(bufp, mh_mloc, field_mloc));
}

INKMLoc
INKMimeHdrFieldCreate(INKMBuffer bufp, INKMLoc mh_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If not allowed, return NULL.
  // Changed the return value to INK_ERROR_PTR from NULL in case of errors.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(mh_mloc) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(mh_mloc) == INK_SUCCESS)) && isWriteable(bufp)) {

    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
    HdrHeap *heap = (HdrHeap *) (((HdrHeapSDKHandle *) bufp)->m_heap);

    MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, mh);
    h->field_ptr = mime_field_create(heap, mh);
    return (h);
  } else {
    return (INKMLoc) INK_ERROR_PTR;
  }
}

INKMLoc
INKMimeHdrFieldCreateNamed(INKMBuffer bufp, INKMLoc mh_mloc, const char *name, int name_len)
{
  sdk_sanity_check_mbuffer(bufp);

  if (name_len == -1)
    name_len = strlen(name);

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
  HdrHeap *heap = (HdrHeap *) (((HdrHeapSDKHandle *) bufp)->m_heap);

  MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, mh);
  h->field_ptr = mime_field_create_named(heap, mh, name, name_len);
  return (h);
}

INKReturnCode
INKMimeHdrFieldCopy(INKMBuffer dest_bufp, INKMLoc dest_hdr, INKMLoc dest_field,
                    INKMBuffer src_bufp, INKMLoc src_hdr, INKMLoc src_field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(src_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(src_hdr) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(src_hdr) == INK_SUCCESS)) &&
      ((sdk_sanity_check_mime_hdr_handle(dest_hdr) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(dest_hdr) == INK_SUCCESS)) &&
      (sdk_sanity_check_field_handle(src_field, src_hdr) == INK_SUCCESS) &&
      (sdk_sanity_check_field_handle(dest_field, dest_hdr) == INK_SUCCESS) && isWriteable(dest_bufp)) {

    INKMimeFieldCopy(dest_bufp, dest_field, src_bufp, src_field);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKMLoc
INKMimeHdrFieldClone(INKMBuffer dest_bufp, INKMLoc dest_hdr, INKMBuffer src_bufp, INKMLoc src_hdr, INKMLoc src_field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If not allowed, return NULL.
  if ((sdk_sanity_check_mbuffer(dest_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_mbuffer(src_bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(dest_hdr) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(dest_hdr) == INK_SUCCESS)) &&
      ((sdk_sanity_check_mime_hdr_handle(src_hdr) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(src_hdr) == INK_SUCCESS)) &&
      (sdk_sanity_check_field_handle(src_field, src_hdr) == INK_SUCCESS) && isWriteable(dest_bufp)
    ) {
    INKMLoc dest_field = INKMimeHdrFieldCreate(dest_bufp, dest_hdr);
    sdk_sanity_check_field_handle(dest_field, dest_hdr);

    INKMimeHdrFieldCopy(dest_bufp, dest_hdr, dest_field, src_bufp, src_hdr, src_field);
    return (dest_field);
  } else {
    return (INKMLoc) INK_ERROR_PTR;
  }
}

INKReturnCode
INKMimeHdrFieldCopyValues(INKMBuffer dest_bufp, INKMLoc dest_hdr, INKMLoc dest_field,
                          INKMBuffer src_bufp, INKMLoc src_hdr, INKMLoc src_field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(src_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(src_hdr) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(src_hdr) == INK_SUCCESS)) &&
      ((sdk_sanity_check_mime_hdr_handle(dest_hdr) == INK_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(dest_hdr) == INK_SUCCESS)) &&
      (sdk_sanity_check_field_handle(src_field, src_hdr) == INK_SUCCESS) &&
      (sdk_sanity_check_field_handle(dest_field, dest_hdr) == INK_SUCCESS) && isWriteable(dest_bufp)) {

    INKMimeFieldCopyValues(dest_bufp, dest_field, src_bufp, src_field);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKMLoc
INKMimeHdrFieldNext(INKMBuffer bufp, INKMLoc hdr, INKMLoc field)
{
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS)
    ) {
    return (INKMimeFieldNext(bufp, field));
  } else {
    return (INKMLoc) INK_ERROR_PTR;
  }
}

INKMLoc
INKMimeHdrFieldNextDup(INKMBuffer bufp, INKMLoc hdr, INKMLoc field)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      ((sdk_sanity_check_mime_hdr_handle(hdr) != INK_SUCCESS) && (sdk_sanity_check_http_hdr_handle(hdr) != INK_SUCCESS))
      || (sdk_sanity_check_field_handle(field, hdr) != INK_SUCCESS)
    ) {
    return (INKMLoc) INK_ERROR_PTR;
  }

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(hdr);
  MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field;
  MIMEField *next = field_handle->field_ptr->m_next_dup;
  if (next == NULL)
    return ((INKMLoc) NULL);

  MIMEFieldSDKHandle *next_handle = sdk_alloc_field_handle(bufp, mh);
  next_handle->field_ptr = next;
  return ((INKMLoc) next_handle);
}

int
INKMimeHdrFieldLengthGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      ((sdk_sanity_check_mime_hdr_handle(hdr) != INK_SUCCESS) &&
       (sdk_sanity_check_http_hdr_handle(hdr) != INK_SUCCESS)) ||
      (sdk_sanity_check_field_handle(field, hdr) != INK_SUCCESS)
    ) {
    return INK_ERROR;
  }
  return (INKMimeFieldLengthGet(bufp, field));
}

const char *
INKMimeHdrFieldNameGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int *length)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      ((sdk_sanity_check_mime_hdr_handle(hdr) != INK_SUCCESS) && (sdk_sanity_check_http_hdr_handle(hdr) != INK_SUCCESS))
      || (sdk_sanity_check_field_handle(field, hdr) != INK_SUCCESS)
    ) {
    return (const char *) INK_ERROR_PTR;
  }
  return (INKMimeFieldNameGet(bufp, field, length));
}

INKReturnCode
INKMimeHdrFieldNameSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, const char *name, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) &&
      (sdk_sanity_check_null_ptr((void *) name) == INK_SUCCESS) && isWriteable(bufp)) {
    if (length == -1)
      length = strlen(name);
    INKMimeFieldNameSet(bufp, field, name, length);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldValuesClear(INKMBuffer bufp, INKMLoc hdr, INKMLoc field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    INKMimeFieldValuesClear(bufp, field);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

int
INKMimeHdrFieldValuesCount(INKMBuffer bufp, INKMLoc hdr, INKMLoc field)
{
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS)
    ) {
    return (INKMimeFieldValuesCount(bufp, field));
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldValueStringGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, const char **value_ptr,
                              int *value_len_ptr)
{
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && (value_ptr != NULL) &&
      sdk_sanity_check_null_ptr((void *) value_len_ptr) == INK_SUCCESS) {
    *value_ptr = INKMimeHdrFieldValueGet(bufp, hdr, field, idx, value_len_ptr);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldValueDateGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, time_t * value_ptr)
{
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && (value_ptr != NULL)
    ) {
    *value_ptr = INKMimeHdrFieldValueGetDate(bufp, hdr, field, 0);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldValueIntGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, int *value_ptr)
{
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && (value_ptr != NULL)
    ) {
    *value_ptr = INKMimeHdrFieldValueGetInt(bufp, hdr, field, idx);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldValueUintGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, unsigned int *value_ptr)
{
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && (value_ptr != NULL)
    ) {
    *value_ptr = INKMimeHdrFieldValueGetUint(bufp, hdr, field, idx);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

// DEPRECATED
const char *
INKMimeHdrFieldValueGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, int *value_len_ptr)
{
  return (INKMimeFieldValueGet(bufp, field, idx, value_len_ptr));
}

const char *
INKMimeHdrFieldValueGetRaw(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int *value_len_ptr)
{
  sdk_sanity_check_field_handle(field, hdr);
  return (INKMimeFieldValueGet(bufp, field, -1, value_len_ptr));
}

// DEPRECATED
int
INKMimeHdrFieldValueGetInt(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx)
{
  return (INKMimeFieldValueGetInt(bufp, field, idx));
}

// DEPRECATED
unsigned int
INKMimeHdrFieldValueGetUint(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx)
{
  return (INKMimeFieldValueGetUint(bufp, field, idx));
}

// DEPRECATED
time_t
INKMimeHdrFieldValueGetDate(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx)
{
  return (INKMimeFieldValueGetDate(bufp, field, idx));
}

INKReturnCode
INKMimeHdrFieldValueStringSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, const char *value, int length)
{
  return INKMimeHdrFieldValueSet(bufp, hdr, field, idx, value, length);
}

INKReturnCode
INKMimeHdrFieldValueDateSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, time_t value)
{
  return INKMimeHdrFieldValueSetDate(bufp, hdr, field, 0, value);
}

INKReturnCode
INKMimeHdrFieldValueIntSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, int value)
{
  return INKMimeHdrFieldValueSetInt(bufp, hdr, field, idx, value);
}

INKReturnCode
INKMimeHdrFieldValueUintSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, unsigned int value)
{
  return INKMimeHdrFieldValueSetUint(bufp, hdr, field, idx, value);
}

// DEPRECATED
INKReturnCode
INKMimeHdrFieldValueSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) &&
      (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    if (length == -1)
      length = strlen(value);
    INKMimeFieldValueSet(bufp, field, idx, value, length);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}


INKReturnCode
INKMimeHdrFieldValueSetRaw(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    if (length == -1)
      length = strlen(value);
    INKMimeFieldValueSet(bufp, field, -1, value, length);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

// DEPRECATED
INKReturnCode
INKMimeHdrFieldValueSetInt(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, int value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    INKMimeFieldValueSetInt(bufp, field, idx, value);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

// DEPRECATED
INKReturnCode
INKMimeHdrFieldValueSetUint(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, unsigned int value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    INKMimeFieldValueSetUint(bufp, field, idx, value);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

// DEPRECATED
INKReturnCode
INKMimeHdrFieldValueSetDate(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, time_t value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    INKMimeFieldValueSetDate(bufp, field, idx, value);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldValueAppend(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && (idx >= 0) && (value != NULL) &&
      isWriteable(bufp)
    ) {
    if (length == -1)
      length = strlen(value);
    INKMimeFieldValueAppend(bufp, field, idx, value, length);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldValueStringInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, const char *value, int length)
{
  return INKMimeHdrFieldValueInsert(bufp, hdr, field, value, length, idx);
}

INKReturnCode
INKMimeHdrFieldValueIntInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, int value)
{
  return INKMimeHdrFieldValueInsertInt(bufp, hdr, field, value, idx);
}

INKReturnCode
INKMimeHdrFieldValueUintInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, unsigned int value)
{
  return INKMimeHdrFieldValueInsertUint(bufp, hdr, field, value, idx);
}

INKReturnCode
INKMimeHdrFieldValueDateInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, time_t value)
{
  if (INKMimeHdrFieldValuesClear(bufp, hdr, field) == INK_ERROR) {
    return INK_ERROR;
  }
  return INKMimeHdrFieldValueInsertDate(bufp, hdr, field, value, -1);
}

// DEPRECATED
INKReturnCode
INKMimeHdrFieldValueInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, const char *value, int length, int idx)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR, else return INK_SUCCESS.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) &&
      (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    if (length == -1)
      length = strlen(value);
    INKMimeFieldValueInsert(bufp, field, value, length, idx);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}


// DEPRECATED
INKReturnCode
INKMimeHdrFieldValueInsertInt(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int value, int idx)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR, else return INK_SUCCESS.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    INKMimeFieldValueInsertInt(bufp, field, value, idx);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

// DEPRECATED
INKReturnCode
INKMimeHdrFieldValueInsertUint(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, unsigned int value, int idx)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR, else return INK_SUCCESS.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && isWriteable(bufp)) {
    INKMimeFieldValueInsertUint(bufp, field, value, idx);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

// DEPRECATED
INKReturnCode
INKMimeHdrFieldValueInsertDate(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, time_t value, int idx)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR, else return INK_SUCCESS
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    INKMimeFieldValueInsertDate(bufp, field, value, idx);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKMimeHdrFieldValueDelete(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == INK_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == INK_SUCCESS) && (idx >= 0) && isWriteable(bufp)
    ) {
    INKMimeFieldValueDelete(bufp, field, idx);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

/**************/
/* HttpParser */
/**************/

INKHttpParser
INKHttpParserCreate()
{
  INKHttpParser parser;

  parser = INKmalloc(sizeof(HTTPParser));
  if (sdk_sanity_check_http_parser(parser) != INK_SUCCESS) {
    return (INKHttpParser) INK_ERROR_PTR;
  }
  http_parser_init((HTTPParser *) parser);

  return parser;
}

INKReturnCode
INKHttpParserClear(INKHttpParser parser)
{
  if (sdk_sanity_check_http_parser(parser) != INK_SUCCESS) {
    return INK_ERROR;
  }
  http_parser_clear((HTTPParser *) parser);
  return INK_SUCCESS;
}

INKReturnCode
INKHttpParserDestroy(INKHttpParser parser)
{
  if (sdk_sanity_check_http_parser(parser) != INK_SUCCESS) {
    return INK_ERROR;
  }
  http_parser_clear((HTTPParser *) parser);
  INKfree(parser);
  return INK_SUCCESS;
}

/***********/
/* HttpHdr */
/***********/


INKMLoc
INKHttpHdrCreate(INKMBuffer bufp)
{
  if (sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) {
    return (INKMLoc) INK_ERROR_PTR;
  }

  HTTPHdr h;
  h.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  h.create(HTTP_TYPE_UNKNOWN);
  return ((INKMLoc) (h.m_http));
}

INKReturnCode
INKHttpHdrDestroy(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS)
    ) {
    return INK_ERROR;
  }
  // No more objects counts in heap or deallocation
  //   so do nothing!
  return INK_SUCCESS;

  // HDR FIX ME - Did this free the MBuffer in Pete's old system
}

INKMLoc
INKHttpHdrClone(INKMBuffer dest_bufp, INKMBuffer src_bufp, INKMLoc src_hdr)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If not allowed, return NULL.
  if ((sdk_sanity_check_mbuffer(dest_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_mbuffer(src_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(src_hdr) == INK_SUCCESS) && isWriteable(dest_bufp)
    ) {
    bool inherit_strs;
    HdrHeap *s_heap, *d_heap;
    HTTPHdrImpl *s_hh, *d_hh;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_hh = (HTTPHdrImpl *) src_hdr;

    ink_assert(s_hh->m_type == HDR_HEAP_OBJ_HTTP_HEADER);

    inherit_strs = (s_heap != d_heap ? true : false);

    d_hh = http_hdr_clone(s_hh, s_heap, d_heap);
    return ((INKMLoc) d_hh);
  } else {
    return (INKMLoc) INK_ERROR_PTR;
  }
}

INKReturnCode
INKHttpHdrCopy(INKMBuffer dest_bufp, INKMLoc dest_obj, INKMBuffer src_bufp, INKMLoc src_obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(src_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(dest_obj) == INK_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(src_obj) == INK_SUCCESS) && isWriteable(dest_bufp)
    ) {
    bool inherit_strs;
    HdrHeap *s_heap, *d_heap;
    HTTPHdrImpl *s_hh, *d_hh;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_hh = (HTTPHdrImpl *) src_obj;
    d_hh = (HTTPHdrImpl *) dest_obj;

    ink_assert(s_hh->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
    ink_assert(d_hh->m_type == HDR_HEAP_OBJ_HTTP_HEADER);

    inherit_strs = (s_heap != d_heap ? true : false);

    INKHttpHdrTypeSet(dest_bufp, dest_obj, (INKHttpType) (s_hh->m_polarity));
    http_hdr_copy_onto(s_hh, s_heap, d_hh, d_heap, inherit_strs);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKReturnCode
INKHttpHdrPrint(INKMBuffer bufp, INKMLoc obj, INKIOBuffer iobufp)
{
  MIOBuffer *b = (MIOBuffer *) iobufp;
  IOBufferBlock *blk;
  int bufindex;
  int tmp, dumpoffset;
  int done;

  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(iobufp) != INK_SUCCESS)
    ) {
    return INK_ERROR;
  }


  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);

  dumpoffset = 0;

  do {
    blk = b->get_current_block();
    if (!blk || blk->write_avail() == 0) {
      b->add_block();
      blk = b->get_current_block();
    }

    bufindex = 0;
    tmp = dumpoffset;

    done = h.print(blk->end(), blk->write_avail(), &bufindex, &tmp);

    dumpoffset += bufindex;
    b->fill(bufindex);
  } while (!done);
  return INK_SUCCESS;
}

int
INKHttpHdrParseReq(INKHttpParser parser, INKMBuffer bufp, INKMLoc obj, const char **start, const char *end)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS) ||
      (start == NULL) || (*start == NULL) || (!isWriteable(bufp))
    ) {
    return INK_PARSE_ERROR;
  }

  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
  INKHttpHdrTypeSet(bufp, obj, INK_HTTP_TYPE_REQUEST);
  return h.parse_req((HTTPParser *) parser, start, end, false);
}

int
INKHttpHdrParseResp(INKHttpParser parser, INKMBuffer bufp, INKMLoc obj, const char **start, const char *end)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) ||
      (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS) ||
      (start == NULL) || (*start == NULL) || (!isWriteable(bufp))
    ) {
    return INK_PARSE_ERROR;
  }
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
  INKHttpHdrTypeSet(bufp, obj, INK_HTTP_TYPE_RESPONSE);
  return h.parse_resp((HTTPParser *) parser, start, end, false);
}

int
INKHttpHdrLengthGet(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS)
    ) {
    return INK_ERROR;
  }

  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
  return h.length_get();
}

INKHttpType
INKHttpHdrTypeGet(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS)
    ) {
    return (INKHttpType) INK_ERROR;
  }
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
   */
  return (INKHttpType) h.type_get();
}

INKReturnCode
INKHttpHdrTypeSet(INKMBuffer bufp, INKMLoc obj, INKHttpType type)
{
#ifdef DEBUG
  if ((type<INK_HTTP_TYPE_UNKNOWN) || (type> INK_HTTP_TYPE_RESPONSE)) {
    return INK_ERROR;
  }
#endif
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS) && isWriteable(bufp)
    ) {

    HTTPHdr h;
    SET_HTTP_HDR(h, bufp, obj);
    ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);

    // FIX: why are we using an HTTPHdr here?  why can't we
    //      just manipulate the impls directly?

    // In Pete's MBuffer system you can change the type
    //   at will.  Not so anymore.  We need to try to
    //   fake the difference.  We not going to let
    //   people change the types of a header.  If they
    //   try, too bad.

    if (h.m_http->m_polarity == HTTP_TYPE_UNKNOWN) {
      if (type == (INKHttpType) HTTP_TYPE_REQUEST) {
        h.m_http->u.req.m_url_impl = url_create(h.m_heap);
        h.m_http->m_polarity = (HTTPType) type;
      } else if (type == (INKHttpType) HTTP_TYPE_RESPONSE) {
        h.m_http->m_polarity = (HTTPType) type;
      }
    }
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

int
INKHttpHdrVersionGet(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS)
    ) {
    return INK_ERROR;
  }

  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
   */

  HTTPVersion ver = h.version_get();

  return ver.m_version;
}

INKReturnCode
INKHttpHdrVersionSet(INKMBuffer bufp, INKMLoc obj, int ver)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    HTTPHdr h;
    SET_HTTP_HDR(h, bufp, obj);
    ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);

    HTTPVersion version(ver);

    h.version_set(version);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

const char *
INKHttpHdrMethodGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS)
    ) {
    return (const char *) INK_ERROR_PTR;
  }
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  /* Don't need the assert as check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
   */

  int value_len;
  const char *value_ptr = h.method_get(&value_len);

  if (length) {
    *length = value_len;
  }

  if (value_ptr == NULL) {
    return NULL;
  }

  if (hdrtoken_is_wks(value_ptr)) {
    return value_ptr;
  } else {
    return ((HdrHeapSDKHandle *) bufp)->make_sdk_string(value_ptr, value_len);
  }
}

INKReturnCode
INKHttpHdrMethodSet(INKMBuffer bufp, INKMLoc obj, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS) &&
      isWriteable(bufp) && (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS)
    ) {
    HTTPHdr h;
    SET_HTTP_HDR(h, bufp, obj);
    /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
       ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
     */

    if (length < 0)
      length = strlen(value);

    h.method_set(value, length);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKMLoc
INKHttpHdrUrlGet(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS)
    ) {
    return (INKMLoc) INK_ERROR_PTR;
  }
  HTTPHdrImpl *hh = (HTTPHdrImpl *) obj;
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(hh->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
   */

  if (hh->m_polarity != HTTP_TYPE_REQUEST)
    return ((INKMLoc) INK_ERROR_PTR);
  else
    return ((INKMLoc) hh->u.req.m_url_impl);
}

INKReturnCode
INKHttpHdrUrlSet(INKMBuffer bufp, INKMLoc obj, INKMLoc url)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS) &&
      (sdk_sanity_check_url_handle(url) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    HTTPHdrImpl *hh = (HTTPHdrImpl *) obj;
    ink_assert(hh->m_type == HDR_HEAP_OBJ_HTTP_HEADER);

    URLImpl *url_impl = (URLImpl *) url;
    http_hdr_url_set(heap, hh, url_impl);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

INKHttpStatus
INKHttpHdrStatusGet(INKMBuffer bufp, INKMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS)
    ) {
    return (INKHttpStatus) INK_ERROR;
  }
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
   */
  return (INKHttpStatus) h.status_get();
}

INKReturnCode
INKHttpHdrStatusSet(INKMBuffer bufp, INKMLoc obj, INKHttpStatus status)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS) && isWriteable(bufp)
    ) {
    HTTPHdr h;
    SET_HTTP_HDR(h, bufp, obj);
    ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
    h.status_set((HTTPStatus) status);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

const char *
INKHttpHdrReasonGet(INKMBuffer bufp, INKMLoc obj, int *length)
{
  if ((sdk_sanity_check_mbuffer(bufp) != INK_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != INK_SUCCESS)
    ) {
    return (const char *) INK_ERROR_PTR;
  }
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
   */

  int value_len;
  const char *value_ptr = h.reason_get(&value_len);

  if (length)
    *length = value_len;
  if (value_ptr == NULL)
    return NULL;

  return ((HdrHeapSDKHandle *) bufp)->make_sdk_string(value_ptr, value_len);
}

INKReturnCode
INKHttpHdrReasonSet(INKMBuffer bufp, INKMLoc obj, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // INK_ERROR. If allowed, return INK_SUCCESS. Changed the
  // return value of function from void to INKReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == INK_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == INK_SUCCESS) &&
      isWriteable(bufp) && (sdk_sanity_check_null_ptr((void *) value) == INK_SUCCESS)
    ) {
    HTTPHdr h;
    SET_HTTP_HDR(h, bufp, obj);
    /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
       ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
     */

    if (length < 0)
      length = strlen(value);
    h.reason_set(value, length);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

const char *
INKHttpHdrReasonLookup(INKHttpStatus status)
{
  return http_hdr_reason_lookup((HTTPStatus) status);
}


/// END CODE REVIEW HERE

////////////////////////////////////////////////////////////////////
//
// Cache
//
////////////////////////////////////////////////////////////////////

inline INKReturnCode
sdk_sanity_check_cachekey(INKCacheKey key)
{
#ifdef DEBUG
  if (key == NULL || key == INK_ERROR_PTR || ((CacheInfo *) key)->magic != CACHE_INFO_MAGIC_ALIVE)
    return INK_ERROR;

  return INK_SUCCESS;
#else
  return INK_SUCCESS;
#endif
}

INKReturnCode
INKCacheKeyGet(INKCacheTxn txnp, void **key, int *length)
{
  NewCacheVC *vc = (NewCacheVC *) txnp;
  Debug("cache_plugin", "[INKCacheKeyGet] vc get cache key");
  //    *key = (void*) NEW(new INK_MD5);

  // just pass back the url and don't do the md5
  vc->getCacheKey(key, length);

  return INK_SUCCESS;
}

INKReturnCode
INKCacheHeaderKeyGet(INKCacheTxn txnp, void **key, int *length)
{
  NewCacheVC *vc = (NewCacheVC *) txnp;
  Debug("cache_plugin", "[INKCacheKeyGet] vc get cache header key");
  vc->getCacheHeaderKey(key, length);

  return INK_SUCCESS;
}

INKReturnCode
INKCacheKeyCreate(INKCacheKey * new_key)
{
#ifdef DEBUG
  if (new_key == NULL)
    return INK_ERROR;
#endif
  *new_key = (INKCacheKey) NEW(new CacheInfo());
  return INK_SUCCESS;
}

INKReturnCode
INKCacheKeyDigestSet(INKCacheKey key, const unsigned char *input, int length)
{
  if (sdk_sanity_check_cachekey(key) != INK_SUCCESS)
    return INK_ERROR;

  if (sdk_sanity_check_iocore_structure((void *) input) != INK_SUCCESS || length < 0)
    return INK_ERROR;

  ((CacheInfo *) key)->cache_key.encodeBuffer((char *) input, length);
  return INK_SUCCESS;
}

INKReturnCode
INKCacheKeyDigestFromUrlSet(INKCacheKey key, INKMLoc url)
{
  if (sdk_sanity_check_cachekey(key) != INK_SUCCESS)
    return INK_ERROR;

  url_MD5_get((URLImpl *) url, &((CacheInfo *) key)->cache_key);
  return INK_SUCCESS;
}

INKReturnCode
INKCacheKeyDataTypeSet(INKCacheKey key, INKCacheDataType type)
{
  if (sdk_sanity_check_cachekey(key) != INK_SUCCESS)
    return INK_ERROR;

  switch (type) {
  case INK_CACHE_DATA_TYPE_NONE:
    ((CacheInfo *) key)->frag_type = CACHE_FRAG_TYPE_NONE;
    break;
  case INK_CACHE_DATA_TYPE_OTHER:      /* other maps to http */
  case INK_CACHE_DATA_TYPE_HTTP:
    ((CacheInfo *) key)->frag_type = CACHE_FRAG_TYPE_HTTP;
    break;
  case INK_CACHE_DATA_TYPE_NNTP:
    ((CacheInfo *) key)->frag_type = CACHE_FRAG_TYPE_NNTP;
    break;
  case INK_CACHE_DATA_TYPE_FTP:
    ((CacheInfo *) key)->frag_type = CACHE_FRAG_TYPE_FTP;
    break;
  case INK_CACHE_DATA_TYPE_MIXT_RTSP:  /* rtsp, wmt, qtime map to rtsp */
  case INK_CACHE_DATA_TYPE_MIXT_WMT:
  case INK_CACHE_DATA_TYPE_MIXT_QTIME:
    ((CacheInfo *) key)->frag_type = CACHE_FRAG_TYPE_RTSP;
    break;
  default:
    return INK_ERROR;
  }
  return INK_SUCCESS;
}

INKReturnCode
INKCacheKeyHostNameSet(INKCacheKey key, const unsigned char *hostname, int host_len)
{
#ifdef DEBUG
  if ((hostname == NULL) || (host_len <= 0))
    return INK_ERROR;
#endif
  if (sdk_sanity_check_cachekey(key) != INK_SUCCESS)
    return INK_ERROR;

  CacheInfo *i = (CacheInfo *) key;
  /* need to make a copy of the hostname. The caller
     might deallocate it anytime in the future */
  i->hostname = (char *) xmalloc(host_len);
  memcpy(i->hostname, hostname, host_len);
  i->len = host_len;
  return INK_SUCCESS;
}

INKReturnCode
INKCacheKeyPinnedSet(INKCacheKey key, time_t pin_in_cache)
{
  if (sdk_sanity_check_cachekey(key) != INK_SUCCESS)
    return INK_ERROR;

  CacheInfo *i = (CacheInfo *) key;
  i->pin_in_cache = pin_in_cache;
  return INK_SUCCESS;
}

INKReturnCode
INKCacheKeyDestroy(INKCacheKey key)
{
  if (sdk_sanity_check_cachekey(key) != INK_SUCCESS)
    return INK_ERROR;

  CacheInfo *i = (CacheInfo *) key;
  if (i->hostname)
    xfree(i->hostname);
  i->magic = CACHE_INFO_MAGIC_DEAD;
  delete i;
  return INK_SUCCESS;
}

INKCacheHttpInfo
INKCacheHttpInfoCopy(INKCacheHttpInfo infop)
{
  CacheHTTPInfo *new_info = NEW(new CacheHTTPInfo);
  new_info->copy((CacheHTTPInfo *) infop);
  return new_info;
}

void
INKCacheHttpInfoReqGet(INKCacheHttpInfo infop, INKMBuffer * bufp, INKMLoc * obj)
{
  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  *bufp = info->request_get();
  *obj = info->request_get()->m_http;
  sdk_sanity_check_mbuffer(*bufp);
}


void
INKCacheHttpInfoRespGet(INKCacheHttpInfo infop, INKMBuffer * bufp, INKMLoc * obj)
{
  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  *bufp = info->response_get();
  *obj = info->response_get()->m_http;
  sdk_sanity_check_mbuffer(*bufp);
}

void
INKCacheHttpInfoReqSet(INKCacheHttpInfo infop, INKMBuffer bufp, INKMLoc obj)
{
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);

  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  info->request_set(&h);
}


void
INKCacheHttpInfoRespSet(INKCacheHttpInfo infop, INKMBuffer bufp, INKMLoc obj)
{
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);

  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  info->response_set(&h);
}


int
INKCacheHttpInfoVector(INKCacheHttpInfo infop, void *data, int length)
{
  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;

  CacheHTTPInfoVector vector;
  vector.insert(info);

  int size = vector.marshal_length();

  if (size > length) {
    // error
    return 0;
  }

  size = vector.marshal((char *) data, length);

  return size;
}


void
INKCacheHttpInfoDestroy(INKCacheHttpInfo * infop)
{

  ((CacheHTTPInfo *) infop)->destroy();
}

INKCacheHttpInfo
INKCacheHttpInfoCreate()
{

  CacheHTTPInfo *info = new CacheHTTPInfo;
  info->create();

  return info;
}


////////////////////////////////////////////////////////////////////
//
// Configuration
//
////////////////////////////////////////////////////////////////////

unsigned int
INKConfigSet(unsigned int id, void *data, INKConfigDestroyFunc funcp)
{
  INKConfigImpl *config = NEW(new INKConfigImpl);
  config->mdata = data;
  config->m_destroy_func = funcp;
  return configProcessor.set(id, config);
}

INKConfig
INKConfigGet(unsigned int id)
{
  return configProcessor.get(id);
}

void
INKConfigRelease(unsigned int id, INKConfig configp)
{
  configProcessor.release(id, (ConfigInfo *) configp);
}

void *
INKConfigDataGet(INKConfig configp)
{
  INKConfigImpl *config = (INKConfigImpl *) configp;
  return config->mdata;
}

////////////////////////////////////////////////////////////////////
//
// Management
//
////////////////////////////////////////////////////////////////////

INKReturnCode
INKMgmtUpdateRegister(INKCont contp, const char *plugin_name, const char *path)
{
  if (sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) plugin_name) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) path) != INK_SUCCESS)
    return INK_ERROR;

  global_config_cbs->insert((INKContInternal *) contp, plugin_name, path);
  return INK_SUCCESS;
}

int
INKMgmtIntGet(const char *var_name, INKMgmtInt * result)
{
  return RecGetRecordInt((char *) var_name, (RecInt *) result) == REC_ERR_OKAY ? 1 : 0;

}

int
INKMgmtCounterGet(const char *var_name, INKMgmtCounter * result)
{
  return RecGetRecordCounter((char *) var_name, (RecCounter *) result) == REC_ERR_OKAY ? 1 : 0;
}

int
INKMgmtFloatGet(const char *var_name, INKMgmtFloat * result)
{
  return RecGetRecordFloat((char *) var_name, (RecFloat *) result) == REC_ERR_OKAY ? 1 : 0;
}

int
INKMgmtStringGet(const char *var_name, INKMgmtString * result)
{
  RecString tmp = 0;
  (void) RecGetRecordString_Xmalloc((char *) var_name, &tmp);

  if (tmp) {
    *result = tmp;
    return 1;
  }
  return 0;
}

////////////////////////////////////////////////////////////////////
//
// Continuations
//
////////////////////////////////////////////////////////////////////

INKCont
INKContCreate(INKEventFunc funcp, INKMutex mutexp)
{
  // mutexp can be NULL
  if ((mutexp != NULL) && (sdk_sanity_check_mutex(mutexp) != INK_SUCCESS))
    return (INKCont) INK_ERROR_PTR;
  INKContInternal *i = INKContAllocator.alloc();

  i->init(funcp, mutexp);
  return (INKCont) i;
}

INKReturnCode
INKContDestroy(INKCont contp)
{
  if (sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS)
    return INK_ERROR;

  INKContInternal *i = (INKContInternal *) contp;
  i->destroy();
  return INK_SUCCESS;
}

INKReturnCode
INKContDataSet(INKCont contp, void *data)
{
  if (sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS)
    return INK_ERROR;

  INKContInternal *i = (INKContInternal *) contp;
  i->mdata = data;
  return INK_SUCCESS;
}

void *
INKContDataGet(INKCont contp)
{
  if (sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS)
    return (INKCont) INK_ERROR_PTR;

  INKContInternal *i = (INKContInternal *) contp;
  return i->mdata;
}

INKAction
INKContSchedule(INKCont contp, unsigned int timeout)
{
  if (sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS)
    return (INKAction) INK_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  INKContInternal *i = (INKContInternal *) contp;
  INKAction action;
  if (ink_atomic_increment((int *) &i->m_event_count, 1) < 0) {
    // simply return error_ptr
    //ink_assert (!"not reached");
    return (INKAction) INK_ERROR_PTR;
  }

  if (timeout == 0) {
    action = eventProcessor.schedule_imm(i, ET_NET);
  } else {
    action = eventProcessor.schedule_in(i, HRTIME_MSECONDS(timeout), ET_NET);
  }

/* This is a hack. SHould be handled in ink_types */
  action = (INKAction) ((paddr_t) action | 0x1);

  return action;
}

int
INKContCall(INKCont contp, INKEvent event, void *edata)
{
  Continuation *c = (Continuation *) contp;
  return c->handleEvent((int) event, edata);
}

INKMutex
INKContMutexGet(INKCont contp)
{
  if (sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS)
    return (INKCont) INK_ERROR_PTR;

  Continuation *c = (Continuation *) contp;
  return (INKMutex) ((ProxyMutex *) c->mutex);
}


/* HTTP hooks */

INKReturnCode
INKHttpHookAdd(INKHttpHookID id, INKCont contp)
{
  if (sdk_sanity_check_continuation(contp) == INK_SUCCESS && sdk_sanity_check_hook_id(id) == INK_SUCCESS) {
    http_global_hooks->append(id, (INKContInternal *) contp);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

/* Cache hooks */

INKReturnCode
INKCacheHookAdd(INKCacheHookID id, INKCont contp)
{
  if (sdk_sanity_check_continuation(contp) == INK_SUCCESS) {
    cache_global_hooks->append(id, (INKContInternal *) contp);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

void
INKHttpIcpDynamicSet(int value)
{
  ink32 old_value, new_value;

  new_value = (value == 0) ? 0 : 1;
  old_value = icp_dynamic_enabled;
  while (old_value != new_value) {
    if (ink_atomic_cas(&icp_dynamic_enabled, old_value, new_value))
      break;
    old_value = icp_dynamic_enabled;
  }
}


/* HTTP sessions */

INKReturnCode
INKHttpSsnHookAdd(INKHttpSsn ssnp, INKHttpHookID id, INKCont contp)
{
  if ((sdk_sanity_check_http_ssn(ssnp) == INK_SUCCESS) &&
      (sdk_sanity_check_continuation(contp) == INK_SUCCESS) && (sdk_sanity_check_hook_id(id) == INK_SUCCESS)) {
    HttpClientSession *cs = (HttpClientSession *) ssnp;
    cs->ssn_hook_append(id, (INKContInternal *) contp);
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}

class INKHttpSsnCallback:public Continuation
{
public:
  INKHttpSsnCallback(HttpClientSession * cs, INKEvent event)
  :Continuation(cs->mutex), m_cs(cs), m_event(event)
  {
    SET_HANDLER(&INKHttpSsnCallback::event_handler);
  }

  int event_handler(int, void *)
  {
    m_cs->handleEvent((int) m_event, 0);
    delete this;
    return 0;
  }

private:
  HttpClientSession * m_cs;
  INKEvent m_event;
};


INKReturnCode
INKHttpSsnReenable(INKHttpSsn ssnp, INKEvent event)
{
  if (sdk_sanity_check_http_ssn(ssnp) == INK_SUCCESS) {
    HttpClientSession *cs = (HttpClientSession *) ssnp;
    EThread *eth = this_ethread();
    // If this function is being executed on a thread created by the API
    // which is DEDICATED, the continuation needs to be called back on a
    // REGULAR thread.
    if (eth->tt != REGULAR) {
      eventProcessor.schedule_imm(NEW(new INKHttpSsnCallback(cs, event)), ET_NET);
    } else {
      MUTEX_TRY_LOCK(trylock, cs->mutex, eth);
      if (!trylock) {
        eventProcessor.schedule_imm(NEW(new INKHttpSsnCallback(cs, event)), ET_NET);
      } else {
        cs->handleEvent((int) event, 0);
      }
    }
    return INK_SUCCESS;
  } else {
    return INK_ERROR;
  }
}


/* HTTP transactions */

INKReturnCode
INKHttpTxnHookAdd(INKHttpTxn txnp, INKHttpHookID id, INKCont contp)
{
  if ((sdk_sanity_check_txn(txnp) != INK_SUCCESS) ||
      (sdk_sanity_check_continuation(contp) != INK_SUCCESS) || (sdk_sanity_check_hook_id(id) != INK_SUCCESS)) {
    return INK_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;
  sm->txn_hook_append(id, (INKContInternal *) contp);
  return INK_SUCCESS;
}


// Private api function for gzip plugin.
//  This function should only appear in InkAPIPrivate.h
int
INKHttpTxnHookRegisteredFor(INKHttpTxn txnp, INKHttpHookID id, INKEventFunc funcp)
{
  HttpSM *sm = (HttpSM *) txnp;
  APIHook *hook = sm->txn_hook_get(id);

  while (hook != NULL) {
    if (hook->m_cont && hook->m_cont->m_event_func == funcp) {
      return 1;
    }
    hook = hook->m_link.next;
  }

  return 0;
}


INKHttpSsn
INKHttpTxnSsnGet(INKHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return (INKHttpSsn) INK_ERROR_PTR;
  }
  HttpSM *sm = (HttpSM *) txnp;
  return (INKHttpSsn) sm->ua_session;
}

int
INKHttpTxnClientKeepaliveSet(INKHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State * s = &(sm->t_state);
  s->hdr_info.trust_response_cl = true;

  return 1;
}


int
INKHttpTxnClientReqGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * obj)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != INK_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  HTTPHdr *hptr = &(sm->t_state.hdr_info.client_request);

  if (hptr->valid()) {
    *bufp = hptr;
    *obj = hptr->m_http;
    sdk_sanity_check_mbuffer(*bufp);

    return 1;
  } else {
    return 0;
  }
}

int
INKHttpTxnClientRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * obj)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != INK_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  HTTPHdr *hptr = &(sm->t_state.hdr_info.client_response);

  if (hptr->valid()) {
    *bufp = hptr;
    *obj = hptr->m_http;
    sdk_sanity_check_mbuffer(*bufp);

    return 1;
  } else {
    return 0;
  }
}


int
INKHttpTxnServerReqGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * obj)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != INK_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  HTTPHdr *hptr = &(sm->t_state.hdr_info.server_request);

  if (hptr->valid()) {
    *bufp = hptr;
    *obj = hptr->m_http;
    sdk_sanity_check_mbuffer(*bufp);

    return 1;
  } else {
    return 0;
  }
}

int
INKHttpTxnServerRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * obj)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != INK_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  HTTPHdr *hptr = &(sm->t_state.hdr_info.server_response);

  if (hptr->valid()) {
    *bufp = hptr;
    *obj = hptr->m_http;
    sdk_sanity_check_mbuffer(*bufp);

    return 1;
  } else {
    return 0;
  }
}

int
INKHttpTxnCachedReqGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * obj)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != INK_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  HTTPInfo *cached_obj = sm->t_state.cache_info.object_read;

  // The following check is need to prevent the HttpSM handle copy from going bad
  // Since the cache manages the header buffer, sm->t_state.cache_info.object_read
  // is the only way to tell if handle has gone bad.
  if ((!cached_obj) || (!cached_obj->valid())) {
    return 0;
  }

  HTTPHdr *cached_hdr = sm->t_state.cache_info.object_read->request_get();
  if (!cached_hdr->valid()) {
    return 0;
  }
  // We can't use the HdrHeapSDKHandle structure in the RamCache since multiple
  // threads can access. We need to create our own for the transaction and return that.
  HdrHeapSDKHandle **handle = &(sm->t_state.cache_req_hdr_heap_handle);
  if (*handle == NULL) {
    *handle = (HdrHeapSDKHandle *) sm->t_state.arena.alloc(sizeof(HdrHeapSDKHandle));
    (*handle)->m_heap = cached_hdr->m_heap;
    (*handle)->m_sdk_alloc.init();
  }

  *bufp = *handle;
  *obj = cached_hdr->m_http;
  sdk_sanity_check_mbuffer(*bufp);

  return 1;
}

int
INKHttpTxnCachedRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * obj)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != INK_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  HTTPInfo *cached_obj = sm->t_state.cache_info.object_read;

  // The following check is need to prevent the HttpSM handle copy from going bad
  // Since the cache manages the header buffer, sm->t_state.cache_info.object_read
  // is the only way to tell if handle has gone bad.
  if ((!cached_obj) || (!cached_obj->valid())) {
    return 0;
  }

  HTTPHdr *cached_hdr = sm->t_state.cache_info.object_read->response_get();
  if (!cached_hdr->valid()) {
    return 0;
  }
  // We can't use the HdrHeapSDKHandle structure in the RamCache since multiple
  //  threads can access.  We need to create our own for the transaction and return that.
  HdrHeapSDKHandle **handle = &(sm->t_state.cache_resp_hdr_heap_handle);
  if (*handle == NULL) {
    *handle = (HdrHeapSDKHandle *) sm->t_state.arena.alloc(sizeof(HdrHeapSDKHandle));
    (*handle)->m_heap = cached_hdr->m_heap;
    (*handle)->m_sdk_alloc.init();
  }

  *bufp = *handle;
  *obj = cached_hdr->m_http;
  sdk_sanity_check_mbuffer(*bufp);

  return 1;
}


int
INKHttpTxnCachedRespModifiableGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * obj)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State * s = &(sm->t_state);
  HTTPHdr *c_resp = NULL;
  HTTPInfo *cached_obj = sm->t_state.cache_info.object_read;
  HTTPInfo *cached_obj_store = &(sm->t_state.cache_info.object_store);

  if ((!cached_obj) || (!cached_obj->valid()))
    return 0;

  if (!cached_obj_store->valid())
    cached_obj_store->create();

  c_resp = cached_obj_store->response_get();
  if (c_resp == NULL || !c_resp->valid())
    cached_obj_store->response_set(cached_obj->response_get());
  c_resp = cached_obj_store->response_get();
  s->api_modifiable_cached_resp = true;

  ink_assert(c_resp != NULL && c_resp->valid());
  *bufp = c_resp;
  *obj = c_resp->m_http;
  sdk_sanity_check_mbuffer(*bufp);

  return 1;
}

INKReturnCode
INKHttpTxnCacheLookupStatusGet(INKHttpTxn txnp, int *lookup_status)
{
  if ((sdk_sanity_check_txn(txnp) != INK_SUCCESS) || (lookup_status == NULL)) {
    return INK_ERROR;
  }

  HttpSM *sm = (HttpSM *) txnp;

  switch (sm->t_state.cache_lookup_result) {
  case HttpTransact::CACHE_LOOKUP_MISS:
  case HttpTransact::CACHE_LOOKUP_HIT_FTP_NON_ANONYMOUS:
  case HttpTransact::CACHE_LOOKUP_DOC_BUSY:
    *lookup_status = INK_CACHE_LOOKUP_MISS;
    break;
  case HttpTransact::CACHE_LOOKUP_HIT_STALE:
    *lookup_status = INK_CACHE_LOOKUP_HIT_STALE;
    break;
  case HttpTransact::CACHE_LOOKUP_HIT_WARNING:
  case HttpTransact::CACHE_LOOKUP_HIT_FRESH:
    *lookup_status = INK_CACHE_LOOKUP_HIT_FRESH;
    break;
  case HttpTransact::CACHE_LOOKUP_SKIPPED:
    *lookup_status = INK_CACHE_LOOKUP_SKIPPED;
    break;
  case HttpTransact::CACHE_LOOKUP_NONE:
  default:
    return INK_ERROR;
  };

  return INK_SUCCESS;
}

INKReturnCode
INKHttpTxnCacheLookupCountGet(INKHttpTxn txnp, int *lookup_count)
{
  if ((sdk_sanity_check_txn(txnp) != INK_SUCCESS) || (lookup_count == NULL)) {
    return INK_ERROR;
  }

  HttpSM *sm = (HttpSM *) txnp;
  *lookup_count = sm->t_state.cache_info.lookup_count;
  return INK_SUCCESS;
}


/* two hooks this function may gets called:
   INK_HTTP_READ_CACHE_HDR_HOOK   &
   INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK
 */
int
INKHttpTxnCacheLookupStatusSet(INKHttpTxn txnp, int cachelookup)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::CacheLookupResult_t * sm_status = &(sm->t_state.cache_lookup_result);

  // converting from a miss to a hit is not allowed
  if (*sm_status == HttpTransact::CACHE_LOOKUP_MISS && cachelookup != INK_CACHE_LOOKUP_MISS)
    return 0;

  // here is to handle converting a hit to a miss
  if (cachelookup == INK_CACHE_LOOKUP_MISS && *sm_status != HttpTransact::CACHE_LOOKUP_MISS) {
    sm->t_state.api_cleanup_cache_read = true;
    ink_assert(sm->t_state.transact_return_point != NULL);
    sm->t_state.transact_return_point = HttpTransact::HandleCacheOpenRead;
  }

  switch (cachelookup) {
  case INK_CACHE_LOOKUP_MISS:
    *sm_status = HttpTransact::CACHE_LOOKUP_MISS;
    break;
  case INK_CACHE_LOOKUP_HIT_STALE:
    *sm_status = HttpTransact::CACHE_LOOKUP_HIT_STALE;
    break;
  case INK_CACHE_LOOKUP_HIT_FRESH:
    *sm_status = HttpTransact::CACHE_LOOKUP_HIT_FRESH;
    break;
  default:
    return 0;
  }

  return 1;
}

int
INKHttpTxnCacheLookupUrlGet(INKHttpTxn txnp, INKMBuffer bufp, INKMLoc obj)
{
  HttpSM *sm = (HttpSM *) txnp;
  URL u, *l_url, *o_url;
  NOWARN_UNUSED(o_url);

  if (sm == NULL)
    return 0;

  sdk_sanity_check_mbuffer(bufp);
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;
  if (!u.valid())
    return 0;

  l_url = sm->t_state.cache_info.lookup_url;
  if (l_url && l_url->valid()) {
    u.copy(l_url);
    return 1;
  }

  return 0;
}

int
INKHttpTxnCachedUrlSet(INKHttpTxn txnp, INKMBuffer bufp, INKMLoc obj)
{
  HttpSM *sm = (HttpSM *) txnp;
  URL u, *s_url;

  sdk_sanity_check_mbuffer(bufp);
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;
  if (!u.valid())
    return 0;

  s_url = &(sm->t_state.cache_info.store_url);
  if (!s_url->valid())
    s_url->create(NULL);
  s_url->copy(&u);
  if (sm->decide_cached_url(&u))
    return 1;

  return 0;
}

int
INKHttpTxnNewCacheLookupDo(INKHttpTxn txnp, INKMBuffer bufp, INKMLoc url_loc)
{
  URL new_url, *client_url, *l_url, *o_url;
  INK_MD5 md51, md52;

  sdk_sanity_check_mbuffer(bufp);
  new_url.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  new_url.m_url_impl = (URLImpl *) url_loc;
  if (!new_url.valid())
    return 0;

  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State * s = &(sm->t_state);

  client_url = s->hdr_info.client_request.url_get();
  if (!(client_url->valid()))
    return 0;

  // if l_url is not valid, then no cache lookup has been done yet
  // so we shouldn't be calling INKHttpTxnNewCacheLookupDo right now
  l_url = s->cache_info.lookup_url;
  if (!l_url || !l_url->valid()) {
    s->cache_info.lookup_url_storage.create(NULL);
    s->cache_info.lookup_url = &(s->cache_info.lookup_url_storage);
    l_url = s->cache_info.lookup_url;
  } else {
    l_url->MD5_get(&md51);
    new_url.MD5_get(&md52);
    if (md51 == md52)
      return 0;
    o_url = &(s->cache_info.original_url);
    if (!o_url->valid()) {
      o_url->create(NULL);
      o_url->copy(l_url);
    }
  }

  // copy the new_url to both client_request and lookup_url
  client_url->copy(&new_url);
  l_url->copy(&new_url);

  // bypass HttpTransact::HandleFiltering
  s->transact_return_point = HttpTransact::DecideCacheLookup;
  s->cache_info.action = HttpTransact::CACHE_DO_LOOKUP;
  sm->add_cache_sm();
  s->api_cleanup_cache_read = true;
  return 1;
}

int
INKHttpTxnSecondUrlTryLock(INKHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State * s = &(sm->t_state);
  // INKHttpTxnNewCacheLookupDo didn't continue
  if (!s->cache_info.original_url.valid())
    return 0;
  sm->add_cache_sm();
  s->api_lock_url = HttpTransact::LOCK_URL_SECOND;
  return 1;
}

INKReturnCode
INKHttpTxnFollowRedirect(INKHttpTxn txnp, int on)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return INK_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;

  sm->api_enable_redirection = (on ? true : false);
  return INK_SUCCESS;
}

// YTS Team, yamsat Plugin
// This API is to create new request to the redirected url
// by setting API INKHttpTxnRedirectRequest
int
INKHttpTxnCreateRequest(INKHttpTxn txnp, const char *hostname, const char *path, int port)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State * s = &(sm->t_state);
  if (sm->enable_redirection) {
    INKMBuffer bufp_resp = NULL;
    INKMLoc hdrresp_loc = NULL;

    INKHttpTxnClientRespGet(txnp, &bufp_resp, &hdrresp_loc);
    //Checking for 302 and 301 response codes
    if (INK_HTTP_STATUS_MOVED_TEMPORARILY == (INKHttpHdrStatusGet(bufp_resp, hdrresp_loc)) ||
        INK_HTTP_STATUS_MOVED_PERMANENTLY == (INKHttpHdrStatusGet(bufp_resp, hdrresp_loc))) {

      INKMBuffer redirBuf;
      INKMLoc redirLoc;
      int redir_url_length;
      int re;

      redirBuf = INKMBufferCreate();
      redirLoc = INKUrlCreate(redirBuf);

      INKUrlSchemeSet(redirBuf, redirLoc, INK_URL_SCHEME_HTTPS, INK_URL_LEN_HTTPS);
      INKUrlHostSet(redirBuf, redirLoc, hostname, strlen(hostname));
      INKUrlPortSet(redirBuf, redirLoc, port);
      INKUrlPathSet(redirBuf, redirLoc, path, strlen(path));

      const char *url_redir_str = INKUrlStringGet(redirBuf, redirLoc, &redir_url_length);
      INKDebug("http", "Redirect URL in createReequest = \'%s\'\n", url_redir_str);

      re = INKUrlParse(redirBuf, redirLoc, &url_redir_str, (url_redir_str + strlen(url_redir_str)));
      if (re != INK_PARSE_DONE) {
        INKDebug("INKHttpTxnCreateRequest", "\n CreateRequest: INKParse failed ");
        INKMBufferDestroy(&redirBuf);
        return 0;
      }

      re = INKHttpTxnRedirectRequest(txnp, redirBuf, redirLoc);

      INKMBuffer req_bufp = NULL;
      INKMLoc req_loc = NULL;
      INKMLoc new_field_loc;
      std::string HOSTNAME;
      char *temp = NULL;
      size_t temp_size = 4 * sizeof(int);
      temp = (char *) xmalloc(temp_size);

      if (!INKHttpTxnClientReqGet(txnp, &req_bufp, &req_loc)) {
        INKError("Error");
      }

      HOSTNAME += hostname;
      HOSTNAME += ":";
      snprintf(temp, temp_size, "%d", port);
      HOSTNAME += temp;

      new_field_loc = INKMimeHdrFieldFind(req_bufp, req_loc, "Host", 4);
      INKMimeHdrFieldValueSet(req_bufp, req_loc, new_field_loc, -1, HOSTNAME.c_str(), strlen(HOSTNAME.c_str()));
      INKHandleMLocRelease(req_bufp, req_loc, new_field_loc);

      DUMP_HEADER("http_hdrs", &s->hdr_info.client_request, sm->sm_id, "Framed Client Request..checking");

      return 0;
    } else if (200 == (INKHttpHdrStatusGet(bufp_resp, hdrresp_loc))) {
      sm->enable_redirection = false;
    }
    return 0;
  }
  return 0;
}


int
INKHttpTxnRedirectRequest(INKHttpTxn txnp, INKMBuffer bufp, INKMLoc url_loc)
{
  URL u, *o_url, *r_url, *client_url;
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State * s = &(sm->t_state);

  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) url_loc;
  if (!u.valid())
    return 0;

  client_url = s->hdr_info.client_request.url_get();
  if (!(client_url->valid()))
    return 0;

  s->redirect_info.redirect_in_process = true;
  o_url = &(s->redirect_info.original_url);
  if (!o_url->valid()) {
    o_url->create(NULL);
    o_url->copy(client_url);
  }
  client_url->copy(&u);

  r_url = &(s->redirect_info.redirect_url);
  if (!r_url->valid())
    r_url->create(NULL);
  r_url->copy(&u);




  s->hdr_info.server_request.destroy();
  // we want to close the server session
  s->api_release_server_session = true;

  s->request_sent_time = 0;
  s->response_received_time = 0;
  s->cache_info.write_lock_state = HttpTransact::CACHE_WL_INIT;
  s->next_action = HttpTransact::REDIRECT_READ;
  return 1;
}

/**
 * timeout is in msec
 * overrides as proxy.config.http.transaction_active_timeout_out
**/
int
INKHttpTxnActiveTimeoutSet(INKHttpTxn txnp, int timeout)
{
  Debug("http_timeout", "setting active timeout to %d msec via API", timeout);
  HttpTransact::State * s = &(((HttpSM *) txnp)->t_state);
  s->api_txn_active_timeout = true;
  s->api_txn_active_timeout_value = timeout;
  return 1;
}

/**
 * timeout is in msec
 * overrides as proxy.config.http.connect_attempts_timeout
**/
int
INKHttpTxnConnectTimeoutSet(INKHttpTxn txnp, int timeout)
{
  Debug("http_timeout", "setting inactive timeout to %d msec via API", timeout);
  HttpTransact::State * s = &(((HttpSM *) txnp)->t_state);
  s->api_txn_connect_timeout = true;
  s->api_txn_connect_timeout_value = timeout;
  return 1;
}

/**
 * timeout is in msec
 * overrides as proxy.config.dns.lookup_timeout
**/
int
INKHttpTxnDNSTimeoutSet(INKHttpTxn txnp, int timeout)
{
  Debug("http_timeout", "setting DNS timeout to %d msec via API", timeout);
  HttpTransact::State * s = &(((HttpSM *) txnp)->t_state);
  s->api_txn_dns_timeout = true;
  s->api_txn_dns_timeout_value = timeout;
  return 1;
}


/**
 * timeout is in msec
 * overrides as proxy.config.http.transaction_no_activity_timeout_out
**/
int
INKHttpTxnNoActivityTimeoutSet(INKHttpTxn txnp, int timeout)
{
  Debug("http_timeout", "setting DNS timeout to %d msec via API", timeout);
  HttpTransact::State * s = &(((HttpSM *) txnp)->t_state);
  s->api_txn_no_activity_timeout = true;
  s->api_txn_no_activity_timeout_value = timeout;
  return 1;
}

int
INKHttpTxnCacheLookupSkip(INKHttpTxn txnp)
{
  HttpTransact::State * s = &(((HttpSM *) txnp)->t_state);
  s->api_skip_cache_lookup = true;
  return 1;
}

int
INKHttpTxnServerRespNoStore(INKHttpTxn txnp)
{
  HttpTransact::State * s = &(((HttpSM *) txnp)->t_state);
  s->api_server_response_no_store = true;
  return 1;
}

int
INKHttpTxnServerRespIgnore(INKHttpTxn txnp)
{
  HttpTransact::State * s = &(((HttpSM *) txnp)->t_state);
  HTTPInfo *cached_obj = s->cache_info.object_read;
  HTTPHdr *cached_resp;

  if (cached_obj == NULL || !cached_obj->valid())
    return 0;

  cached_resp = cached_obj->response_get();
  if (cached_resp == NULL || !cached_resp->valid())
    return 0;

  s->api_server_response_ignore = true;
  return 1;
}

int
INKHttpTxnShutDown(INKHttpTxn txnp, INKEvent event)
{
  if (event == INK_EVENT_HTTP_TXN_CLOSE)
    return 0;

  HttpTransact::State * s = &(((HttpSM *) txnp)->t_state);
  s->api_http_sm_shutdown = true;
  return 1;
}

int
INKHttpTxnAborted(INKHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  switch (sm->t_state.squid_codes.log_code) {
  case SQUID_LOG_ERR_CLIENT_ABORT:
  case SQUID_LOG_TCP_SWAPFAIL:
    // check for client abort and cache read error
    return 1;
  default:
    break;
  }

  if (sm->t_state.current.server && sm->t_state.current.server->abort == HttpTransact::ABORTED) {
    // check for the server abort
    return 1;
  }
  // there can be the case of transformation error. DI is not doing it now,
  // so skip it for the time being. In order to do this, we probably need
  // another state variable?
  return 0;
}

int
INKHttpTxnClientReqIsServerStyle(INKHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  return (sm->t_state.hdr_info.client_req_is_server_style);
}

int
INKHttpTxnOverwriteExpireTime(INKHttpTxn txnp, time_t expire_time)
{
  HttpTransact::State * s = &(((HttpSM *) txnp)->t_state);
  s->plugin_set_expire_time = expire_time;
  return 1;
}

int
INKHttpTxnUpdateCachedObject(INKHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State * s = &(sm->t_state);
  HTTPInfo *cached_obj_store = &(sm->t_state.cache_info.object_store);
  HTTPHdr *client_request = &(sm->t_state.hdr_info.client_request);

  if (!cached_obj_store->valid() || !cached_obj_store->response_get())
    return 0;

  if (!cached_obj_store->request_get() && !client_request->valid())
    return 0;

  if (s->cache_info.write_lock_state == HttpTransact::CACHE_WL_READ_RETRY)
    return 0;

  s->api_update_cached_object = HttpTransact::UPDATE_CACHED_OBJECT_PREPARE;
  return 1;
}

int
INKHttpTxnTransformRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * obj)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  HTTPHdr *hptr = &(sm->t_state.hdr_info.transform_response);

  if (hptr->valid()) {
    *bufp = hptr;
    *obj = hptr->m_http;
    sdk_sanity_check_mbuffer(*bufp);

    return 1;
  } else {
    return 0;
  }
}

unsigned int
INKHttpTxnClientIPGet(INKHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  return sm->t_state.client_info.ip;
}

int
INKHttpTxnClientIncomingPortGet(INKHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return -1;
  }
  HttpSM *sm = (HttpSM *) txnp;
  /* Don't need this check as the check has been moved to sdk_sanity_check_txn
     if (sm == NULL)
     return -1;
   */
  return sm->t_state.client_info.port;
}

unsigned int
INKHttpTxnServerIPGet(INKHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  return sm->t_state.server_info.ip;
}

unsigned int
INKHttpTxnNextHopIPGet(INKHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
    /**
     * Return zero if the server structure is not yet constructed.
     */
  if (                          /* Don't need this check as its already done in sdk_sanity_check_txn
                                   (sm==NULL) ||
                                 */
       (sm->t_state.current.server == NULL))
    return 0;
    /**
     * Else return the value
     */
  return sm->t_state.current.server->ip;
}

int
INKHttpTxnNextHopPortGet(INKHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  int port = 0;

  if (sm && sm->t_state.current.server) {
    port = sm->t_state.current.server->port;
  }

  return port;
}



INKReturnCode
INKHttpTxnErrorBodySet(INKHttpTxn txnp, char *buf, int buflength, char *mimetype)
{
  if ((sdk_sanity_check_txn(txnp) != INK_SUCCESS) || (buf == NULL)
    ) {
    return INK_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;

  sm->t_state.internal_msg_buffer = buf;
  sm->t_state.internal_msg_buffer_type = mimetype;
  sm->t_state.internal_msg_buffer_size = buflength;
  sm->t_state.internal_msg_buffer_fast_allocator_size = -1;
  return INK_SUCCESS;
}

void
INKHttpTxnServerRequestBodySet(INKHttpTxn txnp, char *buf, int buflength)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State * s = &(sm->t_state);

  if (buf == NULL || buflength <= 0 || s->method != HTTP_WKSIDX_GET)
    return;

  if (s->internal_msg_buffer)
    HttpTransact::free_internal_msg_buffer(s->internal_msg_buffer, s->internal_msg_buffer_fast_allocator_size);

  s->api_server_request_body_set = true;
  s->internal_msg_buffer = buf;
  s->internal_msg_buffer_size = buflength;
  s->internal_msg_buffer_fast_allocator_size = -1;
}

INKReturnCode
INKHttpTxnParentProxyGet(INKHttpTxn txnp, char **hostname, int *port)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return INK_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;

  *hostname = sm->t_state.api_info.parent_proxy_name;
  *port = sm->t_state.api_info.parent_proxy_port;
  return INK_SUCCESS;
}

INKReturnCode
INKHttpTxnParentProxySet(INKHttpTxn txnp, char *hostname, int port)
{
  if ((sdk_sanity_check_txn(txnp) != INK_SUCCESS) || (hostname == NULL) || (port <= 0)
    ) {
    return INK_ERROR;
  }

  HttpSM *sm = (HttpSM *) txnp;

  sm->t_state.api_info.parent_proxy_name = sm->t_state.arena.str_store(hostname, strlen(hostname));
  sm->t_state.api_info.parent_proxy_port = port;
  return INK_SUCCESS;
}

INKReturnCode
INKHttpTxnUntransformedRespCache(INKHttpTxn txnp, int on)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return INK_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;

  sm->t_state.api_info.cache_untransformed = (on ? true : false);
  return INK_SUCCESS;
}

INKReturnCode
INKHttpTxnTransformedRespCache(INKHttpTxn txnp, int on)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return INK_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;

  sm->t_state.api_info.cache_transformed = (on ? true : false);
  return INK_SUCCESS;
}


class INKHttpSMCallback:public Continuation
{
public:
  INKHttpSMCallback(HttpSM * sm, INKEvent event)
  :Continuation(sm->mutex), m_sm(sm), m_event(event)
  {
    SET_HANDLER(&INKHttpSMCallback::event_handler);
  }

  int event_handler(int, void *)
  {
    m_sm->state_api_callback((int) m_event, 0);
    delete this;
    return 0;
  }

private:
  HttpSM * m_sm;
  INKEvent m_event;
};


//----------------------------------------------------------------------------
INKIOBufferReader
INKCacheBufferReaderGet(INKCacheTxn txnp)
{
  NewCacheVC *vc = (NewCacheVC *) txnp;

  return vc->getBufferReader();
}


//----------------------------------------------------------------------------
//INKReturnCode
//INKCacheBufferInfoGet(INKHttpTxn txnp, void **buffer, INKU64 *length, INKU64 *offset)
INKReturnCode
INKCacheBufferInfoGet(INKCacheTxn txnp, INKU64 * length, INKU64 * offset)
{
  NewCacheVC *vc = (NewCacheVC *) txnp;

  vc->getCacheBufferInfo(length, offset);

  return INK_SUCCESS;
}



//----------------------------------------------------------------------------
INKReturnCode
INKHttpCacheReenable(INKCacheTxn txnp, const INKEvent event, const void *data, const INKU64 size)
{
  Debug("cache_plugin", "[INKHttpCacheReenable] event id: %d data: %lX size: %llu", event, (unsigned long) data, size);
  //bool calledUser = 0;
  NewCacheVC *vc = (NewCacheVC *) txnp;
  if (vc->isClosed()) {
    return INK_SUCCESS;
  }
  //vc->setCtrlInPlugin(false);
  switch (event) {

  case INK_EVENT_CACHE_READ_READY:
  case INK_EVENT_CACHE_READ_COMPLETE:  // read
    Debug("cache_plugin", "[INKHttpCacheReenable] cache_read");
    if (data != 0) {

      // set CacheHTTPInfo in the VC
      //vc->setCacheHttpInfo(data, size);

      //vc->getTunnel()->append_message_to_producer_buffer(vc->getTunnel()->get_producer(vc),(const char*)data,size);
      //           HTTPInfo *cacheInfo;
//            vc->get_http_info(&cacheInfo);
      //unsigned int doc_size = cacheInfo->object_size_get();
      bool retVal = vc->setRangeAndSize(size);
      //INKMutexLock(vc->getTunnel()->mutex);
      vc->getTunnel()->get_producer(vc)->read_buffer->write((const char *) data, size);
      if (retVal) {
        vc->getTunnel()->get_producer(vc)->read_buffer->write("\r\n", 2);
        vc->add_boundary(true);
      }
      Debug("cache_plugin", "[INKHttpCacheReenable] cache_read ntodo %d", vc->getVio()->ntodo());
      if (vc->getVio()->ntodo() > 0)
        vc->getTunnel()->handleEvent(VC_EVENT_READ_READY, vc->getVio());
      else
        vc->getTunnel()->handleEvent(VC_EVENT_READ_COMPLETE, vc->getVio());
      //INKMutexUnlock(vc->getTunnel()->mutex);
    } else {
      // not in cache
      //INKMutexLock(vc->getTunnel()->mutex);
      vc->getTunnel()->handleEvent(VC_EVENT_ERROR, vc->getVio());
      //INKMutexUnlock(vc->getTunnel()->mutex);
    }

    break;
  case INK_EVENT_CACHE_LOOKUP_COMPLETE:
    Debug("cache_plugin", "[INKHttpCacheReenable] cache_lookup_complete");
    if (data == 0 || !vc->completeCacheHttpInfo(data, size)) {
      Debug("cache_plugin", "[INKHttpCacheReenable] open read failed");
      //INKMutexLock(vc->getCacheSm()->mutex);
      vc->getCacheSm()->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -ECACHE_NO_DOC);
      //INKMutexUnlock(vc->getCacheSm()->mutex);
      break;
    }
    Debug("cache_plugin", "[INKHttpCacheReenable] we have data");
    //INKMutexLock(vc->getCacheSm()->mutex);
    vc->getCacheSm()->handleEvent(CACHE_EVENT_OPEN_READ, (void *) vc);
    //INKMutexUnlock(vc->getCacheSm()->mutex);
    break;
  case INK_EVENT_CACHE_LOOKUP_READY:
    Debug("cache_plugin", "[INKHttpCacheReenable] cache_lookup_ready");
    if (data == 0 || !vc->appendCacheHttpInfo(data, size)) {
      Debug("cache_plugin", "[INKHttpCacheReenable] open read failed");
      // not in cache, free the vc
      //INKMutexLock(vc->getCacheSm()->mutex);
      vc->getCacheSm()->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -ECACHE_NO_DOC);
      //INKMutexUnlock(vc->getCacheSm()->mutex);
    }
    break;
  case INK_EVENT_CACHE_WRITE:  // write
  case INK_EVENT_CACHE_WRITE_HEADER:
    {
      Debug("cache_plugin", "[INKHttpCacheReenable] cache_write");
      if (vc->getState() == NewCacheVC::NEW_CACHE_WRITE_HEADER && vc->getVio()->ntodo() <= 0) {
        //vc->getCacheSm()->handleEvent(VC_EVENT_WRITE_COMPLETE, vc->getVio());
        Debug("cache_plugin", "[INKHttpCacheReenable] NewCacheVC::NEW_CACHE_WRITE_HEADER");
        // writing header
        // do nothing
      } else {
        vc->setTotalObjectSize(size);
        vc->getVio()->ndone = size;
        if (vc->getVio()->ntodo() <= 0) {
          //INKMutexLock(vc->getCacheSm()->mutex);
          vc->getTunnel()->handleEvent(VC_EVENT_WRITE_COMPLETE, vc->getVio());
          //INKMutexUnlock(vc->getCacheSm()->mutex);
        } else {
          //INKMutexLock(vc->getCacheSm()->mutex);
          vc->getTunnel()->handleEvent(VC_EVENT_WRITE_READY, vc->getVio());
          //INKMutexUnlock(vc->getCacheSm()->mutex);
        }
      }
    }
    break;

  case INK_EVENT_CACHE_DELETE:
    break;

    // handle read_ready, read_complete, write_ready, write_complete
    // read_failure, write_failure
  case INK_EVENT_CACHE_CLOSE:
    //do nothing
    break;

  default:
    break;
  }

  return INK_SUCCESS;
}


//----------------------------------------------------------------------------
INKReturnCode
INKHttpTxnReenable(INKHttpTxn txnp, INKEvent event)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return INK_ERROR;
  }

  HttpSM *sm = (HttpSM *) txnp;
  EThread *eth = this_ethread();
  // If this function is being executed on a thread created by the API
  // which is DEDICATED, the continuation needs to be called back on a
  // REGULAR thread.
  if (eth->tt != REGULAR) {
    eventProcessor.schedule_imm(NEW(new INKHttpSMCallback(sm, event)), ET_NET);
  } else {
    MUTEX_TRY_LOCK(trylock, sm->mutex, eth);
    if (!trylock) {
      eventProcessor.schedule_imm(NEW(new INKHttpSMCallback(sm, event)), ET_NET);
    } else {
      sm->state_api_callback((int) event, 0);
    }
  }
  return INK_SUCCESS;
}

int
INKHttpTxnGetMaxArgCnt(void)
{
  return HTTP_TRANSACT_STATE_MAX_USER_ARG;
}

INKReturnCode
INKHttpTxnSetArg(INKHttpTxn txnp, int arg_idx, void *arg)
{
  if (sdk_sanity_check_txn(txnp) == INK_SUCCESS && arg_idx >= 0 && arg_idx < HTTP_TRANSACT_STATE_MAX_USER_ARG) {
    HttpSM *sm = (HttpSM *) txnp;
    sm->t_state.user_args[arg_idx] = arg;
    return INK_SUCCESS;
  }
  return INK_ERROR;
}

INKReturnCode
INKHttpTxnGetArg(INKHttpTxn txnp, int arg_idx, void **argp)
{
  if (sdk_sanity_check_txn(txnp) == INK_SUCCESS && arg_idx >= 0 && arg_idx < HTTP_TRANSACT_STATE_MAX_USER_ARG && argp) {
    HttpSM *sm = (HttpSM *) txnp;
    *argp = sm->t_state.user_args[arg_idx];
    return INK_SUCCESS;
  }
  return INK_ERROR;
}

INKReturnCode
INKHttpTxnSetHttpRetStatus(INKHttpTxn txnp, INKHttpStatus http_retstatus)
{
  if (sdk_sanity_check_txn(txnp) == INK_SUCCESS) {
    HttpSM *sm = (HttpSM *) txnp;
    sm->t_state.http_return_code = (HTTPStatus) http_retstatus;
    return INK_SUCCESS;
  }
  return INK_ERROR;
}

int
INKHttpTxnGetMaxHttpRetBodySize(void)
{
  return HTTP_TRANSACT_STATE_MAX_XBUF_SIZE;
}

INKReturnCode
INKHttpTxnSetHttpRetBody(INKHttpTxn txnp, const char *body_msg, bool plain_msg_flag)
{
  if (sdk_sanity_check_txn(txnp) == INK_SUCCESS) {
    HttpSM *sm = (HttpSM *) txnp;
    HttpTransact::State * s = &(sm->t_state);
    s->return_xbuf_size = 0;
    s->return_xbuf[0] = 0;
    s->return_xbuf_plain = false;
    if (body_msg) {
      strncpy(s->return_xbuf, body_msg, HTTP_TRANSACT_STATE_MAX_XBUF_SIZE - 1);
      s->return_xbuf[HTTP_TRANSACT_STATE_MAX_XBUF_SIZE - 1] = 0;
      s->return_xbuf_size = strlen(s->return_xbuf);
      s->return_xbuf_plain = plain_msg_flag;
    }
    return INK_SUCCESS;
  }
  return INK_ERROR;
}

/* for Media-IXT mms over http */
int
INKHttpTxnCntl(INKHttpTxn txnp, INKHttpCntlType cntl, void *data)
{
  HttpSM *sm = (HttpSM *) txnp;

  switch (cntl) {
  case INK_HTTP_CNTL_GET_LOGGING_MODE:
    {

      if (data == NULL) {
        return 0;
      }

      intptr_t *rptr = (intptr_t *) data;

      if (sm->t_state.api_info.logging_enabled) {
        *rptr = (intptr_t) INK_HTTP_CNTL_ON;
      } else {
        *rptr = (intptr_t) INK_HTTP_CNTL_OFF;
      }

      return 1;
    }

  case INK_HTTP_CNTL_SET_LOGGING_MODE:
    if (data != INK_HTTP_CNTL_ON && data != INK_HTTP_CNTL_OFF) {
      return 0;
    } else {
      sm->t_state.api_info.logging_enabled = (bool) data;
      return 1;
    }
    break;

  case INK_HTTP_CNTL_GET_INTERCEPT_RETRY_MODE:
    {
      if (data == NULL) {
        return 0;
      }

      intptr_t *rptr = (intptr_t *) data;

      if (sm->t_state.api_info.retry_intercept_failures) {
        *rptr = (intptr_t) INK_HTTP_CNTL_ON;
      } else {
        *rptr = (intptr_t) INK_HTTP_CNTL_OFF;
      }

      return 1;
    }

  case INK_HTTP_CNTL_SET_INTERCEPT_RETRY_MODE:
    if (data != INK_HTTP_CNTL_ON && data != INK_HTTP_CNTL_OFF) {
      return 0;
    } else {
      sm->t_state.api_info.retry_intercept_failures = (bool) data;
      return 1;
    }
  default:
    return 0;
  }

  return 0;
}

/* This is kinda horky, we have to use INKServerState instead of
   HttpTransact::ServerState_t, otherwise we have a prototype
   mismatch in the public InkAPI.h interfaces. */
INKServerState
INKHttpTxnServerStateGet(INKHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS)
    return INK_SRVSTATE_STATE_UNDEFINED;

  HttpTransact::State * s = &(((HttpSM *) txnp)->t_state);
  return (INKServerState) s->current.state;
}

/* to access all the stats */
int
INKHttpTxnClientReqHdrBytesGet(INKHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->client_request_hdr_bytes;
  return 1;
}

int
INKHttpTxnClientReqBodyBytesGet(INKHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->client_request_body_bytes;
  return 1;
}

int
INKHttpTxnServerReqHdrBytesGet(INKHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->server_request_hdr_bytes;
  return 1;
}

int
INKHttpTxnServerReqBodyBytesGet(INKHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->server_request_body_bytes;
  return 1;
}

int
INKHttpTxnServerRespHdrBytesGet(INKHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->server_response_hdr_bytes;
  return 1;
}

int
INKHttpTxnServerRespBodyBytesGet(INKHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->server_response_body_bytes;
  return 1;
}

int
INKHttpTxnClientRespHdrBytesGet(INKHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->client_response_hdr_bytes;
  return 1;
}

int
INKHttpTxnClientRespBodyBytesGet(INKHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->client_response_body_bytes;
  return 1;
}

int
INKHttpTxnPushedRespHdrBytesGet(INKHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->pushed_response_hdr_bytes;
  return 1;
}

int
INKHttpTxnPushedRespBodyBytesGet(INKHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->pushed_response_body_bytes;
  return 1;
}

int
INKHttpTxnStartTimeGet(INKHttpTxn txnp, INK64 * start_time)
{
  HttpSM *sm = (HttpSM *) txnp;
  if (sm->milestones.ua_begin == 0)
    return 0;
  else {
    *start_time = (ink64) (sm->milestones.ua_begin);
    return 1;
  }
}

int
INKHttpTxnEndTimeGet(INKHttpTxn txnp, INK64 * end_time)
{
  HttpSM *sm = (HttpSM *) txnp;
  if (sm->milestones.ua_close == 0)
    return 0;
  else {
    *end_time = (ink64) (sm->milestones.ua_close);
    return 1;
  }
}

int
INKHttpTxnStartTimeGetD(INKHttpTxn txnp, double *start_time)
{
  HttpSM *sm = (HttpSM *) txnp;
  if (sm->milestones.ua_begin == 0)
    return 0;
  else {
    //*start_time = (long long)(ink_hrtime_to_msec(sm->milestones.ua_begin));
    *start_time = (double) (sm->milestones.ua_begin);
    return 1;
  }
}

int
INKHttpTxnEndTimeGetD(INKHttpTxn txnp, double *end_time)
{
  HttpSM *sm = (HttpSM *) txnp;
  if (sm->milestones.ua_close == 0)
    return 0;
  else {
    //*end_time = (long long)(ink_hrtime_to_msec(sm->milestones.ua_close));
    *end_time = (double) (sm->milestones.ua_close);
    return 1;
  }
}

int
INKHttpTxnCachedRespTimeGet(INKHttpTxn txnp, long *resp_time)
{
  HttpSM *sm = (HttpSM *) txnp;
  HTTPInfo *cached_obj = sm->t_state.cache_info.object_read;

  if (cached_obj == NULL || !cached_obj->valid())
    return 0;

  *resp_time = (long) (cached_obj->response_received_time_get());
  return 1;
}

int
INKHttpTxnLookingUpTypeGet(INKHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State * s = &(sm->t_state);

  return (int) (s->current.request_to);
}

int
INKHttpCurrentClientConnectionsGet(int *num_connections)
{
  ink64 S;

  HTTP_READ_DYN_SUM(http_current_client_connections_stat, S);
  *num_connections = (int) S;
  return 1;
}

int
INKHttpCurrentActiveClientConnectionsGet(int *num_connections)
{
  ink64 S;

  HTTP_READ_DYN_SUM(http_current_active_client_connections_stat, S);
  *num_connections = (int) S;
  return 1;
}

int
INKHttpCurrentIdleClientConnectionsGet(int *num_connections)
{
  ink64 total = 0;
  ink64 active = 0;
  HTTP_READ_DYN_SUM(http_current_client_connections_stat, total);
  HTTP_READ_DYN_SUM(http_current_active_client_connections_stat, active);

  if (total >= active) {
    *num_connections = total - active;
    return 1;
  }

  return 0;
}

int
INKHttpCurrentCacheConnectionsGet(int *num_connections)
{
  ink64 S;
  HTTP_READ_DYN_SUM(http_current_cache_connections_stat, S);
  *num_connections = (int) S;
  return 1;
}

int
INKHttpCurrentServerConnectionsGet(int *num_connections)
{
  ink64 S;

  HTTP_READ_DYN_SUM(http_current_server_connections_stat, S);
  *num_connections = (int) S;
  return 1;
}


/* HTTP alternate selection */

INKReturnCode
INKHttpAltInfoClientReqGet(INKHttpAltInfo infop, INKMBuffer * bufp, INKMLoc * obj)
{
  HttpAltInfo *info = (HttpAltInfo *) infop;

  if (sdk_sanity_check_alt_info(infop) != INK_SUCCESS) {
    return INK_ERROR;
  }
  *bufp = &info->m_client_req;
  *obj = info->m_client_req.m_http;
  if (sdk_sanity_check_mbuffer(*bufp) != INK_SUCCESS) {
    return INK_ERROR;
  }
  return INK_SUCCESS;
}

INKReturnCode
INKHttpAltInfoCachedReqGet(INKHttpAltInfo infop, INKMBuffer * bufp, INKMLoc * obj)
{
  HttpAltInfo *info = (HttpAltInfo *) infop;
  if (sdk_sanity_check_alt_info(infop) != INK_SUCCESS) {
    return INK_ERROR;
  }
  *bufp = &info->m_cached_req;
  *obj = info->m_cached_req.m_http;
  if (sdk_sanity_check_mbuffer(*bufp) != INK_SUCCESS) {
    return INK_ERROR;
  }
  return INK_SUCCESS;
}

INKReturnCode
INKHttpAltInfoCachedRespGet(INKHttpAltInfo infop, INKMBuffer * bufp, INKMLoc * obj)
{
  HttpAltInfo *info = (HttpAltInfo *) infop;
  if (sdk_sanity_check_alt_info(infop) != INK_SUCCESS) {
    return INK_ERROR;
  }
  *bufp = &info->m_cached_resp;
  *obj = info->m_cached_resp.m_http;
  if (sdk_sanity_check_mbuffer(*bufp) != INK_SUCCESS) {
    return INK_ERROR;
  }
  return INK_SUCCESS;
}

INKReturnCode
INKHttpAltInfoQualitySet(INKHttpAltInfo infop, float quality)
{
  HttpAltInfo *info = (HttpAltInfo *) infop;
  if (sdk_sanity_check_alt_info(infop) != INK_SUCCESS) {
    return INK_ERROR;
  }
  info->m_qvalue = quality;
  return INK_SUCCESS;
}

extern HttpAccept *plugin_http_accept;

INKReturnCode
INKHttpConnect(unsigned int log_ip, int log_port, INKVConn * vc)
{
#ifdef DEBUG
  if (vc == NULL) {
    return INK_ERROR;
  }
  if ((log_ip == 0) || (log_port <= 0)) {
    *vc = NULL;
    return INK_ERROR;
  }
#endif
  if (plugin_http_accept) {
    PluginVCCore *new_pvc = PluginVCCore::alloc();
    new_pvc->set_active_addr(log_ip, log_port);
    new_pvc->set_accept_cont(plugin_http_accept);
    PluginVC *return_vc = new_pvc->connect();
    *vc = (INKVConn) return_vc;
    return ((return_vc) ? INK_SUCCESS : INK_ERROR);
  } else {
    *vc = NULL;
    return INK_ERROR;
  }
}

/* Actions */

// Currently no error handling necessary, actionp can be anything.
INKReturnCode
INKActionCancel(INKAction actionp)
{
  Action *a;
  INKContInternal *i;

/* This is a hack. SHould be handled in ink_types */
  if ((paddr_t) actionp & 0x1) {
    a = (Action *) ((paddr_t) actionp - 1);
    i = (INKContInternal *) a->continuation;
    i->handle_event_count(EVENT_IMMEDIATE);
  } else {
    a = (Action *) actionp;
  }

  a->cancel();
  return INK_SUCCESS;
}

// Currently no error handling necessary, actionp can be anything.
int
INKActionDone(INKAction actionp)
{
  Action *a = (Action *) actionp;
  return (a == ACTION_RESULT_DONE);
}

/* Connections */

/* Deprectated.
   Do not use this API.
   The reason is even if VConn is created using this API, it is still useless.
   For example, if we do INKVConnRead, the read operation returns read_vio, if
   we do INKVIOReenable (read_vio), it actually calls:
   void VIO::reenable()
   {
       if (vc_server) vc_server->reenable(this);
   }
   vc_server->reenable calls:
   VConnection::reenable(VIO)

   this function is virtual in VConnection.h. It is defined separately for
   UnixNet, NTNet and CacheVConnection.

   Thus, unless VConn is either NetVConnection or CacheVConnection, it can't
   be instantiated for functions like reenable.

   Meanwhile, this function has never been used.
   */
INKVConn
INKVConnCreate(INKEventFunc event_funcp, INKMutex mutexp)
{
  if (mutexp == NULL) {
    mutexp = (INKMutex) new_ProxyMutex();
  }

  if (sdk_sanity_check_mutex(mutexp) != INK_SUCCESS)
    return (INKVConn) INK_ERROR_PTR;

  INKVConnInternal *i = INKVConnAllocator.alloc();
#ifdef DEBUG
  if (i == NULL)
    return (INKVConn) INK_ERROR_PTR;
#endif
  i->init(event_funcp, mutexp);
  return (INKCont) i;
}


INKVIO
INKVConnReadVIOGet(INKVConn connp)
{
  if (sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS)
    return (INKCont) INK_ERROR_PTR;

  VConnection *vc = (VConnection *) connp;
  INKVIO data;

  if (!vc->get_data(INK_API_DATA_READ_VIO, &data)) {
    // don't assert, simple return error_ptr
    // ink_assert (!"not reached");
    return (INKVIO) INK_ERROR_PTR;
  }
  return data;
}

INKVIO
INKVConnWriteVIOGet(INKVConn connp)
{
  if (sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS)
    return (INKCont) INK_ERROR_PTR;

  VConnection *vc = (VConnection *) connp;
  INKVIO data;

  if (!vc->get_data(INK_API_DATA_WRITE_VIO, &data)) {
    // don't assert, simple return error_ptr
    // ink_assert (!"not reached");
    return (INKVIO) INK_ERROR_PTR;
  }
  return data;
}

int
INKVConnClosedGet(INKVConn connp)
{
  if (sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS)
    return INK_ERROR;

  VConnection *vc = (VConnection *) connp;
  int data;

  if (!vc->get_data(INK_API_DATA_CLOSED, &data)) {
    // don't assert, simple return error_ptr
    // ink_assert (!"not reached");
    return INK_ERROR;
  }
  return data;
}

INKVIO
INKVConnRead(INKVConn connp, INKCont contp, INKIOBuffer bufp, int nbytes)
{
  if ((sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(bufp) != INK_SUCCESS) || (nbytes < 0))
    return (INKCont) INK_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  VConnection *vc = (VConnection *) connp;
  return vc->do_io(VIO::READ, (INKContInternal *) contp, nbytes, (MIOBuffer *) bufp);
}

INKVIO
INKVConnWrite(INKVConn connp, INKCont contp, INKIOBufferReader readerp, int nbytes)
{
  if ((sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(readerp) != INK_SUCCESS) || (nbytes < 0))
    return (INKCont) INK_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  VConnection *vc = (VConnection *) connp;
  return vc->do_io_write((INKContInternal *) contp, nbytes, (IOBufferReader *) readerp);
}

INKReturnCode
INKVConnClose(INKVConn connp)
{
  if (sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS)
    return INK_ERROR;

  VConnection *vc = (VConnection *) connp;
  vc->do_io_close();
  return INK_SUCCESS;
}

INKReturnCode
INKVConnAbort(INKVConn connp, int error)
{
  if (sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS)
    return INK_ERROR;

  VConnection *vc = (VConnection *) connp;
  vc->do_io_close(error);
  return INK_SUCCESS;
}

INKReturnCode
INKVConnShutdown(INKVConn connp, int read, int write)
{
  if (sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS)
    return INK_ERROR;

  VConnection *vc = (VConnection *) connp;

  if (read && write) {
    vc->do_io_shutdown(IO_SHUTDOWN_READWRITE);
  } else if (read) {
    vc->do_io_shutdown(IO_SHUTDOWN_READ);
  } else if (write) {
    vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
  }
  return INK_SUCCESS;
}

INKReturnCode
INKVConnCacheObjectSizeGet(INKVConn connp, int *obj_size)
{
  if ((sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS) || (obj_size == NULL))
    return INK_ERROR;

  CacheVC *vc = (CacheVC *) connp;
  *obj_size = vc->get_object_size();
  return INK_SUCCESS;
}

void
INKVConnCacheHttpInfoSet(INKVConn connp, INKCacheHttpInfo infop)
{
  CacheVC *vc = (CacheVC *) connp;
  if (vc->base_stat == cache_scan_active_stat)
    vc->set_http_info((CacheHTTPInfo *) infop);
}

/* Transformations */

INKVConn
INKTransformCreate(INKEventFunc event_funcp, INKHttpTxn txnp)
{
  return INKVConnCreate(event_funcp, INKContMutexGet(txnp));
}

INKVConn
INKTransformOutputVConnGet(INKVConn connp)
{
  if (sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS) {
    return (INKVConn) INK_ERROR_PTR;
  }
  VConnection *vc = (VConnection *) connp;
  INKVConn data;

  if (!vc->get_data(INK_API_DATA_OUTPUT_VC, &data)) {
    ink_assert(!"not reached");
  }
  return data;
}

INKReturnCode
INKHttpTxnServerIntercept(INKCont contp, INKHttpTxn txnp)
{
  if ((sdk_sanity_check_txn(txnp) != INK_SUCCESS) || (sdk_sanity_check_continuation(contp) != INK_SUCCESS)) {
    return INK_ERROR;
  }
  HttpSM *http_sm = (HttpSM *) txnp;
  INKContInternal *i = (INKContInternal *) contp;
#ifdef DEBUG
  if (i->mutex == NULL) {
    return INK_ERROR;
  }
#endif
  http_sm->plugin_tunnel_type = HTTP_PLUGIN_AS_SERVER;
  http_sm->plugin_tunnel = PluginVCCore::alloc();
  http_sm->plugin_tunnel->set_accept_cont(i);

  return INK_SUCCESS;
}

INKReturnCode
INKHttpTxnIntercept(INKCont contp, INKHttpTxn txnp)
{
  if ((sdk_sanity_check_txn(txnp) != INK_SUCCESS) || (sdk_sanity_check_continuation(contp) != INK_SUCCESS)) {
    return INK_ERROR;
  }
  HttpSM *http_sm = (HttpSM *) txnp;
  INKContInternal *i = (INKContInternal *) contp;
#ifdef DEBUG
  if (i->mutex == NULL) {
    return INK_ERROR;
  }
#endif
  http_sm->plugin_tunnel_type = HTTP_PLUGIN_AS_INTERCEPT;
  http_sm->plugin_tunnel = PluginVCCore::alloc();
  http_sm->plugin_tunnel->set_accept_cont(i);

  return INK_SUCCESS;
}

/* Net VConnections */
void
INKVConnInactivityTimeoutSet(INKVConn connp, int timeout)
{
  NetVConnection *vc = (NetVConnection *) connp;
  vc->set_inactivity_timeout(timeout);
}

void
INKVConnInactivityTimeoutCancel(INKVConn connp)
{
  NetVConnection *vc = (NetVConnection *) connp;
  vc->cancel_inactivity_timeout();
}

INKReturnCode
INKNetVConnRemoteIPGet(INKVConn connp, unsigned int *ip)
{
  if ((sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS) || (ip == NULL)) {
    return INK_ERROR;
  }

  NetVConnection *vc = (NetVConnection *) connp;
  *ip = vc->get_remote_ip();
  return INK_SUCCESS;
}

INKReturnCode
INKNetVConnRemotePortGet(INKVConn connp, int *port)
{
  if ((sdk_sanity_check_iocore_structure(connp) != INK_SUCCESS) || (port == NULL)) {
    return INK_ERROR;
  }

  NetVConnection *vc = (NetVConnection *) connp;
  *port = vc->get_remote_port();
  return INK_SUCCESS;
}

INKAction
INKNetConnect(INKCont contp, unsigned int ip, int port)
{
  if ((sdk_sanity_check_continuation(contp) != INK_SUCCESS) || (ip == 0) || (port == 0))
    return (INKAction) INK_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  INKContInternal *i = (INKContInternal *) contp;
  return (INKAction) netProcessor.connect_re(i, ip, port);
}

INKAction
INKNetAccept(INKCont contp, int port)
{
  if ((sdk_sanity_check_continuation(contp) != INK_SUCCESS) || (port == 0))
    return (INKAction) INK_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  INKContInternal *i = (INKContInternal *) contp;
  return (INKAction) netProcessor.accept(i, port);
}

/* DNS Lookups */
INKAction
INKHostLookup(INKCont contp, char *hostname, int namelen)
{
  if ((sdk_sanity_check_continuation(contp) != INK_SUCCESS) || (hostname == NULL) || (namelen == 0))
    return (INKAction) INK_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  INKContInternal *i = (INKContInternal *) contp;
  return (INKAction) hostDBProcessor.getbyname_re(i, hostname, namelen);
}

INKReturnCode
INKHostLookupResultIPGet(INKHostLookupResult lookup_result, unsigned int *ip)
{
  if ((sdk_sanity_check_hostlookup_structure(lookup_result) != INK_SUCCESS) || (ip == NULL)) {
    return INK_ERROR;
  }

  *ip = ((HostDBInfo *) lookup_result)->ip();
  return INK_SUCCESS;
}

/*
 * checks if the cache is ready
 */

/* Only INKCacheReady exposed in SDK. No need of INKCacheDataTypeReady */
/* because SDK cache API supports only the data type: NONE */
INKReturnCode
INKCacheReady(int *is_ready)
{
  return INKCacheDataTypeReady(INK_CACHE_DATA_TYPE_NONE, is_ready);
}

/* Private API (used by Mixt) */
INKReturnCode
INKCacheDataTypeReady(INKCacheDataType type, int *is_ready)
{
#ifdef DEBUG
  if (is_ready == NULL) {
    return INK_ERROR;
  }
#endif
  CacheFragType frag_type;

  switch (type) {
  case INK_CACHE_DATA_TYPE_NONE:
    frag_type = CACHE_FRAG_TYPE_NONE;
    break;
  case INK_CACHE_DATA_TYPE_OTHER:      /* other maps to http */
  case INK_CACHE_DATA_TYPE_HTTP:
    frag_type = CACHE_FRAG_TYPE_HTTP;
    break;
  case INK_CACHE_DATA_TYPE_NNTP:
    frag_type = CACHE_FRAG_TYPE_NNTP;
    break;
  case INK_CACHE_DATA_TYPE_FTP:
    frag_type = CACHE_FRAG_TYPE_FTP;
    break;
  case INK_CACHE_DATA_TYPE_MIXT_RTSP:  /* rtsp, wmt, qtime map to rtsp */
  case INK_CACHE_DATA_TYPE_MIXT_WMT:
  case INK_CACHE_DATA_TYPE_MIXT_QTIME:
    frag_type = CACHE_FRAG_TYPE_RTSP;
    break;
  default:
    *is_ready = 0;
    return INK_ERROR;
  }

  *is_ready = cacheProcessor.IsCacheReady(frag_type);
  return INK_SUCCESS;
}

/* Cache VConnections */
INKAction
INKCacheRead(INKCont contp, INKCacheKey key)
{
  if ((sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS) || (sdk_sanity_check_cachekey(key) != INK_SUCCESS))
    return (INKAction) INK_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  CacheInfo *info = (CacheInfo *) key;
  Continuation *i = (INKContInternal *) contp;

  return (INKAction)
    cacheProcessor.open_read(i, &info->cache_key, info->frag_type, info->hostname, info->len);
}

INKAction
INKCacheWrite(INKCont contp, INKCacheKey key)
{
  if ((sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS) || (sdk_sanity_check_cachekey(key) != INK_SUCCESS))
    return (INKAction) INK_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  CacheInfo *info = (CacheInfo *) key;
  Continuation *i = (INKContInternal *) contp;
  return (INKAction)
    cacheProcessor.open_write(i, 0, &info->cache_key,
                              info->frag_type, false, info->pin_in_cache, info->hostname, info->len);
}

INKAction
INKCacheRemove(INKCont contp, INKCacheKey key)
{
  if ((sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS) || (sdk_sanity_check_cachekey(key) != INK_SUCCESS))
    return (INKAction) INK_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  CacheInfo *info = (CacheInfo *) key;
  INKContInternal *i = (INKContInternal *) contp;
  return (INKAction)
    cacheProcessor.remove(i, &info->cache_key, true, false, info->frag_type, info->hostname, info->len);
}

INKAction
INKCacheScan(INKCont contp, INKCacheKey key, int KB_per_second)
{
  if ((sdk_sanity_check_iocore_structure(contp) != INK_SUCCESS) || (sdk_sanity_check_cachekey(key) != INK_SUCCESS))
    return (INKAction) INK_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  INKContInternal *i = (INKContInternal *) contp;
  if (key) {
    CacheInfo *info = (CacheInfo *) key;
    return (INKAction)
      cacheProcessor.scan(i, info->hostname, info->len, KB_per_second);
  }
  return cacheProcessor.scan(i, 0, 0, KB_per_second);
}



/* IP/User Name Cache */

/*int
INKUserNameCacheInsert(INKCont contp, unsigned long ip, const char *userName)
{
  FORCE_PLUGIN_MUTEX(contp);
  int ret;
  ret = ipToUserNameCacheProcessor.insertCacheEntry((Continuation *) contp, ip, userName);

  if (ret == EVENT_DONE)
    return INK_EVENT_IMMEDIATE;
  else if (ret == EVENT_ERROR)
    return INK_EVENT_ERROR;
  else
    return INK_EVENT_CONTINUE;

}


int
INKUserNameCacheLookup(INKCont contp, unsigned long ip, char *userName)
{
  FORCE_PLUGIN_MUTEX(contp);
  int ret;
  ret = ipToUserNameCacheProcessor.lookupCacheEntry((Continuation *) contp, ip, userName, 3);

  if (ret == EVENT_DONE)
    return INK_EVENT_IMMEDIATE;
  else if (ret == EVENT_ERROR)
    return INK_EVENT_ERROR;
  else
    return INK_EVENT_CONTINUE;

}

int
INKUserNameCacheDelete(INKCont contp, unsigned long ip)
{
  FORCE_PLUGIN_MUTEX(contp);
  int ret;
  ret = ipToUserNameCacheProcessor.deleteCacheEntry((Continuation *) contp, ip);

  if (ret == EVENT_DONE)
    return INK_EVENT_IMMEDIATE;
  else if (ret == EVENT_ERROR)
    return INK_EVENT_ERROR;
  else
    return INK_EVENT_CONTINUE;

}*/


//inkcoreapi INKMCOPreload_fp MCOPreload_fp = NULL;

/* Callouts for APIs implemented by shared objects */
/*
int
INKMCOPreload(void *context,    // opaque ptr
              INKCont continuation,     // called w/ progress updates
              const char *const url,    // content to retrieve
              int bytesPerSecond,       // bandwidth throttle
              int callbackPeriod        // interval in seconds
  )
{
  if (MCOPreload_fp) {
    return (*MCOPreload_fp) (context, continuation, url, bytesPerSecond, callbackPeriod);
  } else {
    return -1;
  }
}MCOPreload_fp
*/

/**************************    Stats API    ****************************/
// #define ink_sanity_check_stat_structure(_x) INK_SUCCESS

inline INKReturnCode
ink_sanity_check_stat_structure(void *obj)
{
  if (obj == NULL || obj == INK_ERROR_PTR) {
    return INK_ERROR;
  }

  return INK_SUCCESS;
}

INKStat
INKStatCreate(const char *the_name, INKStatTypes the_type)
{
#ifdef DEBUG
  if (the_name == NULL ||
      the_name == INK_ERROR_PTR || ((the_type != INKSTAT_TYPE_INT64) && (the_type != INKSTAT_TYPE_FLOAT))) {
    return (INKStat) INK_ERROR_PTR;
  }
#endif

  StatDescriptor *n = NULL;

  switch (the_type) {
  case INKSTAT_TYPE_INT64:
    n = StatDescriptor::CreateDescriptor(the_name, (ink64) 0);
    break;

  case INKSTAT_TYPE_FLOAT:
    n = StatDescriptor::CreateDescriptor(the_name, (float) 0);
    break;

  default:
    Warning("INKStatCreate given invalid type enumeration!");
    break;
  };

  return n == NULL ? (INKStat) INK_ERROR_PTR : (INKStat) n;
}

INKReturnCode
INKStatIntAddTo(INKStat the_stat, INK64 amount)
{
  if (ink_sanity_check_stat_structure(the_stat) != INK_SUCCESS)
    return INK_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->add(amount);
  return INK_SUCCESS;
}

INKReturnCode
INKStatFloatAddTo(INKStat the_stat, float amount)
{
  if (ink_sanity_check_stat_structure(the_stat) != INK_SUCCESS)
    return INK_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->add(amount);
  return INK_SUCCESS;
}

INKReturnCode
INKStatDecrement(INKStat the_stat)
{
  if (ink_sanity_check_stat_structure(the_stat) != INK_SUCCESS)
    return INK_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->decrement();
  return INK_SUCCESS;
}

INKReturnCode
INKStatIncrement(INKStat the_stat)
{
  if (ink_sanity_check_stat_structure(the_stat) != INK_SUCCESS)
    return INK_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->increment();
  return INK_SUCCESS;
}


INKReturnCode
INKStatIntGet(INKStat the_stat, INK64 * value)
{
  if (ink_sanity_check_stat_structure(the_stat) != INK_SUCCESS)
    return INK_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  *value = statp->int_value();
  return INK_SUCCESS;
}

INKReturnCode
INKStatFloatGet(INKStat the_stat, float *value)
{
  if (ink_sanity_check_stat_structure(the_stat) != INK_SUCCESS)
    return INK_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  *value = statp->flt_value();
  return INK_SUCCESS;
}

//deprecated in SDK3.0
INK64
INKStatIntRead(INKStat the_stat)
{
  if (ink_sanity_check_stat_structure(the_stat) != INK_SUCCESS)
    return 0;

  INK64 stat_val;
  StatDescriptor *statp = (StatDescriptor *) the_stat;
  stat_val = statp->int_value();
  return stat_val;
}

//deprecated in SDK3.0
float
INKStatFloatRead(INKStat the_stat)
{
  if (ink_sanity_check_stat_structure(the_stat) != INK_SUCCESS)
    return 0.0;

  float stat_val;
  StatDescriptor *statp = (StatDescriptor *) the_stat;
  stat_val = statp->flt_value();
  return stat_val;
}

INKReturnCode
INKStatIntSet(INKStat the_stat, INK64 value)
{
  if (ink_sanity_check_stat_structure(the_stat) != INK_SUCCESS)
    return INK_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->set(value);
  return INK_SUCCESS;
}

INKReturnCode
INKStatFloatSet(INKStat the_stat, float value)
{
  if (ink_sanity_check_stat_structure(the_stat) != INK_SUCCESS)
    return INK_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->set(value);
  return INK_SUCCESS;
}

INKCoupledStat
INKStatCoupledGlobalCategoryCreate(const char *the_name)
{
#ifdef DEBUG
  if (the_name == NULL || the_name == INK_ERROR_PTR)
    return (INKCoupledStat) INK_ERROR_PTR;
#endif

  CoupledStats *category = NEW(new CoupledStats(the_name));
  return (INKCoupledStat) category;
}

INKCoupledStat
INKStatCoupledLocalCopyCreate(const char *the_name, INKCoupledStat global_copy)
{
  if (ink_sanity_check_stat_structure(global_copy) != INK_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) the_name) != INK_SUCCESS)
    return (INKCoupledStat) INK_ERROR_PTR;

  CoupledStatsSnapshot *snap = NEW(new CoupledStatsSnapshot((CoupledStats *) global_copy));

  return (INKCoupledStat) snap;
}

INKReturnCode
INKStatCoupledLocalCopyDestroy(INKCoupledStat stat)
{
  if (ink_sanity_check_stat_structure(stat) != INK_SUCCESS)
    return INK_ERROR;

  CoupledStatsSnapshot *snap = (CoupledStatsSnapshot *) stat;
  if (snap) {
    delete snap;
  }

  return INK_SUCCESS;
}

INKStat
INKStatCoupledGlobalAdd(INKCoupledStat global_copy, const char *the_name, INKStatTypes the_type)
{
  if ((ink_sanity_check_stat_structure(global_copy) != INK_SUCCESS) ||
      sdk_sanity_check_null_ptr((void *) the_name) != INK_SUCCESS ||
      ((the_type != INKSTAT_TYPE_INT64) && (the_type != INKSTAT_TYPE_FLOAT))) {
    return (INKStat) INK_ERROR_PTR;
  }


  CoupledStats *category = (CoupledStats *) global_copy;
  StatDescriptor *n;

  switch (the_type) {
  case INKSTAT_TYPE_INT64:
    n = category->CreateStat(the_name, (ink64) 0);
    break;

  case INKSTAT_TYPE_FLOAT:
    n = category->CreateStat(the_name, (float) 0);
    break;

  default:
    n = NULL;
    Warning("INKStatCreate given invalid type enumeration!");
    break;
  };

  return n == NULL ? (INKStat) INK_ERROR_PTR : (INKStat) n;
}

INKStat
INKStatCoupledLocalAdd(INKCoupledStat local_copy, const char *the_name, INKStatTypes the_type)
{
  if ((ink_sanity_check_stat_structure(local_copy) != INK_SUCCESS) ||
      sdk_sanity_check_null_ptr((void *) the_name) != INK_SUCCESS ||
      ((the_type != INKSTAT_TYPE_INT64) && (the_type != INKSTAT_TYPE_FLOAT))) {
    return (INKStat) INK_ERROR_PTR;
  }

  StatDescriptor *n = ((CoupledStatsSnapshot *) local_copy)->fetchNext();
  return n == NULL ? (INKStat) INK_ERROR_PTR : (INKStat) n;
}

INKReturnCode
INKStatsCoupledUpdate(INKCoupledStat local_copy)
{
  if (ink_sanity_check_stat_structure(local_copy) != INK_SUCCESS)
    return INK_ERROR;

  ((CoupledStatsSnapshot *) local_copy)->CommitUpdates();
  return INK_SUCCESS;
}

/**************************   Tracing API   ****************************/
// returns 1 or 0 to indicate whether TS is being run with a debug tag.
int
INKIsDebugTagSet(const char *t)
{
  return (diags->on(t, DiagsTagType_Debug)) ? 1 : 0;
}

// Plugins would use INKDebug just as the TS internal uses Debug
// e.g. INKDebug("plugin-cool", "Snoopy is a cool guy even after %d requests.\n", num_reqs);
void
INKDebug(const char *tag, const char *format_str, ...)
{
  if (diags->on(tag, DiagsTagType_Debug)) {
    va_list ap;
    va_start(ap, format_str);
    diags->print_va(tag, DL_Diag, NULL, NULL, format_str, ap);
    va_end(ap);
  }
}

/**************************   Logging API   ****************************/

INKReturnCode
INKTextLogObjectCreate(const char *filename, int mode, INKTextLogObject * new_object)
{
#ifdef DEBUG
  if (filename == NULL) {
    *new_object = NULL;
    return INK_ERROR;
  }
#endif
  if (mode<0 || mode>= INK_LOG_MODE_INVALID_FLAG) {
    /* specified mode is invalid */
    *new_object = NULL;
    return INK_ERROR;
  }
  TextLogObject *tlog = NEW(new TextLogObject(filename, Log::config->logfile_dir,
                                              (bool) mode & INK_LOG_MODE_ADD_TIMESTAMP,
                                              NULL,
                                              Log::config->rolling_enabled,
                                              Log::config->rolling_interval_sec,
                                              Log::config->rolling_offset_hr,
                                              Log::config->rolling_size_mb));
  if (tlog) {
    int err = (mode & INK_LOG_MODE_DO_NOT_RENAME ?
               Log::config->log_object_manager.manage_api_object(tlog, 0) :
               Log::config->log_object_manager.manage_api_object(tlog));
    if (err != LogObjectManager::NO_FILENAME_CONFLICTS) {
      // error managing log
      delete tlog;
      *new_object = NULL;
      return INK_ERROR;
    }
  } else {
    // error creating log
    *new_object = NULL;
    return INK_ERROR;
  }
  *new_object = (INKTextLogObject) tlog;
  return INK_SUCCESS;
}

INKReturnCode
INKTextLogObjectWrite(INKTextLogObject the_object, char *format, ...)
{
  if ((sdk_sanity_check_iocore_structure(the_object) != INK_SUCCESS)
      || (format == NULL))
    return INK_ERROR;

  INKReturnCode retVal = INK_SUCCESS;

  va_list ap;
  va_start(ap, format);
  switch (((TextLogObject *) the_object)->va_write(format, ap)) {
  case (Log::LOG_OK):
  case (Log::SKIP):
    break;
  case (Log::FULL):
    retVal = INK_ERROR;
    break;
  case (Log::FAIL):
    retVal = INK_ERROR;
    break;
  default:
    ink_debug_assert(!"invalid return code");
  }
  va_end(ap);
  return retVal;
}

INKReturnCode
INKTextLogObjectFlush(INKTextLogObject the_object)
{
  if (sdk_sanity_check_iocore_structure(the_object) != INK_SUCCESS)
    return INK_ERROR;

  ((TextLogObject *) the_object)->force_new_buffer();
  return INK_SUCCESS;
}

INKReturnCode
INKTextLogObjectDestroy(INKTextLogObject the_object)
{
  if (sdk_sanity_check_iocore_structure(the_object) != INK_SUCCESS)
    return INK_ERROR;

  return (Log::config->log_object_manager.unmanage_api_object((TextLogObject *) the_object) ? INK_SUCCESS : INK_ERROR);
}

inkapi INKReturnCode
INKTextLogObjectHeaderSet(INKTextLogObject the_object, const char *header)
{
  if (sdk_sanity_check_iocore_structure(the_object) != INK_SUCCESS)
    return INK_ERROR;

  ((TextLogObject *) the_object)->set_log_file_header(header);
  return INK_SUCCESS;
}

inkapi INKReturnCode
INKTextLogObjectRollingEnabledSet(INKTextLogObject the_object, int rolling_enabled)
{
  if (sdk_sanity_check_iocore_structure(the_object) != INK_SUCCESS)
    return INK_ERROR;

  ((TextLogObject *) the_object)->set_rolling_enabled(rolling_enabled);
  return INK_SUCCESS;
}

inkapi INKReturnCode
INKTextLogObjectRollingIntervalSecSet(INKTextLogObject the_object, int rolling_interval_sec)
{
  if (sdk_sanity_check_iocore_structure(the_object) != INK_SUCCESS)
    return INK_ERROR;

  ((TextLogObject *) the_object)->set_rolling_interval_sec(rolling_interval_sec);
  return INK_SUCCESS;
}

inkapi INKReturnCode
INKTextLogObjectRollingOffsetHrSet(INKTextLogObject the_object, int rolling_offset_hr)
{
  if (sdk_sanity_check_iocore_structure(the_object) != INK_SUCCESS)
    return INK_ERROR;

  ((TextLogObject *) the_object)->set_rolling_offset_hr(rolling_offset_hr);
  return INK_SUCCESS;
}

int
INKHttpTxnClientFdGet(INKHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return INK_ERROR;
  }
  INKHttpSsn ssnp = INKHttpTxnSsnGet(txnp);
  HttpClientSession *cs = (HttpClientSession *) ssnp;
  if (cs == NULL) {
    return -1;
  }
  NetVConnection *vc = cs->get_netvc();
  if (vc == NULL) {
    return -1;
  }
  return vc->get_socket();
}

INKReturnCode
INKHttpTxnClientRemotePortGet(INKHttpTxn txnp, int *port)
{
  if (sdk_sanity_check_txn(txnp) != INK_SUCCESS) {
    return INK_ERROR;
  }
  INKHttpSsn ssnp = INKHttpTxnSsnGet(txnp);
  HttpClientSession *cs = (HttpClientSession *) ssnp;
  if (cs == NULL) {
    return INK_ERROR;
  }
  NetVConnection *vc = cs->get_netvc();
  if (vc == NULL) {
    return INK_ERROR;
  }
  // Note: SDK spec specifies this API should return port in network byte order
  // iocore returns it in host byte order. So we do the conversion.
  *port = htons(vc->get_remote_port());
  return INK_SUCCESS;
}

/*******************  DI Footprint API (private)  **********************/

/* IP Lookup */
#if 0                           // Not used.
INKIPLookup
INKIPLookupCreate()
{
  return (INKIPLookup) (NEW(new IpLookup("INKIPLookup")));
}
#endif

// This is very suspicious, INKILookup is a (void *), so how on earth
// can we try to delete an instance of it?
#if 0                           // Not used.
void
INKIPLookupDestroy(INKIPLookup iplu)
{
  if (iplu) {
    delete iplu;
  }
}
#endif

#if 0                           // Not used.
INKIPLookupState
INKIPLookupStateCreate()
{
  return (INKIPLookupState) xmalloc(sizeof(IpLookupState));
}
#endif

#if 0                           // Not used.
void
INKIPLookupStateDestroy(INKIPLookupState iplus)
{
  if (iplus) {
    xfree(iplus);
  }
}
#endif

void
INKIPLookupNewEntry(INKIPLookup iplu, INKU32 addr1, INKU32 addr2, void *data)
{
  IpLookup *my_iplu = (IpLookup *) iplu;
  if (my_iplu) {
    my_iplu->NewEntry((ip_addr_t) addr1, (ip_addr_t) addr2, data);
  }
}

int
INKIPLookupMatch(INKIPLookup iplu, INKU32 addr, void **data)
{
  void *dummy;
  IpLookup *my_iplu = (IpLookup *) iplu;
  if (!data) {
    data = &dummy;
  }
  return (my_iplu ? my_iplu->Match((ip_addr_t) addr, data) : 0);
}

int
INKIPLookupMatchFirst(INKIPLookup iplu, INKU32 addr, INKIPLookupState iplus, void **data)
{
  IpLookup *my_iplu = (IpLookup *) iplu;
  IpLookupState *my_iplus = (IpLookupState *) iplus;
  if (my_iplu && my_iplus && my_iplu->MatchFirst(addr, my_iplus, data)) {
    return 1;
  }
  return 0;
}

int
INKIPLookupMatchNext(INKIPLookup iplu, INKIPLookupState iplus, void **data)
{
  IpLookup *my_iplu = (IpLookup *) iplu;
  IpLookupState *my_iplus = (IpLookupState *) iplus;
  if (my_iplu && my_iplus && my_iplu->MatchNext(my_iplus, data)) {
    return 1;
  }
  return 0;
}

void
INKIPLookupPrint(INKIPLookup iplu, INKIPLookupPrintFunc pf)
{
  IpLookup *my_iplu = (IpLookup *) iplu;
  if (my_iplu) {
    my_iplu->Print((IpLookupPrintFunc) pf);
  }
}

/* Matcher Utils */
char *
INKMatcherReadIntoBuffer(char *file_name, int *file_len)
{
  return readIntoBuffer((char *) file_name, "INKMatcher", file_len);
}

char *
INKMatcherTokLine(char *buffer, char **last)
{
  return tokLine(buffer, last);
}

char *
INKMatcherExtractIPRange(char *match_str, INKU32 * addr1, INKU32 * addr2)
{
  return ExtractIpRange(match_str, (ip_addr_t *) addr1, (ip_addr_t *) addr2);
}

INKMatcherLine
INKMatcherLineCreate()
{
  return (void *) xmalloc(sizeof(matcher_line));
}

void
INKMatcherLineDestroy(INKMatcherLine ml)
{
  if (ml) {
    xfree(ml);
  }
}

char *
INKMatcherParseSrcIPConfigLine(char *line, INKMatcherLine ml)
{
  return parseConfigLine(line, (matcher_line *) ml, &ip_allow_tags);
}

char *
INKMatcherLineName(INKMatcherLine ml, int element)
{
  return (((matcher_line *) ml)->line)[0][element];
}

char *
INKMatcherLineValue(INKMatcherLine ml, int element)
{
  return (((matcher_line *) ml)->line)[1][element];
}

/* Configuration Setting */
int
INKMgmtConfigIntSet(const char *var_name, INKMgmtInt value)
{
  INKMgmtInt result;
  char *buffer;

  // is this a valid integer?
  if (!INKMgmtIntGet(var_name, &result)) {
    return 0;
  }
  // construct a buffer
  int buffer_size = strlen(var_name) + 1 + 32 + 1 + 64 + 1;
  buffer = (char *) alloca(buffer_size);
  ink_snprintf(buffer, buffer_size, "%s %d %lld", var_name, INK_INT, value);

  // tell manager to set the configuration; note that this is not
  // transactional (e.g. we return control to the plugin before the
  // value is commited to disk by the manager)
  RecSignalManager(MGMT_SIGNAL_PLUGIN_SET_CONFIG, buffer);
  return 1;
}



/**
 * AAA API
 */

/**
 * IMPORTANT: Change this description.
 * Function will return the User Policy in Flist Format. Flist format is used
 * by Portal's billing software to report any information. The user's policy
 * information can then be queried from this flist using Portal's PIN Macro
 *
 * Input Parameters:
 *             txnp          :INKHttpTxn  :The transaction under consideration.
 *             user_info     :pin_flist_t :Null Pointer. Will contain return
 *                                         value
 *
 * Output Parameters:
 *             user_info     :void        :Pointer to user's account
 *                                         information and services that user
 *                                         has subscribed to.
 *
 *
 * Return Value:
 *            INK_SUCCESS                 : Success
 *            INK_ERROR                 : Failure
 */


INKReturnCode
INKUserPolicyLookup(INKHttpTxn txnp, void **user_info)
{

  unsigned int ip;
  struct USER_INFO *user_struct = NULL;

  ip = INKHttpTxnClientIPGet(txnp);
  user_struct = UserCacheLookup(ip, NULL);

  if (user_struct == NULL) {
    return INK_ERROR;
  }

  switch (user_struct->status) {
  case POLICY_FETCHED:
    if (*user_info == NULL) {
      (*user_info) = INKstrndup((char *) (user_struct->policy), user_struct->len);
    } else {
      strncpy((char *) (*user_info), (char *) (user_struct->policy), strlen((char *) (*user_info)));
    }
    break;
  case POLICY_FETCHING:
  case LOGGED_OFF:
  case REASSIGNED:
    INKfree(user_struct);
    return INK_ERROR;
    break;
  default:
    INKfree(user_struct);
    return INK_ERROR;
    break;
  }

  INKfree(user_struct);
  return INK_SUCCESS;
}



/**
 * Function will change the value of header '\@Bill' in Client Request Header
 * depending on value of bill. variable bill is 0 if the transaction is not
 * supposed to be billed and 1 otherwise. For any other value, no action is
 * taken. If the transaction is billable, eventName has to be specified under
 * which the transaction is to billed.
 *
 * Input Parameters:
 *             txnp          :INKHttpTxn: The transaction under consideration.
 *             bill          :int       : 0 if transaction is not billed.
 *                                        1 otherwise.
 *                                        Any other value, input ignored.
 *             eventName     :string    : Name of the event if transaction is
 *                                        billable. Else NULL.
 *
 *
 * Output Parameters:
 *             NONE
 *
 *
 * Return Value:
 *            INK_SUCCESS                : Success
 *            INK_ERROR                : Failure
 */
INKReturnCode
INKHttpTxnBillable(INKHttpTxn txnp, int bill, const char *eventName)
{

  INKMBuffer clientReqHdr;
  INKMLoc clientReqHdrLoc;
  INKMLoc hdr_loc;

  if ((INKHttpTxnClientReqGet(txnp, &clientReqHdr, &clientReqHdrLoc)) == 0) {
    printf("Cannot retrieve Client's Request\n");
    return INK_ERROR;
  }

  switch (bill) {
  case 0:
    if ((INKMimeHdrFieldFind(clientReqHdr, clientReqHdrLoc, "@Bill", -1)) == 0) {
      hdr_loc = INKMimeHdrFieldCreate(clientReqHdr, clientReqHdrLoc);
      INKMimeHdrFieldNameSet(clientReqHdr, clientReqHdrLoc, hdr_loc, "@Bill", -1);
      INKMimeHdrFieldValueInsertInt(clientReqHdr, clientReqHdrLoc, hdr_loc, 0, -1);
      INKMimeHdrFieldInsert(clientReqHdr, clientReqHdrLoc, hdr_loc, -1);
    }

    if ((INKMimeHdrFieldFind(clientReqHdr, clientReqHdrLoc, "@Event", -1)) == 0) {
      hdr_loc = INKMimeHdrFieldCreate(clientReqHdr, clientReqHdrLoc);
      INKMimeHdrFieldNameSet(clientReqHdr, clientReqHdrLoc, hdr_loc, "@Event", -1);
      INKMimeHdrFieldInsert(clientReqHdr, clientReqHdrLoc, hdr_loc, -1);
    }
    break;

  case 1:
    if (eventName == NULL) {
      return INK_ERROR;
    }

    INKDebug("aaa_api", "[Billable]: Billing the transaction with %s event", eventName);

    if ((hdr_loc = INKMimeHdrFieldFind(clientReqHdr, clientReqHdrLoc, "@Bill", -1)) == 0) {
      INKDebug("aaa_api", "[Billable]: Not Found the header @Bill");
      hdr_loc = INKMimeHdrFieldCreate(clientReqHdr, clientReqHdrLoc);
      INKMimeHdrFieldNameSet(clientReqHdr, clientReqHdrLoc, hdr_loc, "@Bill", -1);
      INKMimeHdrFieldValueInsertInt(clientReqHdr, clientReqHdrLoc, hdr_loc, 1, -1);
      INKMimeHdrFieldInsert(clientReqHdr, clientReqHdrLoc, hdr_loc, -1);
    } else {
      int tempValue;
      tempValue = INKMimeHdrFieldValueGetInt(clientReqHdr, clientReqHdrLoc, hdr_loc, 0);
      INKMimeHdrFieldValuesClear(clientReqHdr, clientReqHdrLoc, hdr_loc);
      INKMimeHdrFieldValueInsertInt(clientReqHdr, clientReqHdrLoc, hdr_loc, ++tempValue, -1);
    }


    if ((hdr_loc = INKMimeHdrFieldFind(clientReqHdr, clientReqHdrLoc, "@Event", -1)) == 0) {
      hdr_loc = INKMimeHdrFieldCreate(clientReqHdr, clientReqHdrLoc);
      INKMimeHdrFieldNameSet(clientReqHdr, clientReqHdrLoc, hdr_loc, "@Event", -1);
      INKMimeHdrFieldValueInsert(clientReqHdr, clientReqHdrLoc, hdr_loc, eventName, -1, -1);
      INKMimeHdrFieldInsert(clientReqHdr, clientReqHdrLoc, hdr_loc, -1);
    } else {
      INKMimeHdrFieldValueInsert(clientReqHdr, clientReqHdrLoc, hdr_loc, eventName, -1, -1);
    }
    break;

  default:
    break;
  }

  return INK_SUCCESS;
}

/**
 * END AAA API
 */

/**
  * AAA policy contiunation set API
  */
static INKCont policy_contp = NULL;

void
INKPolicyContSet(INKCont p)
{
  policy_contp = p;
}

INKReturnCode
INKUserPolicyFetch(INKU32 ip, char *name)
{
  if (policy_contp != NULL) {
    int has_lock = 0;
    INKMutex mtx;
    struct USER_INFO *node;

    node = (struct USER_INFO *) INKmalloc(sizeof(struct USER_INFO));
    node->ipaddr = ip;
    node->name = INKstrdup(name);
    node->policy = NULL;
    node->len = 0;
    node->status = POLICY_FETCHING;

    mtx = INKContMutexGet(policy_contp);
    if (mtx)
      has_lock = 1;
    if (has_lock)
      INKMutexLock(mtx);
    INKContCall(policy_contp, INK_EVENT_POLICY_LOOKUP, (void *) node);
    if (has_lock)
      INKMutexUnlock(mtx);
    INKfree(node->name);
    INKfree(node);

    return INK_SUCCESS;
  } else {
    INKDebug("aaa_api", "[INKUserPolicyFetch]: policy continuation is not set");
    return INK_ERROR;
  }
}

/**
  * End AAA policy contiunation set API
  */

/**
 * AAA USER CACHE API
 */

void freeNode(struct USER_INFO *a);
void utoa(unsigned int input_int_value, char *output_str);
void *marshal(int *marshal_length, INKU32 ip, char *name, status_t s, void *p, int l);
void unmarshal(void *tem, INKU32 * ip, char **name, status_t * s, void **p, int *l);


int disk_remove(INKU32 ip);
int disk_read(INKU32 ip, INKCont caller_cont);
int disk_write(INKU32 ip, void *data, int len);

void HashTableInit();
void HashTableDelete(INKU32 ip);
int HashTableInsert(INKU32 ip, char *name, status_t status, void *policy, int len);
struct USER_INFO *HashTableLookup(INKU32 ip);
int HashTableModify(INKU32 ip, char *name, status_t status, void *policy, int len);
INKAction INKCacheOverwrite(INKCont contp, INKCacheKey key);

/* *************************************************************************
 * Wrapper functions:    interface for memory and disk operation           *
 ***************************************************************************/
INKAction
INKCacheOverwrite(INKCont contp, INKCacheKey key)
{
  FORCE_PLUGIN_MUTEX(contp);
  CacheInfo *info = (CacheInfo *) key;
  Continuation *i = (INKContInternal *) contp;
  return (INKAction)
    cacheProcessor.open_write(i, 0, &info->cache_key,
                              info->frag_type, true, info->pin_in_cache, info->hostname, info->len);
}

void
UserCacheInit()
{
  HashTableInit();
}

void
UserCacheDelete(INKU32 ip)
{
  HashTableDelete(ip);

  disk_remove(ip);
}

int
UserCacheInsert(INKU32 ip, char *name, status_t s, void *p, int len)
{
  void *user;
  int marshal_length;
  int returnValue;

  returnValue = HashTableInsert(ip, name, s, p, len);

  user = marshal(&marshal_length, ip, name, s, p, len);

  disk_write(ip, user, marshal_length);
  INKfree(user);

  if (p == NULL) {
    INKUserPolicyFetch(ip, name);
  }

  return returnValue;
}

struct USER_INFO *
UserCacheLookup(INKU32 ip, INKCont caller_cont)
{
  if (caller_cont) {
    disk_read(ip, caller_cont);
    return NULL;
  }

  return HashTableLookup(ip);
}

int
UserCacheModify(INKU32 ip, char *name, status_t s, void *p, int l)
{
  void *user;
  int marshal_length;
  int returnValue;

  returnValue = HashTableModify(ip, name, s, p, l);

  user = marshal(&marshal_length, ip, name, s, p, l);
  disk_write(ip, user, marshal_length);
  INKfree(user);

  return returnValue;
}


void
UserCacheCloneFree(struct USER_INFO *a)
{
  freeNode(a);
}


/* *************************************************************************
 *                      hash table for memory operation                    *
 ***************************************************************************/
INKMgmtInt hashtablesize;
static struct USER_INFO **hashtable;
static INKMutex *hashlock;

void
HashTableInit()
{
  int i;

  if (INKMgmtIntGet("proxy.config.aaa.hashtable.size", &hashtablesize) != 0) {
    INKError("[HashTableInit] unable to read configuration option: proxy.config.aaa.hashtableinit.size");
  }
  INKDebug("aaa_cache", "[HashTableInit]: hashtablesize = %d", (int) hashtablesize);

  /* alloc memory for hash table pointers */
  hashtable = (struct USER_INFO **) INKmalloc(hashtablesize * (sizeof(struct USER_INFO *)));
  /* initialize the hash table with NULL */
  memset(hashtable, 0, (hashtablesize * (sizeof(struct USER_INFO *))));

  /* alloc memory for hash table bucket mutexes */
  hashlock = (INKMutex *) INKmalloc(hashtablesize * (sizeof(INKMutex)));

  /* allocate a lock for each cache entry. */
  for (i = 0; i < hashtablesize; i++) {
    hashlock[i] = INKMutexCreate();
  }
}

/* the simplest hash function */
int
HashTableEntryGet(INKU32 key)
{
  return (key % hashtablesize);
}

/* Insert an user's info into the entry of hashtable corresponding to
 * its IP addess. Warning: pass in a structure with no pointer within it.
 * Return 1 if success; 0 if failure.
 */
int
HashTableInsert(INKU32 ip, char *name, status_t s, void *p, int len)
{
  int index;
  struct USER_INFO *newnode;

  newnode = (struct USER_INFO *) INKmalloc(sizeof(struct USER_INFO));

  newnode->ipaddr = ip;
  newnode->name = INKstrdup(name);      /* INKmalloc memory and return a dup of name */
  newnode->status = s;
  newnode->len = len;
  newnode->policy = INKstrndup((char *) p, len);

  /* delete the old node whose ip is the same as the new node if any */
  USER_INFO *tmpUserInfo = HashTableLookup(ip);
  if (tmpUserInfo != NULL) {
    INKfree(tmpUserInfo);
    HashTableDelete(ip);
  } else {
    INKfree(tmpUserInfo);
  }

  index = HashTableEntryGet(ip);

  INKMutexLock(hashlock[index]);

  /* insert the new node at the beginnning of the cache entry */
  newnode->next = hashtable[index];
  hashtable[index] = newnode;

  INKDebug("aaa_cache", "hashtable: user \"%s\" inserted in bucket %d", hashtable[index]->name, index);

  INKMutexUnlock(hashlock[index]);
  return 1;
}


/* If failed, returns 0, else returns pointer to the structure for the user. */
struct USER_INFO *
HashTablePtrGet(INKU32 ip)
{
  struct USER_INFO *cur;
  int index;

  index = HashTableEntryGet(ip);

  INKMutexLock(hashlock[index]);

  cur = hashtable[index];

  while ((cur != NULL) && (cur->ipaddr != ip)) {
    cur = cur->next;
  }

  INKMutexUnlock(hashlock[index]);

  if (cur) {
    INKDebug("aaa_cache", "hashtable: user \"%s\" found in bucket %d", cur->name, index);
    return cur;
  }
  return (struct USER_INFO *) 0;
}

/* If failed, returns 0, else returns pointer to the CLONE of the structure for the user.
 * It's your responsiblity to free the memory
 */
struct USER_INFO *
HashTableLookup(INKU32 ip)
{
  struct USER_INFO *clone;
  struct USER_INFO *cur;
  int index;

  index = HashTableEntryGet(ip);
  /* Fixed race condition between HashTableLookup(),
   * HashTableDelete(), HashTableModify() if we call INKMutexLock()
   * after HashTablePtrGet()
   */
  INKMutexLock(hashlock[index]);

  cur = HashTablePtrGet(ip);

  if (cur) {
    /* clone a node */
    clone = (struct USER_INFO *) INKmalloc(sizeof(struct USER_INFO));

    clone->ipaddr = cur->ipaddr;
    clone->name = INKstrdup(cur->name);
    clone->status = cur->status;
    clone->len = cur->len;
    clone->policy = INKstrndup((char *) cur->policy, cur->len);

    INKMutexUnlock(hashlock[index]);

    return clone;
  }

  INKMutexUnlock(hashlock[index]);

  return (struct USER_INFO *) 0;
}

/* delete all the nodes whose ipaddr is the key ip, free the memory  */
void
HashTableDelete(INKU32 ip)
{
  struct USER_INFO *a, *last, *next;
  int index;
  /* keep track of how many nodes are deleted */
  int found = 0;

  index = HashTableEntryGet(ip);
  last = NULL;

  INKMutexLock(hashlock[index]);

  /* delete all the nodes in this entry with the IP address as ip */
  for (a = hashtable[index]; a; a = next) {
    next = a->next;
    if (a->ipaddr == ip) {

      found++;

      if (last) {
        last->next = a->next;
      } else {
        hashtable[index] = a->next;
      }

      INKDebug("aaa_cache", "hashtable: user \"%s\" deleted from bucket %d; %dth time", a->name, index, found);

      freeNode(a);
      continue;
    }
    last = a;
  }

  INKMutexUnlock(hashlock[index]);
  if (!found)
    INKDebug("aaa_cache", "hashtable: no entry for user with ip = %d", ip);
}

/* Update the user info with the same IP if possible
 * Return -1 if the stale info is not found;
 *         0 if the stale info is found and being replaced by new one
 */
int
HashTableModify(INKU32 ip, char *name, status_t s, void *policy, int len)
{
  struct USER_INFO *old;
  int index;

  index = HashTableEntryGet(ip);

  /* Fixed race condition between HashTableLookup(),
   * HashTableDelete(), HashTableModify() if we call INKMutexLock()
   * after HashTablePtrGet()
   */
  INKMutexLock(hashlock[index]);

  old = HashTablePtrGet(ip);

  if (!old) {
    INKMutexUnlock(hashlock[index]);
    return -1;
  }

  /* update the user info */
  if (old->name) {
    INKfree(old->name);
  }
  old->name = INKstrdup(name);
  old->status = s;
  old->len = len;
  if (old->policy) {
    INKfree(old->policy);
  }
  old->policy = INKstrndup((char *) policy, len);

  INKMutexUnlock(hashlock[index]);

  return 0;
}


/*******************************************************************************
 *                  Disk Remove                	                               *
 *******************************************************************************/

static int
remove_cache_handler(INKCont cache_contp, INKEvent event, void *edata)
{
  INKCacheKey key;

  key = (INKCacheKey) INKContDataGet(cache_contp);

  switch (event) {
  case INK_EVENT_CACHE_REMOVE:
    INKDebug("aaa_cache", "[remove_cache_handler]: removed !");
    break;
  case INK_EVENT_CACHE_REMOVE_FAILED:
    INKDebug("aaa_cache", "[remove_cache_handler]: remove failed !");
    break;
  default:
    INKDebug("aaa_cache", "[remove_cache_handler]: unexpected event, %d", event);
    break;
  }

  INKCacheKeyDestroy(key);
  INKContDestroy(cache_contp);
  return 0;
}

int
disk_remove(INKU32 ip)
{
  INKMutex cache_mtx;
  INKCont cache_contp;
  INKCacheKey key;

  char input[50];
  int ilen;

  utoa(ip, input);
  ilen = strlen(input);

  cache_mtx = INKMutexCreate();

  cache_contp = INKContCreate(remove_cache_handler, cache_mtx);

  INKCacheKeyCreate(&key);
  INKCacheKeyDigestSet(key, (unsigned char *) input, ilen);

  INKContDataSet(cache_contp, key);

  INKCacheRemove(cache_contp, key);

  return 0;
}

typedef struct
{
  INKAction actionp;
  INKCacheKey key;
  INKVConn cache_vc;
  INKVIO cache_read_vio;
  INKVIO cache_write_vio;
  INKIOBuffer read_buf;
  INKIOBuffer write_buf;
  INKIOBufferReader read_bufreader;
  INKIOBufferReader write_bufreader;
  INKCont caller_cont;
  char *data;
  int data_len;
  INKU32 ip;

} CacheStruct;

static void
destroy_cache_s(INKCont cache_contp)
{

  CacheStruct *cache_s;

  cache_s = (CacheStruct *) INKContDataGet(cache_contp);

  cache_s->caller_cont = NULL;

  if (cache_s->actionp) {
    INKActionCancel(cache_s->actionp);
  }
  if (cache_s->key) {
    INKCacheKeyDestroy(cache_s->key);
  }
  if (cache_s->cache_vc) {
    INKVConnAbort(cache_s->cache_vc, 1);
  }
  if (cache_s->read_buf) {
    INKIOBufferDestroy(cache_s->read_buf);
  }
  if (cache_s->write_buf) {
    INKIOBufferDestroy(cache_s->write_buf);
  }
  if (cache_s->data) {
    INKfree(cache_s->data);
  }

  INKfree(cache_s);
  INKContDestroy(cache_contp);
  return;
}


/*******************************************************************************
 *                  Disk Write                                   	       *
 *******************************************************************************/

typedef struct
{
  char *data;
  int data_len;
  INKU32 ip;
} passdata_s;

static int
handle_write_fail(INKCont cont, INKEvent event, void *edata)
{
  passdata_s *pd;

  pd = (passdata_s *) INKContDataGet(cont);

  INKDebug("aaa_cache", "[handle_write_fail]: called !");

  disk_write(pd->ip, pd->data, pd->data_len);

  if (pd->data)
    INKfree(pd->data);
  INKfree(pd);
  INKContDestroy(cont);
  return 0;
}

static int
write_cache_handler(INKCont cache_contp, INKEvent event, void *edata)
{

  CacheStruct *cache_s;
  INKCont cont;
  passdata_s *pd;

  cache_s = (CacheStruct *) INKContDataGet(cache_contp);

  switch (event) {
  case INK_EVENT_CACHE_OPEN_WRITE:
    INKDebug("aaa_cache", "[write_cache_handler]: INK_EVENT_CACHE_OPEN_WRITE");
    cache_s->cache_vc = (INKVConn) edata;
    cache_s->cache_write_vio = INKVConnWrite(cache_s->cache_vc, cache_contp,
                                             cache_s->write_bufreader,
                                             INKIOBufferReaderAvail(cache_s->write_bufreader));
    break;
  case INK_EVENT_CACHE_OPEN_WRITE_FAILED:
    INKDebug("aaa_cache", "[write_cache_handler]: INK_EVENT_CACHE_OPEN_WRITE_FAILED");
    pd = (passdata_s *) INKmalloc(sizeof(passdata_s));
    pd->ip = cache_s->ip;
    pd->data = (char *) INKmalloc(cache_s->data_len);
    memset(pd->data, 0, cache_s->data_len);
    memcpy(pd->data, cache_s->data, cache_s->data_len);
    pd->data_len = cache_s->data_len;

    cont = INKContCreate(handle_write_fail, INKMutexCreate());
    INKContDataSet(cont, pd);
    INKContSchedule(cont, 100);
    destroy_cache_s(cache_contp);
    break;
  case INK_EVENT_VCONN_WRITE_READY:
    INKVIOReenable(cache_s->cache_write_vio);
    break;
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    INKDebug("aaa_cache", "[write_cache_handler]: data written to cache");
    INKVConnClose(cache_s->cache_vc);
    cache_s->cache_vc = NULL;
    cache_s->cache_write_vio = NULL;
    destroy_cache_s(cache_contp);
    return 0;
  default:
    INKDebug("aaa_cache", "[write_cache_handler]: unexpected event, %d", event);
    destroy_cache_s(cache_contp);
    return 0;
  }

  return 0;
}

int
disk_write(INKU32 ip, void *data, int len)
{
  CacheStruct *cache_s;
  INKMutex cache_mtx;
  INKCont cache_contp;
  INKAction action;
  char input[50];
  int ilen;

  utoa(ip, input);
  ilen = strlen(input);

  cache_mtx = INKMutexCreate();
  cache_contp = INKContCreate(write_cache_handler, cache_mtx);

  cache_s = (CacheStruct *) INKmalloc(sizeof(CacheStruct));

  cache_s->actionp = NULL;
  cache_s->caller_cont = NULL;

  INKCacheKeyCreate(&(cache_s->key));
  INKCacheKeyDigestSet(cache_s->key, (unsigned char *) input, ilen);

  cache_s->cache_vc = NULL;
  cache_s->cache_read_vio = NULL;
  cache_s->cache_write_vio = NULL;
  cache_s->read_buf = NULL;
  cache_s->read_bufreader = NULL;

  cache_s->write_buf = INKIOBufferCreate();
  cache_s->write_bufreader = INKIOBufferReaderAlloc(cache_s->write_buf);
  INKIOBufferWrite(cache_s->write_buf, (char *) data, len);     /* new API, no doc */
  /* save the data in case write fails in this round */
  cache_s->data = (char *) INKmalloc(len * sizeof(char));
  memset(cache_s->data, 0, len);
  memcpy(cache_s->data, data, len);
  cache_s->data_len = len;
  cache_s->ip = ip;


  INKContDataSet(cache_contp, cache_s);

  action = INKCacheOverwrite(cache_contp, cache_s->key);
  if (!INKActionDone(action)) {
    cache_s->actionp = action;
  }
  return 0;
}


/***********************************************************************
 *       Disk Read                                                     *
 *       1  disk read if it is not in memory                           *
 *       2  memory insert and call the event INK_EVENT_POLICY_LOOKUP   *
 *             if disk cache read succeeds                             *
 ***********************************************************************/

static int
read_cache_handler(INKCont cache_contp, INKEvent event, void *edata)
{
  CacheStruct *cache_s;
  struct USER_INFO *node;
  INKMutex caller_mtx;
  int has_lock = 0;

  int total_avail;
  int block_avail;
  int output_len;
  char *output_string;
  INKIOBufferBlock block;
  const char *block_start;

  INKU32 ip;
  char *name;
  status_t s;
  void *p;
  int len = 0, object_size = 0;

  cache_s = (CacheStruct *) INKContDataGet(cache_contp);

  switch (event) {

  case INK_EVENT_CACHE_OPEN_READ:
    INKDebug("aaa_cache", "[read_cache_handler]: HIT, begin reading data from disk");
    cache_s->cache_vc = (INKVConn) edata;
    cache_s->read_buf = INKIOBufferCreate();
    cache_s->read_bufreader = INKIOBufferReaderAlloc(cache_s->read_buf);
    INKVConnCacheObjectSizeGet(cache_s->cache_vc, &object_size);
    cache_s->cache_read_vio = INKVConnRead(cache_s->cache_vc, cache_contp, cache_s->read_buf, object_size);
    break;
  case INK_EVENT_CACHE_OPEN_READ_FAILED:
    INKDebug("aaa_cache", "[read_cache_handler]: INK_EVENT_CACHE_OPEN_READ_FAILED");
    caller_mtx = INKContMutexGet(cache_s->caller_cont);
    if (caller_mtx)
      has_lock = 1;
    if (has_lock)
      INKMutexLock(caller_mtx);
    /* send NULL to caller to signal error */
    INKContCall(cache_s->caller_cont, INK_EVENT_POLICY_LOOKUP, NULL);
    if (has_lock)
      INKMutexUnlock(caller_mtx);
    destroy_cache_s(cache_contp);
    break;
  case INK_EVENT_VCONN_READ_READY:
    INKVIOReenable(cache_s->cache_read_vio);
    break;
  case INK_EVENT_VCONN_READ_COMPLETE:
    INKDebug("aaa_cache", "[read_cache_handler]: complete reading from disk");

    /* print read content to stderr in following block, adopted from output_hdr.c */

    total_avail = INKIOBufferReaderAvail(cache_s->read_bufreader);
    output_string = (char *) INKmalloc(total_avail + 1);
    output_len = 0;

    block = INKIOBufferReaderStart(cache_s->read_bufreader);
    while (block) {
      block_start = INKIOBufferBlockReadStart(block, cache_s->read_bufreader, &block_avail);
      if (block_avail == 0) {
        break;
      }
      memcpy(output_string + output_len, block_start, block_avail);
      output_len += block_avail;
      INKIOBufferReaderConsume(cache_s->read_bufreader, block_avail);
      block = INKIOBufferReaderStart(cache_s->read_bufreader);
    }

    unmarshal(output_string, &ip, &name, &s, &p, &len);

    INKDebug("aaa_cache", "[read_cache_handler]: name/IP/policy read from disk");
    INKDebug("aaa_cache", "Read data from disk: name = \"%s\"", name);
    INKDebug("aaa_cache", "Read data from disk: IP = \"%ld\"", ip);
    INKDebug("aaa_cache", "Read data from disk: status = \"%ld\"", s);
    INKDebug("aaa_cache", "Read data from disk: policy = \"%s\"", p);

    HashTableInsert(ip, name, s, p, len);

    node = (struct USER_INFO *) INKmalloc(sizeof(struct USER_INFO));
    node->ipaddr = ip;
    node->name = INKstrdup(name);
    node->status = s;
    node->len = len;
    node->policy = INKstrndup((char *) p, len);

    /* call the caller's handle to tell that the data is available */
    caller_mtx = INKContMutexGet(cache_s->caller_cont);
    if (caller_mtx)
      has_lock = 1;
    if (has_lock)
      INKMutexLock(caller_mtx);
    INKContCall(cache_s->caller_cont, INK_EVENT_POLICY_LOOKUP, (void *) node);
    if (has_lock)
      INKMutexUnlock(caller_mtx);

    if (name)
      INKfree(name);
    if (p)
      INKfree(p);
    INKfree(output_string);

    /* close the vconnection and destroy the cache structre obj cache_s */
    INKVConnClose(cache_s->cache_vc);
    cache_s->cache_vc = NULL;
    cache_s->cache_read_vio = NULL;
    destroy_cache_s(cache_contp);
    return 0;

  default:
    INKDebug("aaa_cache", "[read_cache_handler]: unexpected event, %d", event);
    destroy_cache_s(cache_contp);
    return 0;
  }

  return 0;
}


int
disk_read(INKU32 ip, INKCont caller_cont)
{
  CacheStruct *cache_s;
  INKMutex cache_mtx;
  INKCont cache_contp;
  struct USER_INFO *node;
  INKAction action;
  char input[50];
  int ilen;

  /* check if the data in memory, if yes, simple
   * schedule the event to caller_cont handle
   */
  node = HashTableLookup(ip);
  if (node) {
    INKMutex caller_mtx;

    INKDebug("aaa_cache", "[disk_read]: name/IP/policy is read from memory");
    caller_mtx = INKContMutexGet(caller_cont);
    INKMutexLock(caller_mtx);
    INKContCall(caller_cont, INK_EVENT_POLICY_LOOKUP, (void *) node);
    INKMutexUnlock(caller_mtx);
    return 0;
  }

  utoa(ip, input);
  ilen = strlen(input);

  cache_mtx = INKMutexCreate();
  cache_contp = INKContCreate(read_cache_handler, cache_mtx);

  cache_s = (CacheStruct *) INKmalloc(sizeof(CacheStruct));

  cache_s->actionp = NULL;

  INKCacheKeyCreate(&(cache_s->key));
  INKCacheKeyDigestSet(cache_s->key, (unsigned char *) input, ilen);

  cache_s->caller_cont = caller_cont;
  INKAssert(cache_s->caller_cont);

  cache_s->cache_vc = NULL;
  cache_s->cache_read_vio = NULL;
  cache_s->cache_write_vio = NULL;
  cache_s->read_buf = NULL;
  cache_s->write_buf = NULL;
  cache_s->read_bufreader = NULL;
  cache_s->write_bufreader = NULL;
  cache_s->data = NULL;

  INKContDataSet(cache_contp, cache_s);

  action = INKCacheRead(cache_contp, cache_s->key);
  if (!INKActionDone(action)) {
    cache_s->actionp = action;
  }

  return 0;
}

/*******************************************************************************
 *       functions to convert unsigned int into a string        	       *
 *******************************************************************************/

/* reverse the string s with null terminator */
void
reverse(char *s)
{
  char *head;
  char *tail;
  char c;

  head = s;
  tail = s + strlen(s) - 1;

  while (head < tail) {
    c = *head;
    *head = *tail;
    *tail = c;
    head++;
    tail--;
  }
}

/*    Unsigned int ---> a string with the same form,
 * e.g. change the integer 123456789 into "123456789".
 * Note: allocate memory for sv before call utoa */
void
utoa(unsigned int iv, char *sv)
{
  int i = 0;

  if (iv == 0) {
    sv[0] = '0', sv[1] = '\0';
  } else {
    while (iv % 10 || iv / 10) {
      sv[i++] = '0' + iv % 10;
      iv /= 10;
    }
    sv[i] = '\0';

    reverse(sv);
  }
}

/* *************************************************************************
 *     functions to marsh and unmarsh a structure for disk access          *
 ***************************************************************************/

/* Convert a structrue into a piesce of memory which is returned as void *.
 * The memory is allocated in this function and should be INKfreed by you.
 */
void *
marshal(int *marshal_length, INKU32 ip, char *name, status_t s, void *p, int len)
{
  char *temp;
  int size;
  int name_len;
  int cur_pos = 0;

  name_len = strlen(name);

  // INKAssert(len == strlen((char *)p));
  size = 8 + sizeof(int) + name_len + sizeof(status_t) + sizeof(int) + len + 1;
  temp = (char *) INKmalloc(size);
  memset(temp, 0, size);

  memcpy(temp, &ip, 8);

  cur_pos += 8;
  memcpy(temp + cur_pos, &name_len, sizeof(int));

  cur_pos += sizeof(int);
  memcpy(temp + cur_pos, name, name_len);

  cur_pos += name_len;
  memcpy(temp + cur_pos, &s, sizeof(status_t));

  cur_pos += sizeof(status_t);
  memcpy(temp + cur_pos, &len, sizeof(int));

  cur_pos += sizeof(int);
  memcpy(temp + cur_pos, p, len);
  cur_pos += len;
  // INKAssert(size == cur_pos + 1);
  *marshal_length = cur_pos;

  return (void *) temp;
}

/* Unmarshal a piesce of memory. Allocate memory for name, policy.
 * The memory is allocated in this function and should be INKfreed by you.
 */
void
unmarshal(void *tem, INKU32 * ip, char **name, status_t * s, void **p, int *l)
{
  int cur_pos = 0;
  int len;
  char *temp;

  temp = (char *) tem;

  memcpy(ip, temp, 8);
  cur_pos += 8;
  memcpy(&len, temp + cur_pos, sizeof(int));

  cur_pos += sizeof(int);
  *name = (char *) INKmalloc((len + 1) * sizeof(char));
  memset(*name, 0, len + 1);
  memcpy(*name, temp + cur_pos, len);

  cur_pos += len;
  memcpy(s, temp + cur_pos, sizeof(status_t));

  cur_pos += sizeof(status_t);
  memcpy(l, temp + cur_pos, sizeof(int));
  cur_pos += sizeof(int);
  *p = (char *) INKmalloc((*l + 1) * sizeof(char));
  memset(*p, 0, *l + 1);
  memcpy(*p, temp + cur_pos, *l);
  cur_pos += *l;
}

/* free a node */
void
freeNode(struct USER_INFO *a)
{
  if (a->name)
    INKfree(a->name);
  if (a->policy)
    INKfree(a->policy);
  INKfree(a);
}

/**
 * END AAA USER CACHE API
 */

/* Alarm */
/* return type is "int" currently, it should be INKReturnCode */
int
INKSignalWarning(INKAlarmType code, char *msg)
{
  if (code<INK_SIGNAL_WDA_BILLING_CONNECTION_DIED || code> INK_SIGNAL_WDA_RADIUS_CORRUPTED_PACKETS || msg == NULL)
    return -1;                  //INK_ERROR

  REC_SignalWarning(code, msg);
  return 0;                     //INK_SUCCESS
}

void
INKICPFreshnessFuncSet(INKPluginFreshnessCalcFunc funcp)
{
  pluginFreshnessCalcFunc = (PluginFreshnessCalcFunc) funcp;
}

int
INKICPCachedReqGet(INKCont contp, INKMBuffer * bufp, INKMLoc * obj)
{
  ICPPeerReadCont *sm = (ICPPeerReadCont *) contp;
  HTTPInfo *cached_obj;

  if (sm == NULL)
    return 0;

  cached_obj = sm->_object_read;
  if (cached_obj == NULL || !cached_obj->valid())
    return 0;

  HTTPHdr *cached_hdr = cached_obj->request_get();
  if (!cached_hdr->valid())
    return 0;

  // We can't use the HdrHeapSDKHandle structure in the RamCache since multiple
  //  threads can access.  We need to create our own for the transaction and return that.
  HdrHeapSDKHandle **handle = &(sm->_cache_req_hdr_heap_handle);
  if (*handle == NULL) {
    *handle = (HdrHeapSDKHandle *) xmalloc(sizeof(HdrHeapSDKHandle));
    (*handle)->m_heap = cached_hdr->m_heap;
    (*handle)->m_sdk_alloc.init();
  }

  *bufp = *handle;
  *obj = cached_hdr->m_http;
  sdk_sanity_check_mbuffer(*bufp);

  return 1;
}

int
INKICPCachedRespGet(INKCont contp, INKMBuffer * bufp, INKMLoc * obj)
{
  ICPPeerReadCont *sm = (ICPPeerReadCont *) contp;
  HTTPInfo *cached_obj;

  if (sm == NULL)
    return 0;

  cached_obj = sm->_object_read;
  if (cached_obj == NULL || !cached_obj->valid())
    return 0;

  HTTPHdr *cached_hdr = cached_obj->response_get();
  if (!cached_hdr->valid())
    return 0;

  // We can't use the HdrHeapSDKHandle structure in the RamCache since multiple
  //  threads can access.  We need to create our own for the transaction and return that.
  HdrHeapSDKHandle **handle = &(sm->_cache_resp_hdr_heap_handle);
  if (*handle == NULL) {
    *handle = (HdrHeapSDKHandle *) xmalloc(sizeof(HdrHeapSDKHandle));
    (*handle)->m_heap = cached_hdr->m_heap;
    (*handle)->m_sdk_alloc.init();
  }

  *bufp = *handle;
  *obj = cached_hdr->m_http;
  sdk_sanity_check_mbuffer(*bufp);

  return 1;
}

INKReturnCode
INKSetCacheUrl(INKHttpTxn txnp, const char *url)
{
  HttpSM *sm = (HttpSM *) txnp;
  Debug("cache_url", "[INKSetCacheUrl]");

  if (sm->t_state.cache_info.lookup_url == NULL) {
    Debug("cache_url", "[INKSetCacheUrl] changing the cache url to: %s", url);

    int size = strlen(url);
    sm->t_state.cache_info.lookup_url_storage.create(NULL);
    sm->t_state.cache_info.lookup_url = &(sm->t_state.cache_info.lookup_url_storage);
    sm->t_state.cache_info.lookup_url->parse(url, size);
  } else {
    return INK_ERROR;
  }

  return INK_SUCCESS;
}

INKHttpTxn
INKCacheGetStateMachine(INKCacheTxn txnp)
{
  NewCacheVC *vc = (NewCacheVC *) txnp;

  HttpCacheSM *cacheSm = (HttpCacheSM *) vc->getCacheSm();

  return cacheSm->master_sm;
}

void
INKCacheHttpInfoKeySet(INKCacheHttpInfo infop, INKCacheKey keyp)
{
  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  INK_MD5 *key = (INK_MD5 *) keyp;
  info->object_key_set(*key);
}

void
INKCacheHttpInfoSizeSet(INKCacheHttpInfo infop, INKU64 size)
{
  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  info->object_size_set(size);
}


#endif //INK_NO_API
