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

#include "QUICPacketFactory.h"
#include "QUICPacketProtectionKeyInfo.h"
#include "QUICDebugNames.h"

using namespace std::literals;
static constexpr std::string_view tag   = "quic_packet"sv;
static constexpr std::string_view tag_v = "v_quic_packet"sv;

#define QUICDebug(dcid, scid, fmt, ...) \
  Debug(tag.data(), "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__);
#define QUICVDebug(dcid, scid, fmt, ...) \
  Debug(tag_v.data(), "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__);

//
// QUICPacketNumberGenerator
//
QUICPacketNumberGenerator::QUICPacketNumberGenerator() {}

QUICPacketNumber
QUICPacketNumberGenerator::next()
{
  // TODO Increment the number at least one but not only always one
  return this->_current++;
}

void
QUICPacketNumberGenerator::reset()
{
  this->_current = 0;
}

//
// QUICPacketFactory
//
QUICPacketUPtr
QUICPacketFactory::create_null_packet()
{
  return {nullptr, &QUICPacketDeleter::delete_null_packet};
}

QUICPacketUPtr
QUICPacketFactory::create(IpEndpoint from, ats_unique_buf buf, size_t len, QUICPacketNumber base_packet_number,
                          QUICPacketCreationResult &result)
{
  size_t max_plain_txt_len = 2048;
  ats_unique_buf plain_txt = ats_unique_malloc(max_plain_txt_len);
  size_t plain_txt_len     = 0;

  QUICPacketHeaderUPtr header = QUICPacketHeader::load(from, std::move(buf), len, base_packet_number);

  QUICConnectionId dcid = header->destination_cid();
  QUICConnectionId scid = header->source_cid();
  QUICVDebug(scid, dcid, "Decrypting %s packet #%" PRIu64 " using %s", QUICDebugNames::packet_type(header->type()),
             header->packet_number(), QUICDebugNames::key_phase(header->key_phase()));

  if (header->has_version() && !QUICTypeUtil::is_supported_version(header->version())) {
    if (header->type() == QUICPacketType::VERSION_NEGOTIATION) {
      // version of VN packet is 0x00000000
      // This packet is unprotected. Just copy the payload
      result = QUICPacketCreationResult::SUCCESS;
      memcpy(plain_txt.get(), header->payload(), header->payload_size());
      plain_txt_len = header->payload_size();
    } else {
      // We can't decrypt packets that have unknown versions
      // What we can use is invariant field of Long Header - version, dcid, and scid
      result = QUICPacketCreationResult::UNSUPPORTED;
    }
  } else {
    switch (header->type()) {
    case QUICPacketType::STATELESS_RESET:
    case QUICPacketType::RETRY:
      // These packets are unprotected. Just copy the payload
      memcpy(plain_txt.get(), header->payload(), header->payload_size());
      plain_txt_len = header->payload_size();
      result        = QUICPacketCreationResult::SUCCESS;
      break;
    case QUICPacketType::PROTECTED:
      if (this->_pp_key_info.is_decryption_key_available(header->key_phase())) {
        if (this->_pp_protector.unprotect(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(),
                                          header->payload_size(), header->packet_number(), header->buf(), header->size(),
                                          header->key_phase())) {
          result = QUICPacketCreationResult::SUCCESS;
        } else {
          result = QUICPacketCreationResult::FAILED;
        }
      } else {
        result = QUICPacketCreationResult::NOT_READY;
      }
      break;
    case QUICPacketType::INITIAL:
      if (this->_pp_key_info.is_decryption_key_available(QUICKeyPhase::INITIAL)) {
        if (QUICTypeUtil::is_supported_version(header->version())) {
          if (this->_pp_protector.unprotect(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(),
                                            header->payload_size(), header->packet_number(), header->buf(), header->size(),
                                            QUICKeyPhase::INITIAL)) {
            result = QUICPacketCreationResult::SUCCESS;
          } else {
            result = QUICPacketCreationResult::FAILED;
          }
        } else {
          result = QUICPacketCreationResult::SUCCESS;
        }
      } else {
        result = QUICPacketCreationResult::IGNORED;
      }
      break;
    case QUICPacketType::HANDSHAKE:
      if (this->_pp_key_info.is_decryption_key_available(QUICKeyPhase::HANDSHAKE)) {
        if (this->_pp_protector.unprotect(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(),
                                          header->payload_size(), header->packet_number(), header->buf(), header->size(),
                                          QUICKeyPhase::HANDSHAKE)) {
          result = QUICPacketCreationResult::SUCCESS;
        } else {
          result = QUICPacketCreationResult::FAILED;
        }
      } else {
        result = QUICPacketCreationResult::IGNORED;
      }
      break;
    case QUICPacketType::ZERO_RTT_PROTECTED:
      if (this->_pp_key_info.is_decryption_key_available(QUICKeyPhase::ZERO_RTT)) {
        if (this->_pp_protector.unprotect(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(),
                                          header->payload_size(), header->packet_number(), header->buf(), header->size(),
                                          QUICKeyPhase::ZERO_RTT)) {
          result = QUICPacketCreationResult::SUCCESS;
        } else {
          result = QUICPacketCreationResult::IGNORED;
        }
      } else {
        result = QUICPacketCreationResult::NOT_READY;
      }
      break;
    default:
      result = QUICPacketCreationResult::FAILED;
      break;
    }
  }

  QUICPacket *packet = nullptr;
  if (result == QUICPacketCreationResult::SUCCESS || result == QUICPacketCreationResult::UNSUPPORTED) {
    packet = quicPacketAllocator.alloc();
    new (packet) QUICPacket(std::move(header), std::move(plain_txt), plain_txt_len);
  }

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_packet);
}

QUICPacketUPtr
QUICPacketFactory::create_version_negotiation_packet(QUICConnectionId dcid, QUICConnectionId scid)
{
  size_t len = sizeof(QUICVersion) * (countof(QUIC_SUPPORTED_VERSIONS) + 1);
  ats_unique_buf versions(reinterpret_cast<uint8_t *>(ats_malloc(len)));
  uint8_t *p = versions.get();

  size_t n;
  for (auto v : QUIC_SUPPORTED_VERSIONS) {
    QUICTypeUtil::write_QUICVersion(v, p, &n);
    p += n;
  }

  // [draft-18] 6.3. Using Reserved Versions
  // To help ensure this, a server SHOULD include a reserved version (see Section 15) while generating a
  // Version Negotiation packet.
  QUICTypeUtil::write_QUICVersion(QUIC_EXERCISE_VERSION, p, &n);
  p += n;

  ink_assert(len == static_cast<size_t>(p - versions.get()));
  // VN packet dosen't have packet number field and version field is always 0x00000000
  QUICPacketHeaderUPtr header = QUICPacketHeader::build(QUICPacketType::VERSION_NEGOTIATION, QUICKeyPhase::INITIAL, dcid, scid,
                                                        0x00, 0x00, 0x00, false, std::move(versions), len);

  return QUICPacketFactory::_create_unprotected_packet(std::move(header));
}

QUICPacketUPtr
QUICPacketFactory::create_initial_packet(QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                         QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len,
                                         bool retransmittable, bool probing, bool crypto, ats_unique_buf token, size_t token_len)
{
  int index           = QUICTypeUtil::pn_space_index(QUICEncryptionLevel::INITIAL);
  QUICPacketNumber pn = this->_packet_number_generator[index].next();
  QUICPacketHeaderUPtr header =
    QUICPacketHeader::build(QUICPacketType::INITIAL, QUICKeyPhase::INITIAL, destination_cid, source_cid, pn, base_packet_number,
                            this->_version, crypto, std::move(payload), len, std::move(token), token_len);
  return this->_create_encrypted_packet(std::move(header), retransmittable, probing);
}

QUICPacketUPtr
QUICPacketFactory::create_retry_packet(QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                       QUICConnectionId original_dcid, QUICRetryToken &token)
{
  ats_unique_buf payload = ats_unique_malloc(token.length());
  memcpy(payload.get(), token.buf(), token.length());

  QUICPacketHeaderUPtr header =
    QUICPacketHeader::build(QUICPacketType::RETRY, QUICKeyPhase::INITIAL, QUIC_SUPPORTED_VERSIONS[0], destination_cid, source_cid,
                            original_dcid, std::move(payload), token.length());
  return QUICPacketFactory::_create_unprotected_packet(std::move(header));
}

QUICPacketUPtr
QUICPacketFactory::create_handshake_packet(QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                           QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len,
                                           bool retransmittable, bool probing, bool crypto)
{
  int index           = QUICTypeUtil::pn_space_index(QUICEncryptionLevel::HANDSHAKE);
  QUICPacketNumber pn = this->_packet_number_generator[index].next();
  QUICPacketHeaderUPtr header =
    QUICPacketHeader::build(QUICPacketType::HANDSHAKE, QUICKeyPhase::HANDSHAKE, destination_cid, source_cid, pn, base_packet_number,
                            this->_version, crypto, std::move(payload), len);
  return this->_create_encrypted_packet(std::move(header), retransmittable, probing);
}

QUICPacketUPtr
QUICPacketFactory::create_zero_rtt_packet(QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                          QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len,
                                          bool retransmittable, bool probing)
{
  int index           = QUICTypeUtil::pn_space_index(QUICEncryptionLevel::ZERO_RTT);
  QUICPacketNumber pn = this->_packet_number_generator[index].next();
  QUICPacketHeaderUPtr header =
    QUICPacketHeader::build(QUICPacketType::ZERO_RTT_PROTECTED, QUICKeyPhase::ZERO_RTT, destination_cid, source_cid, pn,
                            base_packet_number, this->_version, false, std::move(payload), len);
  return this->_create_encrypted_packet(std::move(header), retransmittable, probing);
}

QUICPacketUPtr
QUICPacketFactory::create_protected_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number,
                                           ats_unique_buf payload, size_t len, bool retransmittable, bool probing)
{
  int index           = QUICTypeUtil::pn_space_index(QUICEncryptionLevel::ONE_RTT);
  QUICPacketNumber pn = this->_packet_number_generator[index].next();
  // TODO Key phase should be picked up from QUICHandshakeProtocol, probably
  QUICPacketHeaderUPtr header = QUICPacketHeader::build(QUICPacketType::PROTECTED, QUICKeyPhase::PHASE_0, connection_id, pn,
                                                        base_packet_number, std::move(payload), len);
  return this->_create_encrypted_packet(std::move(header), retransmittable, probing);
}

QUICPacketUPtr
QUICPacketFactory::create_stateless_reset_packet(QUICConnectionId connection_id, QUICStatelessResetToken stateless_reset_token)
{
  std::random_device rnd;

  uint8_t random_packet_number = static_cast<uint8_t>(rnd() & 0xFF);
  size_t payload_len           = static_cast<uint8_t>((rnd() & 0xFF) | 16); // Mimimum length has to be 16
  ats_unique_buf payload       = ats_unique_malloc(payload_len + 16);
  uint8_t *naked_payload       = payload.get();

  // Generate random octets
  for (int i = payload_len - 1; i >= 0; --i) {
    naked_payload[i] = static_cast<uint8_t>(rnd() & 0xFF);
  }
  // Copy stateless reset token into payload
  memcpy(naked_payload + payload_len - 16, stateless_reset_token.buf(), 16);

  // KeyPhase won't be used
  QUICPacketHeaderUPtr header = QUICPacketHeader::build(QUICPacketType::STATELESS_RESET, QUICKeyPhase::INITIAL, connection_id,
                                                        random_packet_number, 0, std::move(payload), payload_len);
  return QUICPacketFactory::_create_unprotected_packet(std::move(header));
}

QUICPacketUPtr
QUICPacketFactory::_create_unprotected_packet(QUICPacketHeaderUPtr header)
{
  ats_unique_buf cleartext = ats_unique_malloc(2048);
  size_t cleartext_len     = header->payload_size();

  memcpy(cleartext.get(), header->payload(), cleartext_len);
  QUICPacket *packet = quicPacketAllocator.alloc();
  new (packet) QUICPacket(std::move(header), std::move(cleartext), cleartext_len, false, false);

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_packet);
}

QUICPacketUPtr
QUICPacketFactory::_create_encrypted_packet(QUICPacketHeaderUPtr header, bool retransmittable, bool probing)
{
  // TODO: use pmtu of UnixNetVConnection
  size_t max_cipher_txt_len = 2048;
  ats_unique_buf cipher_txt = ats_unique_malloc(max_cipher_txt_len);
  size_t cipher_txt_len     = 0;

  QUICConnectionId dcid = header->destination_cid();
  QUICConnectionId scid = header->source_cid();
  QUICVDebug(dcid, scid, "Encrypting %s packet #%" PRIu64 " using %s", QUICDebugNames::packet_type(header->type()),
             header->packet_number(), QUICDebugNames::key_phase(header->key_phase()));

  QUICPacket *packet = nullptr;
  if (this->_pp_protector.protect(cipher_txt.get(), cipher_txt_len, max_cipher_txt_len, header->payload(), header->payload_size(),
                                  header->packet_number(), header->buf(), header->size(), header->key_phase())) {
    packet = quicPacketAllocator.alloc();
    new (packet) QUICPacket(std::move(header), std::move(cipher_txt), cipher_txt_len, retransmittable, probing);
  } else {
    QUICDebug(dcid, scid, "Failed to encrypt a packet");
  }

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_packet);
}

void
QUICPacketFactory::set_version(QUICVersion negotiated_version)
{
  this->_version = negotiated_version;
}

bool
QUICPacketFactory::is_ready_to_create_protected_packet()
{
  return this->_pp_key_info.is_encryption_key_available(QUICKeyPhase::PHASE_0) ||
         this->_pp_key_info.is_encryption_key_available(QUICKeyPhase::PHASE_1);
}

void
QUICPacketFactory::reset()
{
  for (auto s : QUIC_PN_SPACES) {
    this->_packet_number_generator[static_cast<int>(s)].reset();
  }
}
