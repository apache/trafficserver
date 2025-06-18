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

#include <cassert>
#include <string_view>

using namespace std::literals;

#include "tscore/Arena.h"
#include "tscore/CryptoHash.h"
#include "tscore/HTTPVersion.h"
#include "proxy/hdrs/MIME.h"
#include "proxy/hdrs/URL.h"

#include "tscore/ink_apidefs.h"

class Http2HeaderTable;

enum class HTTPStatus {
  NONE = 0,

  CONTINUE           = 100,
  SWITCHING_PROTOCOL = 101,
  PROCESSING         = 102,
  EARLY_HINTS        = 103,

  OK                            = 200,
  CREATED                       = 201,
  ACCEPTED                      = 202,
  NON_AUTHORITATIVE_INFORMATION = 203,
  NO_CONTENT                    = 204,
  RESET_CONTENT                 = 205,
  PARTIAL_CONTENT               = 206,

  MULTIPLE_CHOICES   = 300,
  MOVED_PERMANENTLY  = 301,
  MOVED_TEMPORARILY  = 302,
  SEE_OTHER          = 303,
  NOT_MODIFIED       = 304,
  USE_PROXY          = 305,
  TEMPORARY_REDIRECT = 307,
  PERMANENT_REDIRECT = 308,

  BAD_REQUEST                   = 400,
  UNAUTHORIZED                  = 401,
  PAYMENT_REQUIRED              = 402,
  FORBIDDEN                     = 403,
  NOT_FOUND                     = 404,
  METHOD_NOT_ALLOWED            = 405,
  NOT_ACCEPTABLE                = 406,
  PROXY_AUTHENTICATION_REQUIRED = 407,
  REQUEST_TIMEOUT               = 408,
  CONFLICT                      = 409,
  GONE                          = 410,
  LENGTH_REQUIRED               = 411,
  PRECONDITION_FAILED           = 412,
  REQUEST_ENTITY_TOO_LARGE      = 413,
  REQUEST_URI_TOO_LONG          = 414,
  UNSUPPORTED_MEDIA_TYPE        = 415,
  RANGE_NOT_SATISFIABLE         = 416,
  TOO_EARLY                     = 425,

  INTERNAL_SERVER_ERROR = 500,
  NOT_IMPLEMENTED       = 501,
  BAD_GATEWAY           = 502,
  SERVICE_UNAVAILABLE   = 503,
  GATEWAY_TIMEOUT       = 504,
  HTTPVER_NOT_SUPPORTED = 505
};

enum class HTTPKeepAlive {
  UNDEFINED = 0,
  NO_KEEPALIVE,
  KEEPALIVE,
};

enum class HTTPWarningCode {
  NONE = 0,

  RESPONSE_STALE         = 110,
  REVALIDATION_FAILED    = 111,
  DISCONNECTED_OPERATION = 112,
  HERUISTIC_EXPIRATION   = 113,
  TRANSFORMATION_APPLIED = 114,
  MISC_WARNING           = 199
};

/* squid log codes
   There is code (e.g. logstats) that depends on these errors coming at the end of this enum */
enum class SquidLogCode {
  EMPTY                     = '0',
  TCP_HIT                   = '1',
  TCP_DISK_HIT              = '2',
  TCP_MEM_HIT               = '.', // Don't want to change others codes
  TCP_MISS                  = '3',
  TCP_EXPIRED_MISS          = '4',
  TCP_REFRESH_HIT           = '5',
  TCP_REF_FAIL_HIT          = '6',
  TCP_REFRESH_MISS          = '7',
  TCP_CLIENT_REFRESH        = '8',
  TCP_IMS_HIT               = '9',
  TCP_IMS_MISS              = 'a',
  TCP_SWAPFAIL              = 'b',
  TCP_DENIED                = 'c',
  TCP_WEBFETCH_MISS         = 'd',
  TCP_FUTURE_2              = 'f',
  TCP_HIT_REDIRECT          = '[', // standard redirect
  TCP_MISS_REDIRECT         = ']', // standard redirect
  TCP_HIT_X_REDIRECT        = '<', // extended redirect
  TCP_MISS_X_REDIRECT       = '>', // extended redirect
  UDP_HIT                   = 'g',
  UDP_WEAK_HIT              = 'h',
  UDP_HIT_OBJ               = 'i',
  UDP_MISS                  = 'j',
  UDP_DENIED                = 'k',
  UDP_INVALID               = 'l',
  UDP_RELOADING             = 'm',
  UDP_FUTURE_1              = 'n',
  UDP_FUTURE_2              = 'o',
  ERR_READ_TIMEOUT          = 'p',
  ERR_LIFETIME_EXP          = 'q',
  ERR_POST_ENTITY_TOO_LARGE = 'L',
  ERR_NO_CLIENTS_BIG_OBJ    = 'r',
  ERR_READ_ERROR            = 's',
  ERR_CLIENT_ABORT          = 't', // Client side abort logging
  ERR_CONNECT_FAIL          = 'u',
  ERR_INVALID_REQ           = 'v',
  ERR_UNSUP_REQ             = 'w',
  ERR_INVALID_URL           = 'x',
  ERR_NO_FDS                = 'y',
  ERR_DNS_FAIL              = 'z',
  ERR_NOT_IMPLEMENTED       = 'A',
  ERR_CANNOT_FETCH          = 'B',
  ERR_NO_RELAY              = 'C',
  ERR_DISK_IO               = 'D',
  ERR_ZERO_SIZE_OBJECT      = 'E',
  TCP_CF_HIT                = 'F', // Collapsed forwarding HIT also known as Read while write hit
  ERR_PROXY_DENIED          = 'G',
  ERR_WEBFETCH_DETECTED     = 'H',
  ERR_FUTURE_1              = 'I',
  ERR_CLIENT_READ_ERROR     = 'J', // Client side abort logging
  ERR_LOOP_DETECTED         = 'K', // Loop or cycle detected, request came back to this server
  ERR_UNKNOWN               = 'Z'
};

// squid log subcodes
enum class SquidSubcode {
  EMPTY                     = '0',
  NUM_REDIRECTIONS_EXCEEDED = '1',
};

/* squid hierarchy codes */
enum class SquidHierarchyCode {
  EMPTY                           = '0',
  NONE                            = '1',
  DIRECT                          = '2',
  SIBLING_HIT                     = '3',
  PARENT_HIT                      = '4',
  DEFAULT_PARENT                  = '5',
  SINGLE_PARENT                   = '6',
  FIRST_UP_PARENT                 = '7',
  NO_PARENT_DIRECT                = '8',
  FIRST_PARENT_MISS               = '9',
  LOCAL_IP_DIRECT                 = 'a',
  FIREWALL_IP_DIRECT              = 'b',
  NO_DIRECT_FAIL                  = 'c',
  SOURCE_FASTEST                  = 'd',
  SIBLING_UDP_HIT_OBJ             = 'e',
  PARENT_UDP_HIT_OBJ              = 'f',
  PASSTHROUGH_PARENT              = 'g',
  SSL_PARENT_MISS                 = 'h',
  INVALID_CODE                    = 'i',
  TIMEOUT_DIRECT                  = 'j',
  TIMEOUT_SIBLING_HIT             = 'k',
  TIMEOUT_PARENT_HIT              = 'l',
  TIMEOUT_DEFAULT_PARENT          = 'm',
  TIMEOUT_SINGLE_PARENT           = 'n',
  TIMEOUT_FIRST_UP_PARENT         = 'o',
  TIMEOUT_NO_PARENT_DIRECT        = 'p',
  TIMEOUT_FIRST_PARENT_MISS       = 'q',
  TIMEOUT_LOCAL_IP_DIRECT         = 'r',
  TIMEOUT_FIREWALL_IP_DIRECT      = 's',
  TIMEOUT_NO_DIRECT_FAIL          = 't',
  TIMEOUT_SOURCE_FASTEST          = 'u',
  TIMEOUT_SIBLING_UDP_HIT_OBJ     = 'v',
  TIMEOUT_PARENT_UDP_HIT_OBJ      = 'w',
  TIMEOUT_PASSTHROUGH_PARENT      = 'x',
  TIMEOUT_TIMEOUT_SSL_PARENT_MISS = 'y',
  INVALID_ASSIGNED_CODE           = 'z'
};

/* squid hit/miss codes */
enum SquidHitMissCode {
  SQUID_HIT_RESERVED                   = '0', // Kinda wonky that this is '0', so skipping 'A' for now
  SQUID_HIT_LEVEL_1                    = 'B',
  SQUID_HIT_LEVEL_2                    = 'C',
  SQUID_HIT_LEVEL_3                    = 'D',
  SQUID_HIT_LEVEL_4                    = 'E',
  SQUID_HIT_LEVEL_5                    = 'F',
  SQUID_HIT_LEVEL_6                    = 'G',
  SQUID_HIT_LEVEL_7                    = 'H',
  SQUID_HIT_LEVEL_8                    = 'I',
  SQUID_HIT_LEVEl_9                    = 'J',
  SQUID_MISS_NONE                      = '1',
  SQUID_MISS_HTTP_NON_CACHE            = '3',
  SQUID_MISS_HTTP_NO_DLE               = '5',
  SQUID_MISS_HTTP_NO_LE                = '6',
  SQUID_MISS_HTTP_CONTENT              = '7',
  SQUID_MISS_PRAGMA_NOCACHE            = '8',
  SQUID_MISS_PASS                      = '9',
  SQUID_MISS_PRE_EXPIRED               = 'a',
  SQUID_MISS_ERROR                     = 'b',
  SQUID_MISS_CACHE_BYPASS              = 'c',
  SQUID_HIT_MISS_INVALID_ASSIGNED_CODE = 'z',
  // These are pre-allocated with special semantics, added here for convenience
  SQUID_HIT_RAM     = SQUID_HIT_LEVEL_1,
  SQUID_HIT_SSD     = SQUID_HIT_LEVEL_2,
  SQUID_HIT_DISK    = SQUID_HIT_LEVEL_3,
  SQUID_HIT_CLUSTER = SQUID_HIT_LEVEL_4,
  SQUID_HIT_NET     = SQUID_HIT_LEVEL_5,
  SQUID_HIT_RWW     = SQUID_HIT_LEVEL_6
};

constexpr std::string_view PSEUDO_HEADER_SCHEME    = ":scheme";
constexpr std::string_view PSEUDO_HEADER_AUTHORITY = ":authority";
constexpr std::string_view PSEUDO_HEADER_PATH      = ":path";
constexpr std::string_view PSEUDO_HEADER_METHOD    = ":method";
constexpr std::string_view PSEUDO_HEADER_STATUS    = ":status";

enum class HTTPType {
  UNKNOWN,
  REQUEST,
  RESPONSE,
};

struct HTTPHdrImpl : public HdrHeapObjImpl {
  // HdrHeapObjImpl is 4 bytes
  HTTPType    m_polarity; // request or response or unknown
  HTTPVersion m_version;  // cooked version number
  // 12 bytes means 4 bytes padding here on 64-bit architectures
  union {
    struct {
      URLImpl    *m_url_impl;
      const char *m_ptr_method;
      uint16_t    m_len_method;
      int16_t     m_method_wks_idx;
    } req;

    struct {
      const char *m_ptr_reason;
      uint16_t    m_len_reason;
      int16_t     m_status;
    } resp;
  } u;

  MIMEHdrImpl *m_fields_impl;

  // Marshaling Functions
  int    marshal(MarshalXlate *ptr_xlate, int num_ptr, MarshalXlate *str_xlate, int num_str);
  void   unmarshal(intptr_t offset);
  void   move_strings(HdrStrHeap *new_heap);
  size_t strings_length();

  // Sanity Check Functions
  void check_strings(HeapCheck *heaps, int num_heaps);
};

struct HTTPValAccept {
  char  *type;
  char  *subtype;
  double qvalue;
};

struct HTTPValAcceptCharset {
  char  *charset;
  double qvalue;
};

struct HTTPValAcceptEncoding {
  char  *encoding;
  double qvalue;
};

struct HTTPValAcceptLanguage {
  char  *language;
  double qvalue;
};

struct HTTPValFieldList {
  char             *name;
  HTTPValFieldList *next;
};

struct HTTPValCacheControl {
  const char *directive;

  union {
    int               delta_seconds;
    HTTPValFieldList *field_names;
  } u;
};

struct HTTPValRange {
  int           start;
  int           end;
  HTTPValRange *next;
};

struct HTTPValTE {
  char  *encoding;
  double qvalue;
};

struct HTTPParser {
  bool       m_parsing_http = false;
  MIMEParser m_mime_parser;
};

extern c_str_view HTTP_METHOD_CONNECT;
extern c_str_view HTTP_METHOD_DELETE;
extern c_str_view HTTP_METHOD_GET;
extern c_str_view HTTP_METHOD_HEAD;
extern c_str_view HTTP_METHOD_OPTIONS;
extern c_str_view HTTP_METHOD_POST;
extern c_str_view HTTP_METHOD_PURGE;
extern c_str_view HTTP_METHOD_PUT;
extern c_str_view HTTP_METHOD_TRACE;
extern c_str_view HTTP_METHOD_PUSH;

extern int HTTP_WKSIDX_CONNECT;
extern int HTTP_WKSIDX_DELETE;
extern int HTTP_WKSIDX_GET;
extern int HTTP_WKSIDX_HEAD;
extern int HTTP_WKSIDX_OPTIONS;
extern int HTTP_WKSIDX_POST;
extern int HTTP_WKSIDX_PURGE;
extern int HTTP_WKSIDX_PUT;
extern int HTTP_WKSIDX_TRACE;
extern int HTTP_WKSIDX_PUSH;
extern int HTTP_WKSIDX_METHODS_CNT;

extern c_str_view HTTP_VALUE_BYTES;
extern c_str_view HTTP_VALUE_CHUNKED;
extern c_str_view HTTP_VALUE_CLOSE;
extern c_str_view HTTP_VALUE_COMPRESS;
extern c_str_view HTTP_VALUE_DEFLATE;
extern c_str_view HTTP_VALUE_GZIP;
extern c_str_view HTTP_VALUE_BROTLI;
extern c_str_view HTTP_VALUE_IDENTITY;
extern c_str_view HTTP_VALUE_KEEP_ALIVE;
extern c_str_view HTTP_VALUE_MAX_AGE;
extern c_str_view HTTP_VALUE_MAX_STALE;
extern c_str_view HTTP_VALUE_MIN_FRESH;
extern c_str_view HTTP_VALUE_MUST_REVALIDATE;
extern c_str_view HTTP_VALUE_NONE;
extern c_str_view HTTP_VALUE_NO_CACHE;
extern c_str_view HTTP_VALUE_NO_STORE;
extern c_str_view HTTP_VALUE_NO_TRANSFORM;
extern c_str_view HTTP_VALUE_ONLY_IF_CACHED;
extern c_str_view HTTP_VALUE_PRIVATE;
extern c_str_view HTTP_VALUE_PROXY_REVALIDATE;
extern c_str_view HTTP_VALUE_PUBLIC;
extern c_str_view HTTP_VALUE_S_MAXAGE;
extern c_str_view HTTP_VALUE_NEED_REVALIDATE_ONCE;
extern c_str_view HTTP_VALUE_100_CONTINUE;

/* Private */
void http_hdr_adjust(HTTPHdrImpl *hdrp, int32_t offset, int32_t length, int32_t delta);

/* Public */
void http_init();

HTTPHdrImpl *http_hdr_create(HdrHeap *heap, HTTPType polarity, HTTPVersion version);
void         http_hdr_init(HdrHeap *heap, HTTPHdrImpl *hh, HTTPType polarity, HTTPVersion version);
HTTPHdrImpl *http_hdr_clone(HTTPHdrImpl *s_hh, HdrHeap *s_heap, HdrHeap *d_heap);
void         http_hdr_copy_onto(HTTPHdrImpl *s_hh, HdrHeap *s_heap, HTTPHdrImpl *d_hh, HdrHeap *d_heap, bool inherit_strs);

int http_hdr_print(HTTPHdrImpl const *hh, char *buf, int bufsize, int *bufindex, int *dumpoffset);

void http_hdr_describe(HdrHeapObjImpl *obj, bool recurse = true);

bool http_hdr_version_set(HTTPHdrImpl *hh, const HTTPVersion &ver);

std::string_view http_hdr_method_get(HTTPHdrImpl *hh);
void http_hdr_method_set(HdrHeap *heap, HTTPHdrImpl *hh, std::string_view method, int16_t method_wks_idx, bool must_copy);

void http_hdr_url_set(HdrHeap *heap, HTTPHdrImpl *hh, URLImpl *url);

// HTTPStatus             http_hdr_status_get (HTTPHdrImpl *hh);
void             http_hdr_status_set(HTTPHdrImpl *hh, HTTPStatus status);
std::string_view http_hdr_reason_get(HTTPHdrImpl *hh);
void             http_hdr_reason_set(HdrHeap *heap, HTTPHdrImpl *hh, std::string_view value, bool must_copy);
const char      *http_hdr_reason_lookup(HTTPStatus status);

void        http_parser_init(HTTPParser *parser);
void        http_parser_clear(HTTPParser *parser);
ParseResult http_parser_parse_req(HTTPParser *parser, HdrHeap *heap, HTTPHdrImpl *hh, const char **start, const char *end,
                                  bool must_copy_strings, bool eof, int strict_uri_parsing, size_t max_request_line_size,
                                  size_t max_hdr_field_size);
ParseResult validate_hdr_request_target(int method_wks_idx, URLImpl *url);
ParseResult validate_hdr_host(HTTPHdrImpl *hh);
ParseResult validate_hdr_content_length(HdrHeap *heap, HTTPHdrImpl *hh);
ParseResult http_parser_parse_resp(HTTPParser *parser, HdrHeap *heap, HTTPHdrImpl *hh, const char **start, const char *end,
                                   bool must_copy_strings, bool eof);

HTTPStatus  http_parse_status(const char *start, const char *end);
HTTPVersion http_parse_version(const char *start, const char *end);

/*
HTTPValAccept*         http_parse_accept (const char *buf, Arena *arena);
HTTPValAcceptCharset*  http_parse_accept_charset (const char *buf, Arena *arena);
HTTPValAcceptEncoding* http_parse_accept_encoding (const char *buf, Arena *arena);
HTTPValAcceptLanguage* http_parse_accept_language (const char *buf, Arena *arena);
HTTPValCacheControl*   http_parse_cache_control (const char *buf, Arena *arena);
const char*            http_parse_cache_directive (const char **buf);
HTTPValRange*          http_parse_range (const char *buf, Arena *arena);
*/
HTTPValTE *http_parse_te(const char *buf, int len, Arena *arena);

bool is_http1_hdr_version_supported(const HTTPVersion &http_version);

class IOBufferReader;

class HTTPHdr : public MIMEHdr
{
public:
  HTTPHdrImpl       *m_http = nullptr;
  mutable URL        m_url_cached;
  mutable MIMEField *m_host_mime             = nullptr;
  mutable int        m_host_length           = 0;     ///< Length of hostname.
  mutable int        m_port                  = 0;     ///< Target port.
  mutable bool       m_target_cached         = false; ///< Whether host name and port are cached.
  mutable bool       m_target_in_url         = false; ///< Whether host name and port are in the URL.
  mutable bool       m_100_continue_sent     = false; ///< Whether ATS sent a 100 Continue optimized response.
  mutable bool       m_100_continue_required = false; ///< Whether 100_continue is in the Expect header.
  /// Set if the port was effectively specified in the header.
  /// @c true if the target (in the URL or the HOST field) also specified
  /// a port. That is, @c true if whatever source had the target host
  /// also had a port, @c false otherwise.
  mutable bool m_port_in_header = false;

  mutable bool early_data = false;

  HTTPHdr() = default; // Force the creation of the default constructor

  int valid() const;

  void create(HTTPType polarity, HTTPVersion version = HTTP_INVALID, HdrHeap *heap = nullptr);
  void clear();
  void reset();
  void copy(const HTTPHdr *hdr);
  void copy_shallow(const HTTPHdr *hdr);

  int unmarshal(char *buf, int len, RefCountObj *block_ref);

  int print(char *buf, int bufsize, int *bufindex, int *dumpoffset) const;

  int length_get() const;

  HTTPType type_get() const;

  HTTPVersion version_get() const;
  void        version_set(HTTPVersion version);

  std::string_view method_get();
  int              method_get_wksidx() const;
  void             method_set(std::string_view value);

  URL *url_create(URL *url);

  URL *url_get() const;
  URL *url_get(URL *url);
  /** Get a string with the effective URL in it.
      If @a length is not @c NULL then the length of the string
      is stored in the int pointed to by @a length.

      Note that this can be different from getting the @c URL
      and invoking @c URL::string_get if the host is in a header
      field and not explicitly in the URL.
   */
  char *url_string_get(Arena *arena  = nullptr, ///< Arena to use, or @c malloc if NULL.
                       int   *length = nullptr  ///< Store string length here.
  );
  /** Get a string with the effective URL in it.
      This is automatically allocated if needed in the request heap.

      @see url_string_get
   */
  char *url_string_get_ref(int *length = nullptr ///< Store string length here.
  );

  /** Print the URL.
      Output is not null terminated.
      @return 0 on failure, non-zero on success.
   */
  int url_print(char    *buff,                                    ///< Output buffer
                int      length,                                  ///< Length of @a buffer
                int     *offset,                                  ///< [in,out] ???
                int     *skip,                                    ///< [in,out] ???
                unsigned normalization_flags = URLNormalize::NONE ///< host/scheme normalized to lower case
  );

  /** Return the length of the URL that url_print() will create.
      @return -1 on failure, non-negative on success.
   */
  int url_printed_length(unsigned normalizaion_flags = URLNormalize::NONE);

  /** Get the URL path.
      This is a reference, not allocated.
      @return A string_view to the path or an empty string_view if there is no valid URL.
  */
  std::string_view path_get();

  /** Get the URL query.
      This is a reference, not allocated.
      @return A string_view to the query or an empty string_view if there is no valid URL.
  */
  std::string_view query_get();

  /** Get the URL fragment.
      This is a reference, not allocated.
      @return A string_view to the fragment or an empty string_view if there is no valid URL.
  */
  std::string_view fragment_get();

  /** Get the target host name.
      The length is returned in @a length if non-NULL.
      @note The results are cached so this is fast after the first call.
      @return A string_view to the host name.
  */
  std::string_view host_get() const;

  /** Get the target port.
      If the target port is not found then it is adjusted to the
      default port for the URL type.
      @note The results are cached so this is fast after the first call.
      @return The canonicalized target port.
  */
  int port_get();

  /** Get the URL scheme.
      This is a reference, not allocated.
      @return A string_view to the scheme or an empty string_view if there is no valid URL.
  */
  std::string_view scheme_get();
  void             url_set(URL *url);
  void             url_set(std::string_view value);

  /// Check location of target host.
  /// @return @c true if the host was in the URL, @c false otherwise.
  /// @note This returns @c false if the host is missing.
  bool is_target_in_url() const;

  /// Check if a port was specified in the target.
  /// @return @c true if the port was part of the target.
  bool is_port_in_header() const;

  /// If the target is in the fields and not the URL, copy it to the @a url.
  /// If @a url is @c NULL the cached URL in this header is used.
  /// @note In the default case the copy is avoided if the cached URL already
  /// has the target. If @a url is non @c NULL the copy is always performed.
  void set_url_target_from_host_field(URL *url = nullptr);

  /// Mark the target cache as invalid.
  /// @internal Ugly but too many places currently that touch the
  /// header internals, they must be able to do this.
  void mark_target_dirty() const;

  HTTPStatus status_get() const;
  void       status_set(HTTPStatus status);

  std::string_view reason_get();
  void             reason_set(std::string_view value);

  void mark_early_data(bool flag = true) const;
  bool is_early_data() const;

  ParseResult parse_req(HTTPParser *parser, const char **start, const char *end, bool eof, int strict_uri_parsing = 0,
                        size_t max_request_line_size = UINT16_MAX, size_t max_hdr_field_size = 131070);
  ParseResult parse_resp(HTTPParser *parser, const char **start, const char *end, bool eof);

  ParseResult parse_req(HTTPParser *parser, IOBufferReader *r, int *bytes_used, bool eof, int strict_uri_parsing = 0,
                        size_t max_request_line_size = UINT16_MAX, size_t max_hdr_field_size = UINT16_MAX);
  ParseResult parse_resp(HTTPParser *parser, IOBufferReader *r, int *bytes_used, bool eof);

  bool check_hdr_implements();

public:
  // Utility routines
  bool          is_cache_control_set(const char *cc_directive_wks);
  bool          is_pragma_no_cache_set();
  bool          is_keep_alive_set() const;
  bool          expect_final_response() const;
  HTTPKeepAlive keep_alive_get() const;

protected:
  /** Load the target cache.
      @see m_host, m_port, m_target_in_url
  */
  void _fill_target_cache() const;
  /** Test the cache and fill it if necessary.
      @internal In contrast to @c _fill_target_cache, this method
      is inline and checks whether the cache is already filled.
      @ _fill_target_cache @b always does a cache fill.
  */
  void _test_and_fill_target_cache() const;

  static Arena *const USE_HDR_HEAP_MAGIC;

  // No gratuitous copies!
  HTTPHdr(const HTTPHdr &m)            = delete;
  HTTPHdr &operator=(const HTTPHdr &m) = delete;

private:
  friend class UrlPrintHack; // don't ask.
};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
HTTPHdr::valid() const
{
  return (m_http && m_mime && m_heap);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::create(HTTPType polarity, HTTPVersion version, HdrHeap *heap)
{
  if (heap) {
    m_heap = heap;
  } else if (!m_heap) {
    m_heap = new_HdrHeap();
  }

  m_http = http_hdr_create(m_heap, polarity, version);
  m_mime = m_http->m_fields_impl;
}

inline void
HTTPHdr::clear()
{
  if (m_http && m_http->m_polarity == HTTPType::REQUEST) {
    m_url_cached.clear();
  }
  this->HdrHeapSDKHandle::clear();
  m_http = nullptr;
  m_mime = nullptr;
}

inline void
HTTPHdr::reset()
{
  m_heap = nullptr;
  m_http = nullptr;
  m_mime = nullptr;
  m_url_cached.reset();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::copy(const HTTPHdr *hdr)
{
  ink_assert(hdr->valid());

  if (valid()) {
    http_hdr_copy_onto(hdr->m_http, hdr->m_heap, m_http, m_heap, (m_heap != hdr->m_heap) ? true : false);
  } else {
    m_heap = new_HdrHeap();
    m_http = http_hdr_clone(hdr->m_http, hdr->m_heap, m_heap);
    m_mime = m_http->m_fields_impl;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::copy_shallow(const HTTPHdr *hdr)
{
  ink_assert(hdr->valid());

  m_heap = hdr->m_heap;
  m_http = hdr->m_http;
  m_mime = hdr->m_mime;

  if (hdr->type_get() == HTTPType::REQUEST && m_url_cached.valid())
    m_url_cached.copy_shallow(&hdr->m_url_cached);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
HTTPHdr::print(char *buf, int bufsize, int *bufindex, int *dumpoffset) const
{
  ink_assert(valid());
  return http_hdr_print(m_http, buf, bufsize, bufindex, dumpoffset);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::_test_and_fill_target_cache() const
{
  if (!m_target_cached)
    this->_fill_target_cache();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
HTTPHdr::host_get() const
{
  this->_test_and_fill_target_cache();
  if (m_target_in_url) {
    return url_get()->host_get();
  } else if (m_host_mime) {
    return std::string_view{m_host_mime->m_ptr_value, static_cast<std::string_view::size_type>(m_host_length)};
  }

  return std::string_view{};
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
HTTPHdr::port_get()
{
  this->_test_and_fill_target_cache();
  return m_port;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
HTTPHdr::is_target_in_url() const
{
  this->_test_and_fill_target_cache();
  return m_target_in_url;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
HTTPHdr::is_port_in_header() const
{
  this->_test_and_fill_target_cache();
  return m_port_in_header;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::mark_target_dirty() const
{
  m_target_cached = false;
}
/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline HTTPType
http_hdr_type_get(HTTPHdrImpl *hh)
{
  return (hh->m_polarity);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline HTTPType
HTTPHdr::type_get() const
{
  ink_assert(valid());
  return http_hdr_type_get(m_http);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline HTTPVersion
HTTPHdr::version_get() const
{
  ink_assert(valid());
  return m_http->m_version;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline static HTTPKeepAlive
is_header_keep_alive(const HTTPVersion &http_version, const MIMEField *con_hdr)
{
  enum class ConToken {
    NONE = 0,
    KEEP_ALIVE,
    CLOSE,
  };

  auto          con_token  = ConToken::NONE;
  HTTPKeepAlive keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
  //    *unknown_tokens = false;

  if (con_hdr) {
    if (con_hdr->value_get_index("keep-alive"sv) >= 0)
      con_token = ConToken::KEEP_ALIVE;
    else if (con_hdr->value_get_index("close"sv) >= 0)
      con_token = ConToken::CLOSE;
  }

  if (HTTP_1_0 == http_version) {
    keep_alive = (con_token == ConToken::KEEP_ALIVE) ? (HTTPKeepAlive::KEEPALIVE) : (HTTPKeepAlive::NO_KEEPALIVE);
  } else if (HTTP_1_1 == http_version) {
    // We deviate from the spec here.  If the we got a response where
    //   where there is no Connection header and the request 1.0 was
    //   1.0 don't treat this as keep-alive since Netscape-Enterprise/3.6 SP1
    //   server doesn't
    keep_alive = ((con_token == ConToken::KEEP_ALIVE) || (con_token == ConToken::NONE && HTTP_1_1 == http_version)) ?
                   (HTTPKeepAlive::KEEPALIVE) :
                   (HTTPKeepAlive::NO_KEEPALIVE);
  } else {
    keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
  }
  return (keep_alive);
}

inline HTTPKeepAlive
HTTPHdr::keep_alive_get() const
{
  HTTPKeepAlive    retval = HTTPKeepAlive::NO_KEEPALIVE;
  const MIMEField *pc     = this->field_find(static_cast<std::string_view>(MIME_FIELD_PROXY_CONNECTION));
  if (pc != nullptr) {
    retval = is_header_keep_alive(this->version_get(), pc);
  } else {
    const MIMEField *c = this->field_find(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
    retval             = is_header_keep_alive(this->version_get(), c);
  }
  return retval;
}

inline bool
HTTPHdr::is_keep_alive_set() const
{
  return this->keep_alive_get() == HTTPKeepAlive::KEEPALIVE;
}

/**
   Check the status code is informational and expecting final response
   - e.g. "100 Continue", "103 Early Hints"

   Please note that "101 Switching Protocol" is not included.
 */
inline bool
HTTPHdr::expect_final_response() const
{
  switch (this->status_get()) {
  case HTTPStatus::CONTINUE:
  case HTTPStatus::EARLY_HINTS:
    return true;
  default:
    return false;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::version_set(HTTPVersion version)
{
  ink_assert(valid());
  http_hdr_version_set(m_http, version);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
HTTPHdr::method_get()
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::REQUEST);

  return http_hdr_method_get(m_http);
}

inline int
HTTPHdr::method_get_wksidx() const
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::REQUEST);

  return (m_http->u.req.m_method_wks_idx);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::method_set(std::string_view value)
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::REQUEST);

  int method_wks_idx = hdrtoken_tokenize(value.data(), static_cast<int>(value.length()));
  http_hdr_method_set(m_heap, m_http, value, method_wks_idx, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline URL *
HTTPHdr::url_create(URL *u)
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::REQUEST);

  u->set(this);
  u->create(m_heap);
  return (u);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline URL *
HTTPHdr::url_get() const
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::REQUEST);

  // It's entirely possible that someone changed URL in our impl
  // without updating the cached copy in the C++ layer.  Check
  // to see if this happened before handing back the url

  URLImpl *real_impl = m_http->u.req.m_url_impl;
  if (m_url_cached.m_url_impl != real_impl) {
    m_url_cached.set(this);
    m_url_cached.m_url_impl = real_impl;
    this->mark_target_dirty();
  }
  return (&m_url_cached);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline URL *
HTTPHdr::url_get(URL *url)
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::REQUEST);

  url->set(this); // attach refcount
  url->m_url_impl = m_http->u.req.m_url_impl;
  return (url);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::url_set(URL *url)
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::REQUEST);

  URLImpl *url_impl = m_http->u.req.m_url_impl;
  ::url_copy_onto(url->m_url_impl, url->m_heap, url_impl, m_heap, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::url_set(std::string_view value)
{
  URLImpl *url_impl;

  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::REQUEST);

  url_impl = m_http->u.req.m_url_impl;
  ::url_clear(url_impl);
  const char *str{value.data()};
  ::url_parse(m_heap, url_impl, &str, str + value.length(), true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline HTTPStatus
http_hdr_status_get(HTTPHdrImpl const *hh)
{
  ink_assert(hh->m_polarity == HTTPType::RESPONSE);
  return (HTTPStatus)hh->u.resp.m_status;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline HTTPStatus
HTTPHdr::status_get() const
{
  ink_assert(valid());

  if (m_http) {
    ink_assert(m_http->m_polarity == HTTPType::RESPONSE);
    return http_hdr_status_get(m_http);
  }

  return HTTPStatus::NONE;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::status_set(HTTPStatus status)
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::RESPONSE);

  http_hdr_status_set(m_http, status);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
HTTPHdr::reason_get()
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::RESPONSE);

  return http_hdr_reason_get(m_http);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::reason_set(std::string_view value)
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::RESPONSE);

  http_hdr_reason_set(m_heap, m_http, value, true);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
HTTPHdr::mark_early_data(bool flag) const
{
  ink_assert(valid());
  early_data = flag;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
HTTPHdr::is_early_data() const
{
  ink_assert(valid());
  return early_data;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline ParseResult
HTTPHdr::parse_req(HTTPParser *parser, const char **start, const char *end, bool eof, int strict_uri_parsing,
                   size_t max_request_line_size, size_t max_hdr_field_size)
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::REQUEST);

  return http_parser_parse_req(parser, m_heap, m_http, start, end, true, eof, strict_uri_parsing, max_request_line_size,
                               max_hdr_field_size);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline ParseResult
HTTPHdr::parse_resp(HTTPParser *parser, const char **start, const char *end, bool eof)
{
  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTPType::RESPONSE);

  return http_parser_parse_resp(parser, m_heap, m_http, start, end, true, eof);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
HTTPHdr::is_cache_control_set(const char *cc_directive_wks)
{
  ink_assert(valid());
  ink_assert(hdrtoken_is_wks(cc_directive_wks));

  const HdrTokenHeapPrefix *prefix = hdrtoken_wks_to_prefix(cc_directive_wks);
  ink_assert(prefix->wks_token_type == HdrTokenType::CACHE_CONTROL);

  uint32_t cc_mask = prefix->wks_type_specific.u.cache_control.cc_mask;
  if (get_cooked_cc_mask() & cc_mask)
    return (true);
  else
    return (false);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
HTTPHdr::is_pragma_no_cache_set()
{
  ink_assert(valid());
  return (get_cooked_pragma_no_cache());
}

inline char *
HTTPHdr::url_string_get_ref(int *length)
{
  return this->url_string_get(USE_HDR_HEAP_MAGIC, length);
}

inline std::string_view
HTTPHdr::path_get()
{
  URL *url = this->url_get();
  if (url) {
    return url->path_get();
  }
  return {};
}

inline std::string_view
HTTPHdr::query_get()
{
  URL *url = this->url_get();
  if (url) {
    return url->query_get();
  }
  return std::string_view{};
}

inline std::string_view
HTTPHdr::fragment_get()
{
  URL *url = this->url_get();
  if (url) {
    return url->fragment_get();
  }
  return std::string_view{};
}

inline std::string_view
HTTPHdr::scheme_get()
{
  URL *url = this->url_get();
  if (url) {
    return url->scheme_get();
  }
  return std::string_view{};
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

enum class CacheAltMagic : uint32_t {
  ALIVE     = 0xabcddeed,
  MARSHALED = 0xdcbadeed,
  DEAD      = 0xdeadeed,
};

// struct HTTPCacheAlt
struct HTTPCacheAlt {
  HTTPCacheAlt();
  void copy(HTTPCacheAlt *to_copy);
  void copy_frag_offsets_from(HTTPCacheAlt *src);
  void destroy();

  CacheAltMagic m_magic = CacheAltMagic::ALIVE;

  // Writeable is set to true is we reside
  //  in a buffer owned by this structure.
  // INVARIANT: if own the buffer this HttpCacheAlt
  //   we also own the buffers for the request &
  //   response headers
  int32_t m_writeable     = 1;
  int32_t m_unmarshal_len = -1;

  int32_t m_id  = -1;
  int32_t m_rid = -1;

  int32_t m_object_key[sizeof(CryptoHash) / sizeof(int32_t)];
  int32_t m_object_size[2];

  HTTPHdr m_request_hdr;
  HTTPHdr m_response_hdr;

  time_t m_request_sent_time      = 0;
  time_t m_response_received_time = 0;

  /// # of fragment offsets in this alternate.
  /// @note This is one less than the number of fragments.
  int m_frag_offset_count = 0;
  /// Type of offset for a fragment.
  using FragOffset = uint64_t;
  /// Table of fragment offsets.
  /// @note The offsets are forward looking so that frag[0] is the
  /// first byte past the end of fragment 0 which is also the first
  /// byte of fragment 1. For this reason there is no fragment offset
  /// for the last fragment.
  FragOffset *m_frag_offsets = nullptr;
  /// # of fragment offsets built in to object.
  static int constexpr N_INTEGRAL_FRAG_OFFSETS = 4;
  /// Integral fragment offset table.
  FragOffset m_integral_frag_offsets[N_INTEGRAL_FRAG_OFFSETS];

  // With clustering, our alt may be in cluster
  //  incoming channel buffer, when we are
  //  destroyed we decrement the refcount
  //  on that buffer so that it gets destroyed
  // We don't want to use a ref count ptr (Ptr<>)
  //  since our ownership model requires explicit
  //  destroys and ref count pointers defeat this
  RefCountObj *m_ext_buffer = nullptr;
};

class HTTPInfo
{
public:
  using FragOffset = HTTPCacheAlt::FragOffset; ///< Import type.

  HTTPCacheAlt *m_alt = nullptr;

  HTTPInfo() {}
  ~HTTPInfo() { clear(); }
  void
  clear()
  {
    m_alt = nullptr;
  }
  bool
  valid() const
  {
    return m_alt != nullptr;
  }

  void create();
  void destroy();

  void copy(HTTPInfo *to_copy);
  void
  copy_shallow(HTTPInfo *info)
  {
    m_alt = info->m_alt;
  }
  void      copy_frag_offsets_from(HTTPInfo *src);
  HTTPInfo &operator=(const HTTPInfo &m);

  int        marshal_length();
  int        marshal(char *buf, int len);
  static int unmarshal(char *buf, int len, RefCountObj *block_ref);
  static int unmarshal_v24_1(char *buf, int len, RefCountObj *block_ref);
  void       set_buffer_reference(RefCountObj *block_ref);
  int        get_handle(char *buf, int len);

  int32_t
  id_get() const
  {
    return m_alt->m_id;
  }
  int32_t
  rid_get()
  {
    return m_alt->m_rid;
  }

  void
  id_set(int32_t id)
  {
    m_alt->m_id = id;
  }
  void
  rid_set(int32_t id)
  {
    m_alt->m_rid = id;
  }

  CryptoHash object_key_get();
  void       object_key_get(CryptoHash *);
  bool       compare_object_key(const CryptoHash *);
  int64_t    object_size_get();

  void
  request_get(HTTPHdr *hdr)
  {
    hdr->copy_shallow(&m_alt->m_request_hdr);
  }
  void
  response_get(HTTPHdr *hdr)
  {
    hdr->copy_shallow(&m_alt->m_response_hdr);
  }

  HTTPHdr *
  request_get()
  {
    return &m_alt->m_request_hdr;
  }
  HTTPHdr *
  response_get()
  {
    return &m_alt->m_response_hdr;
  }

  URL *
  request_url_get(URL *url = nullptr)
  {
    return m_alt->m_request_hdr.url_get(url);
  }

  time_t
  request_sent_time_get()
  {
    return m_alt->m_request_sent_time;
  }
  time_t
  response_received_time_get()
  {
    return m_alt->m_response_received_time;
  }

  void object_key_set(CryptoHash &hash);
  void object_size_set(int64_t size);

  void
  request_set(const HTTPHdr *req)
  {
    m_alt->m_request_hdr.copy(req);
  }
  void
  response_set(const HTTPHdr *resp)
  {
    m_alt->m_response_hdr.copy(resp);
  }

  void
  request_sent_time_set(time_t t)
  {
    m_alt->m_request_sent_time = t;
  }
  void
  response_received_time_set(time_t t)
  {
    m_alt->m_response_received_time = t;
  }

  /// Get the fragment table.
  FragOffset *get_frag_table();
  /// Get the # of fragment offsets
  /// @note This is the size of the fragment offset table, and one less
  /// than the actual # of fragments.
  int get_frag_offset_count();
  /// Add an @a offset to the end of the fragment offset table.
  void push_frag_offset(FragOffset offset);

  // Sanity check functions
  static bool check_marshalled(char *buf, int len);

private:
  HTTPInfo(const HTTPInfo &h);
};

inline void
HTTPInfo::destroy()
{
  if (m_alt) {
    if (m_alt->m_writeable) {
      m_alt->destroy();
    } else if (m_alt->m_ext_buffer) {
      if (m_alt->m_ext_buffer->refcount_dec() == 0) {
        m_alt->m_ext_buffer->free();
      }
    }
  }
  clear();
}

inline HTTPInfo &
HTTPInfo::operator=(const HTTPInfo &m)
{
  m_alt = m.m_alt;
  return *this;
}

inline CryptoHash
HTTPInfo::object_key_get()
{
  CryptoHash val;
  int32_t   *pi = reinterpret_cast<int32_t *>(&val);

  memcpy(pi, m_alt->m_object_key, sizeof(CryptoHash));

  return val;
}

inline void
HTTPInfo::object_key_get(CryptoHash *hash)
{
  int32_t *pi = reinterpret_cast<int32_t *>(hash);
  memcpy(pi, m_alt->m_object_key, CRYPTO_HASH_SIZE);
}

inline bool
HTTPInfo::compare_object_key(const CryptoHash *hash)
{
  int32_t const *pi = reinterpret_cast<int32_t const *>(hash);
  return memcmp(pi, m_alt->m_object_key, CRYPTO_HASH_SIZE) == 0;
}

inline int64_t
HTTPInfo::object_size_get()
{
  int64_t  val = 0; // make gcc shut up.
  int32_t *pi  = reinterpret_cast<int32_t *>(&val);

  pi[0] = m_alt->m_object_size[0];
  pi[1] = m_alt->m_object_size[1];
  return val;
}

inline void
HTTPInfo::object_key_set(CryptoHash &hash)
{
  int32_t *pi = reinterpret_cast<int32_t *>(&hash);
  memcpy(m_alt->m_object_key, pi, CRYPTO_HASH_SIZE);
}

inline void
HTTPInfo::object_size_set(int64_t size)
{
  int32_t *pi             = reinterpret_cast<int32_t *>(&size);
  m_alt->m_object_size[0] = pi[0];
  m_alt->m_object_size[1] = pi[1];
}

inline HTTPInfo::FragOffset *
HTTPInfo::get_frag_table()
{
  return m_alt ? m_alt->m_frag_offsets : nullptr;
}

inline int
HTTPInfo::get_frag_offset_count()
{
  return m_alt ? m_alt->m_frag_offset_count : 0;
}
