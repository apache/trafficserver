/** @file
 *
 *  QUIC Packet Payload Protector (BoringSSL specific code)
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

#include "QUICPacketProtectionKeyInfo.h"
#include "QUICPacketPayloadProtector.h"
#include "tscore/Diags.h"

static constexpr char tag[] = "quic_ppp";

bool
QUICPacketPayloadProtector::_protect(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const Ptr<IOBufferBlock> plain,
                                     uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const uint8_t *key, const uint8_t *iv,
                                     size_t iv_len, const EVP_CIPHER *aead, size_t tag_len) const
{
  EVP_CIPHER_CTX *aead_ctx;
  int len;
  uint8_t nonce[EVP_MAX_IV_LENGTH] = {0};
  size_t nonce_len                 = 0;

  this->_gen_nonce(nonce, nonce_len, pkt_num, iv, iv_len);

  if (!(aead_ctx = EVP_CIPHER_CTX_new())) {
    return false;
  }
  if (!EVP_EncryptInit_ex(aead_ctx, aead, nullptr, nullptr, nullptr)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_IVLEN, nonce_len, nullptr)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  if (!EVP_EncryptInit_ex(aead_ctx, nullptr, nullptr, key, nonce)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  if (!EVP_EncryptUpdate(aead_ctx, nullptr, &len, ad, ad_len)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }

  cipher_len           = 0;
  Ptr<IOBufferBlock> b = plain;
  while (b) {
    if (!EVP_EncryptUpdate(aead_ctx, cipher + cipher_len, &len, reinterpret_cast<unsigned char *>(b->buf()), b->size())) {
      EVP_CIPHER_CTX_free(aead_ctx);
      return false;
    }
    cipher_len += len;
    b = b->next;
  }

  if (!EVP_EncryptFinal_ex(aead_ctx, cipher + cipher_len, &len)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  cipher_len += len;

  if (max_cipher_len < cipher_len + tag_len) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_GET_TAG, tag_len, cipher + cipher_len)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  cipher_len += tag_len;

  EVP_CIPHER_CTX_free(aead_ctx);

  return true;
}

bool
QUICPacketPayloadProtector::_unprotect(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher,
                                       size_t cipher_len, uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const uint8_t *key,
                                       const uint8_t *iv, size_t iv_len, const EVP_CIPHER *aead, size_t tag_len) const
{
  EVP_CIPHER_CTX *aead_ctx;
  int len;
  uint8_t nonce[EVP_MAX_IV_LENGTH] = {0};
  size_t nonce_len                 = 0;

  this->_gen_nonce(nonce, nonce_len, pkt_num, iv, iv_len);

  if (!(aead_ctx = EVP_CIPHER_CTX_new())) {
    return false;
  }
  if (!EVP_DecryptInit_ex(aead_ctx, aead, nullptr, nullptr, nullptr)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_IVLEN, nonce_len, nullptr)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  if (!EVP_DecryptInit_ex(aead_ctx, nullptr, nullptr, key, nonce)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  if (!EVP_DecryptUpdate(aead_ctx, nullptr, &len, ad, ad_len)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }

  if (cipher_len < tag_len) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  cipher_len -= tag_len;
  if (!EVP_DecryptUpdate(aead_ctx, plain, &len, cipher, cipher_len)) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }
  plain_len = len;

  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_TAG, tag_len, const_cast<uint8_t *>(cipher + cipher_len))) {
    EVP_CIPHER_CTX_free(aead_ctx);
    return false;
  }

  int ret = EVP_DecryptFinal_ex(aead_ctx, plain + len, &len);

  EVP_CIPHER_CTX_free(aead_ctx);

  if (ret > 0) {
    plain_len += len;
    return true;
  } else {
    Debug(tag, "Failed to decrypt -- the first 4 bytes decrypted are %0x %0x %0x %0x", plain[0], plain[1], plain[2], plain[3]);
    return false;
  }
}
