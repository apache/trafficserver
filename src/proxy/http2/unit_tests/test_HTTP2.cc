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

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "proxy/http2/HTTP2.h"
#include "iocore/eventsystem/IOBuffer.h"

#include "tsutil/PostScript.h"

#include <string>

TEST_CASE("Convert HTTPHdr", "[HTTP2]")
{
  HTTPParser     parser;
  ts::PostScript parser_defer([&]() -> void { http_parser_clear(&parser); });
  http_parser_init(&parser);

  auto add_field = [](HTTPHdr &hdr, std::string_view name, std::string_view value) {
    MIMEField *f = hdr.field_create(name);

    hdr.field_attach(f);
    f->value_set(hdr.m_heap, hdr.m_mime, value);
  };

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
    CHECK_THAT(buf, Catch::Matchers::StartsWith("GET https://trafficserver.apache.org/index.html HTTP/1.1\r\n"
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
    CHECK_THAT(buf, Catch::Matchers::StartsWith("GET https://trafficserver.apache.org/index.html HTTP/1.1\r\n"
                                                "User-Agent: foobar\r\n"
                                                "Host: trafficserver.apache.org\r\n"
                                                "\r\n"));
  }

  SECTION("CONNECT request")
  {
    HTTPHdr        hdr;
    ts::PostScript hdr_defer([&]() -> void { hdr.destroy(); });
    hdr.create(HTTPType::REQUEST);

    add_field(hdr, PSEUDO_HEADER_METHOD, "CONNECT");
    add_field(hdr, PSEUDO_HEADER_AUTHORITY, "www.example.com:443");
    add_field(hdr, "uuid", "connect");

    REQUIRE(http2_convert_header_from_2_to_1_1(&hdr) == ParseResult::DONE);
    CHECK(hdr.method_get() == "CONNECT");
    CHECK(hdr.url_get()->host_get() == "www.example.com");
    CHECK(hdr.url_get()->port_get() == 443);
    CHECK(hdr.field_find(PSEUDO_HEADER_AUTHORITY) == nullptr);
    CHECK(hdr.field_find("uuid") != nullptr);
  }

  SECTION("reject empty :authority")
  {
    HTTPHdr        hdr;
    ts::PostScript hdr_defer([&]() -> void { hdr.destroy(); });
    hdr.create(HTTPType::REQUEST);

    add_field(hdr, PSEUDO_HEADER_METHOD, "GET");
    add_field(hdr, PSEUDO_HEADER_SCHEME, "https");
    add_field(hdr, PSEUDO_HEADER_AUTHORITY, "");
    add_field(hdr, PSEUDO_HEADER_PATH, "/");

    CHECK(http2_convert_header_from_2_to_1_1(&hdr) == ParseResult::ERROR);
  }

  SECTION("reject CRLF in header value")
  {
    const char request[] = "GET /index.html HTTP/1.1\r\n"
                           "Host: trafficserver.apache.org\r\n"
                           "User-Agent: foobar\r\n"
                           "\r\n";

    HTTPHdr        hdr;
    ts::PostScript hdr_defer([&]() -> void { hdr.destroy(); });
    hdr.create(HTTPType::REQUEST, HTTP_2_0);

    const char *start = request;
    const char *end   = request + sizeof(request) - 1;
    hdr.parse_req(&parser, &start, end, true);
    http2_convert_header_from_1_1_to_2(&hdr);

    MIMEField *evil = hdr.field_create("x-injected");
    hdr.field_attach(evil);
    evil->value_set(hdr.m_heap, hdr.m_mime, std::string_view{"safe\r\ninjected: evil"});

    HTTPHdr        hdr_out;
    ts::PostScript hdr_out_defer([&]() -> void { hdr_out.destroy(); });
    hdr_out.create(HTTPType::REQUEST);
    hdr_out.copy(&hdr);

    CHECK(http2_convert_header_from_2_to_1_1(&hdr_out) == ParseResult::ERROR);
  }

  SECTION("reject bare CR in header value")
  {
    const char request[] = "GET /index.html HTTP/1.1\r\n"
                           "Host: trafficserver.apache.org\r\n"
                           "\r\n";

    HTTPHdr        hdr;
    ts::PostScript hdr_defer([&]() -> void { hdr.destroy(); });
    hdr.create(HTTPType::REQUEST, HTTP_2_0);

    const char *start = request;
    const char *end   = request + sizeof(request) - 1;
    hdr.parse_req(&parser, &start, end, true);
    http2_convert_header_from_1_1_to_2(&hdr);

    MIMEField *evil = hdr.field_create("x-injected");
    hdr.field_attach(evil);
    evil->value_set(hdr.m_heap, hdr.m_mime, std::string_view{"before\rafter"});

    HTTPHdr        hdr_out;
    ts::PostScript hdr_out_defer([&]() -> void { hdr_out.destroy(); });
    hdr_out.create(HTTPType::REQUEST);
    hdr_out.copy(&hdr);

    CHECK(http2_convert_header_from_2_to_1_1(&hdr_out) == ParseResult::ERROR);
  }

  SECTION("reject bare LF in header value")
  {
    const char request[] = "GET /index.html HTTP/1.1\r\n"
                           "Host: trafficserver.apache.org\r\n"
                           "\r\n";

    HTTPHdr        hdr;
    ts::PostScript hdr_defer([&]() -> void { hdr.destroy(); });
    hdr.create(HTTPType::REQUEST, HTTP_2_0);

    const char *start = request;
    const char *end   = request + sizeof(request) - 1;
    hdr.parse_req(&parser, &start, end, true);
    http2_convert_header_from_1_1_to_2(&hdr);

    MIMEField *evil = hdr.field_create("x-injected");
    hdr.field_attach(evil);
    evil->value_set(hdr.m_heap, hdr.m_mime, std::string_view{"before\nafter"});

    HTTPHdr        hdr_out;
    ts::PostScript hdr_out_defer([&]() -> void { hdr_out.destroy(); });
    hdr_out.create(HTTPType::REQUEST);
    hdr_out.copy(&hdr);

    CHECK(http2_convert_header_from_2_to_1_1(&hdr_out) == ParseResult::ERROR);
  }

  SECTION("accept clean header value")
  {
    const char request[] = "GET /index.html HTTP/1.1\r\n"
                           "Host: trafficserver.apache.org\r\n"
                           "\r\n";

    HTTPHdr        hdr;
    ts::PostScript hdr_defer([&]() -> void { hdr.destroy(); });
    hdr.create(HTTPType::REQUEST, HTTP_2_0);

    const char *start = request;
    const char *end   = request + sizeof(request) - 1;
    hdr.parse_req(&parser, &start, end, true);
    http2_convert_header_from_1_1_to_2(&hdr);

    MIMEField *clean = hdr.field_create("x-clean");
    hdr.field_attach(clean);
    clean->value_set(hdr.m_heap, hdr.m_mime, std::string_view{"perfectly-fine-value"});

    HTTPHdr        hdr_out;
    ts::PostScript hdr_out_defer([&]() -> void { hdr_out.destroy(); });
    hdr_out.create(HTTPType::REQUEST);
    hdr_out.copy(&hdr);

    CHECK(http2_convert_header_from_2_to_1_1(&hdr_out) == ParseResult::DONE);
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
    CHECK_THAT(buf, Catch::Matchers::StartsWith("HTTP/1.1 200 OK\r\n\r\n"));
  }
}

// Regression: Http2ConnectionState::rcv_continuation_frame accumulates
// the size of every CONTINUATION payload into stream->header_blocks_length, a uint32_t.
// Before the fix, the increment was performed without overflow checking, so a crafted
// sequence of CONTINUATION frames whose payloads sum to more than UINT32_MAX would
// wrap the accumulator, and the subsequent ats_realloc would allocate a buffer smaller
// than the pre-wrap offset that memcpy then writes to.
TEST_CASE("CONTINUATION header_blocks_length overflow guard", "[HTTP2]")
{
  SECTION("zero accumulator and zero payload do not overflow")
  {
    CHECK_FALSE(http2_continuation_length_would_overflow(0u, 0u));
  }

  SECTION("small additions do not overflow")
  {
    CHECK_FALSE(http2_continuation_length_would_overflow(0u, 16384u));
    CHECK_FALSE(http2_continuation_length_would_overflow(16384u, 16384u));
    CHECK_FALSE(http2_continuation_length_would_overflow(1u << 20, 1u << 20));
  }

  SECTION("sum that exactly fills uint32_t is allowed")
  {
    CHECK_FALSE(http2_continuation_length_would_overflow(UINT32_MAX, 0u));
    CHECK_FALSE(http2_continuation_length_would_overflow(0u, UINT32_MAX));
    CHECK_FALSE(http2_continuation_length_would_overflow(UINT32_MAX - 1u, 1u));
    CHECK_FALSE(http2_continuation_length_would_overflow(1u, UINT32_MAX - 1u));
  }

  SECTION("sum exceeding uint32_t by one wraps and must be rejected")
  {
    CHECK(http2_continuation_length_would_overflow(UINT32_MAX, 1u));
    CHECK(http2_continuation_length_would_overflow(1u, UINT32_MAX));
  }

  SECTION("realistic attack shape: prior bytes plus a max HTTP/2 frame payload")
  {
    constexpr uint32_t max_frame_payload = (1u << 24) - 1u;
    CHECK(http2_continuation_length_would_overflow(UINT32_MAX - max_frame_payload + 1u, max_frame_payload));
    CHECK_FALSE(http2_continuation_length_would_overflow(UINT32_MAX - max_frame_payload, max_frame_payload));
  }
}

namespace
{
// Print a header into a buffer of 4 KB blocks the same way Http2Stream::send_headers
// does, so that a header larger than one block is split across blocks.
void
print_into_blocks(HTTPHdr &hdr, MIOBuffer *buffer)
{
  int bufindex;
  int dumpoffset = 0;
  int done, tmp;

  do {
    bufindex             = 0;
    tmp                  = dumpoffset;
    IOBufferBlock *block = buffer->get_current_block();
    if (!block) {
      buffer->add_block();
      block = buffer->get_current_block();
    }
    done        = hdr.print(block->end(), block->write_avail(), &bufindex, &tmp);
    dumpoffset += bufindex;
    buffer->fill(bufindex);
    if (!done) {
      buffer->add_block();
    }
  } while (!done);
}
} // namespace

TEST_CASE("Parse a header split across IOBuffer blocks at EOF", "[HTTP2]")
{
  // A bodyless HTTP/2 response is handed to the HttpSM along with EOS, so it is
  // parsed with eof set. Every field must still parse when the printed header
  // spans several blocks and a field is cut at a block boundary.
  std::string const large_value(6000, 'v');
  std::string const pad_value(400, 'p');

  HTTPParser     parser;
  ts::PostScript parser_defer([&]() -> void { http_parser_clear(&parser); });
  http_parser_init(&parser);

  MIOBuffer      *buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  ts::PostScript  buffer_defer([&]() -> void { free_MIOBuffer(buffer); });
  IOBufferReader *reader = buffer->alloc_reader();

  SECTION("response")
  {
    std::string text = "HTTP/1.1 302 Found\r\n"
                       "Location: https://example.com/landing\r\n";
    for (int i = 0; i < 3; ++i) {
      text += "X-Pad-" + std::to_string(i) + ": " + pad_value + "\r\n";
    }
    text += "Content-Security-Policy: " + large_value + "\r\n";
    for (int i = 3; i < 6; ++i) {
      text += "X-Pad-" + std::to_string(i) + ": " + pad_value + "\r\n";
    }
    text += "Content-Length: 0\r\n\r\n";

    HTTPHdr        origin;
    ts::PostScript origin_defer([&]() -> void { origin.destroy(); });
    origin.create(HTTPType::RESPONSE);
    char const *start = text.data();
    REQUIRE(origin.parse_resp(&parser, &start, text.data() + text.size(), true) == ParseResult::DONE);
    http_parser_clear(&parser);
    http_parser_init(&parser);

    print_into_blocks(origin, buffer);
    REQUIRE(reader->read_avail() == static_cast<int64_t>(text.size()));
    REQUIRE(reader->block_read_avail() < reader->read_avail());

    HTTPHdr        parsed;
    ts::PostScript parsed_defer([&]() -> void { parsed.destroy(); });
    parsed.create(HTTPType::RESPONSE);
    int bytes_used = 0;
    REQUIRE(parsed.parse_resp(&parser, reader, &bytes_used, true) == ParseResult::DONE);
    CHECK(bytes_used == static_cast<int>(text.size()));
    CHECK(parsed.status_get() == HTTPStatus::MOVED_TEMPORARILY);
    CHECK(parsed.fields_count() == 9);
    CHECK(parsed.value_get("Content-Security-Policy"sv) == large_value);
    CHECK(parsed.value_get("X-Pad-5"sv) == pad_value);
  }

  SECTION("field ends at a block boundary")
  {
    std::string const prefix = "HTTP/1.1 204 No Content\r\nX-Pad: ";
    std::string       text   = prefix + std::string(4096 - prefix.size() - 2, 'p') + "\r\n";
    REQUIRE(text.size() == 4096);
    text += "X-Next: next\r\n\r\n";

    HTTPHdr        origin;
    ts::PostScript origin_defer([&]() -> void { origin.destroy(); });
    origin.create(HTTPType::RESPONSE);
    char const *start = text.data();
    REQUIRE(origin.parse_resp(&parser, &start, text.data() + text.size(), true) == ParseResult::DONE);
    http_parser_clear(&parser);
    http_parser_init(&parser);

    print_into_blocks(origin, buffer);
    REQUIRE(reader->read_avail() == static_cast<int64_t>(text.size()));
    REQUIRE(reader->block_read_avail() == 4096);

    HTTPHdr        parsed;
    ts::PostScript parsed_defer([&]() -> void { parsed.destroy(); });
    parsed.create(HTTPType::RESPONSE);
    int bytes_used = 0;
    REQUIRE(parsed.parse_resp(&parser, reader, &bytes_used, true) == ParseResult::DONE);
    CHECK(bytes_used == static_cast<int>(text.size()));
    CHECK(parsed.value_get("X-Next"sv) == "next");
  }

  SECTION("request")
  {
    std::string text  = "GET /index.html HTTP/1.1\r\n"
                        "Host: example.com\r\n";
    text             += "Cookie: " + large_value + "\r\n";
    text             += "X-Pad: " + pad_value + "\r\n\r\n";

    HTTPHdr        client;
    ts::PostScript client_defer([&]() -> void { client.destroy(); });
    client.create(HTTPType::REQUEST);
    char const *start = text.data();
    REQUIRE(client.parse_req(&parser, &start, text.data() + text.size(), true) == ParseResult::DONE);
    http_parser_clear(&parser);
    http_parser_init(&parser);

    print_into_blocks(client, buffer);
    REQUIRE(reader->read_avail() == static_cast<int64_t>(text.size()));
    REQUIRE(reader->block_read_avail() < reader->read_avail());

    HTTPHdr        parsed;
    ts::PostScript parsed_defer([&]() -> void { parsed.destroy(); });
    parsed.create(HTTPType::REQUEST);
    int bytes_used = 0;
    REQUIRE(parsed.parse_req(&parser, reader, &bytes_used, true) == ParseResult::DONE);
    CHECK(bytes_used == static_cast<int>(text.size()));
    CHECK(parsed.value_get("Cookie"sv) == large_value);
    CHECK(parsed.value_get("X-Pad"sv) == pad_value);
  }
}
