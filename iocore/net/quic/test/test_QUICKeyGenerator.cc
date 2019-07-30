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
TEST_CASE("draft-17 Test Vectors", "[quic]")
{
  SECTION("CLIENT Initial")
  {
    QUICKeyGenerator keygen(QUICKeyGenerator::Context::CLIENT);

    QUICConnectionId cid = {reinterpret_cast<const uint8_t *>("\xc6\x54\xef\xd8\xa3\x1b\x47\x92"), 8};

    uint8_t expected_client_key[] = {
      0x86, 0xd1, 0x83, 0x04, 0x80, 0xb4, 0x0f, 0x86, 0xcf, 0x9d, 0x68, 0xdc, 0xad, 0xf3, 0x5d, 0xfe,
    };
    uint8_t expected_client_iv[] = {
      0x12, 0xf3, 0x93, 0x8a, 0xca, 0x34, 0xaa, 0x02, 0x54, 0x31, 0x63, 0xd4,
    };
    uint8_t expected_client_hp[] = {
      0xcd, 0x25, 0x3a, 0x36, 0xff, 0x93, 0x93, 0x7c, 0x46, 0x93, 0x84, 0xa8, 0x23, 0xaf, 0x6c, 0x56,
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
      0x2c, 0x78, 0x63, 0x3e, 0x20, 0x6e, 0x99, 0xad, 0x25, 0x19, 0x64, 0xf1, 0x9f, 0x6d, 0xcd, 0x6d,
    };
    uint8_t expected_server_iv[] = {
      0x7b, 0x50, 0xbf, 0x36, 0x98, 0xa0, 0x6d, 0xfa, 0xbf, 0x75, 0xf2, 0x87,
    };
    uint8_t expected_server_hp[] = {
      0x25, 0x79, 0xd8, 0x69, 0x6f, 0x85, 0xed, 0xa6, 0x8d, 0x35, 0x02, 0xb6, 0x55, 0x96, 0x58, 0x6b,
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
