/** @file
 *
 *  A key generator for QUIC connection
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

#include "QUICKeyGenerator.h"

#include <openssl/ssl.h>

#include "tscore/ink_assert.h"
#include "tscore/Diags.h"

#include "QUICHKDF.h"
#include "QUICDebugNames.h"

using namespace std::literals;

constexpr static uint8_t QUIC_VERSION_1_SALT[] = {
  0xaf, 0xbf, 0xec, 0x28, 0x99, 0x93, 0xd2, 0x4c, 0x9e, 0x97, 0x86, 0xf1, 0x9c, 0x61, 0x11, 0xe0, 0x43, 0x90, 0xa8, 0x99,
};
constexpr static uint8_t QUIC_VERSION_1_D27_SALT[] = {
  0xc3, 0xee, 0xf7, 0x12, 0xc7, 0x2e, 0xbb, 0x5a, 0x11, 0xa7, 0xd2, 0x43, 0x2b, 0xb4, 0x63, 0x65, 0xbe, 0xf9, 0xf5, 0x02,
};
constexpr static std::string_view LABEL_FOR_CLIENT_INITIAL_SECRET("client in"sv);
constexpr static std::string_view LABEL_FOR_SERVER_INITIAL_SECRET("server in"sv);
constexpr static std::string_view LABEL_FOR_KEY("quic key"sv);
constexpr static std::string_view LABEL_FOR_IV("quic iv"sv);
constexpr static std::string_view LABEL_FOR_HP("quic hp"sv);

void
QUICKeyGenerator::generate(QUICVersion version, uint8_t *hp_key, uint8_t *pp_key, uint8_t *iv, size_t *iv_len, QUICConnectionId cid)
{
  const EVP_CIPHER *cipher = this->_get_cipher_for_initial();
  const EVP_MD *md         = EVP_sha256();
  uint8_t secret[512];
  size_t secret_len = sizeof(secret);
  QUICHKDF hkdf(md);

  switch (this->_ctx) {
  case Context::CLIENT:
    this->_generate_initial_secret(version, secret, &secret_len, hkdf, cid, LABEL_FOR_CLIENT_INITIAL_SECRET.data(),
                                   LABEL_FOR_CLIENT_INITIAL_SECRET.length(), EVP_MD_size(md));
    if (is_debug_tag_set("vv_quic_crypto")) {
      uint8_t print_buf[1024 + 1];
      QUICDebug::to_hex(print_buf, secret, secret_len);
      Debug("vv_quic_crypto", "client_in_secret=%s", print_buf);
    }

    break;
  case Context::SERVER:
    this->_generate_initial_secret(version, secret, &secret_len, hkdf, cid, LABEL_FOR_SERVER_INITIAL_SECRET.data(),
                                   LABEL_FOR_SERVER_INITIAL_SECRET.length(), EVP_MD_size(md));
    if (is_debug_tag_set("vv_quic_crypto")) {
      uint8_t print_buf[1024 + 1];
      QUICDebug::to_hex(print_buf, secret, secret_len);
      Debug("vv_quic_crypto", "server_in_secret=%s", print_buf);
    }

    break;
  }

  this->_generate(hp_key, pp_key, iv, iv_len, hkdf, secret, secret_len, cipher);
}

void
QUICKeyGenerator::regenerate(uint8_t *hp_key, uint8_t *pp_key, uint8_t *iv, size_t *iv_len, const uint8_t *secret,
                             size_t secret_len, const EVP_CIPHER *cipher, QUICHKDF &hkdf)
{
  this->_generate(hp_key, pp_key, iv, iv_len, hkdf, secret, secret_len, cipher);
}

int
QUICKeyGenerator::_generate(uint8_t *hp_key, uint8_t *pp_key, uint8_t *iv, size_t *iv_len, QUICHKDF &hkdf, const uint8_t *secret,
                            size_t secret_len, const EVP_CIPHER *cipher)
{
  // Generate key, iv, and hp_key
  //   key    = HKDF-Expand-Label(S, "quic key", "", key_length)
  //   iv     = HKDF-Expand-Label(S, "quic iv", "", iv_length)
  //   hp_key = HKDF-Expand-Label(S, "quic hp", "", hp_key_length)
  size_t dummy;
  this->_generate_key(pp_key, &dummy, hkdf, secret, secret_len, this->_get_key_len(cipher));
  this->_generate_iv(iv, iv_len, hkdf, secret, secret_len, this->_get_iv_len(cipher));
  this->_generate_hp(hp_key, &dummy, hkdf, secret, secret_len, this->_get_key_len(cipher));

  return 0;
}

int
QUICKeyGenerator::_generate_initial_secret(QUICVersion version, uint8_t *out, size_t *out_len, QUICHKDF &hkdf, QUICConnectionId cid,
                                           const char *label, size_t label_len, size_t length)
{
  uint8_t client_connection_id[QUICConnectionId::MAX_LENGTH];
  size_t cid_len = 0;
  uint8_t initial_secret[512];
  size_t initial_secret_len = sizeof(initial_secret);
  const uint8_t *salt;
  size_t salt_len;

  // TODO: do not extract initial secret twice
  QUICTypeUtil::write_QUICConnectionId(cid, client_connection_id, &cid_len);
  switch (version) {
  case 0xff00001d: // Draft-29
    salt     = QUIC_VERSION_1_SALT;
    salt_len = sizeof(QUIC_VERSION_1_SALT);
    break;
  case 0xff00001b: // Draft-27
    salt     = QUIC_VERSION_1_D27_SALT;
    salt_len = sizeof(QUIC_VERSION_1_D27_SALT);
    break;
  default:
    salt     = QUIC_VERSION_1_SALT;
    salt_len = sizeof(QUIC_VERSION_1_SALT);
    break;
  }
  if (hkdf.extract(initial_secret, &initial_secret_len, salt, salt_len, client_connection_id, cid.length()) != 1) {
    return -1;
  }

  if (is_debug_tag_set("vv_quic_crypto")) {
    uint8_t print_buf[1024 + 1];
    QUICDebug::to_hex(print_buf, initial_secret, initial_secret_len);
    Debug("vv_quic_crypto", "initial_secret=%s", print_buf);
  }

  hkdf.expand(out, out_len, initial_secret, initial_secret_len, reinterpret_cast<const char *>(label), label_len, length);
  return 0;
}

int
QUICKeyGenerator::_generate_key(uint8_t *out, size_t *out_len, QUICHKDF &hkdf, const uint8_t *secret, size_t secret_len,
                                size_t key_length) const
{
  return hkdf.expand(out, out_len, secret, secret_len, LABEL_FOR_KEY.data(), LABEL_FOR_KEY.length(), key_length);
}

int
QUICKeyGenerator::_generate_iv(uint8_t *out, size_t *out_len, QUICHKDF &hkdf, const uint8_t *secret, size_t secret_len,
                               size_t iv_length) const
{
  return hkdf.expand(out, out_len, secret, secret_len, LABEL_FOR_IV.data(), LABEL_FOR_IV.length(), iv_length);
}

int
QUICKeyGenerator::_generate_hp(uint8_t *out, size_t *out_len, QUICHKDF &hkdf, const uint8_t *secret, size_t secret_len,
                               size_t hp_length) const
{
  return hkdf.expand(out, out_len, secret, secret_len, LABEL_FOR_HP.data(), LABEL_FOR_HP.length(), hp_length);
}
