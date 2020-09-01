/** @file

    Unit tests for HTTP2

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

#include "catch.hpp"

#include "HTTP2.h"

#include "tscpp/util/PostScript.h"

TEST_CASE("Convert HTTPHdr", "[HTTP2]")
{
  url_init();
  mime_init();
  http_init();
  http2_init();

  HTTPParser parser;
  ts::PostScript parser_defer([&]() -> void { http_parser_clear(&parser); });
  http_parser_init(&parser);

  SECTION("request")
  {
    const char request[] = "GET /index.html HTTP/1.1\r\n"
                           "Host: trafficserver.apache.org\r\n"
                           "User-Agent: foobar\r\n"
                           "\r\n";

    HTTPHdr hdr_1;
    ts::PostScript hdr_1_defer([&]() -> void { hdr_1.destroy(); });
    hdr_1.create(HTTP_TYPE_REQUEST);
    http2_init_pseudo_headers(hdr_1);

    // parse
    const char *start = request;
    const char *end   = request + sizeof(request) - 1;
    hdr_1.parse_req(&parser, &start, end, true);

    // convert to HTTP/2
    http2_convert_header_from_1_1_to_2(&hdr_1);

    // check pseudo headers
    // :method
    {
      MIMEField *f = hdr_1.field_find(HTTP2_VALUE_METHOD, HTTP2_LEN_METHOD);
      REQUIRE(f != nullptr);
      std::string_view v = f->value_get();
      CHECK(v == "GET");
    }

    // :scheme
    {
      MIMEField *f = hdr_1.field_find(HTTP2_VALUE_SCHEME, HTTP2_LEN_SCHEME);
      REQUIRE(f != nullptr);
      std::string_view v = f->value_get();
      CHECK(v == "https");
    }

    // :authority
    {
      MIMEField *f = hdr_1.field_find(HTTP2_VALUE_AUTHORITY, HTTP2_LEN_AUTHORITY);
      REQUIRE(f != nullptr);
      std::string_view v = f->value_get();
      CHECK(v == "trafficserver.apache.org");
    }

    // :path
    {
      MIMEField *f = hdr_1.field_find(HTTP2_VALUE_PATH, HTTP2_LEN_PATH);
      REQUIRE(f != nullptr);
      std::string_view v = f->value_get();
      CHECK(v == "/index.html");
    }

    // convert to HTTP/1.1
    HTTPHdr hdr_2;
    ts::PostScript hdr_2_defer([&]() -> void { hdr_2.destroy(); });
    hdr_2.create(HTTP_TYPE_REQUEST);
    hdr_2.copy(&hdr_1);

    http2_convert_header_from_2_to_1_1(&hdr_2);

    // dump
    char buf[128]  = {0};
    int bufindex   = 0;
    int dumpoffset = 0;

    hdr_2.print(buf, sizeof(buf), &bufindex, &dumpoffset);

    // check
    CHECK_THAT(buf, Catch::StartsWith("GET https://trafficserver.apache.org/index.html HTTP/1.1\r\n"
                                      "Host: trafficserver.apache.org\r\n"
                                      "User-Agent: foobar\r\n"
                                      "\r\n"));
  }

  SECTION("response")
  {
    const char response[] = "HTTP/1.1 200 OK\r\n"
                            "Connection: close\r\n"
                            "\r\n";

    HTTPHdr hdr_1;
    ts::PostScript hdr_1_defer([&]() -> void { hdr_1.destroy(); });
    hdr_1.create(HTTP_TYPE_RESPONSE);
    http2_init_pseudo_headers(hdr_1);

    // parse
    const char *start = response;
    const char *end   = response + sizeof(response) - 1;
    hdr_1.parse_resp(&parser, &start, end, true);

    // convert to HTTP/2
    http2_convert_header_from_1_1_to_2(&hdr_1);

    // check pseudo headers
    // :status
    {
      MIMEField *f = hdr_1.field_find(HTTP2_VALUE_STATUS, HTTP2_LEN_STATUS);
      REQUIRE(f != nullptr);
      std::string_view v = f->value_get();
      CHECK(v == "200");
    }

    // no connection header
    {
      MIMEField *f = hdr_1.field_find(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);
      CHECK(f == nullptr);
    }

    // convert to HTTP/1.1
    HTTPHdr hdr_2;
    ts::PostScript hdr_2_defer([&]() -> void { hdr_2.destroy(); });
    hdr_2.create(HTTP_TYPE_REQUEST);
    hdr_2.copy(&hdr_1);

    http2_convert_header_from_2_to_1_1(&hdr_2);

    // dump
    char buf[128]  = {0};
    int bufindex   = 0;
    int dumpoffset = 0;

    hdr_2.print(buf, sizeof(buf), &bufindex, &dumpoffset);

    // check
    REQUIRE(bufindex > 0);
    CHECK_THAT(buf, Catch::StartsWith("HTTP/1.1 200 OK\r\n\r\n"));
  }
}
