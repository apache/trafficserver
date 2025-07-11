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

#include <string_view>

using namespace std::literals;

#include "catch.hpp"
#include "tscore/ink_platform.h"
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
  const char *end;
  int         value;

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

  for (const auto &it : tests) {
    auto [buf, val] = it;

    end = buf + strlen(buf);
    if (mime_parse_int(buf, end) != val) {
      std::printf("Failed mime_parse_int\n");
      CHECK(false);
    }
    if (!mime_parse_integer(buf, end, &value)) {
      std::printf("Failed mime_parse_integer call\n");
      CHECK(false);
    } else if (value != val) {
      std::printf("Failed mime_parse_integer value\n");
      CHECK(false);
    }
  }

  // Also check the date parser, which relies heavily on the mime_parse_integer() function
  const char *date1 = "Sun, 05 Dec 1999 08:49:37 GMT";
  const char *date2 = "Sunday, 05-Dec-1999 08:49:37 GMT";

  time_t d1 = mime_parse_date(date1, date1 + strlen(date1));
  time_t d2 = mime_parse_date(date2, date2 + strlen(date2));

  if (d1 != d2) {
    std::printf("Failed mime_parse_date\n");
    CHECK(false);
  }

  std::printf("Date1: %" PRIdMAX "\n", d1);
  std::printf("Date2: %" PRIdMAX "\n", d2);
}
