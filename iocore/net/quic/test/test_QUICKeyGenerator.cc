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

#include "QUICKeyGenerator.h"
#include "QUICPacketProtectionKeyInfo.h"

// https://github.com/quicwg/base-drafts/wiki/Test-Vector-for-the-Clear-Text-AEAD-key-derivation
TEST_CASE("draft-21 Test Vectors", "[quic]")
{
  SECTION("CLIENT Initial")
  {
    QUICKeyGenerator keygen(QUICKeyGenerator::Context::CLIENT);

    QUICConnectionId cid = {reinterpret_cast<const uint8_t *>("\xc6\x54\xef\xd8\xa3\x1b\x47\x92"), 8};

    uint8_t expected_client_key[] = {
      0xd4, 0xe4, 0x3d, 0x22, 0x68, 0xf8, 0xe4, 0x3b, 0xab, 0x1c, 0xa6, 0x7a, 0x36, 0x80, 0x46, 0x0f,
    };
    uint8_t expected_client_iv[] = {
      0x67, 0x1f, 0x1c, 0x3d, 0x21, 0xde, 0x47, 0xff, 0x01, 0x8b, 0x11, 0x3b,
    };
    uint8_t expected_client_hp[] = {
      0xed, 0x6c, 0x63, 0x14, 0xdd, 0xc8, 0x69, 0xa5, 0x94, 0x19, 0x74, 0x42, 0x87, 0x71, 0x39, 0x83,
    };

    QUICPacketProtectionKeyInfo pp_key_info;
    pp_key_info.set_cipher_initial(EVP_aes_128_gcm());
    pp_key_info.set_cipher_for_hp_initial(EVP_aes_128_ecb());
    keygen.generate(pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL), pp_key_info.encryption_key(QUICKeyPhase::INITIAL),
                    pp_key_info.encryption_iv(QUICKeyPhase::INITIAL), pp_key_info.encryption_iv_len(QUICKeyPhase::INITIAL), cid);

    CHECK(pp_key_info.encryption_key_len(QUICKeyPhase::INITIAL) == sizeof(expected_client_key));
    CHECK(memcmp(pp_key_info.encryption_key(QUICKeyPhase::INITIAL), expected_client_key, sizeof(expected_client_key)) == 0);
    CHECK(*pp_key_info.encryption_iv_len(QUICKeyPhase::INITIAL) == sizeof(expected_client_iv));
    CHECK(memcmp(pp_key_info.encryption_iv(QUICKeyPhase::INITIAL), expected_client_iv, sizeof(expected_client_iv)) == 0);
    CHECK(pp_key_info.encryption_key_for_hp_len(QUICKeyPhase::INITIAL) == sizeof(expected_client_hp));
    CHECK(memcmp(pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL), expected_client_hp, sizeof(expected_client_hp)) == 0);
  }

  SECTION("SERVER Initial")
  {
    QUICKeyGenerator keygen(QUICKeyGenerator::Context::SERVER);

    QUICConnectionId cid = {reinterpret_cast<const uint8_t *>("\xc6\x54\xef\xd8\xa3\x1b\x47\x92"), 8};

    uint8_t expected_server_key[] = {
      0x9d, 0xa3, 0x3b, 0xa0, 0x27, 0x46, 0xa3, 0xd3, 0x58, 0x12, 0x89, 0xc0, 0x19, 0x9c, 0x3a, 0xf2,
    };
    uint8_t expected_server_iv[] = {
      0xe6, 0x9c, 0x4e, 0xaf, 0xce, 0x11, 0x3d, 0xb5, 0x70, 0xb9, 0x4c, 0x0c,
    };
    uint8_t expected_server_hp[] = {
      0xc5, 0x0f, 0x34, 0x99, 0x5b, 0x8a, 0xa7, 0x16, 0x08, 0x7b, 0x64, 0x87, 0x6e, 0xdd, 0x68, 0x38,
    };

    QUICPacketProtectionKeyInfo pp_key_info;
    pp_key_info.set_cipher_initial(EVP_aes_128_gcm());
    pp_key_info.set_cipher_for_hp_initial(EVP_aes_128_ecb());
    keygen.generate(pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL), pp_key_info.encryption_key(QUICKeyPhase::INITIAL),
                    pp_key_info.encryption_iv(QUICKeyPhase::INITIAL), pp_key_info.encryption_iv_len(QUICKeyPhase::INITIAL), cid);

    CHECK(pp_key_info.encryption_key_len(QUICKeyPhase::INITIAL) == sizeof(expected_server_key));
    CHECK(memcmp(pp_key_info.encryption_key(QUICKeyPhase::INITIAL), expected_server_key, sizeof(expected_server_key)) == 0);
    CHECK(*pp_key_info.encryption_iv_len(QUICKeyPhase::INITIAL) == sizeof(expected_server_iv));
    CHECK(memcmp(pp_key_info.encryption_iv(QUICKeyPhase::INITIAL), expected_server_iv, sizeof(expected_server_iv)) == 0);
    CHECK(pp_key_info.encryption_key_for_hp_len(QUICKeyPhase::INITIAL) == sizeof(expected_server_hp));
    CHECK(memcmp(pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL), expected_server_hp, sizeof(expected_server_hp)) == 0);
  }
}
