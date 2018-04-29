/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "catch.hpp"

#include <cstring>

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/base.h>
#endif

#include <openssl/ssl.h>

#include "Mock.h"
#include "QUICKeyGenerator.h"

void
print_hex(const uint8_t *v, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    std::cout << std::setw(2) << std::setfill('0') << std::hex << static_cast<uint32_t>(v[i]) << " ";

    if (i != 0 && (i + 1) % 32 == 0 && i != len - 1) {
      std::cout << std::endl;
    }
  }

  std::cout << std::endl;

  return;
}

TEST_CASE("QUICKeyGenerator", "[quic]")
{
  SECTION("CLIENT Cleartext")
  {
    QUICKeyGenerator keygen(QUICKeyGenerator::Context::CLIENT);

    QUICConnectionId cid          = 0x8394c8f03e515708;
    uint8_t expected_client_key[] = {0x3a, 0xd0, 0x54, 0x2c, 0x4a, 0x85, 0x84, 0x74,
                                     0x00, 0x63, 0x04, 0x9e, 0x3b, 0x3c, 0xaa, 0xb2};
    uint8_t expected_client_iv[]  = {0xd1, 0xfd, 0x26, 0x05, 0x42, 0x75, 0x3a, 0xba, 0x38, 0x58, 0x9b, 0xad};

    std::unique_ptr<KeyMaterial> actual_km = keygen.generate(cid);

    CHECK(actual_km->key_len == sizeof(expected_client_key));
    CHECK(memcmp(actual_km->key, expected_client_key, sizeof(expected_client_key)) == 0);
    CHECK(actual_km->iv_len == sizeof(expected_client_iv));
    CHECK(memcmp(actual_km->iv, expected_client_iv, sizeof(expected_client_iv)) == 0);
  }

  SECTION("SERVER Cleartext")
  {
    QUICKeyGenerator keygen(QUICKeyGenerator::Context::SERVER);

    QUICConnectionId cid          = 0x8394c8f03e515708;
    uint8_t expected_server_key[] = {0xbe, 0xe4, 0xc2, 0x4d, 0x2a, 0xf1, 0x33, 0x80,
                                     0xa9, 0xfa, 0x24, 0xa5, 0xe2, 0xba, 0x2c, 0xff};
    uint8_t expected_server_iv[]  = {0x25, 0xb5, 0x8e, 0x24, 0x6d, 0x9e, 0x7d, 0x5f, 0xfe, 0x43, 0x23, 0xfe};

    std::unique_ptr<KeyMaterial> actual_km = keygen.generate(cid);

    CHECK(actual_km->key_len == sizeof(expected_server_key));
    CHECK(memcmp(actual_km->key, expected_server_key, sizeof(expected_server_key)) == 0);
    CHECK(actual_km->iv_len == sizeof(expected_server_iv));
    CHECK(memcmp(actual_km->iv, expected_server_iv, sizeof(expected_server_iv)) == 0);
  }
}
