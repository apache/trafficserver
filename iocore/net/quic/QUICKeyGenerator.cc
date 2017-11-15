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
#include "ts/HKDF.h"

constexpr static uint8_t QUIC_VERSION_1_SALT[] = {
  0xaf, 0xc8, 0x24, 0xec, 0x5f, 0xc7, 0x7e, 0xca,
  0x1e, 0x9d, 0x36, 0xf3, 0x7f, 0xb2, 0xd4, 0x65,
  0x18, 0xc3, 0x66, 0x39,
};
constexpr static ts::string_view LABEL_FOR_CLIENT_CLEARTEXT_SECRET("QUIC client cleartext Secret"_sv);
constexpr static ts::string_view LABEL_FOR_SERVER_CLEARTEXT_SECRET("QUIC server cleartext Secret"_sv);
constexpr static ts::string_view LABEL_FOR_KEY("key"_sv);
constexpr static ts::string_view LABEL_FOR_IV("iv"_sv);

std::unique_ptr<KeyMaterial>
QUICKeyGenerator::generate(QUICConnectionId cid)
{
  std::unique_ptr<KeyMaterial> km = std::make_unique<KeyMaterial>();

  const EVP_MD *md= EVP_sha256();
  const QUIC_EVP_CIPHER *cipher = this->_get_cipher_for_cleartext();
  uint8_t cleartext_secret[256];
  size_t cleartext_secret_len = sizeof(cleartext_secret);
  uint8_t client_connection_id[8];
  size_t cid_len = 0;
  HKDF hkdf(md);

  QUICTypeUtil::write_QUICConnectionId(cid, 8, client_connection_id, &cid_len);
  if (hkdf.extract(cleartext_secret, &cleartext_secret_len, QUIC_VERSION_1_SALT, sizeof(QUIC_VERSION_1_SALT), client_connection_id, 8) != 1) {
    return nullptr;
  }

  switch (this->_ctx) {
  case Context::CLIENT:
    this->_generate(
    km->key, &km->key_len, km->iv, &km->iv_len,
    hkdf,
    cleartext_secret, cleartext_secret_len,
    LABEL_FOR_CLIENT_CLEARTEXT_SECRET.data(), LABEL_FOR_CLIENT_CLEARTEXT_SECRET.length(),
    md, cipher);
    break;
  case Context::SERVER:
    this->_generate(
    km->key, &km->key_len, km->iv, &km->iv_len,
    hkdf,
    cleartext_secret, cleartext_secret_len,
    LABEL_FOR_SERVER_CLEARTEXT_SECRET.data(), LABEL_FOR_SERVER_CLEARTEXT_SECRET.length(),
    md, cipher);
    break;
  }

  return km;
}

std::unique_ptr<KeyMaterial>
QUICKeyGenerator::generate()
{
  return 0;
}

int
QUICKeyGenerator::_generate(uint8_t *key, size_t *key_len, uint8_t *iv, size_t *iv_len, HKDF &hkdf, const uint8_t *base_secret, size_t base_secret_len, const char *label, size_t label_len, const EVP_MD *md, const QUIC_EVP_CIPHER *cipher)
{
  uint8_t secret[256];
  size_t secret_len = sizeof(secret);
    hkdf.expand_label(secret, &secret_len, base_secret, base_secret_len, reinterpret_cast<const char *>(label), label_len, "", 0, EVP_MD_size(md));
  this->_generate_key(key, key_len, hkdf, secret, secret_len, this->_get_key_len(cipher));
  this->_generate_iv(iv, iv_len, hkdf, secret, secret_len, this->_get_iv_len(cipher));

  return 0;
}

int
QUICKeyGenerator::_generate_key(uint8_t *out, size_t *out_len, HKDF &hkdf, const uint8_t *secret, size_t secret_len, size_t key_length) const
{
  return hkdf.expand_label(out, out_len, secret, secret_len, LABEL_FOR_KEY.data(), LABEL_FOR_KEY.length(), "", 0, key_length);
}

int
QUICKeyGenerator::_generate_iv(uint8_t *out, size_t *out_len, HKDF &hkdf, const uint8_t *secret, size_t secret_len, size_t iv_length) const
{
  return hkdf.expand_label(out, out_len, secret, secret_len, LABEL_FOR_IV.data(), LABEL_FOR_IV.length(), "", 0, iv_length);
}
