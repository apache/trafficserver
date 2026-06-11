/** @file

  Unit Tests for HttpTransactHeaders (copy_header_fields)

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

#include <string_view>

using namespace std::string_view_literals;

#include "tscore/Diags.h"
#include "tsutil/PostScript.h"
#include "tscore/ink_memory.h"

#include "proxy/http/HttpTransactHeaders.h"
#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/MIME.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("HttpTransactHeaders::copy_header_fields", "[http]")
{
  url_init();
  mime_init();
  http_init();

  // 1) Basic dynamic removal: Connection names X-Custom; X-Custom removed; Host kept; Connection absent
  SECTION("basic dynamic removal")
  {
    HTTPHdr        src;
    HTTPHdr        dst;
    ts::PostScript src_defer([&]() -> void { src.destroy(); });
    ts::PostScript dst_defer([&]() -> void { dst.destroy(); });

    MIMEField *field;

    src.create(HTTPType::REQUEST);
    // Host should be preserved
    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_HOST));
    src.field_attach(field);
    src.field_value_set(field, "example.com"sv);

    // Connection: X-Custom
    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
    src.field_attach(field);
    src.field_value_set(field, "X-Custom"sv);

    // X-Custom: should be removed after copy
    field = src.field_create("X-Custom"sv);
    src.field_attach(field);
    src.field_value_set(field, "secret"sv);

    HttpTransactHeaders::copy_header_fields(&src, &dst, false, 0);

    // Host remains
    auto *h = dst.field_find(static_cast<std::string_view>(MIME_FIELD_HOST));
    REQUIRE(h != nullptr);
    CHECK(std::string_view{h->value_get()} == "example.com"sv);

    // Connection header should be removed
    CHECK(dst.field_find(static_cast<std::string_view>(MIME_FIELD_CONNECTION)) == nullptr);

    // X-Custom should be removed (expected to fail before fix)
    CHECK(dst.field_find("X-Custom"sv) == nullptr);
  }

  // 2) Multiple Connection tokens: both named headers removed; Host kept
  SECTION("multiple Connection tokens")
  {
    HTTPHdr        src;
    HTTPHdr        dst;
    ts::PostScript src_defer([&]() -> void { src.destroy(); });
    ts::PostScript dst_defer([&]() -> void { dst.destroy(); });

    MIMEField *field;

    src.create(HTTPType::REQUEST);
    // Host should be preserved
    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_HOST));
    src.field_attach(field);
    src.field_value_set(field, "example.com"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
    src.field_attach(field);
    src.field_value_set(field, "X-Foo, X-Bar"sv);

    field = src.field_create("X-Foo"sv);
    src.field_attach(field);
    src.field_value_set(field, "a"sv);

    field = src.field_create("X-Bar"sv);
    src.field_attach(field);
    src.field_value_set(field, "b"sv);

    HttpTransactHeaders::copy_header_fields(&src, &dst, false, 0);

    // Host remains
    auto *h = dst.field_find(static_cast<std::string_view>(MIME_FIELD_HOST));
    REQUIRE(h != nullptr);
    CHECK(std::string_view{h->value_get()} == "example.com"sv);

    // X-Foo and X-Bar should be removed (expected to fail before fix)
    CHECK(dst.field_find("X-Foo"sv) == nullptr);
    CHECK(dst.field_find("X-Bar"sv) == nullptr);
  }

  // 3) Connection: TE with TE: trailers preserved
  SECTION("TE: trailers preserved when Connection names TE")
  {
    HTTPHdr        src;
    HTTPHdr        dst;
    ts::PostScript src_defer([&]() -> void { src.destroy(); });
    ts::PostScript dst_defer([&]() -> void { dst.destroy(); });

    MIMEField *field;

    src.create(HTTPType::REQUEST);
    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_HOST));
    src.field_attach(field);
    src.field_value_set(field, "example.com"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
    src.field_attach(field);
    src.field_value_set(field, "TE"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_TE));
    src.field_attach(field);
    src.field_value_set(field, "trailers"sv);

    HttpTransactHeaders::copy_header_fields(&src, &dst, false, 0);

    // TE: trailers must be preserved
    auto *te = dst.field_find(static_cast<std::string_view>(MIME_FIELD_TE));
    REQUIRE(te != nullptr);
    CHECK(std::string_view{te->value_get()} == "trailers"sv);
  }

  // 4) Nonexistent token name: no crash/assert, Host preserved
  SECTION("nonexistent token header is harmless")
  {
    HTTPHdr        src;
    HTTPHdr        dst;
    ts::PostScript src_defer([&]() -> void { src.destroy(); });
    ts::PostScript dst_defer([&]() -> void { dst.destroy(); });

    MIMEField *field;

    src.create(HTTPType::REQUEST);
    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_HOST));
    src.field_attach(field);
    src.field_value_set(field, "example.com"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
    src.field_attach(field);
    src.field_value_set(field, "X-Nonexistent"sv);

    HttpTransactHeaders::copy_header_fields(&src, &dst, false, 0);

    // Host should remain
    auto *h = dst.field_find(static_cast<std::string_view>(MIME_FIELD_HOST));
    REQUIRE(h != nullptr);
    CHECK(std::string_view{h->value_get()} == "example.com"sv);
  }

  // 5) Whitespace around tokens: tokenized names removed correctly
  SECTION("whitespace-trimmed token handling")
  {
    HTTPHdr        src;
    HTTPHdr        dst;
    ts::PostScript src_defer([&]() -> void { src.destroy(); });
    ts::PostScript dst_defer([&]() -> void { dst.destroy(); });

    MIMEField *field;

    src.create(HTTPType::REQUEST);
    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_HOST));
    src.field_attach(field);
    src.field_value_set(field, "example.com"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
    src.field_attach(field);
    src.field_value_set(field, "  X-Foo , X-Bar "sv);

    field = src.field_create("X-Foo"sv);
    src.field_attach(field);
    src.field_value_set(field, "a"sv);

    field = src.field_create("X-Bar"sv);
    src.field_attach(field);
    src.field_value_set(field, "b"sv);

    HttpTransactHeaders::copy_header_fields(&src, &dst, false, 0);

    CHECK(dst.field_find("X-Foo"sv) == nullptr);
    CHECK(dst.field_find("X-Bar"sv) == nullptr);
  }

  // 6) @-prefix protection: @TCPInfo preserved while normal token removed
  SECTION("@-prefix token protection while normal token is stripped")
  {
    HTTPHdr        src;
    HTTPHdr        dst;
    ts::PostScript src_defer([&]() -> void { src.destroy(); });
    ts::PostScript dst_defer([&]() -> void { dst.destroy(); });

    MIMEField *field;

    src.create(HTTPType::REQUEST);
    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_HOST));
    src.field_attach(field);
    src.field_value_set(field, "example.com"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
    src.field_attach(field);
    src.field_value_set(field, "@TCPInfo, X-Custom"sv);

    field = src.field_create("@TCPInfo"sv);
    src.field_attach(field);
    src.field_value_set(field, "TS; data"sv);

    field = src.field_create("X-Custom"sv);
    src.field_attach(field);
    src.field_value_set(field, "secret"sv);

    HttpTransactHeaders::copy_header_fields(&src, &dst, false, 0);

    REQUIRE(dst.field_find("Host") != nullptr);

    // @TCPInfo should remain
    auto *sfield = dst.field_find("@TCPInfo"sv);
    REQUIRE(sfield != nullptr);
    CHECK(std::string_view{sfield->value_get()} == "TS; data"sv);

    // X-Custom should be removed
    CHECK(dst.field_find("X-Custom"sv) == nullptr);
  }

  // 7) retain_proxy_auth_hdrs=true: Proxy-Authorization preserved when listed in Connection
  SECTION("retain_proxy_auth_hdrs preserves Proxy-Authorization listed in Connection")
  {
    HTTPHdr        src;
    HTTPHdr        dst;
    ts::PostScript src_defer([&]() -> void { src.destroy(); });
    ts::PostScript dst_defer([&]() -> void { dst.destroy(); });

    MIMEField *field;

    src.create(HTTPType::REQUEST);
    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_HOST));
    src.field_attach(field);
    src.field_value_set(field, "example.com"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
    src.field_attach(field);
    src.field_value_set(field, "Proxy-Authorization, X-Custom"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_PROXY_AUTHORIZATION));
    src.field_attach(field);
    src.field_value_set(field, "Basic dXNlcjpwYXNz"sv);

    field = src.field_create("X-Custom"sv);
    src.field_attach(field);
    src.field_value_set(field, "secret"sv);

    HttpTransactHeaders::copy_header_fields(&src, &dst, true, 0);

    // Proxy-Authorization preserved because retain_proxy_auth_hdrs=true
    auto *pa = dst.field_find(static_cast<std::string_view>(MIME_FIELD_PROXY_AUTHORIZATION));
    REQUIRE(pa != nullptr);
    CHECK(std::string_view{pa->value_get()} == "Basic dXNlcjpwYXNz"sv);

    // X-Custom still stripped
    CHECK(dst.field_find("X-Custom"sv) == nullptr);
  }

  // 8) retain_proxy_auth_hdrs=false: Proxy-Authorization stripped when listed in Connection
  SECTION("Proxy-Authorization stripped when retain_proxy_auth_hdrs is false")
  {
    HTTPHdr        src;
    HTTPHdr        dst;
    ts::PostScript src_defer([&]() -> void { src.destroy(); });
    ts::PostScript dst_defer([&]() -> void { dst.destroy(); });

    MIMEField *field;

    src.create(HTTPType::REQUEST);
    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_HOST));
    src.field_attach(field);
    src.field_value_set(field, "example.com"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
    src.field_attach(field);
    src.field_value_set(field, "Proxy-Authorization"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_PROXY_AUTHORIZATION));
    src.field_attach(field);
    src.field_value_set(field, "Basic dXNlcjpwYXNz"sv);

    HttpTransactHeaders::copy_header_fields(&src, &dst, false, 0);

    // Proxy-Authorization stripped because retain_proxy_auth_hdrs=false
    CHECK(dst.field_find(static_cast<std::string_view>(MIME_FIELD_PROXY_AUTHORIZATION)) == nullptr);
  }

  // 9) retain_proxy_auth_hdrs=true: Proxy-Authenticate preserved when listed in Connection
  SECTION("retain_proxy_auth_hdrs preserves Proxy-Authenticate listed in Connection")
  {
    HTTPHdr        src;
    HTTPHdr        dst;
    ts::PostScript src_defer([&]() -> void { src.destroy(); });
    ts::PostScript dst_defer([&]() -> void { dst.destroy(); });

    MIMEField *field;

    src.create(HTTPType::REQUEST);
    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_HOST));
    src.field_attach(field);
    src.field_value_set(field, "example.com"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
    src.field_attach(field);
    src.field_value_set(field, "Proxy-Authenticate"sv);

    field = src.field_create(static_cast<std::string_view>(MIME_FIELD_PROXY_AUTHENTICATE));
    src.field_attach(field);
    src.field_value_set(field, "Basic realm=\"proxy\""sv);

    HttpTransactHeaders::copy_header_fields(&src, &dst, true, 0);

    // Proxy-Authenticate preserved because retain_proxy_auth_hdrs=true
    auto *pa = dst.field_find(static_cast<std::string_view>(MIME_FIELD_PROXY_AUTHENTICATE));
    REQUIRE(pa != nullptr);
    CHECK(std::string_view{pa->value_get()} == "Basic realm=\"proxy\""sv);
  }
}
