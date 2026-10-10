/** @file

  Catch based unit tests for parsing extension types from a raw ClientHello.

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

#include "tscore/ink_inet.h"
#include "iocore/net/TLSSNISupport.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace
{

struct Extension {
  uint16_t             type;
  std::vector<uint8_t> data;
};

void
put_u16(std::vector<uint8_t> &out, size_t value)
{
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value));
}

std::vector<uint8_t>
make_client_hello_body(std::vector<Extension> const &extensions, bool with_extensions_block = true)
{
  std::vector<uint8_t> body;

  put_u16(body, 0x0303);
  body.insert(body.end(), 32, 0xAB);

  // A 32 byte legacy session id.
  body.push_back(32);
  body.insert(body.end(), 32, 0xCD);

  // Cipher suites: GREASE and TLS_AES_128_GCM_SHA256.
  put_u16(body, 4);
  put_u16(body, 0x0A0A);
  put_u16(body, 0x1301);

  // The null compression method.
  body.push_back(1);
  body.push_back(0);

  if (with_extensions_block) {
    std::vector<uint8_t> block;
    for (auto const &ext : extensions) {
      put_u16(block, ext.type);
      put_u16(block, ext.data.size());
      block.insert(block.end(), ext.data.begin(), ext.data.end());
    }
    put_u16(body, block.size());
    body.insert(body.end(), block.begin(), block.end());
  }
  return body;
}

std::vector<uint8_t>
make_handshake_message(std::vector<uint8_t> const &body)
{
  std::vector<uint8_t> msg{SSL3_MT_CLIENT_HELLO, static_cast<uint8_t>(body.size() >> 16), static_cast<uint8_t>(body.size() >> 8),
                           static_cast<uint8_t>(body.size())};
  msg.insert(msg.end(), body.begin(), body.end());
  return msg;
}

std::vector<int>
parse(std::vector<uint8_t> const &msg, bool expect_success = true)
{
  std::vector<int> types{-1};
  bool const       parsed = TLSSNISupport::parse_client_hello_extension_types(msg.data(), msg.size(), types);
  REQUIRE(parsed == expect_success);
  return types;
}

} // namespace

TEST_CASE("Raw ClientHello extension types are listed in wire order", "[TLSSNISupport]")
{
  // GREASE, server_name, ALPS (0x4469), ECH (0xfe0d), an empty extension, and GREASE again.
  std::vector<Extension> const extensions{
    {0x1A1A, {}                     },
    {0x0000, {0x00, 0x00}           },
    {0x4469, {0x00, 0x03, 0x02, 'h'}},
    {0xFE0D, {0x01, 0x02, 0x03}     },
    {0x0017, {}                     },
    {0xCACA, {0x00}                 },
  };

  auto const types = parse(make_handshake_message(make_client_hello_body(extensions)));
  CHECK(types == std::vector<int>{0x1A1A, 0x0000, 0x4469, 0xFE0D, 0x0017, 0xCACA});
}

TEST_CASE("Raw ClientHello duplicate extension types are preserved", "[TLSSNISupport]")
{
  auto const types = parse(make_handshake_message(make_client_hello_body({
    {0x000A, {}},
    {0x000A, {}},
  })));
  CHECK(types == std::vector<int>{0x000A, 0x000A});
}

TEST_CASE("Raw ClientHello without an extensions block", "[TLSSNISupport]")
{
  auto const types = parse(make_handshake_message(make_client_hello_body({}, false)));
  CHECK(types.empty());
}

TEST_CASE("Raw ClientHello with an empty extensions block", "[TLSSNISupport]")
{
  auto const types = parse(make_handshake_message(make_client_hello_body({})));
  CHECK(types.empty());
}

TEST_CASE("Malformed raw ClientHellos are rejected", "[TLSSNISupport]")
{
  std::vector<Extension> const extensions{
    {0x0000, {0x00, 0x00}      },
    {0x002B, {0x02, 0x03, 0x04}},
  };
  auto const good = make_handshake_message(make_client_hello_body(extensions));

  SECTION("empty message")
  {
    CHECK(parse({}, false).empty());
  }

  SECTION("not a ClientHello")
  {
    auto msg = good;
    msg[0]   = SSL3_MT_SERVER_HELLO;
    CHECK(parse(msg, false).empty());
  }

  SECTION("handshake length longer than the message")
  {
    auto msg = good;
    msg.pop_back();
    CHECK(parse(msg, false).empty());
  }

  SECTION("truncated before the extensions block")
  {
    auto body = make_client_hello_body({}, false);
    body.resize(30);
    CHECK(parse(make_handshake_message(body), false).empty());
  }

  SECTION("extension data longer than the extensions block")
  {
    auto body = make_client_hello_body(extensions);
    // Grow the last extension's declared length by one byte.
    body[body.size() - 4] += 1;
    CHECK(parse(make_handshake_message(body), false).empty());
  }

  SECTION("extensions block length disagrees with the handshake length")
  {
    auto body = make_client_hello_body(extensions);
    body.push_back(0);
    CHECK(parse(make_handshake_message(body), false).empty());
  }

  SECTION("truncated extension header")
  {
    auto body = make_client_hello_body(extensions);
    // Append two bytes of a third extension header and fix the block length.
    body.push_back(0x00);
    body.push_back(0x10);
    size_t const block_len_offset = 2 + 32 + 1 + 32 + 2 + 4 + 1 + 1;
    size_t const block_len        = (body[block_len_offset] << 8 | body[block_len_offset + 1]) + 2;
    body[block_len_offset]        = static_cast<uint8_t>(block_len >> 8);
    body[block_len_offset + 1]    = static_cast<uint8_t>(block_len);
    CHECK(parse(make_handshake_message(body), false).empty());
  }
}
