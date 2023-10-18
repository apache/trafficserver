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

    QUICConnectionId cid = {reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x08"), 8};

    uint8_t expected_client_key[] = {0x1f, 0x36, 0x96, 0x13, 0xdd, 0x76, 0xd5, 0x46,
                                     0x77, 0x30, 0xef, 0xcb, 0xe3, 0xb1, 0xa2, 0x2d};
    uint8_t expected_client_iv[]  = {0xfa, 0x04, 0x4b, 0x2f, 0x42, 0xa3, 0xfd, 0x3b, 0x46, 0xfb, 0x25, 0x5c};
    uint8_t expected_client_hp[] = {0x9f, 0x50, 0x44, 0x9e, 0x04, 0xa0, 0xe8, 0x10, 0x28, 0x3a, 0x1e, 0x99, 0x33, 0xad, 0xed, 0xd2};

    QUICPacketProtectionKeyInfo pp_key_info;
    pp_key_info.set_cipher_initial(EVP_aes_128_gcm());
    pp_key_info.set_cipher_for_hp_initial(EVP_aes_128_ecb());
    keygen.generate(0x00000001, pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL),
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

    QUICConnectionId cid = {reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x08"), 8};

    uint8_t expected_server_key[] = {0xcf, 0x3a, 0x53, 0x31, 0x65, 0x3c, 0x36, 0x4c,
                                     0x88, 0xf0, 0xf3, 0x79, 0xb6, 0x06, 0x7e, 0x37};
    uint8_t expected_server_iv[]  = {0x0a, 0xc1, 0x49, 0x3c, 0xa1, 0x90, 0x58, 0x53, 0xb0, 0xbb, 0xa0, 0x3e};
    uint8_t expected_server_hp[] = {0xc2, 0x06, 0xb8, 0xd9, 0xb9, 0xf0, 0xf3, 0x76, 0x44, 0x43, 0x0b, 0x49, 0x0e, 0xea, 0xa3, 0x14};

    QUICPacketProtectionKeyInfo pp_key_info;
    pp_key_info.set_cipher_initial(EVP_aes_128_gcm());
    pp_key_info.set_cipher_for_hp_initial(EVP_aes_128_ecb());
    keygen.generate(0x00000001, pp_key_info.encryption_key_for_hp(QUICKeyPhase::INITIAL),
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
