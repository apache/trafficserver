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

bool
QUICPacketPayloadProtector::protect(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain,
                                    size_t plain_len, uint64_t pkt_num, const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const
{
  size_t tag_len     = this->_pp_key_info.get_tag_len(phase);
  const uint8_t *key = this->_pp_key_info.encryption_key(phase);
  const uint8_t *iv  = this->_pp_key_info.encryption_iv(phase);
  size_t iv_len      = *this->_pp_key_info.encryption_iv_len(phase);
  if (!key) {
    Debug(tag, "Failed to encrypt a packet: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }
  const QUIC_EVP_CIPHER *aead = this->_pp_key_info.get_cipher(phase);

  bool ret =
    this->_protect(cipher, cipher_len, max_cipher_len, plain, plain_len, pkt_num, ad, ad_len, key, iv, iv_len, aead, tag_len);
  if (!ret) {
    Debug(tag, "Failed to encrypt a packet #%" PRIu64, pkt_num);
  }
  return ret;
}

bool
QUICPacketPayloadProtector::unprotect(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher,
                                      size_t cipher_len, uint64_t pkt_num, const uint8_t *ad, size_t ad_len,
                                      QUICKeyPhase phase) const
{
  size_t tag_len     = this->_pp_key_info.get_tag_len(phase);
  const uint8_t *key = this->_pp_key_info.decryption_key(phase);
  const uint8_t *iv  = this->_pp_key_info.decryption_iv(phase);
  size_t iv_len      = *this->_pp_key_info.decryption_iv_len(phase);
  if (!key) {
    Debug(tag, "Failed to decrypt a packet: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }
  const QUIC_EVP_CIPHER *aead = this->_pp_key_info.get_cipher(phase);
  bool ret =
    this->_unprotect(plain, plain_len, max_plain_len, cipher, cipher_len, pkt_num, ad, ad_len, key, iv, iv_len, aead, tag_len);
  if (!ret) {
    Debug(tag, "Failed to decrypt a packet #%" PRIu64, pkt_num);
  }
  return ret;
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
