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

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/Diags.h"
#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/XPACK.h"

#include <deque>
#include <string_view>

// It means that any header field can be compressed/decompressed by ATS
const static int HPACK_ERROR_COMPRESSION_ERROR   = -1;
const static int HPACK_ERROR_SIZE_EXCEEDED_ERROR = -2;

enum class HpackField {
  INDEX,              // [RFC 7541] 6.1. Indexed Header Field Representation
  INDEXED_LITERAL,    // [RFC 7541] 6.2.1. Literal Header Field with Incremental Indexing
  NOINDEX_LITERAL,    // [RFC 7541] 6.2.2. Literal Header Field without Indexing
  NEVERINDEX_LITERAL, // [RFC 7541] 6.2.3. Literal Header Field never Indexed
  TABLESIZE_UPDATE,   // [RFC 7541] 6.3. Dynamic Table Size Update
};

enum class HpackIndex {
  NONE,
  STATIC,
  DYNAMIC,
};

enum class HpackMatch {
  NONE,
  NAME,
  EXACT,
};

// Result of looking for a header field in IndexingTable
struct HpackLookupResult {
  uint32_t   index      = 0;
  HpackIndex index_type = HpackIndex::NONE;
  HpackMatch match_type = HpackMatch::NONE;
};

struct HpackHeaderField {
  std::string_view name;
  std::string_view value;
};

class MIMEFieldWrapper
{
public:
  MIMEFieldWrapper(MIMEField *f, HdrHeap *hh, MIMEHdrImpl *impl) : _field(f), _heap(hh), _mh(impl) {}
  void
  name_set(const char *name, int name_len)
  {
    _field->name_set(_heap, _mh, std::string_view{name, static_cast<std::string_view::size_type>(name_len)});
  }

  void
  value_set(const char *value, int value_len)
  {
    _field->value_set(_heap, _mh, std::string_view{value, static_cast<std::string_view::size_type>(value_len)});
  }

  std::string_view
  name_get() const
  {
    return _field->name_get();
  }

  std::string_view
  value_get() const
  {
    return _field->value_get();
  }

  const MIMEField *
  field_get() const
  {
    return _field;
  }

private:
  MIMEField   *_field;
  HdrHeap     *_heap;
  MIMEHdrImpl *_mh;
};

// [RFC 7541] 2.3. Indexing Table
class HpackIndexingTable
{
public:
  explicit HpackIndexingTable(uint32_t size) : _dynamic_table(size){};
  ~HpackIndexingTable() {}

  // noncopyable
  HpackIndexingTable(HpackIndexingTable &)                  = delete;
  HpackIndexingTable &operator=(const HpackIndexingTable &) = delete;

  HpackLookupResult lookup(const HpackHeaderField &header) const;
  int               get_header_field(uint32_t index, MIMEFieldWrapper &header_field) const;

  void     add_header_field(const HpackHeaderField &header);
  uint32_t maximum_size() const;
  uint32_t size() const;
  void     update_maximum_size(uint32_t new_size);

  // Temporal buffer for internal use but it has to be public because many functions are not members of this class.
  Arena arena;

private:
  XpackDynamicTable _dynamic_table;
};

// Low level interfaces
int64_t encode_indexed_header_field(uint8_t *buf_start, const uint8_t *buf_end, uint32_t index);
int64_t encode_literal_header_field_with_indexed_name(uint8_t *buf_start, const uint8_t *buf_end, const HpackHeaderField &header,
                                                      uint32_t index, HpackIndexingTable &indexing_table, HpackField type);
int64_t encode_literal_header_field_with_new_name(uint8_t *buf_start, const uint8_t *buf_end, const HpackHeaderField &header,
                                                  HpackIndexingTable &indexing_table, HpackField type);

int64_t decode_indexed_header_field(MIMEFieldWrapper &header, const uint8_t *buf_start, const uint8_t *buf_end,
                                    HpackIndexingTable &indexing_table);
int64_t decode_literal_header_field(MIMEFieldWrapper &header, const uint8_t *buf_start, const uint8_t *buf_end,
                                    HpackIndexingTable &indexing_table);
int64_t update_dynamic_table_size(const uint8_t *buf_start, const uint8_t *buf_end, HpackIndexingTable &indexing_table,
                                  uint32_t maximum_table_size);

// High level interfaces
using HpackHandle = HpackIndexingTable;
int64_t hpack_decode_header_block(HpackHandle &handle, HTTPHdr *hdr, const uint8_t *in_buf, const size_t in_buf_len,
                                  uint32_t max_header_size, uint32_t maximum_table_size);
int64_t hpack_encode_header_block(HpackHandle &handle, uint8_t *out_buf, const size_t out_buf_len, HTTPHdr *hdr,
                                  int32_t maximum_table_size = -1);
int32_t hpack_get_maximum_table_size(HpackHandle &handle);
