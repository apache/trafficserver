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

#include "proxy/http2/HTTP2.h"

#include "tsutil/PostScript.h"

TEST_CASE("Convert HTTPHdr", "[HTTP2]")
{
  url_init();
  mime_init();
  http_init();
  http2_init();

  HTTPParser     parser;
  ts::PostScript parser_defer([&]() -> void { http_parser_clear(&parser); });
  http_parser_init(&parser);

  SECTION("request")
  {
    const char request[] = "GET /index.html HTTP/1.1\r\n"
                           "Host: trafficserver.apache.org\r\n"
                           "User-Agent: foobar\r\n"
                           "\r\n";

    HTTPHdr        hdr_1;
    ts::PostScript hdr_1_defer([&]() -> void { hdr_1.destroy(); });
    hdr_1.create(HTTPType::REQUEST, HTTP_2_0);

    // parse
    const char *start = request;
    const char *end   = request + sizeof(request) - 1;
    hdr_1.parse_req(&parser, &start, end, true);

    // convert to HTTP/2
    http2_convert_header_from_1_1_to_2(&hdr_1);

    // check pseudo headers
    // :method
    {
      MIMEField *f = hdr_1.field_find(PSEUDO_HEADER_METHOD);
      REQUIRE(f != nullptr);
      std::string_view v = f->value_get();
      CHECK(v == "GET");
    }

    // :scheme
    {
      MIMEField *f = hdr_1.field_find(PSEUDO_HEADER_SCHEME);
      REQUIRE(f != nullptr);
      std::string_view v = f->value_get();
      CHECK(v == "https");
    }

    // :authority
    {
      MIMEField *f = hdr_1.field_find(PSEUDO_HEADER_AUTHORITY);
      REQUIRE(f != nullptr);
      std::string_view v = f->value_get();
      CHECK(v == "trafficserver.apache.org");
    }

    // :path
    {
      MIMEField *f = hdr_1.field_find(PSEUDO_HEADER_PATH);
      REQUIRE(f != nullptr);
      std::string_view v = f->value_get();
      CHECK(v == "/index.html");
    }

    // convert back to HTTP/1.1
    HTTPHdr        hdr_2;
    ts::PostScript hdr_2_defer([&]() -> void { hdr_2.destroy(); });
    hdr_2.create(HTTPType::REQUEST);
    hdr_2.copy(&hdr_1);

    http2_convert_header_from_2_to_1_1(&hdr_2);

    // dump
    char buf[1024]  = {0};
    int  bufindex   = 0;
    int  dumpoffset = 0;

    hdr_2.print(buf, sizeof(buf), &bufindex, &dumpoffset);

    // check
    CHECK_THAT(buf, Catch::StartsWith("GET https://trafficserver.apache.org/index.html HTTP/1.1\r\n"
                                      "Host: trafficserver.apache.org\r\n"
                                      "User-Agent: foobar\r\n"
                                      "\r\n"));

    // Verify that conversion from HTTP/2 to HTTP/1.1 works correctly when the
    // HTTP/2 request contains a Host header.
    HTTPHdr        hdr_2_with_host;
    ts::PostScript hdr_2_with_host_defer([&]() -> void { hdr_2_with_host.destroy(); });
    hdr_2_with_host.create(HTTPType::REQUEST);
    hdr_2_with_host.copy(&hdr_1);

    MIMEField *host = hdr_2_with_host.field_create(static_cast<std::string_view>(MIME_FIELD_HOST));
    hdr_2_with_host.field_attach(host);
    std::string_view host_value = "bogus.host.com";
    host->value_set(hdr_2_with_host.m_heap, hdr_2_with_host.m_mime, host_value);

    http2_convert_header_from_2_to_1_1(&hdr_2_with_host);

    // dump
    memset(buf, 0, sizeof(buf));
    bufindex   = 0;
    dumpoffset = 0;

    hdr_2_with_host.print(buf, sizeof(buf), &bufindex, &dumpoffset);

    // check: Note that the Host will now be at the end of the Headers since we
    // added it above and it will remain there, albeit with the updated value
    // from the :authority header.
    CHECK_THAT(buf, Catch::StartsWith("GET https://trafficserver.apache.org/index.html HTTP/1.1\r\n"
                                      "User-Agent: foobar\r\n"
                                      "Host: trafficserver.apache.org\r\n"
                                      "\r\n"));
  }

  SECTION("response")
  {
    const char response[] = "HTTP/1.1 200 OK\r\n"
                            "Connection: close\r\n"
                            "\r\n";

    HTTPHdr        hdr_1;
    ts::PostScript hdr_1_defer([&]() -> void { hdr_1.destroy(); });
    hdr_1.create(HTTPType::RESPONSE, HTTP_2_0);

    // parse
    const char *start = response;
    const char *end   = response + sizeof(response) - 1;
    hdr_1.parse_resp(&parser, &start, end, true);

    // convert to HTTP/2
    http2_convert_header_from_1_1_to_2(&hdr_1);

    // check pseudo headers
    // :status
    {
      MIMEField *f = hdr_1.field_find(PSEUDO_HEADER_STATUS);
      REQUIRE(f != nullptr);
      std::string_view v = f->value_get();
      CHECK(v == "200");
    }

    // no connection header
    {
      MIMEField *f = hdr_1.field_find(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
      CHECK(f == nullptr);
    }

    // convert to HTTP/1.1
    HTTPHdr        hdr_2;
    ts::PostScript hdr_2_defer([&]() -> void { hdr_2.destroy(); });
    hdr_2.create(HTTPType::REQUEST);
    hdr_2.copy(&hdr_1);

    http2_convert_header_from_2_to_1_1(&hdr_2);

    // dump
    char buf[1024]  = {0};
    int  bufindex   = 0;
    int  dumpoffset = 0;

    hdr_2.print(buf, sizeof(buf), &bufindex, &dumpoffset);

    // check
    REQUIRE(bufindex > 0);
    CHECK_THAT(buf, Catch::StartsWith("HTTP/1.1 200 OK\r\n\r\n"));
  }
}
