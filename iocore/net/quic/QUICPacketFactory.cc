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
  size_t max_plain_txt_len = 2048;
  ats_unique_buf plain_txt = ats_unique_malloc(max_plain_txt_len);
  size_t plain_txt_len     = 0;

  QUICPacketHeaderUPtr header = QUICPacketHeader::load(from, to, std::move(buf), len, base_packet_number);

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
    Ptr<IOBufferBlock> plain         = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    Ptr<IOBufferBlock> protected_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    protected_ibb->set_internal(reinterpret_cast<void *>(const_cast<uint8_t *>(header->payload())), header->payload_size(),
                                BUFFER_SIZE_NOT_ALLOCATED);
    Ptr<IOBufferBlock> header_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    header_ibb->set_internal(reinterpret_cast<void *>(const_cast<uint8_t *>(header->buf())), header->size(),
                             BUFFER_SIZE_NOT_ALLOCATED);

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
        plain = this->_pp_protector.unprotect(header_ibb, protected_ibb, header->packet_number(), header->key_phase());
        if (plain != nullptr) {
          memcpy(plain_txt.get(), plain->buf(), plain->size());
          plain_txt_len = plain->size();
          result        = QUICPacketCreationResult::SUCCESS;
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
          plain = this->_pp_protector.unprotect(header_ibb, protected_ibb, header->packet_number(), header->key_phase());
          if (plain != nullptr) {
            memcpy(plain_txt.get(), plain->buf(), plain->size());
            plain_txt_len = plain->size();
            result        = QUICPacketCreationResult::SUCCESS;
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
        plain = this->_pp_protector.unprotect(header_ibb, protected_ibb, header->packet_number(), header->key_phase());
        if (plain != nullptr) {
          memcpy(plain_txt.get(), plain->buf(), plain->size());
          plain_txt_len = plain->size();
          result        = QUICPacketCreationResult::SUCCESS;
        } else {
          result = QUICPacketCreationResult::FAILED;
        }
      } else {
        result = QUICPacketCreationResult::IGNORED;
      }
      break;
    case QUICPacketType::ZERO_RTT_PROTECTED:
      if (this->_pp_key_info.is_decryption_key_available(QUICKeyPhase::ZERO_RTT)) {
        plain = this->_pp_protector.unprotect(header_ibb, protected_ibb, header->packet_number(), header->key_phase());
        if (plain != nullptr) {
          memcpy(plain_txt.get(), plain->buf(), plain->size());
          plain_txt_len = plain->size();
          result        = QUICPacketCreationResult::SUCCESS;
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
    packet = new (packet_buf) QUICPacket(udp_con, std::move(header), std::move(plain_txt), plain_txt_len);
  }

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_dont_free);
}

QUICPacketUPtr
QUICPacketFactory::create_version_negotiation_packet(QUICConnectionId dcid, QUICConnectionId scid)
{
  return QUICPacketUPtr(new QUICVersionNegotiationPacket(dcid, scid, QUIC_SUPPORTED_VERSIONS, countof(QUIC_SUPPORTED_VERSIONS)),
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
QUICPacketFactory::create_retry_packet(QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                       QUICConnectionId original_dcid, QUICRetryToken &token)
{
  return QUICPacketUPtr(new QUICRetryPacket(QUIC_SUPPORTED_VERSIONS[0], destination_cid, source_cid, original_dcid, token),
                        &QUICPacketDeleter::delete_packet_new);
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
QUICPacketFactory::create_stateless_reset_packet(QUICStatelessResetToken stateless_reset_token)
{
  return QUICPacketUPtr(new QUICStatelessResetPacket(stateless_reset_token), &QUICPacketDeleter::delete_packet_new);
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
  for (auto i = 0; i < kPacketNumberSpace; i++) {
    this->_packet_number_generator[i].reset();
  }
}
