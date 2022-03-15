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

#include "tscore/ink_defs.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_inet.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include "HTTP.h"
#include "HdrToken.h"
#include "tscore/Diags.h"

/***********************************************************************
 *                                                                     *
 *                    C O M P I L E    O P T I O N S                   *
 *                                                                     *
 ***********************************************************************/

#define ENABLE_PARSER_FAST_PATHS 1

/***********************************************************************
 *                                                                     *
 *                          C O N S T A N T S                          *
 *                                                                     *
 ***********************************************************************/

// TODO: We should enable the creation and use of these WKS. XXXX
#if 0
static const char *cache_control_values[SIZEOF(cache_control_names)];
#endif

const char *HTTP_METHOD_CONNECT;
const char *HTTP_METHOD_DELETE;
const char *HTTP_METHOD_GET;
const char *HTTP_METHOD_HEAD;
const char *HTTP_METHOD_OPTIONS;
const char *HTTP_METHOD_POST;
const char *HTTP_METHOD_PURGE;
const char *HTTP_METHOD_PUT;
const char *HTTP_METHOD_TRACE;
const char *HTTP_METHOD_PUSH;

int HTTP_WKSIDX_CONNECT;
int HTTP_WKSIDX_DELETE;
int HTTP_WKSIDX_GET;
int HTTP_WKSIDX_HEAD;
int HTTP_WKSIDX_OPTIONS;
int HTTP_WKSIDX_POST;
int HTTP_WKSIDX_PURGE;
int HTTP_WKSIDX_PUT;
int HTTP_WKSIDX_TRACE;
int HTTP_WKSIDX_PUSH;
int HTTP_WKSIDX_METHODS_CNT = 0;

int HTTP_LEN_CONNECT;
int HTTP_LEN_DELETE;
int HTTP_LEN_GET;
int HTTP_LEN_HEAD;
int HTTP_LEN_OPTIONS;
int HTTP_LEN_POST;
int HTTP_LEN_PURGE;
int HTTP_LEN_PUT;
int HTTP_LEN_TRACE;
int HTTP_LEN_PUSH;

const char *HTTP_VALUE_BYTES;
const char *HTTP_VALUE_CHUNKED;
const char *HTTP_VALUE_CLOSE;
const char *HTTP_VALUE_COMPRESS;
const char *HTTP_VALUE_DEFLATE;
const char *HTTP_VALUE_GZIP;
const char *HTTP_VALUE_IDENTITY;
const char *HTTP_VALUE_KEEP_ALIVE;
const char *HTTP_VALUE_MAX_AGE;
const char *HTTP_VALUE_MAX_STALE;
const char *HTTP_VALUE_MIN_FRESH;
const char *HTTP_VALUE_MUST_REVALIDATE;
const char *HTTP_VALUE_NONE;
const char *HTTP_VALUE_NO_CACHE;
const char *HTTP_VALUE_NO_STORE;
const char *HTTP_VALUE_NO_TRANSFORM;
const char *HTTP_VALUE_ONLY_IF_CACHED;
const char *HTTP_VALUE_PRIVATE;
const char *HTTP_VALUE_PROXY_REVALIDATE;
const char *HTTP_VALUE_PUBLIC;
const char *HTTP_VALUE_S_MAXAGE;
const char *HTTP_VALUE_NEED_REVALIDATE_ONCE;
const char *HTTP_VALUE_100_CONTINUE;
// Cache-control: extension "need-revalidate-once" is used internally by T.S.
// to invalidate a document, and it is not returned/forwarded.
// If a cached document has this extension set (ie, is invalidated),
// then the T.S. needs to revalidate the document once before returning it.
// After a successful revalidation, the extension will be removed by T.S.
// To set or unset this directive should be done via the following two
// function:
//      set_cooked_cc_need_revalidate_once()
//      unset_cooked_cc_need_revalidate_once()
// To test, use regular Cache-control testing functions, eg,
//      is_cache_control_set(HTTP_VALUE_NEED_REVALIDATE_ONCE)

int HTTP_LEN_BYTES;
int HTTP_LEN_CHUNKED;
int HTTP_LEN_CLOSE;
int HTTP_LEN_COMPRESS;
int HTTP_LEN_DEFLATE;
int HTTP_LEN_GZIP;
int HTTP_LEN_IDENTITY;
int HTTP_LEN_KEEP_ALIVE;
int HTTP_LEN_MAX_AGE;
int HTTP_LEN_MAX_STALE;
int HTTP_LEN_MIN_FRESH;
int HTTP_LEN_MUST_REVALIDATE;
int HTTP_LEN_NONE;
int HTTP_LEN_NO_CACHE;
int HTTP_LEN_NO_STORE;
int HTTP_LEN_NO_TRANSFORM;
int HTTP_LEN_ONLY_IF_CACHED;
int HTTP_LEN_PRIVATE;
int HTTP_LEN_PROXY_REVALIDATE;
int HTTP_LEN_PUBLIC;
int HTTP_LEN_S_MAXAGE;
int HTTP_LEN_NEED_REVALIDATE_ONCE;
int HTTP_LEN_100_CONTINUE;

Arena *const HTTPHdr::USE_HDR_HEAP_MAGIC = reinterpret_cast<Arena *>(1);

/***********************************************************************
 *                                                                     *
 *                 U T I L I T Y    R O U T I N E S                    *
 *                                                                     *
 ***********************************************************************/

inline static int
is_digit(char c)
{
  return ((c <= '9') && (c >= '0'));
}

/***********************************************************************
 *                                                                     *
 *                         M A I N    C O D E                          *
 *                                                                     *
 ***********************************************************************/

void
http_hdr_adjust(HTTPHdrImpl * /* hdrp ATS_UNUSED */, int32_t /* offset ATS_UNUSED */, int32_t /* length ATS_UNUSED */,
                int32_t /* delta ATS_UNUSED */)
{
  ink_release_assert(!"http_hdr_adjust not implemented");
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_init()
{
  static int init = 1;

  if (init) {
    init = 0;

    mime_init();
    url_init();

    HTTP_METHOD_CONNECT = hdrtoken_string_to_wks("CONNECT");
    HTTP_METHOD_DELETE  = hdrtoken_string_to_wks("DELETE");
    HTTP_METHOD_GET     = hdrtoken_string_to_wks("GET");
    HTTP_METHOD_HEAD    = hdrtoken_string_to_wks("HEAD");
    HTTP_METHOD_OPTIONS = hdrtoken_string_to_wks("OPTIONS");
    HTTP_METHOD_POST    = hdrtoken_string_to_wks("POST");
    HTTP_METHOD_PURGE   = hdrtoken_string_to_wks("PURGE");
    HTTP_METHOD_PUT     = hdrtoken_string_to_wks("PUT");
    HTTP_METHOD_TRACE   = hdrtoken_string_to_wks("TRACE");
    HTTP_METHOD_PUSH    = hdrtoken_string_to_wks("PUSH");

    // HTTP methods index calculation. Don't forget to count them!
    // Don't change the order of calculation! Each index has related bitmask (see http quick filter)
    HTTP_WKSIDX_CONNECT = hdrtoken_wks_to_index(HTTP_METHOD_CONNECT);
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_DELETE = hdrtoken_wks_to_index(HTTP_METHOD_DELETE);
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_GET = hdrtoken_wks_to_index(HTTP_METHOD_GET);
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_HEAD = hdrtoken_wks_to_index(HTTP_METHOD_HEAD);
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_OPTIONS = hdrtoken_wks_to_index(HTTP_METHOD_OPTIONS);
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_POST = hdrtoken_wks_to_index(HTTP_METHOD_POST);
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_PURGE = hdrtoken_wks_to_index(HTTP_METHOD_PURGE);
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_PUT = hdrtoken_wks_to_index(HTTP_METHOD_PUT);
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_TRACE = hdrtoken_wks_to_index(HTTP_METHOD_TRACE);
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_PUSH = hdrtoken_wks_to_index(HTTP_METHOD_PUSH);
    HTTP_WKSIDX_METHODS_CNT++;

    HTTP_LEN_CONNECT = hdrtoken_wks_to_length(HTTP_METHOD_CONNECT);
    HTTP_LEN_DELETE  = hdrtoken_wks_to_length(HTTP_METHOD_DELETE);
    HTTP_LEN_GET     = hdrtoken_wks_to_length(HTTP_METHOD_GET);
    HTTP_LEN_HEAD    = hdrtoken_wks_to_length(HTTP_METHOD_HEAD);
    HTTP_LEN_OPTIONS = hdrtoken_wks_to_length(HTTP_METHOD_OPTIONS);
    HTTP_LEN_POST    = hdrtoken_wks_to_length(HTTP_METHOD_POST);
    HTTP_LEN_PURGE   = hdrtoken_wks_to_length(HTTP_METHOD_PURGE);
    HTTP_LEN_PUT     = hdrtoken_wks_to_length(HTTP_METHOD_PUT);
    HTTP_LEN_TRACE   = hdrtoken_wks_to_length(HTTP_METHOD_TRACE);
    HTTP_LEN_PUSH    = hdrtoken_wks_to_length(HTTP_METHOD_PUSH);

    HTTP_VALUE_BYTES                = hdrtoken_string_to_wks("bytes");
    HTTP_VALUE_CHUNKED              = hdrtoken_string_to_wks("chunked");
    HTTP_VALUE_CLOSE                = hdrtoken_string_to_wks("close");
    HTTP_VALUE_COMPRESS             = hdrtoken_string_to_wks("compress");
    HTTP_VALUE_DEFLATE              = hdrtoken_string_to_wks("deflate");
    HTTP_VALUE_GZIP                 = hdrtoken_string_to_wks("gzip");
    HTTP_VALUE_IDENTITY             = hdrtoken_string_to_wks("identity");
    HTTP_VALUE_KEEP_ALIVE           = hdrtoken_string_to_wks("keep-alive");
    HTTP_VALUE_MAX_AGE              = hdrtoken_string_to_wks("max-age");
    HTTP_VALUE_MAX_STALE            = hdrtoken_string_to_wks("max-stale");
    HTTP_VALUE_MIN_FRESH            = hdrtoken_string_to_wks("min-fresh");
    HTTP_VALUE_MUST_REVALIDATE      = hdrtoken_string_to_wks("must-revalidate");
    HTTP_VALUE_NONE                 = hdrtoken_string_to_wks("none");
    HTTP_VALUE_NO_CACHE             = hdrtoken_string_to_wks("no-cache");
    HTTP_VALUE_NO_STORE             = hdrtoken_string_to_wks("no-store");
    HTTP_VALUE_NO_TRANSFORM         = hdrtoken_string_to_wks("no-transform");
    HTTP_VALUE_ONLY_IF_CACHED       = hdrtoken_string_to_wks("only-if-cached");
    HTTP_VALUE_PRIVATE              = hdrtoken_string_to_wks("private");
    HTTP_VALUE_PROXY_REVALIDATE     = hdrtoken_string_to_wks("proxy-revalidate");
    HTTP_VALUE_PUBLIC               = hdrtoken_string_to_wks("public");
    HTTP_VALUE_S_MAXAGE             = hdrtoken_string_to_wks("s-maxage");
    HTTP_VALUE_NEED_REVALIDATE_ONCE = hdrtoken_string_to_wks("need-revalidate-once");
    HTTP_VALUE_100_CONTINUE         = hdrtoken_string_to_wks("100-continue");

    HTTP_LEN_BYTES                = hdrtoken_wks_to_length(HTTP_VALUE_BYTES);
    HTTP_LEN_CHUNKED              = hdrtoken_wks_to_length(HTTP_VALUE_CHUNKED);
    HTTP_LEN_CLOSE                = hdrtoken_wks_to_length(HTTP_VALUE_CLOSE);
    HTTP_LEN_COMPRESS             = hdrtoken_wks_to_length(HTTP_VALUE_COMPRESS);
    HTTP_LEN_DEFLATE              = hdrtoken_wks_to_length(HTTP_VALUE_DEFLATE);
    HTTP_LEN_GZIP                 = hdrtoken_wks_to_length(HTTP_VALUE_GZIP);
    HTTP_LEN_IDENTITY             = hdrtoken_wks_to_length(HTTP_VALUE_IDENTITY);
    HTTP_LEN_KEEP_ALIVE           = hdrtoken_wks_to_length(HTTP_VALUE_KEEP_ALIVE);
    HTTP_LEN_MAX_AGE              = hdrtoken_wks_to_length(HTTP_VALUE_MAX_AGE);
    HTTP_LEN_MAX_STALE            = hdrtoken_wks_to_length(HTTP_VALUE_MAX_STALE);
    HTTP_LEN_MIN_FRESH            = hdrtoken_wks_to_length(HTTP_VALUE_MIN_FRESH);
    HTTP_LEN_MUST_REVALIDATE      = hdrtoken_wks_to_length(HTTP_VALUE_MUST_REVALIDATE);
    HTTP_LEN_NONE                 = hdrtoken_wks_to_length(HTTP_VALUE_NONE);
    HTTP_LEN_NO_CACHE             = hdrtoken_wks_to_length(HTTP_VALUE_NO_CACHE);
    HTTP_LEN_NO_STORE             = hdrtoken_wks_to_length(HTTP_VALUE_NO_STORE);
    HTTP_LEN_NO_TRANSFORM         = hdrtoken_wks_to_length(HTTP_VALUE_NO_TRANSFORM);
    HTTP_LEN_ONLY_IF_CACHED       = hdrtoken_wks_to_length(HTTP_VALUE_ONLY_IF_CACHED);
    HTTP_LEN_PRIVATE              = hdrtoken_wks_to_length(HTTP_VALUE_PRIVATE);
    HTTP_LEN_PROXY_REVALIDATE     = hdrtoken_wks_to_length(HTTP_VALUE_PROXY_REVALIDATE);
    HTTP_LEN_PUBLIC               = hdrtoken_wks_to_length(HTTP_VALUE_PUBLIC);
    HTTP_LEN_S_MAXAGE             = hdrtoken_wks_to_length(HTTP_VALUE_S_MAXAGE);
    HTTP_LEN_NEED_REVALIDATE_ONCE = hdrtoken_wks_to_length(HTTP_VALUE_NEED_REVALIDATE_ONCE);
    HTTP_LEN_100_CONTINUE         = hdrtoken_wks_to_length(HTTP_VALUE_100_CONTINUE);

// TODO: We need to look into enable these CC values as WKS XXX
#if 0
    for (int i = 0; i < (int) SIZEOF(cache_control_values); i++) {
      cache_control_values[i] = hdrtoken_string_to_wks(cache_control_names[i]);
    }
#endif
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

HTTPHdrImpl *
http_hdr_create(HdrHeap *heap, HTTPType polarity)
{
  HTTPHdrImpl *hh;

  hh = (HTTPHdrImpl *)heap->allocate_obj(sizeof(HTTPHdrImpl), HDR_HEAP_OBJ_HTTP_HEADER);
  http_hdr_init(heap, hh, polarity);
  return (hh);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_init(HdrHeap *heap, HTTPHdrImpl *hh, HTTPType polarity)
{
  memset(&(hh->u), 0, sizeof(hh->u));
  hh->m_polarity    = polarity;
  hh->m_version     = HTTP_VERSION(1, 0);
  hh->m_fields_impl = mime_hdr_create(heap);
  if (polarity == HTTP_TYPE_REQUEST) {
    hh->u.req.m_url_impl       = url_create(heap);
    hh->u.req.m_method_wks_idx = -1;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_copy_onto(HTTPHdrImpl *s_hh, HdrHeap *s_heap, HTTPHdrImpl *d_hh, HdrHeap *d_heap, bool inherit_strs)
{
  MIMEHdrImpl *s_mh, *d_mh;
  URLImpl *s_url, *d_url;
  HTTPType d_polarity;

  s_mh       = s_hh->m_fields_impl;
  s_url      = s_hh->u.req.m_url_impl;
  d_mh       = d_hh->m_fields_impl;
  d_url      = d_hh->u.req.m_url_impl;
  d_polarity = d_hh->m_polarity;

  ink_assert(s_hh->m_polarity != HTTP_TYPE_UNKNOWN);
  ink_assert(s_mh != nullptr);
  ink_assert(d_mh != nullptr);

  memcpy(d_hh, s_hh, sizeof(HTTPHdrImpl));
  d_hh->m_fields_impl = d_mh; // restore pre-memcpy mime impl

  if (s_hh->m_polarity == HTTP_TYPE_REQUEST) {
    if (d_polarity == HTTP_TYPE_REQUEST) {
      d_hh->u.req.m_url_impl = d_url; // restore pre-memcpy url impl
    } else {
      d_url = d_hh->u.req.m_url_impl = url_create(d_heap); // create url
    }
    url_copy_onto(s_url, s_heap, d_url, d_heap, false);
  } else if (d_polarity == HTTP_TYPE_REQUEST) {
    // gender bender.  Need to kill off old url
    url_clear(d_url);
  }

  mime_hdr_copy_onto(s_mh, s_heap, d_mh, d_heap, false);
  if (inherit_strs) {
    d_heap->inherit_string_heaps(s_heap);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

HTTPHdrImpl *
http_hdr_clone(HTTPHdrImpl *s_hh, HdrHeap *s_heap, HdrHeap *d_heap)
{
  HTTPHdrImpl *d_hh;

  // FIX: A future optimization is to copy contiguous objects with
  //      one single memcpy.  For this first optimization, we just
  //      copy each object separately.

  d_hh = http_hdr_create(d_heap, s_hh->m_polarity);
  http_hdr_copy_onto(s_hh, s_heap, d_hh, d_heap, ((s_heap != d_heap) ? true : false));
  return (d_hh);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static inline char *
http_hdr_version_to_string(int32_t version, char *buf9)
{
  ink_assert(HTTP_MAJOR(version) < 10);
  ink_assert(HTTP_MINOR(version) < 10);

  buf9[0] = 'H';
  buf9[1] = 'T';
  buf9[2] = 'T';
  buf9[3] = 'P';
  buf9[4] = '/';
  buf9[5] = '0' + HTTP_MAJOR(version);
  buf9[6] = '.';
  buf9[7] = '0' + HTTP_MINOR(version);
  buf9[8] = '\0';

  return (buf9);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
http_version_print(int32_t version, char *buf, int bufsize, int *bufindex, int *dumpoffset)
{
#define TRY(x) \
  if (!x)      \
  return 0

  char tmpbuf[16];
  http_hdr_version_to_string(version, tmpbuf);
  TRY(mime_mem_print(tmpbuf, 8, buf, bufsize, bufindex, dumpoffset));
  return 1;

#undef TRY
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
http_hdr_print(HdrHeap *heap, HTTPHdrImpl *hdr, char *buf, int bufsize, int *bufindex, int *dumpoffset)
{
#define TRY(x) \
  if (!x)      \
  return 0

  int tmplen, hdrstat;
  char tmpbuf[32];
  char *p;

  ink_assert((hdr->m_polarity == HTTP_TYPE_REQUEST) || (hdr->m_polarity == HTTP_TYPE_RESPONSE));

  if (hdr->m_polarity == HTTP_TYPE_REQUEST) {
    if (hdr->u.req.m_ptr_method == nullptr) {
      return 1;
    }

    if ((buf != nullptr) && (*dumpoffset == 0) && (bufsize - *bufindex >= hdr->u.req.m_len_method + 1)) { // fastpath

      p = buf + *bufindex;
      memcpy(p, hdr->u.req.m_ptr_method, hdr->u.req.m_len_method);
      p += hdr->u.req.m_len_method;
      *p++ = ' ';
      *bufindex += hdr->u.req.m_len_method + 1;

      if (hdr->u.req.m_url_impl) {
        TRY(url_print(hdr->u.req.m_url_impl, buf, bufsize, bufindex, dumpoffset));
        if (bufsize - *bufindex >= 1) {
          if (hdr->u.req.m_method_wks_idx == HTTP_WKSIDX_CONNECT) {
            *bufindex -= 1; // remove trailing slash for CONNECT request
          }
          p    = buf + *bufindex;
          *p++ = ' ';
          *bufindex += 1;
        } else {
          return 0;
        }
      }

      if (bufsize - *bufindex >= 9) {
        http_hdr_version_to_string(hdr->m_version, p);
        *bufindex += 9 - 1; // overwrite '\0';
      } else {
        TRY(http_version_print(hdr->m_version, buf, bufsize, bufindex, dumpoffset));
      }

      if (bufsize - *bufindex >= 2) {
        p    = buf + *bufindex;
        *p++ = '\r';
        *p++ = '\n';
        *bufindex += 2;
      } else {
        TRY(mime_mem_print("\r\n", 2, buf, bufsize, bufindex, dumpoffset));
      }

      TRY(mime_hdr_print(heap, hdr->m_fields_impl, buf, bufsize, bufindex, dumpoffset));

    } else {
      TRY(mime_mem_print(hdr->u.req.m_ptr_method, hdr->u.req.m_len_method, buf, bufsize, bufindex, dumpoffset));

      TRY(mime_mem_print(" ", 1, buf, bufsize, bufindex, dumpoffset));

      if (hdr->u.req.m_url_impl) {
        TRY(url_print(hdr->u.req.m_url_impl, buf, bufsize, bufindex, dumpoffset));
        TRY(mime_mem_print(" ", 1, buf, bufsize, bufindex, dumpoffset));
      }

      TRY(http_version_print(hdr->m_version, buf, bufsize, bufindex, dumpoffset));

      TRY(mime_mem_print("\r\n", 2, buf, bufsize, bufindex, dumpoffset));

      TRY(mime_hdr_print(heap, hdr->m_fields_impl, buf, bufsize, bufindex, dumpoffset));
    }

  } else { //  hdr->m_polarity == HTTP_TYPE_RESPONSE

    if ((buf != nullptr) && (*dumpoffset == 0) && (bufsize - *bufindex >= 9 + 6 + 1)) { // fastpath

      p = buf + *bufindex;
      http_hdr_version_to_string(hdr->m_version, p);
      p += 8; // overwrite '\0' with space
      *p++ = ' ';
      *bufindex += 9;

      hdrstat = http_hdr_status_get(hdr);
      if (hdrstat == 200) {
        *p++   = '2';
        *p++   = '0';
        *p++   = '0';
        tmplen = 3;
      } else {
        tmplen = mime_format_int(p, hdrstat, (bufsize - (p - buf)));
        ink_assert(tmplen <= 6);
        p += tmplen;
      }
      *p++ = ' ';
      *bufindex += tmplen + 1;

      if (hdr->u.resp.m_ptr_reason) {
        TRY(mime_mem_print(hdr->u.resp.m_ptr_reason, hdr->u.resp.m_len_reason, buf, bufsize, bufindex, dumpoffset));
      }

      if (bufsize - *bufindex >= 2) {
        p    = buf + *bufindex;
        *p++ = '\r';
        *p++ = '\n';
        *bufindex += 2;
      } else {
        TRY(mime_mem_print("\r\n", 2, buf, bufsize, bufindex, dumpoffset));
      }

      TRY(mime_hdr_print(heap, hdr->m_fields_impl, buf, bufsize, bufindex, dumpoffset));

    } else {
      TRY(http_version_print(hdr->m_version, buf, bufsize, bufindex, dumpoffset));

      TRY(mime_mem_print(" ", 1, buf, bufsize, bufindex, dumpoffset));

      tmplen = mime_format_int(tmpbuf, http_hdr_status_get(hdr), sizeof(tmpbuf));

      TRY(mime_mem_print(tmpbuf, tmplen, buf, bufsize, bufindex, dumpoffset));

      TRY(mime_mem_print(" ", 1, buf, bufsize, bufindex, dumpoffset));

      if (hdr->u.resp.m_ptr_reason) {
        TRY(mime_mem_print(hdr->u.resp.m_ptr_reason, hdr->u.resp.m_len_reason, buf, bufsize, bufindex, dumpoffset));
      }

      TRY(mime_mem_print("\r\n", 2, buf, bufsize, bufindex, dumpoffset));

      TRY(mime_hdr_print(heap, hdr->m_fields_impl, buf, bufsize, bufindex, dumpoffset));
    }
  }

  return 1;

#undef TRY
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_describe(HdrHeapObjImpl *raw, bool recurse)
{
  HTTPHdrImpl *obj = (HTTPHdrImpl *)raw;

  if (obj->m_polarity == HTTP_TYPE_REQUEST) {
    Debug("http", "[TYPE: REQ, V: %04X, URL: %p, METHOD: \"%.*s\", METHOD_LEN: %d, FIELDS: %p]", obj->m_version,
          obj->u.req.m_url_impl, obj->u.req.m_len_method, (obj->u.req.m_ptr_method ? obj->u.req.m_ptr_method : "NULL"),
          obj->u.req.m_len_method, obj->m_fields_impl);
    if (recurse) {
      if (obj->u.req.m_url_impl) {
        obj_describe(obj->u.req.m_url_impl, recurse);
      }
      if (obj->m_fields_impl) {
        obj_describe(obj->m_fields_impl, recurse);
      }
    }
  } else {
    Debug("http", "[TYPE: RSP, V: %04X, STATUS: %d, REASON: \"%.*s\", REASON_LEN: %d, FIELDS: %p]", obj->m_version,
          obj->u.resp.m_status, obj->u.resp.m_len_reason, (obj->u.resp.m_ptr_reason ? obj->u.resp.m_ptr_reason : "NULL"),
          obj->u.resp.m_len_reason, obj->m_fields_impl);
    if (recurse) {
      if (obj->m_fields_impl) {
        obj_describe(obj->m_fields_impl, recurse);
      }
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
http_hdr_length_get(HTTPHdrImpl *hdr)
{
  int length = 0;

  if (hdr->m_polarity == HTTP_TYPE_REQUEST) {
    if (hdr->u.req.m_ptr_method) {
      length = hdr->u.req.m_len_method;
    } else {
      length = 0;
    }

    length += 1; // " "

    if (hdr->u.req.m_url_impl) {
      length += url_length_get(hdr->u.req.m_url_impl);
    }

    length += 1; // " "

    length += 8; // HTTP/%d.%d

    length += 2; // "\r\n"
  } else if (hdr->m_polarity == HTTP_TYPE_RESPONSE) {
    if (hdr->u.resp.m_ptr_reason) {
      length = hdr->u.resp.m_len_reason;
    } else {
      length = 0;
    }

    length += 8; // HTTP/%d.%d

    length += 1; // " "

    length += 3; // status

    length += 1; // " "

    length += 2; // "\r\n"
  }

  length += mime_hdr_length_get(hdr->m_fields_impl);

  return length;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_type_set(HTTPHdrImpl *hh, HTTPType type)
{
  hh->m_polarity = type;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_version_set(HTTPHdrImpl *hh, int32_t ver)
{
  hh->m_version = ver;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
http_hdr_method_get(HTTPHdrImpl *hh, int *length)
{
  const char *str;

  ink_assert(hh->m_polarity == HTTP_TYPE_REQUEST);

  if (hh->u.req.m_method_wks_idx >= 0) {
    str     = hdrtoken_index_to_wks(hh->u.req.m_method_wks_idx);
    *length = hdrtoken_index_to_length(hh->u.req.m_method_wks_idx);
  } else {
    str     = hh->u.req.m_ptr_method;
    *length = hh->u.req.m_len_method;
  }

  return (str);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_method_set(HdrHeap *heap, HTTPHdrImpl *hh, const char *method, int16_t method_wks_idx, int method_length, bool must_copy)
{
  ink_assert(hh->m_polarity == HTTP_TYPE_REQUEST);

  hh->u.req.m_method_wks_idx = method_wks_idx;
  mime_str_u16_set(heap, method, method_length, &(hh->u.req.m_ptr_method), &(hh->u.req.m_len_method), must_copy);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_url_set(HdrHeap *heap, HTTPHdrImpl *hh, URLImpl *url)
{
  ink_assert(hh->m_polarity == HTTP_TYPE_REQUEST);
  if (hh->u.req.m_url_impl != url) {
    if (hh->u.req.m_url_impl != nullptr) {
      heap->deallocate_obj(hh->u.req.m_url_impl);
    }
    // Clone into new heap if the URL was allocated against a different heap
    if ((char *)url < heap->m_data_start || (char *)url >= heap->m_free_start) {
      hh->u.req.m_url_impl = static_cast<URLImpl *>(heap->allocate_obj(url->m_length, url->m_type));
      memcpy(hh->u.req.m_url_impl, url, url->m_length);
      // Make sure there is a read_write heap
      if (heap->m_read_write_heap.get() == nullptr) {
        int url_string_length   = url->strings_length();
        heap->m_read_write_heap = new_HdrStrHeap(url_string_length);
      }
      hh->u.req.m_url_impl->move_strings(heap->m_read_write_heap.get());
    } else {
      hh->u.req.m_url_impl = url;
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_status_set(HTTPHdrImpl *hh, HTTPStatus status)
{
  ink_assert(hh->m_polarity == HTTP_TYPE_RESPONSE);
  hh->u.resp.m_status = status;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
http_hdr_reason_get(HTTPHdrImpl *hh, int *length)
{
  ink_assert(hh->m_polarity == HTTP_TYPE_RESPONSE);
  *length = hh->u.resp.m_len_reason;
  return (hh->u.resp.m_ptr_reason);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_reason_set(HdrHeap *heap, HTTPHdrImpl *hh, const char *value, int length, bool must_copy)
{
  ink_assert(hh->m_polarity == HTTP_TYPE_RESPONSE);
  mime_str_u16_set(heap, value, length, &(hh->u.resp.m_ptr_reason), &(hh->u.resp.m_len_reason), must_copy);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
http_hdr_reason_lookup(unsigned status)
{
#define HTTP_STATUS_ENTRY(value, reason) \
  case value:                            \
    return #reason

  switch (status) {
    HTTP_STATUS_ENTRY(0, None);                  // TS_HTTP_STATUS_NONE
    HTTP_STATUS_ENTRY(100, Continue);            // [RFC2616]
    HTTP_STATUS_ENTRY(101, Switching Protocols); // [RFC2616]
    HTTP_STATUS_ENTRY(102, Processing);          // [RFC2518]
    HTTP_STATUS_ENTRY(103, Early Hints);         // TODO: add RFC number
    // 103-199 Unassigned
    HTTP_STATUS_ENTRY(200, OK);                              // [RFC2616]
    HTTP_STATUS_ENTRY(201, Created);                         // [RFC2616]
    HTTP_STATUS_ENTRY(202, Accepted);                        // [RFC2616]
    HTTP_STATUS_ENTRY(203, Non - Authoritative Information); // [RFC2616]
    HTTP_STATUS_ENTRY(204, No Content);                      // [RFC2616]
    HTTP_STATUS_ENTRY(205, Reset Content);                   // [RFC2616]
    HTTP_STATUS_ENTRY(206, Partial Content);                 // [RFC2616]
    HTTP_STATUS_ENTRY(207, Multi - Status);                  // [RFC4918]
    HTTP_STATUS_ENTRY(208, Already Reported);                // [RFC5842]
    // 209-225 Unassigned
    HTTP_STATUS_ENTRY(226, IM Used); // [RFC3229]
    // 227-299 Unassigned
    HTTP_STATUS_ENTRY(300, Multiple Choices);  // [RFC2616]
    HTTP_STATUS_ENTRY(301, Moved Permanently); // [RFC2616]
    HTTP_STATUS_ENTRY(302, Found);             // [RFC2616]
    HTTP_STATUS_ENTRY(303, See Other);         // [RFC2616]
    HTTP_STATUS_ENTRY(304, Not Modified);      // [RFC2616]
    HTTP_STATUS_ENTRY(305, Use Proxy);         // [RFC2616]
    // 306 Reserved                                                   // [RFC2616]
    HTTP_STATUS_ENTRY(307, Temporary Redirect); // [RFC2616]
    HTTP_STATUS_ENTRY(308, Permanent Redirect); // [RFC-reschke-http-status-308-07]
    // 309-399 Unassigned
    HTTP_STATUS_ENTRY(400, Bad Request);                     // [RFC2616]
    HTTP_STATUS_ENTRY(401, Unauthorized);                    // [RFC2616]
    HTTP_STATUS_ENTRY(402, Payment Required);                // [RFC2616]
    HTTP_STATUS_ENTRY(403, Forbidden);                       // [RFC2616]
    HTTP_STATUS_ENTRY(404, Not Found);                       // [RFC2616]
    HTTP_STATUS_ENTRY(405, Method Not Allowed);              // [RFC2616]
    HTTP_STATUS_ENTRY(406, Not Acceptable);                  // [RFC2616]
    HTTP_STATUS_ENTRY(407, Proxy Authentication Required);   // [RFC2616]
    HTTP_STATUS_ENTRY(408, Request Timeout);                 // [RFC2616]
    HTTP_STATUS_ENTRY(409, Conflict);                        // [RFC2616]
    HTTP_STATUS_ENTRY(410, Gone);                            // [RFC2616]
    HTTP_STATUS_ENTRY(411, Length Required);                 // [RFC2616]
    HTTP_STATUS_ENTRY(412, Precondition Failed);             // [RFC2616]
    HTTP_STATUS_ENTRY(413, Request Entity Too Large);        // [RFC2616]
    HTTP_STATUS_ENTRY(414, Request - URI Too Long);          // [RFC2616]
    HTTP_STATUS_ENTRY(415, Unsupported Media Type);          // [RFC2616]
    HTTP_STATUS_ENTRY(416, Requested Range Not Satisfiable); // [RFC2616]
    HTTP_STATUS_ENTRY(417, Expectation Failed);              // [RFC2616]
    HTTP_STATUS_ENTRY(422, Unprocessable Entity);            // [RFC4918]
    HTTP_STATUS_ENTRY(423, Locked);                          // [RFC4918]
    HTTP_STATUS_ENTRY(424, Failed Dependency);               // [RFC4918]
    // 425 Reserved                                                   // [RFC2817]
    HTTP_STATUS_ENTRY(426, Upgrade Required); // [RFC2817]
    // 427 Unassigned
    HTTP_STATUS_ENTRY(428, Precondition Required); // [RFC6585]
    HTTP_STATUS_ENTRY(429, Too Many Requests);     // [RFC6585]
    // 430 Unassigned
    HTTP_STATUS_ENTRY(431, Request Header Fields Too Large); // [RFC6585]
    // 432-499 Unassigned
    HTTP_STATUS_ENTRY(500, Internal Server Error);      // [RFC2616]
    HTTP_STATUS_ENTRY(501, Not Implemented);            // [RFC2616]
    HTTP_STATUS_ENTRY(502, Bad Gateway);                // [RFC2616]
    HTTP_STATUS_ENTRY(503, Service Unavailable);        // [RFC2616]
    HTTP_STATUS_ENTRY(504, Gateway Timeout);            // [RFC2616]
    HTTP_STATUS_ENTRY(505, HTTP Version Not Supported); // [RFC2616]
    HTTP_STATUS_ENTRY(506, Variant Also Negotiates);    // [RFC2295]
    HTTP_STATUS_ENTRY(507, Insufficient Storage);       // [RFC4918]
    HTTP_STATUS_ENTRY(508, Loop Detected);              // [RFC5842]
    // 509 Unassigned
    HTTP_STATUS_ENTRY(510, Not Extended);                    // [RFC2774]
    HTTP_STATUS_ENTRY(511, Network Authentication Required); // [RFC6585]
    // 512-599 Unassigned
  }

#undef HTTP_STATUS_ENTRY

  return nullptr;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
_http_parser_init(HTTPParser *parser)
{
  parser->m_parsing_http = true;
}

//////////////////////////////////////////////////////
// init     first time structure setup              //
// clear    resets an already-initialized structure //
//////////////////////////////////////////////////////

void
http_parser_init(HTTPParser *parser)
{
  _http_parser_init(parser);
  mime_parser_init(&parser->m_mime_parser);
}

void
http_parser_clear(HTTPParser *parser)
{
  _http_parser_init(parser);
  mime_parser_clear(&parser->m_mime_parser);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

#define GETNEXT(label) \
  {                    \
    cur += 1;          \
    if (cur >= end) {  \
      goto label;      \
    }                  \
  }

#define GETPREV(label)      \
  {                         \
    cur -= 1;               \
    if (cur < line_start) { \
      goto label;           \
    }                       \
  }

// NOTE: end is ONE CHARACTER PAST end of string!

ParseResult
http_parser_parse_req(HTTPParser *parser, HdrHeap *heap, HTTPHdrImpl *hh, const char **start, const char *end,
                      bool must_copy_strings, bool eof, int strict_uri_parsing)
{
  if (parser->m_parsing_http) {
    MIMEScanner *scanner = &parser->m_mime_parser.m_scanner;
    URLImpl *url;

    ParseResult err;
    bool line_is_real;
    const char *cur;
    const char *line_start;
    const char *real_end;
    const char *method_start;
    const char *method_end;
    const char *url_start;
    const char *url_end;
    const char *version_start;
    const char *version_end;

    real_end = end;

  start:
    hh->m_polarity = HTTP_TYPE_REQUEST;

    // Make sure the line is not longer than 64K
    if (scanner->m_line_length >= UINT16_MAX) {
      return PARSE_RESULT_ERROR;
    }

    err = mime_scanner_get(scanner, start, real_end, &line_start, &end, &line_is_real, eof, MIME_SCANNER_TYPE_LINE);
    if (err < 0) {
      return err;
    }
    // We have to get a request line.  If we get parse done here,
    //   that meas we got an empty request
    if (err == PARSE_RESULT_DONE) {
      return PARSE_RESULT_ERROR;
    }
    if (err == PARSE_RESULT_CONT) {
      return err;
    }

    cur = line_start;
    ink_assert((end - cur) >= 0);
    ink_assert((end - cur) < UINT16_MAX);

    must_copy_strings = (must_copy_strings || (!line_is_real));

#if (ENABLE_PARSER_FAST_PATHS)
    // first try fast path
    if (end - cur >= 16) {
      if (((cur[0] ^ 'G') | (cur[1] ^ 'E') | (cur[2] ^ 'T')) != 0) {
        goto slow_case;
      }
      if (((end[-10] ^ 'H') | (end[-9] ^ 'T') | (end[-8] ^ 'T') | (end[-7] ^ 'P') | (end[-6] ^ '/') | (end[-4] ^ '.') |
           (end[-2] ^ '\r') | (end[-1] ^ '\n')) != 0) {
        goto slow_case;
      }
      if (!(is_digit(end[-5]) && is_digit(end[-3]))) {
        goto slow_case;
      }
      if (!(ParseRules::is_space(cur[3]) && (!ParseRules::is_space(cur[4])) && (!ParseRules::is_space(end[-12])) &&
            ParseRules::is_space(end[-11]))) {
        goto slow_case;
      }
      if (&(cur[4]) >= &(end[-11])) {
        goto slow_case;
      }

      int32_t version = HTTP_VERSION(end[-5] - '0', end[-3] - '0');

      http_hdr_method_set(heap, hh, &(cur[0]), hdrtoken_wks_to_index(HTTP_METHOD_GET), 3, must_copy_strings);
      ink_assert(hh->u.req.m_url_impl != nullptr);
      url       = hh->u.req.m_url_impl;
      url_start = &(cur[4]);
      err       = ::url_parse(heap, url, &url_start, &(end[-11]), must_copy_strings, strict_uri_parsing);
      if (err < 0) {
        return err;
      }
      http_hdr_version_set(hh, version);

      end                    = real_end;
      parser->m_parsing_http = false;
      if (version == HTTP_VERSION(0, 9)) {
        return PARSE_RESULT_ERROR;
      }

      ParseResult ret = mime_parser_parse(&parser->m_mime_parser, heap, hh->m_fields_impl, start, end, must_copy_strings, eof);
      // If we're done with the main parse do some validation
      if (ret == PARSE_RESULT_DONE) {
        ret = validate_hdr_host(hh); // check HOST header
      }
      if (ret == PARSE_RESULT_DONE) {
        ret = validate_hdr_content_length(heap, hh);
      }
      return ret;
    }
#endif

  slow_case:

    method_start  = nullptr;
    method_end    = nullptr;
    url_start     = nullptr;
    url_end       = nullptr;
    version_start = nullptr;
    version_end   = nullptr;
    url           = nullptr;

    if (ParseRules::is_cr(*cur))
      GETNEXT(done);
    if (ParseRules::is_lf(*cur)) {
      goto start;
    }

  parse_method1:

    if (ParseRules::is_ws(*cur)) {
      GETNEXT(done);
      goto parse_method1;
    }
    if (!ParseRules::is_token(*cur)) {
      goto done;
    }
    method_start = cur;
    GETNEXT(done);
  parse_method2:
    if (ParseRules::is_ws(*cur)) {
      method_end = cur;
      goto parse_version1;
    }
    if (!ParseRules::is_token(*cur)) {
      goto done;
    }
    GETNEXT(done);
    goto parse_method2;

  parse_version1:
    cur = end - 1;
    if (ParseRules::is_lf(*cur) && (cur >= line_start)) {
      cur -= 1;
    }
    if (ParseRules::is_cr(*cur) && (cur >= line_start)) {
      cur -= 1;
    }
    // A client may add extra white spaces after the HTTP version.
    // So, skip white spaces.
    while (ParseRules::is_ws(*cur) && (cur >= line_start)) {
      cur -= 1;
    }
    version_end = cur + 1;
  parse_version2:
    if (ParseRules::is_digit(*cur)) {
      GETPREV(parse_url);
      goto parse_version2;
    }
    if (*cur == '.') {
      GETPREV(parse_url);
      goto parse_version3;
    }
    goto parse_url;
  parse_version3:
    if (ParseRules::is_digit(*cur)) {
      GETPREV(parse_url);
      goto parse_version3;
    }
    if (*cur == '/') {
      GETPREV(parse_url);
      goto parse_version4;
    }
    goto parse_url;
  parse_version4:
    if ((*cur != 'P') && (*cur != 'p')) {
      goto parse_url;
    }
    GETPREV(parse_url);
    if ((*cur != 'T') && (*cur != 't')) {
      goto parse_url;
    }
    GETPREV(parse_url);
    if ((*cur != 'T') && (*cur != 't')) {
      goto parse_url;
    }
    GETPREV(parse_url);
    if ((*cur != 'H') && (*cur != 'h')) {
      goto parse_url;
    }
    version_start = cur;

  parse_url:
    url_start = method_end + 1;
    if (version_start) {
      url_end = version_start - 1;
    } else {
      url_end = end - 1;
    }
    while ((url_start < end) && ParseRules::is_ws(*url_start)) {
      url_start += 1;
    }
    while ((url_end >= line_start) && ParseRules::is_wslfcr(*url_end)) {
      url_end -= 1;
    }
    url_end += 1;

  done:
    if (!method_start || !method_end) {
      return PARSE_RESULT_ERROR;
    }

    // checking these with an if statement makes coverity flag as dead code because
    // url_start and url_end logically cannot be 0 at this time
    ink_assert(url_start);
    ink_assert(url_end);

    int method_wks_idx = hdrtoken_tokenize(method_start, (int)(method_end - method_start));
    http_hdr_method_set(heap, hh, method_start, method_wks_idx, (int)(method_end - method_start), must_copy_strings);

    ink_assert(hh->u.req.m_url_impl != nullptr);

    url = hh->u.req.m_url_impl;
    err = ::url_parse(heap, url, &url_start, url_end, must_copy_strings, strict_uri_parsing);

    if (err < 0) {
      return err;
    }

    int32_t version;
    if (version_start && version_end) {
      version = http_parse_version(version_start, version_end);
    } else {
      return PARSE_RESULT_ERROR;
    }

    if (version == HTTP_VERSION(0, 9)) {
      return PARSE_RESULT_ERROR;
    }

    http_hdr_version_set(hh, version);

    end                    = real_end;
    parser->m_parsing_http = false;
  }

  ParseResult ret = mime_parser_parse(&parser->m_mime_parser, heap, hh->m_fields_impl, start, end, must_copy_strings, eof);
  // If we're done with the main parse do some validation
  if (ret == PARSE_RESULT_DONE) {
    ret = validate_hdr_host(hh); // check HOST header
  }
  if (ret == PARSE_RESULT_DONE) {
    ret = validate_hdr_content_length(heap, hh);
  }
  return ret;
}

ParseResult
validate_hdr_host(HTTPHdrImpl *hh)
{
  ParseResult ret       = PARSE_RESULT_DONE;
  MIMEField *host_field = mime_hdr_field_find(hh->m_fields_impl, MIME_FIELD_HOST, MIME_LEN_HOST);
  if (host_field) {
    if (host_field->has_dups()) {
      ret = PARSE_RESULT_ERROR; // can't have more than 1 host field.
    } else {
      int host_len         = 0;
      const char *host_val = host_field->value_get(&host_len);
      std::string_view addr, port, rest, host(host_val, host_len);
      if (0 == ats_ip_parse(host, &addr, &port, &rest)) {
        if (!port.empty()) {
          if (port.size() > 5) {
            return PARSE_RESULT_ERROR;
          }
          int port_i = ink_atoi(port.data(), port.size());
          if (port_i >= 65536 || port_i <= 0) {
            return PARSE_RESULT_ERROR;
          }
        }
        if (!validate_host_name(addr)) {
          return PARSE_RESULT_ERROR;
        }
        if (PARSE_RESULT_DONE == ret && !std::all_of(rest.begin(), rest.end(), &ParseRules::is_ws)) {
          return PARSE_RESULT_ERROR;
        }
      } else {
        ret = PARSE_RESULT_ERROR;
      }
    }
  }
  return ret;
}

ParseResult
validate_hdr_content_length(HdrHeap *heap, HTTPHdrImpl *hh)
{
  MIMEField *content_length_field = mime_hdr_field_find(hh->m_fields_impl, MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);

  if (content_length_field) {
    // RFC 7230 section 3.3.3:
    // If a message is received with both a Transfer-Encoding and a
    // Content-Length header field, the Transfer-Encoding overrides
    // the Content-Length
    if (mime_hdr_field_find(hh->m_fields_impl, MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING) != nullptr) {
      // Delete all Content-Length headers
      Debug("http", "Transfer-Encoding header and Content-Length headers the request, removing all Content-Length headers");
      mime_hdr_field_delete(heap, hh->m_fields_impl, content_length_field, true);
      return PARSE_RESULT_DONE;
    }

    // RFC 7230 section 3.3.3:
    // If a message is received without Transfer-Encoding and with
    // either multiple Content-Length header fields having differing
    // field-values or a single Content-Length header field having an
    // invalid value, then the message framing is invalid and the
    // recipient MUST treat it as an unrecoverable error.  If this is a
    // request message, the server MUST respond with a 400 (Bad Request)
    // status code and then close the connection
    int content_length_len         = 0;
    const char *content_length_val = content_length_field->value_get(&content_length_len);

    // RFC 7230 section 3.3.2
    // Content-Length = 1*DIGIT
    //
    // If the content-length value contains a non-numeric value, the header is invalid
    for (int i = 0; i < content_length_len; i++) {
      if (!isdigit(content_length_val[i])) {
        Debug("http", "Content-Length value contains non-digit, returning parse error");
        return PARSE_RESULT_ERROR;
      }
    }

    while (content_length_field->has_dups()) {
      int content_length_len_2         = 0;
      const char *content_length_val_2 = content_length_field->m_next_dup->value_get(&content_length_len_2);

      if ((content_length_len != content_length_len_2) ||
          (memcmp(content_length_val, content_length_val_2, content_length_len) != 0)) {
        // Values are different, parse error
        Debug("http", "Content-Length headers don't match, returning parse error");
        return PARSE_RESULT_ERROR;
      } else {
        // Delete the duplicate since it has the same value
        Debug("http", "Deleting duplicate Content-Length header");
        mime_hdr_field_delete(heap, hh->m_fields_impl, content_length_field->m_next_dup, false);
      }
    }
  }

  return PARSE_RESULT_DONE;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

ParseResult
http_parser_parse_resp(HTTPParser *parser, HdrHeap *heap, HTTPHdrImpl *hh, const char **start, const char *end,
                       bool must_copy_strings, bool eof)
{
  if (parser->m_parsing_http) {
    MIMEScanner *scanner = &parser->m_mime_parser.m_scanner;

    ParseResult err;
    bool line_is_real;
    const char *cur;
    const char *line_start;
    const char *real_end;
    const char *version_start;
    const char *version_end;
    const char *status_start;
    const char *status_end;
    const char *reason_start;
    const char *reason_end;
    const char *old_start;

    real_end  = end;
    old_start = *start;

    hh->m_polarity = HTTP_TYPE_RESPONSE;

    // Make sure the line is not longer than 64K
    if (scanner->m_line_length >= UINT16_MAX) {
      return PARSE_RESULT_ERROR;
    }

    err = mime_scanner_get(scanner, start, real_end, &line_start, &end, &line_is_real, eof, MIME_SCANNER_TYPE_LINE);
    if (err < 0) {
      return err;
    }
    if ((err == PARSE_RESULT_DONE) || (err == PARSE_RESULT_CONT)) {
      return err;
    }

    cur = line_start;
    ink_assert((end - cur) >= 0);
    ink_assert((end - cur) < UINT16_MAX);

    must_copy_strings = (must_copy_strings || (!line_is_real));

#if (ENABLE_PARSER_FAST_PATHS)
    // first try fast path
    if (end - cur >= 16) {
      int http_match =
        ((cur[0] ^ 'H') | (cur[1] ^ 'T') | (cur[2] ^ 'T') | (cur[3] ^ 'P') | (cur[4] ^ '/') | (cur[6] ^ '.') | (cur[8] ^ ' '));
      if ((http_match != 0) || (!(is_digit(cur[5]) && is_digit(cur[7]) && is_digit(cur[9]) && is_digit(cur[10]) &&
                                  is_digit(cur[11]) && (!ParseRules::is_space(cur[13]))))) {
        goto slow_case;
      }

      reason_start = &(cur[13]);
      reason_end   = end - 1;
      while ((reason_end > reason_start + 1) && (ParseRules::is_space(reason_end[-1]))) {
        --reason_end;
      }

      int32_t version   = HTTP_VERSION(cur[5] - '0', cur[7] - '0');
      HTTPStatus status = (HTTPStatus)((cur[9] - '0') * 100 + (cur[10] - '0') * 10 + (cur[11] - '0'));

      http_hdr_version_set(hh, version);
      http_hdr_status_set(hh, status);
      http_hdr_reason_set(heap, hh, reason_start, (int)(reason_end - reason_start), must_copy_strings);

      end                    = real_end;
      parser->m_parsing_http = false;
      return mime_parser_parse(&parser->m_mime_parser, heap, hh->m_fields_impl, start, end, must_copy_strings, eof);
    }
#endif

  slow_case:
    version_start = nullptr;
    version_end   = nullptr;
    status_start  = nullptr;
    status_end    = nullptr;
    reason_start  = nullptr;
    reason_end    = nullptr;

    version_start = cur = line_start;
    if ((*cur != 'H') && (*cur != 'h')) {
      goto eoh;
    }
    GETNEXT(eoh);
    if ((*cur != 'T') && (*cur != 't')) {
      goto eoh;
    }
    GETNEXT(eoh);
    if ((*cur != 'T') && (*cur != 't')) {
      goto eoh;
    }
    GETNEXT(eoh);
    if ((*cur != 'P') && (*cur != 'p')) {
      goto eoh;
    }
    GETNEXT(eoh);
    if (*cur != '/') {
      goto eoh;
    }
    GETNEXT(eoh);
  parse_version2:
    if (ParseRules::is_digit(*cur)) {
      GETNEXT(eoh);
      goto parse_version2;
    }
    if (*cur == '.') {
      GETNEXT(eoh);
      goto parse_version3;
    }
    goto eoh;
  parse_version3:
    if (ParseRules::is_digit(*cur)) {
      GETNEXT(eoh);
      goto parse_version3;
    }
    if (ParseRules::is_ws(*cur)) {
      version_end = cur;
      GETNEXT(eoh);
      goto parse_status1;
    }
    goto eoh;

  parse_status1:
    if (ParseRules::is_ws(*cur)) {
      GETNEXT(done);
      goto parse_status1;
    }
    status_start = cur;
  parse_status2:
    status_end = cur;
    if (ParseRules::is_digit(*cur)) {
      GETNEXT(done);
      goto parse_status2;
    }
    if (ParseRules::is_ws(*cur)) {
      GETNEXT(done);
      goto parse_reason1;
    }
    goto done;

  parse_reason1:
    if (ParseRules::is_ws(*cur)) {
      GETNEXT(done);
      goto parse_reason1;
    }
    reason_start = cur;
    reason_end   = end - 1;
    while ((reason_end >= line_start) && (ParseRules::is_cr(*reason_end) || ParseRules::is_lf(*reason_end))) {
      reason_end -= 1;
    }
    reason_end += 1;
    goto done;

  eoh:
    *start = old_start;
    if (parser->m_allow_non_http) {
      return PARSE_RESULT_DONE;
    } else {
      return PARSE_RESULT_ERROR;
    }

  done:
    if (!version_start || !version_end) {
      return PARSE_RESULT_ERROR;
    }

    int32_t version;
    version = http_parse_version(version_start, version_end);

    if (version == HTTP_VERSION(0, 9)) {
      return PARSE_RESULT_ERROR;
    }

    http_hdr_version_set(hh, version);

    if (status_start && status_end) {
      http_hdr_status_set(hh, http_parse_status(status_start, status_end));
    }

    if (reason_start && reason_end) {
      http_hdr_reason_set(heap, hh, reason_start, (int)(reason_end - reason_start), must_copy_strings);
    }

    end                    = real_end;
    parser->m_parsing_http = false;
  }

  return mime_parser_parse(&parser->m_mime_parser, heap, hh->m_fields_impl, start, end, must_copy_strings, eof);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

HTTPStatus
http_parse_status(const char *start, const char *end)
{
  int status = 0;

  while ((start != end) && ParseRules::is_space(*start)) {
    start += 1;
  }

  while ((start != end) && ParseRules::is_digit(*start)) {
    status = (status * 10) + (*start++ - '0');
  }

  return (HTTPStatus)status;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int32_t
http_parse_version(const char *start, const char *end)
{
  int maj;
  int min;

  if ((end - start) < 8) {
    return HTTP_VERSION(0, 9);
  }

  if (((start[0] == 'H') || (start[0] == 'h')) && ((start[1] == 'T') || (start[1] == 't')) &&
      ((start[2] == 'T') || (start[2] == 't')) && ((start[3] == 'P') || (start[3] == 'p')) && (start[4] == '/')) {
    start += 5;

    maj = 0;
    min = 0;

    while ((start != end) && ParseRules::is_digit(*start)) {
      maj = (maj * 10) + (*start - '0');
      start += 1;
    }

    if (*start == '.') {
      start += 1;
    }

    while ((start != end) && ParseRules::is_digit(*start)) {
      min = (min * 10) + (*start - '0');
      start += 1;
    }

    return HTTP_VERSION(maj, min);
  }

  return HTTP_VERSION(0, 9);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static char *
http_str_store(Arena *arena, const char *str, int length)
{
  const char *wks;
  int idx = hdrtoken_tokenize(str, length, &wks);
  if (idx < 0) {
    return arena->str_store(str, length);
  } else {
    return (char *)wks;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static void
http_skip_ws(const char *&buf, int &len)
{
  while (len > 0 && *buf && ParseRules::is_ws(*buf)) {
    buf += 1;
    len -= 1;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static double
http_parse_qvalue(const char *&buf, int &len)
{
  double val = 1.0;

  if (*buf != ';') {
    return val;
  }

  buf += 1;
  len -= 1;

  while (len > 0 && *buf) {
    http_skip_ws(buf, len);

    if (*buf == 'q') {
      buf += 1;
      len -= 1;
      http_skip_ws(buf, len);

      if (*buf == '=') {
        double n;
        int f;

        buf += 1;
        len -= 1;
        http_skip_ws(buf, len);

        n = 0.0;
        while (len > 0 && *buf && ParseRules::is_digit(*buf)) {
          n = (n * 10) + (*buf++ - '0');
          len -= 1;
        }

        if (*buf == '.') {
          buf += 1;
          len -= 1;

          f = 10;
          while (len > 0 && *buf && ParseRules::is_digit(*buf)) {
            n += (*buf++ - '0') / (double)f;
            f *= 10;
            len -= 1;
          }
        }

        val = n;
      }
    } else {
      // The current parameter is not a q-value, so go to the next param.
      while (len > 0 && *buf) {
        if (*buf != ';') {
          buf += 1;
          len -= 1;
        } else {
          // Move to the character after the semicolon.
          buf += 1;
          len -= 1;
          break;
        }
      }
    }
  }

  return val;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
  TE        = "TE" ":" #( t-codings )
  t-codings = "trailers" | ( transfer-extension [ accept-params ] )
  -------------------------------------------------------------------------*/

HTTPValTE *
http_parse_te(const char *buf, int len, Arena *arena)
{
  HTTPValTE *val;
  const char *s;

  http_skip_ws(buf, len);

  s = buf;

  while (len > 0 && *buf && (*buf != ';')) {
    buf += 1;
    len -= 1;
  }

  val           = (HTTPValTE *)arena->alloc(sizeof(HTTPValTE));
  val->encoding = http_str_store(arena, s, (int)(buf - s));
  val->qvalue   = http_parse_qvalue(buf, len);

  return val;
}

void
HTTPHdr::_fill_target_cache() const
{
  URL *url = this->url_get();
  const char *port_ptr;
  int port_len;

  m_target_in_url  = false;
  m_port_in_header = false;
  m_host_mime      = nullptr;
  // Check in the URL first, then the HOST field.
  if (nullptr != url->host_get(&m_host_length)) {
    m_target_in_url  = true;
    m_port           = url->port_get();
    m_port_in_header = 0 != url->port_get_raw();
    m_host_mime      = nullptr;
  } else if (nullptr !=
             (m_host_mime = const_cast<HTTPHdr *>(this)->get_host_port_values(nullptr, &m_host_length, &port_ptr, &port_len))) {
    m_port = 0;
    if (port_ptr) {
      for (; port_len > 0 && is_digit(*port_ptr); ++port_ptr, --port_len) {
        m_port = m_port * 10 + *port_ptr - '0';
      }
    }
    m_port_in_header = (0 != m_port);
    m_port           = url_canonicalize_port(url->m_url_impl->m_url_type, m_port);
  }

  m_target_cached = true;
}

void
HTTPHdr::set_url_target_from_host_field(URL *url)
{
  this->_test_and_fill_target_cache();

  if (!url) {
    // Use local cached URL and don't copy if the target
    // is already there.
    if (!m_target_in_url && m_host_mime && m_host_length) {
      m_url_cached.host_set(m_host_mime->m_ptr_value, m_host_length);
      if (m_port_in_header) {
        m_url_cached.port_set(m_port);
      }
      m_target_in_url = true; // it's there now.
    }
  } else {
    int host_len     = 0;
    const char *host = host_get(&host_len);

    url->host_set(host, host_len);
    if (m_port_in_header) {
      url->port_set(m_port);
    }
  }
}

// Very ugly, but a proper implementation will require
// rewriting the URL class and all of its clients so that
// clients access the URL through the HTTP header instance
// unless they really need low level access. The header would
// need to either keep two versions of the URL (pristine
// and effective) or URl would have to provide access to
// the URL printer.

/// Hack the URL in the HTTP header to be 1.0 compliant, saving the
/// original values so they can be restored.
class UrlPrintHack
{
  friend class HTTPHdr;
  UrlPrintHack(HTTPHdr *hdr)
  {
    hdr->_test_and_fill_target_cache();
    if (hdr->m_url_cached.valid()) {
      URLImpl *ui = hdr->m_url_cached.m_url_impl;

      m_hdr = hdr; // mark as potentially having modified values.

      /* Get dirty. We reach in to the URL implementation to
         set the host and port if
         1) They are not already set
         AND
         2) The values were in a HTTP header.
      */
      if (!hdr->m_target_in_url && hdr->m_host_length && hdr->m_host_mime) {
        ink_assert(nullptr == ui->m_ptr_host); // shouldn't be non-zero if not in URL.
        ui->m_ptr_host    = hdr->m_host_mime->m_ptr_value;
        ui->m_len_host    = hdr->m_host_length;
        m_host_modified_p = true;
      } else {
        m_host_modified_p = false;
      }

      if (0 == hdr->m_url_cached.port_get_raw() && hdr->m_port_in_header) {
        ink_assert(nullptr == ui->m_ptr_port); // shouldn't be set if not in URL.
        ui->m_ptr_port    = m_port_buff;
        ui->m_len_port    = snprintf(m_port_buff, sizeof(m_port_buff), "%d", hdr->m_port);
        ui->m_port        = hdr->m_port;
        m_port_modified_p = true;
      } else {
        m_port_modified_p = false;
      }
    } else {
      m_hdr = nullptr;
    }
  }

  /// Destructor.
  ~UrlPrintHack()
  {
    if (m_hdr) { // There was a potentially modified header.
      URLImpl *ui = m_hdr->m_url_cached.m_url_impl;
      // Because we only modified if not set, we can just set these values
      // back to zero if modified. We want to be careful because if a
      // heap re-allocation happened while this was active, then a saved value
      // is wrong and will break things if restored. We don't have to worry
      // about these because, if modified, they were originally NULL and should
      // still be NULL after a re-allocate.
      if (m_port_modified_p) {
        ui->m_len_port = 0;
        ui->m_ptr_port = nullptr;
        ui->m_port     = 0;
      }
      if (m_host_modified_p) {
        ui->m_len_host = 0;
        ui->m_ptr_host = nullptr;
      }
    }
  }

  /// Check if the hack worked
  bool
  is_valid() const
  {
    return nullptr != m_hdr;
  }

  /// Saved values.
  ///@{
  bool m_host_modified_p = false;
  bool m_port_modified_p = false;
  HTTPHdr *m_hdr         = nullptr;
  ///@}
  /// Temporary buffer for port data.
  char m_port_buff[32];
};

char *
HTTPHdr::url_string_get(Arena *arena, int *length)
{
  char *zret = nullptr;
  UrlPrintHack hack(this);

  if (hack.is_valid()) {
    // The use of a magic value for Arena to indicate the internal heap is
    // even uglier but it's less so than duplicating this entire method to
    // change that one thing.

    zret = (arena == USE_HDR_HEAP_MAGIC) ? m_url_cached.string_get_ref(length) : m_url_cached.string_get(arena, length);
  }
  return zret;
}

int
HTTPHdr::url_print(char *buff, int length, int *offset, int *skip)
{
  ink_release_assert(offset);
  ink_release_assert(skip);

  int zret = 0;
  UrlPrintHack hack(this);
  if (hack.is_valid()) {
    zret = m_url_cached.print(buff, length, offset, skip);
  }
  return zret;
}

/***********************************************************************
 *                                                                     *
 *                        M A R S H A L I N G                          *
 *                                                                     *
 ***********************************************************************/

int
HTTPHdr::unmarshal(char *buf, int len, RefCountObj *block_ref)
{
  m_heap = (HdrHeap *)buf;

  int res = m_heap->unmarshal(len, HDR_HEAP_OBJ_HTTP_HEADER, (HdrHeapObjImpl **)&m_http, block_ref);

  if (res > 0) {
    m_mime = m_http->m_fields_impl;
  } else {
    clear();
  }

  return res;
}

int
HTTPHdrImpl::marshal(MarshalXlate *ptr_xlate, int num_ptr, MarshalXlate *str_xlate, int num_str)
{
  if (m_polarity == HTTP_TYPE_REQUEST) {
    HDR_MARSHAL_STR(u.req.m_ptr_method, str_xlate, num_str);
    HDR_MARSHAL_PTR(u.req.m_url_impl, URLImpl, ptr_xlate, num_ptr);
  } else if (m_polarity == HTTP_TYPE_RESPONSE) {
    HDR_MARSHAL_STR(u.resp.m_ptr_reason, str_xlate, num_str);
  } else {
    ink_release_assert(!"unknown m_polarity");
  }

  HDR_MARSHAL_PTR(m_fields_impl, MIMEHdrImpl, ptr_xlate, num_ptr);

  return 0;
}

void
HTTPHdrImpl::unmarshal(intptr_t offset)
{
  if (m_polarity == HTTP_TYPE_REQUEST) {
    HDR_UNMARSHAL_STR(u.req.m_ptr_method, offset);
    HDR_UNMARSHAL_PTR(u.req.m_url_impl, URLImpl, offset);
  } else if (m_polarity == HTTP_TYPE_RESPONSE) {
    HDR_UNMARSHAL_STR(u.resp.m_ptr_reason, offset);
  } else {
    ink_release_assert(!"unknown m_polarity");
  }

  HDR_UNMARSHAL_PTR(m_fields_impl, MIMEHdrImpl, offset);
}

void
HTTPHdrImpl::move_strings(HdrStrHeap *new_heap)
{
  if (m_polarity == HTTP_TYPE_REQUEST) {
    HDR_MOVE_STR(u.req.m_ptr_method, u.req.m_len_method);
  } else if (m_polarity == HTTP_TYPE_RESPONSE) {
    HDR_MOVE_STR(u.resp.m_ptr_reason, u.resp.m_len_reason);
  } else {
    ink_release_assert(!"unknown m_polarity");
  }
}

size_t
HTTPHdrImpl::strings_length()
{
  size_t ret = 0;

  if (m_polarity == HTTP_TYPE_REQUEST) {
    ret += u.req.m_len_method;
  } else if (m_polarity == HTTP_TYPE_RESPONSE) {
    ret += u.resp.m_len_reason;
  }
  return ret;
}

void
HTTPHdrImpl::check_strings(HeapCheck *heaps, int num_heaps)
{
  if (m_polarity == HTTP_TYPE_REQUEST) {
    CHECK_STR(u.req.m_ptr_method, u.req.m_len_method, heaps, num_heaps);
  } else if (m_polarity == HTTP_TYPE_RESPONSE) {
    CHECK_STR(u.resp.m_ptr_reason, u.resp.m_len_reason, heaps, num_heaps);
  } else {
    ink_release_assert(!"unknown m_polarity");
  }
}

ClassAllocator<HTTPCacheAlt> httpCacheAltAllocator("httpCacheAltAllocator");

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
int constexpr HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS;

HTTPCacheAlt::HTTPCacheAlt()
  : m_magic(CACHE_ALT_MAGIC_ALIVE),
    m_writeable(1),
    m_unmarshal_len(-1),
    m_id(-1),
    m_rid(-1),
    m_request_hdr(),
    m_response_hdr(),
    m_request_sent_time(0),
    m_response_received_time(0),
    m_frag_offset_count(0),
    m_frag_offsets(nullptr),
    m_ext_buffer(nullptr)
{
  memset(&m_object_key[0], 0, CRYPTO_HASH_SIZE);
  m_object_size[0] = 0;
  m_object_size[1] = 0;
}

void
HTTPCacheAlt::destroy()
{
  ink_assert(m_magic == CACHE_ALT_MAGIC_ALIVE);
  ink_assert(m_writeable);
  m_magic     = CACHE_ALT_MAGIC_DEAD;
  m_writeable = 0;
  m_request_hdr.destroy();
  m_response_hdr.destroy();
  m_frag_offset_count = 0;
  if (m_frag_offsets && m_frag_offsets != m_integral_frag_offsets) {
    ats_free(m_frag_offsets);
    m_frag_offsets = nullptr;
  }
  httpCacheAltAllocator.free(this);
}

void
HTTPCacheAlt::copy(HTTPCacheAlt *to_copy)
{
  m_magic = to_copy->m_magic;
  // m_writeable =      to_copy->m_writeable;
  m_unmarshal_len = to_copy->m_unmarshal_len;
  m_id            = to_copy->m_id;
  m_rid           = to_copy->m_rid;
  memcpy(&m_object_key[0], &to_copy->m_object_key[0], CRYPTO_HASH_SIZE);
  m_object_size[0] = to_copy->m_object_size[0];
  m_object_size[1] = to_copy->m_object_size[1];

  if (to_copy->m_request_hdr.valid()) {
    m_request_hdr.copy(&to_copy->m_request_hdr);
  }

  if (to_copy->m_response_hdr.valid()) {
    m_response_hdr.copy(&to_copy->m_response_hdr);
  }

  m_request_sent_time      = to_copy->m_request_sent_time;
  m_response_received_time = to_copy->m_response_received_time;
  this->copy_frag_offsets_from(to_copy);
}

void
HTTPCacheAlt::copy_frag_offsets_from(HTTPCacheAlt *src)
{
  m_frag_offset_count = src->m_frag_offset_count;
  if (m_frag_offset_count > 0) {
    if (m_frag_offset_count > N_INTEGRAL_FRAG_OFFSETS) {
      /* Mixed feelings about this - technically we don't need it to be a
         power of two when copied because currently that means it is frozen.
         But that could change later and it would be a nasty bug to find.
         So we'll do it for now. The relative overhead is tiny.
      */
      int bcount = HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS * 2;
      while (bcount < m_frag_offset_count) {
        bcount *= 2;
      }
      m_frag_offsets = static_cast<FragOffset *>(ats_malloc(sizeof(FragOffset) * bcount));
    } else {
      m_frag_offsets = m_integral_frag_offsets;
    }
    memcpy(m_frag_offsets, src->m_frag_offsets, sizeof(FragOffset) * m_frag_offset_count);
  }
}

const int HTTP_ALT_MARSHAL_SIZE = ROUND(sizeof(HTTPCacheAlt), HDR_PTR_SIZE);

void
HTTPInfo::create()
{
  m_alt = httpCacheAltAllocator.alloc();
}

void
HTTPInfo::copy(HTTPInfo *hi)
{
  if (m_alt && m_alt->m_writeable) {
    destroy();
  }

  create();
  m_alt->copy(hi->m_alt);
}

void
HTTPInfo::copy_frag_offsets_from(HTTPInfo *src)
{
  if (m_alt && src->m_alt) {
    m_alt->copy_frag_offsets_from(src->m_alt);
  }
}

int
HTTPInfo::marshal_length()
{
  int len = HTTP_ALT_MARSHAL_SIZE;

  if (m_alt->m_request_hdr.valid()) {
    len += m_alt->m_request_hdr.m_heap->marshal_length();
  }

  if (m_alt->m_response_hdr.valid()) {
    len += m_alt->m_response_hdr.m_heap->marshal_length();
  }

  if (m_alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    len += sizeof(FragOffset) * m_alt->m_frag_offset_count;
  }

  return len;
}

int
HTTPInfo::marshal(char *buf, int len)
{
  int tmp;
  int used                  = 0;
  HTTPCacheAlt *marshal_alt = (HTTPCacheAlt *)buf;
  // non-zero only if the offsets are external. Otherwise they get
  // marshalled along with the alt struct.
  ink_assert(m_alt->m_magic == CACHE_ALT_MAGIC_ALIVE);

  // Make sure the buffer is aligned
  //    ink_assert(((intptr_t)buf) & 0x3 == 0);

  // Memcpy the whole object so that we can use it
  //   live later.  This involves copying a few
  //   extra bytes now but will save copying any
  //   bytes on the way out of the cache
  memcpy(buf, m_alt, sizeof(HTTPCacheAlt));
  marshal_alt->m_magic         = CACHE_ALT_MAGIC_MARSHALED;
  marshal_alt->m_writeable     = 0;
  marshal_alt->m_unmarshal_len = -1;
  marshal_alt->m_ext_buffer    = nullptr;
  buf += HTTP_ALT_MARSHAL_SIZE;
  used += HTTP_ALT_MARSHAL_SIZE;

  if (m_alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    marshal_alt->m_frag_offsets = static_cast<FragOffset *>(reinterpret_cast<void *>(used));
    memcpy(buf, m_alt->m_frag_offsets, m_alt->m_frag_offset_count * sizeof(FragOffset));
    buf += m_alt->m_frag_offset_count * sizeof(FragOffset);
    used += m_alt->m_frag_offset_count * sizeof(FragOffset);
  } else {
    marshal_alt->m_frag_offsets = nullptr;
  }

  // The m_{request,response}_hdr->m_heap pointers are converted
  //    to zero based offsets from the start of the buffer we're
  //    marshalling in to
  if (m_alt->m_request_hdr.valid()) {
    tmp                               = m_alt->m_request_hdr.m_heap->marshal(buf, len - used);
    marshal_alt->m_request_hdr.m_heap = (HdrHeap *)(intptr_t)used;
    ink_assert(((intptr_t)marshal_alt->m_request_hdr.m_heap) < len);
    buf += tmp;
    used += tmp;
  } else {
    marshal_alt->m_request_hdr.m_heap = nullptr;
  }

  if (m_alt->m_response_hdr.valid()) {
    tmp                                = m_alt->m_response_hdr.m_heap->marshal(buf, len - used);
    marshal_alt->m_response_hdr.m_heap = (HdrHeap *)(intptr_t)used;
    ink_assert(((intptr_t)marshal_alt->m_response_hdr.m_heap) < len);
    used += tmp;
  } else {
    marshal_alt->m_response_hdr.m_heap = nullptr;
  }

  // The prior system failed the marshal if there wasn't
  //   enough space by measuring the space for every
  //   component. Seems much faster to check once to
  //   see if we spammed memory
  ink_release_assert(used <= len);

  return used;
}

int
HTTPInfo::unmarshal(char *buf, int len, RefCountObj *block_ref)
{
  HTTPCacheAlt *alt = (HTTPCacheAlt *)buf;
  int orig_len      = len;

  if (alt->m_magic == CACHE_ALT_MAGIC_ALIVE) {
    // Already unmarshaled, must be a ram cache
    //  it
    ink_assert(alt->m_unmarshal_len > 0);
    ink_assert(alt->m_unmarshal_len <= len);
    return alt->m_unmarshal_len;
  } else if (alt->m_magic != CACHE_ALT_MAGIC_MARSHALED) {
    ink_assert(!"HTTPInfo::unmarshal bad magic");
    return -1;
  }

  ink_assert(alt->m_unmarshal_len < 0);
  alt->m_magic = CACHE_ALT_MAGIC_ALIVE;
  ink_assert(alt->m_writeable == 0);
  len -= HTTP_ALT_MARSHAL_SIZE;

  if (alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    alt->m_frag_offsets = reinterpret_cast<FragOffset *>(buf + reinterpret_cast<intptr_t>(alt->m_frag_offsets));
    len -= sizeof(FragOffset) * alt->m_frag_offset_count;
    ink_assert(len >= 0);
  } else if (alt->m_frag_offset_count > 0) {
    alt->m_frag_offsets = alt->m_integral_frag_offsets;
  } else {
    alt->m_frag_offsets = nullptr; // should really already be zero.
  }

  HdrHeap *heap   = (HdrHeap *)(alt->m_request_hdr.m_heap ? (buf + (intptr_t)alt->m_request_hdr.m_heap) : nullptr);
  HTTPHdrImpl *hh = nullptr;
  int tmp;
  if (heap != nullptr) {
    tmp = heap->unmarshal(len, HDR_HEAP_OBJ_HTTP_HEADER, (HdrHeapObjImpl **)&hh, block_ref);
    if (hh == nullptr || tmp < 0) {
      ink_assert(!"HTTPInfo::request unmarshal failed");
      return -1;
    }
    len -= tmp;
    alt->m_request_hdr.m_heap              = heap;
    alt->m_request_hdr.m_http              = hh;
    alt->m_request_hdr.m_mime              = hh->m_fields_impl;
    alt->m_request_hdr.m_url_cached.m_heap = heap;
  }

  heap = (HdrHeap *)(alt->m_response_hdr.m_heap ? (buf + (intptr_t)alt->m_response_hdr.m_heap) : nullptr);
  if (heap != nullptr) {
    tmp = heap->unmarshal(len, HDR_HEAP_OBJ_HTTP_HEADER, (HdrHeapObjImpl **)&hh, block_ref);
    if (hh == nullptr || tmp < 0) {
      ink_assert(!"HTTPInfo::response unmarshal failed");
      return -1;
    }
    len -= tmp;

    alt->m_response_hdr.m_heap = heap;
    alt->m_response_hdr.m_http = hh;
    alt->m_response_hdr.m_mime = hh->m_fields_impl;
  }

  alt->m_unmarshal_len = orig_len - len;

  return alt->m_unmarshal_len;
}

int
HTTPInfo::unmarshal_v24_1(char *buf, int len, RefCountObj *block_ref)
{
  HTTPCacheAlt *alt = (HTTPCacheAlt *)buf;
  int orig_len      = len;

  if (alt->m_magic == CACHE_ALT_MAGIC_ALIVE) {
    // Already unmarshaled, must be a ram cache
    //  it
    ink_assert(alt->m_unmarshal_len > 0);
    ink_assert(alt->m_unmarshal_len <= len);
    return alt->m_unmarshal_len;
  } else if (alt->m_magic != CACHE_ALT_MAGIC_MARSHALED) {
    ink_assert(!"HTTPInfo::unmarshal bad magic");
    return -1;
  }

  ink_assert(alt->m_unmarshal_len < 0);
  alt->m_magic = CACHE_ALT_MAGIC_ALIVE;
  ink_assert(alt->m_writeable == 0);
  len -= HTTP_ALT_MARSHAL_SIZE;

  if (alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    // stuff that didn't fit in the integral slots.
    int extra       = sizeof(FragOffset) * alt->m_frag_offset_count - sizeof(alt->m_integral_frag_offsets);
    char *extra_src = buf + reinterpret_cast<intptr_t>(alt->m_frag_offsets);
    // Actual buffer size, which must be a power of two.
    // Well, technically not, because we never modify an unmarshalled fragment
    // offset table, but it would be a nasty bug should that be done in the
    // future.
    int bcount = HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS * 2;

    while (bcount < alt->m_frag_offset_count) {
      bcount *= 2;
    }
    alt->m_frag_offsets =
      static_cast<FragOffset *>(ats_malloc(bcount * sizeof(FragOffset))); // WRONG - must round up to next power of 2.
    memcpy(alt->m_frag_offsets, alt->m_integral_frag_offsets, sizeof(alt->m_integral_frag_offsets));
    memcpy(alt->m_frag_offsets + HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS, extra_src, extra);
    len -= extra;
  } else if (alt->m_frag_offset_count > 0) {
    alt->m_frag_offsets = alt->m_integral_frag_offsets;
  } else {
    alt->m_frag_offsets = nullptr; // should really already be zero.
  }

  HdrHeap *heap   = (HdrHeap *)(alt->m_request_hdr.m_heap ? (buf + (intptr_t)alt->m_request_hdr.m_heap) : nullptr);
  HTTPHdrImpl *hh = nullptr;
  int tmp;
  if (heap != nullptr) {
    tmp = heap->unmarshal(len, HDR_HEAP_OBJ_HTTP_HEADER, (HdrHeapObjImpl **)&hh, block_ref);
    if (hh == nullptr || tmp < 0) {
      ink_assert(!"HTTPInfo::request unmarshal failed");
      return -1;
    }
    len -= tmp;
    alt->m_request_hdr.m_heap              = heap;
    alt->m_request_hdr.m_http              = hh;
    alt->m_request_hdr.m_mime              = hh->m_fields_impl;
    alt->m_request_hdr.m_url_cached.m_heap = heap;
  }

  heap = (HdrHeap *)(alt->m_response_hdr.m_heap ? (buf + (intptr_t)alt->m_response_hdr.m_heap) : nullptr);
  if (heap != nullptr) {
    tmp = heap->unmarshal(len, HDR_HEAP_OBJ_HTTP_HEADER, (HdrHeapObjImpl **)&hh, block_ref);
    if (hh == nullptr || tmp < 0) {
      ink_assert(!"HTTPInfo::response unmarshal failed");
      return -1;
    }
    len -= tmp;

    alt->m_response_hdr.m_heap = heap;
    alt->m_response_hdr.m_http = hh;
    alt->m_response_hdr.m_mime = hh->m_fields_impl;
  }

  alt->m_unmarshal_len = orig_len - len;

  return alt->m_unmarshal_len;
}

// bool HTTPInfo::check_marshalled(char* buf, int len)
//  Checks a marhshalled HTTPInfo buffer to make
//    sure it's sane.  Returns true if sane, false otherwise
//
bool
HTTPInfo::check_marshalled(char *buf, int len)
{
  HTTPCacheAlt *alt = (HTTPCacheAlt *)buf;

  if (alt->m_magic != CACHE_ALT_MAGIC_MARSHALED) {
    return false;
  }

  if (alt->m_writeable != false) {
    return false;
  }

  if (len < HTTP_ALT_MARSHAL_SIZE) {
    return false;
  }

  if (alt->m_request_hdr.m_heap == nullptr) {
    return false;
  }

  if ((intptr_t)alt->m_request_hdr.m_heap > len) {
    return false;
  }

  HdrHeap *heap = (HdrHeap *)(buf + (intptr_t)alt->m_request_hdr.m_heap);
  if (heap->check_marshalled(len) == false) {
    return false;
  }

  if (alt->m_response_hdr.m_heap == nullptr) {
    return false;
  }

  if ((intptr_t)alt->m_response_hdr.m_heap > len) {
    return false;
  }

  heap = (HdrHeap *)(buf + (intptr_t)alt->m_response_hdr.m_heap);
  if (heap->check_marshalled(len) == false) {
    return false;
  }

  return true;
}

// void HTTPInfo::set_buffer_reference(RefCountObj* block_ref)
//
//    Setting a buffer reference for the alt is separate from
//     the unmarshalling operation because the clustering
//     utilizes the system differently than cache does
//    The cache maintains external refcounting of the buffer that
//     the alt is in & doesn't always destroy the alt when its
//     done with it because it figures it doesn't need to since
//     it is managing the buffer
//    The receiver of ClusterRPC system has the alt manage the
//     buffer itself and therefore needs to call this function
//     to set up the reference
//
void
HTTPInfo::set_buffer_reference(RefCountObj *block_ref)
{
  ink_assert(m_alt->m_magic == CACHE_ALT_MAGIC_ALIVE);

  // Free existing reference
  if (m_alt->m_ext_buffer != nullptr) {
    if (m_alt->m_ext_buffer->refcount_dec() == 0) {
      m_alt->m_ext_buffer->free();
    }
  }
  // Set up the ref count for the external buffer
  //   if there is one
  if (block_ref) {
    block_ref->refcount_inc();
  }

  m_alt->m_ext_buffer = block_ref;
}

int
HTTPInfo::get_handle(char *buf, int len)
{
  // All the offsets have already swizzled to pointers.  All we
  //  need to do is set m_alt and make sure things are sane
  HTTPCacheAlt *a = (HTTPCacheAlt *)buf;

  if (a->m_magic == CACHE_ALT_MAGIC_ALIVE) {
    m_alt = a;
    ink_assert(m_alt->m_unmarshal_len > 0);
    ink_assert(m_alt->m_unmarshal_len <= len);
    return m_alt->m_unmarshal_len;
  }

  clear();
  return -1;
}

void
HTTPInfo::push_frag_offset(FragOffset offset)
{
  ink_assert(m_alt);
  if (nullptr == m_alt->m_frag_offsets) {
    m_alt->m_frag_offsets = m_alt->m_integral_frag_offsets;
  } else if (m_alt->m_frag_offset_count >= HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS &&
             0 == (m_alt->m_frag_offset_count & (m_alt->m_frag_offset_count - 1))) {
    // need more space than in integral storage and we're at an upgrade
    // size (power of 2).
    FragOffset *nf = static_cast<FragOffset *>(ats_malloc(sizeof(FragOffset) * (m_alt->m_frag_offset_count * 2)));
    memcpy(nf, m_alt->m_frag_offsets, sizeof(FragOffset) * m_alt->m_frag_offset_count);
    if (m_alt->m_frag_offsets != m_alt->m_integral_frag_offsets) {
      ats_free(m_alt->m_frag_offsets);
    }
    m_alt->m_frag_offsets = nf;
  }

  m_alt->m_frag_offsets[m_alt->m_frag_offset_count++] = offset;
}
