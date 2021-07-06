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

// https://github.com/quicwg/base-drafts/wiki/Test-Vector-for-the-Initial-AEAD-key-derivation
TEST_CASE("draft-23~27 Test Vectors", "[quic]")
{
  SECTION("CLIENT Initial")
  {
    QUICKeyGenerator keygen(QUICKeyGenerator::Context::CLIENT);

    QUICConnectionId cid = {reinterpret_cast<const uint8_t *>("\xc6\x54\xef\xd8\xa3\x1b\x47\x92"), 8};

    uint8_t expected_client_key[] = {0xfc, 0x4a, 0x14, 0x7a, 0x7e, 0xe9, 0x70, 0x29, 0x1b, 0x8f, 0x1c, 0x3, 0x2d, 0x2c, 0x40, 0xf9};
    uint8_t expected_client_iv[]  = {0x1e, 0x6a, 0x5d, 0xdb, 0x7c, 0x1d, 0x1a, 0xa7, 0xa0, 0xfd, 0x70, 0x5};
    uint8_t expected_client_hp[] = {0x43, 0x1d, 0x22, 0x82, 0xb4, 0x7b, 0xb9, 0x3f, 0xeb, 0xd2, 0xcf, 0x19, 0x85, 0x21, 0xe2, 0xbe};

    QUICPacketProtectionKeyInfo pp_key_info;
    pp_key_info.set_cipher_initial(EVP_aes_128_gcm());
    pp_key_info.set_cipher_for_hp_initial(EVP_aes_128_ecb());
    keygen.generate(0xff00001b, pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL),
                    pp_key_info.encryption_key(QUICKeyPhase::INITIAL), pp_key_info.encryption_iv(QUICKeyPhase::INITIAL),
                    pp_key_info.encryption_iv_len(QUICKeyPhase::INITIAL), cid);

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

    uint8_t expected_server_key[] = {0x60, 0xc0, 0x2f, 0xa6, 0x12, 0x1e, 0xb1, 0xab,
                                     0xa4, 0x35, 0x1f, 0x2a, 0x63, 0xb0, 0xac, 0xf8};
    uint8_t expected_server_iv[]  = {0x38, 0xd, 0xf3, 0xc0, 0xf2, 0x8d, 0x94, 0x7, 0x76, 0x5c, 0x55, 0xa1};
    uint8_t expected_server_hp[] = {0x92, 0xe8, 0x67, 0xb1, 0x20, 0xb1, 0x3f, 0x40, 0x9c, 0x1a, 0xa8, 0xef, 0x54, 0x30, 0x53, 0x51};

    QUICPacketProtectionKeyInfo pp_key_info;
    pp_key_info.set_cipher_initial(EVP_aes_128_gcm());
    pp_key_info.set_cipher_for_hp_initial(EVP_aes_128_ecb());
    keygen.generate(0xff00001b, pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL),
                    pp_key_info.encryption_key(QUICKeyPhase::INITIAL), pp_key_info.encryption_iv(QUICKeyPhase::INITIAL),
                    pp_key_info.encryption_iv_len(QUICKeyPhase::INITIAL), cid);

    CHECK(pp_key_info.encryption_key_len(QUICKeyPhase::INITIAL) == sizeof(expected_server_key));
    CHECK(memcmp(pp_key_info.encryption_key(QUICKeyPhase::INITIAL), expected_server_key, sizeof(expected_server_key)) == 0);
    CHECK(*pp_key_info.encryption_iv_len(QUICKeyPhase::INITIAL) == sizeof(expected_server_iv));
    CHECK(memcmp(pp_key_info.encryption_iv(QUICKeyPhase::INITIAL), expected_server_iv, sizeof(expected_server_iv)) == 0);
    CHECK(pp_key_info.encryption_key_for_hp_len(QUICKeyPhase::INITIAL) == sizeof(expected_server_hp));
    CHECK(memcmp(pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL), expected_server_hp, sizeof(expected_server_hp)) == 0);
  }
}
