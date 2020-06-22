/** @file
 *
 *  QUIC Packet Header Protector
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
#include "QUICDebugNames.h"

static constexpr char tag[] = "quic_ppp";

Ptr<IOBufferBlock>
QUICPacketPayloadProtector::protect(const Ptr<IOBufferBlock> unprotected_header, const Ptr<IOBufferBlock> unprotected_payload,
                                    uint64_t pkt_num, QUICKeyPhase phase) const
{
  Ptr<IOBufferBlock> protected_payload;
  protected_payload = nullptr;

  if (!this->_pp_key_info.is_encryption_key_available(phase)) {
    Debug(tag, "Failed to encrypt a packet: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return protected_payload;
  }

  size_t tag_len     = this->_pp_key_info.get_tag_len(phase);
  const uint8_t *key = this->_pp_key_info.encryption_key(phase);
  const uint8_t *iv  = this->_pp_key_info.encryption_iv(phase);
  size_t iv_len      = *this->_pp_key_info.encryption_iv_len(phase);

  const EVP_CIPHER *cipher = this->_pp_key_info.get_cipher(phase);

  protected_payload              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  size_t unprotected_payload_len = 0;
  for (Ptr<IOBufferBlock> tmp = unprotected_payload; tmp; tmp = tmp->next) {
    unprotected_payload_len += tmp->size();
  }
  protected_payload->alloc(iobuffer_size_to_index(unprotected_payload_len + tag_len, BUFFER_SIZE_INDEX_32K));

  size_t written_len = 0;
  if (!this->_protect(reinterpret_cast<uint8_t *>(protected_payload->start()), written_len, protected_payload->write_avail(),
                      unprotected_payload, pkt_num, reinterpret_cast<uint8_t *>(unprotected_header->start()),
                      unprotected_header->size(), key, iv, iv_len, cipher, tag_len)) {
    Debug(tag, "Failed to encrypt a packet #%" PRIu64 " with keys for %s", pkt_num, QUICDebugNames::key_phase(phase));
    protected_payload = nullptr;
  } else {
    protected_payload->fill(written_len);
  }

  return protected_payload;
}

Ptr<IOBufferBlock>
QUICPacketPayloadProtector::unprotect(const Ptr<IOBufferBlock> unprotected_header, const Ptr<IOBufferBlock> protected_payload,
                                      uint64_t pkt_num, QUICKeyPhase phase) const
{
  Ptr<IOBufferBlock> unprotected_payload;
  unprotected_payload = nullptr;

  size_t tag_len     = this->_pp_key_info.get_tag_len(phase);
  const uint8_t *key = this->_pp_key_info.decryption_key(phase);
  const uint8_t *iv  = this->_pp_key_info.decryption_iv(phase);
  size_t iv_len      = *this->_pp_key_info.decryption_iv_len(phase);
  if (!key) {
    Debug(tag, "Failed to decrypt a packet: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return unprotected_payload;
  }
  const EVP_CIPHER *cipher = this->_pp_key_info.get_cipher(phase);

  unprotected_payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  unprotected_payload->alloc(iobuffer_size_to_index(protected_payload->size(), BUFFER_SIZE_INDEX_32K));

  size_t written_len = 0;
  if (!this->_unprotect(reinterpret_cast<uint8_t *>(unprotected_payload->start()), written_len, unprotected_payload->write_avail(),
                        reinterpret_cast<uint8_t *>(protected_payload->start()), protected_payload->size(), pkt_num,
                        reinterpret_cast<uint8_t *>(unprotected_header->start()), unprotected_header->size(), key, iv, iv_len,
                        cipher, tag_len)) {
    Debug(tag, "Failed to decrypt a packet #%" PRIu64, pkt_num);
    unprotected_payload = nullptr;
  } else {
    unprotected_payload->fill(written_len);
  }
  return unprotected_payload;
}

/**
 * Example iv_len = 12
 *
 *   0                   1
 *   0 1 2 3 4 5 6 7 8 9 0 1 2  (byte)
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |           iv            |    // IV
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |0|0|0|0|    pkt num      |    // network byte order & left-padded with zeros
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |          nonce          |    // nonce = iv xor pkt_num
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
void
QUICPacketPayloadProtector::_gen_nonce(uint8_t *nonce, size_t &nonce_len, uint64_t pkt_num, const uint8_t *iv, size_t iv_len) const
{
  nonce_len = iv_len;
  memcpy(nonce, iv, iv_len);

  pkt_num    = htobe64(pkt_num);
  uint8_t *p = reinterpret_cast<uint8_t *>(&pkt_num);

  for (size_t i = 0; i < 8; ++i) {
    nonce[iv_len - 8 + i] ^= p[i];
  }
}

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
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_IVLEN, nonce_len, nullptr)) {
    return false;
  }
  if (!EVP_EncryptInit_ex(aead_ctx, nullptr, nullptr, key, nonce)) {
    return false;
  }
  if (!EVP_EncryptUpdate(aead_ctx, nullptr, &len, ad, ad_len)) {
    return false;
  }

  cipher_len = 0;
  for (Ptr<IOBufferBlock> b = plain; b; b = b->next) {
    if (!EVP_EncryptUpdate(aead_ctx, cipher + cipher_len, &len, reinterpret_cast<unsigned char *>(b->start()), b->size())) {
      return false;
    }
    cipher_len += len;
  }

  if (!EVP_EncryptFinal_ex(aead_ctx, cipher + cipher_len, &len)) {
    return false;
  }
  cipher_len += len;

  if (max_cipher_len < cipher_len + tag_len) {
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_GET_TAG, tag_len, cipher + cipher_len)) {
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
    return false;
  }
  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_IVLEN, nonce_len, nullptr)) {
    return false;
  }
  if (!EVP_DecryptInit_ex(aead_ctx, nullptr, nullptr, key, nonce)) {
    return false;
  }
  if (!EVP_DecryptUpdate(aead_ctx, nullptr, &len, ad, ad_len)) {
    return false;
  }

  if (cipher_len < tag_len) {
    return false;
  }
  cipher_len -= tag_len;
  if (!EVP_DecryptUpdate(aead_ctx, plain, &len, cipher, cipher_len)) {
    return false;
  }
  plain_len = len;

  if (!EVP_CIPHER_CTX_ctrl(aead_ctx, EVP_CTRL_AEAD_SET_TAG, tag_len, const_cast<uint8_t *>(cipher + cipher_len))) {
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
