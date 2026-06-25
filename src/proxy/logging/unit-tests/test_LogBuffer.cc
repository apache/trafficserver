/** @file

  Unit tests for the self-describing v3 binary log format (LogBuffer v3).

  Covers the core (in-tree) pieces: that each field's LogField::Type matches
  its on-wire framing, the marshal/unmarshal INT round-trip, the writer's
  field-list-to-schema mapping, and the version-aware header sizing. The JSON
  reference decoder lives in traffic_logcat and is tested by test_LogEntryJson.

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

#include <catch2/catch_test_macros.hpp>

#include "proxy/logging/LogBuffer.h"
#include "proxy/logging/LogAccess.h"
#include "proxy/logging/LogField.h"
#include "proxy/logging/LogFormat.h"
#include "proxy/logging/Log.h"

#include "tscore/ink_align.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("LogField::Type reflects each field's on-wire framing", "[logging][v3]")
{
  Log::init_fields();

  // Each field's declared type must match its on-wire framing, since the schema
  // serializes Type directly: pssc/crc single ints, cqu string, chi IP, sshv a
  // pair (HTTP version), ppv a string (after the sINT/dINT type fixes).
  LogFormat fmt("v3wt", "%<pssc> %<crc> %<cqu> %<chi> %<sshv> %<ppv>");
  REQUIRE(fmt.valid());

  std::vector<LogField::Type> types;
  for (LogField *f = fmt.field_list().first(); f != nullptr; f = fmt.field_list().next(f)) {
    types.push_back(f->type());
  }

  REQUIRE(types == (std::vector<LogField::Type>{LogField::Type::sINT, LogField::Type::sINT, LogField::Type::STRING,
                                                LogField::Type::IP, LogField::Type::dINT, LogField::Type::STRING}));
}

TEST_CASE("marshal_int / unmarshal_int round-trip", "[logging][v3]")
{
  alignas(int64_t) char buf[INK_MIN_ALIGN] = {};
  LogAccess::marshal_int(buf, 0x0102030405060708LL);

  char *p = buf;
  CHECK(LogAccess::unmarshal_int(&p) == 0x0102030405060708LL);
  CHECK(p == buf + INK_MIN_ALIGN);
}

TEST_CASE("v3 schema maps each field to its type in field-list order", "[logging][v3]")
{
  Log::init_fields();

  // chi -> IP, cqu -> STRING, pssc -> sINT (see Log::init_fields).
  LogFormat fmt("v3test", "%<chi> %<cqu> %<pssc>");
  REQUIRE(fmt.valid());

  const LogFieldList         &fields = fmt.field_list();
  std::vector<std::string>    symbols;
  std::vector<LogField::Type> types;
  for (LogField *f = fields.first(); f != nullptr; f = fields.next(f)) {
    symbols.emplace_back(f->symbol());
    types.push_back(f->type());
  }

  REQUIRE(symbols == (std::vector<std::string>{"chi", "cqu", "pssc"}));
  REQUIRE(types == (std::vector<LogField::Type>{LogField::Type::IP, LogField::Type::STRING, LogField::Type::sINT}));
}

TEST_CASE("log_buffer_header_size sizes the header per version", "[logging][v3]")
{
  CHECK(log_buffer_header_size(2) == offsetof(LogBufferHeader, fmt_fieldtypes_offset));
  CHECK(log_buffer_header_size(3) == sizeof(LogBufferHeader));
  CHECK(log_buffer_header_size(1) == 0); // below the supported range
  CHECK(log_buffer_header_size(4) == 0); // above the supported range
}

TEST_CASE("LogBufferIterator walks a well-formed segment then stops", "[logging][v3]")
{
  alignas(8) char storage[256] = {};
  auto           *h            = reinterpret_cast<LogBufferHeader *>(storage);
  h->version                   = LOG_SEGMENT_VERSION;
  h->data_offset               = sizeof(LogBufferHeader);
  h->entry_count               = 1;

  auto          *e   = reinterpret_cast<LogEntryHeader *>(storage + h->data_offset);
  const uint32_t len = INK_ALIGN_DEFAULT(sizeof(LogEntryHeader) + INK_MIN_ALIGN);
  e->entry_len       = len;
  h->byte_count      = h->data_offset + len;

  LogBufferIterator iter(h);
  CHECK(iter.next() == e);
  CHECK(iter.next() == nullptr);
}

TEST_CASE("LogBufferIterator rejects an out-of-segment data_offset", "[logging][v3]")
{
  // A corrupt/hostile data_offset past the segment must not let next()
  // dereference outside the buffer (.blog read by logcat/logstats is untrusted).
  alignas(8) char storage[256] = {};
  auto           *h            = reinterpret_cast<LogBufferHeader *>(storage);
  h->version                   = LOG_SEGMENT_VERSION;
  h->byte_count                = sizeof(storage);
  h->entry_count               = 1;
  h->data_offset               = sizeof(storage) + 64; // points past the segment

  LogBufferIterator iter(h);
  CHECK(iter.next() == nullptr);
}

TEST_CASE("LogBufferIterator stops on an entry_len that overruns the segment", "[logging][v3]")
{
  alignas(8) char storage[256] = {};
  auto           *h            = reinterpret_cast<LogBufferHeader *>(storage);
  h->version                   = LOG_SEGMENT_VERSION;
  h->byte_count                = sizeof(storage);
  h->entry_count               = 1;
  h->data_offset               = sizeof(LogBufferHeader);

  auto *e      = reinterpret_cast<LogEntryHeader *>(storage + h->data_offset);
  e->entry_len = sizeof(storage); // from data_offset this runs past byte_count

  LogBufferIterator iter(h);
  CHECK(iter.next() == nullptr);
}

TEST_CASE("LogBufferIterator rejects a misaligned data_offset", "[logging][v3]")
{
  // A non-8-aligned data_offset would make the entry's int64 reads misaligned.
  alignas(8) char storage[256] = {};
  auto           *h            = reinterpret_cast<LogBufferHeader *>(storage);
  h->version                   = LOG_SEGMENT_VERSION;
  h->byte_count                = sizeof(storage);
  h->entry_count               = 1;
  h->data_offset               = sizeof(LogBufferHeader) + 4; // in range but not 8-aligned

  LogBufferIterator iter(h);
  CHECK(iter.next() == nullptr);
}

TEST_CASE("LogBufferIterator stops on a misaligned entry_len", "[logging][v3]")
{
  alignas(8) char storage[256] = {};
  auto           *h            = reinterpret_cast<LogBufferHeader *>(storage);
  h->version                   = LOG_SEGMENT_VERSION;
  h->byte_count                = sizeof(storage);
  h->entry_count               = 1;
  h->data_offset               = sizeof(LogBufferHeader);

  auto *e      = reinterpret_cast<LogEntryHeader *>(storage + h->data_offset);
  e->entry_len = sizeof(LogEntryHeader) + 4; // >= header and in range, but not 8-aligned

  LogBufferIterator iter(h);
  CHECK(iter.next() == nullptr);
}
