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
#include <iomanip>

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/base.h>
#endif

#include <openssl/ssl.h>

// #include "Mock.h"
#include "QUICKeyGenerator.h"

TEST_CASE("QUICKeyGenerator", "[quic]")
{
  SECTION("CLIENT Initial")
  {
    QUICKeyGenerator keygen(QUICKeyGenerator::Context::CLIENT);

    QUICConnectionId cid = {reinterpret_cast<const uint8_t *>("\x06\xb8\x58\xec\x6f\x80\x45\x2b"), 8};

    uint8_t expected_client_key[] = {
      0xa7, 0x99, 0x43, 0x56, 0x6c, 0x41, 0x34, 0x2f, 0x2b, 0xc3, 0xde, 0x6b, 0x7c, 0x15, 0x39, 0xdf,
    };
    uint8_t expected_client_iv[] = {
      0x84, 0xeb, 0x95, 0x4f, 0xfe, 0x16, 0x1c, 0x38, 0x75, 0x91, 0x9f, 0x5f,
    };
    uint8_t expected_client_pn[] = {
      0x5c, 0x0f, 0x64, 0x72, 0xa1, 0x56, 0x58, 0x04, 0x7a, 0x3c, 0xc1, 0xf1, 0x54, 0x78, 0xdc, 0xf4,
    };

    std::unique_ptr<KeyMaterial> actual_km = keygen.generate(cid);

    CHECK(actual_km->key_len == sizeof(expected_client_key));
    CHECK(memcmp(actual_km->key, expected_client_key, sizeof(expected_client_key)) == 0);
    CHECK(actual_km->iv_len == sizeof(expected_client_iv));
    CHECK(memcmp(actual_km->iv, expected_client_iv, sizeof(expected_client_iv)) == 0);
  }

  SECTION("SERVER Initial")
  {
    QUICKeyGenerator keygen(QUICKeyGenerator::Context::SERVER);

    QUICConnectionId cid = {reinterpret_cast<const uint8_t *>("\x06\xb8\x58\xec\x6f\x80\x45\x2b"), 8};

    uint8_t expected_server_key[] = {
      0x26, 0x08, 0x0e, 0x60, 0xd2, 0x88, 0xdb, 0x7d, 0xf8, 0x16, 0xa1, 0xcb, 0x0b, 0xc6, 0xc7, 0xf4,
    };
    uint8_t expected_server_iv[] = {
      0xb9, 0xfd, 0xc5, 0xb4, 0x48, 0xaf, 0x3e, 0x02, 0x34, 0x22, 0x44, 0x3b,
    };
    uint8_t expected_server_pn[] = {
      0x00, 0xba, 0xbb, 0xe1, 0xbe, 0x0f, 0x0c, 0x66, 0x18, 0x18, 0x8b, 0x4f, 0xcc, 0xa5, 0x7a, 0x96,
    };

    std::unique_ptr<KeyMaterial> actual_km = keygen.generate(cid);

    CHECK(actual_km->key_len == sizeof(expected_server_key));
    CHECK(memcmp(actual_km->key, expected_server_key, sizeof(expected_server_key)) == 0);
    CHECK(actual_km->iv_len == sizeof(expected_server_iv));
    CHECK(memcmp(actual_km->iv, expected_server_iv, sizeof(expected_server_iv)) == 0);
  }
}
