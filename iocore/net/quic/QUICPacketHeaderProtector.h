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

class QUICPacketProtectionKeyInfo;

class QUICPacketHeaderProtector
{
public:
  QUICPacketHeaderProtector(const QUICPacketProtectionKeyInfo &pp_key_info) : _pp_key_info(pp_key_info) {}

  bool unprotect(uint8_t *protected_packet, size_t protected_packet_len) const;
  bool protect(uint8_t *unprotected_packet, size_t unprotected_packet_len, int dcil) const;

private:
  const QUICPacketProtectionKeyInfo &_pp_key_info;

  bool _calc_sample_offset(uint8_t *sample_offset, const uint8_t *protected_packet, size_t protected_packet_len, int dcil) const;

  bool _generate_mask(uint8_t *mask, const uint8_t *sample, const uint8_t *key, const EVP_CIPHER *cipher) const;

  bool _unprotect(uint8_t *packet, size_t packet_len, const uint8_t *mask) const;
  bool _protect(uint8_t *packet, size_t packet_len, const uint8_t *mask, int dcil) const;
};
