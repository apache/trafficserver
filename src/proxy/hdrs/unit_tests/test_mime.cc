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

#include <string_view>

using namespace std::literals;

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include "tscore/ink_platform.h"
#include "tscore/Diags.h"
#include "tscore/BaseLogFile.h"
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
