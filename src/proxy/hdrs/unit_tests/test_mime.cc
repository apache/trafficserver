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

#include "catch.hpp"
#include "tscore/ink_platform.h"
#include "proxy/hdrs/MIME.h"

TEST_CASE("Mime", "[proxy][mime]")
{
  MIMEField *field;
  MIMEHdr    hdr;
  hdr.create(NULL);

  hdr.field_create("Test1", 5);
  hdr.field_create("Test2", 5);
  hdr.field_create("Test3", 5);
  hdr.field_create("Test4", 5);
  field = hdr.field_create("Test5", 5);

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
  const char *host;
  int         host_len;
  const char *port;
  int         port_len;

  header_value = "host";
  hdr.value_set("Host", 4, header_value, strlen(header_value));
  hdr.get_host_port_values(&host, &host_len, &port, &port_len);
  if (host_len != 4) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (strncmp(host, "host", host_len) != 0) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (port_len != 0) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }
  if (port != nullptr) {
    std::printf("port string doesn't match\n");
    CHECK(false);
  }

  header_value = "host:";
  hdr.value_set("Host", 4, header_value, strlen(header_value));
  hdr.get_host_port_values(&host, &host_len, &port, &port_len);
  if (host_len != 4) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (strncmp(host, "host", host_len) != 0) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (port_len != 0) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }
  if (port != nullptr) {
    std::printf("port string doesn't match\n");
    CHECK(false);
  }

  header_value = "[host]";
  hdr.value_set("Host", 4, header_value, strlen(header_value));
  hdr.get_host_port_values(&host, &host_len, &port, &port_len);
  if (host_len != 6) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (strncmp(host, "[host]", host_len) != 0) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (port_len != 0) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }
  if (port != nullptr) {
    std::printf("port string doesn't match\n");
    CHECK(false);
  }

  header_value = "host:port";
  hdr.value_set("Host", 4, header_value, strlen(header_value));
  hdr.get_host_port_values(&host, &host_len, &port, &port_len);
  if (host_len != 4) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (strncmp(host, "host", host_len) != 0) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (port_len != 4) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }
  if (strncmp(port, "port", port_len) != 0) {
    std::printf("port string doesn't match\n");
    CHECK(false);
  }

  header_value = "[host]:port";
  hdr.value_set("Host", 4, header_value, strlen(header_value));
  hdr.get_host_port_values(&host, &host_len, &port, &port_len);
  if (host_len != 6) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (strncmp(host, "[host]", host_len) != 0) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (port_len != 4) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }
  if (strncmp(port, "port", port_len) != 0) {
    std::printf("port string doesn't match\n");
    CHECK(false);
  }

  header_value = "[host]:";
  hdr.value_set("Host", 4, header_value, strlen(header_value));
  hdr.get_host_port_values(&host, &host_len, &port, &port_len);
  if (host_len != 6) {
    std::printf("host length doesn't match\n");
    CHECK(false);
  }
  if (strncmp(host, "[host]", host_len) != 0) {
    std::printf("host string doesn't match\n");
    CHECK(false);
  }
  if (port_len != 0) {
    std::printf("port length doesn't match\n");
    CHECK(false);
  }
  if (port != nullptr) {
    std::printf("port string doesn't match\n");
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
