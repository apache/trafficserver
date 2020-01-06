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

bool
QUICPacketHeaderProtector::_generate_mask(uint8_t *mask, const uint8_t *sample, const uint8_t *key,
                                          const QUIC_EVP_CIPHER *cipher) const
{
  static constexpr unsigned char FIVE_ZEROS[] = {0x00, 0x00, 0x00, 0x00, 0x00};
  EVP_AEAD_CTX ctx;

  if (!EVP_AEAD_CTX_init(&ctx, cipher, key, EVP_AEAD_key_length(cipher), 16, nullptr)) {
    return false;
  }

  size_t len = 0;
  if (cipher == EVP_aead_chacha20_poly1305()) {
    if (!EVP_AEAD_CTX_seal(&ctx, mask, &len, EVP_MAX_BLOCK_LENGTH, sample + 4, 12, FIVE_ZEROS, sizeof(FIVE_ZEROS), sample, 4)) {
      return false;
    }
  } else {
    if (!EVP_AEAD_CTX_seal(&ctx, mask, &len, EVP_MAX_BLOCK_LENGTH, nullptr, 0, sample, 16, nullptr, 0)) {
      return false;
    }
  }

  return true;
}
