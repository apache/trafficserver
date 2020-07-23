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
#include "QUICPacketHeaderProtector.h"
#include "QUICDebugNames.h"
#include "QUICPacket.h"

#include "tscore/Diags.h"

bool
QUICPacketHeaderProtector::protect(uint8_t *unprotected_packet, size_t unprotected_packet_len, int dcil) const
{
  // Do nothing if the packet is VN
  QUICPacketType type;
  QUICPacketR::type(type, unprotected_packet, unprotected_packet_len);
  if (type == QUICPacketType::VERSION_NEGOTIATION) {
    return true;
  }

  QUICKeyPhase phase;
  if (QUICInvariants::is_long_header(unprotected_packet)) {
    QUICLongHeaderPacketR::key_phase(phase, unprotected_packet, unprotected_packet_len);
  } else {
    // This is a kind of hack. For short header we need to use the same key for header protection regardless of the key phase.
    phase = QUICKeyPhase::PHASE_0;
    type  = QUICPacketType::PROTECTED;
  }

  Debug("v_quic_pne", "Protecting a packet number of %s packet using %s", QUICDebugNames::packet_type(type),
        QUICDebugNames::key_phase(phase));

  const EVP_CIPHER *aead = this->_pp_key_info.get_cipher_for_hp(phase);
  if (!aead) {
    Debug("quic_pne", "Failed to encrypt a packet number: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }

  const uint8_t *key = this->_pp_key_info.encryption_key_for_hp(phase);
  if (!key) {
    Debug("quic_pne", "Failed to encrypt a packet number: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }

  uint8_t sample_offset;
  if (!this->_calc_sample_offset(&sample_offset, unprotected_packet, unprotected_packet_len, dcil)) {
    Debug("v_quic_pne", "Failed to calculate a sample offset");
    return false;
  }

  uint8_t mask[EVP_MAX_BLOCK_LENGTH];
  if (!this->_generate_mask(mask, unprotected_packet + sample_offset, key, aead)) {
    Debug("v_quic_pne", "Failed to generate a mask");
    return false;
  }

  if (!this->_protect(unprotected_packet, unprotected_packet_len, mask, dcil)) {
    Debug("quic_pne", "Failed to encrypt a packet number");
  }

  return true;
}

bool
QUICPacketHeaderProtector::unprotect(uint8_t *protected_packet, size_t protected_packet_len) const
{
  // Do nothing if the packet is VN or RETRY
  QUICPacketType type;
  QUICPacketR::type(type, protected_packet, protected_packet_len);
  if (type == QUICPacketType::VERSION_NEGOTIATION || type == QUICPacketType::RETRY) {
    return true;
  }

  QUICKeyPhase phase;
  if (QUICInvariants::is_long_header(protected_packet)) {
    QUICLongHeaderPacketR::key_phase(phase, protected_packet, protected_packet_len);
  } else {
    // This is a kind of hack. For short header we need to use the same key for header protection regardless of the key phase.
    phase = QUICKeyPhase::PHASE_0;
    type  = QUICPacketType::PROTECTED;
  }

  Debug("v_quic_pne", "Unprotecting a packet number of %s packet using %s", QUICDebugNames::packet_type(type),
        QUICDebugNames::key_phase(phase));

  const EVP_CIPHER *aead = this->_pp_key_info.get_cipher_for_hp(phase);
  if (!aead) {
    Debug("quic_pne", "Failed to decrypt a packet number: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }

  const uint8_t *key = this->_pp_key_info.decryption_key_for_hp(phase);
  if (!key) {
    Debug("quic_pne", "Failed to decrypt a packet number: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }

  uint8_t sample_offset;
  if (!this->_calc_sample_offset(&sample_offset, protected_packet, protected_packet_len, QUICConnectionId::SCID_LEN)) {
    Debug("v_quic_pne", "Failed to calculate a sample offset");
    return false;
  }

  uint8_t mask[EVP_MAX_BLOCK_LENGTH];
  if (!this->_generate_mask(mask, protected_packet + sample_offset, key, aead)) {
    Debug("v_quic_pne", "Failed to generate a mask");
    return false;
  }

  if (!this->_unprotect(protected_packet, protected_packet_len, mask)) {
    Debug("quic_pne", "Failed to decrypt a packet number");
  }

  return true;
}

bool
QUICPacketHeaderProtector::_calc_sample_offset(uint8_t *sample_offset, const uint8_t *protected_packet, size_t protected_packet_len,
                                               int dcil) const
{
  if (QUICInvariants::is_long_header(protected_packet)) {
    size_t dummy;
    uint8_t length_len;
    size_t length_offset;
    if (!QUICLongHeaderPacketR::length(dummy, length_len, length_offset, protected_packet, protected_packet_len)) {
      return false;
    }

    *sample_offset = length_offset + length_len + 4;
  } else {
    *sample_offset = QUICInvariants::SH_DCID_OFFSET + dcil + 4;
  }

  return static_cast<size_t>(*sample_offset + 16) <= protected_packet_len;
}

bool
QUICPacketHeaderProtector::_unprotect(uint8_t *protected_packet, size_t protected_packet_len, const uint8_t *mask) const
{
  size_t pn_offset;

  // Unprotect packet number
  if (QUICInvariants::is_long_header(protected_packet)) {
    protected_packet[0] ^= mask[0] & 0x0f;
    QUICLongHeaderPacketR::packet_number_offset(pn_offset, protected_packet, protected_packet_len);
  } else {
    protected_packet[0] ^= mask[0] & 0x1f;
    QUICShortHeaderPacketR::packet_number_offset(pn_offset, protected_packet, protected_packet_len, QUICConnectionId::SCID_LEN);
  }
  uint8_t pn_length = QUICTypeUtil::read_QUICPacketNumberLen(protected_packet);

  for (int i = 0; i < pn_length; ++i) {
    protected_packet[pn_offset + i] ^= mask[1 + i];
  }

  return true;
}

bool
QUICPacketHeaderProtector::_protect(uint8_t *protected_packet, size_t protected_packet_len, const uint8_t *mask, int dcil) const
{
  size_t pn_offset;

  uint8_t pn_length = QUICTypeUtil::read_QUICPacketNumberLen(protected_packet);

  // Protect packet number
  if (QUICInvariants::is_long_header(protected_packet)) {
    protected_packet[0] ^= mask[0] & 0x0f;
    QUICLongHeaderPacketR::packet_number_offset(pn_offset, protected_packet, protected_packet_len);
  } else {
    protected_packet[0] ^= mask[0] & 0x1f;
    QUICShortHeaderPacketR::packet_number_offset(pn_offset, protected_packet, protected_packet_len, dcil);
  }

  for (int i = 0; i < pn_length; ++i) {
    protected_packet[pn_offset + i] ^= mask[1 + i];
  }

  return true;
}
