/** @file

  Common functions for HPACK and QPACK

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

#include "proxy/hdrs/XPACK.h"
#include "proxy/hdrs/HuffmanCodec.h"
#include "proxy/hdrs/HdrToken.h"

#include "tscore/Arena.h"
#include "tscore/Diags.h"
#include "tscore/ink_memory.h"
#include "tsutil/LocalBuffer.h"
#include <cstdint>

namespace
{
DbgCtl dbg_ctl_xpack{"xpack"};

#define XPACKDbg(fmt, ...) Dbg(dbg_ctl_xpack, fmt, ##__VA_ARGS__)

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

} // end anonymous namespace

//
// [RFC 7541] 5.1. Integer representation
//
int64_t
xpack_decode_integer(uint64_t &dst, const uint8_t *buf_start, const uint8_t *buf_end, uint8_t n)
{
  if (buf_start >= buf_end) {
    return XPACK_ERROR_COMPRESSION_ERROR;
  }

  const uint8_t *p = buf_start;

  dst = (*p & ((1 << n) - 1));
  if (dst == static_cast<uint64_t>(1 << n) - 1) {
    int m = 0;
    do {
      if (++p >= buf_end) {
        return XPACK_ERROR_COMPRESSION_ERROR;
      }

      uint64_t added_value = *p & 0x7f;
      if ((UINT64_MAX >> m) < added_value) {
        // Excessively large integer encodings - in value or octet
        // length - MUST be treated as a decoding error.
        return XPACK_ERROR_COMPRESSION_ERROR;
      }
      dst += added_value << m;
      m   += 7;
    } while (*p & 0x80);
  }

  return p - buf_start + 1;
}

//
// [RFC 7541] 5.2. String Literal Representation
// return content from String Data (Length octets) with huffman decoding if it is encoded
//
int64_t
xpack_decode_string(Arena &arena, char **str, uint64_t &str_length, const uint8_t *buf_start, const uint8_t *buf_end, uint8_t n)
{
  if (buf_start >= buf_end) {
    return XPACK_ERROR_COMPRESSION_ERROR;
  }

  const uint8_t *p                  = buf_start;
  bool           isHuffman          = *p & (0x01 << n);
  uint64_t       encoded_string_len = 0;
  int64_t        len                = 0;

  len = xpack_decode_integer(encoded_string_len, p, buf_end, n);
  if (len == XPACK_ERROR_COMPRESSION_ERROR) {
    return XPACK_ERROR_COMPRESSION_ERROR;
  }
  p += len;

  if (buf_end < p || static_cast<uint64_t>(buf_end - p) < encoded_string_len) {
    return XPACK_ERROR_COMPRESSION_ERROR;
  }

  if (isHuffman) {
    // Allocate temporary area twice the size of before decoded data
    *str = arena.str_alloc(encoded_string_len * 2);

    len = huffman_decode(*str, p, encoded_string_len);
    if (len < 0) {
      return XPACK_ERROR_COMPRESSION_ERROR;
    }
    str_length = len;
  } else {
    *str = arena.str_alloc(encoded_string_len);

    memcpy(*str, reinterpret_cast<const char *>(p), encoded_string_len);

    str_length = encoded_string_len;
  }

  return p + encoded_string_len - buf_start;
}

//
// [RFC 7541] 5.1. Integer representation
//
int64_t
xpack_encode_integer(uint8_t *buf_start, const uint8_t *buf_end, uint64_t value, uint8_t n)
{
  if (buf_start >= buf_end) {
    return -1;
  }

  uint8_t *p = buf_start;

  // Preserve the first n bits
  uint8_t prefix = *buf_start & (0xFF << n);

  if (value < (static_cast<uint64_t>(UINT64_C(1) << n) - 1)) {
    *(p++) = value;
  } else {
    *(p++)  = (1 << n) - 1;
    value  -= (1 << n) - 1;
    while (value >= 128) {
      if (p >= buf_end) {
        return -1;
      }
      *(p++) = (value & 0x7F) | 0x80;
      value  = value >> 7;
    }
    if (p + 1 >= buf_end) {
      return -1;
    }
    *(p++) = value;
  }

  // Restore the prefix
  *buf_start |= prefix;

  return p - buf_start;
}

int64_t
xpack_encode_string(uint8_t *buf_start, const uint8_t *buf_end, const char *value, uint64_t value_len, uint8_t n)
{
  uint8_t       *p           = buf_start;
  constexpr bool use_huffman = true;

  ts::LocalBuffer<uint8_t, 4096> local_buffer(value_len * 4);
  uint8_t                       *data     = local_buffer.data();
  int64_t                        data_len = 0;

  // TODO Choose whether to use Huffman encoding wisely
  // cppcheck-suppress knownConditionTrueFalse; leaving "use_huffman" for wise huffman usage in the future
  if (use_huffman && value_len) {
    data_len = huffman_encode(data, reinterpret_cast<const uint8_t *>(value), value_len);
  }

  // Length
  const int64_t len = xpack_encode_integer(p, buf_end, data_len, n);
  if (len == -1) {
    return -1;
  }

  if (use_huffman) {
    *p |= 0x01 << n;
  } else {
    *p &= ~(0x01 << n);
  }
  p += len;

  if (buf_end < p || buf_end - p < data_len) {
    return -1;
  }

  // Value
  if (data_len) {
    memcpy(p, data, data_len);
    p += data_len;
  }

  return p - buf_start;
}

//
// DynamicTable
//
XpackDynamicTable::XpackDynamicTable(uint32_t size) : _maximum_size(size), _available(size), _max_entries(size), _storage(size)
{
  XPACKDbg("Dynamic table size: %u", size);
  this->_entries      = static_cast<struct XpackDynamicTableEntry *>(ats_malloc(sizeof(struct XpackDynamicTableEntry) * size));
  this->_entries_head = size - 1;
  this->_entries_tail = size - 1;
}

XpackDynamicTable::~XpackDynamicTable()
{
  if (this->_entries) {
    ats_free(this->_entries);
    this->_entries = nullptr;
  }
}

const XpackLookupResult
XpackDynamicTable::lookup(uint32_t index, const char **name, size_t *name_len, const char **value, size_t *value_len) const
{
  XPACKDbg("Lookup entry: abs_index=%u", index);

  if (this->is_empty()) {
    // There's no entry
    return {0, XpackLookupResult::MatchType::NONE};
  }

  if (index > this->_entries[this->_entries_head].index) {
    // The index is invalid
    return {0, XpackLookupResult::MatchType::NONE};
  }

  if (index < this->_entries[this->_calc_index(this->_entries_tail, 1)].index) {
    // The index is invalid
    return {0, XpackLookupResult::MatchType::NONE};
  }

  uint32_t pos = this->_calc_index(this->_entries_head, index - this->_entries[this->_entries_head].index);
  *name_len    = this->_entries[pos].name_len;
  *value_len   = this->_entries[pos].value_len;
  this->_storage.read(this->_entries[pos].offset, name, *name_len, value, *value_len);
  if (this->_entries[pos].wks) {
    *name = this->_entries[pos].wks;
  }
  return {index, XpackLookupResult::MatchType::EXACT};
}

const XpackLookupResult
XpackDynamicTable::lookup(const char *name, size_t name_len, const char *value, size_t value_len) const
{
  XPACKDbg("Lookup entry: name=%.*s, value=%.*s", static_cast<int>(name_len), name, static_cast<int>(value_len), value);
  XpackLookupResult::MatchType match_type      = XpackLookupResult::MatchType::NONE;
  uint32_t                     i               = this->_calc_index(this->_entries_tail, 1);
  uint32_t                     end             = this->_calc_index(this->_entries_head, 1);
  uint32_t                     candidate_index = 0;
  const char                  *tmp_name        = nullptr;
  const char                  *tmp_value       = nullptr;

  // DynamicTable is empty
  if (this->is_empty()) {
    return {candidate_index, match_type};
  }

  for (; i != end; i = this->_calc_index(i, 1)) {
    if (name_len != 0 && this->_entries[i].name_len == name_len) {
      this->_storage.read(this->_entries[i].offset, &tmp_name, this->_entries[i].name_len, &tmp_value, this->_entries[i].value_len);
      if (match(name, name_len, tmp_name, this->_entries[i].name_len)) {
        candidate_index = this->_entries[i].index;
        if (match(value, value_len, tmp_value, this->_entries[i].value_len)) {
          // Exact match
          match_type = XpackLookupResult::MatchType::EXACT;
          break;
        } else {
          // Name match -- Keep it for no exact matches
          match_type = XpackLookupResult::MatchType::NAME;
        }
      }
    }
  }

  XPACKDbg("Lookup entry: candidate_index=%u, match_type=%u", candidate_index, static_cast<unsigned int>(match_type));
  return {candidate_index, match_type};
}

const XpackLookupResult
XpackDynamicTable::lookup(const std::string_view name, const std::string_view value) const
{
  return lookup(name.data(), name.length(), value.data(), value.length());
}

const XpackLookupResult
XpackDynamicTable::lookup_relative(uint32_t relative_index, const char **name, size_t *name_len, const char **value,
                                   size_t *value_len) const
{
  XPACKDbg("Lookup entry: rel_index=%u", relative_index);
  return this->lookup(this->_entries[this->_entries_head].index - relative_index, name, name_len, value, value_len);
}

const XpackLookupResult
XpackDynamicTable::lookup_relative(const char *name, size_t name_len, const char *value, size_t value_len) const
{
  XpackLookupResult result = this->lookup(name, name_len, value, value_len);
  result.index             = this->_entries[this->_entries_head].index - result.index;
  return result;
}

const XpackLookupResult
XpackDynamicTable::lookup_relative(const std::string_view name, const std::string_view value) const
{
  return this->lookup_relative(name.data(), name.length(), value.data(), value.length());
}

const XpackLookupResult
XpackDynamicTable::insert_entry(const char *name, size_t name_len, const char *value, size_t value_len)
{
  if (this->_max_entries == 0) {
    return {UINT32_C(0), XpackLookupResult::MatchType::NONE};
  }

  // Make enough space to insert a new entry
  uint64_t required_size = static_cast<uint64_t>(name_len) + static_cast<uint64_t>(value_len) + ADDITIONAL_32_BYTES;
  if (required_size > this->_available) {
    if (!this->_make_space(required_size - this->_available)) {
      // We can't insert a new entry because some stream(s) refer an entry that need to be evicted or the header is too big to
      // store. This is fine with HPACK, but not with QPACK.
      return {UINT32_C(0), XpackLookupResult::MatchType::NONE};
    }
  }

  // Insert
  const char *wks = nullptr;
  hdrtoken_tokenize(name, name_len, &wks);
  this->_entries_head                 = this->_calc_index(this->_entries_head, 1);
  this->_entries[this->_entries_head] = {
    this->_entries_inserted++,
    this->_storage.write(name, static_cast<uint32_t>(name_len), value, static_cast<uint32_t>(value_len)),
    static_cast<uint32_t>(name_len),
    static_cast<uint32_t>(value_len),
    0,
    wks};
  this->_available -= required_size;

  XPACKDbg("Insert Entry: entry=%u, index=%u, size=%zu", this->_entries_head, this->_entries_inserted - 1, name_len + value_len);
  XPACKDbg("Available size: %u", this->_available);
  return {this->_entries_inserted, value_len ? XpackLookupResult::MatchType::EXACT : XpackLookupResult::MatchType::NAME};
}

const XpackLookupResult
XpackDynamicTable::insert_entry(const std::string_view name, const std::string_view value)
{
  return insert_entry(name.data(), name.length(), value.data(), value.length());
}

const XpackLookupResult
XpackDynamicTable::duplicate_entry(uint32_t current_index)
{
  const char *name      = nullptr;
  size_t      name_len  = 0;
  const char *value     = nullptr;
  size_t      value_len = 0;
  char       *duped_name;
  char       *duped_value;

  XpackLookupResult result = this->lookup(current_index, &name, &name_len, &value, &value_len);
  if (result.match_type != XpackLookupResult::MatchType::EXACT) {
    return result;
  }

  // We need to dup name and value to avoid memcpy-param-overlap
  duped_name  = ats_strndup(name, name_len);
  duped_value = ats_strndup(value, value_len);
  result      = this->insert_entry(duped_name, name_len, duped_value, value_len);
  ats_free(duped_name);
  ats_free(duped_value);

  return result;
}

bool
XpackDynamicTable::should_duplicate(uint32_t /* index ATS_UNUSED */)
{
  // TODO: Check whether a specified entry should be duplicated
  // Just return false for now
  return false;
}

bool
XpackDynamicTable::update_maximum_size(uint32_t new_max_size)
{
  uint32_t used = this->_maximum_size - this->_available;
  if (used < new_max_size) {
    this->_maximum_size = new_max_size;
    this->_available    = new_max_size - used;
    this->_expand_storage_size(new_max_size);
    return true;
  }

  // used is larger than or equal to new_max_size. This means that _maximum_size
  // is shrinking and we need to evict entries to get the used space below
  // new_max_size.
  bool ret = this->_make_space(used - new_max_size);
  // Size update must succeed.
  if (ret) {
    this->_available    = new_max_size - (this->_maximum_size - this->_available);
    this->_maximum_size = new_max_size;
  }
  return ret;
}

uint32_t
XpackDynamicTable::size() const
{
  return this->_maximum_size - this->_available;
}

uint32_t
XpackDynamicTable::maximum_size() const
{
  return this->_maximum_size;
}

void
XpackDynamicTable::ref_entry(uint32_t index)
{
  uint32_t pos = this->_calc_index(this->_entries_head, (index - this->_entries[this->_entries_head].index));
  ++this->_entries[pos].ref_count;
}

void
XpackDynamicTable::unref_entry(uint32_t index)
{
  uint32_t pos = this->_calc_index(this->_entries_head, (index - this->_entries[this->_entries_head].index));
  --this->_entries[pos].ref_count;
}

bool
XpackDynamicTable::is_empty() const
{
  return this->_entries_head == this->_entries_tail;
}

uint32_t
XpackDynamicTable::largest_index() const
{
  // This function can return a meaningful value only if there is at least one entry on the table.
  ink_assert(!this->is_empty());
  return this->_entries_inserted - 1;
}

uint32_t
XpackDynamicTable::count() const
{
  if (is_empty()) {
    return 0;
  } else if (this->_entries_head > this->_entries_tail) {
    return this->_entries_head - this->_entries_tail;
  } else {
    return (this->_max_entries - this->_entries_tail - 1) + (this->_entries_head + 1);
  }
}

void
XpackDynamicTable::_expand_storage_size(uint32_t new_storage_size)
{
  ExpandCapacityContext context{this->_storage, new_storage_size};
  if (!context.ok_to_expand()) {
    return;
  }
  uint32_t i   = this->_calc_index(this->_entries_tail, 1);
  uint32_t end = this->_calc_index(this->_entries_head, 1);
  for (; i != end; i = this->_calc_index(i, 1)) {
    auto &entry  = this->_entries[i];
    entry.offset = context.copy_field(entry.offset, entry.name_len + entry.value_len);
  }
}

bool
XpackDynamicTable::_make_space(uint64_t extra_space_needed)
{
  uint32_t freed = 0;
  uint32_t tail  = this->_entries_tail;

  // Check to see if we need more space and that we have entries to evict
  while (extra_space_needed > freed && this->_entries_head != tail) {
    tail = this->_calc_index(tail, 1); // Move to the next entry

    if (this->_entries[tail].ref_count) {
      break;
    }
    freed += this->_entries[tail].name_len + this->_entries[tail].value_len + ADDITIONAL_32_BYTES;
  }

  // Evict
  if (freed > 0) {
    XPACKDbg("Evict entries: from %u to %u", this->_entries[this->_calc_index(this->_entries_tail, 1)].index,
             this->_entries[tail - 1].index);
    this->_available    += freed;
    this->_entries_tail  = tail;

    XPACKDbg("Available size: %u", this->_available);
  }

  return freed >= extra_space_needed;
}

uint32_t
XpackDynamicTable::_calc_index(uint32_t base, int64_t offset) const
{
  if (unlikely(this->_max_entries == 0)) {
    return base + offset;
  } else {
    return (base + offset) % this->_max_entries;
  }
}

//
// DynamicTableStorage
//

XpackDynamicTableStorage::XpackDynamicTableStorage(uint32_t size)
  : _overwrite_threshold(size), _capacity{size * 2}, _head{_capacity - 1}
{
  this->_data = reinterpret_cast<uint8_t *>(ats_malloc(this->_capacity));
}

XpackDynamicTableStorage::~XpackDynamicTableStorage()
{
  ats_free(this->_data);
  this->_data = nullptr;
}

void
XpackDynamicTableStorage::read(uint32_t offset, const char **name, uint32_t name_len, const char **value,
                               uint32_t /* value_len ATS_UNUSED */) const
{
  *name  = reinterpret_cast<const char *>(this->_data + offset);
  *value = reinterpret_cast<const char *>(this->_data + offset + name_len);
}

uint32_t
XpackDynamicTableStorage::write(const char *name, uint32_t name_len, const char *value, uint32_t value_len)
{
  // insert_entry should guard against buffer overrun, but rather than overrun
  // our buffer we assert here in case something horrible went wrong.
  ink_release_assert(name_len + value_len <= this->_capacity);
  ink_release_assert(this->_head == this->_capacity - 1 || this->_head + name_len + value_len <= this->_capacity);

  uint32_t offset = (this->_head + 1) % this->_capacity;
  if (name_len > 0) {
    memcpy(this->_data + offset, name, name_len);
  }
  if (value_len > 0) {
    memcpy(this->_data + offset + name_len, value, value_len);
  }

  this->_head = (this->_head + (name_len + value_len)) % this->_capacity;
  if (this->_head > this->_overwrite_threshold) {
    // This is how we wrap back around to the beginning of the buffer.
    this->_head = this->_capacity - 1;
  }

  return offset;
}

bool
XpackDynamicTableStorage::_start_expanding_capacity(uint32_t new_maximum_size)
{
  if ((new_maximum_size * 2) <= this->_capacity) {
    // We never shrink our memory for the data storage because we don't support
    // arbitrary eviction from it via XpackDynamicTableStorage. The
    // XpackDynamicTable class keeps track of field sizes and therefore can
    // evict properly. Also, we don't want to invalidate XpackDynamicTable's
    // offsets into the storage.
    return false;
  }
  this->_capacity = new_maximum_size * 2;

  this->_old_data = this->_data;
  this->_data     = reinterpret_cast<uint8_t *>(ats_malloc(this->_capacity));
  if (this->_data == nullptr) {
    // Realloc failed. We're out of memory and ATS is in trouble.
    return false;
  }

  this->_overwrite_threshold = new_maximum_size;
  this->_head                = this->_capacity - 1;
  return true;
}

void
XpackDynamicTableStorage::_finish_expanding_capacity()
{
  ats_free(this->_old_data);
  this->_old_data = nullptr;
}

uint32_t
ExpandCapacityContext::copy_field(uint32_t offset, uint32_t len)
{
  char const *field = reinterpret_cast<char const *>(this->_storage._old_data + offset);
  return this->_storage.write(field, len, nullptr, 0);
}
