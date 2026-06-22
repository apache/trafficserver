/** @file

  Unit tests for the v3 binary-log JSON reference decoder (log_entry_to_json).

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

#include "LogEntryJson.h"

#include "proxy/logging/LogBuffer.h"
#include "proxy/logging/LogAccess.h"
#include "proxy/logging/LogField.h"

#include "tscore/ink_inet.h"
#include "tscore/ink_align.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace std::literals;

namespace
{
// Hand-build a minimal in-memory v3 segment containing a single entry, then
// decode it with log_entry_to_json. The decoder is exercised with no global
// field table: it sees only the symbols and the field-type schema, exactly as
// an out-of-tree reader would.
struct V3Segment {
  alignas(16) char storage[1024] = {};

  LogBufferHeader *
  header()
  {
    return reinterpret_cast<LogBufferHeader *>(storage);
  }
};

// Offsets chosen to clear sizeof(LogBufferHeader) and stay 8-byte aligned so
// that the int64 marshalling below is well-aligned.
constexpr unsigned SYM_OFF    = 256;
constexpr unsigned SCHEMA_OFF = 320;
constexpr unsigned DATA_OFF   = 384;

void
init_segment(V3Segment &seg, const char *symbols, const std::vector<LogField::Type> &codes)
{
  LogBufferHeader *h       = seg.header();
  h->cookie                = LOG_SEGMENT_COOKIE;
  h->version               = LOG_SEGMENT_VERSION;
  h->byte_count            = sizeof(seg.storage); // the whole hand-built segment is readable
  h->fmt_fieldlist_offset  = SYM_OFF;
  h->fmt_fieldtypes_offset = SCHEMA_OFF;
  h->data_offset           = DATA_OFF;

  memcpy(seg.storage + SYM_OFF, symbols, strlen(symbols) + 1);

  auto *schema        = reinterpret_cast<LogFieldTypeSchema *>(seg.storage + SCHEMA_OFF);
  schema->field_count = static_cast<uint16_t>(codes.size());

  auto *type_codes = const_cast<uint8_t *>(schema->type_codes());
  for (size_t i = 0; i < codes.size(); ++i) {
    type_codes[i] = static_cast<uint8_t>(codes[i]);
  }
}
} // namespace

TEST_CASE("v3 generic decode round-trip with IPv4, STRING, INT", "[logcat][v3]")
{
  V3Segment seg;
  init_segment(seg, "chi,cqu,pssc", {LogField::Type::IP, LogField::Type::STRING, LogField::Type::sINT});

  auto *entry           = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  entry->timestamp      = 1234;
  entry->timestamp_usec = 5678;

  char *w = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);

  IpEndpoint ep;
  REQUIRE(0 == ats_ip_pton("192.0.2.10"sv, &ep.sa));
  w += LogAccess::marshal_ip(w, &ep.sa);

  const char *url  = "GET /index.html";
  int         slen = LogAccess::padded_strlen(url);
  LogAccess::marshal_str(w, url, slen);
  w += slen;

  LogAccess::marshal_int(w, 200);
  w += INK_MIN_ALIGN;

  entry->entry_len = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry));

  char out[256];
  int  n = log_entry_to_json(entry, seg.header(), out, sizeof(out));
  REQUIRE(n > 0);
  CHECK(std::string(out, n) == R"({"chi":"192.0.2.10","cqu":"GET /index.html","pssc":200})");
}

TEST_CASE("v3 generic decode handles IPv6 and unspecified IP", "[logcat][v3]")
{
  auto decode_single_ip = [](sockaddr const *ip) -> std::string {
    V3Segment seg;
    init_segment(seg, "chi", {LogField::Type::IP});

    auto *entry       = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
    char *w           = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);
    w                += LogAccess::marshal_ip(w, ip);
    entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry));

    char out[256];
    int  n = log_entry_to_json(entry, seg.header(), out, sizeof(out));
    REQUIRE(n > 0);
    return std::string(out, n);
  };

  SECTION("IPv6")
  {
    IpEndpoint ep;
    REQUIRE(0 == ats_ip_pton("2001:db8::1"sv, &ep.sa));
    CHECK(decode_single_ip(&ep.sa) == R"({"chi":"2001:db8::1"})");
  }

  SECTION("unspecified / null IP")
  {
    // marshal_ip(nullptr) records an AF_UNSPEC address; the decoder renders the
    // existing "invalid address" sentinel.
    CHECK(decode_single_ip(nullptr) == R"({"chi":"0"})");
  }
}

TEST_CASE("v3 generic decode requires the field-type schema", "[logcat][v3]")
{
  V3Segment        seg;
  LogBufferHeader *h       = seg.header();
  h->cookie                = LOG_SEGMENT_COOKIE;
  h->version               = LOG_SEGMENT_VERSION;
  h->byte_count            = sizeof(seg.storage);
  h->fmt_fieldlist_offset  = SYM_OFF;
  h->fmt_fieldtypes_offset = 0; // emulate a v2 segment: no schema
  h->data_offset           = DATA_OFF;
  memcpy(seg.storage + SYM_OFF, "chi", 4);

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char  out[64];
  CHECK(log_entry_to_json(entry, h, out, sizeof(out)) == -1);
}

TEST_CASE("v3 generic decode escapes JSON structural characters in strings", "[logcat][v3]")
{
  V3Segment seg;
  init_segment(seg, "msg", {LogField::Type::STRING});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);

  const char *msg  = "he\"llo\\x"; // contains a quote and a backslash
  int         slen = LogAccess::padded_strlen(msg);
  LogAccess::marshal_str(w, msg, slen);
  w                += slen;
  entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry));

  char out[256];
  int  n = log_entry_to_json(entry, seg.header(), out, sizeof(out));
  REQUIRE(n > 0);
  CHECK(std::string(out, n) == R"({"msg":"he\"llo\\x"})");
}

TEST_CASE("v3 generic decode escapes control characters in strings", "[logcat][v3]")
{
  V3Segment seg;
  init_segment(seg, "msg", {LogField::Type::STRING});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);

  // Newline and tab take the short escapes; 0x01 has no short form and must use
  // \u00XX. Emitted raw, any of the three would produce invalid JSON. (The
  // \x01 escape is split from 'd' so the literal is the byte 0x01, not 0x1D.)
  const char *msg  = "a\nb\tc\x01"
                     "d";
  int         slen = LogAccess::padded_strlen(msg);
  LogAccess::marshal_str(w, msg, slen);
  w                += slen;
  entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry));

  char out[256];
  int  n = log_entry_to_json(entry, seg.header(), out, sizeof(out));
  REQUIRE(n > 0);
  CHECK(std::string(out, n) == R"({"msg":"a\nb\tc\u0001d"})");
}

TEST_CASE("v3 generic decode escapes JSON structural characters in symbol keys", "[logcat][v3]")
{
  // A corrupt/untrusted segment may carry arbitrary bytes in fmt_fieldlist, and
  // the symbol becomes a JSON key. The quote and backslash must be escaped just
  // like string values (control-character escaping is covered above); emitted
  // raw they would produce invalid (or structurally different) JSON.
  V3Segment seg;
  init_segment(seg, "a\"b\\c", {LogField::Type::sINT});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);
  LogAccess::marshal_int(w, 7);
  w                += INK_MIN_ALIGN;
  entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry));

  char out[256];
  int  n = log_entry_to_json(entry, seg.header(), out, sizeof(out));
  REQUIRE(n > 0);
  CHECK(std::string(out, n) == R"({"a\"b\\c":7})");
}

TEST_CASE("v3 generic decode rejects an unknown type code", "[logcat][v3]")
{
  V3Segment seg;
  init_segment(seg, "x", {static_cast<LogField::Type>(99)});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);
  LogAccess::marshal_int(w, 1);
  w                += INK_MIN_ALIGN;
  entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry));

  char out[64];
  CHECK(log_entry_to_json(entry, seg.header(), out, sizeof(out)) == -1);
}

TEST_CASE("v3 generic decode returns -1 when the output buffer is too small", "[logcat][v3]")
{
  V3Segment seg;
  init_segment(seg, "pssc", {LogField::Type::sINT});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);
  LogAccess::marshal_int(w, 200);
  w                += INK_MIN_ALIGN;
  entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry));

  char out[4]; // too small for {"pssc":200}
  CHECK(log_entry_to_json(entry, seg.header(), out, sizeof(out)) == -1);
}

TEST_CASE("v3 generic decode rejects a field_count larger than the entry data", "[logcat][v3]")
{
  V3Segment seg;
  // Schema claims three INT fields, but the entry only contains one.
  init_segment(seg, "a,b,c", {LogField::Type::sINT, LogField::Type::sINT, LogField::Type::sINT});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);
  LogAccess::marshal_int(w, 1);
  w                += INK_MIN_ALIGN;
  entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry)); // room for one INT only

  char out[256];
  CHECK(log_entry_to_json(entry, seg.header(), out, sizeof(out)) == -1);
}

TEST_CASE("v3 generic decode rejects field_count not matching the symbol list", "[logcat][v3]")
{
  V3Segment seg;
  // Two symbols, but the schema claims a single field: the counts disagree, so
  // the segment is malformed and must be refused (not decoded with garbage keys).
  init_segment(seg, "a,b", {LogField::Type::sINT});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);
  LogAccess::marshal_int(w, 1);
  w                += INK_MIN_ALIGN;
  entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry));

  char out[64];
  CHECK(log_entry_to_json(entry, seg.header(), out, sizeof(out)) == -1);
}

TEST_CASE("v3 generic decode reads a dINT field (16 bytes)", "[logcat][v3]")
{
  V3Segment seg;
  init_segment(seg, "sshv", {LogField::Type::dINT});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);

  LogAccess::marshal_int(w, 1); // major
  w += INK_MIN_ALIGN;
  LogAccess::marshal_int(w, 1); // minor
  w                += INK_MIN_ALIGN;
  entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry));

  char out[256];
  int  n = log_entry_to_json(entry, seg.header(), out, sizeof(out));
  REQUIRE(n > 0);
  CHECK(std::string(out, n) == R"({"sshv":[1,1]})");
}

TEST_CASE("v3 generic decode rejects a truncated dINT field", "[logcat][v3]")
{
  V3Segment seg;
  init_segment(seg, "sshv", {LogField::Type::dINT});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);

  LogAccess::marshal_int(w, 1);
  w                += INK_MIN_ALIGN;
  entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry)); // only one int, not two

  char out[64];
  CHECK(log_entry_to_json(entry, seg.header(), out, sizeof(out)) == -1);
}

TEST_CASE("v3 generic decode rejects an unterminated string field", "[logcat][v3]")
{
  V3Segment seg;
  init_segment(seg, "s", {LogField::Type::STRING});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);
  memset(w, 'A', 8); // no NUL within the entry window
  entry->entry_len = static_cast<uint32_t>((w - reinterpret_cast<char *>(entry)) + 8);

  char out[64];
  CHECK(log_entry_to_json(entry, seg.header(), out, sizeof(out)) == -1);
}

TEST_CASE("v3 generic decode emits raw values, never field semantics", "[logcat][v3]")
{
  // Pins "framing, not semantics" (see LogEntryJson.h). crc is alias-mapped to
  // "TCP_HIT" and cqts to a date by the ASCII path; the schema-driven decoder
  // dispatches on the framing type (sINT) and must emit the raw integers.
  V3Segment seg;
  init_segment(seg, "crc,cqts", {LogField::Type::sINT, LogField::Type::sINT});

  auto *entry = reinterpret_cast<LogEntryHeader *>(seg.storage + DATA_OFF);
  char *w     = reinterpret_cast<char *>(entry) + sizeof(LogEntryHeader);
  LogAccess::marshal_int(w, 2); // a cache-result code; "TCP_HIT"-like in ASCII
  w += INK_MIN_ALIGN;
  LogAccess::marshal_int(w, 1700000000); // a timestamp; a date string in ASCII
  w                += INK_MIN_ALIGN;
  entry->entry_len  = static_cast<uint32_t>(w - reinterpret_cast<char *>(entry));

  char out[256];
  int  n = log_entry_to_json(entry, seg.header(), out, sizeof(out));
  REQUIRE(n > 0);
  CHECK(std::string(out, n) == R"({"crc":2,"cqts":1700000000})");
}
