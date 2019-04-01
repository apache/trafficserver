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

#include "tscore/Arena.h"
#include "HdrToken.h"
#include "HdrHeap.h"
#include "tscore/CryptoHash.h"
#include "MIME.h"
#include <string_view>

#include "tscore/ink_apidefs.h"

typedef int64_t cache_generation_t;

enum URLType {
  URL_TYPE_NONE,
  URL_TYPE_HTTP,
  URL_TYPE_HTTPS,
};

struct URLImpl : public HdrHeapObjImpl {
  // HdrHeapObjImpl is 4 bytes
  uint16_t m_len_scheme;
  uint16_t m_len_user;
  uint16_t m_len_password;
  uint16_t m_len_host;
  uint16_t m_len_port;
  uint16_t m_len_path;
  uint16_t m_len_params;
  uint16_t m_len_query;
  uint16_t m_len_fragment;
  uint16_t m_len_printed_string;
  // 4 + 20 byte = 24, 8 bytes aligned

  const char *m_ptr_scheme;
  const char *m_ptr_user;
  const char *m_ptr_password;
  const char *m_ptr_host;
  const char *m_ptr_port;
  const char *m_ptr_path;
  const char *m_ptr_params;
  const char *m_ptr_query;
  const char *m_ptr_fragment;
  const char *m_ptr_printed_string;
  // pointer aligned (4 or 8)

  // Tokenized values
  int16_t m_scheme_wks_idx;
  uint16_t m_port;
  uint8_t m_url_type;  // e.g. HTTP
  uint8_t m_type_code; // RFC 1738 limits type code to 1 char
  // 6 bytes

  uint32_t m_clean : 1;
  // 8 bytes + 1 bit, will result in padding

  // Marshaling Functions
  int marshal(MarshalXlate *str_xlate, int num_xlate);
  void unmarshal(intptr_t offset);
  void move_strings(HdrStrHeap *new_heap);
  void rehome_strings(HdrHeap *new_heap);
  size_t strings_length();

  // Sanity Check Functions
  void check_strings(HeapCheck *heaps, int num_heaps);
};

using URLHashContext = CryptoContext;

extern const char *URL_SCHEME_FILE;
extern const char *URL_SCHEME_FTP;
extern const char *URL_SCHEME_GOPHER;
extern const char *URL_SCHEME_HTTP;
extern const char *URL_SCHEME_HTTPS;
extern const char *URL_SCHEME_WS;
extern const char *URL_SCHEME_WSS;
extern const char *URL_SCHEME_MAILTO;
extern const char *URL_SCHEME_NEWS;
extern const char *URL_SCHEME_NNTP;
extern const char *URL_SCHEME_PROSPERO;
extern const char *URL_SCHEME_TELNET;
extern const char *URL_SCHEME_TUNNEL;
extern const char *URL_SCHEME_WAIS;
extern const char *URL_SCHEME_PNM;
extern const char *URL_SCHEME_RTSP;
extern const char *URL_SCHEME_RTSPU;
extern const char *URL_SCHEME_MMS;
extern const char *URL_SCHEME_MMSU;
extern const char *URL_SCHEME_MMST;

extern int URL_WKSIDX_FILE;
extern int URL_WKSIDX_FTP;
extern int URL_WKSIDX_GOPHER;
extern int URL_WKSIDX_HTTP;
extern int URL_WKSIDX_HTTPS;
extern int URL_WKSIDX_WS;
extern int URL_WKSIDX_WSS;
extern int URL_WKSIDX_MAILTO;
extern int URL_WKSIDX_NEWS;
extern int URL_WKSIDX_NNTP;
extern int URL_WKSIDX_PROSPERO;
extern int URL_WKSIDX_TELNET;
extern int URL_WKSIDX_TUNNEL;
extern int URL_WKSIDX_WAIS;
extern int URL_WKSIDX_PNM;
extern int URL_WKSIDX_RTSP;
extern int URL_WKSIDX_RTSPU;
extern int URL_WKSIDX_MMS;
extern int URL_WKSIDX_MMSU;
extern int URL_WKSIDX_MMST;

extern int URL_LEN_FILE;
extern int URL_LEN_FTP;
extern int URL_LEN_GOPHER;
extern int URL_LEN_HTTP;
extern int URL_LEN_HTTPS;
extern int URL_LEN_WS;
extern int URL_LEN_WSS;
extern int URL_LEN_MAILTO;
extern int URL_LEN_NEWS;
extern int URL_LEN_NNTP;
extern int URL_LEN_PROSPERO;
extern int URL_LEN_TELNET;
extern int URL_LEN_TUNNEL;
extern int URL_LEN_WAIS;
extern int URL_LEN_PNM;
extern int URL_LEN_RTSP;
extern int URL_LEN_RTSPU;
extern int URL_LEN_MMS;
extern int URL_LEN_MMSU;
extern int URL_LEN_MMST;

/* Public */
bool validate_host_name(std::string_view addr);
void url_init();

URLImpl *url_create(HdrHeap *heap);
void url_clear(URLImpl *url_impl);
void url_nuke_proxy_stuff(URLImpl *d_url);

URLImpl *url_copy(URLImpl *s_url, HdrHeap *s_heap, HdrHeap *d_heap, bool inherit_strs = true);
void url_copy_onto(URLImpl *s_url, HdrHeap *s_heap, URLImpl *d_url, HdrHeap *d_heap, bool inherit_strs = true);
void url_copy_onto_as_server_url(URLImpl *s_url, HdrHeap *s_heap, URLImpl *d_url, HdrHeap *d_heap, bool inherit_strs = true);

int url_print(URLImpl *u, char *buf, int bufsize, int *bufindex, int *dumpoffset, bool normalized = false);
void url_describe(HdrHeapObjImpl *raw, bool recurse);

int url_length_get(URLImpl *url);
char *url_string_get(URLImpl *url, Arena *arena, int *length, HdrHeap *heap, bool normalized = false);
void url_clear_string_ref(URLImpl *url);
char *url_string_get_ref(HdrHeap *heap, URLImpl *url, int *length, bool normalized = false);
void url_called_set(URLImpl *url);
char *url_string_get_buf(URLImpl *url, char *dstbuf, int dstbuf_size, int *length);

void url_CryptoHash_get(const URLImpl *url, CryptoHash *hash, cache_generation_t generation = -1);
void url_host_CryptoHash_get(URLImpl *url, CryptoHash *hash);
const char *url_scheme_set(HdrHeap *heap, URLImpl *url, const char *value, int value_wks_idx, int length, bool copy_string);

/* Internet specific */
void url_user_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string);
void url_password_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string);
void url_host_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string);
void url_port_set(HdrHeap *heap, URLImpl *url, unsigned int port);

/* HTTP specific */
void url_path_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string);

void url_type_set(URLImpl *url, unsigned int type);

/* HTTP specific */
void url_params_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string);
void url_query_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string);
void url_fragment_set(HdrHeap *heap, URLImpl *url, const char *value, int length, bool copy_string);

ParseResult url_parse(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings,
                      bool strict_uri_parsing = false);
ParseResult url_parse_no_path_component_breakdown(HdrHeap *heap, URLImpl *url, const char **start, const char *end,
                                                  bool copy_strings);
ParseResult url_parse_internet(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings);
ParseResult url_parse_http(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings);
ParseResult url_parse_http_no_path_component_breakdown(HdrHeap *heap, URLImpl *url, const char **start, const char *end,
                                                       bool copy_strings);

char *url_unescapify(Arena *arena, const char *str, int length);

void unescape_str(char *&buf, char *buf_e, const char *&str, const char *str_e, int &state);
void unescape_str_tolower(char *&buf, char *end, const char *&str, const char *str_e, int &state);

inline int
url_canonicalize_port(int type, int port)
{
  if (port == 0) {
    if (type == URL_TYPE_HTTP)
      port = 80;
    else if (type == URL_TYPE_HTTPS)
      port = 443;
  }
  return (port);
}

class URL : public HdrHeapSDKHandle
{
public:
  URLImpl *m_url_impl = nullptr;

  URL();
  ~URL();

  int valid() const;

  void create(HdrHeap *h);
  void copy(const URL *url);
  void copy_shallow(const URL *url);
  void clear();
  void reset();
  // Note that URL::destroy() is inherited from HdrHeapSDKHandle.
  void nuke_proxy_stuff();

  int print(char *buf, int bufsize, int *bufindex, int *dumpoffset, bool normalized = false);

  int length_get();
  void clear_string_ref();
  char *string_get(Arena *arena, int *length = nullptr, bool normalized = false);
  char *string_get_ref(int *length = nullptr, bool normalized = false);
  char *string_get_buf(char *dstbuf, int dsbuf_size, int *length = nullptr);
  void hash_get(CryptoHash *hash, cache_generation_t generation = -1) const;
  void host_hash_get(CryptoHash *hash);

  const char *scheme_get(int *length);
  const std::string_view scheme_get();
  int scheme_get_wksidx();
  void scheme_set(const char *value, int length);

  const char *user_get(int *length);
  void user_set(const char *value, int length);
  const char *password_get(int *length);
  void password_set(const char *value, int length);
  const char *host_get(int *length);
  void host_set(const char *value, int length);
  int port_get();
  int port_get_raw();
  void port_set(int port);

  const char *path_get(int *length);
  void path_set(const char *value, int length);

  int type_get();
  void type_set(int type);

  const char *params_get(int *length);
  void params_set(const char *value, int length);
  const char *query_get(int *length);
  void query_set(const char *value, int length);
  const char *fragment_get(int *length);
  void fragment_set(const char *value, int length);

  ParseResult parse(const char **start, const char *end);
  ParseResult parse(const char *str, int length);
  ParseResult parse_no_path_component_breakdown(const char *str, int length);

public:
  static char *unescapify(Arena *arena, const char *str, int length);
  // No gratuitous copies!
  URL(const URL &u) = delete;
  URL &operator=(const URL &u) = delete;
};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline URL::URL() {}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline URL::~URL() {}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
URL::valid() const
{
  return (m_heap && m_url_impl);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::create(HdrHeap *heap)
{
  if (heap) {
    m_heap = heap;
  } else if (!m_heap) {
    m_heap = new_HdrHeap();
  }

  m_url_impl = url_create(m_heap);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::copy(const URL *url)
{
  ink_assert(url != nullptr && url->valid());
  url_copy_onto(url->m_url_impl, url->m_heap, m_url_impl, m_heap);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::copy_shallow(const URL *url)
{
  ink_assert(url->valid());
  this->set(url);
  m_url_impl = url->m_url_impl;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::clear()
{
  m_url_impl = nullptr;
  HdrHeapSDKHandle::clear();
}

inline void
URL::reset()
{
  m_url_impl = nullptr;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::nuke_proxy_stuff()
{
  ink_assert(valid());
  url_nuke_proxy_stuff(m_url_impl);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
URL::print(char *buf, int bufsize, int *bufindex, int *dumpoffset, bool normalized)
{
  ink_assert(valid());
  return url_print(m_url_impl, buf, bufsize, bufindex, dumpoffset, normalized);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
URL::length_get()
{
  ink_assert(valid());
  return url_length_get(m_url_impl);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline char *
URL::string_get(Arena *arena_or_null_for_malloc, int *length, bool normalized)
{
  ink_assert(valid());
  return url_string_get(m_url_impl, arena_or_null_for_malloc, length, m_heap, normalized);
}

inline char *
URL::string_get_ref(int *length, bool normalized)
{
  ink_assert(valid());
  return url_string_get_ref(m_heap, m_url_impl, length, normalized);
}

inline void
URL::clear_string_ref()
{
  ink_assert(valid());
  url_clear_string_ref(m_url_impl);
  return;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
inline char *
URL::string_get_buf(char *dstbuf, int dsbuf_size, int *length)
{
  ink_assert(valid());
  return url_string_get_buf(m_url_impl, dstbuf, dsbuf_size, length);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::hash_get(CryptoHash *hash, cache_generation_t generation) const
{
  ink_assert(valid());
  url_CryptoHash_get(m_url_impl, hash, generation);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::host_hash_get(CryptoHash *hash)
{
  ink_assert(valid());
  url_host_CryptoHash_get(m_url_impl, hash);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const std::string_view
URL::scheme_get()
{
  ink_assert(valid());

  if (m_url_impl->m_scheme_wks_idx >= 0) {
    return std::string_view{hdrtoken_index_to_wks(m_url_impl->m_scheme_wks_idx),
                            static_cast<size_t>(hdrtoken_index_to_length(m_url_impl->m_scheme_wks_idx))};
  } else {
    return std::string_view{m_url_impl->m_ptr_scheme, m_url_impl->m_len_scheme};
  }
}

inline const char *
URL::scheme_get(int *length)
{
  std::string_view ret = this->scheme_get();
  *length              = ret.size();
  return ret.data();
}

inline int
URL::scheme_get_wksidx()
{
  ink_assert(valid());
  return (m_url_impl->m_scheme_wks_idx);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::scheme_set(const char *value, int length)
{
  ink_assert(valid());
  int scheme_wks_idx = (value ? hdrtoken_tokenize(value, length) : -1);
  url_scheme_set(m_heap, m_url_impl, value, scheme_wks_idx, length, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
URL::user_get(int *length)
{
  ink_assert(valid());
  *length = m_url_impl->m_len_user;
  return m_url_impl->m_ptr_user;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::user_set(const char *value, int length)
{
  ink_assert(valid());
  url_user_set(m_heap, m_url_impl, value, length, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
URL::password_get(int *length)
{
  ink_assert(valid());
  *length = m_url_impl->m_len_password;
  return m_url_impl->m_ptr_password;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::password_set(const char *value, int length)
{
  ink_assert(valid());
  url_password_set(m_heap, m_url_impl, value, length, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
URL::host_get(int *length)
{
  ink_assert(valid());
  *length = m_url_impl->m_len_host;
  return m_url_impl->m_ptr_host;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::host_set(const char *value, int length)
{
  ink_assert(valid());
  url_host_set(m_heap, m_url_impl, value, length, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
URL::port_get()
{
  ink_assert(valid());
  return url_canonicalize_port(m_url_impl->m_url_type, m_url_impl->m_port);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
URL::port_get_raw()
{
  ink_assert(valid());
  return m_url_impl->m_port;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::port_set(int port)
{
  ink_assert(valid());
  url_port_set(m_heap, m_url_impl, port);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
URL::path_get(int *length)
{
  ink_assert(valid());
  *length = m_url_impl->m_len_path;
  return m_url_impl->m_ptr_path;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::path_set(const char *value, int length)
{
  ink_assert(valid());
  url_path_set(m_heap, m_url_impl, value, length, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
URL::type_get()
{
  ink_assert(valid());
  return m_url_impl->m_type_code;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::type_set(int type)
{
  ink_assert(valid());
  url_type_set(m_url_impl, type);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
URL::params_get(int *length)
{
  ink_assert(valid());
  *length = m_url_impl->m_len_params;
  return m_url_impl->m_ptr_params;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::params_set(const char *value, int length)
{
  ink_assert(valid());
  url_params_set(m_heap, m_url_impl, value, length, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
URL::query_get(int *length)
{
  ink_assert(valid());
  *length = m_url_impl->m_len_query;
  return m_url_impl->m_ptr_query;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::query_set(const char *value, int length)
{
  ink_assert(valid());
  url_query_set(m_heap, m_url_impl, value, length, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
URL::fragment_get(int *length)
{
  ink_assert(valid());
  *length = m_url_impl->m_len_fragment;
  return m_url_impl->m_ptr_fragment;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::fragment_set(const char *value, int length)
{
  ink_assert(valid());
  url_fragment_set(m_heap, m_url_impl, value, length, true);
}

/**
  Parser doesn't clear URL first, so if you parse over a non-clear URL,
  the resulting URL may contain some of the previous data.

 */
inline ParseResult
URL::parse(const char **start, const char *end)
{
  ink_assert(valid());
  return url_parse(m_heap, m_url_impl, start, end, true);
}

/**
  Parser doesn't clear URL first, so if you parse over a non-clear URL,
  the resulting URL may contain some of the previous data.

 */
inline ParseResult
URL::parse(const char *str, int length)
{
  ink_assert(valid());
  if (length < 0)
    length = (int)strlen(str);
  return parse(&str, str + length);
}

/**
  Parser doesn't clear URL first, so if you parse over a non-clear URL,
  the resulting URL may contain some of the previous data.

 */
inline ParseResult
URL::parse_no_path_component_breakdown(const char *str, int length)
{
  ink_assert(valid());
  if (length < 0)
    length = (int)strlen(str);
  ink_assert(valid());
  return url_parse_no_path_component_breakdown(m_heap, m_url_impl, &str, str + length, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline char *
URL::unescapify(Arena *arena, const char *str, int length)
{
  return url_unescapify(arena, str, length);
}
