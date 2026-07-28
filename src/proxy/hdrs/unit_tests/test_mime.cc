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
#include "tscore/ParseRules.h"
#include "proxy/hdrs/HdrToken.h"
#include "proxy/hdrs/MIME.h"

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
  // still detect duplicates of fields already present in that header. The
  // dup-skip Bloom is keyed to the header it was seeded from; without that, a
  // stale seed latch would skip reseeding and a wire duplicate of a pre-existing
  // custom field would be attached as an independent head rather than joining
  // the dup chain.
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

  auto collect = [](MIMEHdr &h, std::string_view n) {
    std::vector<std::string> vals;
    for (MIMEField *fld = h.field_find(n); fld != nullptr; fld = fld->m_next_dup) {
      auto v = fld->value_get();
      vals.emplace_back(v.data(), v.size());
    }
    return vals;
  };

  std::set<std::string> names;
  for (auto const &f : scenario) {
    names.insert(f.name);
  }
  for (auto const &name : names) {
    std::vector<std::string> va = collect(hdrA, name);
    std::vector<std::string> vb = collect(hdrB, name);
    CAPTURE(name, va.size(), vb.size());
    CHECK(va == vb);
  }

  hdrA.destroy();
  hdrB.destroy();
}

TEST_CASE("HdrTokenFusedNameScanParity", "[proxy][hdrtoken]")
{
  // opt2 fused the colon scan, FNV hash, and field-name validation into
  // hdrtoken_field_name_scan + hdrtoken_tokenize_prehashed. Verify the fused
  // path agrees with references: the colon position, per-byte validity, and --
  // via the prehashed lookup fed the fused hash -- the well-known index the
  // standalone tokenizer returns (which is a proxy for hash parity).
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

TEST_CASE("MimeDateParser", "[proxy][mimedateparser]")
{
  const char *date1 = "Sun, 05 Dec 1999 08:49:37 GMT";
  const char *date2 = "Sunday, 05-Dec-1999 08:49:37 GMT";

  time_t d1 = mime_parse_date(date1, date1 + strlen(date1));
  time_t d2 = mime_parse_date(date2, date2 + strlen(date2));

  CHECK(d1 == d2);
}
