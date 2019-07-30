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

  protected_payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  protected_payload->alloc(iobuffer_size_to_index(unprotected_payload->size() + tag_len));

  size_t written_len = 0;
  if (!this->_protect(reinterpret_cast<uint8_t *>(protected_payload->start()), written_len, protected_payload->write_avail(),
                      unprotected_payload, pkt_num, reinterpret_cast<uint8_t *>(unprotected_header->buf()),
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
  unprotected_payload->alloc(iobuffer_size_to_index(protected_payload->size()));

  size_t written_len = 0;
  if (!this->_unprotect(reinterpret_cast<uint8_t *>(unprotected_payload->start()), written_len, unprotected_payload->write_avail(),
                        reinterpret_cast<uint8_t *>(protected_payload->buf()), protected_payload->size(), pkt_num,
                        reinterpret_cast<uint8_t *>(unprotected_header->buf()), unprotected_header->size(), key, iv, iv_len, cipher,
                        tag_len)) {
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
