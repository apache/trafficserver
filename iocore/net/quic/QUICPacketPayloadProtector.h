/** @file
 *
 *  QUIC Packet Payload Protector
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

#pragma once

#include "I_IOBuffer.h"
#include "QUICTypes.h"
#include "QUICKeyGenerator.h"

class QUICPacketProtectionKeyInfo;

class QUICPacketPayloadProtector
{
public:
  QUICPacketPayloadProtector(const QUICPacketProtectionKeyInfo &pp_key_info) : _pp_key_info(pp_key_info) {}

  Ptr<IOBufferBlock> protect(const Ptr<IOBufferBlock> protected_payload, const Ptr<IOBufferBlock> unprotected_payload,
                             uint64_t pkt_num, QUICKeyPhase phase) const;
  Ptr<IOBufferBlock> unprotect(const Ptr<IOBufferBlock> unprotected_payload, const Ptr<IOBufferBlock> protected_payload,
                               uint64_t pkt_num, QUICKeyPhase phase) const;

private:
  const QUICPacketProtectionKeyInfo &_pp_key_info;

  bool _unprotect(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *protected_payload,
                  size_t protected_payload_len, uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const uint8_t *key,
                  const uint8_t *iv, size_t iv_len, const EVP_CIPHER *cipher, size_t tag_len) const;
  bool _protect(uint8_t *protected_payload, size_t &protected_payload_len, size_t max_protected_payload_len,
                const Ptr<IOBufferBlock> plain, uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const uint8_t *key,
                const uint8_t *iv, size_t iv_len, const EVP_CIPHER *cipher, size_t tag_len) const;

  void _gen_nonce(uint8_t *nonce, size_t &nonce_len, uint64_t pkt_num, const uint8_t *iv, size_t iv_len) const;
};
