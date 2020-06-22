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

struct StaticTable {
  StaticTable(const char *n, const char *v) : name(n), value(v), name_size(strlen(name)), value_size(strlen(value)) {}
  const char *name;
  const char *value;
  const int name_size;
  const int value_size;
};

static const StaticTable STATIC_TABLE[] = {{"", ""},
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

/**
  Threshold for total HdrHeap size which used by HPAK Dynamic Table.
  The HdrHeap is filled by MIMEHdrImpl and MIMEFieldBlockImpl like below.
  This threshold allow to allocate 3 HdrHeap at maximum.

                     +------------------+-----------------------------+
   HdrHeap 1 (2048): | MIMEHdrImpl(592) | MIMEFieldBlockImpl(528) x 2 |
                     +------------------+-----------------------------+--...--+
   HdrHeap 2 (4096): | MIMEFieldBlockImpl(528) x 7                            |
                     +------------------------------------------------+--...--+--...--+
   HdrHeap 3 (8192): | MIMEFieldBlockImpl(528) x 15                                   |
                     +------------------------------------------------+--...--+--...--+
*/
static constexpr uint32_t HPACK_HDR_HEAP_THRESHOLD = sizeof(MIMEHdrImpl) + sizeof(MIMEFieldBlockImpl) * (2 + 7 + 15);

/******************
 * Local functions
 ******************/
static inline bool
hpack_field_is_literal(HpackField ftype)
{
  return ftype == HpackField::INDEXED_LITERAL || ftype == HpackField::NOINDEX_LITERAL || ftype == HpackField::NEVERINDEX_LITERAL;
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

/************************
 * HpackIndexingTable
 ************************/
HpackLookupResult
HpackIndexingTable::lookup(const MIMEFieldWrapper &field) const
{
  int target_name_len = 0, target_value_len = 0;
  const char *target_name  = field.name_get(&target_name_len);
  const char *target_value = field.value_get(&target_value_len);
  return lookup(target_name, target_name_len, target_value, target_value_len);
}

HpackLookupResult
HpackIndexingTable::lookup(const char *name, int name_len, const char *value, int value_len) const
{
  HpackLookupResult result;
  const unsigned int entry_num = TS_HPACK_STATIC_TABLE_ENTRY_NUM + _dynamic_table->length();

  for (unsigned int index = 1; index < entry_num; ++index) {
    const char *table_name, *table_value;
    int table_name_len = 0, table_value_len = 0;

    if (index < TS_HPACK_STATIC_TABLE_ENTRY_NUM) {
      // static table
      table_name      = STATIC_TABLE[index].name;
      table_value     = STATIC_TABLE[index].value;
      table_name_len  = STATIC_TABLE[index].name_size;
      table_value_len = STATIC_TABLE[index].value_size;
    } else {
      // dynamic table
      const MIMEField *m_field = _dynamic_table->get_header_field(index - TS_HPACK_STATIC_TABLE_ENTRY_NUM);

      table_name  = m_field->name_get(&table_name_len);
      table_value = m_field->value_get(&table_value_len);
    }

    // Check whether name (and value) are matched
    if (ptr_len_casecmp(name, name_len, table_name, table_name_len) == 0) {
      if ((value_len == table_value_len) && (memcmp(value, table_value, value_len) == 0)) {
        result.index      = index;
        result.match_type = HpackMatch::EXACT;
        break;
      } else if (!result.index) {
        result.index      = index;
        result.match_type = HpackMatch::NAME;
      }
    }
  }
  if (result.match_type != HpackMatch::NONE) {
    if (result.index < TS_HPACK_STATIC_TABLE_ENTRY_NUM) {
      result.index_type = HpackIndex::STATIC;
    } else {
      result.index_type = HpackIndex::DYNAMIC;
    }
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
    field.name_set(STATIC_TABLE[index].name, STATIC_TABLE[index].name_size);
    field.value_set(STATIC_TABLE[index].value, STATIC_TABLE[index].value_size);
  } else if (index < TS_HPACK_STATIC_TABLE_ENTRY_NUM + _dynamic_table->length()) {
    // dynamic table
    const MIMEField *m_field = _dynamic_table->get_header_field(index - TS_HPACK_STATIC_TABLE_ENTRY_NUM);

    int name_len, value_len;
    const char *name  = m_field->name_get(&name_len);
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
HpackIndexingTable::add_header_field(const MIMEField *field)
{
  _dynamic_table->add_header_field(field);
}

uint32_t
HpackIndexingTable::maximum_size() const
{
  return _dynamic_table->maximum_size();
}

uint32_t
HpackIndexingTable::size() const
{
  return _dynamic_table->size();
}

void
HpackIndexingTable::update_maximum_size(uint32_t new_size)
{
  _dynamic_table->update_maximum_size(new_size);
}

//
// HpackDynamicTable
//
HpackDynamicTable::~HpackDynamicTable()
{
  this->_headers.clear();

  this->_mhdr->fields_clear();
  this->_mhdr->destroy();
  delete this->_mhdr;

  if (this->_mhdr_old != nullptr) {
    this->_mhdr_old->fields_clear();
    this->_mhdr_old->destroy();
    delete this->_mhdr_old;
  }
}

const MIMEField *
HpackDynamicTable::get_header_field(uint32_t index) const
{
  return this->_headers.at(index);
}

void
HpackDynamicTable::add_header_field(const MIMEField *field)
{
  int name_len, value_len;
  const char *name     = field->name_get(&name_len);
  const char *value    = field->value_get(&value_len);
  uint32_t header_size = ADDITIONAL_OCTETS + name_len + value_len;

  if (header_size > _maximum_size) {
    // [RFC 7541] 4.4. Entry Eviction When Adding New Entries
    // It is not an error to attempt to add an entry that is larger than
    // the maximum size; an attempt to add an entry larger than the entire
    // table causes the table to be emptied of all existing entries.
    this->_headers.clear();
    this->_mhdr->fields_clear();

    if (this->_mhdr_old) {
      this->_mhdr_old->fields_clear();
      this->_mhdr_old->destroy();
      delete this->_mhdr_old;
      this->_mhdr_old = nullptr;
    }

    this->_current_size = 0;
  } else {
    this->_current_size += header_size;
    this->_evict_overflowed_entries();

    MIMEField *new_field = this->_mhdr->field_create(name, name_len);
    new_field->value_set(this->_mhdr->m_heap, this->_mhdr->m_mime, value, value_len);
    this->_mhdr->field_attach(new_field);
    this->_headers.push_front(new_field);
  }
}

uint32_t
HpackDynamicTable::maximum_size() const
{
  return _maximum_size;
}

uint32_t
HpackDynamicTable::size() const
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
void
HpackDynamicTable::update_maximum_size(uint32_t new_size)
{
  this->_maximum_size = new_size;
  this->_evict_overflowed_entries();
}

uint32_t
HpackDynamicTable::length() const
{
  return this->_headers.size();
}

void
HpackDynamicTable::_evict_overflowed_entries()
{
  if (this->_current_size <= this->_maximum_size) {
    // Do nothing
    return;
  }

  for (auto h = this->_headers.rbegin(); h != this->_headers.rend(); ++h) {
    int name_len, value_len;
    (*h)->name_get(&name_len);
    (*h)->value_get(&value_len);

    this->_current_size -= ADDITIONAL_OCTETS + name_len + value_len;

    if (this->_mhdr_old && this->_mhdr_old->fields_count() != 0) {
      this->_mhdr_old->field_delete(*h, false);
    } else {
      this->_mhdr->field_delete(*h, false);
    }

    this->_headers.pop_back();

    if (this->_current_size <= this->_maximum_size) {
      break;
    }
  }

  this->_mime_hdr_gc();
}

/**
   When HdrHeap size of current MIMEHdr exceeds the threshold, allocate new MIMEHdr and HdrHeap.
   The old MIMEHdr and HdrHeap will be freed, when all MIMEFiled are deleted by HPACK Entry Eviction.
 */
void
HpackDynamicTable::_mime_hdr_gc()
{
  if (this->_mhdr_old == nullptr) {
    if (this->_mhdr->m_heap->total_used_size() >= HPACK_HDR_HEAP_THRESHOLD) {
      this->_mhdr_old = this->_mhdr;
      this->_mhdr     = new MIMEHdr();
      this->_mhdr->create();
    }
  } else {
    if (this->_mhdr_old->fields_count() == 0) {
      this->_mhdr_old->destroy();
      delete this->_mhdr_old;
      this->_mhdr_old = nullptr;
    }
  }
}

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
  p += len;

  Debug("hpack_encode", "Encoded field: %d", index);
  return p - buf_start;
}

int64_t
encode_literal_header_field_with_indexed_name(uint8_t *buf_start, const uint8_t *buf_end, const MIMEFieldWrapper &header,
                                              uint32_t index, HpackIndexingTable &indexing_table, HpackField type)
{
  uint8_t *p = buf_start;
  int64_t len;
  uint8_t prefix = 0, flag = 0;

  ink_assert(hpack_field_is_literal(type));

  switch (type) {
  case HpackField::INDEXED_LITERAL:
    indexing_table.add_header_field(header.field_get());
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
  p += len;

  // Value String
  int value_len;
  const char *value = header.value_get(&value_len);
  len               = xpack_encode_string(p, buf_end, value, value_len);
  if (len == -1) {
    return -1;
  }
  p += len;

  Debug("hpack_encode", "Encoded field: %d: %.*s", index, value_len, value);
  return p - buf_start;
}

int64_t
encode_literal_header_field_with_new_name(uint8_t *buf_start, const uint8_t *buf_end, const MIMEFieldWrapper &header,
                                          HpackIndexingTable &indexing_table, HpackField type)
{
  uint8_t *p = buf_start;
  int64_t len;
  uint8_t flag = 0;

  ink_assert(hpack_field_is_literal(type));

  switch (type) {
  case HpackField::INDEXED_LITERAL:
    indexing_table.add_header_field(header.field_get());
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

  // Convert field name to lower case to follow HTTP2 spec.
  // This conversion is needed because WKSs in MIMEFields is old fashioned
  Arena arena;
  int name_len;
  const char *name = header.name_get(&name_len);
  char *lower_name = arena.str_store(name, name_len);
  for (int i = 0; i < name_len; i++) {
    lower_name[i] = ParseRules::ink_tolower(lower_name[i]);
  }

  // Name String
  len = xpack_encode_string(p, buf_end, lower_name, name_len);
  if (len == -1) {
    return -1;
  }
  p += len;

  // Value String
  int value_len;
  const char *value = header.value_get(&value_len);
  len               = xpack_encode_string(p, buf_end, value, value_len);
  if (len == -1) {
    return -1;
  }

  p += len;

  Debug("hpack_encode", "Encoded field: %.*s: %.*s", name_len, name, value_len, value);
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
  int64_t len    = 0;

  len = xpack_decode_integer(index, buf_start, buf_end, 7);
  if (len == XPACK_ERROR_COMPRESSION_ERROR) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  if (indexing_table.get_header_field(index, header) == HPACK_ERROR_COMPRESSION_ERROR) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  if (is_debug_tag_set("hpack_decode")) {
    int decoded_name_len;
    const char *decoded_name = header.name_get(&decoded_name_len);
    int decoded_value_len;
    const char *decoded_value = header.value_get(&decoded_value_len);

    Arena arena;
    Debug("hpack_decode", "Decoded field: %s: %s", arena.str_store(decoded_name, decoded_name_len),
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
                            HpackIndexingTable &indexing_table)
{
  const uint8_t *p         = buf_start;
  bool isIncremental       = false;
  uint64_t index           = 0;
  int64_t len              = 0;
  HpackField ftype         = hpack_parse_field_type(*p);
  bool has_http2_violation = false;

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

  Arena arena;

  // Decode header field name
  if (index) {
    if (indexing_table.get_header_field(index, header) == HPACK_ERROR_COMPRESSION_ERROR) {
      return HPACK_ERROR_COMPRESSION_ERROR;
    }
  } else {
    char *name_str        = nullptr;
    uint64_t name_str_len = 0;

    len = xpack_decode_string(arena, &name_str, name_str_len, p, buf_end);
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
  }

  // Decode header field value
  char *value_str        = nullptr;
  uint64_t value_str_len = 0;

  len = xpack_decode_string(arena, &value_str, value_str_len, p, buf_end);
  if (len == XPACK_ERROR_COMPRESSION_ERROR) {
    return HPACK_ERROR_COMPRESSION_ERROR;
  }

  p += len;
  header.value_set(value_str, value_str_len);

  // Incremental Indexing adds header to header table as new entry
  if (isIncremental) {
    indexing_table.add_header_field(header.field_get());
  }

  // Print decoded header field
  if (is_debug_tag_set("hpack_decode")) {
    int decoded_name_len;
    const char *decoded_name = header.name_get(&decoded_name_len);
    int decoded_value_len;
    const char *decoded_value = header.value_get(&decoded_value_len);

    Debug("hpack_decode", "Decoded field: %s: %s", arena.str_store(decoded_name, decoded_name_len),
          arena.str_store(decoded_value, decoded_value_len));
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
  int64_t len   = xpack_decode_integer(size, buf_start, buf_end, 5);
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
  const uint8_t *cursor           = in_buf;
  const uint8_t *const in_buf_end = in_buf + in_buf_len;
  HdrHeap *heap                   = hdr->m_heap;
  HTTPHdrImpl *hh                 = hdr->m_http;
  bool header_field_started       = false;
  bool has_http2_violation        = false;
  uint32_t total_header_size      = 0;

  while (cursor < in_buf_end) {
    int64_t read_bytes = 0;

    // decode a header field encoded by HPACK
    MIMEField *field = mime_field_create(heap, hh->m_fields_impl);
    MIMEFieldWrapper header(field, heap, hh->m_fields_impl);
    HpackField ftype = hpack_parse_field_type(*cursor);

    switch (ftype) {
    case HpackField::INDEX:
      read_bytes = decode_indexed_header_field(header, cursor, in_buf_end, indexing_table);
      if (read_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
        return HPACK_ERROR_COMPRESSION_ERROR;
      }
      cursor += read_bytes;
      header_field_started = true;
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
      cursor += read_bytes;
      header_field_started = true;
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

    int name_len  = 0;
    int value_len = 0;

    field->name_get(&name_len);
    field->value_get(&value_len);

    // [RFC 7540] 6.5.2. SETTINGS_MAX_HEADER_LIST_SIZE:
    // The value is based on the uncompressed size of header fields, including the length of the name and value in octets plus an
    // overhead of 32 octets for each header field.
    total_header_size += name_len + value_len + ADDITIONAL_OCTETS;

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
  uint8_t *cursor                  = out_buf;
  const uint8_t *const out_buf_end = out_buf + out_buf_len;
  int64_t written                  = 0;

  ink_assert(http_hdr_type_get(hdr->m_http) != HTTP_TYPE_UNKNOWN);

  // Update dynamic table size
  if (maximum_table_size >= 0) {
    indexing_table.update_maximum_size(maximum_table_size);
    written = encode_dynamic_table_size_update(cursor, out_buf_end, maximum_table_size);
    if (written == HPACK_ERROR_COMPRESSION_ERROR) {
      return HPACK_ERROR_COMPRESSION_ERROR;
    }
    cursor += written;
  }

  MIMEFieldIter field_iter;
  for (MIMEField *field = hdr->iter_get_first(&field_iter); field != nullptr; field = hdr->iter_get_next(&field_iter)) {
    HpackField field_type;
    MIMEFieldWrapper header(field, hdr->m_heap, hdr->m_http->m_fields_impl);
    int name_len;
    int value_len;
    const char *name = header.name_get(&name_len);
    header.value_get(&value_len);
    // Choose field representation (See RFC7541 7.1.3)
    // - Authorization header obviously should not be indexed
    // - Short Cookie header should not be indexed because of low entropy
    if ((ptr_len_casecmp(name, name_len, MIME_FIELD_COOKIE, MIME_LEN_COOKIE) == 0 && value_len < 20) ||
        (ptr_len_casecmp(name, name_len, MIME_FIELD_AUTHORIZATION, MIME_LEN_AUTHORIZATION) == 0)) {
      field_type = HpackField::NEVERINDEX_LITERAL;
    } else {
      field_type = HpackField::INDEXED_LITERAL;
    }
    const HpackLookupResult result = indexing_table.lookup(header);
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
      // Does it happen?
      written = 0;
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
