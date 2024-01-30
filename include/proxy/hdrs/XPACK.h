/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <cstdint>
#include <string_view>
#include "tscore/Arena.h"

const static int XPACK_ERROR_COMPRESSION_ERROR   = -1;
const static int XPACK_ERROR_SIZE_EXCEEDED_ERROR = -2;

// These primitives are shared with HPACK and QPACK.
int64_t xpack_encode_integer(uint8_t *buf_start, const uint8_t *buf_end, uint64_t value, uint8_t n);
int64_t xpack_decode_integer(uint64_t &dst, const uint8_t *buf_start, const uint8_t *buf_end, uint8_t n);
int64_t xpack_encode_string(uint8_t *buf_start, const uint8_t *buf_end, const char *value, uint64_t value_len, uint8_t n = 7);
int64_t xpack_decode_string(Arena &arena, char **str, uint64_t &str_length, const uint8_t *buf_start, const uint8_t *buf_end,
                            uint8_t n = 7);

struct XpackLookupResult {
  uint32_t index                                  = 0;
  enum MatchType { NONE, NAME, EXACT } match_type = MatchType::NONE;
};

struct XpackDynamicTableEntry {
  uint32_t index     = 0;
  uint32_t offset    = 0;
  uint32_t name_len  = 0;
  uint32_t value_len = 0;
  uint32_t ref_count = 0;
  const char *wks    = nullptr;
};

class XpackDynamicTableStorage
{
public:
  XpackDynamicTableStorage(uint32_t size);
  ~XpackDynamicTableStorage();
  void read(uint32_t offset, const char **name, uint32_t name_len, const char **value, uint32_t value_len) const;
  uint32_t write(const char *name, uint32_t name_len, const char *value, uint32_t value_len);
  void erase(uint32_t name_len, uint32_t value_len);

private:
  uint32_t _overwrite_threshold = 0;
  uint8_t *_data                = nullptr;
  uint32_t _data_size           = 0;
  uint32_t _head                = 0;
  uint32_t _tail                = 0;
};

class XpackDynamicTable
{
public:
  XpackDynamicTable(uint32_t size);
  ~XpackDynamicTable();

  const XpackLookupResult lookup(uint32_t absolute_index, const char **name, size_t *name_len, const char **value,
                                 size_t *value_len) const;
  const XpackLookupResult lookup(const char *name, size_t name_len, const char *value, size_t value_len) const;
  const XpackLookupResult lookup(const std::string_view name, const std::string_view value) const;
  const XpackLookupResult lookup_relative(uint32_t relative_index, const char **name, size_t *name_len, const char **value,
                                          size_t *value_len) const;
  const XpackLookupResult lookup_relative(const char *name, size_t name_len, const char *value, size_t value_len) const;
  const XpackLookupResult lookup_relative(const std::string_view name, const std::string_view value) const;
  const XpackLookupResult insert_entry(const char *name, size_t name_len, const char *value, size_t value_len);
  const XpackLookupResult insert_entry(const std::string_view name, const std::string_view value);
  const XpackLookupResult duplicate_entry(uint32_t current_index);
  bool should_duplicate(uint32_t index);
  bool update_maximum_size(uint32_t max_size);
  uint32_t size() const;
  uint32_t maximum_size() const;
  void ref_entry(uint32_t index);
  void unref_entry(uint32_t index);
  bool is_empty() const;
  uint32_t largest_index() const;
  uint32_t count() const;

private:
  static constexpr uint8_t ADDITIONAL_32_BYTES = 32;
  uint32_t _maximum_size                       = 0;
  uint32_t _available                          = 0;
  uint32_t _entries_inserted                   = 0;

  struct XpackDynamicTableEntry *_entries = nullptr;
  uint32_t _max_entries                   = 0;
  uint32_t _entries_head                  = 0;
  uint32_t _entries_tail                  = 0;
  XpackDynamicTableStorage _storage;

  /**
   * The type of reuired_size is uint64 so that we can handle a size that is begger than the table capacity.
   * Passing a value more than UINT32_MAX evicts every entry and return false.
   */
  bool _make_space(uint64_t required_size);
};
