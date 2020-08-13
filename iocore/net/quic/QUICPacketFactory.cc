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
QUICPacketFactory::create(uint8_t *packet_buf, UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, ats_unique_buf buf,
                          size_t len, QUICPacketNumber base_packet_number, QUICPacketCreationResult &result)
{
  QUICPacket *packet = nullptr;

  // FIXME This is temporal. Receive IOBufferBlock from the caller.
  Ptr<IOBufferBlock> whole_data = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  whole_data->alloc(iobuffer_size_to_index(len, BUFFER_SIZE_INDEX_32K));
  memcpy(whole_data->start(), buf.get(), len);
  whole_data->fill(len);

  QUICPacketType type;
  QUICVersion version;
  QUICConnectionId dcid;
  QUICConnectionId scid;
  QUICPacketNumber packet_number;
  QUICKeyPhase key_phase;

  if (QUICPacketR::read_essential_info(whole_data, type, version, dcid, scid, packet_number, base_packet_number, key_phase)) {
    QUICVDebug(scid, dcid, "Decrypting %s packet #%" PRIu64 " using %s", QUICDebugNames::packet_type(type), packet_number,
               QUICDebugNames::key_phase(key_phase));

    if (type != QUICPacketType::PROTECTED && !QUICTypeUtil::is_supported_version(version)) {
      if (type == QUICPacketType::VERSION_NEGOTIATION) {
        packet = new QUICVersionNegotiationPacketR(udp_con, from, to, whole_data);
        result = QUICPacketCreationResult::SUCCESS;
      } else {
        // We can't decrypt packets that have unknown versions
        // What we can use is invariant field of Long Header - version, dcid, and scid
        result = QUICPacketCreationResult::UNSUPPORTED;
      }
    } else {
      Ptr<IOBufferBlock> plain;
      switch (type) {
      case QUICPacketType::STATELESS_RESET:
        packet = new (packet_buf) QUICStatelessResetPacketR(udp_con, from, to, whole_data);
        result = QUICPacketCreationResult::SUCCESS;
        break;
      case QUICPacketType::RETRY:
        packet = new (packet_buf) QUICRetryPacketR(udp_con, from, to, whole_data);
        result = QUICPacketCreationResult::SUCCESS;
        break;
      case QUICPacketType::PROTECTED:
        packet = new (packet_buf) QUICShortHeaderPacketR(udp_con, from, to, whole_data, base_packet_number);
        if (this->_pp_key_info.is_decryption_key_available(packet->key_phase())) {
          plain = this->_pp_protector.unprotect(packet->header_block(), packet->payload_block(), packet->packet_number(),
                                                packet->key_phase());
          if (plain != nullptr) {
            static_cast<QUICShortHeaderPacketR *>(packet)->attach_payload(plain, true);
            result = QUICPacketCreationResult::SUCCESS;
          } else {
            result = QUICPacketCreationResult::FAILED;
          }
        } else {
          result = QUICPacketCreationResult::NOT_READY;
        }
        break;
      case QUICPacketType::INITIAL:
        packet = new (packet_buf) QUICInitialPacketR(udp_con, from, to, whole_data, base_packet_number);
        if (this->_pp_key_info.is_decryption_key_available(QUICKeyPhase::INITIAL)) {
          plain = this->_pp_protector.unprotect(packet->header_block(), packet->payload_block(), packet->packet_number(),
                                                packet->key_phase());
          if (plain != nullptr) {
            static_cast<QUICInitialPacketR *>(packet)->attach_payload(plain, true);
            result = QUICPacketCreationResult::SUCCESS;
          } else {
            result = QUICPacketCreationResult::FAILED;
          }
        } else {
          result = QUICPacketCreationResult::IGNORED;
        }
        break;
      case QUICPacketType::HANDSHAKE:
        packet = new (packet_buf) QUICHandshakePacketR(udp_con, from, to, whole_data, base_packet_number);
        if (this->_pp_key_info.is_decryption_key_available(QUICKeyPhase::HANDSHAKE)) {
          plain = this->_pp_protector.unprotect(packet->header_block(), packet->payload_block(), packet->packet_number(),
                                                packet->key_phase());
          if (plain != nullptr) {
            static_cast<QUICHandshakePacketR *>(packet)->attach_payload(plain, true);
            result = QUICPacketCreationResult::SUCCESS;
          } else {
            result = QUICPacketCreationResult::FAILED;
          }
        } else {
          result = QUICPacketCreationResult::IGNORED;
        }
        break;
      case QUICPacketType::ZERO_RTT_PROTECTED:
        packet = new (packet_buf) QUICZeroRttPacketR(udp_con, from, to, whole_data, base_packet_number);
        if (this->_pp_key_info.is_decryption_key_available(QUICKeyPhase::ZERO_RTT)) {
          plain = this->_pp_protector.unprotect(packet->header_block(), packet->payload_block(), packet->packet_number(),
                                                packet->key_phase());
          if (plain != nullptr) {
            static_cast<QUICZeroRttPacketR *>(packet)->attach_payload(plain, true);
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
  } else {
    Debug(tag.data(), "Failed to read essential field");
    uint8_t *buf = reinterpret_cast<uint8_t *>(whole_data->start());
    if (len > 16) {
      Debug(tag_v.data(), "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", buf[0], buf[1], buf[2],
            buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
    }
    if (len > 32) {
      Debug(tag_v.data(), "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", buf[16 + 0],
            buf[16 + 1], buf[16 + 2], buf[16 + 3], buf[16 + 4], buf[16 + 5], buf[16 + 6], buf[16 + 7], buf[16 + 8], buf[16 + 9],
            buf[16 + 10], buf[16 + 11], buf[16 + 12], buf[16 + 13], buf[16 + 14], buf[16 + 15]);
    }
    if (len > 48) {
      Debug(tag_v.data(), "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", buf[32 + 0],
            buf[32 + 1], buf[32 + 2], buf[32 + 3], buf[32 + 4], buf[32 + 5], buf[32 + 6], buf[32 + 7], buf[32 + 8], buf[32 + 9],
            buf[32 + 10], buf[32 + 11], buf[32 + 12], buf[32 + 13], buf[32 + 14], buf[32 + 15]);
    }
    result = QUICPacketCreationResult::FAILED;
  }

  if (result != QUICPacketCreationResult::SUCCESS && result != QUICPacketCreationResult::UNSUPPORTED) {
    packet = nullptr;
  }

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_dont_free);
}

QUICPacketUPtr
QUICPacketFactory::create_version_negotiation_packet(QUICConnectionId dcid, QUICConnectionId scid, QUICVersion version_in_initial)
{
  return QUICPacketUPtr(
    new QUICVersionNegotiationPacket(dcid, scid, QUIC_SUPPORTED_VERSIONS, countof(QUIC_SUPPORTED_VERSIONS), version_in_initial),
    &QUICPacketDeleter::delete_packet_new);
}

QUICPacketUPtr
QUICPacketFactory::create_initial_packet(uint8_t *packet_buf, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                         QUICPacketNumber base_packet_number, Ptr<IOBufferBlock> payload, size_t length,
                                         bool ack_eliciting, bool probing, bool crypto, ats_unique_buf token, size_t token_len)
{
  QUICPacketNumberSpace index = QUICTypeUtil::pn_space(QUICEncryptionLevel::INITIAL);
  QUICPacketNumber pn         = this->_packet_number_generator[static_cast<int>(index)].next();

  QUICInitialPacket *packet = new (packet_buf) QUICInitialPacket(this->_version, destination_cid, source_cid, token_len,
                                                                 std::move(token), length, pn, ack_eliciting, probing, crypto);

  packet->attach_payload(payload, true); // Attach a cleartext payload with extra headers
  Ptr<IOBufferBlock> protected_payload =
    this->_pp_protector.protect(packet->header_block(), packet->payload_block(), packet->packet_number(), packet->key_phase());
  if (protected_payload != nullptr) {
    packet->attach_payload(protected_payload, false); // Replace its payload with the protected payload
  } else {
    QUICDebug(destination_cid, source_cid, "Failed to encrypt a packet");
    packet = nullptr;
  }

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_dont_free);
}

QUICPacketUPtr
QUICPacketFactory::create_retry_packet(QUICVersion version, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                       QUICRetryToken &token)
{
  return QUICPacketUPtr(new QUICRetryPacket(version, destination_cid, source_cid, token), &QUICPacketDeleter::delete_packet_new);
}

QUICPacketUPtr
QUICPacketFactory::create_handshake_packet(uint8_t *packet_buf, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                           QUICPacketNumber base_packet_number, Ptr<IOBufferBlock> payload, size_t length,
                                           bool ack_eliciting, bool probing, bool crypto)
{
  QUICPacketNumberSpace index = QUICTypeUtil::pn_space(QUICEncryptionLevel::HANDSHAKE);
  QUICPacketNumber pn         = this->_packet_number_generator[static_cast<int>(index)].next();

  QUICHandshakePacket *packet =
    new (packet_buf) QUICHandshakePacket(this->_version, destination_cid, source_cid, length, pn, ack_eliciting, probing, crypto);

  packet->attach_payload(payload, true); // Attach a cleartext payload with extra headers
  Ptr<IOBufferBlock> protected_payload =
    this->_pp_protector.protect(packet->header_block(), packet->payload_block(), packet->packet_number(), packet->key_phase());
  if (protected_payload != nullptr) {
    packet->attach_payload(protected_payload, false); // Replace its payload with the protected payload
  } else {
    QUICDebug(destination_cid, source_cid, "Failed to encrypt a packet");
    packet = nullptr;
  }

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_dont_free);
}

QUICPacketUPtr
QUICPacketFactory::create_zero_rtt_packet(uint8_t *packet_buf, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                          QUICPacketNumber base_packet_number, Ptr<IOBufferBlock> payload, size_t length,
                                          bool ack_eliciting, bool probing)
{
  QUICPacketNumberSpace index = QUICTypeUtil::pn_space(QUICEncryptionLevel::ZERO_RTT);
  QUICPacketNumber pn         = this->_packet_number_generator[static_cast<int>(index)].next();

  QUICZeroRttPacket *packet =
    new (packet_buf) QUICZeroRttPacket(this->_version, destination_cid, source_cid, length, pn, ack_eliciting, probing);

  packet->attach_payload(payload, true); // Attach a cleartext payload with extra headers
  Ptr<IOBufferBlock> protected_payload =
    this->_pp_protector.protect(packet->header_block(), packet->payload_block(), packet->packet_number(), packet->key_phase());
  if (protected_payload != nullptr) {
    packet->attach_payload(protected_payload, false); // Replace its payload with the protected payload
  } else {
    QUICDebug(destination_cid, source_cid, "Failed to encrypt a packet");
    packet = nullptr;
  }

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_dont_free);
}

QUICPacketUPtr
QUICPacketFactory::create_short_header_packet(uint8_t *packet_buf, QUICConnectionId destination_cid,
                                              QUICPacketNumber base_packet_number, Ptr<IOBufferBlock> payload, size_t length,
                                              bool ack_eliciting, bool probing)
{
  QUICPacketNumberSpace index = QUICTypeUtil::pn_space(QUICEncryptionLevel::ONE_RTT);
  QUICPacketNumber pn         = this->_packet_number_generator[static_cast<int>(index)].next();

  // TODO Key phase should be picked up from QUICHandshakeProtocol, probably
  QUICShortHeaderPacket *packet =
    new (packet_buf) QUICShortHeaderPacket(destination_cid, pn, base_packet_number, QUICKeyPhase::PHASE_0, ack_eliciting, probing);

  packet->attach_payload(payload, true); // Attach a cleartext payload with extra headers
  Ptr<IOBufferBlock> protected_payload =
    this->_pp_protector.protect(packet->header_block(), packet->payload_block(), packet->packet_number(), packet->key_phase());
  if (protected_payload != nullptr) {
    packet->attach_payload(protected_payload, false); // Replace its payload with the protected payload
  } else {
    QUICDebug(destination_cid, QUICConnectionId::ZERO(), "Failed to encrypt a packet");
    packet = nullptr;
  }

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_dont_free);
}

QUICPacketUPtr
QUICPacketFactory::create_stateless_reset_packet(QUICStatelessResetToken stateless_reset_token, size_t maximum_size)
{
  return QUICPacketUPtr(new QUICStatelessResetPacket(stateless_reset_token, maximum_size), &QUICPacketDeleter::delete_packet_new);
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
  for (auto i = 0; i < QUIC_N_PACKET_SPACES; i++) {
    this->_packet_number_generator[i].reset();
  }
}
