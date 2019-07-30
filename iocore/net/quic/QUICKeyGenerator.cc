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
  0xef, 0x4f, 0xb0, 0xab, 0xb4, 0x74, 0x70, 0xc4, 0x1b, 0xef, 0xcf, 0x80, 0x31, 0x33, 0x4f, 0xae, 0x48, 0x5e, 0x09, 0xa0,
};
constexpr static std::string_view LABEL_FOR_CLIENT_INITIAL_SECRET("client in"sv);
constexpr static std::string_view LABEL_FOR_SERVER_INITIAL_SECRET("server in"sv);
constexpr static std::string_view LABEL_FOR_KEY("quic key"sv);
constexpr static std::string_view LABEL_FOR_IV("quic iv"sv);
constexpr static std::string_view LABEL_FOR_HP("quic hp"sv);

void
QUICKeyGenerator::generate(uint8_t *hp_key, uint8_t *pp_key, uint8_t *iv, size_t *iv_len, QUICConnectionId cid)
{
  const QUIC_EVP_CIPHER *cipher = this->_get_cipher_for_initial();
  const EVP_MD *md              = EVP_sha256();
  uint8_t secret[512];
  size_t secret_len = sizeof(secret);
  QUICHKDF hkdf(md);

  switch (this->_ctx) {
  case Context::CLIENT:
    this->_generate_initial_secret(secret, &secret_len, hkdf, cid, LABEL_FOR_CLIENT_INITIAL_SECRET.data(),
                                   LABEL_FOR_CLIENT_INITIAL_SECRET.length(), EVP_MD_size(md));
    if (is_debug_tag_set("vv_quic_crypto")) {
      uint8_t print_buf[1024 + 1];
      QUICDebug::to_hex(print_buf, secret, secret_len);
      Debug("vv_quic_crypto", "client_in_secret=%s", print_buf);
    }

    break;
  case Context::SERVER:
    this->_generate_initial_secret(secret, &secret_len, hkdf, cid, LABEL_FOR_SERVER_INITIAL_SECRET.data(),
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
                             size_t secret_len, const QUIC_EVP_CIPHER *cipher, QUICHKDF &hkdf)
{
  this->_generate(hp_key, pp_key, iv, iv_len, hkdf, secret, secret_len, cipher);
}

int
QUICKeyGenerator::_generate(uint8_t *hp_key, uint8_t *pp_key, uint8_t *iv, size_t *iv_len, QUICHKDF &hkdf, const uint8_t *secret,
                            size_t secret_len, const QUIC_EVP_CIPHER *cipher)
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
QUICKeyGenerator::_generate_initial_secret(uint8_t *out, size_t *out_len, QUICHKDF &hkdf, QUICConnectionId cid, const char *label,
                                           size_t label_len, size_t length)
{
  uint8_t client_connection_id[QUICConnectionId::MAX_LENGTH];
  size_t cid_len = 0;
  uint8_t initial_secret[512];
  size_t initial_secret_len = sizeof(initial_secret);

  // TODO: do not extract initial secret twice
  QUICTypeUtil::write_QUICConnectionId(cid, client_connection_id, &cid_len);
  if (hkdf.extract(initial_secret, &initial_secret_len, QUIC_VERSION_1_SALT, sizeof(QUIC_VERSION_1_SALT), client_connection_id,
                   cid.length()) != 1) {
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
