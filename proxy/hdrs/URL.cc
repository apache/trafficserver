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

#include <cassert>
#include <new>
#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/TsBuffer.h"
#include "URL.h"
#include "MIME.h"
#include "HTTP.h"
#include "tscore/Diags.h"

const char *URL_SCHEME_FILE;
const char *URL_SCHEME_FTP;
const char *URL_SCHEME_GOPHER;
const char *URL_SCHEME_HTTP;
const char *URL_SCHEME_HTTPS;
const char *URL_SCHEME_WSS;
const char *URL_SCHEME_WS;
const char *URL_SCHEME_MAILTO;
const char *URL_SCHEME_NEWS;
const char *URL_SCHEME_NNTP;
const char *URL_SCHEME_PROSPERO;
const char *URL_SCHEME_TELNET;
const char *URL_SCHEME_TUNNEL;
const char *URL_SCHEME_WAIS;
const char *URL_SCHEME_PNM;
const char *URL_SCHEME_RTSP;
const char *URL_SCHEME_RTSPU;
const char *URL_SCHEME_MMS;
const char *URL_SCHEME_MMSU;
const char *URL_SCHEME_MMST;

int URL_WKSIDX_FILE;
int URL_WKSIDX_FTP;
int URL_WKSIDX_GOPHER;
int URL_WKSIDX_HTTP;
int URL_WKSIDX_HTTPS;
int URL_WKSIDX_WS;
int URL_WKSIDX_WSS;
int URL_WKSIDX_MAILTO;
int URL_WKSIDX_NEWS;
int URL_WKSIDX_NNTP;
int URL_WKSIDX_PROSPERO;
int URL_WKSIDX_TELNET;
int URL_WKSIDX_TUNNEL;
int URL_WKSIDX_WAIS;
int URL_WKSIDX_PNM;
int URL_WKSIDX_RTSP;
int URL_WKSIDX_RTSPU;
int URL_WKSIDX_MMS;
int URL_WKSIDX_MMSU;
int URL_WKSIDX_MMST;

int URL_LEN_FILE;
int URL_LEN_FTP;
int URL_LEN_GOPHER;
int URL_LEN_HTTP;
int URL_LEN_HTTPS;
int URL_LEN_WS;
int URL_LEN_WSS;
int URL_LEN_MAILTO;
int URL_LEN_NEWS;
int URL_LEN_NNTP;
int URL_LEN_PROSPERO;
int URL_LEN_TELNET;
int URL_LEN_TUNNEL;
int URL_LEN_WAIS;
int URL_LEN_PNM;
int URL_LEN_RTSP;
int URL_LEN_RTSPU;
int URL_LEN_MMS;
int URL_LEN_MMSU;
int URL_LEN_MMST;

// Whether we should implement url_CryptoHash_get() using url_CryptoHash_get_fast(). Note that
// url_CryptoHash_get_fast() does NOT produce the same result as url_CryptoHash_get_general().
static int url_hash_method = 0;

// test to see if a character is a valid character for a host in a URI according to
// RFC 3986 and RFC 1034
inline static int
is_host_char(char c)
{
  return (ParseRules::is_alnum(c) || (c == '-') || (c == '.') || (c == '[') || (c == ']') || (c == '_') || (c == ':') ||
          (c == '~') || (c == '%'));
}

// Checks if `addr` is a valid FQDN string
bool
validate_host_name(std::string_view addr)
{
  return std::all_of(addr.begin(), addr.end(), &is_host_char);
}

/**
   Checks if the (un-well-known) scheme is valid

   RFC 3986 Section 3.1
   These are the valid characters in a scheme:
     scheme      = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
   return an error if there is another character in the scheme
*/
bool
validate_scheme(std::string_view scheme)
{
  if (scheme.empty()) {
    return false;
  }

  if (!ParseRules::is_alpha(scheme[0])) {
    return false;
  }

  for (size_t i = 0; i < scheme.size(); ++i) {
    const char &c = scheme[i];

    if (!(ParseRules::is_alnum(c) != 0 || c == '+' || c == '-' || c == '.')) {
      return false;
    }
  }

  return true;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_init()
{
  static int init = 1;

  if (init) {
    init = 0;

    hdrtoken_init();

    URL_SCHEME_FILE     = hdrtoken_string_to_wks("file");
    URL_SCHEME_FTP      = hdrtoken_string_to_wks("ftp");
    URL_SCHEME_GOPHER   = hdrtoken_string_to_wks("gopher");
    URL_SCHEME_HTTP     = hdrtoken_string_to_wks("http");
    URL_SCHEME_HTTPS    = hdrtoken_string_to_wks("https");
    URL_SCHEME_WSS      = hdrtoken_string_to_wks("wss");
    URL_SCHEME_WS       = hdrtoken_string_to_wks("ws");
    URL_SCHEME_MAILTO   = hdrtoken_string_to_wks("mailto");
    URL_SCHEME_NEWS     = hdrtoken_string_to_wks("news");
    URL_SCHEME_NNTP     = hdrtoken_string_to_wks("nntp");
    URL_SCHEME_PROSPERO = hdrtoken_string_to_wks("prospero");
    URL_SCHEME_TELNET   = hdrtoken_string_to_wks("telnet");
    URL_SCHEME_TUNNEL   = hdrtoken_string_to_wks("tunnel");
    URL_SCHEME_WAIS     = hdrtoken_string_to_wks("wais");
    URL_SCHEME_PNM      = hdrtoken_string_to_wks("pnm");
    URL_SCHEME_RTSP     = hdrtoken_string_to_wks("rtsp");
    URL_SCHEME_RTSPU    = hdrtoken_string_to_wks("rtspu");
    URL_SCHEME_MMS      = hdrtoken_string_to_wks("mms");
    URL_SCHEME_MMSU     = hdrtoken_string_to_wks("mmsu");
    URL_SCHEME_MMST     = hdrtoken_string_to_wks("mmst");

    ink_assert(URL_SCHEME_FILE && URL_SCHEME_FTP && URL_SCHEME_GOPHER && URL_SCHEME_HTTP && URL_SCHEME_HTTPS && URL_SCHEME_WS &&
               URL_SCHEME_WSS && URL_SCHEME_MAILTO && URL_SCHEME_NEWS && URL_SCHEME_NNTP && URL_SCHEME_PROSPERO &&
               URL_SCHEME_TELNET && URL_SCHEME_TUNNEL && URL_SCHEME_WAIS && URL_SCHEME_PNM && URL_SCHEME_RTSP && URL_SCHEME_RTSPU &&
               URL_SCHEME_MMS && URL_SCHEME_MMSU && URL_SCHEME_MMST);

    URL_WKSIDX_FILE     = hdrtoken_wks_to_index(URL_SCHEME_FILE);
    URL_WKSIDX_FTP      = hdrtoken_wks_to_index(URL_SCHEME_FTP);
    URL_WKSIDX_GOPHER   = hdrtoken_wks_to_index(URL_SCHEME_GOPHER);
    URL_WKSIDX_HTTP     = hdrtoken_wks_to_index(URL_SCHEME_HTTP);
    URL_WKSIDX_HTTPS    = hdrtoken_wks_to_index(URL_SCHEME_HTTPS);
    URL_WKSIDX_WS       = hdrtoken_wks_to_index(URL_SCHEME_WS);
    URL_WKSIDX_WSS      = hdrtoken_wks_to_index(URL_SCHEME_WSS);
    URL_WKSIDX_MAILTO   = hdrtoken_wks_to_index(URL_SCHEME_MAILTO);
    URL_WKSIDX_NEWS     = hdrtoken_wks_to_index(URL_SCHEME_NEWS);
    URL_WKSIDX_NNTP     = hdrtoken_wks_to_index(URL_SCHEME_NNTP);
    URL_WKSIDX_PROSPERO = hdrtoken_wks_to_index(URL_SCHEME_PROSPERO);
    URL_WKSIDX_TELNET   = hdrtoken_wks_to_index(URL_SCHEME_TELNET);
    URL_WKSIDX_TUNNEL   = hdrtoken_wks_to_index(URL_SCHEME_TUNNEL);
    URL_WKSIDX_WAIS     = hdrtoken_wks_to_index(URL_SCHEME_WAIS);
    URL_WKSIDX_PNM      = hdrtoken_wks_to_index(URL_SCHEME_PNM);
    URL_WKSIDX_RTSP     = hdrtoken_wks_to_index(URL_SCHEME_RTSP);
    URL_WKSIDX_RTSPU    = hdrtoken_wks_to_index(URL_SCHEME_RTSPU);
    URL_WKSIDX_MMS      = hdrtoken_wks_to_index(URL_SCHEME_MMS);
    URL_WKSIDX_MMSU     = hdrtoken_wks_to_index(URL_SCHEME_MMSU);
    URL_WKSIDX_MMST     = hdrtoken_wks_to_index(URL_SCHEME_MMST);

    URL_LEN_FILE     = hdrtoken_wks_to_length(URL_SCHEME_FILE);
    URL_LEN_FTP      = hdrtoken_wks_to_length(URL_SCHEME_FTP);
    URL_LEN_GOPHER   = hdrtoken_wks_to_length(URL_SCHEME_GOPHER);
    URL_LEN_HTTP     = hdrtoken_wks_to_length(URL_SCHEME_HTTP);
    URL_LEN_HTTPS    = hdrtoken_wks_to_length(URL_SCHEME_HTTPS);
    URL_LEN_WS       = hdrtoken_wks_to_length(URL_SCHEME_WS);
    URL_LEN_WSS      = hdrtoken_wks_to_length(URL_SCHEME_WSS);
    URL_LEN_MAILTO   = hdrtoken_wks_to_length(URL_SCHEME_MAILTO);
    URL_LEN_NEWS     = hdrtoken_wks_to_length(URL_SCHEME_NEWS);
    URL_LEN_NNTP     = hdrtoken_wks_to_length(URL_SCHEME_NNTP);
    URL_LEN_PROSPERO = hdrtoken_wks_to_length(URL_SCHEME_PROSPERO);
    URL_LEN_TELNET   = hdrtoken_wks_to_length(URL_SCHEME_TELNET);
    URL_LEN_TUNNEL   = hdrtoken_wks_to_length(URL_SCHEME_TUNNEL);
    URL_LEN_WAIS     = hdrtoken_wks_to_length(URL_SCHEME_WAIS);
    URL_LEN_PNM      = hdrtoken_wks_to_length(URL_SCHEME_PNM);
    URL_LEN_RTSP     = hdrtoken_wks_to_length(URL_SCHEME_RTSP);
    URL_LEN_RTSPU    = hdrtoken_wks_to_length(URL_SCHEME_RTSPU);
    URL_LEN_MMS      = hdrtoken_wks_to_length(URL_SCHEME_MMS);
    URL_LEN_MMSU     = hdrtoken_wks_to_length(URL_SCHEME_MMSU);
    URL_LEN_MMST     = hdrtoken_wks_to_length(URL_SCHEME_MMST);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/***********************************************************************
 *                                                                     *
 *             U R L    C R E A T I O N    A N D    C O P Y            *
 *                                                                     *
 ***********************************************************************/

URLImpl *
url_create(HdrHeap *heap)
{
  URLImpl *url;

  url = (URLImpl *)heap->allocate_obj(sizeof(URLImpl), HDR_HEAP_OBJ_URL);
  obj_clear_data((HdrHeapObjImpl *)url);
  url->m_url_type       = URL_TYPE_NONE;
  url->m_scheme_wks_idx = -1;
  url_clear_string_ref(url);
  return url;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_clear(URLImpl *url_impl)
{
  obj_clear_data((HdrHeapObjImpl *)url_impl);
  url_impl->m_url_type       = URL_TYPE_NONE;
  url_impl->m_scheme_wks_idx = -1;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

URLImpl *
url_copy(URLImpl *s_url, HdrHeap *s_heap, HdrHeap *d_heap, bool inherit_strs)
{
  URLImpl *d_url = url_create(d_heap);
  url_copy_onto(s_url, s_heap, d_url, d_heap, inherit_strs);
  return d_url;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_copy_onto(URLImpl *s_url, HdrHeap *s_heap, URLImpl *d_url, HdrHeap *d_heap, bool inherit_strs)
{
  if (s_url != d_url) {
    obj_copy_data((HdrHeapObjImpl *)s_url, (HdrHeapObjImpl *)d_url);
    if (inherit_strs && (s_heap != d_heap)) {
      d_heap->inherit_string_heaps(s_heap);
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_nuke_proxy_stuff(URLImpl *d_url)
{
  d_url->m_len_scheme   = 0;
  d_url->m_len_user     = 0;
  d_url->m_len_password = 0;
  d_url->m_len_host     = 0;
  d_url->m_len_port     = 0;

  d_url->m_ptr_scheme   = nullptr;
  d_url->m_ptr_user     = nullptr;
  d_url->m_ptr_password = nullptr;
  d_url->m_ptr_host     = nullptr;
  d_url->m_ptr_port     = nullptr;

  d_url->m_scheme_wks_idx = -1;
  d_url->m_port           = 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/**
  This routine is like url_copy_onto, but clears the
  scheme/host/user/pass/port components, resulting in a server-style URL.

*/
void
url_copy_onto_as_server_url(URLImpl *s_url, HdrHeap *s_heap, URLImpl *d_url, HdrHeap *d_heap, bool inherit_strs)
{
  url_nuke_proxy_stuff(d_url);

  d_url->m_ptr_path     = s_url->m_ptr_path;
  d_url->m_ptr_params   = s_url->m_ptr_params;
  d_url->m_ptr_query    = s_url->m_ptr_query;
  d_url->m_ptr_fragment = s_url->m_ptr_fragment;
  url_clear_string_ref(d_url);

  d_url->m_len_path     = s_url->m_len_path;
  d_url->m_len_params   = s_url->m_len_params;
  d_url->m_len_query    = s_url->m_len_query;
  d_url->m_len_fragment = s_url->m_len_fragment;

  d_url->m_url_type  = s_url->m_url_type;
  d_url->m_type_code = s_url->m_type_code;

  if (inherit_strs && (s_heap != d_heap)) {
    d_heap->inherit_string_heaps(s_heap);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/***********************************************************************
 *                                                                     *
 *                        M A R S H A L I N G                          *
 *                                                                     *
 ***********************************************************************/
int
URLImpl::marshal(MarshalXlate *str_xlate, int num_xlate)
{
  HDR_MARSHAL_STR(m_ptr_scheme, str_xlate, num_xlate);
  HDR_MARSHAL_STR(m_ptr_user, str_xlate, num_xlate);
  HDR_MARSHAL_STR(m_ptr_password, str_xlate, num_xlate);
  HDR_MARSHAL_STR(m_ptr_host, str_xlate, num_xlate);
  HDR_MARSHAL_STR(m_ptr_port, str_xlate, num_xlate);
  HDR_MARSHAL_STR(m_ptr_path, str_xlate, num_xlate);
  HDR_MARSHAL_STR(m_ptr_params, str_xlate, num_xlate);
  HDR_MARSHAL_STR(m_ptr_query, str_xlate, num_xlate);
  HDR_MARSHAL_STR(m_ptr_fragment, str_xlate, num_xlate);
  //    HDR_MARSHAL_STR(m_ptr_printed_string, str_xlate, num_xlate);
  return 0;
}

void
URLImpl::unmarshal(intptr_t offset)
{
  HDR_UNMARSHAL_STR(m_ptr_scheme, offset);
  HDR_UNMARSHAL_STR(m_ptr_user, offset);
  HDR_UNMARSHAL_STR(m_ptr_password, offset);
  HDR_UNMARSHAL_STR(m_ptr_host, offset);
  HDR_UNMARSHAL_STR(m_ptr_port, offset);
  HDR_UNMARSHAL_STR(m_ptr_path, offset);
  HDR_UNMARSHAL_STR(m_ptr_params, offset);
  HDR_UNMARSHAL_STR(m_ptr_query, offset);
  HDR_UNMARSHAL_STR(m_ptr_fragment, offset);
  //    HDR_UNMARSHAL_STR(m_ptr_printed_string, offset);
}

void
URLImpl::move_strings(HdrStrHeap *new_heap)
{
  HDR_MOVE_STR(m_ptr_scheme, m_len_scheme);
  HDR_MOVE_STR(m_ptr_user, m_len_user);
  HDR_MOVE_STR(m_ptr_password, m_len_password);
  HDR_MOVE_STR(m_ptr_host, m_len_host);
  HDR_MOVE_STR(m_ptr_port, m_len_port);
  HDR_MOVE_STR(m_ptr_path, m_len_path);
  HDR_MOVE_STR(m_ptr_params, m_len_params);
  HDR_MOVE_STR(m_ptr_query, m_len_query);
  HDR_MOVE_STR(m_ptr_fragment, m_len_fragment);
  HDR_MOVE_STR(m_ptr_printed_string, m_len_printed_string);
}

size_t
URLImpl::strings_length()
{
  size_t ret = 0;

  ret += m_len_scheme;
  ret += m_len_user;
  ret += m_len_password;
  ret += m_len_host;
  ret += m_len_port;
  ret += m_len_path;
  ret += m_len_params;
  ret += m_len_query;
  ret += m_len_fragment;
  ret += m_len_printed_string;
  return ret;
}

void
URLImpl::check_strings(HeapCheck *heaps, int num_heaps)
{
  CHECK_STR(m_ptr_scheme, m_len_scheme, heaps, num_heaps);
  CHECK_STR(m_ptr_user, m_len_user, heaps, num_heaps);
  CHECK_STR(m_ptr_password, m_len_password, heaps, num_heaps);
  CHECK_STR(m_ptr_host, m_len_host, heaps, num_heaps);
  CHECK_STR(m_ptr_port, m_len_port, heaps, num_heaps);
  CHECK_STR(m_ptr_path, m_len_path, heaps, num_heaps);
  CHECK_STR(m_ptr_params, m_len_params, heaps, num_heaps);
  CHECK_STR(m_ptr_query, m_len_query, heaps, num_heaps);
  CHECK_STR(m_ptr_fragment, m_len_fragment, heaps, num_heaps);
  //    CHECK_STR(m_ptr_printed_string, m_len_printed_string, heaps, num_heaps);
}

/***********************************************************************
 *                                                                     *
 *                               S E T                                 *
 *                                                                     *
 ***********************************************************************/

const char *
url_scheme_set(HdrHeap *heap, URLImpl *url, const char *scheme_str, int scheme_wks_idx, int length, bool copy_string)
{
  const char *scheme_wks;
  url_called_set(url);
  if (length == 0) {
    scheme_str = nullptr;
  }

  mime_str_u16_set(heap, scheme_str, length, &(url->m_ptr_scheme), &(url->m_len_scheme), copy_string);

  url->m_scheme_wks_idx = scheme_wks_idx;
  if (scheme_wks_idx >= 0) {
    scheme_wks = hdrtoken_index_to_wks(scheme_wks_idx);
  } else {
    scheme_wks = nullptr;
  }

  if (scheme_wks == URL_SCHEME_HTTP || scheme_wks == URL_SCHEME_WS) {
    url->m_url_type = URL_TYPE_HTTP;
  } else if (scheme_wks == URL_SCHEME_HTTPS || scheme_wks == URL_SCHEME_WSS) {
    url->m_url_type = URL_TYPE_HTTPS;
  } else {
    url->m_url_type = URL_TYPE_HTTP;
  }

  return scheme_wks; // tokenized string or NULL if not well known
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_user_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string)
{
  url_called_set(url);
  if (length == 0) {
    value = nullptr;
  }
  mime_str_u16_set(heap, value, length, &(url->m_ptr_user), &(url->m_len_user), copy_string);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_password_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string)
{
  url_called_set(url);
  if (length == 0) {
    value = nullptr;
  }
  mime_str_u16_set(heap, value, length, &(url->m_ptr_password), &(url->m_len_password), copy_string);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_host_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string)
{
  url_called_set(url);
  if (length == 0) {
    value = nullptr;
  }
  mime_str_u16_set(heap, value, length, &(url->m_ptr_host), &(url->m_len_host), copy_string);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_port_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string)
{
  url_called_set(url);
  if (length == 0) {
    value = nullptr;
  }
  mime_str_u16_set(heap, value, length, &(url->m_ptr_port), &(url->m_len_port), copy_string);

  url->m_port = 0;
  for (int i = 0; i < length; i++) {
    if (!ParseRules::is_digit(value[i])) {
      break;
    }
    url->m_port = url->m_port * 10 + (value[i] - '0');
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_port_set(HdrHeap *heap, URLImpl *url, unsigned int port)
{
  url_called_set(url);
  if (port > 0) {
    char value[6];
    int length;

    length = ink_fast_itoa(port, value, sizeof(value));
    mime_str_u16_set(heap, value, length, &(url->m_ptr_port), &(url->m_len_port), true);
  } else {
    mime_str_u16_set(heap, nullptr, 0, &(url->m_ptr_port), &(url->m_len_port), true);
  }
  url->m_port = port;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_path_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string)
{
  url_called_set(url);
  if (length == 0) {
    value = nullptr;
  }
  mime_str_u16_set(heap, value, length, &(url->m_ptr_path), &(url->m_len_path), copy_string);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

// empties params/query/fragment component
// url_{params|query|fragment}_set()

void
url_params_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string)
{
  url_called_set(url);
  mime_str_u16_set(heap, value, length, &(url->m_ptr_params), &(url->m_len_params), copy_string);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_query_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string)
{
  url_called_set(url);
  mime_str_u16_set(heap, value, length, &(url->m_ptr_query), &(url->m_len_query), copy_string);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_fragment_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string)
{
  url_called_set(url);
  mime_str_u16_set(heap, value, length, &(url->m_ptr_fragment), &(url->m_len_fragment), copy_string);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_type_set(URLImpl *url, unsigned int typecode)
{
  url_called_set(url);
  url->m_type_code = typecode;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/***********************************************************************
 *                                                                     *
 *                               G E T                                 *
 *                                                                     *
 ***********************************************************************/

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_called_set(URLImpl *url)
{
  url->m_clean = !url->m_ptr_printed_string;
}

void
url_clear_string_ref(URLImpl *url)
{
  if (url->m_ptr_printed_string) {
    url->m_len_printed_string = 0;
    url->m_ptr_printed_string = nullptr;
    url->m_clean              = true;
  }
  return;
}

char *
url_string_get_ref(HdrHeap *heap, URLImpl *url, int *length)
{
  if (!url) {
    return nullptr;
  }

  if (url->m_ptr_printed_string && url->m_clean) {
    if (length) {
      *length = url->m_len_printed_string;
    }
    return (char *)url->m_ptr_printed_string;
  } else { // either not clean or never printed
    int len = url_length_get(url);
    char *buf;
    int index  = 0;
    int offset = 0;

    /* stuff alloc'd here gets gc'd on HdrHeap::destroy() */
    buf = heap->allocate_str(len + 1);
    url_print(url, buf, len, &index, &offset);
    buf[len] = '\0';

    if (length) {
      *length = len;
    }
    url->m_clean              = true; // reset since we have url_print()'ed again
    url->m_len_printed_string = len;
    url->m_ptr_printed_string = buf;
    return buf;
  }
}

char *
url_string_get(URLImpl *url, Arena *arena, int *length, HdrHeap *heap)
{
  int len = url_length_get(url);
  char *buf;
  char *buf2;
  int index  = 0;
  int offset = 0;

  buf = arena ? arena->str_alloc(len) : (char *)ats_malloc(len + 1);

  url_print(url, buf, len, &index, &offset);
  buf[len] = '\0';

  /* see string_get_ref() */
  if (heap) {
    buf2 = heap->allocate_str(len + 1);
    memcpy(buf2, buf, len);
    buf2[len]                 = '\0';
    url->m_clean              = true; // reset since we have url_print()'ed again
    url->m_len_printed_string = len;
    url->m_ptr_printed_string = buf2;
  }

  if (length) {
    *length = len;
  }
  return buf;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

char *
url_string_get_buf(URLImpl *url, char *dstbuf, int dstbuf_size, int *length)
{
  int len    = url_length_get(url);
  int index  = 0;
  int offset = 0;
  char *buf  = nullptr;

  if (dstbuf && dstbuf_size > 0) {
    buf = dstbuf;
    if (len >= dstbuf_size) {
      len = dstbuf_size - 1;
    }
    url_print(url, dstbuf, len, &index, &offset);
    buf[len] = 0;

    if (length) {
      *length = len;
    }
  }
  return buf;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
url_scheme_get(URLImpl *url, int *length)
{
  const char *str;

  if (url->m_scheme_wks_idx >= 0) {
    str     = hdrtoken_index_to_wks(url->m_scheme_wks_idx);
    *length = hdrtoken_index_to_length(url->m_scheme_wks_idx);
  } else {
    str     = url->m_ptr_scheme;
    *length = url->m_len_scheme;
  }
  return str;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
url_user_get(URLImpl *url, int *length)
{
  *length = url->m_len_user;
  return url->m_ptr_user;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
url_password_get(URLImpl *url, int *length)
{
  *length = url->m_len_password;
  return url->m_ptr_password;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
url_host_get(URLImpl *url, int *length)
{
  *length = url->m_len_host;
  return url->m_ptr_host;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
url_port_get(URLImpl *url)
{
  return url->m_port;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
url_path_get(URLImpl *url, int *length)
{
  *length = url->m_len_path;
  return url->m_ptr_path;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
url_params_get(URLImpl *url, int *length)
{
  *length = url->m_len_params;
  return url->m_ptr_params;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
url_query_get(URLImpl *url, int *length)
{
  *length = url->m_len_query;
  return url->m_ptr_query;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
url_fragment_get(URLImpl *url, int *length)
{
  *length = url->m_len_fragment;
  return url->m_ptr_fragment;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
url_type_get(URLImpl *url)
{
  return url->m_type_code;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/***********************************************************************
 *                                                                     *
 *               U R L    S T R I N G    F U N C T I O N S             *
 *                                                                     *
 ***********************************************************************/

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
url_length_get(URLImpl *url)
{
  int length = 0;

  if (url->m_ptr_scheme) {
    if ((url->m_scheme_wks_idx >= 0) && (hdrtoken_index_to_wks(url->m_scheme_wks_idx) == URL_SCHEME_FILE)) {
      length += url->m_len_scheme + 1; // +1 for ":"
    } else {
      length += url->m_len_scheme + 3; // +3 for "://"
    }
  }

  if (url->m_ptr_user) {
    length += url->m_len_user + 1; // +1 for "@"
    if (url->m_ptr_password) {
      length += url->m_len_password + 1; // +1 for ":"
    }
  }

  if (url->m_ptr_host) {
    length += url->m_len_host;
    if (url->m_ptr_port && url->m_port) {
      length += url->m_len_port + 1; // +1 for ":"
    }
  }

  if (url->m_ptr_path) {
    length += url->m_len_path + 1; // +1 for /
  } else {
    length += 1; // +1 for /
  }

  if (url->m_ptr_params && url->m_len_params > 0) {
    length += url->m_len_params + 1; // +1 for ";"
  }

  if (url->m_ptr_query && url->m_len_query > 0) {
    length += url->m_len_query + 1; // +1 for "?"
  }

  if (url->m_ptr_fragment && url->m_len_fragment > 0) {
    length += url->m_len_fragment + 1; // +1 for "#"
  }

  return length;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

char *
url_to_string(URLImpl *url, Arena *arena, int *length)
{
  int len;
  int idx;
  char *str;

  len = url_length_get(url) + 1;

  if (length) {
    *length = len;
  }

  if (arena) {
    str = arena->str_alloc(len);
  } else {
    str = (char *)ats_malloc(len + 1);
  }

  idx = 0;

  if (url->m_ptr_scheme) {
    memcpy(&str[idx], url->m_ptr_scheme, url->m_len_scheme);
    idx += url->m_len_scheme;
    if ((url->m_scheme_wks_idx >= 0) && (hdrtoken_index_to_wks(url->m_scheme_wks_idx) == URL_SCHEME_FILE)) {
      str[idx++] = ':';
    } else {
      str[idx++] = ':';
      str[idx++] = '/';
      str[idx++] = '/';
    }
  }

  if (url->m_ptr_user) {
    memcpy(&str[idx], url->m_ptr_user, url->m_len_user);
    idx += url->m_len_user;
    if (url->m_ptr_password) {
      str[idx++] = ':';
      memcpy(&str[idx], url->m_ptr_password, url->m_len_password);
      idx += url->m_len_password;
    }
    str[idx++] = '@';
  }

  if (url->m_ptr_host) {
    memcpy(&str[idx], url->m_ptr_host, url->m_len_host);
    idx += url->m_len_host;
    if (url->m_ptr_port != nullptr) {
      str[idx++] = ':';
      memcpy(&str[idx], url->m_ptr_port, url->m_len_port);
      idx += url->m_len_port;
    }
  }

  memcpy(&str[idx], url->m_ptr_path, url->m_len_path);
  idx += url->m_len_path;

  if (url->m_ptr_params && url->m_len_params > 0) {
    str[idx++] = ';';
    memcpy(&str[idx], url->m_ptr_params, url->m_len_params);
    idx += url->m_len_params;
  }

  if (url->m_ptr_query && url->m_len_query > 0) {
    str[idx++] = '?';
    memcpy(&str[idx], url->m_ptr_query, url->m_len_query);
    idx += url->m_len_query;
  }

  if (url->m_ptr_fragment && url->m_len_fragment > 0) {
    str[idx++] = '#';
    memcpy(&str[idx], url->m_ptr_fragment, url->m_len_fragment);
    idx += url->m_len_fragment;
  }

  str[idx++] = '\0';

  ink_release_assert(idx == len);

  return str;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/***********************************************************************
 *                                                                     *
 *                     E S C A P E - H A N D L I N G                   *
 *                                                                     *
 ***********************************************************************/

void
unescape_str(char *&buf, char *buf_e, const char *&str, const char *str_e, int &state)
{
  int copy_len;
  char *first_pct;
  int buf_len = (int)(buf_e - buf);
  int str_len = (int)(str_e - str);
  int min_len = (int)(str_len < buf_len ? str_len : buf_len);

  first_pct = ink_memcpy_until_char(buf, (char *)str, min_len, '%');
  copy_len  = (int)(first_pct - str);
  str += copy_len;
  buf += copy_len;
  if (copy_len == min_len) {
    return;
  }

  while (str < str_e && (buf != buf_e)) {
    switch (state) {
    case 0:
      if (str[0] == '%') {
        str += 1;
        state = 1;
      } else {
        *buf++ = str[0];
        str += 1;
      }
      break;
    case 1:
      if (ParseRules::is_hex(str[0])) {
        str += 1;
        state = 2;
      } else {
        *buf++ = str[-1];
        state  = 0;
      }
      break;
    case 2:
      if (ParseRules::is_hex(str[0])) {
        int tmp;

        if (ParseRules::is_alpha(str[-1])) {
          tmp = (ParseRules::ink_toupper(str[-1]) - 'A' + 10) * 16;
        } else {
          tmp = (str[-1] - '0') * 16;
        }
        if (ParseRules::is_alpha(str[0])) {
          tmp += (ParseRules::ink_toupper(str[0]) - 'A' + 10);
        } else {
          tmp += str[0] - '0';
        }

        *buf++ = tmp;
        str += 1;
        state = 0;
      } else {
        *buf++ = str[-2];
        state  = 3;
      }
      break;
    case 3:
      *buf++ = str[-1];
      state  = 0;
      break;
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
unescape_str_tolower(char *&buf, char *end, const char *&str, const char *str_e, int &state)
{
  while (str < str_e && (buf != end)) {
    switch (state) {
    case 0:
      if (str[0] == '%') {
        str += 1;
        state = 1;
      } else {
        *buf++ = ParseRules::ink_tolower(str[0]);
        str += 1;
      }
      break;
    case 1:
      if (ParseRules::is_hex(str[0])) {
        str += 1;
        state = 2;
      } else {
        *buf++ = ParseRules::ink_tolower(str[-1]);
        state  = 0;
      }
      break;
    case 2:
      if (ParseRules::is_hex(str[0])) {
        int tmp;

        if (ParseRules::is_alpha(str[-1])) {
          tmp = (ParseRules::ink_toupper(str[-1]) - 'A' + 10) * 16;
        } else {
          tmp = (str[-1] - '0') * 16;
        }
        if (ParseRules::is_alpha(str[0])) {
          tmp += (ParseRules::ink_toupper(str[0]) - 'A' + 10);
        } else {
          tmp += str[0] - '0';
        }

        *buf++ = tmp;
        str += 1;
        state = 0;
      } else {
        *buf++ = ParseRules::ink_tolower(str[-2]);
        state  = 3;
      }
      break;
    case 3:
      *buf++ = ParseRules::ink_tolower(str[-1]);
      state  = 0;
      break;
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

char *
url_unescapify(Arena *arena, const char *str, int length)
{
  char *buffer;
  char *t, *e;
  int s;

  if (length == -1) {
    length = (int)strlen(str);
  }

  buffer = arena->str_alloc(length);
  t      = buffer;
  e      = buffer + length;
  s      = 0;

  unescape_str(t, e, str, str + length, s);
  *t = '\0';

  return buffer;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/***********************************************************************
 *                                                                     *
 *                            P A R S I N G                            *
 *                                                                     *
 ***********************************************************************/

#define GETNEXT(label) \
  {                    \
    cur += 1;          \
    if (cur >= end) {  \
      goto label;      \
    }                  \
  }

ParseResult
url_parse_scheme(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings_p)
{
  const char *cur = *start;
  const char *scheme_wks;
  const char *scheme_start = nullptr;
  const char *scheme_end   = nullptr;
  int scheme_wks_idx;

  // Skip over spaces
  while (' ' == *cur && ++cur < end) {
  }

  if (cur < end) {
    scheme_start = scheme_end = cur;

    // If the URL is more complex then a path, parse to see if there is a scheme
    if ('/' != *cur) {
      // Search for a : it could be part of a scheme or a username:password
      while (':' != *cur && ++cur < end) {
      }

      // If there is a :// then there is a scheme
      if (cur + 2 < end && cur[1] == '/' && cur[2] == '/') { // found "://"
        scheme_end     = cur;
        scheme_wks_idx = hdrtoken_tokenize(scheme_start, scheme_end - scheme_start, &scheme_wks);

        if (!(scheme_wks_idx > 0 && hdrtoken_wks_to_token_type(scheme_wks) == HDRTOKEN_TYPE_SCHEME)) {
          // Unknown scheme, validate the scheme
          if (!validate_scheme({scheme_start, static_cast<size_t>(scheme_end - scheme_start)})) {
            return PARSE_RESULT_ERROR;
          }
        }
        url_scheme_set(heap, url, scheme_start, scheme_wks_idx, scheme_end - scheme_start, copy_strings_p);
      }
    }
    *start = scheme_end;
    return PARSE_RESULT_CONT;
  }
  return PARSE_RESULT_ERROR; // no non-whitespace found
}

/**
 *  This method will return TRUE if the uri is strictly compliant with
 *  RFC 3986 and it will return FALSE if not.
 */
static bool
url_is_strictly_compliant(const char *start, const char *end)
{
  for (const char *i = start; i < end; ++i) {
    if (!ParseRules::is_uri(*i)) {
      Debug("http", "Non-RFC compliant character [0x%.2X] found in URL", (unsigned char)*i);
      return false;
    }
  }
  return true;
}

/**
 *  This method will return TRUE if the uri is mostly compliant with
 *  RFC 3986 and it will return FALSE if not. Specifically denying white
 *  space an unprintable characters
 */
static bool
url_is_mostly_compliant(const char *start, const char *end)
{
  for (const char *i = start; i < end; ++i) {
    if (isspace(*i)) {
      Debug("http", "Whitespace character [0x%.2X] found in URL", (unsigned char)*i);
      return false;
    }
    if (!isprint(*i)) {
      Debug("http", "Non-printable character [0x%.2X] found in URL", (unsigned char)*i);
      return false;
    }
  }
  return true;
}

ParseResult
url_parse(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings_p, int strict_uri_parsing)
{
  if (strict_uri_parsing == 1 && !url_is_strictly_compliant(*start, end)) {
    return PARSE_RESULT_ERROR;
  }
  if (strict_uri_parsing == 2 && !url_is_mostly_compliant(*start, end)) {
    return PARSE_RESULT_ERROR;
  }

  ParseResult zret = url_parse_scheme(heap, url, start, end, copy_strings_p);
  return PARSE_RESULT_CONT == zret ? url_parse_http(heap, url, start, end, copy_strings_p) : zret;
}

ParseResult
url_parse_no_path_component_breakdown(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings_p)
{
  ParseResult zret = url_parse_scheme(heap, url, start, end, copy_strings_p);
  return PARSE_RESULT_CONT == zret ? url_parse_http_no_path_component_breakdown(heap, url, start, end, copy_strings_p) : zret;
}

/**
  Parse internet URL.

  @verbatim
  [://][user[:password]@]host[:port]

  some.place/
  some.place:80/
  foo@some.place:80/
  foo:bar@some.place:80/
  foo:bar@some.place/
  foo:42@some.place/
  @endverbatim

*/

ParseResult
url_parse_internet(HdrHeap *heap, URLImpl *url, const char **start, char const *end, bool copy_strings_p)
{
  const char *cur = *start;
  const char *base;              // Base for host/port field.
  const char *bracket = nullptr; // marker for open bracket, if any.
  ts::ConstBuffer user, passw, host, port;
  static size_t const MAX_COLON = 8; // max # of valid colons.
  size_t n_colon                = 0;
  const char *last_colon        = nullptr; // pointer to last colon seen.

  // Do a quick check for "://"
  if (end - cur > 3 && (((':' ^ *cur) | ('/' ^ cur[1]) | ('/' ^ cur[2])) == 0)) {
    cur += 3;
  } else if (':' == *cur && (++cur >= end || ('/' == *cur && (++cur >= end || ('/' == *cur && ++cur >= end))))) {
    return PARSE_RESULT_ERROR;
  }

  base = cur;
  // skipped leading stuff, start real parsing.
  while (cur < end) {
    // Note: Each case is responsible for incrementing @a cur if
    // appropriate!
    switch (*cur) {
    case ']': // address close
      if (nullptr == bracket || n_colon >= MAX_COLON) {
        return PARSE_RESULT_ERROR;
      }
      ++cur;
      /* We keep the brackets because there are too many other places
         that depend on them and it's too painful to keep track if
         they should be used. I thought about being clever with
         stripping brackets from non-IPv6 content but that gets ugly
         as well. Just not worth it.
       */
      host.set(bracket, cur);
      // Spec requires This constitute the entire host so the next
      // character must be missing (EOS), slash, or colon.
      if (cur >= end || '/' == *cur) { // done which is OK
        last_colon = nullptr;
        break;
      } else if (':' != *cur) { // otherwise it must be a colon
        return PARSE_RESULT_ERROR;
      }
      /* We want to prevent more than 1 colon following so we set @a
         n_colon appropriately.
      */
      n_colon = MAX_COLON - 1;
    // FALL THROUGH
    case ':': // track colons, fail if too many.
      if (++n_colon > MAX_COLON) {
        return PARSE_RESULT_ERROR;
      }
      last_colon = cur;
      ++cur;
      break;
    case '@': // user/password marker.
      if (user || n_colon > 1) {
        return PARSE_RESULT_ERROR; // we already got one, or too many colons.
      }
      if (n_colon) {
        user.set(base, last_colon);
        passw.set(last_colon + 1, cur);
        n_colon    = 0;
        last_colon = nullptr;
      } else {
        user.set(base, cur);
      }
      ++cur;
      base = cur;
      break;
    case '[':                       // address open
      if (bracket || base != cur) { // must be first char in field
        return PARSE_RESULT_ERROR;
      }
      bracket = cur; // location and flag.
      ++cur;
      break;
    case '/':    // we're done with this phase.
      end = cur; // cause loop exit
      break;
    default:
      ++cur;
      break;
    };
  }
  // Time to pick up the pieces. At this pointer cur._ptr is the first
  // character past the parse area.

  if (user) {
    url_user_set(heap, url, user._ptr, user._size, copy_strings_p);
    if (passw) {
      url_password_set(heap, url, passw._ptr, passw._size, copy_strings_p);
    }
  }

  // @a host not set means no brackets to mark explicit host.
  if (!host) {
    if (1 == n_colon || MAX_COLON == n_colon) { // presume port.
      host.set(base, last_colon);
    } else { // it's all host.
      host.set(base, cur);
      last_colon = nullptr; // prevent port setting.
    }
  }
  if (host._size) {
    if (validate_host_name(std::string_view(host._ptr, host._size))) {
      url_host_set(heap, url, host._ptr, host._size, copy_strings_p);
    } else {
      return PARSE_RESULT_ERROR;
    }
  }

  if (last_colon) {
    ink_assert(n_colon);
    port.set(last_colon + 1, cur);
    if (!port._size) {
      return PARSE_RESULT_ERROR; // colon w/o port value.
    }
    url_port_set(heap, url, port._ptr, port._size, copy_strings_p);
  }
  if ('/' == *cur) {
    ++cur; // must do this after filling in host/port.
  }
  *start = cur;
  return PARSE_RESULT_DONE;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

// empties params/query/fragment component

ParseResult
url_parse_http(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings)
{
  ParseResult err;
  const char *cur;
  const char *path_start     = nullptr;
  const char *path_end       = nullptr;
  const char *params_start   = nullptr;
  const char *params_end     = nullptr;
  const char *query_start    = nullptr;
  const char *query_end      = nullptr;
  const char *fragment_start = nullptr;
  const char *fragment_end   = nullptr;
  char mask;

  err = url_parse_internet(heap, url, start, end, copy_strings);
  if (err < 0) {
    return err;
  }

  cur = *start;
  if (*start == end) {
    goto done;
  }

  path_start = cur;
  mask       = ';' & '?' & '#';
parse_path2:
  if ((*cur & mask) == mask) {
    if (*cur == ';') {
      path_end = cur;
      goto parse_params1;
    }
    if (*cur == '?') {
      path_end = cur;
      goto parse_query1;
    }
    if (*cur == '#') {
      path_end = cur;
      goto parse_fragment1;
    }
  } else {
    ink_assert((*cur != ';') && (*cur != '?') && (*cur != '#'));
  }
  GETNEXT(done);
  goto parse_path2;

parse_params1:
  params_start = cur + 1;
  GETNEXT(done);
parse_params2:
  if (*cur == '?') {
    params_end = cur;
    goto parse_query1;
  }
  if (*cur == '#') {
    params_end = cur;
    goto parse_fragment1;
  }
  GETNEXT(done);
  goto parse_params2;

parse_query1:
  query_start = cur + 1;
  GETNEXT(done);
parse_query2:
  if (*cur == '#') {
    query_end = cur;
    goto parse_fragment1;
  }
  GETNEXT(done);
  goto parse_query2;

parse_fragment1:
  fragment_start = cur + 1;
  GETNEXT(done);
  fragment_end = end;

done:
  if (path_start) {
    if (!path_end) {
      path_end = cur;
    }
    url_path_set(heap, url, path_start, path_end - path_start, copy_strings);
  }
  if (params_start) {
    if (!params_end) {
      params_end = cur;
    }
    url_params_set(heap, url, params_start, params_end - params_start, copy_strings);
  }
  if (query_start) {
    if (!query_end) {
      query_end = cur;
    }
    url_query_set(heap, url, query_start, query_end - query_start, copy_strings);
  }
  if (fragment_start) {
    if (!fragment_end) {
      fragment_end = cur;
    }
    url_fragment_set(heap, url, fragment_start, fragment_end - fragment_start, copy_strings);
  }

  *start = cur;
  return PARSE_RESULT_DONE;
}

ParseResult
url_parse_http_no_path_component_breakdown(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings)
{
  const char *cur = *start;
  const char *host_end;

  // Do a quick check for "://" - our only format check.
  if (end - cur > 3 && (((':' ^ *cur) | ('/' ^ cur[1]) | ('/' ^ cur[2])) == 0)) {
    cur += 3;
  } else if (':' == *cur && (++cur >= end || ('/' == *cur && (++cur >= end || ('/' == *cur && ++cur >= end))))) {
    return PARSE_RESULT_ERROR;
  }

  // Grab everything until EOS or slash.
  const char *base = cur;
  cur              = static_cast<const char *>(memchr(cur, '/', end - cur));
  if (cur) {
    host_end = cur;
    ++cur;
  } else {
    host_end = cur = end;
  }

  // Did we find something for the host?
  if (base != host_end) {
    const char *port = nullptr;
    int port_len     = 0;

    // Check for port. Search from the end stopping on the first non-digit
    // or more than 5 digits and a delimiter.
    port                   = host_end - 1;
    const char *port_limit = host_end - 6;
    if (port_limit < base) {
      port_limit = base; // don't go past start.
    }

    while (port >= port_limit && isdigit(*port)) {
      --port;
    }

    // A port if we're still in the host area and we found a ':' as
    // the immediately preceeding character.
    if (port >= base && ':' == *port) {
      port_len = host_end - port - 1; // must compute this first.
      host_end = port;                // then point at colon.
      ++port;                         // drop colon from port.
      url_port_set(heap, url, port, port_len, copy_strings);
    }

    // Now we can set the host.
    url_host_set(heap, url, base, host_end - base, copy_strings);
  }

  // path is anything that's left.
  if (cur < end) {
    url_path_set(heap, url, cur, end - cur, copy_strings);
    cur = end;
  }
  *start = cur;
  return PARSE_RESULT_DONE;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/***********************************************************************
 *                                                                     *
 *                           P R I N T I N G                           *
 *                                                                     *
 ***********************************************************************/

int
url_print(URLImpl *url, char *buf_start, int buf_length, int *buf_index_inout, int *buf_chars_to_skip_inout)
{
#define TRY(x) \
  if (!x)      \
  return 0

  if (url->m_ptr_scheme) {
    TRY(mime_mem_print(url->m_ptr_scheme, url->m_len_scheme, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    // [amc] Why is "file:" special cased to be wrong?
    //    if ((url->m_scheme_wks_idx >= 0) && (hdrtoken_index_to_wks(url->m_scheme_wks_idx) == URL_SCHEME_FILE)) {
    //      TRY(mime_mem_print(":", 1, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    //    } else {
    TRY(mime_mem_print("://", 3, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    //    }
  }

  if (url->m_ptr_user) {
    TRY(mime_mem_print(url->m_ptr_user, url->m_len_user, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    if (url->m_ptr_password) {
      TRY(mime_mem_print(":", 1, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
      TRY(
        mime_mem_print(url->m_ptr_password, url->m_len_password, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    }
    TRY(mime_mem_print("@", 1, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
  }

  if (url->m_ptr_host) {
    // Force brackets for IPv6. Note colon must occur in first 5 characters.
    // But it can be less (e.g. "::1").
    int n          = url->m_len_host;
    bool bracket_p = '[' != *url->m_ptr_host && (nullptr != memchr(url->m_ptr_host, ':', n > 5 ? 5 : n));
    if (bracket_p) {
      TRY(mime_mem_print("[", 1, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    }
    TRY(mime_mem_print(url->m_ptr_host, url->m_len_host, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    if (bracket_p) {
      TRY(mime_mem_print("]", 1, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    }
    if (url->m_ptr_port && url->m_port) {
      TRY(mime_mem_print(":", 1, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
      TRY(mime_mem_print(url->m_ptr_port, url->m_len_port, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    }
  }

  TRY(mime_mem_print("/", 1, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));

  if (url->m_ptr_path) {
    TRY(mime_mem_print(url->m_ptr_path, url->m_len_path, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
  }

  if (url->m_ptr_params && url->m_len_params > 0) {
    TRY(mime_mem_print(";", 1, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    TRY(mime_mem_print(url->m_ptr_params, url->m_len_params, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
  }

  if (url->m_ptr_query && url->m_len_query > 0) {
    TRY(mime_mem_print("?", 1, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    TRY(mime_mem_print(url->m_ptr_query, url->m_len_query, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
  }

  if (url->m_ptr_fragment && url->m_len_fragment > 0) {
    TRY(mime_mem_print("#", 1, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
    TRY(mime_mem_print(url->m_ptr_fragment, url->m_len_fragment, buf_start, buf_length, buf_index_inout, buf_chars_to_skip_inout));
  }

  return 1;

#undef TRY
}

void
url_describe(HdrHeapObjImpl *raw, bool /* recurse ATS_UNUSED */)
{
  URLImpl *obj = (URLImpl *)raw;

  Debug("http", "[URLTYPE: %d, SWKSIDX: %d,", obj->m_url_type, obj->m_scheme_wks_idx);
  Debug("http", "\tSCHEME: \"%.*s\", SCHEME_LEN: %d,", obj->m_len_scheme, (obj->m_ptr_scheme ? obj->m_ptr_scheme : "NULL"),
        obj->m_len_scheme);
  Debug("http", "\tUSER: \"%.*s\", USER_LEN: %d,", obj->m_len_user, (obj->m_ptr_user ? obj->m_ptr_user : "NULL"), obj->m_len_user);
  Debug("http", "\tPASSWORD: \"%.*s\", PASSWORD_LEN: %d,", obj->m_len_password,
        (obj->m_ptr_password ? obj->m_ptr_password : "NULL"), obj->m_len_password);
  Debug("http", "\tHOST: \"%.*s\", HOST_LEN: %d,", obj->m_len_host, (obj->m_ptr_host ? obj->m_ptr_host : "NULL"), obj->m_len_host);
  Debug("http", "\tPORT: \"%.*s\", PORT_LEN: %d, PORT_NUM: %d", obj->m_len_port, (obj->m_ptr_port ? obj->m_ptr_port : "NULL"),
        obj->m_len_port, obj->m_port);
  Debug("http", "\tPATH: \"%.*s\", PATH_LEN: %d,", obj->m_len_path, (obj->m_ptr_path ? obj->m_ptr_path : "NULL"), obj->m_len_path);
  Debug("http", "\tPARAMS: \"%.*s\", PARAMS_LEN: %d,", obj->m_len_params, (obj->m_ptr_params ? obj->m_ptr_params : "NULL"),
        obj->m_len_params);
  Debug("http", "\tQUERY: \"%.*s\", QUERY_LEN: %d,", obj->m_len_query, (obj->m_ptr_query ? obj->m_ptr_query : "NULL"),
        obj->m_len_query);
  Debug("http", "\tFRAGMENT: \"%.*s\", FRAGMENT_LEN: %d]", obj->m_len_fragment,
        (obj->m_ptr_fragment ? obj->m_ptr_fragment : "NULL"), obj->m_len_fragment);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/***********************************************************************
 *                                                                     *
 *                        U R L    D I G E S T S                       *
 *                                                                     *
 ***********************************************************************/

static inline void
memcpy_tolower(char *d, const char *s, int n)
{
  while (n--) {
    *d = ParseRules::ink_tolower(*s);
    s++;
    d++;
  }
}

#define BUFSIZE 512

// fast path for CryptoHash, HTTP, no user/password/params/query,
// no buffer overflow, no unescaping needed

static inline void
url_CryptoHash_get_fast(const URLImpl *url, CryptoContext &ctx, CryptoHash *hash, cache_generation_t generation)
{
  char buffer[BUFSIZE];
  char *p;

  p = buffer;
  memcpy_tolower(p, url->m_ptr_scheme, url->m_len_scheme);
  p += url->m_len_scheme;
  *p++ = ':';
  *p++ = '/';
  *p++ = '/';
  // no user
  *p++ = ':';
  // no password
  *p++ = '@';
  memcpy_tolower(p, url->m_ptr_host, url->m_len_host);
  p += url->m_len_host;
  *p++ = '/';
  memcpy(p, url->m_ptr_path, url->m_len_path);
  p += url->m_len_path;
  *p++ = ';';
  // no params
  *p++ = '?';
  // no query

  ink_assert(sizeof(url->m_port) == 2);
  uint16_t port = (uint16_t)url_canonicalize_port(url->m_url_type, url->m_port);
  *p++          = ((char *)&port)[0];
  *p++          = ((char *)&port)[1];

  ctx.update(buffer, p - buffer);
  if (generation != -1) {
    ctx.update(&generation, sizeof(generation));
  }

  ctx.finalize(*hash);
}

static inline void
url_CryptoHash_get_general(const URLImpl *url, CryptoContext &ctx, CryptoHash &hash, cache_generation_t generation)
{
  char buffer[BUFSIZE];
  char *p, *e;
  const char *strs[13], *ends[13];
  const char *t;
  in_port_t port;
  int i, s;

  strs[0] = url->m_ptr_scheme;
  strs[1] = "://";
  strs[2] = url->m_ptr_user;
  strs[3] = ":";
  strs[4] = url->m_ptr_password;
  strs[5] = "@";
  strs[6] = url->m_ptr_host;
  strs[7] = "/";
  strs[8] = url->m_ptr_path;

  ends[0] = strs[0] + url->m_len_scheme;
  ends[1] = strs[1] + 3;
  ends[2] = strs[2] + url->m_len_user;
  ends[3] = strs[3] + 1;
  ends[4] = strs[4] + url->m_len_password;
  ends[5] = strs[5] + 1;
  ends[6] = strs[6] + url->m_len_host;
  ends[7] = strs[7] + 1;
  ends[8] = strs[8] + url->m_len_path;

  strs[9]  = ";";
  strs[10] = url->m_ptr_params;
  strs[11] = "?";
  strs[12] = url->m_ptr_query;
  ends[9]  = strs[9] + 1;
  ends[10] = strs[10] + url->m_len_params;
  ends[11] = strs[11] + 1;
  ends[12] = strs[12] + url->m_len_query;

  p = buffer;
  e = buffer + BUFSIZE;

  for (i = 0; i < 13; i++) {
    if (strs[i]) {
      t = strs[i];
      s = 0;

      while (t < ends[i]) {
        if ((i == 0) || (i == 6)) { // scheme and host
          unescape_str_tolower(p, e, t, ends[i], s);
        } else {
          unescape_str(p, e, t, ends[i], s);
        }

        if (p == e) {
          ctx.update(buffer, BUFSIZE);
          p = buffer;
        }
      }
    }
  }

  if (p != buffer) {
    ctx.update(buffer, p - buffer);
  }
  int buffer_len = static_cast<int>(p - buffer);
  port           = url_canonicalize_port(url->m_url_type, url->m_port);

  ctx.update(&port, sizeof(port));
  if (generation != -1) {
    ctx.update(&generation, sizeof(generation));
    Debug("url_cachekey", "Final url string for cache hash key %.*s%d%d", buffer_len, buffer, port, static_cast<int>(generation));
  } else {
    Debug("url_cachekey", "Final url string for cache hash key %.*s%d", buffer_len, buffer, port);
  }
  ctx.finalize(hash);
}

void
url_CryptoHash_get(const URLImpl *url, CryptoHash *hash, cache_generation_t generation)
{
  URLHashContext ctx;
  if ((url_hash_method != 0) && (url->m_url_type == URL_TYPE_HTTP) &&
      ((url->m_len_user + url->m_len_password + url->m_len_params + url->m_len_query) == 0) &&
      (3 + 1 + 1 + 1 + 1 + 1 + 2 + url->m_len_scheme + url->m_len_host + url->m_len_path < BUFSIZE) &&
      (memchr(url->m_ptr_host, '%', url->m_len_host) == nullptr) && (memchr(url->m_ptr_path, '%', url->m_len_path) == nullptr)) {
    url_CryptoHash_get_fast(url, ctx, hash, generation);
#ifdef DEBUG
    CryptoHash hash_general;
    url_CryptoHash_get_general(url, ctx, hash_general, generation);
    ink_assert(*hash == hash_general);
#endif
  } else {
    url_CryptoHash_get_general(url, ctx, *hash, generation);
  }
}

#undef BUFSIZE

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
url_host_CryptoHash_get(URLImpl *url, CryptoHash *hash)
{
  CryptoContext ctx;

  if (url->m_ptr_scheme) {
    ctx.update(url->m_ptr_scheme, url->m_len_scheme);
  }

  ctx.update("://", 3);

  if (url->m_ptr_host) {
    ctx.update(url->m_ptr_host, url->m_len_host);
  }

  ctx.update(":", 1);

  // [amc] Why is this <int> and not <in_port_t>?
  // Especially since it's in_port_t for url_CryptoHash_get.
  int port = url_canonicalize_port(url->m_url_type, url->m_port);
  ctx.update(&port, sizeof(port));
  ctx.finalize(*hash);
}

/*-------------------------------------------------------------------------
 * Regression tests
  -------------------------------------------------------------------------*/
#if TS_HAS_TESTS
#include "tscore/TestBox.h"

const static struct {
  const char *const text;
  bool valid;
} http_validate_hdr_field_test_case[] = {{"yahoo", true},
                                         {"yahoo.com", true},
                                         {"yahoo.wow.com", true},
                                         {"yahoo.wow.much.amaze.com", true},
                                         {"209.131.52.50", true},
                                         {"192.168.0.1", true},
                                         {"localhost", true},
                                         {"3ffe:1900:4545:3:200:f8ff:fe21:67cf", true},
                                         {"fe80:0:0:0:200:f8ff:fe21:67cf", true},
                                         {"fe80::200:f8ff:fe21:67cf", true},
                                         {"<svg onload=alert(1)>", false}, // Sample host header XSS attack
                                         {"jlads;f8-9349*(D&F*D(234jD*(FSD*(VKLJ#(*$@()#$)))))", false},
                                         {"\"\t\n", false},
                                         {"!@#$%^ &*(*&^%$#@#$%^&*(*&^%$#))", false},
                                         {":):(:O!!!!!!", false}};

REGRESSION_TEST(VALIDATE_HDR_FIELD)(RegressionTest *t, int /* level ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  for (auto i : http_validate_hdr_field_test_case) {
    const char *const txt = i.text;
    box.check(validate_host_name({txt}) == i.valid, "Validation of FQDN (host) header: \"%s\", expected %s, but not", txt,
              (i.valid ? "true" : "false"));
  }
}

REGRESSION_TEST(ParseRules_strict_URI)(RegressionTest *t, int /* level ATS_UNUSED */, int *pstatus)
{
  const struct {
    const char *const uri;
    bool valid;
  } http_strict_uri_parsing_test_case[] = {{"/home", true},
                                           {"/path/data?key=value#id", true},
                                           {"/ABCDEFGHIJKLMNOPQRSTUVWXYZ", true},
                                           {"/abcdefghijklmnopqrstuvwxyz", true},
                                           {"/0123456789", true},
                                           {":/?#[]@", true},
                                           {"!$&'()*+,;=", true},
                                           {"-._~", true},
                                           {"%", true},
                                           {"\n", false},
                                           {"\"", false},
                                           {"<", false},
                                           {">", false},
                                           {"\\", false},
                                           {"^", false},
                                           {"`", false},
                                           {"{", false},
                                           {"|", false},
                                           {"}", false},
                                           {"é", false}};

  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  for (auto i : http_strict_uri_parsing_test_case) {
    const char *const uri = i.uri;
    box.check(url_is_strictly_compliant(uri, uri + strlen(uri)) == i.valid, "Strictly parse URI: \"%s\", expected %s, but not", uri,
              (i.valid ? "true" : "false"));
  }
}

#endif // TS_HAS_TESTS
