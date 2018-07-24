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

#include "ts/ink_assert.h"
#include "ts/Diags.h"

#include "QUICHKDF.h"
#include "QUICDebugNames.h"

using namespace std::literals;

constexpr static uint8_t QUIC_VERSION_1_SALT[] = {
  0x9c, 0x10, 0x8f, 0x98, 0x52, 0x0a, 0x5c, 0x5c, 0x32, 0x96, 0x8e, 0x95, 0x0e, 0x8a, 0x2c, 0x5f, 0xe0, 0x6d, 0x6c, 0x38,
};
constexpr static std::string_view LABEL_FOR_CLIENT_INITIAL_SECRET("client in"sv);
constexpr static std::string_view LABEL_FOR_SERVER_INITIAL_SECRET("server in"sv);
constexpr static std::string_view LABEL_FOR_KEY("key"sv);
constexpr static std::string_view LABEL_FOR_IV("iv"sv);
constexpr static std::string_view LABEL_FOR_PN("pn"sv);

std::unique_ptr<KeyMaterial>
QUICKeyGenerator::generate(QUICConnectionId cid)
{
  std::unique_ptr<KeyMaterial> km = std::make_unique<KeyMaterial>();

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
      uint8_t print_buf[512];
      QUICDebug::to_hex(print_buf, secret, secret_len);
      Debug("vv_quic_crypto", "client_in_secret=%s", print_buf);
    }

    break;
  case Context::SERVER:
    this->_generate_initial_secret(secret, &secret_len, hkdf, cid, LABEL_FOR_SERVER_INITIAL_SECRET.data(),
                                   LABEL_FOR_SERVER_INITIAL_SECRET.length(), EVP_MD_size(md));
    if (is_debug_tag_set("vv_quic_crypto")) {
      uint8_t print_buf[512];
      QUICDebug::to_hex(print_buf, secret, secret_len);
      Debug("vv_quic_crypto", "server_in_secret=%s", print_buf);
    }

    break;
  }

  this->_generate(*km, hkdf, secret, secret_len, cipher);

  return km;
}

int
QUICKeyGenerator::_generate(KeyMaterial &km, QUICHKDF &hkdf, const uint8_t *secret, size_t secret_len,
                            const QUIC_EVP_CIPHER *cipher)
{
  // Generate key, iv, and pn_key
  //   key    = HKDF-Expand-Label(S, "key", "", key_length)
  //   iv     = HKDF-Expand-Label(S, "iv", "", iv_length)
  //   pn_key = HKDF-Expand-Label(S, "pn", "", pn_key_length)
  this->_generate_key(km.key, &km.key_len, hkdf, secret, secret_len, this->_get_key_len(cipher));
  this->_generate_iv(km.iv, &km.iv_len, hkdf, secret, secret_len, this->_get_iv_len(cipher));
  QUICKeyGenerator::generate_pn(km.pn, &km.pn_len, hkdf, secret, secret_len, this->_get_key_len(cipher));

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
    uint8_t print_buf[512];
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
QUICKeyGenerator::generate_pn(uint8_t *out, size_t *out_len, QUICHKDF &hkdf, const uint8_t *secret, size_t secret_len,
                              size_t pn_length)
{
  return hkdf.expand(out, out_len, secret, secret_len, LABEL_FOR_PN.data(), LABEL_FOR_PN.length(), pn_length);
}
