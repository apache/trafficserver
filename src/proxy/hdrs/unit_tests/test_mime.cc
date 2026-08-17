/** @file

  A brief file description

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

#include <cstdio>
#include <climits>
#include <cstdint>
#include <cstring>

#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals;

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include "tscore/ink_platform.h"
#include "tscore/Diags.h"
#include "tscore/BaseLogFile.h"
#include "tscore/ParseRules.h"
#include "proxy/hdrs/HdrToken.h"
#include "proxy/hdrs/MIME.h"
#include "proxy/hdrs/HdrHeap.h"

namespace
{
// Build a filler string of `n` copies of `c`. The length is laundered through a
// volatile so GCC at -O3 cannot constant-fold it into the inlined
// hdrtoken_wks_to_index() lookup performed by the field setters. With a known
// constant size GCC emits a -Warray-bounds false positive (a [-1] subscript it
// cannot rule out through the hdrtoken_is_wks() guard, which does prevent the
// access at run time). clang does not warn; the volatile keeps both happy.
std::string
mime_filler(std::size_t n, char c)
{
  volatile std::size_t len = n;
  return std::string(len, c);
}
} // namespace

TEST_CASE("Mime", "[proxy][mime]")
{
  MIMEField *field;
  MIMEHdr    hdr;
  hdr.create(NULL);

  hdr.field_create("Test1"sv);
  hdr.field_create("Test2"sv);
  hdr.field_create("Test3"sv);
  hdr.field_create("Test4"sv);
  field = hdr.field_create("Test5"sv);

  if (!hdr.m_mime->m_first_fblock.contains(field)) {
    std::printf("The field block doesn't contain the field but it should\n");
    CHECK(false);
  }
  if (hdr.m_mime->m_first_fblock.contains(field + (1L << 32))) {
    std::printf("The field block contains the field but it shouldn't\n");
    CHECK(false);
  }

  int slot_num = mime_hdr_field_slotnum(hdr.m_mime, field);
  if (slot_num != 4) {
    std::printf("Slot number is %d but should be 4\n", slot_num);
    CHECK(false);
  }

  slot_num = mime_hdr_field_slotnum(hdr.m_mime, field + (1L << 32));
  if (slot_num != -1) {
    std::printf("Slot number is %d but should be -1\n", slot_num);
    CHECK(false);
  }

  hdr.destroy();
}

TEST_CASE("MimeParserReuseAcrossHeaders", "[proxy][mimeparser]")
{
  // A parser reused on a different header without an intervening clear must
  // still detect duplicates of fields already present in that header. The tail
  // append derives its candidate from the header under parse, so a change of
  // header cannot carry state over. If it instead trusted parser-held state, a
  // wire duplicate of a pre-existing custom field would attach as an
  // independent head rather than joining the dup chain.
  MIMEParser parser;
  mime_parser_init(&parser);

  // First header: parse a non-WKS field so the parser seeds its dup state.
  MIMEHdr hdrA;
  hdrA.create(nullptr);
  {
    std::string_view text  = "X-Foo: 1\r\n\r\n"sv;
    const char      *start = text.data();
    REQUIRE(hdrA.parse(&parser, &start, text.data() + text.size(), true, false, false) == ParseResult::DONE);
  }

  // Second header (different mh) already carries a live non-WKS field; reuse the
  // same parser WITHOUT clearing it and parse a duplicate of that field.
  MIMEHdr hdrB;
  hdrB.create(nullptr);
  MIMEField *pre = hdrB.field_create("X-Baz"sv);
  pre->value_set(hdrB.m_heap, hdrB.m_mime, "a"sv);
  hdrB.field_attach(pre);
  {
    std::string_view text  = "X-Baz: b\r\n\r\n"sv;
    const char      *start = text.data();
    REQUIRE(hdrB.parse(&parser, &start, text.data() + text.size(), true, false, false) == ParseResult::DONE);
  }

  // The wire field must have joined the pre-existing field's dup chain: exactly
  // two X-Baz values reachable from the head.
  MIMEField *head = hdrB.field_find("X-Baz"sv);
  REQUIRE(head != nullptr);
  int count = 0;
  for (MIMEField *f = head; f != nullptr; f = f->m_next_dup) {
    ++count;
  }
  CHECK(count == 2);

  mime_parser_clear(&parser);
  hdrA.destroy();
  hdrB.destroy();
}

TEST_CASE("MimeParserTailAppendEquivalence", "[proxy][mimeparser]")
{
  // The O(1) adjacent-duplicate tail append must produce the same field/dup
  // structure as attach's full duplicate search. Build the same field sequence
  // two ways -- via the parser (which takes the tail-append path) and via
  // explicit create+attach (the reference full-attach path) -- and compare the
  // dup chain of every name.
  struct Field {
    const char *name;
    const char *value;
  };
  auto scenario = GENERATE(from_range(std::vector<std::vector<Field>>{
    {{"X-A", "1"}, {"X-A", "2"}, {"X-A", "3"}}, // consecutive custom dups
    {{"X-A", "1"}, {"X-B", "2"}, {"X-A", "3"}}, // interleaved
    {{"X-A", "1"}, {"X-A", "2"}, {"X-B", "3"}, {"X-B", "4"}}, // two adjacent runs
    {{"Set-Cookie", "a"}, {"Set-Cookie", "b"}, {"Set-Cookie", "c"}, {"Set-Cookie", "d"}}, // well-known dups
    {{"X-A", "1"}, {"X-B", "2"}, {"X-C", "3"}}, // no dups
  }));

  // Parser-built header (tail-append path).
  std::string raw;
  for (auto const &f : scenario) {
    raw += f.name;
    raw += ": ";
    raw += f.value;
    raw += "\r\n";
  }
  raw += "\r\n";
  MIMEParser parser;
  mime_parser_init(&parser);
  MIMEHdr hdrA;
  hdrA.create(nullptr);
  {
    const char *start = raw.data();
    REQUIRE(hdrA.parse(&parser, &start, raw.data() + raw.size(), true, false, false) == ParseResult::DONE);
  }
  mime_parser_clear(&parser);

  // Reference header via explicit create+attach (attach's full path, no tail append).
  MIMEHdr hdrB;
  hdrB.create(nullptr);
  for (auto const &f : scenario) {
    MIMEField *fld = hdrB.field_create(std::string_view{f.name});
    fld->value_set(hdrB.m_heap, hdrB.m_mime, std::string_view{f.value});
    hdrB.field_attach(fld);
  }

  // Compare structure, not just values. The tail append sets the dup-head flag
  // and the well-known index itself instead of letting attach do it, and it
  // must still raise the presence bit for a well-known name.
  struct Slot {
    std::string value;
    bool        dup_head;
    int16_t     wks_idx;

    bool
    operator==(Slot const &o) const
    {
      return value == o.value && dup_head == o.dup_head && wks_idx == o.wks_idx;
    }
  };

  auto collect = [](MIMEHdr &h, std::string_view n) {
    std::vector<Slot> slots;
    for (MIMEField *fld = h.field_find(n); fld != nullptr; fld = fld->m_next_dup) {
      auto v = fld->value_get();
      slots.push_back(Slot{std::string{v}, fld->is_dup_head() != 0, fld->m_wks_idx});
    }
    return slots;
  };

  std::set<std::string> names;
  for (auto const &f : scenario) {
    names.insert(f.name);
  }
  for (auto const &name : names) {
    std::vector<Slot> va = collect(hdrA, name);
    std::vector<Slot> vb = collect(hdrB, name);
    CAPTURE(name, va.size(), vb.size());
    REQUIRE(va.size() == vb.size());
    for (size_t i = 0; i < va.size(); i++) {
      CAPTURE(i, va[i].value, vb[i].value, va[i].dup_head, vb[i].dup_head, va[i].wks_idx, vb[i].wks_idx);
      CHECK(va[i] == vb[i]);
    }
  }

  CHECK(hdrA.fields_count() == hdrB.fields_count());
  CHECK(hdrA.m_mime->m_presence_bits == hdrB.m_mime->m_presence_bits);

  hdrA.destroy();
  hdrB.destroy();
}

TEST_CASE("HdrTokenFusedNameScanParity", "[proxy][hdrtoken]")
{
  // hdrtoken_field_name_scan and hdrtoken_tokenize_prehashed fuse the colon
  // scan, the FNV hash, and field-name validation into one pass. Verify the
  // fused path agrees with references: the colon position, per-byte validity,
  // and -- via the prehashed lookup fed the fused hash -- the well-known index
  // the standalone tokenizer returns (which is a proxy for hash parity).
  struct Case {
    const char *name;
    const char *tail;
  };
  static const std::vector<Case> cases = {
    {"Content-Length",    ": 5"    },
    {"content-length",    ":5"     },
    {"CONTENT-LENGTH",    ":5"     },
    {"Host",              ": x"    },
    {"hOsT",              ":x"     },
    {"Set-Cookie",        ": a=b"  },
    {"Cache-Control",     ":no"    },
    {"Transfer-Encoding", ":chunk" },
    {"@Ats-Internal",     ":z"     },
    {"X-Custom-Header",   ": v"    },
    {"sec-ch-ua",         ": \"x\""},
    {"sec-fetch-mode",    ":cors"  },
    {"priority",          ":u=1"   },
    {"X-My-Header",       ":v"     },
    {"a",                 ":b"     },
  };

  for (auto const &c : cases) {
    std::string const buf      = std::string(c.name) + c.tail;
    int const         name_len = static_cast<int>(strlen(c.name));
    uint32_t          hash     = 0;
    bool              valid    = false;
    int const         colon    = hdrtoken_field_name_scan(buf.data(), static_cast<int>(buf.size()), &hash, &valid);
    CAPTURE(c.name);
    CHECK(colon == name_len);

    bool ref_valid = true;
    for (int i = 0; i < name_len; ++i) {
      if (!ParseRules::is_http_field_name(c.name[i])) {
        ref_valid = false;
        break;
      }
    }
    CHECK(valid == ref_valid);
    CHECK(hdrtoken_tokenize_prehashed(c.name, name_len, hash) == hdrtoken_tokenize(c.name, name_len));
  }

  // Edge cases: no colon, empty name, and an invalid byte in the name.
  uint32_t h = 0;
  bool     v = false;
  CHECK(hdrtoken_field_name_scan("NoColon", 7, &h, &v) < 0);
  CHECK(hdrtoken_field_name_scan(":value", 6, &h, &v) == 0);
  {
    const char bad[] = {'X', '\x01', 'Y', ':', 'v'};
    CHECK(hdrtoken_field_name_scan(bad, 5, &h, &v) == 3);
    CHECK(v == false);
  }
}

TEST_CASE("MimeGetHostPortValues", "[proxy][mimeport]")
{
  MIMEHdr hdr;
  hdr.create(NULL);

  const char *header_value;

  header_value = "host";
  hdr.value_set("Host"sv, header_value);
  auto [field, host, port]{hdr.get_host_port_values()};
  if (host.length() != 4) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (host != "host"sv) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (!port.empty()) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }

  header_value = "host:";
  hdr.value_set("Host"sv, header_value);
  std::tie(field, host, port) = hdr.get_host_port_values();
  if (host.length() != 4) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (host != "host"sv) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (!port.empty()) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }

  header_value = "[host]";
  hdr.value_set("Host"sv, header_value);
  std::tie(field, host, port) = hdr.get_host_port_values();
  if (host.length() != 6) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (host != "[host]"sv) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (!port.empty()) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }

  header_value = "host:port";
  hdr.value_set("Host"sv, header_value);
  std::tie(field, host, port) = hdr.get_host_port_values();
  if (host.length() != 4) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (host != "host"sv) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (port.length() != 4) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }
  if (port != "port"sv) {
    std::printf("port string doesn't match\n");
    CHECK(false);
  }

  header_value = "[host]:port";
  hdr.value_set("Host"sv, header_value);
  std::tie(field, host, port) = hdr.get_host_port_values();
  if (host.length() != 6) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (host != "[host]"sv) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (port.length() != 4) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }
  if (port != "port"sv) {
    std::printf("port string doesn't match\n");
    CHECK(false);
  }

  header_value = "[host]:";
  hdr.value_set("Host"sv, header_value);
  std::tie(field, host, port) = hdr.get_host_port_values();
  if (host.length() != 6) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (host != "[host]"sv) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (!port.empty()) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }

  hdr.destroy();
}

TEST_CASE("MimeParsers", "[proxy][mimeparsers]")
{
  static const std::vector<std::pair<const char *, int>> tests = {
    {"0",             0         },
    {"1234",          1234      },
    {"-1234",         -1234     },
    {"2147483647",    2147483647},
    {"-2147483648",   2147483648},
    {"2147483648",    INT_MAX   },
    {"-2147483649",   INT_MIN   },
    {"2147483647",    INT_MAX   },
    {"-2147483648",   INT_MIN   },
    {"999999999999",  INT_MAX   },
    {"-999999999999", INT_MIN   }
  };

  auto [buf, val] = GENERATE(from_range(tests));
  CAPTURE(buf, val);

  const char *end = buf + strlen(buf);
  int         value;
  CHECK(mime_parse_int(buf, end) == val);
  REQUIRE(mime_parse_integer(buf, end, &value));
  CHECK(value == val);
}

TEST_CASE("MimeParseInt64Overflow", "[proxy][mimeparseint64]")
{
  static const std::vector<std::pair<const char *, int64_t>> tests = {
    {"0",                    0        },
    {"12345",                12345    },
    {"-12345",               -12345   },
    {"9223372036854775807",  INT64_MAX},
    {"-9223372036854775808", INT64_MIN},
    {"9223372036854775808",  INT64_MAX},
    {"-9223372036854775809", INT64_MIN},
    {"99999999999999999999", INT64_MAX},
    {" 42",                  42       },
  };

  auto [buf, val] = GENERATE(from_range(tests));
  CAPTURE(buf, val);

  const char *end = buf + strlen(buf);
  CHECK(mime_parse_int64(buf, end) == val);
}

TEST_CASE("MimeParseUintOverflow", "[proxy][mimeparseuint]")
{
  static const std::vector<std::pair<const char *, uint32_t>> tests = {
    {"0",          0         },
    {"12345",      12345     },
    {"4294967295", UINT32_MAX},
    {"4294967296", UINT32_MAX},
    {"9999999999", UINT32_MAX},
    {" 42",        42        },
  };

  auto [buf, val] = GENERATE(from_range(tests));
  CAPTURE(buf, val);

  const char *end = buf + strlen(buf);
  CHECK(mime_parse_uint(buf, end) == val);
}

TEST_CASE("MimeDateParser", "[proxy][mimedateparser]")
{
  const char *date1 = "Sun, 05 Dec 1999 08:49:37 GMT";
  const char *date2 = "Sunday, 05-Dec-1999 08:49:37 GMT";

  time_t d1 = mime_parse_date(date1, date1 + strlen(date1));
  time_t d2 = mime_parse_date(date2, date2 + strlen(date2));

  CHECK(d1 == d2);
}

// m_len_name is uint16_t (m_len_value is uint32_t : 24 and could hold more, but
// is capped at UINT16_MAX for a uniform limit). A name of 65536+n bytes used to
// wrap to length n while the byte pointer still spanned the full input, so the
// stored length no longer matched the data. mime_str_u16_set() and the no-copy
// branch of mime_field_name_value_set() now reject oversized strings.
TEST_CASE("MimeOversizedNameValueRejected", "[proxy][mime]")
{
  // Storing oversized strings calls Warning(), which dereferences diags(). Make
  // sure diags is initialized so these calls do not crash under the test binary.
  [[maybe_unused]] static bool diags_initialized = []() {
    if (diags() == nullptr) {
      DiagsPtr::set(new Diags("test_mime", nullptr, nullptr, new BaseLogFile("stderr")));
    }
    return true;
  }();

  // 70000 bytes > UINT16_MAX (65535). On unpatched code, length wraps to 4464.
  std::string const big = mime_filler(70000, 'a');
  std::string_view  big_sv{big};

  SECTION("mime_str_u16_set rejects oversized input and clears length")
  {
    HdrHeap    *heap  = new_HdrHeap();
    const char *d_str = nullptr;
    uint16_t    d_len = 0;

    const char *ret = mime_str_u16_set(heap, big_sv, &d_str, &d_len, true);

    REQUIRE(ret == nullptr);
    REQUIRE(d_str == nullptr);
    REQUIRE(d_len == 0);

    heap->destroy();
  }

  SECTION("mime_str_u16_set rejects oversized input on no-copy path too")
  {
    HdrHeap    *heap  = new_HdrHeap();
    const char *d_str = nullptr;
    uint16_t    d_len = 0;

    const char *ret = mime_str_u16_set(heap, big_sv, &d_str, &d_len, false);

    REQUIRE(ret == nullptr);
    REQUIRE(d_str == nullptr);
    REQUIRE(d_len == 0);

    heap->destroy();
  }

  SECTION("MIMEHdr::name_set with oversized name preserves the existing name")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    bool const stored = field->name_set(hdr.m_heap, hdr.m_mime, big_sv);

    // A rejected rename is a no-op: the prior name is left intact.
    REQUIRE(stored == false);
    REQUIRE(field->name_get() == "X-Test"sv);

    hdr.destroy();
  }

  SECTION("MIMEHdr value_set with oversized value leaves stored value empty")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    field->value_set(hdr.m_heap, hdr.m_mime, big_sv);

    auto value = field->value_get();
    REQUIRE(value.length() == 0);

    hdr.destroy();
  }

  SECTION("mime_field_name_value_set no-copy branch rejects oversized name")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = mime_field_create(hdr.m_heap, hdr.m_mime);
    REQUIRE(field != nullptr);

    int const raw_len = static_cast<int>(big_sv.length()) + 4;
    mime_field_name_value_set(hdr.m_heap, hdr.m_mime, field, -1, big_sv, "v"sv, 1, raw_len, false);

    REQUIRE(field->m_len_name == 0);
    REQUIRE(field->m_len_value == 0);

    hdr.destroy();
  }

  SECTION("mime_field_name_value_set no-copy branch rejects oversized value")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = mime_field_create(hdr.m_heap, hdr.m_mime);
    REQUIRE(field != nullptr);

    int const raw_len = static_cast<int>(big_sv.length()) + 4;
    mime_field_name_value_set(hdr.m_heap, hdr.m_mime, field, -1, "X-Test"sv, big_sv, 1, raw_len, false);

    REQUIRE(field->m_len_name == 0);
    REQUIRE(field->m_len_value == 0);

    hdr.destroy();
  }
}

// These tests verify that the setters return bool (true = stored, false =
// rejected as oversized with the field left empty).
//
// The header field-size limit is a uniform UINT16_MAX (65535) cap on both the
// name and the value: UINT16_MAX is the largest accepted length and 65536 and
// above are rejected. (m_len_name is uint16_t; m_len_value is uint32_t : 24 and
// could hold more, but the same cap is applied for consistency.)
TEST_CASE("MimeSetterBoolReturn", "[proxy][mime]")
{
  // Rejecting oversized strings calls Warning(), which dereferences diags().
  // Initialize diags so these calls do not crash under the test binary.
  [[maybe_unused]] static bool diags_initialized = []() {
    if (diags() == nullptr) {
      DiagsPtr::set(new Diags("test_mime", nullptr, nullptr, new BaseLogFile("stderr")));
    }
    return true;
  }();

  SECTION("value_set with normal length returns true and stores the value")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    std::string const value = mime_filler(100, 'v');
    std::string_view  value_sv{value};

    bool const stored = field->value_set(hdr.m_heap, hdr.m_mime, value_sv);

    REQUIRE(stored == true);
    REQUIRE(field->value_get().length() == 100);

    hdr.destroy();
  }

  SECTION("value_set at the largest accepted length (UINT16_MAX) returns true")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    std::string const value = mime_filler(UINT16_MAX, 'v'); // 65535
    std::string_view  value_sv{value};

    bool const stored = field->value_set(hdr.m_heap, hdr.m_mime, value_sv);

    REQUIRE(stored == true);
    REQUIRE(field->value_get().length() == UINT16_MAX);

    hdr.destroy();
  }

  SECTION("value_set at UINT16_MAX+1 returns false and leaves the value empty")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    std::string const value = mime_filler(UINT16_MAX + 1, 'v'); // 65536
    std::string_view  value_sv{value};

    bool const stored = field->value_set(hdr.m_heap, hdr.m_mime, value_sv);

    REQUIRE(stored == false);
    REQUIRE(field->value_get().length() == 0);

    hdr.destroy();
  }

  SECTION("value_set above UINT16_MAX returns false and leaves the value empty")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    std::string const value = mime_filler(UINT16_MAX + 5, 'v'); // 65540
    std::string_view  value_sv{value};

    bool const stored = field->value_set(hdr.m_heap, hdr.m_mime, value_sv);

    REQUIRE(stored == false);
    REQUIRE(field->value_get().length() == 0);

    hdr.destroy();
  }

  SECTION("oversized value_set is a no-op that preserves the existing value")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    std::string const original = mime_filler(100, 'v');
    REQUIRE(field->value_set(hdr.m_heap, hdr.m_mime, std::string_view{original}) == true);
    REQUIRE(field->value_get().length() == 100);

    std::string const oversized = mime_filler(UINT16_MAX + 5, 'V'); // 65540
    bool const        stored    = field->value_set(hdr.m_heap, hdr.m_mime, std::string_view{oversized});

    // The rejected set must not disturb the previously stored value.
    REQUIRE(stored == false);
    REQUIRE(field->value_get() == std::string_view{original});

    hdr.destroy();
  }

  SECTION("name_set with normal length returns true and stores the name")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    std::string const name = mime_filler(100, 'n');
    std::string_view  name_sv{name};

    bool const stored = field->name_set(hdr.m_heap, hdr.m_mime, name_sv);

    REQUIRE(stored == true);
    REQUIRE(field->name_get().length() == 100);

    hdr.destroy();
  }

  SECTION("name_set at the largest accepted length (UINT16_MAX) returns true")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    std::string const name = mime_filler(UINT16_MAX, 'n'); // 65535
    std::string_view  name_sv{name};

    bool const stored = field->name_set(hdr.m_heap, hdr.m_mime, name_sv);

    REQUIRE(stored == true);
    REQUIRE(field->name_get().length() == UINT16_MAX);

    hdr.destroy();
  }

  SECTION("name_set at UINT16_MAX+1 returns false and preserves the existing name")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    std::string const name = mime_filler(UINT16_MAX + 1, 'n'); // 65536
    std::string_view  name_sv{name};

    bool const stored = field->name_set(hdr.m_heap, hdr.m_mime, name_sv);

    // The rejected rename must leave the prior name intact.
    REQUIRE(stored == false);
    REQUIRE(field->name_get() == "X-Test"sv);

    hdr.destroy();
  }

  SECTION("name_set above UINT16_MAX returns false and preserves the existing name")
  {
    MIMEHdr hdr;
    hdr.create(nullptr);

    MIMEField *field = hdr.field_create("X-Test"sv);
    REQUIRE(field != nullptr);

    std::string const name = mime_filler(UINT16_MAX + 5, 'n'); // 65540
    std::string_view  name_sv{name};

    bool const stored = field->name_set(hdr.m_heap, hdr.m_mime, name_sv);

    // The rejected rename must leave the prior name intact.
    REQUIRE(stored == false);
    REQUIRE(field->name_get() == "X-Test"sv);

    hdr.destroy();
  }
}

TEST_CASE("MimeParserStaleLastAttachedAcrossDestroyedHeader", "[proxy][mimeparser]")
{
  // The O(1) tail append derives the preceding field from the header it was
  // handed, and caches nothing in the parser. This test guards that property.
  //
  // An earlier version cached a MIMEField* in the MIMEParser. Because a parser
  // outlives the headers it parses -- it is a member of Http2Stream, while
  // reset_send_headers destroys and recreates the header before re-parsing the
  // trailer block with the same parser -- that pointer could name a field of a
  // destroyed header. A freed HdrHeap returns from the allocator at the same
  // address, so the stale slot still read live, the new field was spliced onto
  // it, and mime_hdr_field_attach never ran: no presence bit, no DUP_HEAD. The
  // field printed on the wire but mime_hdr_field_find could not see it, and
  // deleting it later walked a broken dup chain into a null dereference.
  //
  // Reuse one parser across a destroyed header and assert the invariant a cache
  // would break: a parsed field must be a proper chain head, reachable through
  // its presence bit. Iterate, because whether the freed heap comes back at the
  // same address is up to the allocator.
  MIMEParser parser;
  mime_parser_init(&parser);

  for (int round = 0; round < 8; ++round) {
    // Header A: parse TWO fields so the last-attached one sits at slot 1, then
    // destroy A. m_last_attached now dangles at slot 1 of the freed block.
    // Two fields matter: with only one, the recycled slot 0 aliases the field B
    // is about to create, which is not LIVE yet, so the hazard is masked.
    {
      MIMEHdr a;
      a.create(nullptr);
      std::string_view text  = "X-Pad: 0\r\nAge: 1\r\n\r\n"sv;
      const char      *start = text.data();
      REQUIRE(a.parse(&parser, &start, text.data() + text.size(), true, false, false) == ParseResult::DONE);
      a.destroy();
    }

    // Header B: same parser, deliberately NOT cleared, same field name.
    MIMEHdr b;
    b.create(nullptr);
    {
      std::string_view text  = "Age: 2\r\n\r\n"sv;
      const char      *start = text.data();
      REQUIRE(b.parse(&parser, &start, text.data() + text.size(), true, false, false) == ParseResult::DONE);
    }

    CAPTURE(round);
    // The presence bit is what mime_hdr_field_find short-circuits on, so losing
    // it makes the field unreachable by interned name even though it prints.
    CHECK(b.presence(MIME_PRESENCE_AGE) != 0);
    const MIMEField *found = b.field_find(static_cast<std::string_view>(MIME_FIELD_AGE));
    REQUIRE(found != nullptr);
    CHECK(found->value_get_int64() == 2);
    // A single field must be its own chain head with no dup successor.
    CHECK(found->m_next_dup == nullptr);

    b.destroy();
  }

  mime_parser_clear(&parser);
}
