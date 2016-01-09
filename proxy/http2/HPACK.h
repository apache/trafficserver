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

#ifndef __HPACK_H__
#define __HPACK_H__

#include "ts/ink_platform.h"
#include "ts/Vec.h"
#include "ts/Diags.h"
#include "HTTP.h"

// Constant strings for pseudo headers of HPACK
extern const char *HPACK_VALUE_SCHEME;
extern const char *HPACK_VALUE_METHOD;
extern const char *HPACK_VALUE_AUTHORITY;
extern const char *HPACK_VALUE_PATH;
extern const char *HPACK_VALUE_STATUS;

extern const unsigned HPACK_LEN_SCHEME;
extern const unsigned HPACK_LEN_METHOD;
extern const unsigned HPACK_LEN_AUTHORITY;
extern const unsigned HPACK_LEN_PATH;
extern const unsigned HPACK_LEN_STATUS;

// It means that any header field can be compressed/decompressed by ATS
const static int HPACK_ERROR_COMPRESSION_ERROR = -1;
// It means that any header field is invalid in HTTP/2 spec
const static int HPACK_ERROR_HTTP2_PROTOCOL_ERROR = -2;

enum HpackFieldType {
  HPACK_FIELD_INDEX,              // [RFC 7541] 6.1. Indexed Header Field Representation
  HPACK_FIELD_INDEXED_LITERAL,    // [RFC 7541] 6.2.1. Literal Header Field with Incremental Indexing
  HPACK_FIELD_NOINDEX_LITERAL,    // [RFC 7541] 6.2.2. Literal Header Field without Indexing
  HPACK_FIELD_NEVERINDEX_LITERAL, // [RFC 7541] 6.2.3. Literal Header Field never Indexed
  HPACK_FIELD_TABLESIZE_UPDATE,   // [RFC 7541] 6.3. Dynamic Table Size Update
};

class MIMEFieldWrapper
{
public:
  MIMEFieldWrapper(MIMEField *f, HdrHeap *hh, MIMEHdrImpl *impl) : _field(f), _heap(hh), _mh(impl) {}

  void
  name_set(const char *name, int name_len)
  {
    _field->name_set(_heap, _mh, name, name_len);
  }

  void
  value_set(const char *value, int value_len)
  {
    _field->value_set(_heap, _mh, value, value_len);
  }

  const char *
  name_get(int *length) const
  {
    return _field->name_get(length);
  }

  const char *
  value_get(int *length) const
  {
    return _field->value_get(length);
  }

  const MIMEField *
  field_get() const
  {
    return _field;
  }

private:
  MIMEField *_field;
  HdrHeap *_heap;
  MIMEHdrImpl *_mh;
};

// Result of looking for a header field in IndexingTable
struct Http2LookupIndexResult {
  Http2LookupIndexResult() : index(0), value_is_indexed(false) {}

  int index;
  bool value_is_indexed;
};

// [RFC 7541] 2.3.2. Dynamic Table
class Http2DynamicTable
{
public:
  Http2DynamicTable() : _current_size(0), _settings_dynamic_table_size(4096)
  {
    _mhdr = new MIMEHdr();
    _mhdr->create();
  }

  ~Http2DynamicTable()
  {
    _headers.clear();
    _mhdr->fields_clear();
    _mhdr->destroy();
    delete _mhdr;
  }

  const MIMEField *get_header_field(uint32_t index) const;
  void add_header_field(const MIMEField *field);

  uint32_t get_size() const;
  bool set_size(uint32_t new_size);

  const uint32_t get_current_entry_num() const;

  // For regression test
  bool is_header_in(const char *target_name, const char *target_value) const;

private:
  uint32_t _current_size;
  uint32_t _settings_dynamic_table_size;

  MIMEHdr *_mhdr;
  Vec<MIMEField *> _headers;
};


// [RFC 7541] 2.3. Indexing Table
class Http2IndexingTable
{
public:
  Http2LookupIndexResult get_index(const MIMEFieldWrapper &field) const;
  int get_header_field(uint32_t index, MIMEFieldWrapper &header_field) const;

  void add_header_field_to_dynamic_table(const MIMEField *field);
  uint32_t get_dynamic_table_size() const;
  bool set_dynamic_table_size(uint32_t new_size);
  bool is_header_in_dynamic_table(const char *target_name, const char *target_value) const;

private:
  Http2DynamicTable _dynamic_table;
};

HpackFieldType hpack_parse_field_type(uint8_t ftype);

static inline bool
hpack_field_is_literal(HpackFieldType ftype)
{
  return ftype == HPACK_FIELD_INDEXED_LITERAL || ftype == HPACK_FIELD_NOINDEX_LITERAL || ftype == HPACK_FIELD_NEVERINDEX_LITERAL;
}

int64_t encode_integer(uint8_t *buf_start, const uint8_t *buf_end, uint32_t value, uint8_t n);
int64_t decode_integer(uint32_t &dst, const uint8_t *buf_start, const uint8_t *buf_end, uint8_t n);
int64_t encode_string(uint8_t *buf_start, const uint8_t *buf_end, const char *value, size_t value_len);
int64_t decode_string(Arena &arena, char **str, uint32_t &str_length, const uint8_t *buf_start, const uint8_t *buf_end);

int64_t encode_indexed_header_field(uint8_t *buf_start, const uint8_t *buf_end, uint32_t index);
int64_t encode_literal_header_field_with_indexed_name(uint8_t *buf_start, const uint8_t *buf_end, const MIMEFieldWrapper &header,
                                                      uint32_t index, Http2IndexingTable &indexing_table, HpackFieldType type);
int64_t encode_literal_header_field_with_new_name(uint8_t *buf_start, const uint8_t *buf_end, const MIMEFieldWrapper &header,
                                                  Http2IndexingTable &indexing_table, HpackFieldType type);

// When these functions returns minus value, any error occurs
// TODO Separate error code and length of processed buffer
int64_t decode_indexed_header_field(MIMEFieldWrapper &header, const uint8_t *buf_start, const uint8_t *buf_end,
                                    Http2IndexingTable &indexing_table);
int64_t decode_literal_header_field(MIMEFieldWrapper &header, const uint8_t *buf_start, const uint8_t *buf_end,
                                    Http2IndexingTable &indexing_table);
int64_t update_dynamic_table_size(const uint8_t *buf_start, const uint8_t *buf_end, Http2IndexingTable &indexing_table);

#endif /* __HPACK_H__ */
