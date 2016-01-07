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

#include "HPACK.h"
#include "HuffmanCodec.h"

// Constant strings for pseudo headers of HPACK
const char *HPACK_VALUE_SCHEME = ":scheme";
const char *HPACK_VALUE_METHOD = ":method";
const char *HPACK_VALUE_AUTHORITY = ":authority";
const char *HPACK_VALUE_PATH = ":path";
const char *HPACK_VALUE_STATUS = ":status";

const unsigned HPACK_LEN_SCHEME = countof(":scheme") - 1;
const unsigned HPACK_LEN_METHOD = countof(":method") - 1;
const unsigned HPACK_LEN_AUTHORITY = countof(":authority") - 1;
const unsigned HPACK_LEN_PATH = countof(":path") - 1;
const unsigned HPACK_LEN_STATUS = countof(":status") - 1;

// [RFC 7541] 4.1. Calculating Table Size
// The size of an entry is the sum of its name's length in octets (as defined in Section 5.2),
// its value's length in octets, and 32.
const static unsigned ADDITIONAL_OCTETS = 32;

typedef enum {
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
} TS_HPACK_STATIC_TABLE_ENTRY;

const static struct {
  const char *name;
  const char *value;
} STATIC_TABLE[] = {{"", ""},
                    {":authority", ""},
                    {":method", "GET"},
                    {":method", "POST"},
                    {":path", "/"},
                    {":path", "/index.html"},
                    {":scheme", "http"},
                    {":scheme", "https"},
                    {":status", "200"},
                    {":status", "204"},
                    {":status", "206"},
                    {":status", "304"},
                    {":status", "400"},
                    {":status", "404"},
                    {":status", "500"},
                    {"accept-charset", ""},
                    {"accept-encoding", "gzip, deflate"},
                    {"accept-language", ""},
                    {"accept-ranges", ""},
                    {"accept", ""},
                    {"access-control-allow-origin", ""},
                    {"age", ""},
                    {"allow", ""},
                    {"authorization", ""},
                    {"cache-control", ""},
                    {"content-disposition", ""},
                    {"content-encoding", ""},
                    {"content-language", ""},
                    {"content-length", ""},
                    {"content-location", ""},
                    {"content-range", ""},
                    {"content-type", ""},
                    {"cookie", ""},
                    {"date", ""},
                    {"etag", ""},
                    {"expect", ""},
                    {"expires", ""},
                    {"from", ""},
                    {"host", ""},
                    {"if-match", ""},
                    {"if-modified-since", ""},
                    {"if-none-match", ""},
                    {"if-range", ""},
                    {"if-unmodified-since", ""},
                    {"last-modified", ""},
                    {"link", ""},
                    {"location", ""},
                    {"max-forwards", ""},
                    {"proxy-authenticate", ""},
                    {"proxy-authorization", ""},
                    {"range", ""},
                    {"referer", ""},
                    {"refresh", ""},
                    {"retry-after", ""},
                    {"server", ""},
                    {"set-cookie", ""},
                    {"strict-transport-security", ""},
                    {"transfer-encoding", ""},
                    {"user-agent", ""},
                    {"vary", ""},
                    {"via", ""},
                    {"www-authenticate", ""}};

Http2LookupIndexResult
Http2IndexingTable::get_index(const MIMEFieldWrapper &field) const
{
  Http2LookupIndexResult result;
  int target_name_len = 0, target_value_len = 0;
  const char *target_name = field.name_get(&target_name_len);
  const char *target_value = field.value_get(&target_value_len);
  const int entry_num = TS_HPACK_STATIC_TABLE_ENTRY_NUM + _dynamic_table.get_current_entry_num();

  for (int index = 1; index < entry_num; ++index) {
    const char *table_name, *table_value;
    int table_name_len = 0, table_value_len = 0;

    if (index < TS_HPACK_STATIC_TABLE_ENTRY_NUM) {
      // static table
      table_name = STATIC_TABLE[index].name;
      table_value = STATIC_TABLE[index].value;
      table_name_len = strlen(table_name);
      table_value_len = strlen(table_value);
    } else {
      // dynamic table
      const MIMEField *m_field = _dynamic_table.get_header_field(index - TS_HPACK_STATIC_TABLE_ENTRY_NUM);

      table_name = m_field->name_get(&table_name_len);
      table_value = m_field->value_get(&table_value_len);
    }

    // Check whether name (and value) are matched
    if (ptr_len_casecmp(target_name, target_name_len, table_name, table_name_len) == 0) {
      if (ptr_len_cmp(target_value, target_value_len, table_value, table_value_len) == 0) {
        result.index = index;
        result.value_is_indexed = true;
        break;
      } else if (!result.index) {
        result.index = index;
      }
    }
  }

  return result;
}

int
Http2IndexingTable::get_header_field(uint32_t index, MIMEFieldWrapper &field) const
{
  // Index Address Space starts at 1, so index == 0 is invalid.
  if (!index)
    return HPACK_ERROR_COMPRESSION_ERROR;

  if (index < TS_HPACK_STATIC_TABLE_ENTRY_NUM) {
    // static table
    field.name_set(STATIC_TABLE[index].name, strlen(STATIC_TABLE[index].name));
    field.value_set(STATIC_TABLE[index].value, strlen(STATIC_TABLE[index].value));
  } else if (index < TS_HPACK_STATIC_TABLE_ENTRY_NUM + _dynamic_table.get_current_entry_num()) {
    // dynamic table
    const MIMEField *m_field = _dynamic_table.get_header_field(index - TS_HPACK_STATIC_TABLE_ENTRY_NUM);

    int name_len, value_len;
    const char *name = m_field->name_get(&name_len);
    const char *value = m_field->value_get(&value_len);

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
Http2IndexingTable::add_header_field_to_dynamic_table(const MIMEField *field)
{
  _dynamic_table.add_header_field(field);
}

uint32_t
Http2IndexingTable::get_dynamic_table_size() const
{
  return _dynamic_table.get_size();
}

bool
Http2IndexingTable::set_dynamic_table_size(uint32_t new_size)
{
  return _dynamic_table.set_size(new_size);
}

bool
Http2IndexingTable::is_header_in_dynamic_table(const char *target_name, const char *target_value) const
{
  return _dynamic_table.is_header_in(target_name, target_value);
}

const MIMEField *
Http2DynamicTable::get_header_field(uint32_t index) const
{
  return _headers.get(index);
}

void
Http2DynamicTable::add_header_field(const MIMEField *field)
{
  int name_len, value_len;
  const char *name = field->name_get(&name_len);
  const char *value = field->value_get(&value_len);
  uint32_t header_size = ADDITIONAL_OCTETS + name_len + value_len;

  if (header_size > _settings_dynamic_table_size) {
    // [RFC 7541] 4.4. Entry Eviction When Adding New Entries
    // It is not an error to attempt to add an entry that is larger than
    // the maximum size; an attempt to add an entry larger than the entire
    // table causes the table to be emptied of all existing entries.
    _headers.clear();
    _mhdr->fields_clear();
    _current_size = 0;
  } else {
    _current_size += header_size;
    while (_current_size > _settings_dynamic_table_size) {
      int last_name_len, last_value_len;
      MIMEField *last_field = _headers.last();

      last_field->name_get(&last_name_len);
      last_field->value_get(&last_value_len);
      _current_size -= ADDITIONAL_OCTETS + last_name_len + last_value_len;

      _headers.remove_index(_headers.length() - 1);
      _mhdr->field_delete(last_field, false);
    }

    MIMEField *new_field = _mhdr->field_create(name, name_len);
    new_field->value_set(_mhdr->m_heap, _mhdr->m_mime, value, value_len);
    _mhdr->field_attach(new_field);
    // XXX Because entire Vec instance is copied, Its too expensive!
    _headers.insert(0, new_field);
  }
}

uint32_t
Http2DynamicTable::get_size() const
{
  return _current_size;
}

//
// [RFC 7541] 4.3. Entry Eviction when Header Table Size Changes
//
// Whenever the maximum size for the header table is reduced, entries
// are evicted from the end of the header table until the size of the
// header table is less than or equal to the maximum size.
//
bool
Http2DynamicTable::set_size(uint32_t new_size)
{
  while (_current_size > new_size) {
    if (_headers.n <= 0) {
      return false;
    }
    int last_name_len, last_value_len;
    MIMEField *last_field = _headers.last();

    last_field->name_get(&last_name_len);
    last_field->value_get(&last_value_len);
    _current_size -= ADDITIONAL_OCTETS + last_name_len + last_value_len;

    _headers.remove_index(_headers.length() - 1);
    _mhdr->field_delete(last_field, false);
  }

  _settings_dynamic_table_size = new_size;
  return true;
}

const uint32_t
Http2DynamicTable::get_current_entry_num() const
{
  return _headers.length();
}

bool
Http2DynamicTable::is_header_in(const char *target_name, const char *target_value) const
{
  const MIMEField *field = _mhdr->field_find(target_name, strlen(target_name));

  if (field) {
    do {
      int target_value_len = strlen(target_value);
      int table_value_len = 0;
      const char *table_value = field->value_get(&table_value_len);
      if (ptr_len_cmp(target_value, target_value_len, table_value, table_value_len) == 0) {
        return true;
      }
    } while (field->has_dups() && (field = field->m_next_dup) != NULL);
  }

  return false;
}

//
// The first byte of an HPACK field unambiguously tells us what
// kind of field it is. Field types are specified in the high 4 bits
// and all bits are defined, so there's no way to get an invalid field type.
//
HpackFieldType
hpack_parse_field_type(uint8_t ftype)
{
  if (ftype & 0x80) {
    return HPACK_FIELD_INDEX;
  }

  if (ftype & 0x40) {
    return HPACK_FIELD_INDEXED_LITERAL;
  }

  if (ftype & 0x20) {
    return HPACK_FIELD_TABLESIZE_UPDATE;
  }

  if (ftype & 0x10) {
    return HPACK_FIELD_NEVERINDEX_LITERAL;
  }

  ink_assert((ftype & 0xf0) == 0x0);
  return HPACK_FIELD_NOINDEX_LITERAL;
}

//
// [RFC 7541] 5.1. Integer representation
//
int64_t
encode_integer(uint8_t *buf_start, const uint8_t *buf_end, uint32_t value, uint8_t n)
{
  if (buf_start >= buf_end)
    return -1;

  uint8_t *p = buf_start;

  if (value < (static_cast<uint32_t>(1 << n) - 1)) {
    *(p++) = value;
  } else {
    *(p++) = (1 << n) - 1;
    value -= (1 << n) - 1;
    while (value >= 128) {
      if (p >= buf_end) {
        return -1;
      }
      *(p++) = (value & 0x7F) | 0x80;
      value = value >> 7;
    }
    if (p + 1 >= buf_end) {
      return -1;
    }
    *(p++) = value;
  }
  return p - buf_start;
}

int64_t
encode_string(uint8_t *buf_start, const uint8_t *buf_end, const char *value, size_t value_len)
{
  uint8_t *p = buf_start;
  bool use_huffman = true;
  char *data = NULL;
  int64_t data_len = 0;

  // TODO Choose whether to use Huffman encoding wisely

  if (use_huffman) {
    data = static_cast<char *>(ats_malloc(value_len * 4));
    if (data == NULL)
      return -1;
    data_len = huffman_encode(reinterpret_cast<uint8_t *>(data), reinterpret_cast<const uint8_t *>(value), value_len);
  }

  // Length
  const int64_t len = encode_integer(p, buf_end, data_len, 7);
  if (len == -1) {
    if (use_huffman) {
      ats_free(data);
    }

    return -1;
  }

  if (use_huffman) {
    *p |= 0x80;
  }
  p += len;

  if (buf_end < p || buf_end - p < data_len) {
    if (use_huffman) {
      ats_free(data);
    }

    return -1;
  }

  // Value
  memcpy(p, data, data_len);
  p += data_len;

  if (use_huffman) {
    ats_free(data);
  }

  return p - buf_start;
}

int64_t
encode_indexed_header_field(uint8_t *buf_start, const uint8_t *buf_end, uint32_t index)
{
  if (buf_start >= buf_end)
    return -1;

  uint8_t *p = buf_start;

  // Index
  const int64_t len = encode_integer(p, buf_end, index, 7);
  if (len == -1)
    return -1;

  // Representation type
  if (p + 1 >= buf_end) {
    return -1;
  }

  *p |= 0x80;
  p += len;

  Debug("http2_hpack_encode", "Encoded field: %d", index);
  return p - buf_start;
}

int64_t
encode_literal_header_field_with_indexed_name(uint8_t *buf_start, const uint8_t *buf_end, const MIMEFieldWrapper &header,
                                              uint32_t index, Http2IndexingTable &indexing_table, HpackFieldType type)
{
  uint8_t *p = buf_start;
  int64_t len;
  uint8_t prefix = 0, flag = 0;

  ink_assert(hpack_field_is_literal(type));

  switch (type) {
  case HPACK_FIELD_INDEXED_LITERAL:
    indexing_table.add_header_field_to_dynamic_table(header.field_get());
    prefix = 6;
    flag = 0x40;
    break;
  case HPACK_FIELD_NOINDEX_LITERAL:
    prefix = 4;
    flag = 0x00;
    break;
  case HPACK_FIELD_NEVERINDEX_LITERAL:
    prefix = 4;
    flag = 0x10;
    break;
  default:
    return -1;
  }

  // Index
  len = encode_integer(p, buf_end, index, prefix);
  if (len == -1)
    return -1;

  // Representation type
  if (p + 1 >= buf_end) {
    return -1;
  }
  *p |= flag;
  p += len;

  // Value String
  int value_len;
  const char *value = header.value_get(&value_len);
  len = encode_string(p, buf_end, value, value_len);
  if (len == -1)
    return -1;
  p += len;

  Debug("http2_hpack_encode", "Encoded field: %d: %.*s", index, value_len, value);
  return p - buf_start;
}

int64_t
encode_literal_header_field_with_new_name(uint8_t *buf_start, const uint8_t *buf_end, const MIMEFieldWrapper &header,
                                          Http2IndexingTable &indexing_table, HpackFieldType type)
{
  uint8_t *p = buf_start;
  int64_t len;
  uint8_t flag = 0;

  ink_assert(hpack_field_is_literal(type));

  switch (type) {
  case HPACK_FIELD_INDEXED_LITERAL:
    indexing_table.add_header_field_to_dynamic_table(header.field_get());
    flag = 0x40;
    break;
  case HPACK_FIELD_NOINDEX_LITERAL:
    flag = 0x00;
    break;
  case HPACK_FIELD_NEVERINDEX_LITERAL:
    flag = 0x10;
    break;
  default:
    return -1;
  }
  if (p + 1 >= buf_end) {
    return -1;
  }
  *(p++) = flag;

  // Convert field name to lower case
  Arena arena;
  int name_len;
  const char *name = header.name_get(&name_len);
  char *lower_name = arena.str_store(name, name_len);
  for (int i = 0; i < name_len; i++)
    lower_name[i] = ParseRules::ink_tolower(lower_name[i]);

  // Name String
  len = encode_string(p, buf_end, lower_name, name_len);
  if (len == -1)
    return -1;
  p += len;

  // Value String
  int value_len;
  const char *value = header.value_get(&value_len);
  len = encode_string(p, buf_end, value, value_len);
  if (len == -1) {
    return -1;
  }

  p += len;

  Debug("http2_hpack_encode", "Encoded field: %.*s: %.*s", name_len, name, value_len, value);
  return p - buf_start;
}

//
// [RFC 7541] 5.1. Integer representation
//
int64_t
decode_integer(uint32_t &dst, const uint8_t *buf_start, const uint8_t *buf_end, uint8_t n)
{
  if (buf_start >= buf_end) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  const uint8_t *p = buf_start;

  dst = (*p & ((1 << n) - 1));
  if (dst == static_cast<uint32_t>(1 << n) - 1) {
    int m = 0;
    do {
      if (++p >= buf_end)
        return HPACK_ERROR_COMPRESSION_ERROR;

      uint32_t added_value = *p & 0x7f;
      if ((UINT32_MAX >> m) < added_value) {
        // Excessively large integer encodings - in value or octet
        // length - MUST be treated as a decoding error.
        return HPACK_ERROR_COMPRESSION_ERROR;
      }
      dst += added_value << m;
      m += 7;
    } while (*p & 0x80);
  }

  return p - buf_start + 1;
}

//
// [RFC 7541] 5.2. String Literal Representation
// return content from String Data (Length octets) with huffman decoding if it is encoded
//
int64_t
decode_string(Arena &arena, char **str, uint32_t &str_length, const uint8_t *buf_start, const uint8_t *buf_end)
{
  if (buf_start >= buf_end) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  const uint8_t *p = buf_start;
  bool isHuffman = *p & 0x80;
  uint32_t encoded_string_len = 0;
  int64_t len = 0;

  len = decode_integer(encoded_string_len, p, buf_end, 7);
  if (len == HPACK_ERROR_COMPRESSION_ERROR)
    return HPACK_ERROR_COMPRESSION_ERROR;
  p += len;

  if ((p + encoded_string_len) > buf_end) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  if (isHuffman) {
    // Allocate temporary area twice the size of before decoded data
    *str = arena.str_alloc(encoded_string_len * 2);

    len = huffman_decode(*str, p, encoded_string_len);
    if (len == HPACK_ERROR_COMPRESSION_ERROR)
      return HPACK_ERROR_COMPRESSION_ERROR;
    str_length = len;
  } else {
    *str = arena.str_alloc(encoded_string_len);

    memcpy(*str, reinterpret_cast<const char *>(p), encoded_string_len);

    str_length = encoded_string_len;
  }

  return p + encoded_string_len - buf_start;
}

//
// [RFC 7541] 6.1. Indexed Header Field Representation
//
int64_t
decode_indexed_header_field(MIMEFieldWrapper &header, const uint8_t *buf_start, const uint8_t *buf_end,
                            Http2IndexingTable &indexing_table)
{
  uint32_t index = 0;
  int64_t len = 0;

  len = decode_integer(index, buf_start, buf_end, 7);
  if (len == HPACK_ERROR_COMPRESSION_ERROR)
    return HPACK_ERROR_COMPRESSION_ERROR;

  if (indexing_table.get_header_field(index, header) == HPACK_ERROR_COMPRESSION_ERROR) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  if (is_debug_tag_set("http2_hpack_decode")) {
    int decoded_name_len;
    const char *decoded_name = header.name_get(&decoded_name_len);
    int decoded_value_len;
    const char *decoded_value = header.value_get(&decoded_value_len);

    Arena arena;
    Debug("http2_hpack_decode", "Decoded field: %s: %s", arena.str_store(decoded_name, decoded_name_len),
          arena.str_store(decoded_value, decoded_value_len));
  }

  return len;
}

//
// [RFC 7541] 6.2. Literal Header Field Representation
// Decode Literal Header Field Representation based on HpackFieldType
//
int64_t
decode_literal_header_field(MIMEFieldWrapper &header, const uint8_t *buf_start, const uint8_t *buf_end,
                            Http2IndexingTable &indexing_table)
{
  const uint8_t *p = buf_start;
  bool isIncremental = false;
  uint32_t index = 0;
  int64_t len = 0;
  HpackFieldType ftype = hpack_parse_field_type(*p);

  if (ftype == HPACK_FIELD_INDEXED_LITERAL) {
    len = decode_integer(index, p, buf_end, 6);
    isIncremental = true;
  } else if (ftype == HPACK_FIELD_NEVERINDEX_LITERAL) {
    len = decode_integer(index, p, buf_end, 4);
  } else {
    ink_assert(ftype == HPACK_FIELD_NOINDEX_LITERAL);
    len = decode_integer(index, p, buf_end, 4);
  }

  if (len == HPACK_ERROR_COMPRESSION_ERROR)
    return HPACK_ERROR_COMPRESSION_ERROR;

  p += len;

  Arena arena;

  // Decode header field name
  if (index) {
    indexing_table.get_header_field(index, header);
  } else {
    char *name_str = NULL;
    uint32_t name_str_len = 0;

    len = decode_string(arena, &name_str, name_str_len, p, buf_end);
    if (len == HPACK_ERROR_COMPRESSION_ERROR)
      return HPACK_ERROR_COMPRESSION_ERROR;

    // Check whether header field name is lower case
    for (uint32_t i = 0; i < name_str_len; i++) {
      if (ParseRules::is_upalpha(name_str[i])) {
        return -2;
      }
    }

    p += len;
    header.name_set(name_str, name_str_len);
  }

  // Decode header field value
  char *value_str = NULL;
  uint32_t value_str_len = 0;

  len = decode_string(arena, &value_str, value_str_len, p, buf_end);
  if (len == HPACK_ERROR_COMPRESSION_ERROR)
    return HPACK_ERROR_COMPRESSION_ERROR;

  p += len;
  header.value_set(value_str, value_str_len);

  // Incremental Indexing adds header to header table as new entry
  if (isIncremental) {
    indexing_table.add_header_field_to_dynamic_table(header.field_get());
  }

  // Print decoded header field
  if (is_debug_tag_set("http2_hpack_decode")) {
    int decoded_name_len;
    const char *decoded_name = header.name_get(&decoded_name_len);
    int decoded_value_len;
    const char *decoded_value = header.value_get(&decoded_value_len);

    Debug("http2_hpack_decode", "Decoded field: %s: %s", arena.str_store(decoded_name, decoded_name_len),
          arena.str_store(decoded_value, decoded_value_len));
  }

  return p - buf_start;
}

//
// [RFC 7541] 6.3. Dynamic Table Size Update
//
int64_t
update_dynamic_table_size(const uint8_t *buf_start, const uint8_t *buf_end, Http2IndexingTable &indexing_table)
{
  if (buf_start == buf_end)
    return HPACK_ERROR_COMPRESSION_ERROR;

  // Update header table size if its required.
  uint32_t size = 0;
  int64_t len = decode_integer(size, buf_start, buf_end, 5);
  if (len == HPACK_ERROR_COMPRESSION_ERROR)
    return HPACK_ERROR_COMPRESSION_ERROR;

  if (indexing_table.set_dynamic_table_size(size) == false) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  return len;
}
