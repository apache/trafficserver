/** @file

  Reference decoder for the self-describing v3 binary log format.

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

#include "LogEntryJson.h"

#include "proxy/logging/LogBuffer.h"
#include "proxy/logging/LogAccess.h"
#include "proxy/logging/LogField.h"

// Deliberately not including the global field table (proxy/logging/Log.h):
// decoding must depend only on the segment's schema (see LogEntryJson.h).

#include "tscore/ink_inet.h"
#include "tscore/ink_align.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
// fmt_fieldlist symbols are comma-separated (e.g. "chi,cqu,pssc"); tolerate
// spaces too.
bool
is_field_sep(char c)
{
  return c == ',' || c == ' ';
}
} // namespace

int
log_entry_to_json(LogEntryHeader *entry, LogBufferHeader *header, char *buf, int buf_len)
{
  if (entry == nullptr || header == nullptr || buf == nullptr || buf_len <= 0) {
    return -1;
  }

  // [seg_start, seg_end) is the only readable region (byte_count == segment size).
  char *seg_start = reinterpret_cast<char *>(header);
  char *seg_end   = seg_start + header->byte_count;

  // v3 decode needs the field-type schema and the symbols.
  char *schema_blob = header->fmt_fieldtypes();
  char *symbols     = header->fmt_fieldlist();
  if (schema_blob == nullptr || symbols == nullptr) {
    return -1;
  }
  // Both must lie within the segment, including the fixed schema prefix.
  if (schema_blob < seg_start || schema_blob + sizeof(LogFieldTypeSchema) > seg_end || symbols < seg_start || symbols >= seg_end) {
    return -1;
  }

  // field_count is a uint16 that may sit at an unaligned offset (the writer
  // places the schema right after the NUL-terminated header strings), so read
  // it byte-wise rather than through the struct.
  uint16_t fc16 = 0;
  memcpy(&fc16, schema_blob, sizeof(fc16));
  const unsigned field_count = fc16;
  const uint8_t *codes       = reinterpret_cast<const uint8_t *>(schema_blob) + sizeof(LogFieldTypeSchema);
  if (reinterpret_cast<const char *>(codes) + field_count > seg_end) {
    return -1;
  }

  // field_count must match the symbol count; a mismatch is a corrupt segment.
  unsigned symbol_count = 0;
  bool     in_token     = false;
  for (const char *p = symbols; p < seg_end && *p != '\0'; ++p) {
    if (is_field_sep(*p)) {
      in_token = false;
    } else if (!in_token) {
      in_token = true;
      ++symbol_count;
    }
  }
  if (symbol_count != field_count) {
    return -1;
  }

  // Clamp value reads to this entry (or the segment, if entry_len is bogus).
  char *entry_start = reinterpret_cast<char *>(entry);
  if (entry_start < seg_start || entry_start + sizeof(LogEntryHeader) > seg_end) {
    return -1;
  }
  char *read_from = entry_start + sizeof(LogEntryHeader);
  char *read_end  = entry_start + entry->entry_len;
  if (read_end < read_from || read_end > seg_end) {
    read_end = seg_end;
  }

  int written = 0;

  // Append n bytes, keeping one byte in reserve for the trailing NUL.
  auto put = [&](const char *src, int n) -> bool {
    if (n < 0 || written + n >= buf_len) {
      return false;
    }
    memcpy(buf + written, src, n);
    written += n;
    return true;
  };
  auto put_ch = [&](char c) -> bool { return put(&c, 1); };

  if (!put_ch('{')) {
    return -1;
  }

  const char *sym = symbols;
  for (unsigned i = 0; i < field_count; ++i) {
    // Next symbol token (comma-separated in fmt_fieldlist), bounded by the segment.
    while (sym < seg_end && is_field_sep(*sym)) {
      ++sym;
    }
    const char *sym_start = sym;
    while (sym < seg_end && *sym != '\0' && !is_field_sep(*sym)) {
      ++sym;
    }
    int sym_len = static_cast<int>(sym - sym_start);

    if (i > 0 && !put_ch(',')) {
      return -1;
    }
    if (!put_ch('"') || !put(sym_start, sym_len) || !put_ch('"') || !put_ch(':')) {
      return -1;
    }

    // Dispatch on the framing type only, never the symbol; no field semantics
    // (see the contract in LogEntryJson.h).
    switch (static_cast<LogField::Type>(codes[i])) {
    case LogField::Type::sINT: {
      if (read_from + INK_MIN_ALIGN > read_end) {
        return -1;
      }
      int64_t v = LogAccess::unmarshal_int(&read_from);
      char    num[24]; // INT64_MIN is 20 digits + sign + NUL
      int     n = snprintf(num, sizeof(num), "%" PRId64, v);
      if (!put(num, n)) {
        return -1;
      }
      break;
    }
    case LogField::Type::STRING: {
      // NUL-terminated, 8-byte padded; require the terminator and padded field
      // within the entry.
      char *nul = static_cast<char *>(memchr(read_from, '\0', static_cast<size_t>(read_end - read_from)));
      if (nul == nullptr) {
        return -1;
      }
      const char *s          = read_from;
      size_t      padded_len = INK_ALIGN_DEFAULT(static_cast<size_t>(nul - read_from) + 1);
      if (read_from + padded_len > read_end) {
        return -1;
      }
      read_from += padded_len;
      if (!put_ch('"')) {
        return -1;
      }
      for (const char *p = s; p < nul; ++p) {
        // Minimal JSON escaping for structural characters.
        if (*p == '"' || *p == '\\') {
          if (!put_ch('\\')) {
            return -1;
          }
        }
        if (!put_ch(*p)) {
          return -1;
        }
      }
      if (!put_ch('"')) {
        return -1;
      }
      break;
    }
    case LogField::Type::IP: {
      // uint16 family + family-sized address, 8-byte padded.
      if (read_from + sizeof(LogFieldIp) > read_end) {
        return -1;
      }
      const LogFieldIp *ip  = reinterpret_cast<const LogFieldIp *>(read_from);
      size_t            len = sizeof(LogFieldIp);
      if (ip->_family == AF_INET) {
        len = sizeof(LogFieldIp4);
      } else if (ip->_family == AF_INET6) {
        len = sizeof(LogFieldIp6);
      } else if (ip->_family == AF_UNIX) {
        len = sizeof(LogFieldUn);
      }
      if (read_from + INK_ALIGN_DEFAULT(len) > read_end) {
        return -1;
      }
      char ip_str[INET6_ADDRSTRLEN + 1] = {0};
      int  n                            = LogAccess::unmarshal_ip_to_str(&read_from, ip_str, sizeof(ip_str) - 1);
      if (n < 0) {
        return -1;
      }
      if (!put_ch('"') || !put(ip_str, n) || !put_ch('"')) {
        return -1;
      }
      break;
    }
    case LogField::Type::dINT: {
      // Two int64s (16 bytes), rendered as a JSON array; meaning is the consumer's.
      if (read_from + 2 * INK_MIN_ALIGN > read_end) {
        return -1;
      }
      int64_t first  = LogAccess::unmarshal_int(&read_from);
      int64_t second = LogAccess::unmarshal_int(&read_from);
      char    num[48]; // two int64s, brackets, comma, NUL
      int     n = snprintf(num, sizeof(num), "[%" PRId64 ",%" PRId64 "]", first, second);
      if (!put(num, n)) {
        return -1;
      }
      break;
    }
    default:
      // INVALID or an unknown future type: length unknown, so we can't advance
      // safely. Bail rather than desync the entry.
      return -1;
    }
  }

  if (!put_ch('}')) {
    return -1;
  }
  buf[written] = '\0';
  return written;
}
