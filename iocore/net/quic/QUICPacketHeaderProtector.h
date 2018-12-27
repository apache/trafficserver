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

#pragma once

#include "QUICTypes.h"
#include "QUICKeyGenerator.h"
#include "QUICHandshakeProtocol.h"

class QUICPacketHeaderProtector
{
public:
  bool unprotect(uint8_t *protected_packet, size_t protected_packet_len);

  bool protect(uint8_t *protected_pn, uint8_t &protected_pn_len, const uint8_t *unprotected_pn, uint8_t unprotected_pn_len,
               const uint8_t *sample, QUICKeyPhase phase) const;

  // FIXME We don't need QUICHandshakeProtocol here, and should pass QUICCryptoInfoProvider or somethign instead.
  // For now it receives a CONST pointer so PacketNubmerProtector cannot bother handshake.
  void set_hs_protocol(const QUICHandshakeProtocol *hs_protocol);

private:
  const QUICHandshakeProtocol *_hs_protocol = nullptr;

  bool _calc_sample_offset(uint8_t *sample_offset, const uint8_t *protected_packet, size_t protected_packet_len);

  bool _generate_mask(uint8_t *mask, const uint8_t *sample, const uint8_t *key, const EVP_CIPHER *aead);

  bool _unprotect(uint8_t *packet, size_t packet_len, const uint8_t *mask);

  bool _encrypt_pn(uint8_t *protected_pn, uint8_t &protected_pn_len, const uint8_t *unprotected_pn, uint8_t unprotected_pn_len,
                   const uint8_t *sample, const uint8_t *key, size_t key_len, const QUIC_EVP_CIPHER *aead) const;
};
