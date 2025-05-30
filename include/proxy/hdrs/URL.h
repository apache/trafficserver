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
#include "proxy/hdrs/HdrToken.h"
#include "proxy/hdrs/HdrHeap.h"
#include "tscore/CryptoHash.h"
#include "proxy/hdrs/MIME.h"
#include <string_view>

#include "tscore/ink_apidefs.h"

using cache_generation_t = int64_t;

enum class URLType : uint8_t {
  NONE,
  HTTP,
  HTTPS,
};

class URLImpl : public HdrHeapObjImpl
{
public:
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
  int16_t  m_scheme_wks_idx;
  uint16_t m_port;
  URLType  m_url_type;  // e.g. HTTP
  uint8_t  m_type_code; // RFC 1738 limits type code to 1 char
  // 6 bytes

  uint32_t m_clean : 1;
  /// Whether the URI had an absolutely empty path, not even an initial '/'.
  uint32_t m_path_is_empty       : 1;
  uint32_t m_normalization_flags : 2; // Only valid if both m_clean and m_ptr_printed_sting are non-zero.
  // 8 bytes + 4 bits, will result in padding

  // Accessors
  std::string_view get_scheme() const noexcept;
  const char      *set_scheme(HdrHeap *heap, std::string_view value, int value_wks_idx, bool copy_string);
  std::string_view get_user() const noexcept;
  void             set_user(HdrHeap *heap, std::string_view value, bool copy_string);
  std::string_view get_password() const noexcept;
  void             set_password(HdrHeap *heap, std::string_view value, bool copy_string);
  std::string_view get_host() const noexcept;
  void             set_host(HdrHeap *heap, std::string_view value, bool copy_string);
  int              get_port();
  void             set_port(HdrHeap *heap, unsigned int port);
  void             set_port(HdrHeap *heap, std::string_view value, bool copy_string);
  std::string_view get_path() const noexcept;
  void             set_path(HdrHeap *heap, std::string_view value, bool copy_string);
  URLType          get_type();
  void             set_type(URLType type);
  int              get_type_code();
  void             set_type_code(unsigned int typecode);
  std::string_view get_params() const noexcept;
  void             set_params(HdrHeap *heap, std::string_view value, bool copy_string);
  std::string_view get_query() const noexcept;
  void             set_query(HdrHeap *heap, std::string_view value, bool copy_string);
  std::string_view get_fragment() const noexcept;
  void             set_fragment(HdrHeap *heap, std::string_view value, bool copy_string);

  // Marshaling Functions
  int    marshal(MarshalXlate *str_xlate, int num_xlate);
  void   unmarshal(intptr_t offset);
  void   move_strings(HdrStrHeap *new_heap);
  void   rehome_strings(HdrHeap *new_heap);
  size_t strings_length();

  // Sanity Check Functions
  void check_strings(HeapCheck *heaps, int num_heaps);

private:
};

using URLHashContext = CryptoContext;

extern c_str_view URL_SCHEME_FILE;
extern c_str_view URL_SCHEME_FTP;
extern c_str_view URL_SCHEME_GOPHER;
extern c_str_view URL_SCHEME_HTTP;
extern c_str_view URL_SCHEME_HTTPS;
extern c_str_view URL_SCHEME_WS;
extern c_str_view URL_SCHEME_WSS;
extern c_str_view URL_SCHEME_MAILTO;
extern c_str_view URL_SCHEME_NEWS;
extern c_str_view URL_SCHEME_NNTP;
extern c_str_view URL_SCHEME_PROSPERO;
extern c_str_view URL_SCHEME_TELNET;
extern c_str_view URL_SCHEME_TUNNEL;
extern c_str_view URL_SCHEME_WAIS;
extern c_str_view URL_SCHEME_PNM;
extern c_str_view URL_SCHEME_RTSP;
extern c_str_view URL_SCHEME_RTSPU;
extern c_str_view URL_SCHEME_MMS;
extern c_str_view URL_SCHEME_MMSU;
extern c_str_view URL_SCHEME_MMST;

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

/* Public */
bool validate_host_name(std::string_view addr);
bool validate_scheme(std::string_view scheme);

void url_init();

URLImpl *url_create(HdrHeap *heap);
void     url_clear(URLImpl *url_impl);
void     url_nuke_proxy_stuff(URLImpl *d_url);

URLImpl *url_copy(URLImpl *s_url, HdrHeap *s_heap, HdrHeap *d_heap, bool inherit_strs = true);
void     url_copy_onto(URLImpl *s_url, HdrHeap *s_heap, URLImpl *d_url, HdrHeap *d_heap, bool inherit_strs = true);

// Normalization flag masks.
namespace URLNormalize
{
unsigned const NONE           = 0;
unsigned const IMPLIED_SCHEME = 1; // If scheme missing, add scheme implied by URL type.
unsigned const LC_SCHEME_HOST = 2; // Force scheme and host to lower case if necessary.
}; // namespace URLNormalize

int  url_print(URLImpl *u, char *buf, int bufsize, int *bufindex, int *dumpoffset,
               unsigned normalization_flags = URLNormalize::NONE);
void url_describe(HdrHeapObjImpl *raw, bool recurse);

int   url_length_get(URLImpl *url, unsigned normalization_flags = URLNormalize::NONE);
char *url_string_get(URLImpl *url, Arena *arena, int *length, HdrHeap *heap);
void  url_clear_string_ref(URLImpl *url);
char *url_string_get_ref(HdrHeap *heap, URLImpl *url, int *length, unsigned normalization_flags = URLNormalize::NONE);
void  url_called_set(URLImpl *url);
char *url_string_get_buf(URLImpl *url, char *dstbuf, int dstbuf_size, int *length);

void url_CryptoHash_get(const URLImpl *url, CryptoHash *hash, bool ignore_query = false, cache_generation_t generation = -1);
void url_host_CryptoHash_get(URLImpl *url, CryptoHash *hash);

constexpr bool USE_STRICT_URI_PARSING = true;

ParseResult url_parse(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings,
                      int strict_uri_parsing = false, bool verify_host_characters = true);

constexpr bool COPY_STRINGS = true;

ParseResult url_parse_regex(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings);
ParseResult url_parse_internet(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings,
                               bool verify_host_characters);
ParseResult url_parse_http(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings,
                           bool verify_host_characters);
ParseResult url_parse_http_regex(HdrHeap *heap, URLImpl *url, const char **start, const char *end, bool copy_strings);

char *url_unescapify(Arena *arena, const char *str, int length);

void unescape_str(char *&buf, char *buf_e, const char *&str, const char *str_e, int &state);
void unescape_str_tolower(char *&buf, char *end, const char *&str, const char *str_e, int &state);

inline int
url_canonicalize_port(URLType type, int port)
{
  if (port == 0) {
    if (type == URLType::HTTP)
      port = 80;
    else if (type == URLType::HTTPS)
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

  int print(char *buf, int bufsize, int *bufindex, int *dumpoffset, unsigned normalization_flags = URLNormalize::NONE) const;

  int length_get(unsigned normalization_flags = URLNormalize::NONE) const;

  void clear_string_ref();

  char *string_get(Arena *arena, int *length = nullptr) const;
  char *string_get_ref(int *length = nullptr, unsigned normalization_flags = URLNormalize::NONE) const;
  char *string_get_buf(char *dstbuf, int dsbuf_size, int *length = nullptr) const;
  void  hash_get(CryptoHash *hash, bool ignore_query = false, cache_generation_t generation = -1) const;
  void  host_hash_get(CryptoHash *hash) const;

  std::string_view scheme_get() const noexcept;
  int              scheme_get_wksidx() const;
  void             scheme_set(std::string_view value);

  std::string_view user_get() const noexcept;
  void             user_set(std::string_view value);
  std::string_view password_get() const noexcept;
  void             password_set(std::string_view value);
  std::string_view host_get() const noexcept;
  void             host_set(std::string_view value);

  int  port_get() const;
  int  port_get_raw() const;
  void port_set(int port);

  std::string_view path_get() const noexcept;
  void             path_set(std::string_view value);

  int  type_code_get();
  void type_code_set(int type);

  std::string_view query_get() const noexcept;
  void             query_set(std::string_view value);
  std::string_view fragment_get() const noexcept;
  void             fragment_set(std::string_view value);

  /**
   * Parse the given URL string and populate URL state with the parts.
   *
   * @param[in] url The URL to parse.
   *
   * @return ParseResult::DONE if parsing was successful, ParseResult::ERROR
   * otherwise.
   */
  ParseResult parse(std::string_view url);

  /** Same as parse() but do not verify that the host has proper FQDN
   * characters.
   *
   * This is useful for RemapConfig To targets which have "$[0-9]" references
   * in their host names which will later be substituted for other text.
   */
  ParseResult parse_no_host_check(std::string_view url);

  ParseResult parse(const char **start, const char *end);
  ParseResult parse(const char *str, int length);

  /** Perform more simplified parsing that is resilient to receiving regular
   * expressions.
   *
   * This simply looks for the first '/' in a URL and considers that the end of
   * the authority and the beginning of the rest of the URL. This allows for
   * the '?' character in an authority as a part of a regex without it being
   * considered a query parameter and, thus, avoids confusing the parser.
   *
   * This is only used in RemapConfig and may have no other uses.
   */
  ParseResult parse_regex(std::string_view url);
  ParseResult parse_regex(const char *str, int length);

public:
  static char *unescapify(Arena *arena, const char *str, int length);
  // No gratuitous copies!
  URL(const URL &u)            = delete;
  URL &operator=(const URL &u) = delete;

private:
  static constexpr bool VERIFY_HOST_CHARACTERS = true;
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
URL::print(char *buf, int bufsize, int *bufindex, int *dumpoffset, unsigned normalization_flags) const
{
  ink_assert(valid());
  return url_print(m_url_impl, buf, bufsize, bufindex, dumpoffset, normalization_flags);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
URL::length_get(unsigned normalization_flags) const
{
  ink_assert(valid());
  return url_length_get(m_url_impl, normalization_flags);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline char *
URL::string_get(Arena *arena_or_null_for_malloc, int *length) const
{
  ink_assert(valid());
  return url_string_get(m_url_impl, arena_or_null_for_malloc, length, m_heap);
}

inline char *
URL::string_get_ref(int *length, unsigned normalization_flags) const
{
  ink_assert(valid());
  return url_string_get_ref(m_heap, m_url_impl, length, normalization_flags);
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
URL::string_get_buf(char *dstbuf, int dsbuf_size, int *length) const
{
  ink_assert(valid());
  return url_string_get_buf(m_url_impl, dstbuf, dsbuf_size, length);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::hash_get(CryptoHash *hash, bool ignore_query, cache_generation_t generation) const
{
  ink_assert(valid());
  url_CryptoHash_get(m_url_impl, hash, ignore_query, generation);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::host_hash_get(CryptoHash *hash) const
{
  ink_assert(valid());
  url_host_CryptoHash_get(m_url_impl, hash);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
URL::scheme_get() const noexcept
{
  ink_assert(valid());
  return m_url_impl->get_scheme();
}

inline int
URL::scheme_get_wksidx() const
{
  ink_assert(valid());
  return (m_url_impl->m_scheme_wks_idx);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::scheme_set(std::string_view value)
{
  ink_assert(valid());
  int scheme_wks_idx = (!value.empty() ? hdrtoken_tokenize(value.data(), value.length()) : -1);
  m_url_impl->set_scheme(m_heap, value, scheme_wks_idx, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
URL::user_get() const noexcept
{
  ink_assert(valid());
  return m_url_impl->get_user();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::user_set(std::string_view value)
{
  ink_assert(valid());
  m_url_impl->set_user(m_heap, value, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
URL::password_get() const noexcept
{
  ink_assert(valid());
  return m_url_impl->get_password();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::password_set(std::string_view value)
{
  ink_assert(valid());
  m_url_impl->set_password(m_heap, value, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
URL::host_get() const noexcept
{
  ink_assert(valid());
  return m_url_impl->get_host();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::host_set(std::string_view value)
{
  ink_assert(valid());
  m_url_impl->set_host(m_heap, value, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
URL::port_get() const
{
  ink_assert(valid());
  return url_canonicalize_port(m_url_impl->get_type(), m_url_impl->get_port());
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
URL::port_get_raw() const
{
  ink_assert(valid());
  return m_url_impl->get_port();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::port_set(int port)
{
  ink_assert(valid());
  m_url_impl->set_port(m_heap, port);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
URL::path_get() const noexcept
{
  ink_assert(valid());
  return m_url_impl->get_path();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::path_set(std::string_view value)
{
  ink_assert(valid());
  m_url_impl->set_path(m_heap, value, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
URL::type_code_get()
{
  ink_assert(valid());
  return m_url_impl->get_type_code();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::type_code_set(int typecode)
{
  ink_assert(valid());
  m_url_impl->set_type_code(typecode);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
URL::query_get() const noexcept
{
  ink_assert(valid());
  return m_url_impl->get_query();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::query_set(std::string_view value)
{
  ink_assert(valid());
  m_url_impl->set_query(m_heap, value, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
URL::fragment_get() const noexcept
{
  ink_assert(valid());
  return m_url_impl->get_fragment();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
URL::fragment_set(std::string_view value)
{
  ink_assert(valid());
  m_url_impl->set_fragment(m_heap, value, true);
}

/**
  Parser doesn't clear URL first, so if you parse over a non-clear URL,
  the resulting URL may contain some of the previous data.

 */
inline ParseResult
URL::parse(std::string_view url)
{
  return this->parse(url.data(), static_cast<int>(url.size()));
}

/**
  Parser doesn't clear URL first, so if you parse over a non-clear URL,
  the resulting URL may contain some of the previous data.

 */
inline ParseResult
URL::parse_no_host_check(std::string_view url)
{
  ink_assert(valid());
  const char *start = url.data();
  const char *end   = url.data() + url.length();
  return url_parse(m_heap, m_url_impl, &start, end, COPY_STRINGS, !USE_STRICT_URI_PARSING, !VERIFY_HOST_CHARACTERS);
}

/**
  Parser doesn't clear URL first, so if you parse over a non-clear URL,
  the resulting URL may contain some of the previous data.

 */
inline ParseResult
URL::parse(const char **start, const char *end)
{
  ink_assert(valid());
  return url_parse(m_heap, m_url_impl, start, end, COPY_STRINGS);
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
URL::parse_regex(std::string_view url)
{
  ink_assert(valid());
  const char *str = url.data();
  return url_parse_regex(m_heap, m_url_impl, &str, str + url.length(), COPY_STRINGS);
}

/**
  Parser doesn't clear URL first, so if you parse over a non-clear URL,
  the resulting URL may contain some of the previous data.

 */
inline ParseResult
URL::parse_regex(const char *str, int length)
{
  ink_assert(valid());
  if (length < 0)
    length = (int)strlen(str);
  ink_assert(valid());
  return url_parse_regex(m_heap, m_url_impl, &str, str + length, COPY_STRINGS);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline char *
URL::unescapify(Arena *arena, const char *str, int length)
{
  return url_unescapify(arena, str, length);
}
