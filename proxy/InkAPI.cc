/** @file

  Implements callin functions for TSAPI plugins.

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

#ifndef TS_NO_API

// Avoid complaining about the deprecated APIs.
// #define TS_DEPRECATED

#include <stdio.h>

#include "inktomi++.h"
#include "I_Layout.h"

#include "ts.h"
#include "InkAPIInternal.h"
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
#include "ProxyConfig.h"
#include "stats/CoupledStats.h"
#include "stats/Stats.h"
#include "Plugin.h"
#include "LogObject.h"
#include "LogConfig.h"
//#include "UserNameCache.h"
#include "PluginVC.h"
#include "api/ts/experimental.h"
#include "ICP.h"
#include "HttpAccept.h"
#include "PluginVC.h"
#include "FetchSM.h"
#if TS_HAS_V2STATS
#include <string> // TODO: Do we really need STL strings?
#include "StatSystemV2.h"
#endif
#include "HttpDebugNames.h"
#include "I_AIO.h"


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

// helper macro for setting HTTPHdr data
#define SET_HTTP_HDR(_HDR, _BUF_PTR, _OBJ_PTR) \
    _HDR.m_heap = ((HdrHeapSDKHandle*) _BUF_PTR)->m_heap; \
    _HDR.m_http = (HTTPHdrImpl*) _OBJ_PTR; \
    _HDR.m_mime = _HDR.m_http->m_fields_impl;

// Globals for new librecords stats
volatile int top_stat = 0;
RecRawStatBlock *api_rsb;


/* URL schemes */
tsapi const char *TS_URL_SCHEME_FILE;
tsapi const char *TS_URL_SCHEME_FTP;
tsapi const char *TS_URL_SCHEME_GOPHER;
tsapi const char *TS_URL_SCHEME_HTTP;
tsapi const char *TS_URL_SCHEME_HTTPS;
tsapi const char *TS_URL_SCHEME_MAILTO;
tsapi const char *TS_URL_SCHEME_NEWS;
tsapi const char *TS_URL_SCHEME_NNTP;
tsapi const char *TS_URL_SCHEME_PROSPERO;
tsapi const char *TS_URL_SCHEME_RTSP;
tsapi const char *TS_URL_SCHEME_RTSPU;
tsapi const char *TS_URL_SCHEME_TELNET;
tsapi const char *TS_URL_SCHEME_WAIS;

/* URL schemes string lengths */
tsapi int TS_URL_LEN_FILE;
tsapi int TS_URL_LEN_FTP;
tsapi int TS_URL_LEN_GOPHER;
tsapi int TS_URL_LEN_HTTP;
tsapi int TS_URL_LEN_HTTPS;
tsapi int TS_URL_LEN_MAILTO;
tsapi int TS_URL_LEN_NEWS;
tsapi int TS_URL_LEN_NNTP;
tsapi int TS_URL_LEN_PROSPERO;
tsapi int TS_URL_LEN_TELNET;
tsapi int TS_URL_LEN_WAIS;

/* MIME fields */
tsapi const char *TS_MIME_FIELD_ACCEPT;
tsapi const char *TS_MIME_FIELD_ACCEPT_CHARSET;
tsapi const char *TS_MIME_FIELD_ACCEPT_ENCODING;
tsapi const char *TS_MIME_FIELD_ACCEPT_LANGUAGE;
tsapi const char *TS_MIME_FIELD_ACCEPT_RANGES;
tsapi const char *TS_MIME_FIELD_AGE;
tsapi const char *TS_MIME_FIELD_ALLOW;
tsapi const char *TS_MIME_FIELD_APPROVED;
tsapi const char *TS_MIME_FIELD_AUTHORIZATION;
tsapi const char *TS_MIME_FIELD_BYTES;
tsapi const char *TS_MIME_FIELD_CACHE_CONTROL;
tsapi const char *TS_MIME_FIELD_CLIENT_IP;
tsapi const char *TS_MIME_FIELD_CONNECTION;
tsapi const char *TS_MIME_FIELD_CONTENT_BASE;
tsapi const char *TS_MIME_FIELD_CONTENT_ENCODING;
tsapi const char *TS_MIME_FIELD_CONTENT_LANGUAGE;
tsapi const char *TS_MIME_FIELD_CONTENT_LENGTH;
tsapi const char *TS_MIME_FIELD_CONTENT_LOCATION;
tsapi const char *TS_MIME_FIELD_CONTENT_MD5;
tsapi const char *TS_MIME_FIELD_CONTENT_RANGE;
tsapi const char *TS_MIME_FIELD_CONTENT_TYPE;
tsapi const char *TS_MIME_FIELD_CONTROL;
tsapi const char *TS_MIME_FIELD_COOKIE;
tsapi const char *TS_MIME_FIELD_DATE;
tsapi const char *TS_MIME_FIELD_DISTRIBUTION;
tsapi const char *TS_MIME_FIELD_ETAG;
tsapi const char *TS_MIME_FIELD_EXPECT;
tsapi const char *TS_MIME_FIELD_EXPIRES;
tsapi const char *TS_MIME_FIELD_FOLLOWUP_TO;
tsapi const char *TS_MIME_FIELD_FROM;
tsapi const char *TS_MIME_FIELD_HOST;
tsapi const char *TS_MIME_FIELD_IF_MATCH;
tsapi const char *TS_MIME_FIELD_IF_MODIFIED_SINCE;
tsapi const char *TS_MIME_FIELD_IF_NONE_MATCH;
tsapi const char *TS_MIME_FIELD_IF_RANGE;
tsapi const char *TS_MIME_FIELD_IF_UNMODIFIED_SINCE;
tsapi const char *TS_MIME_FIELD_KEEP_ALIVE;
tsapi const char *TS_MIME_FIELD_KEYWORDS;
tsapi const char *TS_MIME_FIELD_LAST_MODIFIED;
tsapi const char *TS_MIME_FIELD_LINES;
tsapi const char *TS_MIME_FIELD_LOCATION;
tsapi const char *TS_MIME_FIELD_MAX_FORWARDS;
tsapi const char *TS_MIME_FIELD_MESSAGE_ID;
tsapi const char *TS_MIME_FIELD_NEWSGROUPS;
tsapi const char *TS_MIME_FIELD_ORGANIZATION;
tsapi const char *TS_MIME_FIELD_PATH;
tsapi const char *TS_MIME_FIELD_PRAGMA;
tsapi const char *TS_MIME_FIELD_PROXY_AUTHENTICATE;
tsapi const char *TS_MIME_FIELD_PROXY_AUTHORIZATION;
tsapi const char *TS_MIME_FIELD_PROXY_CONNECTION;
tsapi const char *TS_MIME_FIELD_PUBLIC;
tsapi const char *TS_MIME_FIELD_RANGE;
tsapi const char *TS_MIME_FIELD_REFERENCES;
tsapi const char *TS_MIME_FIELD_REFERER;
tsapi const char *TS_MIME_FIELD_REPLY_TO;
tsapi const char *TS_MIME_FIELD_RETRY_AFTER;
tsapi const char *TS_MIME_FIELD_SENDER;
tsapi const char *TS_MIME_FIELD_SERVER;
tsapi const char *TS_MIME_FIELD_SET_COOKIE;
tsapi const char *TS_MIME_FIELD_SUBJECT;
tsapi const char *TS_MIME_FIELD_SUMMARY;
tsapi const char *TS_MIME_FIELD_TE;
tsapi const char *TS_MIME_FIELD_TRANSFER_ENCODING;
tsapi const char *TS_MIME_FIELD_UPGRADE;
tsapi const char *TS_MIME_FIELD_USER_AGENT;
tsapi const char *TS_MIME_FIELD_VARY;
tsapi const char *TS_MIME_FIELD_VIA;
tsapi const char *TS_MIME_FIELD_WARNING;
tsapi const char *TS_MIME_FIELD_WWW_AUTHENTICATE;
tsapi const char *TS_MIME_FIELD_XREF;
tsapi const char *TS_MIME_FIELD_X_FORWARDED_FOR;

/* MIME fields string lengths */
tsapi int TS_MIME_LEN_ACCEPT;
tsapi int TS_MIME_LEN_ACCEPT_CHARSET;
tsapi int TS_MIME_LEN_ACCEPT_ENCODING;
tsapi int TS_MIME_LEN_ACCEPT_LANGUAGE;
tsapi int TS_MIME_LEN_ACCEPT_RANGES;
tsapi int TS_MIME_LEN_AGE;
tsapi int TS_MIME_LEN_ALLOW;
tsapi int TS_MIME_LEN_APPROVED;
tsapi int TS_MIME_LEN_AUTHORIZATION;
tsapi int TS_MIME_LEN_BYTES;
tsapi int TS_MIME_LEN_CACHE_CONTROL;
tsapi int TS_MIME_LEN_CLIENT_IP;
tsapi int TS_MIME_LEN_CONNECTION;
tsapi int TS_MIME_LEN_CONTENT_BASE;
tsapi int TS_MIME_LEN_CONTENT_ENCODING;
tsapi int TS_MIME_LEN_CONTENT_LANGUAGE;
tsapi int TS_MIME_LEN_CONTENT_LENGTH;
tsapi int TS_MIME_LEN_CONTENT_LOCATION;
tsapi int TS_MIME_LEN_CONTENT_MD5;
tsapi int TS_MIME_LEN_CONTENT_RANGE;
tsapi int TS_MIME_LEN_CONTENT_TYPE;
tsapi int TS_MIME_LEN_CONTROL;
tsapi int TS_MIME_LEN_COOKIE;
tsapi int TS_MIME_LEN_DATE;
tsapi int TS_MIME_LEN_DISTRIBUTION;
tsapi int TS_MIME_LEN_ETAG;
tsapi int TS_MIME_LEN_EXPECT;
tsapi int TS_MIME_LEN_EXPIRES;
tsapi int TS_MIME_LEN_FOLLOWUP_TO;
tsapi int TS_MIME_LEN_FROM;
tsapi int TS_MIME_LEN_HOST;
tsapi int TS_MIME_LEN_IF_MATCH;
tsapi int TS_MIME_LEN_IF_MODIFIED_SINCE;
tsapi int TS_MIME_LEN_IF_NONE_MATCH;
tsapi int TS_MIME_LEN_IF_RANGE;
tsapi int TS_MIME_LEN_IF_UNMODIFIED_SINCE;
tsapi int TS_MIME_LEN_KEEP_ALIVE;
tsapi int TS_MIME_LEN_KEYWORDS;
tsapi int TS_MIME_LEN_LAST_MODIFIED;
tsapi int TS_MIME_LEN_LINES;
tsapi int TS_MIME_LEN_LOCATION;
tsapi int TS_MIME_LEN_MAX_FORWARDS;
tsapi int TS_MIME_LEN_MESSAGE_ID;
tsapi int TS_MIME_LEN_NEWSGROUPS;
tsapi int TS_MIME_LEN_ORGANIZATION;
tsapi int TS_MIME_LEN_PATH;
tsapi int TS_MIME_LEN_PRAGMA;
tsapi int TS_MIME_LEN_PROXY_AUTHENTICATE;
tsapi int TS_MIME_LEN_PROXY_AUTHORIZATION;
tsapi int TS_MIME_LEN_PROXY_CONNECTION;
tsapi int TS_MIME_LEN_PUBLIC;
tsapi int TS_MIME_LEN_RANGE;
tsapi int TS_MIME_LEN_REFERENCES;
tsapi int TS_MIME_LEN_REFERER;
tsapi int TS_MIME_LEN_REPLY_TO;
tsapi int TS_MIME_LEN_RETRY_AFTER;
tsapi int TS_MIME_LEN_SENDER;
tsapi int TS_MIME_LEN_SERVER;
tsapi int TS_MIME_LEN_SET_COOKIE;
tsapi int TS_MIME_LEN_SUBJECT;
tsapi int TS_MIME_LEN_SUMMARY;
tsapi int TS_MIME_LEN_TE;
tsapi int TS_MIME_LEN_TRANSFER_ENCODING;
tsapi int TS_MIME_LEN_UPGRADE;
tsapi int TS_MIME_LEN_USER_AGENT;
tsapi int TS_MIME_LEN_VARY;
tsapi int TS_MIME_LEN_VIA;
tsapi int TS_MIME_LEN_WARNING;
tsapi int TS_MIME_LEN_WWW_AUTHENTICATE;
tsapi int TS_MIME_LEN_XREF;
tsapi int TS_MIME_LEN_X_FORWARDED_FOR;


/* HTTP miscellaneous values */
tsapi const char *TS_HTTP_VALUE_BYTES;
tsapi const char *TS_HTTP_VALUE_CHUNKED;
tsapi const char *TS_HTTP_VALUE_CLOSE;
tsapi const char *TS_HTTP_VALUE_COMPRESS;
tsapi const char *TS_HTTP_VALUE_DEFLATE;
tsapi const char *TS_HTTP_VALUE_GZIP;
tsapi const char *TS_HTTP_VALUE_IDENTITY;
tsapi const char *TS_HTTP_VALUE_KEEP_ALIVE;
tsapi const char *TS_HTTP_VALUE_MAX_AGE;
tsapi const char *TS_HTTP_VALUE_MAX_STALE;
tsapi const char *TS_HTTP_VALUE_MIN_FRESH;
tsapi const char *TS_HTTP_VALUE_MUST_REVALIDATE;
tsapi const char *TS_HTTP_VALUE_NONE;
tsapi const char *TS_HTTP_VALUE_NO_CACHE;
tsapi const char *TS_HTTP_VALUE_NO_STORE;
tsapi const char *TS_HTTP_VALUE_NO_TRANSFORM;
tsapi const char *TS_HTTP_VALUE_ONLY_IF_CACHED;
tsapi const char *TS_HTTP_VALUE_PRIVATE;
tsapi const char *TS_HTTP_VALUE_PROXY_REVALIDATE;
tsapi const char *TS_HTTP_VALUE_PUBLIC;
tsapi const char *TS_HTTP_VALUE_S_MAXAGE;

/* HTTP miscellaneous values string lengths */
tsapi int TS_HTTP_LEN_BYTES;
tsapi int TS_HTTP_LEN_CHUNKED;
tsapi int TS_HTTP_LEN_CLOSE;
tsapi int TS_HTTP_LEN_COMPRESS;
tsapi int TS_HTTP_LEN_DEFLATE;
tsapi int TS_HTTP_LEN_GZIP;
tsapi int TS_HTTP_LEN_IDENTITY;
tsapi int TS_HTTP_LEN_KEEP_ALIVE;
tsapi int TS_HTTP_LEN_MAX_AGE;
tsapi int TS_HTTP_LEN_MAX_STALE;
tsapi int TS_HTTP_LEN_MIN_FRESH;
tsapi int TS_HTTP_LEN_MUST_REVALIDATE;
tsapi int TS_HTTP_LEN_NONE;
tsapi int TS_HTTP_LEN_NO_CACHE;
tsapi int TS_HTTP_LEN_NO_STORE;
tsapi int TS_HTTP_LEN_NO_TRANSFORM;
tsapi int TS_HTTP_LEN_ONLY_IF_CACHED;
tsapi int TS_HTTP_LEN_PRIVATE;
tsapi int TS_HTTP_LEN_PROXY_REVALIDATE;
tsapi int TS_HTTP_LEN_PUBLIC;
tsapi int TS_HTTP_LEN_S_MAXAGE;

/* HTTP methods */
tsapi const char *TS_HTTP_METHOD_CONNECT;
tsapi const char *TS_HTTP_METHOD_DELETE;
tsapi const char *TS_HTTP_METHOD_GET;
tsapi const char *TS_HTTP_METHOD_HEAD;
tsapi const char *TS_HTTP_METHOD_ICP_QUERY;
tsapi const char *TS_HTTP_METHOD_OPTIONS;
tsapi const char *TS_HTTP_METHOD_POST;
tsapi const char *TS_HTTP_METHOD_PURGE;
tsapi const char *TS_HTTP_METHOD_PUT;
tsapi const char *TS_HTTP_METHOD_TRACE;

/* HTTP methods string lengths */
tsapi int TS_HTTP_LEN_CONNECT;
tsapi int TS_HTTP_LEN_DELETE;
tsapi int TS_HTTP_LEN_GET;
tsapi int TS_HTTP_LEN_HEAD;
tsapi int TS_HTTP_LEN_ICP_QUERY;
tsapi int TS_HTTP_LEN_OPTIONS;
tsapi int TS_HTTP_LEN_POST;
tsapi int TS_HTTP_LEN_PURGE;
tsapi int TS_HTTP_LEN_PUT;
tsapi int TS_HTTP_LEN_TRACE;

/* MLoc Constants */
tsapi const TSMLoc TS_NULL_MLOC = (TSMLoc) NULL;

HttpAPIHooks *http_global_hooks = NULL;
CacheAPIHooks *cache_global_hooks = NULL;
ConfigUpdateCbTable *global_config_cbs = NULL;

static char traffic_server_version[128] = "";

static ClassAllocator<APIHook> apiHookAllocator("apiHookAllocator");
static ClassAllocator<INKContInternal> INKContAllocator("INKContAllocator");
static ClassAllocator<INKVConnInternal> INKVConnAllocator("INKVConnAllocator");

// Error Ptr.
tsapi const void *TS_ERROR_PTR = (const void *) 0x00000bad;

////////////////////////////////////////////////////////////////////
//
// API error logging
//
////////////////////////////////////////////////////////////////////
void
TSError(const char *fmt, ...)
{
  va_list args;

  if (is_action_tag_set("deft") || is_action_tag_set("sdk_vbos_errors")) {
    va_start(args, fmt);
    diags->print_va(NULL, DL_Error, NULL, NULL, fmt, args);
    va_end(args);
  }
  va_start(args, fmt);
  Log::va_error((char *) fmt, args);
  va_end(args);
}

// Assert in debug AND optim
int
_TSReleaseAssert(const char *text, const char *file, int line)
{
  _ink_assert(text, file, line);
  return (0);
}

// Assert only in debug
int
_TSAssert(const char *text, const char *file, int line)
{
#ifdef DEBUG
  _ink_assert(text, file, line);
#else
  NOWARN_UNUSED(text);
  NOWARN_UNUSED(file);
  NOWARN_UNUSED(line);
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
_hdr_mloc_to_mime_hdr_impl(TSMLoc mloc)
{
  return (_hdr_obj_to_mime_hdr_impl((HdrHeapObjImpl *) mloc));
}

inline TSReturnCode
sdk_sanity_check_field_handle(TSMLoc field, TSMLoc parent_hdr = NULL)
{
#ifdef DEBUG
  if ((field == TS_NULL_MLOC) || (field == TS_ERROR_PTR)) {
    return TS_ERROR;
  }
  MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field;
  if (field_handle->m_type != HDR_HEAP_OBJ_FIELD_SDK_HANDLE) {
    return TS_ERROR;
  }
  if (parent_hdr != NULL) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(parent_hdr);
    if (field_handle->mh != mh) {
      return TS_ERROR;
    }
  }
  return TS_SUCCESS;
#else
  NOWARN_UNUSED(field);
  NOWARN_UNUSED(parent_hdr);
  return TS_SUCCESS;
#endif
}

inline TSReturnCode
sdk_sanity_check_mbuffer(TSMBuffer bufp)
{
#ifdef DEBUG
  HdrHeapSDKHandle *handle = (HdrHeapSDKHandle *) bufp;
  if ((handle == NULL) ||
      (handle == TS_ERROR_PTR) || (handle->m_heap == NULL) || (handle->m_heap->m_magic != HDR_BUF_MAGIC_ALIVE)) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
#else
  NOWARN_UNUSED(bufp);
  return TS_SUCCESS;
#endif
}

TSReturnCode
sdk_sanity_check_mime_hdr_handle(TSMLoc field)
{
#ifdef DEBUG
  if ((field == TS_NULL_MLOC) || (field == TS_ERROR_PTR)) {
    return TS_ERROR;
  }
  MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field;
  if (field_handle->m_type != HDR_HEAP_OBJ_MIME_HEADER) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
#else
  NOWARN_UNUSED(field);
  return TS_SUCCESS;
#endif
}

TSReturnCode
sdk_sanity_check_url_handle(TSMLoc field)
{
#ifdef DEBUG
  if ((field == TS_NULL_MLOC) || (field == TS_ERROR_PTR)) {
    return TS_ERROR;
  }
  MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field;
  if (field_handle->m_type != HDR_HEAP_OBJ_URL) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
#else
  NOWARN_UNUSED(field);
  return TS_SUCCESS;
#endif
}

inline TSReturnCode
sdk_sanity_check_http_hdr_handle(TSMLoc field)
{
#ifdef DEBUG
  if ((field == TS_NULL_MLOC) || (field == TS_ERROR_PTR)) {
    return TS_ERROR;
  }
  HTTPHdrImpl *field_handle = (HTTPHdrImpl *) field;
  if (field_handle->m_type != HDR_HEAP_OBJ_HTTP_HEADER) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
#else
  NOWARN_UNUSED(field);
  return TS_SUCCESS;
#endif
}

inline TSReturnCode
sdk_sanity_check_continuation(TSCont cont)
{
#ifdef DEBUG
  if ((cont != NULL) && (cont != TS_ERROR_PTR) &&
      (((INKContInternal *) cont)->m_free_magic != INKCONT_INTERN_MAGIC_DEAD)) {
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
#else
  NOWARN_UNUSED(cont);
  return TS_SUCCESS;
#endif
}

inline TSReturnCode
sdk_sanity_check_http_ssn(TSHttpSsn ssnp)
{
#ifdef DEBUG
  if ((ssnp != NULL) && (ssnp != TS_ERROR_PTR)) {
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
#else
  NOWARN_UNUSED(ssnp);
  return TS_SUCCESS;
#endif
}

inline TSReturnCode
sdk_sanity_check_txn(TSHttpTxn txnp)
{
#ifdef DEBUG
  if ((txnp != NULL) && (txnp != TS_ERROR_PTR) && (((HttpSM *) txnp)->magic == HTTP_SM_MAGIC_ALIVE)) {
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
#else
  NOWARN_UNUSED(txnp);
  return TS_SUCCESS;
#endif
}

inline TSReturnCode
sdk_sanity_check_mime_parser(TSMimeParser parser)
{
#ifdef DEBUG
  if ((parser != NULL) && (parser != TS_ERROR_PTR)) {
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
#endif
  NOWARN_UNUSED(parser);
  return TS_SUCCESS;
}

inline TSReturnCode
sdk_sanity_check_http_parser(TSHttpParser parser)
{
#ifdef DEBUG
  if ((parser != NULL) && (parser != TS_ERROR_PTR)) {
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
#endif
  NOWARN_UNUSED(parser);
  return TS_SUCCESS;
}

inline TSReturnCode
sdk_sanity_check_alt_info(TSHttpAltInfo info)
{
#ifdef DEBUG
  if ((info != NULL) && (info != TS_ERROR_PTR)) {
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
#endif
  NOWARN_UNUSED(info);
  return TS_SUCCESS;
}

inline TSReturnCode
sdk_sanity_check_hook_id(TSHttpHookID id)
{
#ifdef DEBUG
  if (id<TS_HTTP_READ_REQUEST_HDR_HOOK || id> TS_HTTP_LAST_HOOK)
    return TS_ERROR;
  return TS_SUCCESS;
#else
  NOWARN_UNUSED(id);
  return TS_SUCCESS;
#endif
}


inline TSReturnCode
sdk_sanity_check_null_ptr(void *ptr)
{
#ifdef DEBUG
  if (ptr == NULL)
    return TS_ERROR;
  return TS_SUCCESS;
#else
  NOWARN_UNUSED(ptr);
  return TS_SUCCESS;
#endif
}

/**
  The function checks if the buffer is Modifiable and returns true if
  it is modifiable, else returns false.

*/
bool
isWriteable(TSMBuffer bufp)
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
sdk_alloc_field_handle(TSMBuffer bufp, MIMEHdrImpl *mh)
{
  sdk_sanity_check_mbuffer(bufp);
  HdrHeapSDKHandle *sdk_heap = (HdrHeapSDKHandle *) bufp;

  MIMEFieldSDKHandle *handle = sdk_heap->m_sdk_alloc.allocate_mhandle();
  obj_init_header(handle, HDR_HEAP_OBJ_FIELD_SDK_HANDLE, sizeof(MIMEFieldSDKHandle), 0);
  handle->mh = mh;
  return (handle);
}

static void
sdk_free_field_handle(TSMBuffer bufp, MIMEFieldSDKHandle *field_handle)
{
  sdk_sanity_check_mbuffer(bufp);
  HdrHeapSDKHandle *sdk_heap = (HdrHeapSDKHandle *) bufp;
  field_handle->m_type = HDR_HEAP_OBJ_EMPTY;
  field_handle->mh = NULL;
  field_handle->field_ptr = NULL;

  sdk_heap->m_sdk_alloc.free_mhandle(field_handle);
}

static void
sdk_free_standalone_field(TSMBuffer bufp, MIMEField *sa_field)
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
    m_fd = open(filename, O_RDONLY | _O_ATTRIB_NORMAL);
  } else if (mode[0] == 'w') {
    if (mode[1] != '\0') {
      return 0;
    }
    m_mode = WRITE;
    m_fd = open(filename, O_WRONLY | O_CREAT | _O_ATTRIB_NORMAL, 0644);
  } else if (mode[0] == 'a') {
    if (mode[1] != '\0') {
      return 0;
    }
    m_mode = WRITE;
    m_fd = open(filename, O_WRONLY | O_CREAT | O_APPEND | _O_ATTRIB_NORMAL, 0644);
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

    close(m_fd);
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
      err = read(m_fd, &m_buf[m_bufpos], amount);
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
        err = write(m_fd, p, e - p);
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
 : DummyVConnection(NULL), mdata(NULL), m_event_func(NULL), m_event_count(0), m_closed(1), m_deletable(0), m_deleted(0),
   m_free_magic(INKCONT_INTERN_MAGIC_ALIVE)
{
}

INKContInternal::INKContInternal(TSEventFunc funcp, TSMutex mutexp)
 : DummyVConnection((ProxyMutex *) mutexp),
   mdata(NULL), m_event_func(funcp), m_event_count(0), m_closed(1), m_deletable(0), m_deleted(0),
   m_free_magic(INKCONT_INTERN_MAGIC_ALIVE)
{
  SET_HANDLER(&INKContInternal::handle_event);
}

void
INKContInternal::init(TSEventFunc funcp, TSMutex mutexp)
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
    TSContSchedule(this, 0);
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

#if TS_HAS_V2STATS
void INKContInternal::setName(const char *name) {
  cont_name = name;

  cont_time_stats.resize((int)(TS_HTTP_LAST_HOOK + 1));
  cont_calls.resize((int)(TS_HTTP_LAST_HOOK + 1));

  for(TSHttpHookID cur_hook_id = TS_HTTP_READ_REQUEST_HDR_HOOK; cur_hook_id <= TS_HTTP_LAST_HOOK; cur_hook_id = TSHttpHookID(cur_hook_id+1)) {
    // TODO: Fix the name of these stats to be something appropriate, e.g. proxy.x.y.z or some such (for now at least)
    // TODO: Get rid of std::string (snprintf anyone?)
    std::string stat_base = "cont." + cont_name + "." + HttpDebugNames::get_api_hook_name(cur_hook_id);
    // TODO: This needs to be supported with non-V2 APIs as well.
    cont_time_stats[cur_hook_id].init(stat_base + ".time_spent", 64000);

    StatSystemV2::registerStat((stat_base + ".calls").c_str(), &cont_calls[cur_hook_id]);
  }
  stats_enabled = true;
}

const char *INKContInternal::getName() {
  return cont_name.c_str();
}

void INKContInternal::statCallsMade(TSHttpHookID hook_id) {
  if(cont_name == "")
    return;
  StatSystemV2::increment(cont_calls[hook_id]);
}
#endif

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
    return m_event_func((TSCont) this, (TSEvent) event, edata);
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

INKVConnInternal::INKVConnInternal(TSEventFunc funcp, TSMutex mutexp)
:INKContInternal(funcp, mutexp), m_read_vio(), m_write_vio(), m_output_vc(NULL)
{
  m_closed = 0;
  SET_HANDLER(&INKVConnInternal::handle_event);
}

void
INKVConnInternal::init(TSEventFunc funcp, TSMutex mutexp)
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
    return m_event_func((TSCont) this, (TSEvent) event, edata);
  }
  return EVENT_DONE;
}

VIO *
INKVConnInternal::do_io_read(Continuation *c, int64 nbytes, MIOBuffer *buf)
{
  m_read_vio.buffer.writer_for(buf);
  m_read_vio.op = VIO::READ;
  m_read_vio.set_continuation(c);
  m_read_vio.nbytes = nbytes;
  m_read_vio.ndone = 0;
  m_read_vio.vc_server = this;

  if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  eventProcessor.schedule_imm(this, ET_NET);

  return &m_read_vio;
}

VIO *
INKVConnInternal::do_io_write(Continuation *c, int64 nbytes, IOBufferReader *buf, bool owner)
{
  ink_assert(!owner);
  m_write_vio.buffer.reader_for(buf);
  m_write_vio.op = VIO::WRITE;
  m_write_vio.set_continuation(c);
  m_write_vio.nbytes = nbytes;
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
INKVConnInternal::do_io_transform(VConnection *vc)
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
    m_closed = TS_VC_CLOSE_ABORT;
  } else {
    m_closed = TS_VC_CLOSE_NORMAL;
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
INKVConnInternal::reenable(VIO *vio)
{
  NOWARN_UNUSED(vio);
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
  case TS_API_DATA_READ_VIO:
    *((TSVIO *) data) = &m_read_vio;
    return true;
  case TS_API_DATA_WRITE_VIO:
    *((TSVIO *) data) = &m_write_vio;
    return true;
  case TS_API_DATA_OUTPUT_VC:
    *((TSVConn *) data) = m_output_vc;
    return true;
  case TS_API_DATA_CLOSED:
    *((int *) data) = m_closed;
    return true;
  default:
    return INKContInternal::get_data(id, data);
  }
}

bool INKVConnInternal::set_data(int id, void *data)
{
  switch (id) {
  case TS_API_DATA_OUTPUT_VC:
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
APIHooks::prepend(INKContInternal *cont)
{
  APIHook *api_hook;

  api_hook = apiHookAllocator.alloc();
  api_hook->m_cont = cont;

  m_hooks.push(api_hook);
}

void
APIHooks::append(INKContInternal *cont)
{
  APIHook *api_hook;

  api_hook = apiHookAllocator.alloc();
  api_hook->m_cont = cont;

  m_hooks.enqueue(api_hook);
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

  for (i = 0; i < TS_HTTP_LAST_HOOK; i++) {
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
HttpAPIHooks::prepend(TSHttpHookID id, INKContInternal *cont)
{
  hooks_set = 1;
  m_hooks[id].prepend(cont);
}

void
HttpAPIHooks::append(TSHttpHookID id, INKContInternal *cont)
{
  hooks_set = 1;
  m_hooks[id].append(cont);
}

APIHook *
HttpAPIHooks::get(TSHttpHookID id)
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

  for (i = 0; i < TS_CACHE_LAST_HOOK; i++) {
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
CacheAPIHooks::append(TSCacheHookID id, INKContInternal *cont)
{
  hooks_set = 1;
  m_hooks[id].append(cont);
}

APIHook *
CacheAPIHooks::get(TSCacheHookID id)
{
  return m_hooks[id].get();
}

void
CacheAPIHooks::prepend(TSCacheHookID id, INKContInternal *cont)
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
ConfigUpdateCbTable::insert(INKContInternal *contp, const char *name, const char *config_path)
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
ConfigUpdateCbTable::invoke(INKContInternal *contp)
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
    TS_URL_SCHEME_FILE = URL_SCHEME_FILE;
    TS_URL_SCHEME_FTP = URL_SCHEME_FTP;
    TS_URL_SCHEME_GOPHER = URL_SCHEME_GOPHER;
    TS_URL_SCHEME_HTTP = URL_SCHEME_HTTP;
    TS_URL_SCHEME_HTTPS = URL_SCHEME_HTTPS;
    TS_URL_SCHEME_MAILTO = URL_SCHEME_MAILTO;
    TS_URL_SCHEME_NEWS = URL_SCHEME_NEWS;
    TS_URL_SCHEME_NNTP = URL_SCHEME_NNTP;
    TS_URL_SCHEME_PROSPERO = URL_SCHEME_PROSPERO;
    TS_URL_SCHEME_TELNET = URL_SCHEME_TELNET;
    TS_URL_SCHEME_WAIS = URL_SCHEME_WAIS;

    TS_URL_LEN_FILE = URL_LEN_FILE;
    TS_URL_LEN_FTP = URL_LEN_FTP;
    TS_URL_LEN_GOPHER = URL_LEN_GOPHER;
    TS_URL_LEN_HTTP = URL_LEN_HTTP;
    TS_URL_LEN_HTTPS = URL_LEN_HTTPS;
    TS_URL_LEN_MAILTO = URL_LEN_MAILTO;
    TS_URL_LEN_NEWS = URL_LEN_NEWS;
    TS_URL_LEN_NNTP = URL_LEN_NNTP;
    TS_URL_LEN_PROSPERO = URL_LEN_PROSPERO;
    TS_URL_LEN_TELNET = URL_LEN_TELNET;
    TS_URL_LEN_WAIS = URL_LEN_WAIS;

    /* MIME fields */
    TS_MIME_FIELD_ACCEPT = MIME_FIELD_ACCEPT;
    TS_MIME_FIELD_ACCEPT_CHARSET = MIME_FIELD_ACCEPT_CHARSET;
    TS_MIME_FIELD_ACCEPT_ENCODING = MIME_FIELD_ACCEPT_ENCODING;
    TS_MIME_FIELD_ACCEPT_LANGUAGE = MIME_FIELD_ACCEPT_LANGUAGE;
    TS_MIME_FIELD_ACCEPT_RANGES = MIME_FIELD_ACCEPT_RANGES;
    TS_MIME_FIELD_AGE = MIME_FIELD_AGE;
    TS_MIME_FIELD_ALLOW = MIME_FIELD_ALLOW;
    TS_MIME_FIELD_APPROVED = MIME_FIELD_APPROVED;
    TS_MIME_FIELD_AUTHORIZATION = MIME_FIELD_AUTHORIZATION;
    TS_MIME_FIELD_BYTES = MIME_FIELD_BYTES;
    TS_MIME_FIELD_CACHE_CONTROL = MIME_FIELD_CACHE_CONTROL;
    TS_MIME_FIELD_CLIENT_IP = MIME_FIELD_CLIENT_IP;
    TS_MIME_FIELD_CONNECTION = MIME_FIELD_CONNECTION;
    TS_MIME_FIELD_CONTENT_BASE = MIME_FIELD_CONTENT_BASE;
    TS_MIME_FIELD_CONTENT_ENCODING = MIME_FIELD_CONTENT_ENCODING;
    TS_MIME_FIELD_CONTENT_LANGUAGE = MIME_FIELD_CONTENT_LANGUAGE;
    TS_MIME_FIELD_CONTENT_LENGTH = MIME_FIELD_CONTENT_LENGTH;
    TS_MIME_FIELD_CONTENT_LOCATION = MIME_FIELD_CONTENT_LOCATION;
    TS_MIME_FIELD_CONTENT_MD5 = MIME_FIELD_CONTENT_MD5;
    TS_MIME_FIELD_CONTENT_RANGE = MIME_FIELD_CONTENT_RANGE;
    TS_MIME_FIELD_CONTENT_TYPE = MIME_FIELD_CONTENT_TYPE;
    TS_MIME_FIELD_CONTROL = MIME_FIELD_CONTROL;
    TS_MIME_FIELD_COOKIE = MIME_FIELD_COOKIE;
    TS_MIME_FIELD_DATE = MIME_FIELD_DATE;
    TS_MIME_FIELD_DISTRIBUTION = MIME_FIELD_DISTRIBUTION;
    TS_MIME_FIELD_ETAG = MIME_FIELD_ETAG;
    TS_MIME_FIELD_EXPECT = MIME_FIELD_EXPECT;
    TS_MIME_FIELD_EXPIRES = MIME_FIELD_EXPIRES;
    TS_MIME_FIELD_FOLLOWUP_TO = MIME_FIELD_FOLLOWUP_TO;
    TS_MIME_FIELD_FROM = MIME_FIELD_FROM;
    TS_MIME_FIELD_HOST = MIME_FIELD_HOST;
    TS_MIME_FIELD_IF_MATCH = MIME_FIELD_IF_MATCH;
    TS_MIME_FIELD_IF_MODIFIED_SINCE = MIME_FIELD_IF_MODIFIED_SINCE;
    TS_MIME_FIELD_IF_NONE_MATCH = MIME_FIELD_IF_NONE_MATCH;
    TS_MIME_FIELD_IF_RANGE = MIME_FIELD_IF_RANGE;
    TS_MIME_FIELD_IF_UNMODIFIED_SINCE = MIME_FIELD_IF_UNMODIFIED_SINCE;
    TS_MIME_FIELD_KEEP_ALIVE = MIME_FIELD_KEEP_ALIVE;
    TS_MIME_FIELD_KEYWORDS = MIME_FIELD_KEYWORDS;
    TS_MIME_FIELD_LAST_MODIFIED = MIME_FIELD_LAST_MODIFIED;
    TS_MIME_FIELD_LINES = MIME_FIELD_LINES;
    TS_MIME_FIELD_LOCATION = MIME_FIELD_LOCATION;
    TS_MIME_FIELD_MAX_FORWARDS = MIME_FIELD_MAX_FORWARDS;
    TS_MIME_FIELD_MESSAGE_ID = MIME_FIELD_MESSAGE_ID;
    TS_MIME_FIELD_NEWSGROUPS = MIME_FIELD_NEWSGROUPS;
    TS_MIME_FIELD_ORGANIZATION = MIME_FIELD_ORGANIZATION;
    TS_MIME_FIELD_PATH = MIME_FIELD_PATH;
    TS_MIME_FIELD_PRAGMA = MIME_FIELD_PRAGMA;
    TS_MIME_FIELD_PROXY_AUTHENTICATE = MIME_FIELD_PROXY_AUTHENTICATE;
    TS_MIME_FIELD_PROXY_AUTHORIZATION = MIME_FIELD_PROXY_AUTHORIZATION;
    TS_MIME_FIELD_PROXY_CONNECTION = MIME_FIELD_PROXY_CONNECTION;
    TS_MIME_FIELD_PUBLIC = MIME_FIELD_PUBLIC;
    TS_MIME_FIELD_RANGE = MIME_FIELD_RANGE;
    TS_MIME_FIELD_REFERENCES = MIME_FIELD_REFERENCES;
    TS_MIME_FIELD_REFERER = MIME_FIELD_REFERER;
    TS_MIME_FIELD_REPLY_TO = MIME_FIELD_REPLY_TO;
    TS_MIME_FIELD_RETRY_AFTER = MIME_FIELD_RETRY_AFTER;
    TS_MIME_FIELD_SENDER = MIME_FIELD_SENDER;
    TS_MIME_FIELD_SERVER = MIME_FIELD_SERVER;
    TS_MIME_FIELD_SET_COOKIE = MIME_FIELD_SET_COOKIE;
    TS_MIME_FIELD_SUBJECT = MIME_FIELD_SUBJECT;
    TS_MIME_FIELD_SUMMARY = MIME_FIELD_SUMMARY;
    TS_MIME_FIELD_TE = MIME_FIELD_TE;
    TS_MIME_FIELD_TRANSFER_ENCODING = MIME_FIELD_TRANSFER_ENCODING;
    TS_MIME_FIELD_UPGRADE = MIME_FIELD_UPGRADE;
    TS_MIME_FIELD_USER_AGENT = MIME_FIELD_USER_AGENT;
    TS_MIME_FIELD_VARY = MIME_FIELD_VARY;
    TS_MIME_FIELD_VIA = MIME_FIELD_VIA;
    TS_MIME_FIELD_WARNING = MIME_FIELD_WARNING;
    TS_MIME_FIELD_WWW_AUTHENTICATE = MIME_FIELD_WWW_AUTHENTICATE;
    TS_MIME_FIELD_XREF = MIME_FIELD_XREF;
    TS_MIME_FIELD_X_FORWARDED_FOR = MIME_FIELD_X_FORWARDED_FOR;


    TS_MIME_LEN_ACCEPT = MIME_LEN_ACCEPT;
    TS_MIME_LEN_ACCEPT_CHARSET = MIME_LEN_ACCEPT_CHARSET;
    TS_MIME_LEN_ACCEPT_ENCODING = MIME_LEN_ACCEPT_ENCODING;
    TS_MIME_LEN_ACCEPT_LANGUAGE = MIME_LEN_ACCEPT_LANGUAGE;
    TS_MIME_LEN_ACCEPT_RANGES = MIME_LEN_ACCEPT_RANGES;
    TS_MIME_LEN_AGE = MIME_LEN_AGE;
    TS_MIME_LEN_ALLOW = MIME_LEN_ALLOW;
    TS_MIME_LEN_APPROVED = MIME_LEN_APPROVED;
    TS_MIME_LEN_AUTHORIZATION = MIME_LEN_AUTHORIZATION;
    TS_MIME_LEN_BYTES = MIME_LEN_BYTES;
    TS_MIME_LEN_CACHE_CONTROL = MIME_LEN_CACHE_CONTROL;
    TS_MIME_LEN_CLIENT_IP = MIME_LEN_CLIENT_IP;
    TS_MIME_LEN_CONNECTION = MIME_LEN_CONNECTION;
    TS_MIME_LEN_CONTENT_BASE = MIME_LEN_CONTENT_BASE;
    TS_MIME_LEN_CONTENT_ENCODING = MIME_LEN_CONTENT_ENCODING;
    TS_MIME_LEN_CONTENT_LANGUAGE = MIME_LEN_CONTENT_LANGUAGE;
    TS_MIME_LEN_CONTENT_LENGTH = MIME_LEN_CONTENT_LENGTH;
    TS_MIME_LEN_CONTENT_LOCATION = MIME_LEN_CONTENT_LOCATION;
    TS_MIME_LEN_CONTENT_MD5 = MIME_LEN_CONTENT_MD5;
    TS_MIME_LEN_CONTENT_RANGE = MIME_LEN_CONTENT_RANGE;
    TS_MIME_LEN_CONTENT_TYPE = MIME_LEN_CONTENT_TYPE;
    TS_MIME_LEN_CONTROL = MIME_LEN_CONTROL;
    TS_MIME_LEN_COOKIE = MIME_LEN_COOKIE;
    TS_MIME_LEN_DATE = MIME_LEN_DATE;
    TS_MIME_LEN_DISTRIBUTION = MIME_LEN_DISTRIBUTION;
    TS_MIME_LEN_ETAG = MIME_LEN_ETAG;
    TS_MIME_LEN_EXPECT = MIME_LEN_EXPECT;
    TS_MIME_LEN_EXPIRES = MIME_LEN_EXPIRES;
    TS_MIME_LEN_FOLLOWUP_TO = MIME_LEN_FOLLOWUP_TO;
    TS_MIME_LEN_FROM = MIME_LEN_FROM;
    TS_MIME_LEN_HOST = MIME_LEN_HOST;
    TS_MIME_LEN_IF_MATCH = MIME_LEN_IF_MATCH;
    TS_MIME_LEN_IF_MODIFIED_SINCE = MIME_LEN_IF_MODIFIED_SINCE;
    TS_MIME_LEN_IF_NONE_MATCH = MIME_LEN_IF_NONE_MATCH;
    TS_MIME_LEN_IF_RANGE = MIME_LEN_IF_RANGE;
    TS_MIME_LEN_IF_UNMODIFIED_SINCE = MIME_LEN_IF_UNMODIFIED_SINCE;
    TS_MIME_LEN_KEEP_ALIVE = MIME_LEN_KEEP_ALIVE;
    TS_MIME_LEN_KEYWORDS = MIME_LEN_KEYWORDS;
    TS_MIME_LEN_LAST_MODIFIED = MIME_LEN_LAST_MODIFIED;
    TS_MIME_LEN_LINES = MIME_LEN_LINES;
    TS_MIME_LEN_LOCATION = MIME_LEN_LOCATION;
    TS_MIME_LEN_MAX_FORWARDS = MIME_LEN_MAX_FORWARDS;
    TS_MIME_LEN_MESSAGE_ID = MIME_LEN_MESSAGE_ID;
    TS_MIME_LEN_NEWSGROUPS = MIME_LEN_NEWSGROUPS;
    TS_MIME_LEN_ORGANIZATION = MIME_LEN_ORGANIZATION;
    TS_MIME_LEN_PATH = MIME_LEN_PATH;
    TS_MIME_LEN_PRAGMA = MIME_LEN_PRAGMA;
    TS_MIME_LEN_PROXY_AUTHENTICATE = MIME_LEN_PROXY_AUTHENTICATE;
    TS_MIME_LEN_PROXY_AUTHORIZATION = MIME_LEN_PROXY_AUTHORIZATION;
    TS_MIME_LEN_PROXY_CONNECTION = MIME_LEN_PROXY_CONNECTION;
    TS_MIME_LEN_PUBLIC = MIME_LEN_PUBLIC;
    TS_MIME_LEN_RANGE = MIME_LEN_RANGE;
    TS_MIME_LEN_REFERENCES = MIME_LEN_REFERENCES;
    TS_MIME_LEN_REFERER = MIME_LEN_REFERER;
    TS_MIME_LEN_REPLY_TO = MIME_LEN_REPLY_TO;
    TS_MIME_LEN_RETRY_AFTER = MIME_LEN_RETRY_AFTER;
    TS_MIME_LEN_SENDER = MIME_LEN_SENDER;
    TS_MIME_LEN_SERVER = MIME_LEN_SERVER;
    TS_MIME_LEN_SET_COOKIE = MIME_LEN_SET_COOKIE;
    TS_MIME_LEN_SUBJECT = MIME_LEN_SUBJECT;
    TS_MIME_LEN_SUMMARY = MIME_LEN_SUMMARY;
    TS_MIME_LEN_TE = MIME_LEN_TE;
    TS_MIME_LEN_TRANSFER_ENCODING = MIME_LEN_TRANSFER_ENCODING;
    TS_MIME_LEN_UPGRADE = MIME_LEN_UPGRADE;
    TS_MIME_LEN_USER_AGENT = MIME_LEN_USER_AGENT;
    TS_MIME_LEN_VARY = MIME_LEN_VARY;
    TS_MIME_LEN_VIA = MIME_LEN_VIA;
    TS_MIME_LEN_WARNING = MIME_LEN_WARNING;
    TS_MIME_LEN_WWW_AUTHENTICATE = MIME_LEN_WWW_AUTHENTICATE;
    TS_MIME_LEN_XREF = MIME_LEN_XREF;
    TS_MIME_LEN_X_FORWARDED_FOR = MIME_LEN_X_FORWARDED_FOR;


    /* HTTP methods */
    TS_HTTP_METHOD_CONNECT = HTTP_METHOD_CONNECT;
    TS_HTTP_METHOD_DELETE = HTTP_METHOD_DELETE;
    TS_HTTP_METHOD_GET = HTTP_METHOD_GET;
    TS_HTTP_METHOD_HEAD = HTTP_METHOD_HEAD;
    TS_HTTP_METHOD_ICP_QUERY = HTTP_METHOD_ICP_QUERY;
    TS_HTTP_METHOD_OPTIONS = HTTP_METHOD_OPTIONS;
    TS_HTTP_METHOD_POST = HTTP_METHOD_POST;
    TS_HTTP_METHOD_PURGE = HTTP_METHOD_PURGE;
    TS_HTTP_METHOD_PUT = HTTP_METHOD_PUT;
    TS_HTTP_METHOD_TRACE = HTTP_METHOD_TRACE;

    TS_HTTP_LEN_CONNECT = HTTP_LEN_CONNECT;
    TS_HTTP_LEN_DELETE = HTTP_LEN_DELETE;
    TS_HTTP_LEN_GET = HTTP_LEN_GET;
    TS_HTTP_LEN_HEAD = HTTP_LEN_HEAD;
    TS_HTTP_LEN_ICP_QUERY = HTTP_LEN_ICP_QUERY;
    TS_HTTP_LEN_OPTIONS = HTTP_LEN_OPTIONS;
    TS_HTTP_LEN_POST = HTTP_LEN_POST;
    TS_HTTP_LEN_PURGE = HTTP_LEN_PURGE;
    TS_HTTP_LEN_PUT = HTTP_LEN_PUT;
    TS_HTTP_LEN_TRACE = HTTP_LEN_TRACE;

    /* HTTP miscellaneous values */
    TS_HTTP_VALUE_BYTES = HTTP_VALUE_BYTES;
    TS_HTTP_VALUE_CHUNKED = HTTP_VALUE_CHUNKED;
    TS_HTTP_VALUE_CLOSE = HTTP_VALUE_CLOSE;
    TS_HTTP_VALUE_COMPRESS = HTTP_VALUE_COMPRESS;
    TS_HTTP_VALUE_DEFLATE = HTTP_VALUE_DEFLATE;
    TS_HTTP_VALUE_GZIP = HTTP_VALUE_GZIP;
    TS_HTTP_VALUE_IDENTITY = HTTP_VALUE_IDENTITY;
    TS_HTTP_VALUE_KEEP_ALIVE = HTTP_VALUE_KEEP_ALIVE;
    TS_HTTP_VALUE_MAX_AGE = HTTP_VALUE_MAX_AGE;
    TS_HTTP_VALUE_MAX_STALE = HTTP_VALUE_MAX_STALE;
    TS_HTTP_VALUE_MIN_FRESH = HTTP_VALUE_MIN_FRESH;
    TS_HTTP_VALUE_MUST_REVALIDATE = HTTP_VALUE_MUST_REVALIDATE;
    TS_HTTP_VALUE_NONE = HTTP_VALUE_NONE;
    TS_HTTP_VALUE_NO_CACHE = HTTP_VALUE_NO_CACHE;
    TS_HTTP_VALUE_NO_STORE = HTTP_VALUE_NO_STORE;
    TS_HTTP_VALUE_NO_TRANSFORM = HTTP_VALUE_NO_TRANSFORM;
    TS_HTTP_VALUE_ONLY_IF_CACHED = HTTP_VALUE_ONLY_IF_CACHED;
    TS_HTTP_VALUE_PRIVATE = HTTP_VALUE_PRIVATE;
    TS_HTTP_VALUE_PROXY_REVALIDATE = HTTP_VALUE_PROXY_REVALIDATE;
    TS_HTTP_VALUE_PUBLIC = HTTP_VALUE_PUBLIC;
    TS_HTTP_VALUE_S_MAXAGE = HTTP_VALUE_S_MAXAGE;

    TS_HTTP_LEN_BYTES = HTTP_LEN_BYTES;
    TS_HTTP_LEN_CHUNKED = HTTP_LEN_CHUNKED;
    TS_HTTP_LEN_CLOSE = HTTP_LEN_CLOSE;
    TS_HTTP_LEN_COMPRESS = HTTP_LEN_COMPRESS;
    TS_HTTP_LEN_DEFLATE = HTTP_LEN_DEFLATE;
    TS_HTTP_LEN_GZIP = HTTP_LEN_GZIP;
    TS_HTTP_LEN_IDENTITY = HTTP_LEN_IDENTITY;
    TS_HTTP_LEN_KEEP_ALIVE = HTTP_LEN_KEEP_ALIVE;
    TS_HTTP_LEN_MAX_AGE = HTTP_LEN_MAX_AGE;
    TS_HTTP_LEN_MAX_STALE = HTTP_LEN_MAX_STALE;
    TS_HTTP_LEN_MIN_FRESH = HTTP_LEN_MIN_FRESH;
    TS_HTTP_LEN_MUST_REVALIDATE = HTTP_LEN_MUST_REVALIDATE;
    TS_HTTP_LEN_NONE = HTTP_LEN_NONE;
    TS_HTTP_LEN_NO_CACHE = HTTP_LEN_NO_CACHE;
    TS_HTTP_LEN_NO_STORE = HTTP_LEN_NO_STORE;
    TS_HTTP_LEN_NO_TRANSFORM = HTTP_LEN_NO_TRANSFORM;
    TS_HTTP_LEN_ONLY_IF_CACHED = HTTP_LEN_ONLY_IF_CACHED;
    TS_HTTP_LEN_PRIVATE = HTTP_LEN_PRIVATE;
    TS_HTTP_LEN_PROXY_REVALIDATE = HTTP_LEN_PROXY_REVALIDATE;
    TS_HTTP_LEN_PUBLIC = HTTP_LEN_PUBLIC;
    TS_HTTP_LEN_S_MAXAGE = HTTP_LEN_S_MAXAGE;

    http_global_hooks = NEW(new HttpAPIHooks);
    cache_global_hooks = NEW(new CacheAPIHooks);
    global_config_cbs = NEW(new ConfigUpdateCbTable);

    if (TS_MAX_API_STATS > 0) {
      api_rsb = RecAllocateRawStatBlock(TS_MAX_API_STATS);
      if (NULL == api_rsb) {
        Warning("Can't allocate API stats block");
      } else {
        Debug("sdk", "initialized SDK stats APIs with %d slots", TS_MAX_API_STATS);
      }
    } else {
      api_rsb = NULL;
    }

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
_TSmalloc(size_t size, const char *path)
{
  return _xmalloc(size, path);
}

void *
_TSrealloc(void *ptr, size_t size, const char *path)
{
  return _xrealloc(ptr, size, path);
}

// length has to be int64 and not size_t, since -1 means to call strlen() to get length
char *
_TSstrdup(const char *str, int64 length, const char *path)
{
  return _xstrdup(str, length, path);
}

void
_TSfree(void *ptr)
{
  _xfree(ptr);
}

////////////////////////////////////////////////////////////////////
//
// API utility routines
//
////////////////////////////////////////////////////////////////////

unsigned int
TSrandom()
{
  return this_ethread()->generator.random();
}

double
TSdrandom()
{
  return this_ethread()->generator.drandom();
}

ink_hrtime
TShrtime()
{
  return ink_get_based_hrtime();
}

////////////////////////////////////////////////////////////////////
//
// API install and plugin locations
//
////////////////////////////////////////////////////////////////////

const char *
TSInstallDirGet(void)
{
  return system_root_dir;
}

const char *
TSConfigDirGet(void)
{
  return system_config_directory;
}

const char *
TSTrafficServerVersionGet(void)
{
  return traffic_server_version;
}

const char *
TSPluginDirGet(void)
{
  static char path[PATH_NAME_MAX + 1] = "";

  if (*path == '\0') {
    char *plugin_dir = NULL;
    RecGetRecordString_Xmalloc("proxy.config.plugin.plugin_dir", &plugin_dir);
    if (!plugin_dir) {
      Error("Unable to read proxy.config.plugin.plugin_dir");
      return NULL;
    }
    Layout::relative_to(path, sizeof(path),
                        Layout::get()->prefix, plugin_dir);
    xfree(plugin_dir);
  }

  return path;
}

////////////////////////////////////////////////////////////////////
//
// Plugin registration
//
////////////////////////////////////////////////////////////////////

int
TSPluginRegister(TSSDKVersion sdk_version, TSPluginRegistrationInfo *plugin_info)
{

  ink_assert(plugin_reg_current != NULL);
  if (!plugin_reg_current)
    return 0;

  if (sdk_sanity_check_null_ptr((void *) plugin_info) != TS_SUCCESS) {
    return 0;
  }

  plugin_reg_current->plugin_registered = true;

  if (sdk_version >= TS_SDK_VERSION_2_0 && sdk_version <= TS_SDK_VERSION_2_0) {
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
// Plugin info registration - coded in 2.0, but not documented
//
////////////////////////////////////////////////////////////////////

TSReturnCode
TSPluginInfoRegister(TSPluginRegistrationInfo *plugin_info)
{
  if (sdk_sanity_check_null_ptr((void *) plugin_info) == TS_ERROR) {
    return TS_ERROR;
  }

  ink_assert(plugin_reg_current != NULL);
  if (!plugin_reg_current)
    return TS_ERROR;

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

  return TS_SUCCESS;
}

////////////////////////////////////////////////////////////////////
//
// API file management
//
////////////////////////////////////////////////////////////////////

TSFile
TSfopen(const char *filename, const char *mode)
{
  FileImpl *file;

  file = NEW(new FileImpl);
  if (!file->fopen(filename, mode)) {
    delete file;
    return NULL;
  }

  return (TSFile) file;
}

void
TSfclose(TSFile filep)
{
  FileImpl *file = (FileImpl *) filep;
  file->fclose();
  delete file;
}

size_t
TSfread(TSFile filep, void *buf, size_t length)
{
  FileImpl *file = (FileImpl *) filep;
  return file->fread(buf, length);
}

size_t
TSfwrite(TSFile filep, const void *buf, size_t length)
{
  FileImpl *file = (FileImpl *) filep;
  return file->fwrite(buf, length);
}

void
TSfflush(TSFile filep)
{
  FileImpl *file = (FileImpl *) filep;
  file->fflush();
}

char *
TSfgets(TSFile filep, char *buf, size_t length)
{
  FileImpl *file = (FileImpl *) filep;
  return file->fgets(buf, length);
}

////////////////////////////////////////////////////////////////////
//
// Header component object handles
//
////////////////////////////////////////////////////////////////////

TSReturnCode
TSHandleMLocRelease(TSMBuffer bufp, TSMLoc parent, TSMLoc mloc)
{
  MIMEFieldSDKHandle *field_handle;
  HdrHeapObjImpl *obj = (HdrHeapObjImpl *) mloc;

  if (mloc == TS_NULL_MLOC)
    return (TS_SUCCESS);

  if (sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) {
    return TS_ERROR;
  }

  switch (obj->m_type) {
  case HDR_HEAP_OBJ_URL:
  case HDR_HEAP_OBJ_HTTP_HEADER:
  case HDR_HEAP_OBJ_MIME_HEADER:
    return (TS_SUCCESS);

  case HDR_HEAP_OBJ_FIELD_SDK_HANDLE:
    field_handle = (MIMEFieldSDKHandle *) obj;
    if (sdk_sanity_check_field_handle(mloc, parent) != TS_SUCCESS) {
      return TS_ERROR;
    }
    sdk_free_field_handle(bufp, field_handle);
    return (TS_SUCCESS);

  default:
    ink_release_assert(!"invalid mloc");
    return (TS_ERROR);
  }
}

TSReturnCode
TSHandleStringRelease(TSMBuffer bufp, TSMLoc parent, const char *str)
{
  NOWARN_UNUSED(parent);
  if (str == NULL)
    return (TS_SUCCESS);
  if (bufp == NULL)
    return (TS_ERROR);

  if (hdrtoken_is_wks(str))
    return (TS_SUCCESS);

  HdrHeapSDKHandle *sdk_h = (HdrHeapSDKHandle *) bufp;
  int r = sdk_h->destroy_sdk_string((char *) str);

  return ((r == 0) ? TS_ERROR : TS_SUCCESS);
}

////////////////////////////////////////////////////////////////////
//
// HdrHeaps (previously known as "Marshal Buffers")
//
////////////////////////////////////////////////////////////////////

// TSMBuffer: pointers to HdrHeapSDKHandle objects

TSMBuffer
TSMBufferCreate()
{
  TSMBuffer bufp;
  HdrHeapSDKHandle *new_heap = NEW(new HdrHeapSDKHandle);
  new_heap->m_heap = new_HdrHeap();
  bufp = (TSMBuffer) new_heap;
  if (sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) {
    delete new_heap;
    return (TSMBuffer) TS_ERROR_PTR;
  }
  return (bufp);
}

TSReturnCode
TSMBufferDestroy(TSMBuffer bufp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if (isWriteable(bufp)) {
    sdk_sanity_check_mbuffer(bufp);
    HdrHeapSDKHandle *sdk_heap = (HdrHeapSDKHandle *) bufp;
    sdk_heap->m_heap->destroy();
    delete sdk_heap;
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

////////////////////////////////////////////////////////////////////
//
// URLs
//
////////////////////////////////////////////////////////////////////

// TSMBuffer: pointers to HdrHeapSDKHandle objects
// TSMLoc:    pointers to URLImpl objects

TSMLoc
TSUrlCreate(TSMBuffer bufp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, return TS_ERROR_PTR.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) && isWriteable(bufp)) {
    HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    return ((TSMLoc) (url_create(heap)));
  } else {
    return (TSMLoc) TS_ERROR_PTR;
  }
}

TSReturnCode
TSUrlDestroy(TSMBuffer bufp, TSMLoc url_loc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_url_handle(url_loc) == TS_SUCCESS) && isWriteable(bufp)) {
    // No more objects counts in heap or deallocation so do nothing!
    // FIX ME - Did this free the MBuffer in Pete's old system?
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSMLoc
TSUrlClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_url)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, return NULL.
  if ((sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_url_handle(src_url) == TS_SUCCESS) && isWriteable(dest_bufp)) {

    HdrHeap *s_heap, *d_heap;
    URLImpl *s_url, *d_url;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_url = (URLImpl *) src_url;

    d_url = url_copy(s_url, s_heap, d_heap, (s_heap != d_heap));
    return ((TSMLoc) d_url);
  } else {
    return (TSMLoc) TS_ERROR_PTR;
  }
}

TSReturnCode
TSUrlCopy(TSMBuffer dest_bufp, TSMLoc dest_obj, TSMBuffer src_bufp, TSMLoc src_obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_url_handle(src_obj) == TS_SUCCESS) &&
      (sdk_sanity_check_url_handle(dest_obj) == TS_SUCCESS) && isWriteable(dest_bufp)) {

    HdrHeap *s_heap, *d_heap;
    URLImpl *s_url, *d_url;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_url = (URLImpl *) src_obj;
    d_url = (URLImpl *) dest_obj;

    url_copy_onto(s_url, s_heap, d_url, d_heap, (s_heap != d_heap));
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSUrlPrint(TSMBuffer bufp, TSMLoc obj, TSIOBuffer iobufp)
{
  MIOBuffer *b = (MIOBuffer *) iobufp;
  IOBufferBlock *blk;
  int bufindex;
  int tmp, dumpoffset;
  int done;

  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      (sdk_sanity_check_url_handle(obj) != TS_SUCCESS) || (sdk_sanity_check_iocore_structure(iobufp) != TS_SUCCESS)) {
    return TS_ERROR;
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
  return TS_SUCCESS;
}

int
TSUrlParse(TSMBuffer bufp, TSMLoc obj, const char **start, const char *end)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      (sdk_sanity_check_url_handle(obj) != TS_SUCCESS) ||
      (start == NULL) || (*start == NULL) ||
      sdk_sanity_check_null_ptr((void *) end) != TS_SUCCESS || (!isWriteable(bufp))) {
    return TS_PARSE_ERROR;
  }

  URL u;
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;
  url_clear(u.m_url_impl);
  return u.parse(start, end);
}

int
TSUrlLengthGet(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_url_handle(obj) != TS_SUCCESS)) {
    return TS_ERROR;
  }
  URLImpl *url_impl = (URLImpl *) obj;
  return (url_length_get(url_impl));
}

char *
TSUrlStringGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_url_handle(obj) != TS_SUCCESS)) {
    return (char *) TS_ERROR_PTR;
  }
  URLImpl *url_impl = (URLImpl *) obj;
  return (url_string_get(url_impl, NULL, length, NULL));

}

typedef const char *(URL::*URLPartGetF) (int *length);
typedef void (URL::*URLPartSetF) (const char *value, int length);

static const char *
URLPartGet(TSMBuffer bufp, TSMLoc obj, int *length, URLPartGetF url_f)
{

  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      (sdk_sanity_check_url_handle(obj) != TS_SUCCESS) || (length == NULL)) {
    return (const char *) TS_ERROR_PTR;
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

static TSReturnCode
URLPartSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length, URLPartSetF url_f)
{

  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      (sdk_sanity_check_url_handle(obj) != TS_SUCCESS) ||
      (sdk_sanity_check_null_ptr((void *) value) != TS_SUCCESS) || (!isWriteable(bufp))) {
    return TS_ERROR;
  }

  URL u;
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;

  if (length < 0)
    length = strlen(value);

  (u.*url_f) (value, length);
  return TS_SUCCESS;
}

const char *
TSUrlSchemeGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::scheme_get);
}

TSReturnCode
TSUrlSchemeSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::scheme_set);
  }

  return TS_ERROR;
}

/* Internet specific URLs */

const char *
TSUrlUserGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::user_get);
}

TSReturnCode
TSUrlUserSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  return URLPartSet(bufp, obj, value, length, &URL::user_set);
}

const char *
TSUrlPasswordGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::password_get);
}

TSReturnCode
TSUrlPasswordSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::password_set);
  }

  return TS_ERROR;

}

const char *
TSUrlHostGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::host_get);
}

TSReturnCode
TSUrlHostSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::host_set);
  }

  return TS_ERROR;

}

int
TSUrlPortGet(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_url_handle(obj) != TS_SUCCESS)) {
    return TS_ERROR;
  }
  URL u;
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;

  return u.port_get();
}

TSReturnCode
TSUrlPortSet(TSMBuffer bufp, TSMLoc obj, int port)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_url_handle(obj) == TS_SUCCESS) && isWriteable(bufp) && (port > 0)) {
    URL u;
    u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    u.m_url_impl = (URLImpl *) obj;
    u.port_set(port);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

/* FTP and HTTP specific URLs  */

const char *
TSUrlPathGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::path_get);
}

TSReturnCode
TSUrlPathSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::path_set);
  }

  return TS_ERROR;
}

/* FTP specific URLs */

int
TSUrlFtpTypeGet(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_url_handle(obj) != TS_SUCCESS)) {
    return TS_ERROR;
  }
  URL u;
  u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  u.m_url_impl = (URLImpl *) obj;

  return u.type_get();
}

TSReturnCode
TSUrlFtpTypeSet(TSMBuffer bufp, TSMLoc obj, int type)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.

  //The valid values are : 0, 65('A'), 97('a'),
  //69('E'), 101('e'), 73 ('I') and 105('i').

  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_url_handle(obj) == TS_SUCCESS) &&
      (type == 0 || type == 'A' || type == 'E' || type == 'I' || type == 'a' || type == 'i' || type == 'e') &&
      isWriteable(bufp)) {
    URL u;
    u.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    u.m_url_impl = (URLImpl *) obj;
    u.type_set(type);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

/* HTTP specific URLs */

const char *
TSUrlHttpParamsGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::params_get);
}

TSReturnCode
TSUrlHttpParamsSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::params_set);
  }

  return TS_ERROR;

}

const char *
TSUrlHttpQueryGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::query_get);
}

TSReturnCode
TSUrlHttpQuerySet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::query_set);
  }

  return TS_ERROR;
}

const char *
TSUrlHttpFragmentGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  return URLPartGet(bufp, obj, length, &URL::fragment_get);
}

TSReturnCode
TSUrlHttpFragmentSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  if (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS) {
    return URLPartSet(bufp, obj, value, length, &URL::fragment_set);
  }

  return TS_ERROR;

}

////////////////////////////////////////////////////////////////////
//
// MIME Headers
//
////////////////////////////////////////////////////////////////////

/**************/
/* MimeParser */
/**************/

TSMimeParser
TSMimeParserCreate()
{
  TSMimeParser parser;

  parser = xmalloc(sizeof(MIMEParser));
  if (sdk_sanity_check_mime_parser(parser) != TS_SUCCESS) {
    xfree(parser);
    return (TSMimeParser) TS_ERROR_PTR;
  }
  mime_parser_init((MIMEParser *) parser);

  return parser;
}

TSReturnCode
TSMimeParserClear(TSMimeParser parser)
{
  if (sdk_sanity_check_mime_parser(parser) != TS_SUCCESS) {
    return TS_ERROR;
  }
  mime_parser_clear((MIMEParser *) parser);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeParserDestroy(TSMimeParser parser)
{
  if (sdk_sanity_check_mime_parser(parser) != TS_SUCCESS) {
    return TS_ERROR;
  }
  mime_parser_clear((MIMEParser *) parser);
  xfree(parser);
  return TS_SUCCESS;
}

/***********/
/* MimeHdr */
/***********/

// TSMBuffer: pointers to HdrHeapSDKHandle objects
// TSMLoc:    pointers to MIMEFieldSDKHandle objects

TSMLoc
TSMimeHdrCreate(TSMBuffer bufp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, return NULL.
  // Changed the return value for SDK3.0 from NULL to TS_ERROR_PTR.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) && isWriteable(bufp)) {
    return (TSMLoc) mime_hdr_create(((HdrHeapSDKHandle *) bufp)->m_heap);
  } else {
    return (TSMLoc) TS_ERROR_PTR;
  }
}

TSReturnCode
TSMimeHdrDestroy(TSMBuffer bufp, TSMLoc obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS))
      && isWriteable(bufp)) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
    mime_hdr_destroy(((HdrHeapSDKHandle *) bufp)->m_heap, mh);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSMLoc
TSMimeHdrClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_hdr)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, return NULL.
  if ((sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(src_hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(src_hdr) == TS_SUCCESS)) &&
      isWriteable(dest_bufp)) {
    HdrHeap *s_heap, *d_heap;
    MIMEHdrImpl *s_mh, *d_mh;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_mh = _hdr_mloc_to_mime_hdr_impl(src_hdr);

    d_mh = mime_hdr_clone(s_mh, s_heap, d_heap, (s_heap != d_heap));
    return ((TSMLoc) d_mh);
  } else {
    return (TSMLoc) TS_ERROR_PTR;
  }
}

TSReturnCode
TSMimeHdrCopy(TSMBuffer dest_bufp, TSMLoc dest_obj, TSMBuffer src_bufp, TSMLoc src_obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(src_obj) == TS_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(src_obj) == TS_SUCCESS)) &&
      ((sdk_sanity_check_mime_hdr_handle(dest_obj) == TS_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(dest_obj) == TS_SUCCESS)) && isWriteable(dest_bufp)) {
    HdrHeap *s_heap, *d_heap;
    MIMEHdrImpl *s_mh, *d_mh;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_mh = _hdr_mloc_to_mime_hdr_impl(src_obj);
    d_mh = _hdr_mloc_to_mime_hdr_impl(dest_obj);

    mime_hdr_fields_clear(d_heap, d_mh);
    mime_hdr_copy_onto(s_mh, s_heap, d_mh, d_heap, (s_heap != d_heap));
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSMimeHdrPrint(TSMBuffer bufp, TSMLoc obj, TSIOBuffer iobufp)
{
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS)) &&
      (sdk_sanity_check_iocore_structure(iobufp) == TS_SUCCESS)) {
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
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

int
TSMimeHdrParse(TSMimeParser parser, TSMBuffer bufp, TSMLoc obj, const char **start, const char *end)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      ((sdk_sanity_check_mime_hdr_handle(obj) != TS_SUCCESS) && (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS))
      || (start == NULL) || (*start == NULL) || (!isWriteable(bufp))) {
    return TS_PARSE_ERROR;
  }
  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
  return (mime_parser_parse((MIMEParser *) parser, ((HdrHeapSDKHandle *) bufp)->m_heap, mh, start, end, false, false));
}

int
TSMimeHdrLengthGet(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS))) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
    return (mime_hdr_length_get(mh));
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSMimeHdrFieldsClear(TSMBuffer bufp, TSMLoc obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS))
      && isWriteable(bufp)) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
    mime_hdr_fields_clear(((HdrHeapSDKHandle *) bufp)->m_heap, mh);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

int
TSMimeHdrFieldsCount(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS))) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
    return (mime_hdr_fields_count(mh));
  } else {
    return TS_ERROR;
  }
}

/* TODO: These are supposedly obsoleted, but yet used all over the place in here ... */

/*************/
/* MimeField */
/*************/

const char *
TSMimeFieldValueGet(TSMBuffer bufp, TSMLoc field_obj, int idx, int *value_len_ptr)
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

void
TSMimeFieldValueSet(TSMBuffer bufp, TSMLoc field_obj, int idx, const char *value, int length)
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

TSMLoc
TSMimeFieldValueInsert(TSMBuffer bufp, TSMLoc field_obj, const char *value, int length, int idx)
{
  sdk_sanity_check_mbuffer(bufp);
  sdk_sanity_check_field_handle(field_obj);

  if (length == -1)
    length = strlen(value);

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field_obj;
  HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  mime_field_value_insert_comma_val(heap, handle->mh, handle->field_ptr, idx, value, length);
  return (TS_NULL_MLOC);
}


/****************/
/* MimeHdrField */
/****************/

// TSMBuffer: pointers to HdrHeapSDKHandle objects
// TSMLoc:    pointers to MIMEFieldSDKHandle objects

int
TSMimeHdrFieldEqual(TSMBuffer bufp, TSMLoc hdr_obj, TSMLoc field1_obj, TSMLoc field2_obj)
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

TSMLoc
TSMimeHdrFieldGet(TSMBuffer bufp, TSMLoc hdr_obj, int idx)
{

  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr_obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr_obj) == TS_SUCCESS)) &&
      (idx >= 0)) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(hdr_obj);
    MIMEField *f = mime_hdr_field_get(mh, idx);
    if (f == NULL)
      return ((TSMLoc) NULL);

    MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, mh);
    h->field_ptr = f;
    return (h);
  } else {
    return (TSMLoc) TS_ERROR_PTR;
  }
}

TSMLoc
TSMimeHdrFieldFind(TSMBuffer bufp, TSMLoc hdr_obj, const char *name, int length)
{
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr_obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr_obj) == TS_SUCCESS)) &&
      (name != NULL)) {

    if (length == -1)
      length = strlen(name);

    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(hdr_obj);
    MIMEField *f = mime_hdr_field_find(mh, name, length);
    if (f == NULL)
      return ((TSMLoc) NULL);

    MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, mh);
    h->field_ptr = f;
    return (h);
  } else {
    return (TSMLoc) TS_ERROR_PTR;
  }
}

TSReturnCode
TSMimeHdrFieldAppend(TSMBuffer bufp, TSMLoc mh_mloc, TSMLoc field_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(mh_mloc) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(mh_mloc) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field_mloc) == TS_SUCCESS) && isWriteable(bufp)) {
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

    ink_assert(field_handle->mh == mh);
    ink_assert(field_handle->field_ptr->m_ptr_name);

    mime_hdr_field_attach(mh, field_handle->field_ptr, 1, NULL);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldRemove(TSMBuffer bufp, TSMLoc mh_mloc, TSMLoc field_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(mh_mloc) == TS_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(mh_mloc) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field_mloc, mh_mloc) == TS_SUCCESS) && isWriteable(bufp)) {

    MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field_mloc;

    if (field_handle->mh != NULL) {
      MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
      ink_assert(mh == field_handle->mh);
      sdk_sanity_check_field_handle(field_mloc, mh_mloc);
      mime_hdr_field_detach(mh, field_handle->field_ptr, false);        // only detach this dup
    }
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSMimeHdrFieldDestroy(TSMBuffer bufp, TSMLoc mh_mloc, TSMLoc field_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(mh_mloc) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(mh_mloc) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field_mloc, mh_mloc) == TS_SUCCESS) && isWriteable(bufp)) {

    MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field_mloc;

    if (field_handle->mh == NULL)       // standalone field
      {
        MIMEField *field_ptr = field_handle->field_ptr;
        ink_assert(field_ptr->m_readiness != MIME_FIELD_SLOT_READINESS_DELETED);
        sdk_free_standalone_field(bufp, field_ptr);
      } else if (field_handle->mh != NULL) {
      MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
      HdrHeap *heap = (HdrHeap *) (((HdrHeapSDKHandle *) bufp)->m_heap);
      ink_assert(mh == field_handle->mh);
      sdk_sanity_check_field_handle(field_mloc, mh_mloc);

      // detach and delete this field, but not all dups
      mime_hdr_field_delete(heap, mh, field_handle->field_ptr, false);
    }
    // for consistence, the handle will not be released here.
    // users will be required to do it.

    //sdk_free_field_handle(bufp, field_handle);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSMLoc
TSMimeHdrFieldCreate(TSMBuffer bufp, TSMLoc mh_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, return NULL.
  // Changed the return value to TS_ERROR_PTR from NULL in case of errors.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(mh_mloc) == TS_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(mh_mloc) == TS_SUCCESS)) && isWriteable(bufp)) {

    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
    HdrHeap *heap = (HdrHeap *) (((HdrHeapSDKHandle *) bufp)->m_heap);

    MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, mh);
    h->field_ptr = mime_field_create(heap, mh);
    return (h);
  } else {
    return (TSMLoc) TS_ERROR_PTR;
  }
}

TSMLoc
TSMimeHdrFieldCreateNamed(TSMBuffer bufp, TSMLoc mh_mloc, const char *name, int name_len)
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

TSReturnCode
TSMimeHdrFieldCopy(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMLoc dest_field,
                    TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc src_field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(src_hdr) == TS_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(src_hdr) == TS_SUCCESS)) &&
      ((sdk_sanity_check_mime_hdr_handle(dest_hdr) == TS_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(dest_hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(src_field, src_hdr) == TS_SUCCESS) &&
      (sdk_sanity_check_field_handle(dest_field, dest_hdr) == TS_SUCCESS) && isWriteable(dest_bufp)) {

    bool dest_attached;

    MIMEFieldSDKHandle *s_handle = (MIMEFieldSDKHandle *) src_field;
    MIMEFieldSDKHandle *d_handle = (MIMEFieldSDKHandle *) dest_field;
    HdrHeap *d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;

    // FIX: This tortuous detach/change/attach algorithm is due to the
    //      fact that we can't change the name of an attached header (assertion)

    // TODO: This is never used ... is_live() has no side effects, so this should be ok
    // to not call, so commented out
    // src_attached = (s_handle->mh && s_handle->field_ptr->is_live());
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
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSMLoc
TSMimeHdrFieldClone(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc src_field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, return NULL.
  if ((sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(dest_hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(dest_hdr) == TS_SUCCESS)) &&
      ((sdk_sanity_check_mime_hdr_handle(src_hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(src_hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(src_field, src_hdr) == TS_SUCCESS) && isWriteable(dest_bufp)) {
    TSMLoc dest_field = TSMimeHdrFieldCreate(dest_bufp, dest_hdr);
    sdk_sanity_check_field_handle(dest_field, dest_hdr);

    TSMimeHdrFieldCopy(dest_bufp, dest_hdr, dest_field, src_bufp, src_hdr, src_field);
    return (dest_field);
  } else {
    return (TSMLoc) TS_ERROR_PTR;
  }
}

TSReturnCode
TSMimeHdrFieldCopyValues(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMLoc dest_field,
                          TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc src_field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(src_hdr) == TS_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(src_hdr) == TS_SUCCESS)) &&
      ((sdk_sanity_check_mime_hdr_handle(dest_hdr) == TS_SUCCESS) ||
       (sdk_sanity_check_http_hdr_handle(dest_hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(src_field, src_hdr) == TS_SUCCESS) &&
      (sdk_sanity_check_field_handle(dest_field, dest_hdr) == TS_SUCCESS) && isWriteable(dest_bufp)) {

    MIMEFieldSDKHandle *s_handle = (MIMEFieldSDKHandle *) src_field;
    MIMEFieldSDKHandle *d_handle = (MIMEFieldSDKHandle *) dest_field;
    HdrHeap *d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    MIMEField *s_field, *d_field;

    s_field = s_handle->field_ptr;
    d_field = d_handle->field_ptr;
    mime_field_value_set(d_heap, d_handle->mh, d_field, s_field->m_ptr_value, s_field->m_len_value, true);

    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

// TODO: This is implemented horribly slowly, but who's using it anyway?
//       If we threaded all the MIMEFields, this function could be easier,
//       but we'd have to print dups in order and we'd need a flag saying
//       end of dup list or dup follows.
TSMLoc
TSMimeHdrFieldNext(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS)) {
    MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field;
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
  } else {
    return (TSMLoc) TS_ERROR_PTR;
  }
}

TSMLoc
TSMimeHdrFieldNextDup(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      ((sdk_sanity_check_mime_hdr_handle(hdr) != TS_SUCCESS) && (sdk_sanity_check_http_hdr_handle(hdr) != TS_SUCCESS))
      || (sdk_sanity_check_field_handle(field, hdr) != TS_SUCCESS)) {
    return (TSMLoc) TS_ERROR_PTR;
  }

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(hdr);
  MIMEFieldSDKHandle *field_handle = (MIMEFieldSDKHandle *) field;
  MIMEField *next = field_handle->field_ptr->m_next_dup;
  if (next == NULL)
    return ((TSMLoc) NULL);

  MIMEFieldSDKHandle *next_handle = sdk_alloc_field_handle(bufp, mh);
  next_handle->field_ptr = next;
  return ((TSMLoc) next_handle);
}

int
TSMimeHdrFieldLengthGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      ((sdk_sanity_check_mime_hdr_handle(hdr) != TS_SUCCESS) && (sdk_sanity_check_http_hdr_handle(hdr) != TS_SUCCESS)) ||
      (sdk_sanity_check_field_handle(field, hdr) != TS_SUCCESS)) {
    return TS_ERROR;
  }

  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field;
  return mime_field_length_get(handle->field_ptr);
}

const char *
TSMimeHdrFieldNameGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int *length)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      ((sdk_sanity_check_mime_hdr_handle(hdr) != TS_SUCCESS) && (sdk_sanity_check_http_hdr_handle(hdr) != TS_SUCCESS)) ||
      (sdk_sanity_check_field_handle(field, hdr) != TS_SUCCESS)) {
    return (const char *) TS_ERROR_PTR;
  }

  int name_len;
  const char *name_ptr;
  MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field;

  name_ptr = mime_field_name_get(handle->field_ptr, &name_len);
  if (length)
    *length = name_len;
  return (((HdrHeapSDKHandle *) bufp)->make_sdk_string(name_ptr, name_len));
}

TSReturnCode
TSMimeHdrFieldNameSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, const char *name, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) &&
      (sdk_sanity_check_null_ptr((void *) name) == TS_SUCCESS) && isWriteable(bufp)) {
    if (length == -1)
      length = strlen(name);

    MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field;
    HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;

    int attached = (handle->mh && handle->field_ptr->is_live());
    if (attached)
      mime_hdr_field_detach(handle->mh, handle->field_ptr, false);

    handle->field_ptr->name_set(heap, handle->mh, name, length);

    if (attached)
      mime_hdr_field_attach(handle->mh, handle->field_ptr, 1, NULL);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSMimeHdrFieldValuesClear(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && isWriteable(bufp)) {
    MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field;
    HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    /**
     * Modified the string value passed from an empty string ("") to NULL.
     * An empty string is also considered to be a token. The correct value of
     * the field after this function should be NULL.
     */
    mime_field_value_set(heap, handle->mh, handle->field_ptr, NULL, 0, 1);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

int
TSMimeHdrFieldValuesCount(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS)) {
    MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field;
    return (mime_field_value_get_comma_val_count(handle->field_ptr));
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSMimeHdrFieldValueStringGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char **value_ptr,
                              int *value_len_ptr)
{
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS))
      && (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && (value_ptr != NULL) &&
      sdk_sanity_check_null_ptr((void *) value_len_ptr) == TS_SUCCESS) {
    *value_ptr = TSMimeFieldValueGet(bufp, field, idx, value_len_ptr);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSMimeHdrFieldValueDateGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, time_t *value_ptr)
{
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && (value_ptr != NULL)) {
    int value_len;
    const char *value_str = TSMimeFieldValueGet(bufp, field, -1, &value_len);

    if (value_str == NULL) {
      *value_ptr = (time_t) 0;
    } else {
      *value_ptr = mime_parse_date(value_str, value_str + value_len);
      ((HdrHeapSDKHandle *) bufp)->destroy_sdk_string((char *) value_str);
    }
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueIntGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int *value_ptr)
{
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && (value_ptr != NULL)) {

    int value_len;
    const char *value_str = TSMimeFieldValueGet(bufp, field, idx, &value_len);

    if (value_str == NULL) {
      *value_ptr = 0;  // TODO: Hmmm, this is weird, but it's the way it worked before ...
    } else{
      *value_ptr = mime_parse_int(value_str, value_str + value_len);
      ((HdrHeapSDKHandle *) bufp)->destroy_sdk_string((char *) value_str);
    }
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueUintGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, unsigned int *value_ptr)
{
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && (value_ptr != NULL)) {

    int value_len;
    const char *value_str = TSMimeFieldValueGet(bufp, field, idx, &value_len);

    if (value_str == NULL) {
      *value_ptr = 0;
    } else {
      *value_ptr = mime_parse_uint(value_str, value_str + value_len);
      ((HdrHeapSDKHandle *) bufp)->destroy_sdk_string((char *) value_str);
    }
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueStringSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) &&
      (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS) && isWriteable(bufp)) {
    if (length == -1)
      length = strlen(value);
    TSMimeFieldValueSet(bufp, field, idx, value, length);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueDateSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, time_t value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && isWriteable(bufp)) {
    char tmp[33];
    int len = mime_format_date(tmp, value);

    // idx is ignored and we overwrite all existing values
    // TSMimeFieldValueSet(bufp, field_obj, idx, tmp, len);
    TSMimeFieldValueSet(bufp, field, -1, tmp, len);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueIntSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && isWriteable(bufp)) {
    char tmp[16];
    int len = mime_format_int(tmp, value, sizeof(tmp));

    TSMimeFieldValueSet(bufp, field, idx, tmp, len);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueUintSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, unsigned int value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && isWriteable(bufp)) {
    char tmp[16];
    int len = mime_format_uint(tmp, value, sizeof(tmp));

    TSMimeFieldValueSet(bufp, field, idx, tmp, len);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueAppend(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && (idx >= 0) && (value != NULL) &&
      isWriteable(bufp)) {
    MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field;
    HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;

    if (length == -1)
      length = strlen(value);
    mime_field_value_extend_comma_val(heap, handle->mh, handle->field_ptr, idx, value, length);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueStringInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR, else return TS_SUCCESS.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) &&
      (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS) && isWriteable(bufp)) {
    if (length == -1)
      length = strlen(value);
    TSMimeFieldValueInsert(bufp, field, value, length, idx);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueIntInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR, else return TS_SUCCESS.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && isWriteable(bufp)) {
    char tmp[16];
    int len = mime_format_int(tmp, value, sizeof(tmp));

    (void)TSMimeFieldValueInsert(bufp, field, tmp, len, idx);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueUintInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, unsigned int value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR, else return TS_SUCCESS.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && isWriteable(bufp)) {
    char tmp[16];
    int len = mime_format_uint(tmp, value, sizeof(tmp));

    (void)TSMimeFieldValueInsert(bufp, field, tmp, len, idx);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueDateInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, time_t value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR, else return TS_SUCCESS
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && isWriteable(bufp)) {

    if (TSMimeHdrFieldValuesClear(bufp, hdr, field) == TS_ERROR) {
      return TS_ERROR;
    }

    char tmp[33];
    int len = mime_format_date(tmp, value);
    // idx ignored, overwrite all exisiting values
    // (void)TSMimeFieldValueInsert(bufp, field_obj, tmp, len, idx);
    (void) TSMimeFieldValueSet(bufp, field, -1, tmp, len);

    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSMimeHdrFieldValueDelete(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      ((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS)) &&
      (sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS) && (idx >= 0) && isWriteable(bufp)) {
    MIMEFieldSDKHandle *handle = (MIMEFieldSDKHandle *) field;
    HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;

    mime_field_value_delete_comma_val(heap, handle->mh, handle->field_ptr, idx);
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

/**************/
/* HttpParser */
/**************/

TSHttpParser
TSHttpParserCreate()
{
  TSHttpParser parser;

  parser = xmalloc(sizeof(HTTPParser));
  if (sdk_sanity_check_http_parser(parser) != TS_SUCCESS) {
    return (TSHttpParser) TS_ERROR_PTR;
  }
  http_parser_init((HTTPParser *) parser);

  return parser;
}

TSReturnCode
TSHttpParserClear(TSHttpParser parser)
{
  if (sdk_sanity_check_http_parser(parser) != TS_SUCCESS) {
    return TS_ERROR;
  }
  http_parser_clear((HTTPParser *) parser);
  return TS_SUCCESS;
}

TSReturnCode
TSHttpParserDestroy(TSHttpParser parser)
{
  if (sdk_sanity_check_http_parser(parser) != TS_SUCCESS) {
    return TS_ERROR;
  }
  http_parser_clear((HTTPParser *) parser);
  xfree(parser);
  return TS_SUCCESS;
}

/***********/
/* HttpHdr */
/***********/


TSMLoc
TSHttpHdrCreate(TSMBuffer bufp)
{
  if (sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) {
    return (TSMLoc) TS_ERROR_PTR;
  }

  HTTPHdr h;
  h.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  h.create(HTTP_TYPE_UNKNOWN);
  return ((TSMLoc) (h.m_http));
}

TSReturnCode
TSHttpHdrDestroy(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS)) {
    return TS_ERROR;
  }
  // No more objects counts in heap or deallocation
  //   so do nothing!
  return TS_SUCCESS;

  // HDR FIX ME - Did this free the MBuffer in Pete's old system
}

TSMLoc
TSHttpHdrClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_hdr)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, return NULL.
  if ((sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(src_hdr) == TS_SUCCESS) && isWriteable(dest_bufp)) {
    HdrHeap *s_heap, *d_heap;
    HTTPHdrImpl *s_hh, *d_hh;

    s_heap = ((HdrHeapSDKHandle *) src_bufp)->m_heap;
    d_heap = ((HdrHeapSDKHandle *) dest_bufp)->m_heap;
    s_hh = (HTTPHdrImpl *) src_hdr;

    ink_assert(s_hh->m_type == HDR_HEAP_OBJ_HTTP_HEADER);

    // TODO: This is never used
    // inherit_strs = (s_heap != d_heap ? true : false);

    d_hh = http_hdr_clone(s_hh, s_heap, d_heap);
    return ((TSMLoc) d_hh);
  } else {
    return (TSMLoc) TS_ERROR_PTR;
  }
}

TSReturnCode
TSHttpHdrCopy(TSMBuffer dest_bufp, TSMLoc dest_obj, TSMBuffer src_bufp, TSMLoc src_obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(dest_obj) == TS_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(src_obj) == TS_SUCCESS) && isWriteable(dest_bufp)) {
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

    TSHttpHdrTypeSet(dest_bufp, dest_obj, (TSHttpType) (s_hh->m_polarity));
    http_hdr_copy_onto(s_hh, s_heap, d_hh, d_heap, inherit_strs);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSHttpHdrPrint(TSMBuffer bufp, TSMLoc obj, TSIOBuffer iobufp)
{
  MIOBuffer *b = (MIOBuffer *) iobufp;
  IOBufferBlock *blk;
  int bufindex;
  int tmp, dumpoffset;
  int done;

  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(iobufp) != TS_SUCCESS)) {
    return TS_ERROR;
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
  return TS_SUCCESS;
}

int
TSHttpHdrParseReq(TSHttpParser parser, TSMBuffer bufp, TSMLoc obj, const char **start, const char *end)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS) ||
      (start == NULL) || (*start == NULL) || (!isWriteable(bufp))) {
    return TS_PARSE_ERROR;
  }

  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
  TSHttpHdrTypeSet(bufp, obj, TS_HTTP_TYPE_REQUEST);
  return h.parse_req((HTTPParser *) parser, start, end, false);
}

int
TSHttpHdrParseResp(TSHttpParser parser, TSMBuffer bufp, TSMLoc obj, const char **start, const char *end)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) ||
      (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS) ||
      (start == NULL) || (*start == NULL) || (!isWriteable(bufp))) {
    return TS_PARSE_ERROR;
  }
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
  TSHttpHdrTypeSet(bufp, obj, TS_HTTP_TYPE_RESPONSE);
  return h.parse_resp((HTTPParser *) parser, start, end, false);
}

int
TSHttpHdrLengthGet(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS)) {
    return TS_ERROR;
  }

  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
  return h.length_get();
}

TSHttpType
TSHttpHdrTypeGet(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS)) {
    return (TSHttpType) TS_ERROR;
  }
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
   */
  return (TSHttpType) h.type_get();
}

TSReturnCode
TSHttpHdrTypeSet(TSMBuffer bufp, TSMLoc obj, TSHttpType type)
{
#ifdef DEBUG
  if ((type<TS_HTTP_TYPE_UNKNOWN) || (type> TS_HTTP_TYPE_RESPONSE)) {
    return TS_ERROR;
  }
#endif
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS) && isWriteable(bufp)) {

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
      if (type == (TSHttpType) HTTP_TYPE_REQUEST) {
        h.m_http->u.req.m_url_impl = url_create(h.m_heap);
        h.m_http->m_polarity = (HTTPType) type;
      } else if (type == (TSHttpType) HTTP_TYPE_RESPONSE) {
        h.m_http->m_polarity = (HTTPType) type;
      }
    }
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

int
TSHttpHdrVersionGet(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS)) {
    return TS_ERROR;
  }

  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
   */

  HTTPVersion ver = h.version_get();

  return ver.m_version;
}

TSReturnCode
TSHttpHdrVersionSet(TSMBuffer bufp, TSMLoc obj, int ver)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS) && isWriteable(bufp)) {
    HTTPHdr h;
    SET_HTTP_HDR(h, bufp, obj);
    ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);

    HTTPVersion version(ver);

    h.version_set(version);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

const char *
TSHttpHdrMethodGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS)) {
    return (const char *) TS_ERROR_PTR;
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

TSReturnCode
TSHttpHdrMethodSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS) &&
      isWriteable(bufp) && (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS)) {
    HTTPHdr h;
    SET_HTTP_HDR(h, bufp, obj);
    /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
       ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
     */

    if (length < 0)
      length = strlen(value);

    h.method_set(value, length);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSMLoc
TSHttpHdrUrlGet(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS)) {
    return (TSMLoc) TS_ERROR_PTR;
  }
  HTTPHdrImpl *hh = (HTTPHdrImpl *) obj;
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(hh->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
   */

  if (hh->m_polarity != HTTP_TYPE_REQUEST)
    return ((TSMLoc) TS_ERROR_PTR);
  else
    return ((TSMLoc) hh->u.req.m_url_impl);
}

TSReturnCode
TSHttpHdrUrlSet(TSMBuffer bufp, TSMLoc obj, TSMLoc url)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS) &&
      (sdk_sanity_check_url_handle(url) == TS_SUCCESS) && isWriteable(bufp)) {
    HdrHeap *heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
    HTTPHdrImpl *hh = (HTTPHdrImpl *) obj;
    ink_assert(hh->m_type == HDR_HEAP_OBJ_HTTP_HEADER);

    URLImpl *url_impl = (URLImpl *) url;
    http_hdr_url_set(heap, hh, url_impl);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSHttpStatus
TSHttpHdrStatusGet(TSMBuffer bufp, TSMLoc obj)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS)) {
    return (TSHttpStatus) TS_ERROR;
  }
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
   */
  return (TSHttpStatus) h.status_get();
}

TSReturnCode
TSHttpHdrStatusSet(TSMBuffer bufp, TSMLoc obj, TSHttpStatus status)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS) && isWriteable(bufp)) {
    HTTPHdr h;
    SET_HTTP_HDR(h, bufp, obj);
    ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
    h.status_set((HTTPStatus) status);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

const char *
TSHttpHdrReasonGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  if ((sdk_sanity_check_mbuffer(bufp) != TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) != TS_SUCCESS)) {
    return (const char *) TS_ERROR_PTR;
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

TSReturnCode
TSHttpHdrReasonSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if ((sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) &&
      (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS) &&
      isWriteable(bufp) && (sdk_sanity_check_null_ptr((void *) value) == TS_SUCCESS)) {
    HTTPHdr h;
    SET_HTTP_HDR(h, bufp, obj);
    /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
       ink_assert(h.m_http->m_type == HDR_HEAP_OBJ_HTTP_HEADER);
     */

    if (length < 0)
      length = strlen(value);
    h.reason_set(value, length);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

const char *
TSHttpHdrReasonLookup(TSHttpStatus status)
{
  return http_hdr_reason_lookup((HTTPStatus) status);
}


/// END CODE REVIEW HERE

////////////////////////////////////////////////////////////////////
//
// Cache
//
////////////////////////////////////////////////////////////////////

inline TSReturnCode
sdk_sanity_check_cachekey(TSCacheKey key)
{
#ifdef DEBUG
  if (key == NULL || key == TS_ERROR_PTR || ((CacheInfo *) key)->magic != CACHE_INFO_MAGIC_ALIVE)
    return TS_ERROR;

  return TS_SUCCESS;
#else
  NOWARN_UNUSED(key);
  return TS_SUCCESS;
#endif
}

TSReturnCode
TSCacheKeyGet(TSCacheTxn txnp, void **key, int *length)
{
  NewCacheVC *vc = (NewCacheVC *) txnp;
  Debug("cache_plugin", "[TSCacheKeyGet] vc get cache key");
  //    *key = (void*) NEW(new TS_MD5);

  // just pass back the url and don't do the md5
  vc->getCacheKey(key, length);

  return TS_SUCCESS;
}

TSReturnCode
TSCacheHeaderKeyGet(TSCacheTxn txnp, void **key, int *length)
{
  NewCacheVC *vc = (NewCacheVC *) txnp;
  Debug("cache_plugin", "[TSCacheKeyGet] vc get cache header key");
  vc->getCacheHeaderKey(key, length);

  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyCreate(TSCacheKey *new_key)
{
#ifdef DEBUG
  if (new_key == NULL)
    return TS_ERROR;
#endif
  *new_key = (TSCacheKey) NEW(new CacheInfo());
  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyDigestSet(TSCacheKey key, const unsigned char *input, int length)
{
  if (sdk_sanity_check_cachekey(key) != TS_SUCCESS)
    return TS_ERROR;

  if (sdk_sanity_check_iocore_structure((void *) input) != TS_SUCCESS || length < 0)
    return TS_ERROR;

  ((CacheInfo *) key)->cache_key.encodeBuffer((char *) input, length);
  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyDigestFromUrlSet(TSCacheKey key, TSMLoc url)
{
  if (sdk_sanity_check_cachekey(key) != TS_SUCCESS)
    return TS_ERROR;

  url_MD5_get((URLImpl *) url, &((CacheInfo *) key)->cache_key);
  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyDataTypeSet(TSCacheKey key, TSCacheDataType type)
{
  if (sdk_sanity_check_cachekey(key) != TS_SUCCESS)
    return TS_ERROR;

  switch (type) {
  case TS_CACHE_DATA_TYPE_NONE:
    ((CacheInfo *) key)->frag_type = CACHE_FRAG_TYPE_NONE;
    break;
  case TS_CACHE_DATA_TYPE_OTHER:      /* other maps to http */
  case TS_CACHE_DATA_TYPE_HTTP:
    ((CacheInfo *) key)->frag_type = CACHE_FRAG_TYPE_HTTP;
    break;
  default:
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyHostNameSet(TSCacheKey key, const unsigned char *hostname, int host_len)
{
#ifdef DEBUG
  if ((hostname == NULL) || (host_len <= 0))
    return TS_ERROR;
#endif
  if (sdk_sanity_check_cachekey(key) != TS_SUCCESS)
    return TS_ERROR;

  CacheInfo *i = (CacheInfo *) key;
  /* need to make a copy of the hostname. The caller
     might deallocate it anytime in the future */
  i->hostname = (char *) xmalloc(host_len);
  memcpy(i->hostname, hostname, host_len);
  i->len = host_len;
  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyPinnedSet(TSCacheKey key, time_t pin_in_cache)
{
  if (sdk_sanity_check_cachekey(key) != TS_SUCCESS)
    return TS_ERROR;

  CacheInfo *i = (CacheInfo *) key;
  i->pin_in_cache = pin_in_cache;
  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyDestroy(TSCacheKey key)
{
  if (sdk_sanity_check_cachekey(key) != TS_SUCCESS)
    return TS_ERROR;

  CacheInfo *i = (CacheInfo *) key;
  if (i->hostname)
    xfree(i->hostname);
  i->magic = CACHE_INFO_MAGIC_DEAD;
  delete i;
  return TS_SUCCESS;
}

TSCacheHttpInfo
TSCacheHttpInfoCopy(TSCacheHttpInfo infop)
{
  CacheHTTPInfo *new_info = NEW(new CacheHTTPInfo);
  new_info->copy((CacheHTTPInfo *) infop);
  return new_info;
}

void
TSCacheHttpInfoReqGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *obj)
{
  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  *bufp = info->request_get();
  *obj = info->request_get()->m_http;
  sdk_sanity_check_mbuffer(*bufp);
}


void
TSCacheHttpInfoRespGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *obj)
{
  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  *bufp = info->response_get();
  *obj = info->response_get()->m_http;
  sdk_sanity_check_mbuffer(*bufp);
}

void
TSCacheHttpInfoReqSet(TSCacheHttpInfo infop, TSMBuffer bufp, TSMLoc obj)
{
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);

  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  info->request_set(&h);
}


void
TSCacheHttpInfoRespSet(TSCacheHttpInfo infop, TSMBuffer bufp, TSMLoc obj)
{
  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);

  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  info->response_set(&h);
}


int
TSCacheHttpInfoVector(TSCacheHttpInfo infop, void *data, int length)
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
TSCacheHttpInfoDestroy(TSCacheHttpInfo *infop)
{

  ((CacheHTTPInfo *) infop)->destroy();
}

TSCacheHttpInfo
TSCacheHttpInfoCreate()
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
TSConfigSet(unsigned int id, void *data, TSConfigDestroyFunc funcp)
{
  INKConfigImpl *config = NEW(new INKConfigImpl);
  config->mdata = data;
  config->m_destroy_func = funcp;
  return configProcessor.set(id, config);
}

TSConfig
TSConfigGet(unsigned int id)
{
  return configProcessor.get(id);
}

void
TSConfigRelease(unsigned int id, TSConfig configp)
{
  configProcessor.release(id, (ConfigInfo *) configp);
}

void *
TSConfigDataGet(TSConfig configp)
{
  INKConfigImpl *config = (INKConfigImpl *) configp;
  return config->mdata;
}

////////////////////////////////////////////////////////////////////
//
// Management
//
////////////////////////////////////////////////////////////////////

TSReturnCode
TSMgmtUpdateRegister(TSCont contp, const char *plugin_name, const char *path)
{
  if (sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) plugin_name) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) path) != TS_SUCCESS)
    return TS_ERROR;

  global_config_cbs->insert((INKContInternal *) contp, plugin_name, path);
  return TS_SUCCESS;
}

int
TSMgmtIntGet(const char *var_name, TSMgmtInt *result)
{
  return RecGetRecordInt((char *) var_name, (RecInt *) result) == REC_ERR_OKAY ? 1 : 0;
}

int
TSMgmtCounterGet(const char *var_name, TSMgmtCounter *result)
{
  return RecGetRecordCounter((char *) var_name, (RecCounter *) result) == REC_ERR_OKAY ? 1 : 0;
}

int
TSMgmtFloatGet(const char *var_name, TSMgmtFloat *result)
{
  return RecGetRecordFloat((char *) var_name, (RecFloat *) result) == REC_ERR_OKAY ? 1 : 0;
}

int
TSMgmtStringGet(const char *var_name, TSMgmtString *result)
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

TSCont
TSContCreate(TSEventFunc funcp, TSMutex mutexp)
{
  // mutexp can be NULL
  if ((mutexp != NULL) && (sdk_sanity_check_mutex(mutexp) != TS_SUCCESS))
    return (TSCont) TS_ERROR_PTR;
  INKContInternal *i = INKContAllocator.alloc();

  i->init(funcp, mutexp);
  return (TSCont) i;
}

TSReturnCode
TSContDestroy(TSCont contp)
{
  if (sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS)
    return TS_ERROR;

  INKContInternal *i = (INKContInternal *) contp;
  i->destroy();
  return TS_SUCCESS;
}

TSReturnCode
TSContDataSet(TSCont contp, void *data)
{
  if (sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS)
    return TS_ERROR;

  INKContInternal *i = (INKContInternal *) contp;
  i->mdata = data;
  return TS_SUCCESS;
}

void *
TSContDataGet(TSCont contp)
{
  if (sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS)
    return (TSCont) TS_ERROR_PTR;

  INKContInternal *i = (INKContInternal *) contp;
  return i->mdata;
}

TSAction
TSContSchedule(TSCont contp, ink_hrtime timeout)
{
  if (sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS)
    return (TSAction) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  INKContInternal *i = (INKContInternal *) contp;
  TSAction action;
  if (ink_atomic_increment((int *) &i->m_event_count, 1) < 0) {
    // simply return error_ptr
    //ink_assert (!"not reached");
    return (TSAction) TS_ERROR_PTR;
  }

  if (timeout == 0) {
    action = eventProcessor.schedule_imm(i, ET_NET);
  } else {
    action = eventProcessor.schedule_in(i, HRTIME_MSECONDS(timeout), ET_NET);
  }

/* This is a hack. SHould be handled in ink_types */
  action = (TSAction) ((uintptr_t) action | 0x1);

  return action;
}

TSAction
TSHttpSchedule(TSCont contp ,TSHttpTxn txnp, ink_hrtime timeout)
{
  if (sdk_sanity_check_iocore_structure (contp) != TS_SUCCESS)
    return (TSAction) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  TSAction action;
  Continuation *cont  = (Continuation*)contp;
  HttpSM *sm = (HttpSM*)txnp;
  sm->set_http_schedule(cont);

  if (timeout == 0) {
    action = eventProcessor.schedule_imm (sm, ET_NET);
  } else {
    action = eventProcessor.schedule_in (sm, HRTIME_MSECONDS (timeout), ET_NET);
  }

  action = (TSAction) ((uintptr_t) action | 0x1);

  return action;
}

int
TSContCall(TSCont contp, TSEvent event, void *edata)
{
  Continuation *c = (Continuation *) contp;
  return c->handleEvent((int) event, edata);
}

TSMutex
TSContMutexGet(TSCont contp)
{
  if (sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS)
    return (TSCont) TS_ERROR_PTR;

  Continuation *c = (Continuation *) contp;
  return (TSMutex) ((ProxyMutex *) c->mutex);
}


/* HTTP hooks */

TSReturnCode
TSHttpHookAdd(TSHttpHookID id, TSCont contp)
{
  if (sdk_sanity_check_continuation(contp) == TS_SUCCESS && sdk_sanity_check_hook_id(id) == TS_SUCCESS) {
    http_global_hooks->append(id, (INKContInternal *) contp);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

/* Cache hooks */

TSReturnCode
TSCacheHookAdd(TSCacheHookID id, TSCont contp)
{
  if (sdk_sanity_check_continuation(contp) == TS_SUCCESS) {
    cache_global_hooks->append(id, (INKContInternal *) contp);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

void
TSHttpIcpDynamicSet(int value)
{
  int32 old_value, new_value;

  new_value = (value == 0) ? 0 : 1;
  old_value = icp_dynamic_enabled;
  while (old_value != new_value) {
    if (ink_atomic_cas(&icp_dynamic_enabled, old_value, new_value))
      break;
    old_value = icp_dynamic_enabled;
  }
}


/* HTTP sessions */

TSReturnCode
TSHttpSsnHookAdd(TSHttpSsn ssnp, TSHttpHookID id, TSCont contp)
{
  if ((sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS) &&
      (sdk_sanity_check_continuation(contp) == TS_SUCCESS) && (sdk_sanity_check_hook_id(id) == TS_SUCCESS)) {
    HttpClientSession *cs = (HttpClientSession *) ssnp;
    cs->ssn_hook_append(id, (INKContInternal *) contp);
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

class TSHttpSsnCallback:public Continuation
{
public:
  TSHttpSsnCallback(HttpClientSession *cs, TSEvent event)
  :Continuation(cs->mutex), m_cs(cs), m_event(event)
  {
    SET_HANDLER(&TSHttpSsnCallback::event_handler);
  }

  int event_handler(int, void *)
  {
    m_cs->handleEvent((int) m_event, 0);
    delete this;
    return 0;
  }

private:
  HttpClientSession *m_cs;
  TSEvent m_event;
};


TSReturnCode
TSHttpSsnReenable(TSHttpSsn ssnp, TSEvent event)
{
  if (sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS) {
    HttpClientSession *cs = (HttpClientSession *) ssnp;
    EThread *eth = this_ethread();
    // If this function is being executed on a thread created by the API
    // which is DEDICATED, the continuation needs to be called back on a
    // REGULAR thread.
    if (eth->tt != REGULAR) {
      eventProcessor.schedule_imm(NEW(new TSHttpSsnCallback(cs, event)), ET_NET);
    } else {
      MUTEX_TRY_LOCK(trylock, cs->mutex, eth);
      if (!trylock) {
        eventProcessor.schedule_imm(NEW(new TSHttpSsnCallback(cs, event)), ET_NET);
      } else {
        cs->handleEvent((int) event, 0);
      }
    }
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}


/* HTTP transactions */

TSReturnCode
TSHttpTxnHookAdd(TSHttpTxn txnp, TSHttpHookID id, TSCont contp)
{
  if ((sdk_sanity_check_txn(txnp) != TS_SUCCESS) ||
      (sdk_sanity_check_continuation(contp) != TS_SUCCESS) || (sdk_sanity_check_hook_id(id) != TS_SUCCESS)) {
    return TS_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;
  sm->txn_hook_append(id, (INKContInternal *) contp);
  return TS_SUCCESS;
}


// Private api function for gzip plugin.
//  This function should only appear in TsapiPrivate.h
int
TSHttpTxnHookRegisteredFor(TSHttpTxn txnp, TSHttpHookID id, TSEventFunc funcp)
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


TSHttpSsn
TSHttpTxnSsnGet(TSHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return (TSHttpSsn) TS_ERROR_PTR;
  }
  HttpSM *sm = (HttpSM *) txnp;
  return (TSHttpSsn) sm->ua_session;
}

int
TSHttpTxnClientKeepaliveSet(TSHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State *s = &(sm->t_state);
  s->hdr_info.trust_response_cl = true;

  return 1;
}


int
TSHttpTxnClientReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != TS_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  HTTPHdr *hptr = &(sm->t_state.hdr_info.client_request);

  if (hptr->valid()) {
    *bufp = hptr;
    *obj = hptr->m_http;
    sdk_sanity_check_mbuffer(*bufp);
    hptr->mark_target_dirty();

    return 1;
  } else {
    return 0;
  }
}

// pristine url is the url before remap
TSReturnCode
TSHttpTxnPristineUrlGet (TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *url_loc)
{
  if (sdk_sanity_check_txn(txnp)!=TS_SUCCESS ||
    sdk_sanity_check_null_ptr((void*)bufp) != TS_SUCCESS ||
    sdk_sanity_check_null_ptr((void*)url_loc) != TS_SUCCESS) {
    return TS_ERROR;
  }
  HttpSM *sm = (HttpSM*) txnp;
  HTTPHdr *hptr = &(sm->t_state.hdr_info.client_request);

  if (hptr->valid()) {
    *bufp = hptr;
    *url_loc = (TSMLoc)sm->t_state.pristine_url.m_url_impl;
    sdk_sanity_check_mbuffer(*bufp);
    if (*url_loc)
      return TS_SUCCESS;
    else
      return TS_ERROR;
  }
  else
    return TS_ERROR;
}

// Shortcut to just get the URL.
char*
TSHttpTxnEffectiveUrlStringGet (TSHttpTxn txnp, int* length) {
  char* zret = 0;
  if (TS_SUCCESS == sdk_sanity_check_txn(txnp)) {
    HttpSM *sm = reinterpret_cast<HttpSM*>(txnp);
    zret = sm->t_state.hdr_info.client_request.url_string_get(0, length);
  }
  return zret;
}

int
TSHttpTxnClientRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != TS_SUCCESS) {
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
TSHttpTxnServerReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != TS_SUCCESS) {
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
TSHttpTxnServerRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != TS_SUCCESS) {
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
TSHttpTxnCachedReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != TS_SUCCESS) {
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
TSHttpTxnCachedRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) bufp) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) obj) != TS_SUCCESS) {
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
TSHttpTxnCachedRespModifiableGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State *s = &(sm->t_state);
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

TSReturnCode
TSHttpTxnCacheLookupStatusGet(TSHttpTxn txnp, int *lookup_status)
{
  if ((sdk_sanity_check_txn(txnp) != TS_SUCCESS) || (lookup_status == NULL)) {
    return TS_ERROR;
  }

  HttpSM *sm = (HttpSM *) txnp;

  switch (sm->t_state.cache_lookup_result) {
  case HttpTransact::CACHE_LOOKUP_MISS:
  case HttpTransact::CACHE_LOOKUP_DOC_BUSY:
    *lookup_status = TS_CACHE_LOOKUP_MISS;
    break;
  case HttpTransact::CACHE_LOOKUP_HIT_STALE:
    *lookup_status = TS_CACHE_LOOKUP_HIT_STALE;
    break;
  case HttpTransact::CACHE_LOOKUP_HIT_WARNING:
  case HttpTransact::CACHE_LOOKUP_HIT_FRESH:
    *lookup_status = TS_CACHE_LOOKUP_HIT_FRESH;
    break;
  case HttpTransact::CACHE_LOOKUP_SKIPPED:
    *lookup_status = TS_CACHE_LOOKUP_SKIPPED;
    break;
  case HttpTransact::CACHE_LOOKUP_NONE:
  default:
    return TS_ERROR;
  };

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnCacheLookupCountGet(TSHttpTxn txnp, int *lookup_count)
{
  if ((sdk_sanity_check_txn(txnp) != TS_SUCCESS) || (lookup_count == NULL)) {
    return TS_ERROR;
  }

  HttpSM *sm = (HttpSM *) txnp;
  *lookup_count = sm->t_state.cache_info.lookup_count;
  return TS_SUCCESS;
}


/* two hooks this function may gets called:
   TS_HTTP_READ_CACHE_HDR_HOOK   &
   TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK
 */
int
TSHttpTxnCacheLookupStatusSet(TSHttpTxn txnp, int cachelookup)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::CacheLookupResult_t *sm_status = &(sm->t_state.cache_lookup_result);

  // converting from a miss to a hit is not allowed
  if (*sm_status == HttpTransact::CACHE_LOOKUP_MISS && cachelookup != TS_CACHE_LOOKUP_MISS)
    return 0;

  // here is to handle converting a hit to a miss
  if (cachelookup == TS_CACHE_LOOKUP_MISS && *sm_status != HttpTransact::CACHE_LOOKUP_MISS) {
    sm->t_state.api_cleanup_cache_read = true;
    ink_assert(sm->t_state.transact_return_point != NULL);
    sm->t_state.transact_return_point = HttpTransact::HandleCacheOpenRead;
  }

  switch (cachelookup) {
  case TS_CACHE_LOOKUP_MISS:
    *sm_status = HttpTransact::CACHE_LOOKUP_MISS;
    break;
  case TS_CACHE_LOOKUP_HIT_STALE:
    *sm_status = HttpTransact::CACHE_LOOKUP_HIT_STALE;
    break;
  case TS_CACHE_LOOKUP_HIT_FRESH:
    *sm_status = HttpTransact::CACHE_LOOKUP_HIT_FRESH;
    break;
  default:
    return 0;
  }

  return 1;
}

int
TSHttpTxnCacheLookupUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj)
{
  HttpSM *sm = (HttpSM *) txnp;
  URL u, *l_url;

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
TSHttpTxnCachedUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj)
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
TSHttpTxnNewCacheLookupDo(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc url_loc)
{
  URL new_url, *client_url, *l_url, *o_url;
  INK_MD5 md51, md52;

  sdk_sanity_check_mbuffer(bufp);
  new_url.m_heap = ((HdrHeapSDKHandle *) bufp)->m_heap;
  new_url.m_url_impl = (URLImpl *) url_loc;
  if (!new_url.valid())
    return 0;

  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State *s = &(sm->t_state);

  client_url = s->hdr_info.client_request.url_get();
  if (!(client_url->valid()))
    return 0;

  // if l_url is not valid, then no cache lookup has been done yet
  // so we shouldn't be calling TSHttpTxnNewCacheLookupDo right now
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
TSHttpTxnSecondUrlTryLock(TSHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State *s = &(sm->t_state);
  // TSHttpTxnNewCacheLookupDo didn't continue
  if (!s->cache_info.original_url.valid())
    return 0;
  sm->add_cache_sm();
  s->api_lock_url = HttpTransact::LOCK_URL_SECOND;
  return 1;
}

TSReturnCode
TSHttpTxnFollowRedirect(TSHttpTxn txnp, int on)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;

  sm->api_enable_redirection = (on ? true : false);
  return TS_SUCCESS;
}

int
TSHttpTxnRedirectRequest(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc url_loc)
{
  URL u, *o_url, *r_url, *client_url;
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State *s = &(sm->t_state);

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
TSHttpTxnActiveTimeoutSet(TSHttpTxn txnp, int timeout)
{
  Debug("http_timeout", "setting active timeout to %d msec via API", timeout);
  HttpTransact::State *s = &(((HttpSM *) txnp)->t_state);
  s->api_txn_active_timeout_value = timeout;
  return 1;
}

/**
 * timeout is in msec
 * overrides as proxy.config.http.connect_attempts_timeout
**/
int
TSHttpTxnConnectTimeoutSet(TSHttpTxn txnp, int timeout)
{
  Debug("http_timeout", "setting inactive timeout to %d msec via API", timeout);
  HttpTransact::State *s = &(((HttpSM *) txnp)->t_state);
  s->api_txn_connect_timeout_value = timeout;
  return 1;
}

/**
 * timeout is in msec
 * overrides as proxy.config.dns.lookup_timeout
**/
int
TSHttpTxnDNSTimeoutSet(TSHttpTxn txnp, int timeout)
{
  Debug("http_timeout", "setting DNS timeout to %d msec via API", timeout);
  HttpTransact::State *s = &(((HttpSM *) txnp)->t_state);
  s->api_txn_dns_timeout_value = timeout;
  return 1;
}


/**
 * timeout is in msec
 * overrides as proxy.config.http.transaction_no_activity_timeout_out
**/
int
TSHttpTxnNoActivityTimeoutSet(TSHttpTxn txnp, int timeout)
{
  Debug("http_timeout", "setting DNS timeout to %d msec via API", timeout);
  HttpTransact::State *s = &(((HttpSM *) txnp)->t_state);
  s->api_txn_no_activity_timeout_value = timeout;
  return 1;
}

int
TSHttpTxnCacheLookupSkip(TSHttpTxn txnp)
{
  HttpTransact::State *s = &(((HttpSM *) txnp)->t_state);
  s->api_skip_cache_lookup = true;
  return 1;
}

int
TSHttpTxnServerRespNoStore(TSHttpTxn txnp)
{
  HttpTransact::State *s = &(((HttpSM *) txnp)->t_state);
  s->api_server_response_no_store = true;
  return 1;
}

int
TSHttpTxnServerRespIgnore(TSHttpTxn txnp)
{
  HttpTransact::State *s = &(((HttpSM *) txnp)->t_state);
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
TSHttpTxnShutDown(TSHttpTxn txnp, TSEvent event)
{
  if (event == TS_EVENT_HTTP_TXN_CLOSE)
    return 0;

  HttpTransact::State *s = &(((HttpSM *) txnp)->t_state);
  s->api_http_sm_shutdown = true;
  return 1;
}

int
TSHttpTxnAborted(TSHttpTxn txnp)
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
  // there can be the case of transformation error.
  return 0;
}

void
TSHttpTxnSetReqCacheableSet(TSHttpTxn txnp)
{
  HttpSM* sm = (HttpSM*)txnp;
  sm->t_state.api_req_cacheable = true;
}

void
TSHttpTxnSetRespCacheableSet(TSHttpTxn txnp)
{
  HttpSM* sm = (HttpSM*)txnp;
  sm->t_state.api_resp_cacheable = true;
}

int
TSHttpTxnClientReqIsServerStyle(TSHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  return (sm->t_state.hdr_info.client_req_is_server_style);
}

int
TSHttpTxnOverwriteExpireTime(TSHttpTxn txnp, time_t expire_time)
{
  HttpTransact::State *s = &(((HttpSM *) txnp)->t_state);
  s->plugin_set_expire_time = expire_time;
  return 1;
}

int
TSHttpTxnUpdateCachedObject(TSHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State *s = &(sm->t_state);
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
TSHttpTxnTransformRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
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
TSHttpTxnClientIPGet(TSHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  return sm->t_state.client_info.ip;
}

int
TSHttpTxnClientIncomingPortGet(TSHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
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
TSHttpTxnServerIPGet(TSHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return 0;
  }
  HttpSM *sm = (HttpSM *) txnp;
  return sm->t_state.server_info.ip;
}

unsigned int
TSHttpTxnNextHopIPGet(TSHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
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
TSHttpTxnNextHopPortGet(TSHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  int port = 0;

  if (sm && sm->t_state.current.server) {
    port = sm->t_state.current.server->port;
  }

  return port;
}



TSReturnCode
TSHttpTxnErrorBodySet(TSHttpTxn txnp, char *buf, int buflength, char *mimetype)
{
  if ((sdk_sanity_check_txn(txnp) != TS_SUCCESS) || (buf == NULL)) {
    return TS_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;

  sm->t_state.internal_msg_buffer = buf;
  sm->t_state.internal_msg_buffer_type = mimetype;
  sm->t_state.internal_msg_buffer_size = buflength;
  sm->t_state.internal_msg_buffer_fast_allocator_size = -1;
  return TS_SUCCESS;
}

void
TSHttpTxnServerRequestBodySet(TSHttpTxn txnp, char *buf, int64 buflength)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State *s = &(sm->t_state);

  if (buf == NULL || buflength <= 0 || s->method != HTTP_WKSIDX_GET)
    return;

  if (s->internal_msg_buffer)
    HttpTransact::free_internal_msg_buffer(s->internal_msg_buffer, s->internal_msg_buffer_fast_allocator_size);

  s->api_server_request_body_set = true;
  s->internal_msg_buffer = buf;
  s->internal_msg_buffer_size = buflength;
  s->internal_msg_buffer_fast_allocator_size = -1;
}

TSReturnCode
TSHttpTxnParentProxyGet(TSHttpTxn txnp, char **hostname, int *port)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;

  *hostname = sm->t_state.api_info.parent_proxy_name;
  *port = sm->t_state.api_info.parent_proxy_port;
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnParentProxySet(TSHttpTxn txnp, char *hostname, int port)
{
  if ((sdk_sanity_check_txn(txnp) != TS_SUCCESS) || (hostname == NULL) || (port <= 0)) {
    return TS_ERROR;
  }

  HttpSM *sm = (HttpSM *) txnp;

  sm->t_state.api_info.parent_proxy_name = sm->t_state.arena.str_store(hostname, strlen(hostname));
  sm->t_state.api_info.parent_proxy_port = port;
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnUntransformedRespCache(TSHttpTxn txnp, int on)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;

  sm->t_state.api_info.cache_untransformed = (on ? true : false);
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnTransformedRespCache(TSHttpTxn txnp, int on)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  HttpSM *sm = (HttpSM *) txnp;

  sm->t_state.api_info.cache_transformed = (on ? true : false);
  return TS_SUCCESS;
}


class TSHttpSMCallback:public Continuation
{
public:
  TSHttpSMCallback(HttpSM *sm, TSEvent event)
  :Continuation(sm->mutex), m_sm(sm), m_event(event)
  {
    SET_HANDLER(&TSHttpSMCallback::event_handler);
  }

  int event_handler(int, void *)
  {
    m_sm->state_api_callback((int) m_event, 0);
    delete this;
    return 0;
  }

private:
  HttpSM *m_sm;
  TSEvent m_event;
};


//----------------------------------------------------------------------------
TSIOBufferReader
TSCacheBufferReaderGet(TSCacheTxn txnp)
{
  NewCacheVC *vc = (NewCacheVC *) txnp;

  return vc->getBufferReader();
}


//----------------------------------------------------------------------------
//TSReturnCode
//TSCacheBufferInfoGet(TSHttpTxn txnp, void **buffer, uint64 *length, uint64 *offset)
TSReturnCode
TSCacheBufferInfoGet(TSCacheTxn txnp, uint64 *length, uint64 *offset)
{
  NewCacheVC *vc = (NewCacheVC *) txnp;

  vc->getCacheBufferInfo(length, offset);

  return TS_SUCCESS;
}



//----------------------------------------------------------------------------
TSReturnCode
TSHttpCacheReenable(TSCacheTxn txnp, const TSEvent event, const void *data, const uint64 size)
{
  Debug("cache_plugin", "[TSHttpCacheReenable] event id: %d data: %lX size: %llu", event, (unsigned long) data, size);
  //bool calledUser = 0;
  NewCacheVC *vc = (NewCacheVC *) txnp;
  if (vc->isClosed()) {
    return TS_SUCCESS;
  }
  //vc->setCtrlInPlugin(false);
  switch (event) {

  case TS_EVENT_CACHE_READ_READY:
  case TS_EVENT_CACHE_READ_COMPLETE:  // read
    Debug("cache_plugin", "[TSHttpCacheReenable] cache_read");
    if (data != 0) {

      // set CacheHTTPInfo in the VC
      //vc->setCacheHttpInfo(data, size);

      //vc->getTunnel()->append_message_to_producer_buffer(vc->getTunnel()->get_producer(vc),(const char*)data,size);
      //           HTTPInfo *cacheInfo;
      //            vc->get_http_info(&cacheInfo);
      //unsigned int doc_size = cacheInfo->object_size_get();
      bool retVal = vc->setRangeAndSize(size);
      //TSMutexLock(vc->getTunnel()->mutex);
      vc->getTunnel()->get_producer(vc)->read_buffer->write((const char *) data, size);
      if (retVal) {
        vc->getTunnel()->get_producer(vc)->read_buffer->write("\r\n", 2);
        vc->add_boundary(true);
      }
      Debug("cache_plugin", "[TSHttpCacheReenable] cache_read ntodo %d", vc->getVio()->ntodo());
      if (vc->getVio()->ntodo() > 0)
        vc->getTunnel()->handleEvent(VC_EVENT_READ_READY, vc->getVio());
      else
        vc->getTunnel()->handleEvent(VC_EVENT_READ_COMPLETE, vc->getVio());
      //TSMutexUnlock(vc->getTunnel()->mutex);
    } else {
      // not in cache
      //TSMutexLock(vc->getTunnel()->mutex);
      vc->getTunnel()->handleEvent(VC_EVENT_ERROR, vc->getVio());
      //TSMutexUnlock(vc->getTunnel()->mutex);
    }

    break;
  case TS_EVENT_CACHE_LOOKUP_COMPLETE:
    Debug("cache_plugin", "[TSHttpCacheReenable] cache_lookup_complete");
    if (data == 0 || !vc->completeCacheHttpInfo(data, size)) {
      Debug("cache_plugin", "[TSHttpCacheReenable] open read failed");
      //TSMutexLock(vc->getCacheSm()->mutex);
      vc->getCacheSm()->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -ECACHE_NO_DOC);
      //TSMutexUnlock(vc->getCacheSm()->mutex);
      break;
    }
    Debug("cache_plugin", "[TSHttpCacheReenable] we have data");
    //TSMutexLock(vc->getCacheSm()->mutex);
    vc->getCacheSm()->handleEvent(CACHE_EVENT_OPEN_READ, (void *) vc);
    //TSMutexUnlock(vc->getCacheSm()->mutex);
    break;
  case TS_EVENT_CACHE_LOOKUP_READY:
    Debug("cache_plugin", "[TSHttpCacheReenable] cache_lookup_ready");
    if (data == 0 || !vc->appendCacheHttpInfo(data, size)) {
      Debug("cache_plugin", "[TSHttpCacheReenable] open read failed");
      // not in cache, free the vc
      //TSMutexLock(vc->getCacheSm()->mutex);
      vc->getCacheSm()->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -ECACHE_NO_DOC);
      //TSMutexUnlock(vc->getCacheSm()->mutex);
    }
    break;
  case TS_EVENT_CACHE_WRITE:  // write
  case TS_EVENT_CACHE_WRITE_HEADER:
    {
      Debug("cache_plugin", "[TSHttpCacheReenable] cache_write");
      if (vc->getState() == NewCacheVC::NEW_CACHE_WRITE_HEADER && vc->getVio()->ntodo() <= 0) {
        //vc->getCacheSm()->handleEvent(VC_EVENT_WRITE_COMPLETE, vc->getVio());
        Debug("cache_plugin", "[TSHttpCacheReenable] NewCacheVC::NEW_CACHE_WRITE_HEADER");
        // writing header
        // do nothing
      } else {
        vc->setTotalObjectSize(size);
        vc->getVio()->ndone = size;
        if (vc->getVio()->ntodo() <= 0) {
          //TSMutexLock(vc->getCacheSm()->mutex);
          vc->getTunnel()->handleEvent(VC_EVENT_WRITE_COMPLETE, vc->getVio());
          //TSMutexUnlock(vc->getCacheSm()->mutex);
        } else {
          //TSMutexLock(vc->getCacheSm()->mutex);
          vc->getTunnel()->handleEvent(VC_EVENT_WRITE_READY, vc->getVio());
          //TSMutexUnlock(vc->getCacheSm()->mutex);
        }
      }
    }
    break;

  case TS_EVENT_CACHE_DELETE:
    break;

    // handle read_ready, read_complete, write_ready, write_complete
    // read_failure, write_failure
  case TS_EVENT_CACHE_CLOSE:
    //do nothing
    break;

  default:
    break;
  }

  return TS_SUCCESS;
}


//----------------------------------------------------------------------------
TSReturnCode
TSHttpTxnReenable(TSHttpTxn txnp, TSEvent event)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return TS_ERROR;
  }

  HttpSM *sm = (HttpSM *) txnp;
  EThread *eth = this_ethread();
  // If this function is being executed on a thread created by the API
  // which is DEDICATED, the continuation needs to be called back on a
  // REGULAR thread.
  if (eth->tt != REGULAR) {
    eventProcessor.schedule_imm(NEW(new TSHttpSMCallback(sm, event)), ET_NET);
  } else {
    MUTEX_TRY_LOCK(trylock, sm->mutex, eth);
    if (!trylock) {
      eventProcessor.schedule_imm(NEW(new TSHttpSMCallback(sm, event)), ET_NET);
    } else {
      sm->state_api_callback((int) event, 0);
    }
  }
  return TS_SUCCESS;
}

int
TSHttpTxnGetMaxArgCnt(void)
{
  return HTTP_TRANSACT_STATE_MAX_USER_ARG;
}

TSReturnCode
TSHttpTxnSetArg(TSHttpTxn txnp, int arg_idx, void *arg)
{
  if (sdk_sanity_check_txn(txnp) == TS_SUCCESS && arg_idx >= 0 && arg_idx < HTTP_TRANSACT_STATE_MAX_USER_ARG) {
    HttpSM *sm = (HttpSM *) txnp;
    sm->t_state.user_args[arg_idx] = arg;
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

TSReturnCode
TSHttpTxnGetArg(TSHttpTxn txnp, int arg_idx, void **argp)
{
  if (sdk_sanity_check_txn(txnp) == TS_SUCCESS && arg_idx >= 0 && arg_idx < HTTP_TRANSACT_STATE_MAX_USER_ARG && argp) {
    HttpSM *sm = (HttpSM *) txnp;
    *argp = sm->t_state.user_args[arg_idx];
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

TSReturnCode
TSHttpTxnSetHttpRetStatus(TSHttpTxn txnp, TSHttpStatus http_retstatus)
{
  if (sdk_sanity_check_txn(txnp) == TS_SUCCESS) {
    HttpSM *sm = (HttpSM *) txnp;
    sm->t_state.http_return_code = (HTTPStatus) http_retstatus;
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

int
TSHttpTxnGetMaxHttpRetBodySize(void)
{
  return HTTP_TRANSACT_STATE_MAX_XBUF_SIZE;
}

TSReturnCode
TSHttpTxnSetHttpRetBody(TSHttpTxn txnp, const char *body_msg, int plain_msg_flag)
{
  if (sdk_sanity_check_txn(txnp) == TS_SUCCESS) {
    HttpSM *sm = (HttpSM *) txnp;
    HttpTransact::State *s = &(sm->t_state);
    s->return_xbuf_size = 0;
    s->return_xbuf[0] = 0;
    s->return_xbuf_plain = false;
    if (body_msg) {
      strncpy(s->return_xbuf, body_msg, HTTP_TRANSACT_STATE_MAX_XBUF_SIZE - 1);
      s->return_xbuf[HTTP_TRANSACT_STATE_MAX_XBUF_SIZE - 1] = 0;
      s->return_xbuf_size = strlen(s->return_xbuf);
      s->return_xbuf_plain = plain_msg_flag;
    }
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

/* for Media-IXT mms over http */
int
TSHttpTxnCntl(TSHttpTxn txnp, TSHttpCntlType cntl, void *data)
{
  HttpSM *sm = (HttpSM *) txnp;

  switch (cntl) {
  case TS_HTTP_CNTL_GET_LOGGING_MODE:
    {
      if (data == NULL) {
        return 0;
      }

      intptr_t *rptr = (intptr_t *) data;

      if (sm->t_state.api_info.logging_enabled) {
        *rptr = (intptr_t) TS_HTTP_CNTL_ON;
      } else {
        *rptr = (intptr_t) TS_HTTP_CNTL_OFF;
      }

      return 1;
    }

  case TS_HTTP_CNTL_SET_LOGGING_MODE:
    if (data != TS_HTTP_CNTL_ON && data != TS_HTTP_CNTL_OFF) {
      return 0;
    } else {
      sm->t_state.api_info.logging_enabled = (bool) data;
      return 1;
    }
    break;

  case TS_HTTP_CNTL_GET_INTERCEPT_RETRY_MODE:
    {
      if (data == NULL) {
        return 0;
      }

      intptr_t *rptr = (intptr_t *) data;

      if (sm->t_state.api_info.retry_intercept_failures) {
        *rptr = (intptr_t) TS_HTTP_CNTL_ON;
      } else {
        *rptr = (intptr_t) TS_HTTP_CNTL_OFF;
      }

      return 1;
    }

  case TS_HTTP_CNTL_SET_INTERCEPT_RETRY_MODE:
    if (data != TS_HTTP_CNTL_ON && data != TS_HTTP_CNTL_OFF) {
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

/* This is kinda horky, we have to use TSServerState instead of
   HttpTransact::ServerState_t, otherwise we have a prototype
   mismatch in the public ts/ts.h interfaces. */
TSServerState
TSHttpTxnServerStateGet(TSHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS)
    return TS_SRVSTATE_STATE_UNDEFINED;

  HttpTransact::State *s = &(((HttpSM *) txnp)->t_state);
  return (TSServerState) s->current.state;
}

/* to access all the stats */
int
TSHttpTxnClientReqHdrBytesGet(TSHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->client_request_hdr_bytes;
  return 1;
}

int
TSHttpTxnClientReqBodyBytesGet(TSHttpTxn txnp, int64 *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->client_request_body_bytes;
  return 1;
}

int
TSHttpTxnServerReqHdrBytesGet(TSHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->server_request_hdr_bytes;
  return 1;
}

int
TSHttpTxnServerReqBodyBytesGet(TSHttpTxn txnp, int64 *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->server_request_body_bytes;
  return 1;
}

int
TSHttpTxnServerRespHdrBytesGet(TSHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->server_response_hdr_bytes;
  return 1;
}

int
TSHttpTxnServerRespBodyBytesGet(TSHttpTxn txnp, int64 *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->server_response_body_bytes;
  return 1;
}

int
TSHttpTxnClientRespHdrBytesGet(TSHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->client_response_hdr_bytes;
  return 1;
}

int
TSHttpTxnClientRespBodyBytesGet(TSHttpTxn txnp, int64 *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->client_response_body_bytes;
  return 1;
}

int
TSHttpTxnPushedRespHdrBytesGet(TSHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->pushed_response_hdr_bytes;
  return 1;
}

int
TSHttpTxnPushedRespBodyBytesGet(TSHttpTxn txnp, int *bytes)
{
  HttpSM *sm = (HttpSM *) txnp;
  *bytes = sm->pushed_response_body_bytes;
  return 1;
}

int
TSHttpTxnStartTimeGet(TSHttpTxn txnp, ink_hrtime *start_time)
{
  HttpSM *sm = (HttpSM *) txnp;

  if (sm->milestones.ua_begin == 0)
    return 0;
  else {
    *start_time = sm->milestones.ua_begin;
    return 1;
  }
}

int
TSHttpTxnEndTimeGet(TSHttpTxn txnp, ink_hrtime *end_time)
{
  HttpSM *sm = (HttpSM *) txnp;

  if (sm->milestones.ua_close == 0)
    return 0;
  else {
    *end_time = sm->milestones.ua_close;
    return 1;
  }
}

int
TSHttpTxnCachedRespTimeGet(TSHttpTxn txnp, time_t *resp_time)
{
  HttpSM *sm = (HttpSM *) txnp;
  HTTPInfo *cached_obj = sm->t_state.cache_info.object_read;

  if (cached_obj == NULL || !cached_obj->valid())
    return 0;

  *resp_time = cached_obj->response_received_time_get();
  return 1;
}

int
TSHttpTxnLookingUpTypeGet(TSHttpTxn txnp)
{
  HttpSM *sm = (HttpSM *) txnp;
  HttpTransact::State *s = &(sm->t_state);

  return (int) (s->current.request_to);
}

int
TSHttpCurrentClientConnectionsGet(int *num_connections)
{
  int64 S;

  HTTP_READ_DYN_SUM(http_current_client_connections_stat, S);
  *num_connections = (int) S;
  return 1;
}

int
TSHttpCurrentActiveClientConnectionsGet(int *num_connections)
{
  int64 S;

  HTTP_READ_DYN_SUM(http_current_active_client_connections_stat, S);
  *num_connections = (int) S;
  return 1;
}

int
TSHttpCurrentIdleClientConnectionsGet(int *num_connections)
{
  int64 total = 0;
  int64 active = 0;

  HTTP_READ_DYN_SUM(http_current_client_connections_stat, total);
  HTTP_READ_DYN_SUM(http_current_active_client_connections_stat, active);

  if (total >= active) {
    *num_connections = total - active;
    return 1;
  }

  return 0;
}

int
TSHttpCurrentCacheConnectionsGet(int *num_connections)
{
  int64 S;

  HTTP_READ_DYN_SUM(http_current_cache_connections_stat, S);
  *num_connections = (int) S;
  return 1;
}

int
TSHttpCurrentServerConnectionsGet(int *num_connections)
{
  int64 S;

  HTTP_READ_GLOBAL_DYN_SUM(http_current_server_connections_stat, S);
  *num_connections = (int) S;
  return 1;
}


/* HTTP alternate selection */

TSReturnCode
TSHttpAltInfoClientReqGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *obj)
{
  HttpAltInfo *info = (HttpAltInfo *) infop;

  if (sdk_sanity_check_alt_info(infop) != TS_SUCCESS) {
    return TS_ERROR;
  }
  *bufp = &info->m_client_req;
  *obj = info->m_client_req.m_http;
  if (sdk_sanity_check_mbuffer(*bufp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

TSReturnCode
TSHttpAltInfoCachedReqGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *obj)
{
  HttpAltInfo *info = (HttpAltInfo *) infop;
  if (sdk_sanity_check_alt_info(infop) != TS_SUCCESS) {
    return TS_ERROR;
  }
  *bufp = &info->m_cached_req;
  *obj = info->m_cached_req.m_http;
  if (sdk_sanity_check_mbuffer(*bufp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

TSReturnCode
TSHttpAltInfoCachedRespGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *obj)
{
  HttpAltInfo *info = (HttpAltInfo *) infop;
  if (sdk_sanity_check_alt_info(infop) != TS_SUCCESS) {
    return TS_ERROR;
  }
  *bufp = &info->m_cached_resp;
  *obj = info->m_cached_resp.m_http;
  if (sdk_sanity_check_mbuffer(*bufp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

TSReturnCode
TSHttpAltInfoQualitySet(TSHttpAltInfo infop, float quality)
{
  HttpAltInfo *info = (HttpAltInfo *) infop;
  if (sdk_sanity_check_alt_info(infop) != TS_SUCCESS) {
    return TS_ERROR;
  }
  info->m_qvalue = quality;
  return TS_SUCCESS;
}

extern HttpAccept *plugin_http_accept;

TSReturnCode
TSHttpConnect(unsigned int log_ip, int log_port, TSVConn *vc)
{
#ifdef DEBUG
  if (vc == NULL) {
    return TS_ERROR;
  }
  if ((log_ip == 0) || (log_port <= 0)) {
    *vc = NULL;
    return TS_ERROR;
  }
#endif
  if (plugin_http_accept) {
    PluginVCCore *new_pvc = PluginVCCore::alloc();
    new_pvc->set_active_addr(log_ip, log_port);
    new_pvc->set_accept_cont(plugin_http_accept);
    PluginVC *return_vc = new_pvc->connect();

    if(return_vc !=NULL) {
      PluginVC* other_side = return_vc->get_other_side();
      if(other_side != NULL) {
        other_side->set_is_internal_request(true);
      }
    }

    *vc = (TSVConn) return_vc;
    return ((return_vc) ? TS_SUCCESS : TS_ERROR);
  } else {
    *vc = NULL;
    return TS_ERROR;
  }
}

/* Actions */

// Currently no error handling necessary, actionp can be anything.
TSReturnCode
TSActionCancel(TSAction actionp)
{
  Action *a;
  INKContInternal *i;

/* This is a hack. SHould be handled in ink_types */
  if ((uintptr_t) actionp & 0x1) {
    a = (Action *) ((uintptr_t) actionp - 1);
    i = (INKContInternal *) a->continuation;
    i->handle_event_count(EVENT_IMMEDIATE);
  } else {
    a = (Action *) actionp;
  }

  a->cancel();
  return TS_SUCCESS;
}

// Currently no error handling necessary, actionp can be anything.
int
TSActionDone(TSAction actionp)
{
  Action *a = (Action *) actionp;
  return (a == ACTION_RESULT_DONE);
}

/* Connections */

/* Deprectated.
   Do not use this API.
   The reason is even if VConn is created using this API, it is still useless.
   For example, if we do TSVConnRead, the read operation returns read_vio, if
   we do TSVIOReenable (read_vio), it actually calls:
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
TSVConn
TSVConnCreate(TSEventFunc event_funcp, TSMutex mutexp)
{
  if (mutexp == NULL) {
    mutexp = (TSMutex) new_ProxyMutex();
  }

  if (sdk_sanity_check_mutex(mutexp) != TS_SUCCESS)
    return (TSVConn) TS_ERROR_PTR;

  INKVConnInternal *i = INKVConnAllocator.alloc();
#ifdef DEBUG
  if (i == NULL)
    return (TSVConn) TS_ERROR_PTR;
#endif
  i->init(event_funcp, mutexp);
  return (TSCont) i;
}


TSVIO
TSVConnReadVIOGet(TSVConn connp)
{
  if (sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS)
    return (TSCont) TS_ERROR_PTR;

  VConnection *vc = (VConnection *) connp;
  TSVIO data;

  if (!vc->get_data(TS_API_DATA_READ_VIO, &data)) {
    // don't assert, simple return error_ptr
    // ink_assert (!"not reached");
    return (TSVIO) TS_ERROR_PTR;
  }
  return data;
}

TSVIO
TSVConnWriteVIOGet(TSVConn connp)
{
  if (sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS)
    return (TSCont) TS_ERROR_PTR;

  VConnection *vc = (VConnection *) connp;
  TSVIO data;

  if (!vc->get_data(TS_API_DATA_WRITE_VIO, &data)) {
    // don't assert, simple return error_ptr
    // ink_assert (!"not reached");
    return (TSVIO) TS_ERROR_PTR;
  }
  return data;
}

int
TSVConnClosedGet(TSVConn connp)
{
  if (sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS)
    return TS_ERROR;

  VConnection *vc = (VConnection *) connp;
  int data;

  if (!vc->get_data(TS_API_DATA_CLOSED, &data)) {
    // don't assert, simple return error_ptr
    // ink_assert (!"not reached");
    return TS_ERROR;
  }
  return data;
}

TSVIO
TSVConnRead(TSVConn connp, TSCont contp, TSIOBuffer bufp, int64 nbytes)
{
  if ((sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(bufp) != TS_SUCCESS) || (nbytes < 0))
    return (TSCont) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  VConnection *vc = (VConnection *) connp;
  return vc->do_io(VIO::READ, (INKContInternal *) contp, nbytes, (MIOBuffer *) bufp);
}

TSVIO
TSVConnWrite(TSVConn connp, TSCont contp, TSIOBufferReader readerp, int64 nbytes)
{
  if ((sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(readerp) != TS_SUCCESS) || (nbytes < 0))
    return (TSCont) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  VConnection *vc = (VConnection *) connp;
  return vc->do_io_write((INKContInternal *) contp, nbytes, (IOBufferReader *) readerp);
}

TSReturnCode
TSVConnClose(TSVConn connp)
{
  if (sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS)
    return TS_ERROR;

  VConnection *vc = (VConnection *) connp;
  vc->do_io_close();
  return TS_SUCCESS;
}

TSReturnCode
TSVConnAbort(TSVConn connp, int error)
{
  if (sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS)
    return TS_ERROR;

  VConnection *vc = (VConnection *) connp;
  vc->do_io_close(error);
  return TS_SUCCESS;
}

TSReturnCode
TSVConnShutdown(TSVConn connp, int read, int write)
{
  if (sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS)
    return TS_ERROR;

  VConnection *vc = (VConnection *) connp;

  if (read && write) {
    vc->do_io_shutdown(IO_SHUTDOWN_READWRITE);
  } else if (read) {
    vc->do_io_shutdown(IO_SHUTDOWN_READ);
  } else if (write) {
    vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
  }
  return TS_SUCCESS;
}

TSReturnCode
TSVConnCacheObjectSizeGet(TSVConn connp, int64 *obj_size)
{
  if ((sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS) || (obj_size == NULL))
    return TS_ERROR;

  CacheVC *vc = (CacheVC *) connp;
  *obj_size = vc->get_object_size();
  return TS_SUCCESS;
}

void
TSVConnCacheHttpInfoSet(TSVConn connp, TSCacheHttpInfo infop)
{
  CacheVC *vc = (CacheVC *) connp;
  if (vc->base_stat == cache_scan_active_stat)
    vc->set_http_info((CacheHTTPInfo *) infop);
}

/* Transformations */

TSVConn
TSTransformCreate(TSEventFunc event_funcp, TSHttpTxn txnp)
{
  return TSVConnCreate(event_funcp, TSContMutexGet(txnp));
}

TSVConn
TSTransformOutputVConnGet(TSVConn connp)
{
  if (sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS) {
    return (TSVConn) TS_ERROR_PTR;
  }
  VConnection *vc = (VConnection *) connp;
  TSVConn data;

  if (!vc->get_data(TS_API_DATA_OUTPUT_VC, &data)) {
    ink_assert(!"not reached");
  }
  return data;
}

TSReturnCode
TSHttpTxnServerIntercept(TSCont contp, TSHttpTxn txnp)
{
  if ((sdk_sanity_check_txn(txnp) != TS_SUCCESS) || (sdk_sanity_check_continuation(contp) != TS_SUCCESS)) {
    return TS_ERROR;
  }
  HttpSM *http_sm = (HttpSM *) txnp;
  INKContInternal *i = (INKContInternal *) contp;
#ifdef DEBUG
  if (i->mutex == NULL) {
    return TS_ERROR;
  }
#endif
  http_sm->plugin_tunnel_type = HTTP_PLUGIN_AS_SERVER;
  http_sm->plugin_tunnel = PluginVCCore::alloc();
  http_sm->plugin_tunnel->set_accept_cont(i);

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnIntercept(TSCont contp, TSHttpTxn txnp)
{
  if ((sdk_sanity_check_txn(txnp) != TS_SUCCESS) || (sdk_sanity_check_continuation(contp) != TS_SUCCESS)) {
    return TS_ERROR;
  }
  HttpSM *http_sm = (HttpSM *) txnp;
  INKContInternal *i = (INKContInternal *) contp;
#ifdef DEBUG
  if (i->mutex == NULL) {
    return TS_ERROR;
  }
#endif
  http_sm->plugin_tunnel_type = HTTP_PLUGIN_AS_INTERCEPT;
  http_sm->plugin_tunnel = PluginVCCore::alloc();
  http_sm->plugin_tunnel->set_accept_cont(i);

  return TS_SUCCESS;
}

/* Net VConnections */
void
TSVConnInactivityTimeoutSet(TSVConn connp, int timeout)
{
  NetVConnection *vc = (NetVConnection *) connp;
  vc->set_inactivity_timeout(timeout);
}

void
TSVConnInactivityTimeoutCancel(TSVConn connp)
{
  NetVConnection *vc = (NetVConnection *) connp;
  vc->cancel_inactivity_timeout();
}

void
TSVConnActiveTimeoutSet(TSVConn connp, int timeout)
{
  NetVConnection *vc = (NetVConnection *) connp;
  vc->set_active_timeout(timeout);
}

void
TSVConnActiveTimeoutCancel(TSVConn connp)
{
  NetVConnection *vc = (NetVConnection *) connp;
  vc->cancel_active_timeout();
}

TSReturnCode
TSNetVConnRemoteIPGet(TSVConn connp, unsigned int *ip)
{
  if ((sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS) || (ip == NULL)) {
    return TS_ERROR;
  }

  NetVConnection *vc = (NetVConnection *) connp;
  *ip = vc->get_remote_ip();
  return TS_SUCCESS;
}

TSReturnCode
TSNetVConnRemotePortGet(TSVConn connp, int *port)
{
  if ((sdk_sanity_check_iocore_structure(connp) != TS_SUCCESS) || (port == NULL)) {
    return TS_ERROR;
  }

  NetVConnection *vc = (NetVConnection *) connp;
  *port = vc->get_remote_port();
  return TS_SUCCESS;
}

TSAction
TSNetConnect(TSCont contp, unsigned int ip, int port)
{
  if ((sdk_sanity_check_continuation(contp) != TS_SUCCESS) || (ip == 0) || (port == 0))
    return (TSAction) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  INKContInternal *i = (INKContInternal *) contp;
  return (TSAction) netProcessor.connect_re(i, ip, port);
}

TSAction
TSNetAccept(TSCont contp, int port)
{
  if ((sdk_sanity_check_continuation(contp) != TS_SUCCESS) || (port == 0))
    return (TSAction) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  INKContInternal *i = (INKContInternal *) contp;
  return (TSAction) netProcessor.accept(i, port);
}

/* DNS Lookups */
TSAction
TSHostLookup(TSCont contp, char *hostname, int namelen)
{
  if ((sdk_sanity_check_continuation(contp) != TS_SUCCESS) || (hostname == NULL) || (namelen == 0))
    return (TSAction) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  INKContInternal *i = (INKContInternal *) contp;
  return (TSAction) hostDBProcessor.getbyname_re(i, hostname, namelen);
}

TSReturnCode
TSHostLookupResultIPGet(TSHostLookupResult lookup_result, unsigned int *ip)
{
  if ((sdk_sanity_check_hostlookup_structure(lookup_result) != TS_SUCCESS) || (ip == NULL)) {
    return TS_ERROR;
  }

  *ip = ((HostDBInfo *) lookup_result)->ip();
  return TS_SUCCESS;
}

/*
 * checks if the cache is ready
 */

/* Only TSCacheReady exposed in SDK. No need of TSCacheDataTypeReady */
/* because SDK cache API supports only the data type: NONE */
TSReturnCode
TSCacheReady(int *is_ready)
{
  return TSCacheDataTypeReady(TS_CACHE_DATA_TYPE_NONE, is_ready);
}

/* Private API (used by Mixt) */
TSReturnCode
TSCacheDataTypeReady(TSCacheDataType type, int *is_ready)
{
#ifdef DEBUG
  if (is_ready == NULL) {
    return TS_ERROR;
  }
#endif
  CacheFragType frag_type;

  switch (type) {
  case TS_CACHE_DATA_TYPE_NONE:
    frag_type = CACHE_FRAG_TYPE_NONE;
    break;
  case TS_CACHE_DATA_TYPE_OTHER:      /* other maps to http */
  case TS_CACHE_DATA_TYPE_HTTP:
    frag_type = CACHE_FRAG_TYPE_HTTP;
    break;
  default:
    *is_ready = 0;
    return TS_ERROR;
  }

  *is_ready = cacheProcessor.IsCacheReady(frag_type);
  return TS_SUCCESS;
}

/* Cache VConnections */
TSAction
TSCacheRead(TSCont contp, TSCacheKey key)
{
  if ((sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS) || (sdk_sanity_check_cachekey(key) != TS_SUCCESS))
    return (TSAction) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  CacheInfo *info = (CacheInfo *) key;
  Continuation *i = (INKContInternal *) contp;

  return (TSAction)cacheProcessor.open_read(i, &info->cache_key, info->frag_type, info->hostname, info->len);
}

TSAction
TSCacheWrite(TSCont contp, TSCacheKey key)
{
  if ((sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS) || (sdk_sanity_check_cachekey(key) != TS_SUCCESS))
    return (TSAction) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  CacheInfo *info = (CacheInfo *) key;
  Continuation *i = (INKContInternal *) contp;
  return (TSAction)cacheProcessor.open_write(i, &info->cache_key, info->frag_type, 0, false, info->pin_in_cache,
                                             info->hostname, info->len);
}

TSAction
TSCacheRemove(TSCont contp, TSCacheKey key)
{
  if ((sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS) || (sdk_sanity_check_cachekey(key) != TS_SUCCESS))
    return (TSAction) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  CacheInfo *info = (CacheInfo *) key;
  INKContInternal *i = (INKContInternal *) contp;
  return (TSAction)cacheProcessor.remove(i, &info->cache_key, info->frag_type, true, false, info->hostname, info->len);
}

TSAction
TSCacheScan(TSCont contp, TSCacheKey key, int KB_per_second)
{
  if ((sdk_sanity_check_iocore_structure(contp) != TS_SUCCESS) || (key && sdk_sanity_check_cachekey(key) != TS_SUCCESS))
    return (TSAction) TS_ERROR_PTR;

  FORCE_PLUGIN_MUTEX(contp);
  INKContInternal *i = (INKContInternal *) contp;
  if (key) {
    CacheInfo *info = (CacheInfo *) key;
    return (TSAction)
      cacheProcessor.scan(i, info->hostname, info->len, KB_per_second);
  }
  return cacheProcessor.scan(i, 0, 0, KB_per_second);
}



/* IP/User Name Cache */

/*int
TSUserNameCacheInsert(TSCont contp, unsigned long ip, const char *userName)
{
  FORCE_PLUGIN_MUTEX(contp);
  int ret;
  ret = ipToUserNameCacheProcessor.insertCacheEntry((Continuation *) contp, ip, userName);

  if (ret == EVENT_DONE)
    return TS_EVENT_IMMEDIATE;
  else if (ret == EVENT_ERROR)
    return TS_EVENT_ERROR;
  else
    return TS_EVENT_CONTINUE;

}


int
TSUserNameCacheLookup(TSCont contp, unsigned long ip, char *userName)
{
  FORCE_PLUGIN_MUTEX(contp);
  int ret;
  ret = ipToUserNameCacheProcessor.lookupCacheEntry((Continuation *) contp, ip, userName, 3);

  if (ret == EVENT_DONE)
    return TS_EVENT_IMMEDIATE;
  else if (ret == EVENT_ERROR)
    return TS_EVENT_ERROR;
  else
    return TS_EVENT_CONTINUE;

}

int
TSUserNameCacheDelete(TSCont contp, unsigned long ip)
{
  FORCE_PLUGIN_MUTEX(contp);
  int ret;
  ret = ipToUserNameCacheProcessor.deleteCacheEntry((Continuation *) contp, ip);

  if (ret == EVENT_DONE)
    return TS_EVENT_IMMEDIATE;
  else if (ret == EVENT_ERROR)
    return TS_EVENT_ERROR;
  else
    return TS_EVENT_CONTINUE;

}*/


//inkcoreapi TSMCOPreload_fp MCOPreload_fp = NULL;

/* Callouts for APIs implemented by shared objects */
/*
int
TSMCOPreload(void *context,    // opaque ptr
              TSCont continuation,     // called w/ progress updates
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

/************************   REC Stats API    **************************/
int
TSStatCreate(const char *the_name, TSStatDataType the_type, TSStatPersistence persist, TSStatSync sync)
{
  int volatile id = ink_atomic_increment(&top_stat, 1);
  RecRawStatSyncCb syncer = RecRawStatSyncCount;

  // TODO: This only supports "int" data types at this point, since the "Raw" stats
  // interfaces only supports integers. Going forward, we could extend either the "Raw"
  // stats APIs, or make non-int use the direct (synchronous) stats APIs (slower).
  if ((sdk_sanity_check_null_ptr((void *)the_name) != TS_SUCCESS) ||
      (sdk_sanity_check_null_ptr((void *)api_rsb) != TS_SUCCESS))
    return TS_ERROR;

  switch (sync) {
  case TS_STAT_SYNC_SUM:
    syncer = RecRawStatSyncSum;
    break;
  case TS_STAT_SYNC_AVG:
    syncer = RecRawStatSyncAvg;
    break;
  case TS_STAT_SYNC_TIMEAVG:
    syncer = RecRawStatSyncHrTimeAvg;
    break;
  default:
    syncer = RecRawStatSyncCount;
    break;
  }
  RecRegisterRawStat(api_rsb, RECT_PLUGIN, the_name, (RecDataT)the_type, RecPersistT(persist), id, syncer);

  return id;
}

TSReturnCode
TSStatIntIncrement(int the_stat, TSMgmtInt amount)
{
  if (sdk_sanity_check_null_ptr((void *)api_rsb) != TS_SUCCESS)
    return TS_ERROR;

  RecIncrRawStat(api_rsb, NULL, the_stat, amount);
  return TS_SUCCESS;
}

TSReturnCode
TSStatIntDecrement(int the_stat, TSMgmtInt amount)
{
  if (sdk_sanity_check_null_ptr((void *)api_rsb) != TS_SUCCESS)
    return TS_ERROR;

  RecDecrRawStat(api_rsb, NULL, the_stat, amount);
  return TS_SUCCESS;
}

TSReturnCode
TSStatIntGet(int the_stat, TSMgmtInt* value)
{
  if (sdk_sanity_check_null_ptr((void *)api_rsb) != TS_SUCCESS)
    return TS_ERROR;

  RecGetGlobalRawStatSum(api_rsb, the_stat, value);
  return TS_SUCCESS;
}

TSReturnCode
TSStatIntSet(int the_stat, TSMgmtInt value)
{
  if (sdk_sanity_check_null_ptr((void *)api_rsb) != TS_SUCCESS)
    return TS_ERROR;

  RecSetGlobalRawStatSum(api_rsb, the_stat, value);
  return TS_SUCCESS;
}

int
TSStatFindName(const char* name)
{
  int id;

  if ((sdk_sanity_check_null_ptr((void *)name) != TS_SUCCESS) ||
      (sdk_sanity_check_null_ptr((void *)api_rsb) != TS_SUCCESS))
    return TS_ERROR;

  if (RecGetRecordOrderAndId(name, NULL, &id) == REC_ERR_OKAY)
    return id;

  return -1;
}


/**************************    Stats API    ****************************/
// THESE APIS ARE DEPRECATED, USE THE REC APIs INSTEAD
// #define ink_sanity_check_stat_structure(_x) TS_SUCCESS

inline TSReturnCode
ink_sanity_check_stat_structure(void *obj)
{
  if (obj == NULL || obj == TS_ERROR_PTR) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

INKStat
INKStatCreate(const char *the_name, INKStatTypes the_type)
{
#ifdef DEBUG
  if (the_name == NULL ||
      the_name == TS_ERROR_PTR || ((the_type != INKSTAT_TYPE_INT64) && (the_type != INKSTAT_TYPE_FLOAT))) {
    return (INKStat) TS_ERROR_PTR;
  }
#endif

  StatDescriptor *n = NULL;

  switch (the_type) {
  case INKSTAT_TYPE_INT64:
    n = StatDescriptor::CreateDescriptor(the_name, (int64) 0);
    break;

  case INKSTAT_TYPE_FLOAT:
    n = StatDescriptor::CreateDescriptor(the_name, (float) 0);
    break;

  default:
    Warning("INKStatCreate given invalid type enumeration!");
    break;
  };

  return n == NULL ? (INKStat) TS_ERROR_PTR : (INKStat) n;
}

TSReturnCode
INKStatIntAddTo(INKStat the_stat, int64 amount)
{
  if (ink_sanity_check_stat_structure(the_stat) != TS_SUCCESS)
    return TS_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->add(amount);
  return TS_SUCCESS;
}

TSReturnCode
INKStatFloatAddTo(INKStat the_stat, float amount)
{
  if (ink_sanity_check_stat_structure(the_stat) != TS_SUCCESS)
    return TS_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->add(amount);
  return TS_SUCCESS;
}

TSReturnCode
INKStatDecrement(INKStat the_stat)
{
  if (ink_sanity_check_stat_structure(the_stat) != TS_SUCCESS)
    return TS_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->decrement();
  return TS_SUCCESS;
}

TSReturnCode
INKStatIncrement(INKStat the_stat)
{
  if (ink_sanity_check_stat_structure(the_stat) != TS_SUCCESS)
    return TS_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->increment();
  return TS_SUCCESS;
}

#if TS_HAS_V2STATS
TSReturnCode
TSStatCreateV2(const char *the_name, uint32_t *stat_num)
{
  if(StatSystemV2::registerStat(the_name, stat_num)) {
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

TSReturnCode
TSStatIncrementV2(uint32_t stat_num, int64 inc_by)
{
  if(StatSystemV2::increment(stat_num, inc_by)) {
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

TSReturnCode
TSStatIncrementByNameV2(const char *stat_name, int64 inc_by)
{
  if(StatSystemV2::increment(stat_name, inc_by)) {
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

TSReturnCode
TSStatDecrementV2(uint32_t stat_num, int64 dec_by)
{
  return TSStatIncrementV2(stat_num, (-1)*dec_by);
}

TSReturnCode
TSStatDecrementByNameV2(const char *stat_name, int64 dec_by)
{
  return TSStatIncrementByNameV2(stat_name, (-1)*dec_by);
}

TSReturnCode
TSStatGetCurrentV2(uint32_t stat_num, int64 *stat_val)
{
  if(StatSystemV2::get_current(stat_num, stat_val))
    return TS_SUCCESS;
  return TS_ERROR;
}

TSReturnCode
TSStatGetCurrentByNameV2(const char *stat_name, int64 *stat_val)
{
  if(StatSystemV2::get_current(stat_name, stat_val))
    return TS_SUCCESS;
  return TS_ERROR;
}

TSReturnCode
TSStatGetV2(uint32_t stat_num, int64 *stat_val)
{
  if(StatSystemV2::get(stat_num, stat_val))
    return TS_SUCCESS;
  return TS_ERROR;
}

TSReturnCode
TSStatGetByNameV2(const char *stat_name, int64 *stat_val)
{
  if(StatSystemV2::get(stat_name, stat_val))
    return TS_SUCCESS;
  return TS_ERROR;
}
#endif

TSReturnCode
INKStatIntGet(INKStat the_stat, int64 *value)
{
  if (ink_sanity_check_stat_structure(the_stat) != TS_SUCCESS)
    return TS_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  *value = statp->int_value();
  return TS_SUCCESS;
}

TSReturnCode
INKStatFloatGet(INKStat the_stat, float *value)
{
  if (ink_sanity_check_stat_structure(the_stat) != TS_SUCCESS)
    return TS_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  *value = statp->flt_value();
  return TS_SUCCESS;
}

TSReturnCode
INKStatIntSet(INKStat the_stat, int64 value)
{
  if (ink_sanity_check_stat_structure(the_stat) != TS_SUCCESS)
    return TS_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->set(value);
  return TS_SUCCESS;
}

TSReturnCode
INKStatFloatSet(INKStat the_stat, float value)
{
  if (ink_sanity_check_stat_structure(the_stat) != TS_SUCCESS)
    return TS_ERROR;

  StatDescriptor *statp = (StatDescriptor *) the_stat;
  statp->set(value);
  return TS_SUCCESS;
}

INKCoupledStat
INKStatCoupledGlobalCategoryCreate(const char *the_name)
{
#ifdef DEBUG
  if (the_name == NULL || the_name == TS_ERROR_PTR)
    return (INKCoupledStat) TS_ERROR_PTR;
#endif

  CoupledStats *category = NEW(new CoupledStats(the_name));
  return (INKCoupledStat) category;
}

INKCoupledStat
INKStatCoupledLocalCopyCreate(const char *the_name, INKCoupledStat global_copy)
{
  if (ink_sanity_check_stat_structure(global_copy) != TS_SUCCESS ||
      sdk_sanity_check_null_ptr((void *) the_name) != TS_SUCCESS)
    return (INKCoupledStat) TS_ERROR_PTR;

  CoupledStatsSnapshot *snap = NEW(new CoupledStatsSnapshot((CoupledStats *) global_copy));

  return (INKCoupledStat) snap;
}

TSReturnCode
INKStatCoupledLocalCopyDestroy(INKCoupledStat stat)
{
  if (ink_sanity_check_stat_structure(stat) != TS_SUCCESS)
    return TS_ERROR;

  CoupledStatsSnapshot *snap = (CoupledStatsSnapshot *) stat;
  if (snap) {
    delete snap;
  }

  return TS_SUCCESS;
}

INKStat
INKStatCoupledGlobalAdd(INKCoupledStat global_copy, const char *the_name, INKStatTypes the_type)
{
  if ((ink_sanity_check_stat_structure(global_copy) != TS_SUCCESS) ||
      sdk_sanity_check_null_ptr((void *) the_name) != TS_SUCCESS ||
      ((the_type != INKSTAT_TYPE_INT64) && (the_type != INKSTAT_TYPE_FLOAT))) {
    return (INKStat) TS_ERROR_PTR;
  }


  CoupledStats *category = (CoupledStats *) global_copy;
  StatDescriptor *n;

  switch (the_type) {
  case INKSTAT_TYPE_INT64:
    n = category->CreateStat(the_name, (int64) 0);
    break;

  case INKSTAT_TYPE_FLOAT:
    n = category->CreateStat(the_name, (float) 0);
    break;

  default:
    n = NULL;
    Warning("INKStatCreate given invalid type enumeration!");
    break;
  };

  return n == NULL ? (INKStat) TS_ERROR_PTR : (INKStat) n;
}

INKStat
INKStatCoupledLocalAdd(INKCoupledStat local_copy, const char *the_name, INKStatTypes the_type)
{
  if ((ink_sanity_check_stat_structure(local_copy) != TS_SUCCESS) ||
      sdk_sanity_check_null_ptr((void *) the_name) != TS_SUCCESS ||
      ((the_type != INKSTAT_TYPE_INT64) && (the_type != INKSTAT_TYPE_FLOAT))) {
    return (INKStat) TS_ERROR_PTR;
  }

  StatDescriptor *n = ((CoupledStatsSnapshot *) local_copy)->fetchNext();
  return n == NULL ? (INKStat) TS_ERROR_PTR : (INKStat) n;
}

TSReturnCode
INKStatsCoupledUpdate(INKCoupledStat local_copy)
{
  if (ink_sanity_check_stat_structure(local_copy) != TS_SUCCESS)
    return TS_ERROR;

  ((CoupledStatsSnapshot *) local_copy)->CommitUpdates();
  return TS_SUCCESS;
}

/**************************   Tracing API   ****************************/
// returns 1 or 0 to indicate whether TS is being run with a debug tag.
int
TSIsDebugTagSet(const char *t)
{
  return (diags->on(t, DiagsTagType_Debug)) ? 1 : 0;
}

// Plugins would use TSDebug just as the TS internal uses Debug
// e.g. TSDebug("plugin-cool", "Snoopy is a cool guy even after %d requests.\n", num_reqs);
void
TSDebug(const char *tag, const char *format_str, ...)
{
  if (diags->on(tag, DiagsTagType_Debug)) {
    va_list ap;
    va_start(ap, format_str);
    diags->print_va(tag, DL_Diag, NULL, NULL, format_str, ap);
    va_end(ap);
  }
}

/**************************   Logging API   ****************************/

TSReturnCode
TSTextLogObjectCreate(const char *filename, int mode, TSTextLogObject *new_object)
{
#ifdef DEBUG
  if (filename == NULL) {
    *new_object = NULL;
    return TS_ERROR;
  }
#endif
  if (mode<0 || mode>= TS_LOG_MODE_INVALID_FLAG) {
    /* specified mode is invalid */
    *new_object = NULL;
    return TS_ERROR;
  }
  TextLogObject *tlog = NEW(new TextLogObject(filename, Log::config->logfile_dir,
                                              (bool) mode & TS_LOG_MODE_ADD_TIMESTAMP,
                                              NULL,
                                              Log::config->rolling_enabled,
                                              Log::config->rolling_interval_sec,
                                              Log::config->rolling_offset_hr,
                                              Log::config->rolling_size_mb));
  if (tlog) {
    int err = (mode & TS_LOG_MODE_DO_NOT_RENAME ?
               Log::config->log_object_manager.manage_api_object(tlog, 0) :
               Log::config->log_object_manager.manage_api_object(tlog));
    if (err != LogObjectManager::NO_FILENAME_CONFLICTS) {
      // error managing log
      delete tlog;
      *new_object = NULL;
      return TS_ERROR;
    }
  } else {
    // error creating log
    *new_object = NULL;
    return TS_ERROR;
  }
  *new_object = (TSTextLogObject) tlog;
  return TS_SUCCESS;
}

TSReturnCode
TSTextLogObjectWrite(TSTextLogObject the_object, char *format, ...)
{
  if ((sdk_sanity_check_iocore_structure(the_object) != TS_SUCCESS)
      || (format == NULL))
    return TS_ERROR;

  TSReturnCode retVal = TS_SUCCESS;

  va_list ap;
  va_start(ap, format);
  switch (((TextLogObject *) the_object)->va_write(format, ap)) {
  case (Log::LOG_OK):
  case (Log::SKIP):
    break;
  case (Log::FULL):
    retVal = TS_ERROR;
    break;
  case (Log::FAIL):
    retVal = TS_ERROR;
    break;
  default:
    ink_debug_assert(!"invalid return code");
  }
  va_end(ap);
  return retVal;
}

TSReturnCode
TSTextLogObjectFlush(TSTextLogObject the_object)
{
  if (sdk_sanity_check_iocore_structure(the_object) != TS_SUCCESS)
    return TS_ERROR;

  ((TextLogObject *) the_object)->force_new_buffer();
  return TS_SUCCESS;
}

TSReturnCode
TSTextLogObjectDestroy(TSTextLogObject the_object)
{
  if (sdk_sanity_check_iocore_structure(the_object) != TS_SUCCESS)
    return TS_ERROR;

  return (Log::config->log_object_manager.unmanage_api_object((TextLogObject *) the_object) ? TS_SUCCESS : TS_ERROR);
}

tsapi TSReturnCode
TSTextLogObjectHeaderSet(TSTextLogObject the_object, const char *header)
{
  if (sdk_sanity_check_iocore_structure(the_object) != TS_SUCCESS)
    return TS_ERROR;

  ((TextLogObject *) the_object)->set_log_file_header(header);
  return TS_SUCCESS;
}

tsapi TSReturnCode
TSTextLogObjectRollingEnabledSet(TSTextLogObject the_object, int rolling_enabled)
{
  if (sdk_sanity_check_iocore_structure(the_object) != TS_SUCCESS)
    return TS_ERROR;

  ((TextLogObject *) the_object)->set_rolling_enabled(rolling_enabled);
  return TS_SUCCESS;
}

tsapi TSReturnCode
TSTextLogObjectRollingIntervalSecSet(TSTextLogObject the_object, int rolling_interval_sec)
{
  if (sdk_sanity_check_iocore_structure(the_object) != TS_SUCCESS)
    return TS_ERROR;

  ((TextLogObject *) the_object)->set_rolling_interval_sec(rolling_interval_sec);
  return TS_SUCCESS;
}

tsapi TSReturnCode
TSTextLogObjectRollingOffsetHrSet(TSTextLogObject the_object, int rolling_offset_hr)
{
  if (sdk_sanity_check_iocore_structure(the_object) != TS_SUCCESS)
    return TS_ERROR;

  ((TextLogObject *) the_object)->set_rolling_offset_hr(rolling_offset_hr);
  return TS_SUCCESS;
}

int
TSHttpTxnClientFdGet(TSHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);
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

TSReturnCode
TSHttpTxnClientRemotePortGet(TSHttpTxn txnp, int *port)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);
  HttpClientSession *cs = (HttpClientSession *) ssnp;
  if (cs == NULL) {
    return TS_ERROR;
  }
  NetVConnection *vc = cs->get_netvc();
  if (vc == NULL) {
    return TS_ERROR;
  }
  // Note: SDK spec specifies this API should return port in network byte order
  // iocore returns it in host byte order. So we do the conversion.
  *port = htons(vc->get_remote_port());
  return TS_SUCCESS;
}

/* IP Lookup */

// This is very suspicious, TSILookup is a (void *), so how on earth
// can we try to delete an instance of it?



void
TSIPLookupNewEntry(TSIPLookup iplu, uint32 addr1, uint32 addr2, void *data)
{
  IpLookup *my_iplu = (IpLookup *) iplu;
  if (my_iplu) {
    my_iplu->NewEntry((ip_addr_t) addr1, (ip_addr_t) addr2, data);
  }
}

int
TSIPLookupMatch(TSIPLookup iplu, uint32 addr, void **data)
{
  void *dummy;
  IpLookup *my_iplu = (IpLookup *) iplu;
  if (!data) {
    data = &dummy;
  }
  return (my_iplu ? my_iplu->Match((ip_addr_t) addr, data) : 0);
}

int
TSIPLookupMatchFirst(TSIPLookup iplu, uint32 addr, TSIPLookupState iplus, void **data)
{
  IpLookup *my_iplu = (IpLookup *) iplu;
  IpLookupState *my_iplus = (IpLookupState *) iplus;
  if (my_iplu && my_iplus && my_iplu->MatchFirst(addr, my_iplus, data)) {
    return 1;
  }
  return 0;
}

int
TSIPLookupMatchNext(TSIPLookup iplu, TSIPLookupState iplus, void **data)
{
  IpLookup *my_iplu = (IpLookup *) iplu;
  IpLookupState *my_iplus = (IpLookupState *) iplus;
  if (my_iplu && my_iplus && my_iplu->MatchNext(my_iplus, data)) {
    return 1;
  }
  return 0;
}

void
TSIPLookupPrint(TSIPLookup iplu, TSIPLookupPrintFunc pf)
{
  IpLookup *my_iplu = (IpLookup *) iplu;
  if (my_iplu) {
    my_iplu->Print((IpLookupPrintFunc) pf);
  }
}

/* Matcher Utils */
char *
TSMatcherReadIntoBuffer(char *file_name, int *file_len)
{
  return readIntoBuffer((char *) file_name, "TSMatcher", file_len);
}

char *
TSMatcherTokLine(char *buffer, char **last)
{
  return tokLine(buffer, last);
}

char *
TSMatcherExtractIPRange(char *match_str, uint32 *addr1, uint32 *addr2)
{
  return (char*)ExtractIpRange(match_str, (ip_addr_t *) addr1, (ip_addr_t *) addr2);
}

TSMatcherLine
TSMatcherLineCreate()
{
  return (void *) xmalloc(sizeof(matcher_line));
}

void
TSMatcherLineDestroy(TSMatcherLine ml)
{
  if (ml) {
    xfree(ml);
  }
}

const char *
TSMatcherParseSrcIPConfigLine(char *line, TSMatcherLine ml)
{
  return parseConfigLine(line, (matcher_line *) ml, &ip_allow_tags);
}

char *
TSMatcherLineName(TSMatcherLine ml, int element)
{
  return (((matcher_line *) ml)->line)[0][element];
}

char *
TSMatcherLineValue(TSMatcherLine ml, int element)
{
  return (((matcher_line *) ml)->line)[1][element];
}

/* Configuration Setting */
int
TSMgmtConfigIntSet(const char *var_name, TSMgmtInt value)
{
  TSMgmtInt result;
  char *buffer;

  // is this a valid integer?
  if (!TSMgmtIntGet(var_name, &result)) {
    return 0;
  }
  // construct a buffer
  int buffer_size = strlen(var_name) + 1 + 32 + 1 + 64 + 1;
  buffer = (char *) alloca(buffer_size);
  snprintf(buffer, buffer_size, "%s %d %lld", var_name, INK_INT, value);

  // tell manager to set the configuration; note that this is not
  // transactional (e.g. we return control to the plugin before the
  // value is commited to disk by the manager)
  RecSignalManager(MGMT_SIGNAL_PLUGIN_SET_CONFIG, buffer);
  return 1;
}


/* Alarm */
/* return type is "int" currently, it should be TSReturnCode */
int
TSSignalWarning(TSAlarmType code, char *msg)
{
  if (code<TS_SIGNAL_WDA_BILLING_CONNECTION_DIED || code> TS_SIGNAL_WDA_RADIUS_CORRUPTED_PACKETS || msg == NULL)
    return -1;                  //TS_ERROR

  REC_SignalWarning(code, msg);
  return 0;                     //TS_SUCCESS
}

void
TSICPFreshnessFuncSet(TSPluginFreshnessCalcFunc funcp)
{
  pluginFreshnessCalcFunc = (PluginFreshnessCalcFunc) funcp;
}

int
TSICPCachedReqGet(TSCont contp, TSMBuffer *bufp, TSMLoc *obj)
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
TSICPCachedRespGet(TSCont contp, TSMBuffer *bufp, TSMLoc *obj)
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

TSReturnCode
TSCacheUrlSet(TSHttpTxn txnp, const char *url, int length)
{
  HttpSM *sm = (HttpSM *) txnp;
  Debug("cache_url", "[TSCacheUrlSet]");

  if (sm->t_state.cache_info.lookup_url == NULL) {
    Debug("cache_url", "[TSCacheUrlSet] changing the cache url to: %s", url);

    if (length == -1)
      length = strlen(url);

    sm->t_state.cache_info.lookup_url_storage.create(NULL);
    sm->t_state.cache_info.lookup_url = &(sm->t_state.cache_info.lookup_url_storage);
    sm->t_state.cache_info.lookup_url->parse(url, length);
  } else {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSHttpTxn
TSCacheGetStateMachine(TSCacheTxn txnp)
{
  NewCacheVC *vc = (NewCacheVC *) txnp;

  HttpCacheSM *cacheSm = (HttpCacheSM *) vc->getCacheSm();

  return cacheSm->master_sm;
}

void
TSCacheHttpInfoKeySet(TSCacheHttpInfo infop, TSCacheKey keyp)
{
  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  INK_MD5 *key = (INK_MD5 *) keyp;
  info->object_key_set(*key);
}

void
TSCacheHttpInfoSizeSet(TSCacheHttpInfo infop, int64 size)
{
  CacheHTTPInfo *info = (CacheHTTPInfo *) infop;
  info->object_size_set(size);
}

// this function should be called at TS_EVENT_HTTP_READ_RESPONSE_HDR
TSReturnCode TSRedirectUrlSet(TSHttpTxn txnp, const char* url, const int url_len)
{
  if (url == NULL) {
    return TS_ERROR;
  }
  if (sdk_sanity_check_txn(txnp)!=TS_SUCCESS) {
    return TS_ERROR;
  }
  HttpSM *sm = (HttpSM*) txnp;

  if (sm->redirect_url != NULL) {
    xfree(sm->redirect_url);
    sm->redirect_url = NULL;
    sm->redirect_url_len = 0;
  }

  if ((sm->redirect_url = (char*)xmalloc(url_len + 1)) != NULL) {
    ink_strncpy(sm->redirect_url, (char*)url, url_len + 1);
    sm->redirect_url_len = url_len;
    // have to turn redirection on for this transaction if user wants to redirect to another URL
    if (sm->enable_redirection == false) {
      sm->enable_redirection = true;
      // max-out "redirection_tries" to avoid the regular redirection being turned on in
      // this transaction improperly. This variable doesn't affect the custom-redirection
      sm->redirection_tries = HttpConfig::m_master.number_of_redirections;
    }
    return TS_SUCCESS;
  }
  else {
    return TS_ERROR;
  }
}

const char* TSRedirectUrlGet(TSHttpTxn txnp, int* url_len_ptr)
{
  if (sdk_sanity_check_txn(txnp)!=TS_SUCCESS) {
    return NULL;
  }
  HttpSM *sm = (HttpSM*) txnp;
  *url_len_ptr = sm->redirect_url_len;
  return (const char*)sm->redirect_url;
}

char* TSFetchRespGet(TSHttpTxn txnp, int *length)
{

   FetchSM *fetch_sm = (FetchSM*)txnp;
   return  fetch_sm->resp_get(length);
}

int
TSFetchPageRespGet (TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
    if ( sdk_sanity_check_null_ptr((void*)bufp) != TS_SUCCESS ||
                sdk_sanity_check_null_ptr((void*)obj) != TS_SUCCESS) {
        return 0;
    }
    HTTPHdr *hptr = (HTTPHdr*) txnp;

    if (hptr->valid()) {
        *bufp = hptr;
        *obj = hptr->m_http;
        sdk_sanity_check_mbuffer(*bufp);

        return 1;
    } else {
        return 0;
    }
}

extern ClassAllocator<FetchSM> FetchSMAllocator;

TSReturnCode
TSFetchPages(TSFetchUrlParams_t *params)
{
   TSFetchUrlParams_t *myparams = params;
   while(myparams!=NULL) {

     FetchSM *fetch_sm =  FetchSMAllocator.alloc();
     fetch_sm->init(myparams->contp,myparams->options,myparams->events,myparams->request,myparams->request_len, myparams->ip, myparams->port);
     fetch_sm->httpConnect();
     myparams= myparams->next;
  }
  return TS_SUCCESS;
}
TSReturnCode
TSFetchUrl(const char* headers, int request_len, unsigned int ip, int port , TSCont contp, TSFetchWakeUpOptions callback_options,TSFetchEvent events)
{
   FetchSM *fetch_sm =  FetchSMAllocator.alloc();
   fetch_sm->init(contp,callback_options,events,headers,request_len,ip,port);
   fetch_sm->httpConnect();
  return TS_SUCCESS;
}

int
TSHttpIsInternalRequest(TSHttpTxn txnp)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return 0;
  }
  TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);
  HttpClientSession *cs = (HttpClientSession *) ssnp;
  NetVConnection *vc = cs->get_netvc();
  if (!cs || !vc) {
    return 0;
  }
  return vc->get_is_internal_request();
}


TSReturnCode
TSAIORead(int fd, off_t offset, char* buf, size_t buffSize, TSCont contp)
{

  if (sdk_sanity_check_iocore_structure (contp) != TS_SUCCESS) {
    return TS_ERROR;
  }

  Continuation* pCont = (Continuation*) contp;
  AIOCallback* pAIO = new_AIOCallback();
  if( pAIO == NULL ) {
    return TS_ERROR;
  }

  pAIO->aiocb.aio_fildes = fd;
  pAIO->aiocb.aio_offset = offset;
  pAIO->aiocb.aio_nbytes = buffSize;


  pAIO->aiocb.aio_buf = buf;
  pAIO->action = pCont;
  pAIO->thread = ((ProxyMutex*) pCont->mutex)->thread_holding;

  if (ink_aio_read(pAIO, 1) == 1) {
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

char*
TSAIOBufGet(void *data)
{
  AIOCallback* pAIO = (AIOCallback*)data;
  return (char*)pAIO->aiocb.aio_buf;
}

int
TSAIONBytesGet(void *data)
{
  AIOCallback* pAIO = (AIOCallback*)data;
  return (int)pAIO->aio_result;
}

TSReturnCode
TSAIOWrite(int fd, off_t offset, char* buf, const size_t bufSize, TSCont contp)
{

  if (sdk_sanity_check_iocore_structure (contp) != TS_SUCCESS) {
    return TS_ERROR;
  }

  Continuation* pCont = (Continuation*) contp;

  AIOCallback* pAIO = new_AIOCallback();
  if( pAIO == NULL ) {
    return TS_ERROR;
  }

  pAIO->aiocb.aio_fildes = fd;
  pAIO->aiocb.aio_offset = offset;
  pAIO->aiocb.aio_buf = buf;
  pAIO->aiocb.aio_nbytes = bufSize;
  pAIO->action = pCont;
  pAIO->thread = ((ProxyMutex*) pCont->mutex)->thread_holding;

  if (ink_aio_write(pAIO, 1) == 1) {
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSAIOThreadNumSet(int thread_num)
{
  if (ink_aio_thread_num_set(thread_num) == 1) {
    return TS_SUCCESS;
  }
  else {
    return TS_ERROR;
  }
}

void
TSRecordDump(TSRecordType rec_type, TSRecordDumpCb callback, void *edata)
{
  RecDumpRecords((RecT)rec_type, (RecDumpEntryCb)callback, edata);
}

/* ability to skip the remap phase of the State Machine 
   this only really makes sense in TS_HTTP_READ_REQUEST_HDR_HOOK
*/
TSReturnCode TSSkipRemappingSet(TSHttpTxn txnp, int flag)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  HttpSM *sm = (HttpSM*) txnp;
  sm->t_state.api_skip_all_remapping = (flag != 0);
  return TS_SUCCESS;
}

#endif //TS_NO_API
