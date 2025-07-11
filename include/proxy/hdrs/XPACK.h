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
  uint32_t index                                        = 0;
  enum class MatchType { NONE, NAME, EXACT } match_type = MatchType::NONE;
};

struct XpackDynamicTableEntry {
  uint32_t    index     = 0;
  uint32_t    offset    = 0;
  uint32_t    name_len  = 0;
  uint32_t    value_len = 0;
  uint32_t    ref_count = 0;
  const char *wks       = nullptr;
};

/** The memory containing the header fields. */
class XpackDynamicTableStorage
{
  friend class ExpandCapacityContext;

public:
  /** The storage for a dynamic table.
   *
   * @param[in] size The capacity of the table for header fields.
   */
  XpackDynamicTableStorage(uint32_t size);
  ~XpackDynamicTableStorage();

  /** Obtain the HTTP field name and value at @a offset bytes.
   *
   * @param[in] offset The offset from the start of the allocation from which to
   * obtain the header field.
   * @param[out] name A pointer to contain the name of the header field.
   * @param[out] name_len The length of the name.
   * @param[out] value A pointer to contain the value of the header field.
   * @param[out] value_len The length of the value.
   */
  void read(uint32_t offset, const char **name, uint32_t name_len, const char **value, uint32_t value_len) const;

  /** Writ the HTTP field at the head of the allocated data
   *
   * @param[in] name The HTTP field name to write.
   * @param[in] name_len The length of the name.
   * @param[in] value The HTTP field value to write.
   * @param[in] value_len The length of the value.
   *
   * @return The offset from the start of the allocation where the header field
   * was written.
   */
  uint32_t write(const char *name, uint32_t name_len, const char *value, uint32_t value_len);

  /** The amount of written bytes.
   *
   * The amount of written, unerased data. This is the difference between @a
   * _head and @a _tail.
   *
   * @return The number of written bytes.
   */
  uint32_t size() const;

private:
  /** Start expanding the capacity.
   *
   * Expanding the capacity is a two step process in which @a
   * _start_expanding_capacity is used to prepare for the expansion. This
   * populates @a _old_data with a pointer to the current @a _data pointer and
   * then allocates a new buffer per @a new_max_size and sets @a _data to that.
   * The caller then reinserts the current headers into the new buffer. Once
   * that is complete, the caller calls @a _finish_expanding_capacity to free
   * the old buffer.
   *
   * Handling these two phases should only be done by ExpandCapacityContext,
   * therefore these methods are private and only accessible to
   * ExpandCapacityContext via a friend relationship.
   *
   * @note XpackDynamicTableStorage only supports expanding the buffer. This
   * preserves offsets used by XpackDynamicTableEntry. Thus this function will
   * only return true when @a new_max_size is greater than the current capacity.
   *
   * The caller will need to reinsert all header fields after expanding the
   * capacity.
   *
   * @param[in] new_max_size The new maximum size of the table.
   * @return true if the capacity was expanded, false otherwise.
   */
  bool _start_expanding_capacity(uint32_t new_max_size);

  /** Finish expanding the capacity by freeing @a old_data. */
  void _finish_expanding_capacity();

private:
  /** The amount of space above @a size allocated as a buffer. */
  uint32_t _overwrite_threshold = 0;

  /** The space allocated and populated for the header fields. */
  uint8_t *_data = nullptr;

  /** When in an expansion phase, this points to the old memory.
   *
   * See the documentation in @a _start_expanding_capacity.
   */
  uint8_t *_old_data = nullptr;

  /** The size of allocated space for @a data.
   *
   * This is set to twice the requested space provided as @a size to the
   * constructor. This is done to avoid buffer wrapping.
   */
  uint32_t _capacity = 0;

  /** A pointer to the last byte written.
   *
   * @a _head is initialized to the last allocated byte. As header field data is
   * populated in the allocated space, this is advanced to the last byte
   * written. Thus the next write will start at the byte just after @a _head.
   */
  uint32_t _head = 0;
};

/** Define a context for expanding XpackDynamicTableStorage.
 *
 * Construction and destruction starts the expansion and finishes it, respectively.
 */
class ExpandCapacityContext
{
public:
  /** Begin the storage expansion phase to the @a new_max_size. */
  ExpandCapacityContext(XpackDynamicTableStorage &storage, uint32_t new_max_size) : _storage{storage}
  {
    this->_ok_to_expand = this->_storage._start_expanding_capacity(new_max_size);
  }
  /** End the storage expansion phase, cleaning up the old storage memory. */
  ~ExpandCapacityContext() { this->_storage._finish_expanding_capacity(); }

  // No copying or moving.
  ExpandCapacityContext(const ExpandCapacityContext &)            = delete;
  ExpandCapacityContext &operator=(const ExpandCapacityContext &) = delete;
  ExpandCapacityContext(ExpandCapacityContext &&)                 = delete;
  ExpandCapacityContext &operator=(ExpandCapacityContext &&)      = delete;

  /** Copy the field data from the old memory to the new one.
   * @param[in] old_offset The offset of data in the old memory.
   * @param[in] len The length of data to copy.
   * @return The offset of the copied data in the new memory.
   */
  uint32_t copy_field(uint32_t old_offset, uint32_t len);

  /** Indicate whether the expansion should proceed.
   * Return whether @a _storage indicates that the new max size is valid for
   * expanding the storage. If not, the expansion should not proceed.
   *
   * @return true if the new size is valid, false otherwise.
   */
  bool
  ok_to_expand() const
  {
    return this->_ok_to_expand;
  }

private:
  XpackDynamicTableStorage &_storage;
  bool                      _ok_to_expand = false;
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
  bool                    should_duplicate(uint32_t index);
  bool                    update_maximum_size(uint32_t max_size);
  uint32_t                size() const;
  uint32_t                maximum_size() const;
  void                    ref_entry(uint32_t index);
  void                    unref_entry(uint32_t index);
  bool                    is_empty() const;
  uint32_t                largest_index() const;
  uint32_t                count() const;

private:
  static constexpr uint8_t ADDITIONAL_32_BYTES = 32;
  uint32_t                 _maximum_size       = 0;
  uint32_t                 _available          = 0;
  uint32_t                 _entries_inserted   = 0;

  struct XpackDynamicTableEntry *_entries      = nullptr;
  uint32_t                       _max_entries  = 0;
  uint32_t                       _entries_head = 0;
  uint32_t                       _entries_tail = 0;
  XpackDynamicTableStorage       _storage;

  /** Expand @a _storage to the new size.
   *
   * This takes care of expanding @a _storage's size and handles updating the
   * new offsets for each entry that this expansion requires.
   *
   * @param[in] new_storage_size The new size to expand @a _storage to.
   */
  void _expand_storage_size(uint32_t new_storage_size);

  /** Evict entries to obtain the extra space needed.
   *
   * The type of reuired_size is uint64 so that we can handle a size that is bigger than the table capacity.
   * Passing a value more than UINT32_MAX evicts every entry and returns false.
   *
   * @param[in] extra_space_needed The amount of space needed to be freed.
   * @return true if the required space was freed, false otherwise.
   */
  bool _make_space(uint64_t extra_space_needed);

  /** Calcurates the index number for _entries, which is a kind of circular buffer.
   *
   * @param[in] base The place to start indexing from. Passing @a _tail
   * references the start of the buffer, while @a _head references the end of
   * the buffer.
   *
   * @param[in] offset The offset from the base. A value of 1 means the first
   * entry from @a base. Thus a value of @a _tail for @a base and 1 for @a
   * offset references the first entry in the buffer.
   */
  uint32_t _calc_index(uint32_t base, int64_t offset) const;
};
