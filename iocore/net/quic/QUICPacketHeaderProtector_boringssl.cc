/** @file
 *
 *  QUIC Packet Header Protector (BoringSSL specific code)
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

#include "QUICPacketHeaderProtector.h"

#include <openssl/chacha.h>

bool
QUICPacketHeaderProtector::_generate_mask(uint8_t *mask, const uint8_t *sample, const uint8_t *key, const EVP_CIPHER *cipher) const
{
  static constexpr unsigned char FIVE_ZEROS[] = {0x00, 0x00, 0x00, 0x00, 0x00};

  if (cipher == nullptr) {
    uint32_t counter = htole32(*reinterpret_cast<const uint32_t *>(&sample[0]));
    CRYPTO_chacha_20(mask, FIVE_ZEROS, sizeof(FIVE_ZEROS), key, &sample[4], counter);
  } else {
    int len             = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
      return false;
    }
    if (!EVP_EncryptInit_ex(ctx, cipher, nullptr, key, sample)) {
      EVP_CIPHER_CTX_free(ctx);
      return false;
    }
    if (!EVP_EncryptUpdate(ctx, mask, &len, sample, 16)) {
      EVP_CIPHER_CTX_free(ctx);
      return false;
    }
    if (!EVP_EncryptFinal_ex(ctx, mask + len, &len)) {
      EVP_CIPHER_CTX_free(ctx);
      return false;
    }
    EVP_CIPHER_CTX_free(ctx);
  }

  return true;
}
