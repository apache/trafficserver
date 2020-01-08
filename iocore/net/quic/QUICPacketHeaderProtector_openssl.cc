/** @file
 *
 *  QUIC Packet Header Protector (OpenSSL specific code)
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

bool
QUICPacketHeaderProtector::_generate_mask(uint8_t *mask, const uint8_t *sample, const uint8_t *key, const EVP_CIPHER *cipher) const
{
  static constexpr unsigned char FIVE_ZEROS[] = {0x00, 0x00, 0x00, 0x00, 0x00};
  EVP_CIPHER_CTX *ctx                         = EVP_CIPHER_CTX_new();

  if (!ctx || !EVP_EncryptInit_ex(ctx, cipher, nullptr, key, sample)) {
    return false;
  }

  int len = 0;
  if (cipher == EVP_chacha20()) {
    if (!EVP_EncryptUpdate(ctx, mask, &len, FIVE_ZEROS, sizeof(FIVE_ZEROS))) {
      return false;
    }
  } else {
    if (!EVP_EncryptUpdate(ctx, mask, &len, sample, 16)) {
      return false;
    }
  }
  if (!EVP_EncryptFinal_ex(ctx, mask + len, &len)) {
    return false;
  }

  EVP_CIPHER_CTX_free(ctx);

  return true;
}
