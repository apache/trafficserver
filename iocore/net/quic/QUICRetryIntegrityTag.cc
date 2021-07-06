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

#include "QUICRetryIntegrityTag.h"

bool
QUICRetryIntegrityTag::compute(uint8_t *out, QUICVersion version, QUICConnectionId odcid, Ptr<IOBufferBlock> header,
                               Ptr<IOBufferBlock> payload)
{
  EVP_CIPHER_CTX *aead_ctx;

  if (!(aead_ctx = EVP_CIPHER_CTX_new())) {
    return false;
  }
  if (!EVP_EncryptInit_ex(aead_ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr)) {
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr)) {
    return false;
  }
  const uint8_t *key;
  const uint8_t *nonce;
  switch (version) {
  case 0xff00001d: // Draft-29
    key   = KEY_FOR_RETRY_INTEGRITY_TAG;
    nonce = NONCE_FOR_RETRY_INTEGRITY_TAG;
    break;
  case 0xff00001b: // Draft-27
    key   = KEY_FOR_RETRY_INTEGRITY_TAG_D27;
    nonce = NONCE_FOR_RETRY_INTEGRITY_TAG_D27;
    break;
  default:
    key   = KEY_FOR_RETRY_INTEGRITY_TAG;
    nonce = NONCE_FOR_RETRY_INTEGRITY_TAG;
    break;
  }
  if (!EVP_EncryptInit_ex(aead_ctx, nullptr, nullptr, key, nonce)) {
    return false;
  }

  // Original Destination Connection ID
  size_t n;
  int dummy;
  uint8_t odcid_buf[1 + QUICConnectionId::MAX_LENGTH];
  // Len
  odcid_buf[0] = odcid.length();
  // ID
  QUICTypeUtil::write_QUICConnectionId(odcid, odcid_buf + 1, &n);
  if (!EVP_EncryptUpdate(aead_ctx, nullptr, &dummy, odcid_buf, 1 + odcid.length())) {
    return false;
  }

  // Retry Packet
  for (Ptr<IOBufferBlock> b = header; b; b = b->next) {
    if (!EVP_EncryptUpdate(aead_ctx, nullptr, &dummy, reinterpret_cast<unsigned char *>(b->start()), b->size())) {
      return false;
    }
  }
  for (Ptr<IOBufferBlock> b = payload; b; b = b->next) {
    if (!EVP_EncryptUpdate(aead_ctx, nullptr, &dummy, reinterpret_cast<unsigned char *>(b->start()), b->size())) {
      return false;
    }
  }

  if (!EVP_EncryptFinal_ex(aead_ctx, nullptr, &dummy)) {
    return false;
  }

  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_GET_TAG, LEN, out)) {
    return false;
  }

  EVP_CIPHER_CTX_free(aead_ctx);

  return true;
}
