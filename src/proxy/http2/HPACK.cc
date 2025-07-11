/** @file

  [RFC 7541] HPACK: Header Compression for HTTP/2

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

#include "proxy/http2/HPACK.h"

#include "tsutil/LocalBuffer.h"
#include "swoc/TextView.h"

namespace
{
// [RFC 7541] 4.1. Calculating Table Size
// The size of an entry is the sum of its name's length in octets (as defined in Section 5.2),
// its value's length in octets, and 32.
const static unsigned ADDITIONAL_OCTETS = 32;

using TS_HPACK_STATIC_TABLE_ENTRY = enum {
  TS_HPACK_STATIC_TABLE_0 = 0,
  TS_HPACK_STATIC_TABLE_AUTHORITY,
  TS_HPACK_STATIC_TABLE_METHOD_GET,
  TS_HPACK_STATIC_TABLE_METHOD_POST,
  TS_HPACK_STATIC_TABLE_PATH_ROOT,
  TS_HPACK_STATIC_TABLE_PATH_INDEX,
  TS_HPACK_STATIC_TABLE_SCHEME_HTTP,
  TS_HPACK_STATIC_TABLE_SCHEME_HTTPS,
  TS_HPACK_STATIC_TABLE_STATUS_200,
  TS_HPACK_STATIC_TABLE_STATUS_204,
  TS_HPACK_STATIC_TABLE_STATUS_206,
  TS_HPACK_STATIC_TABLE_STATUS_304,
  TS_HPACK_STATIC_TABLE_STATUS_400,
  TS_HPACK_STATIC_TABLE_STATUS_404,
  TS_HPACK_STATIC_TABLE_STATUS_500,
  TS_HPACK_STATIC_TABLE_ACCEPT_CHARSET,
  TS_HPACK_STATIC_TABLE_ACCEPT_ENCODING,
  TS_HPACK_STATIC_TABLE_ACCEPT_LANGUAGE,
  TS_HPACK_STATIC_TABLE_ACCEPT_RANGES,
  TS_HPACK_STATIC_TABLE_ACCEPT,
  TS_HPACK_STATIC_TABLE_ACCESS_CONTROL_ALLOW_ORIGIN,
  TS_HPACK_STATIC_TABLE_AGE,
  TS_HPACK_STATIC_TABLE_ALLOW,
  TS_HPACK_STATIC_TABLE_AUTHORIZATION,
  TS_HPACK_STATIC_TABLE_CACHE_CONTROL,
  TS_HPACK_STATIC_TABLE_CONTENT_DISPOSITION,
  TS_HPACK_STATIC_TABLE_CONTENT_ENCODING,
  TS_HPACK_STATIC_TABLE_CONTENT_LANGUAGE,
  TS_HPACK_STATIC_TABLE_CONTENT_LENGTH,
  TS_HPACK_STATIC_TABLE_CONTENT_LOCATION,
  TS_HPACK_STATIC_TABLE_CONTENT_RANGE,
  TS_HPACK_STATIC_TABLE_CONTENT_TYPE,
  TS_HPACK_STATIC_TABLE_COOKIE,
  TS_HPACK_STATIC_TABLE_DATE,
  TS_HPACK_STATIC_TABLE_ETAG,
  TS_HPACK_STATIC_TABLE_EXPECT,
  TS_HPACK_STATIC_TABLE_EXPIRES,
  TS_HPACK_STATIC_TABLE_FROM,
  TS_HPACK_STATIC_TABLE_HOST,
  TS_HPACK_STATIC_TABLE_IF_MATCH,
  TS_HPACK_STATIC_TABLE_IF_MODIFIED_SINCE,
  TS_HPACK_STATIC_TABLE_IF_NONE_MATCH,
  TS_HPACK_STATIC_TABLE_IF_RANGE,
  TS_HPACK_STATIC_TABLE_IF_UNMODIFIED_SINCE,
  TS_HPACK_STATIC_TABLE_LAST_MODIFIED,
  TS_HPACK_STATIC_TABLE_LINK,
  TS_HPACK_STATIC_TABLE_LOCATION,
  TS_HPACK_STATIC_TABLE_MAX_FORWARDS,
  TS_HPACK_STATIC_TABLE_PROXY_AUTHENTICATE,
  TS_HPACK_STATIC_TABLE_PROXY_AUTHORIZATION,
  TS_HPACK_STATIC_TABLE_RANGE,
  TS_HPACK_STATIC_TABLE_REFERER,
  TS_HPACK_STATIC_TABLE_REFRESH,
  TS_HPACK_STATIC_TABLE_RETRY_AFTER,
  TS_HPACK_STATIC_TABLE_SERVER,
  TS_HPACK_STATIC_TABLE_SET_COOKIE,
  TS_HPACK_STATIC_TABLE_STRICT_TRANSPORT_SECURITY,
  TS_HPACK_STATIC_TABLE_TRANSFER_ENCODING,
  TS_HPACK_STATIC_TABLE_USER_AGENT,
  TS_HPACK_STATIC_TABLE_VARY,
  TS_HPACK_STATIC_TABLE_VIA,
  TS_HPACK_STATIC_TABLE_WWW_AUTHENTICATE,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM
};

constexpr int HPACK_STATIC_TABLE_OFFSET[26] = {
  TS_HPACK_STATIC_TABLE_ACCEPT_CHARSET,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM,
  TS_HPACK_STATIC_TABLE_CACHE_CONTROL,
  TS_HPACK_STATIC_TABLE_DATE,
  TS_HPACK_STATIC_TABLE_ETAG,
  TS_HPACK_STATIC_TABLE_FROM,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM,
  TS_HPACK_STATIC_TABLE_HOST,
  TS_HPACK_STATIC_TABLE_IF_MATCH,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM,
  TS_HPACK_STATIC_TABLE_LAST_MODIFIED,
  TS_HPACK_STATIC_TABLE_MAX_FORWARDS,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM,
  TS_HPACK_STATIC_TABLE_PROXY_AUTHENTICATE,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM,
  TS_HPACK_STATIC_TABLE_RANGE,
  TS_HPACK_STATIC_TABLE_SERVER,
  TS_HPACK_STATIC_TABLE_TRANSFER_ENCODING,
  TS_HPACK_STATIC_TABLE_USER_AGENT,
  TS_HPACK_STATIC_TABLE_VARY,
  TS_HPACK_STATIC_TABLE_WWW_AUTHENTICATE,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM,
  TS_HPACK_STATIC_TABLE_ENTRY_NUM,
};

constexpr HpackHeaderField STATIC_TABLE[] = {
  {"",                            ""             },
  {":authority",                  ""             },
  {":method",                     "GET"          },
  {":method",                     "POST"         },
  {":path",                       "/"            },
  {":path",                       "/index.html"  },
  {":scheme",                     "http"         },
  {":scheme",                     "https"        },
  {":status",                     "200"          },
  {":status",                     "204"          },
  {":status",                     "206"          },
  {":status",                     "304"          },
  {":status",                     "400"          },
  {":status",                     "404"          },
  {":status",                     "500"          },
  {"accept-charset",              ""             },
  {"accept-encoding",             "gzip, deflate"},
  {"accept-language",             ""             },
  {"accept-ranges",               ""             },
  {"accept",                      ""             },
  {"access-control-allow-origin", ""             },
  {"age",                         ""             },
  {"allow",                       ""             },
  {"authorization",               ""             },
  {"cache-control",               ""             },
  {"content-disposition",         ""             },
  {"content-encoding",            ""             },
  {"content-language",            ""             },
  {"content-length",              ""             },
  {"content-location",            ""             },
  {"content-range",               ""             },
  {"content-type",                ""             },
  {"cookie",                      ""             },
  {"date",                        ""             },
  {"etag",                        ""             },
  {"expect",                      ""             },
  {"expires",                     ""             },
  {"from",                        ""             },
  {"host",                        ""             },
  {"if-match",                    ""             },
  {"if-modified-since",           ""             },
  {"if-none-match",               ""             },
  {"if-range",                    ""             },
  {"if-unmodified-since",         ""             },
  {"last-modified",               ""             },
  {"link",                        ""             },
  {"location",                    ""             },
  {"max-forwards",                ""             },
  {"proxy-authenticate",          ""             },
  {"proxy-authorization",         ""             },
  {"range",                       ""             },
  {"referer",                     ""             },
  {"refresh",                     ""             },
  {"retry-after",                 ""             },
  {"server",                      ""             },
  {"set-cookie",                  ""             },
  {"strict-transport-security",   ""             },
  {"transfer-encoding",           ""             },
  {"user-agent",                  ""             },
  {"vary",                        ""             },
  {"via",                         ""             },
  {"www-authenticate",            ""             }
};

constexpr std::string_view HPACK_HDR_FIELD_COOKIE        = STATIC_TABLE[TS_HPACK_STATIC_TABLE_COOKIE].name;
constexpr std::string_view HPACK_HDR_FIELD_AUTHORIZATION = STATIC_TABLE[TS_HPACK_STATIC_TABLE_AUTHORIZATION].name;

DbgCtl dbg_ctl_hpack_encode{"hpack_encode"};
DbgCtl dbg_ctl_hpack_decode{"hpack_decode"};

//
// Local functions
//
bool
hpack_field_is_literal(HpackField ftype)
{
  return ftype == HpackField::INDEXED_LITERAL || ftype == HpackField::NOINDEX_LITERAL || ftype == HpackField::NEVERINDEX_LITERAL;
}

// Try not to use memcmp(sv, sv) and strncasecmp(sv, sv) because we don't care which value comes first on a dictionary.
// Return immediately if the lengths of given strings don't match.
// Also, we noticed with profiling that taking char* and int was more performant than taking std::string_view.
inline bool
match(const char *s1, int s1_len, const char *s2, int s2_len)
{
  if (s1_len != s2_len) {
    return false;
  }

  if (s1 == s2) {
    return true;
  }

  if (memcmp(s1, s2, s1_len) != 0) {
    return false;
  }

  return true;
}

//
// The first byte of an HPACK field unambiguously tells us what
// kind of field it is. Field types are specified in the high 4 bits
// and all bits are defined, so there's no way to get an invalid field type.
//
HpackField
hpack_parse_field_type(uint8_t ftype)
{
  if (ftype & 0x80) {
    return HpackField::INDEX;
  }

  if (ftype & 0x40) {
    return HpackField::INDEXED_LITERAL;
  }

  if (ftype & 0x20) {
    return HpackField::TABLESIZE_UPDATE;
  }

  if (ftype & 0x10) {
    return HpackField::NEVERINDEX_LITERAL;
  }

  ink_assert((ftype & 0xf0) == 0x0);
  return HpackField::NOINDEX_LITERAL;
}

//
// HpackStaticTable
//
namespace HpackStaticTable
{
  HpackLookupResult
  lookup(const HpackHeaderField &header)
  {
    HpackLookupResult result;

    // Limit the search range of static table
    unsigned int start = 1; // First effective index for TS_HPACK_STATIC_TABLE_ENTRY is 1
    unsigned int end   = TS_HPACK_STATIC_TABLE_ENTRY_NUM;
    if (const auto c = header.name[0]; 'a' <= c && c <= 'z') {
      start = HPACK_STATIC_TABLE_OFFSET[c - 'a'];
      if ('z' != c) {
        // This does not always set the ideal end index but works for some cases
        end = HPACK_STATIC_TABLE_OFFSET[c - 'a' + 1];
      }
    } else if (':' == c) {
      end = HPACK_STATIC_TABLE_OFFSET[0];
    }

    for (unsigned int index = start; index < end; ++index) {
      // Profiling showed that use of const reference here is more performant than copying string_views.
      const std::string_view &name  = STATIC_TABLE[index].name;
      const std::string_view &value = STATIC_TABLE[index].value;

      // Check whether name (and value) are matched
      if (match(header.name.data(), header.name.length(), name.data(), name.length())) {
        if (match(header.value.data(), header.value.length(), value.data(), value.length())) {
          result.index      = index;
          result.index_type = HpackIndex::STATIC;
          result.match_type = HpackMatch::EXACT;
          break;
        } else if (!result.index) {
          result.index      = index;
          result.index_type = HpackIndex::STATIC;
          result.match_type = HpackMatch::NAME;
        }
      }
    }

    return result;
  }
} // namespace HpackStaticTable
} // namespace

//
// HpackIndexingTable
//
HpackLookupResult
HpackIndexingTable::lookup(const HpackHeaderField &header) const
{
  // static table
  HpackLookupResult result = HpackStaticTable::lookup(header);

  // if result is not EXACT match, lookup dynamic table
  if (result.match_type == HpackMatch::EXACT) {
    return result;
  }

  // dynamic table
  if (XpackLookupResult dt_result = this->_dynamic_table.lookup_relative(header.name, header.value);
      dt_result.match_type == XpackLookupResult::MatchType::EXACT) {
    return {static_cast<uint32_t>(TS_HPACK_STATIC_TABLE_ENTRY_NUM + dt_result.index), HpackIndex::DYNAMIC,
            static_cast<HpackMatch>(dt_result.match_type)};
  }

  return result;
}

int
HpackIndexingTable::get_header_field(uint32_t index, MIMEFieldWrapper &field) const
{
  // Index Address Space starts at 1, so index == 0 is invalid.
  if (!index) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  if (index < TS_HPACK_STATIC_TABLE_ENTRY_NUM) {
    // static table
    field.name_set(STATIC_TABLE[index].name.data(), STATIC_TABLE[index].name.size());
    field.value_set(STATIC_TABLE[index].value.data(), STATIC_TABLE[index].value.size());
  } else if (index < (TS_HPACK_STATIC_TABLE_ENTRY_NUM + _dynamic_table.count())) {
    // dynamic table
    size_t      name_len, value_len;
    const char *name;
    const char *value;

    auto result = _dynamic_table.lookup_relative(index - TS_HPACK_STATIC_TABLE_ENTRY_NUM, &name, &name_len, &value, &value_len);
    if (result.match_type != XpackLookupResult::MatchType::EXACT) {
      return HPACK_ERROR_COMPRESSION_ERROR;
    }

    field.name_set(name, name_len);
    field.value_set(value, value_len);
  } else {
    // [RFC 7541] 2.3.3. Index Address Space
    // Indices strictly greater than the sum of the lengths of both tables
    // MUST be treated as a decoding error.
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  return 0;
}

void
HpackIndexingTable::add_header_field(const HpackHeaderField &header)
{
  _dynamic_table.insert_entry(header.name, header.value);
}

uint32_t
HpackIndexingTable::maximum_size() const
{
  return _dynamic_table.maximum_size();
}

uint32_t
HpackIndexingTable::size() const
{
  return _dynamic_table.size();
}

void
HpackIndexingTable::update_maximum_size(uint32_t new_size)
{
  _dynamic_table.update_maximum_size(new_size);
}

//
// Global functions
//
int64_t
encode_indexed_header_field(uint8_t *buf_start, const uint8_t *buf_end, uint32_t index)
{
  if (buf_start >= buf_end) {
    return -1;
  }

  uint8_t *p = buf_start;

  // Index
  const int64_t len = xpack_encode_integer(p, buf_end, index, 7);
  if (len == -1) {
    return -1;
  }

  // Representation type
  if (p + 1 >= buf_end) {
    return -1;
  }

  *p |= 0x80;
  p  += len;

  Dbg(dbg_ctl_hpack_encode, "Encoded field: %d", index);
  return p - buf_start;
}

int64_t
encode_literal_header_field_with_indexed_name(uint8_t *buf_start, const uint8_t *buf_end, const HpackHeaderField &header,
                                              uint32_t index, HpackIndexingTable &indexing_table, HpackField type)
{
  uint8_t *p = buf_start;
  int64_t  len;
  uint8_t  prefix = 0, flag = 0;

  ink_assert(hpack_field_is_literal(type));

  switch (type) {
  case HpackField::INDEXED_LITERAL:
    indexing_table.add_header_field(header);
    prefix = 6;
    flag   = 0x40;
    break;
  case HpackField::NOINDEX_LITERAL:
    prefix = 4;
    flag   = 0x00;
    break;
  case HpackField::NEVERINDEX_LITERAL:
    prefix = 4;
    flag   = 0x10;
    break;
  default:
    return -1;
  }

  // Index
  *p  = 0;
  len = xpack_encode_integer(p, buf_end, index, prefix);
  if (len == -1) {
    return -1;
  }

  // Representation type
  if (p + 1 >= buf_end) {
    return -1;
  }
  *p |= flag;
  p  += len;

  // Value String
  len = xpack_encode_string(p, buf_end, header.value.data(), header.value.size());
  if (len == -1) {
    return -1;
  }
  p += len;

  Dbg(dbg_ctl_hpack_encode, "Encoded field: %d: %.*s", index, static_cast<int>(header.value.size()), header.value.data());
  return p - buf_start;
}

int64_t
encode_literal_header_field_with_new_name(uint8_t *buf_start, const uint8_t *buf_end, const HpackHeaderField &header,
                                          HpackIndexingTable &indexing_table, HpackField type)
{
  uint8_t *p = buf_start;
  int64_t  len;
  uint8_t  flag = 0;

  ink_assert(hpack_field_is_literal(type));

  switch (type) {
  case HpackField::INDEXED_LITERAL:
    indexing_table.add_header_field(header);
    flag = 0x40;
    break;
  case HpackField::NOINDEX_LITERAL:
    flag = 0x00;
    break;
  case HpackField::NEVERINDEX_LITERAL:
    flag = 0x10;
    break;
  default:
    return -1;
  }
  if (p + 1 >= buf_end) {
    return -1;
  }
  *(p++) = flag;

  // Name String
  len = xpack_encode_string(p, buf_end, header.name.data(), header.name.size());
  if (len == -1) {
    return -1;
  }
  p += len;

  // Value String
  len = xpack_encode_string(p, buf_end, header.value.data(), header.value.size());
  if (len == -1) {
    return -1;
  }

  p += len;

  Dbg(dbg_ctl_hpack_encode, "Encoded field: %.*s: %.*s", static_cast<int>(header.name.size()), header.name.data(),
      static_cast<int>(header.value.size()), header.value.data());

  return p - buf_start;
}

int64_t
encode_dynamic_table_size_update(uint8_t *buf_start, const uint8_t *buf_end, uint32_t size)
{
  buf_start[0]      = 0x20;
  const int64_t len = xpack_encode_integer(buf_start, buf_end, size, 5);
  if (len == -1) {
    return -1;
  }

  return len;
}

//
// [RFC 7541] 6.1. Indexed Header Field Representation
//
int64_t
decode_indexed_header_field(MIMEFieldWrapper &header, const uint8_t *buf_start, const uint8_t *buf_end,
                            HpackIndexingTable &indexing_table)
{
  uint64_t index = 0;
  int64_t  len   = 0;

  len = xpack_decode_integer(index, buf_start, buf_end, 7);
  if (len == XPACK_ERROR_COMPRESSION_ERROR) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  if (indexing_table.get_header_field(index, header) == HPACK_ERROR_COMPRESSION_ERROR) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  if (dbg_ctl_hpack_decode.on()) {
    auto decoded_name{header.name_get()};
    auto decoded_value{header.value_get()};

    Dbg(dbg_ctl_hpack_decode, "Decoded field: %.*s: %.*s", static_cast<int>(decoded_name.length()), decoded_name.data(),
        static_cast<int>(decoded_value.length()), decoded_value.data());
  }

  return len;
}

//
// [RFC 7541] 6.2. Literal Header Field Representation
// Decode Literal Header Field Representation based on HpackFieldType
//
int64_t
decode_literal_header_field(MIMEFieldWrapper &header, const uint8_t *buf_start, const uint8_t *buf_end,
                            HpackIndexingTable &indexing_table)
{
  const uint8_t *p                   = buf_start;
  bool           isIncremental       = false;
  uint64_t       index               = 0;
  int64_t        len                 = 0;
  HpackField     ftype               = hpack_parse_field_type(*p);
  bool           has_http2_violation = false;

  if (ftype == HpackField::INDEXED_LITERAL) {
    len           = xpack_decode_integer(index, p, buf_end, 6);
    isIncremental = true;
  } else if (ftype == HpackField::NEVERINDEX_LITERAL) {
    len = xpack_decode_integer(index, p, buf_end, 4);
  } else {
    ink_assert(ftype == HpackField::NOINDEX_LITERAL);
    len = xpack_decode_integer(index, p, buf_end, 4);
  }

  if (len == XPACK_ERROR_COMPRESSION_ERROR) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  p += len;

  // Decode header field name
  if (index) {
    if (indexing_table.get_header_field(index, header) == HPACK_ERROR_COMPRESSION_ERROR) {
      return HPACK_ERROR_COMPRESSION_ERROR;
    }
  } else {
    char    *name_str     = nullptr;
    uint64_t name_str_len = 0;

    len = xpack_decode_string(indexing_table.arena, &name_str, name_str_len, p, buf_end);
    if (len == XPACK_ERROR_COMPRESSION_ERROR) {
      return HPACK_ERROR_COMPRESSION_ERROR;
    }

    // Check whether header field name is lower case
    // XXX This check shouldn't be here because this rule is not a part of HPACK but HTTP2.
    for (uint32_t i = 0; i < name_str_len; i++) {
      if (ParseRules::is_upalpha(name_str[i])) {
        has_http2_violation = true;
        break;
      }
    }

    p += len;
    header.name_set(name_str, name_str_len);
    indexing_table.arena.str_free(name_str);
  }

  // Decode header field value
  char    *value_str     = nullptr;
  uint64_t value_str_len = 0;

  len = xpack_decode_string(indexing_table.arena, &value_str, value_str_len, p, buf_end);
  if (len == XPACK_ERROR_COMPRESSION_ERROR) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  p += len;
  header.value_set(value_str, value_str_len);
  indexing_table.arena.str_free(value_str);

  // Incremental Indexing adds header to header table as new entry
  if (isIncremental) {
    const MIMEField *field = header.field_get();
    indexing_table.add_header_field({field->name_get(), field->value_get()});
  }

  // Print decoded header field
  if (dbg_ctl_hpack_decode.on()) {
    auto decoded_name{header.name_get()};
    auto decoded_value{header.value_get()};

    Dbg(dbg_ctl_hpack_decode, "Decoded field: %.*s: %.*s", static_cast<int>(decoded_name.length()), decoded_name.data(),
        static_cast<int>(decoded_value.length()), decoded_value.data());
  }

  if (has_http2_violation) {
    // XXX Need to return the length to continue decoding
    return -(p - buf_start);
  } else {
    return p - buf_start;
  }
}

//
// [RFC 7541] 6.3. Dynamic Table Size Update
//
int64_t
update_dynamic_table_size(const uint8_t *buf_start, const uint8_t *buf_end, HpackIndexingTable &indexing_table,
                          uint32_t maximum_table_size)
{
  if (buf_start == buf_end) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  // Update header table size if its required.
  uint64_t size = 0;
  int64_t  len  = xpack_decode_integer(size, buf_start, buf_end, 5);
  if (len == XPACK_ERROR_COMPRESSION_ERROR) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  if (size > maximum_table_size) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  indexing_table.update_maximum_size(size);

  return len;
}

int64_t
hpack_decode_header_block(HpackIndexingTable &indexing_table, HTTPHdr *hdr, const uint8_t *in_buf, const size_t in_buf_len,
                          uint32_t max_header_size, uint32_t maximum_table_size)
{
  const uint8_t       *cursor               = in_buf;
  const uint8_t *const in_buf_end           = in_buf + in_buf_len;
  HdrHeap             *heap                 = hdr->m_heap;
  HTTPHdrImpl         *hh                   = hdr->m_http;
  bool                 header_field_started = false;
  bool                 has_http2_violation  = false;
  uint32_t             total_header_size    = 0;

  while (cursor < in_buf_end) {
    int64_t read_bytes = 0;

    // decode a header field encoded by HPACK
    MIMEField       *field = mime_field_create(heap, hh->m_fields_impl);
    MIMEFieldWrapper header(field, heap, hh->m_fields_impl);
    HpackField       ftype = hpack_parse_field_type(*cursor);

    switch (ftype) {
    case HpackField::INDEX:
      read_bytes = decode_indexed_header_field(header, cursor, in_buf_end, indexing_table);
      if (read_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
        return HPACK_ERROR_COMPRESSION_ERROR;
      }
      cursor               += read_bytes;
      header_field_started  = true;
      break;
    case HpackField::INDEXED_LITERAL:
    case HpackField::NOINDEX_LITERAL:
    case HpackField::NEVERINDEX_LITERAL:
      read_bytes = decode_literal_header_field(header, cursor, in_buf_end, indexing_table);
      if (read_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
        return HPACK_ERROR_COMPRESSION_ERROR;
      }
      if (read_bytes < 0) {
        has_http2_violation = true;
        read_bytes          = -read_bytes;
      }
      cursor               += read_bytes;
      header_field_started  = true;
      break;
    case HpackField::TABLESIZE_UPDATE:
      if (header_field_started) {
        return HPACK_ERROR_COMPRESSION_ERROR;
      }
      read_bytes = update_dynamic_table_size(cursor, in_buf_end, indexing_table, maximum_table_size);
      if (read_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
        return HPACK_ERROR_COMPRESSION_ERROR;
      }
      cursor += read_bytes;
      continue;
    }

    auto name{field->name_get()};
    auto value{field->value_get()};

    // [RFC 7540] 6.5.2. SETTINGS_MAX_HEADER_LIST_SIZE:
    // The value is based on the uncompressed size of header fields, including the length of the name and value in octets plus an
    // overhead of 32 octets for each header field.
    total_header_size += name.length() + value.length() + ADDITIONAL_OCTETS;

    if (total_header_size > max_header_size) {
      return HPACK_ERROR_SIZE_EXCEEDED_ERROR;
    }

    // Store to HdrHeap
    mime_hdr_field_attach(hh->m_fields_impl, field, 1, nullptr);
  }
  // Parsing all headers is done
  if (has_http2_violation) {
    return -(cursor - in_buf);
  } else {
    return cursor - in_buf;
  }
}

int64_t
hpack_encode_header_block(HpackIndexingTable &indexing_table, uint8_t *out_buf, const size_t out_buf_len, HTTPHdr *hdr,
                          int32_t maximum_table_size)
{
  uint8_t             *cursor      = out_buf;
  const uint8_t *const out_buf_end = out_buf + out_buf_len;

  ink_assert(http_hdr_type_get(hdr->m_http) != HTTPType::UNKNOWN);

  // Update dynamic table size
  if (maximum_table_size >= 0) {
    indexing_table.update_maximum_size(maximum_table_size);
    int64_t written = encode_dynamic_table_size_update(cursor, out_buf_end, maximum_table_size);
    if (written == HPACK_ERROR_COMPRESSION_ERROR) {
      return HPACK_ERROR_COMPRESSION_ERROR;
    }
    cursor += written;
  }

  for (auto &field : *hdr) {
    // Convert field name to lower case to follow HTTP2 spec
    // This conversion is needed because WKSs in MIMEFields is old fashioned
    std::string_view      original_name = field.name_get();
    int                   name_len      = original_name.size();
    ts::LocalBuffer<char> local_buffer(name_len);
    char                 *lower_name = local_buffer.data();
    for (int i = 0; i < name_len; i++) {
      lower_name[i] = ParseRules::ink_tolower(original_name[i]);
    }

    std::string_view name{lower_name, static_cast<size_t>(name_len)};
    std::string_view value = field.value_get();

    // Choose field representation (See RFC7541 7.1.3)
    // - Authorization header obviously should not be indexed
    // - Short Cookie header should not be indexed because of low entropy
    HpackField field_type;
    if ((value.size() < 20 && match(name.data(), name.length(), HPACK_HDR_FIELD_COOKIE.data(), HPACK_HDR_FIELD_COOKIE.length())) ||
        match(name.data(), name.length(), HPACK_HDR_FIELD_AUTHORIZATION.data(), HPACK_HDR_FIELD_AUTHORIZATION.length())) {
      field_type = HpackField::NEVERINDEX_LITERAL;
    } else {
      field_type = HpackField::INDEXED_LITERAL;
    }

    HpackHeaderField        header{name, value};
    const HpackLookupResult result = indexing_table.lookup(header);

    int64_t written = 0;
    switch (result.match_type) {
    case HpackMatch::NONE:
      written = encode_literal_header_field_with_new_name(cursor, out_buf_end, header, indexing_table, field_type);
      break;
    case HpackMatch::NAME:
      written =
        encode_literal_header_field_with_indexed_name(cursor, out_buf_end, header, result.index, indexing_table, field_type);
      break;
    case HpackMatch::EXACT:
      written = encode_indexed_header_field(cursor, out_buf_end, result.index);
      break;
    default:
      break;
    }

    if (written == HPACK_ERROR_COMPRESSION_ERROR) {
      return HPACK_ERROR_COMPRESSION_ERROR;
    }
    cursor += written;
  }
  return cursor - out_buf;
}

int32_t
hpack_get_maximum_table_size(HpackIndexingTable &indexing_table)
{
  return indexing_table.maximum_size();
}
