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

#include <array>
#include <cstdint>
#include <memory>
#include <new>
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

// The lengths of the variable length fields that precede the extensions.
struct Lengths {
  size_t session_id          = 32;
  size_t cipher_suites       = 4;
  size_t compression_methods = 1;
};

std::vector<uint8_t>
make_client_hello_body(std::vector<Extension> const &extensions, bool with_extensions_block = true, Lengths lengths = {})
{
  std::vector<uint8_t> block;
  if (with_extensions_block) {
    for (auto const &ext : extensions) {
      put_u16(block, ext.type);
      put_u16(block, ext.data.size());
      block.insert(block.end(), ext.data.begin(), ext.data.end());
    }
  }

  // Reserve the whole body first: growing a two byte vector with insert()
  // trips a false out of bounds warning in GCC 11 and 12 at -O3.
  std::vector<uint8_t> body;
  body.reserve(2 + 32 + 1 + lengths.session_id + 2 + lengths.cipher_suites + 1 + lengths.compression_methods +
               (with_extensions_block ? 2 + block.size() : 0));

  put_u16(body, 0x0303);
  body.insert(body.end(), 32, 0xAB);

  body.push_back(static_cast<uint8_t>(lengths.session_id));
  body.insert(body.end(), lengths.session_id, 0xCD);

  // By default, GREASE and TLS_AES_128_GCM_SHA256.
  put_u16(body, lengths.cipher_suites);
  for (size_t i = 0; i < lengths.cipher_suites; ++i) {
    body.push_back(i < 2 ? 0x0A : (i % 2 == 0 ? 0x13 : 0x01));
  }

  // By default, the null compression method.
  body.push_back(static_cast<uint8_t>(lengths.compression_methods));
  body.insert(body.end(), lengths.compression_methods, 0);

  if (with_extensions_block) {
    put_u16(body, block.size());
    body.insert(body.end(), block.begin(), block.end());
  }
  return body;
}

std::vector<uint8_t>
make_handshake_message(std::vector<uint8_t> const &body)
{
  // Reserve the whole message first, for the same GCC warning as above.
  std::vector<uint8_t> msg;
  msg.reserve(4 + body.size());
  msg.push_back(SSL3_MT_CLIENT_HELLO);
  msg.push_back(static_cast<uint8_t>(body.size() >> 16));
  msg.push_back(static_cast<uint8_t>(body.size() >> 8));
  msg.push_back(static_cast<uint8_t>(body.size()));
  msg.insert(msg.end(), body.begin(), body.end());
  return msg;
}

// Parses @a msg the way on_client_hello_message() does when the extensions do
// not fit its fixed buffer: count first, then parse into exact storage.
std::vector<int>
parse(std::vector<uint8_t> const &msg, bool expect_success = true)
{
  auto const count = TLSSNISupport::parse_client_hello_extension_types(msg.data(), msg.size(), nullptr, 0);
  REQUIRE(count.has_value() == expect_success);
  if (!count) {
    return {};
  }

  std::vector<int> types(*count);
  REQUIRE(TLSSNISupport::parse_client_hello_extension_types(msg.data(), msg.size(), types.data(), types.size()) == count);
  return types;
}

class RecycledTLSSNISupport : public TLSSNISupport
{
public:
  using TLSSNISupport::_clear;
  using TLSSNISupport::_get_raw_extension_types;

protected:
  in_port_t
  _get_local_port() override
  {
    return 0;
  }
};

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

  SECTION("legacy session id longer than 32 bytes")
  {
    CHECK(parse(make_handshake_message(make_client_hello_body(extensions, true, {.session_id = 33})), false).empty());
  }

  SECTION("no cipher suites")
  {
    CHECK(parse(make_handshake_message(make_client_hello_body(extensions, true, {.cipher_suites = 0})), false).empty());
  }

  SECTION("odd cipher suites length")
  {
    CHECK(parse(make_handshake_message(make_client_hello_body(extensions, true, {.cipher_suites = 3})), false).empty());
  }

  SECTION("no compression methods")
  {
    CHECK(parse(make_handshake_message(make_client_hello_body(extensions, true, {.compression_methods = 0})), false).empty());
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

TEST_CASE("Raw ClientHello extension types beyond a short buffer are counted", "[TLSSNISupport]")
{
  auto const msg = make_handshake_message(make_client_hello_body({
    {0x000A, {}},
    {0x000B, {}},
    {0x000D, {}},
  }));

  std::array<int, 2> types{};
  CHECK(TLSSNISupport::parse_client_hello_extension_types(msg.data(), msg.size(), types.data(), types.size()) == 3);
  CHECK(types == std::array<int, 2>{0x000A, 0x000B});
}

TEST_CASE("_clear() releases the raw extension list of a recycled connection", "[TLSSNISupport]")
{
  // More than the 32 extensions the fixed buffer holds, so the list is on the heap.
  std::vector<Extension> extensions;
  for (uint16_t i = 0; i < 40; ++i) {
    extensions.push_back({static_cast<uint16_t>(0x0100 + i), {}});
  }
  auto const msg = make_handshake_message(make_client_hello_body(extensions));
  REQUIRE(parse(msg).size() == 40);

  // Recycle the object the way ClassAllocator<SSLNetVConnection, false> does: clear(),
  // no destructor, then placement new over the same memory. LeakSanitizer reports
  // the heap list if _clear() does not release it.
  void *storage = ::operator new(sizeof(RecycledTLSSNISupport), std::align_val_t{alignof(RecycledTLSSNISupport)});
  auto *snis    = ::new (storage) RecycledTLSSNISupport;
  for (int i = 0; i < 4; ++i) {
    snis->on_client_hello_message(msg.data(), msg.size());

    size_t     count = 0;
    auto const types = snis->_get_raw_extension_types(count);
    REQUIRE(types != nullptr);
    REQUIRE(count == extensions.size());
    for (size_t j = 0; j < count; ++j) {
      CHECK(types[j] == extensions[j].type);
    }

    snis->_clear();
    size_t cleared_count = 0;
    CHECK(snis->_get_raw_extension_types(cleared_count) == nullptr);
    CHECK(cleared_count == 0);
    snis = ::new (storage) RecycledTLSSNISupport;
  }
  std::destroy_at(snis);
  ::operator delete(storage, std::align_val_t{alignof(RecycledTLSSNISupport)});
}
