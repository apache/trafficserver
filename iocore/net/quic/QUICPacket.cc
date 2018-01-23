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

#include <ts/ink_assert.h>
#include <ts/Diags.h>
#include "QUICPacket.h"

ClassAllocator<QUICPacket> quicPacketAllocator("quicPacketAllocator");
ClassAllocator<QUICPacketLongHeader> quicPacketLongHeaderAllocator("quicPacketLongHeaderAllocator");
ClassAllocator<QUICPacketShortHeader> quicPacketShortHeaderAllocator("quicPacketShortHeaderAllocator");

static constexpr int LONG_HDR_OFFSET_CONNECTION_ID = 1;
static constexpr int LONG_HDR_OFFSET_VERSION       = 9;
static constexpr int LONG_HDR_OFFSET_PACKET_NUMBER = 13;
static constexpr int LONG_HDR_OFFSET_PAYLOAD       = 17;
static constexpr int LONG_HDR_LENGTH               = 17;

static constexpr int SHORT_HDR_OFFSET_CONNECTION_ID = 1;

static constexpr int VERSION_NEGOTIATION_PKT_HEADER_LENGTH = 13;

//
// QUICPacketHeader
//
const uint8_t *
QUICPacketHeader::buf()
{
  if (this->_buf) {
    return this->_buf;
  } else {
    // TODO Reuse serialzied data if nothing has changed
    size_t dummy;
    this->store(this->_serialized, &dummy);
    return this->_serialized;
  }
}

uint16_t
QUICPacketHeader::packet_size() const
{
  return this->_buf_len;
}

QUICPacketHeader *
QUICPacketHeader::load(const uint8_t *buf, size_t len, QUICPacketNumber base)
{
  if (QUICTypeUtil::has_long_header(buf)) {
    QUICPacketLongHeader *long_header = quicPacketLongHeaderAllocator.alloc();
    new (long_header) QUICPacketLongHeader(buf, len, base);
    return long_header;
  } else {
    QUICPacketShortHeader *short_header = quicPacketShortHeaderAllocator.alloc();
    new (short_header) QUICPacketShortHeader(buf, len, base);
    return short_header;
  }
}

QUICPacketHeader *
QUICPacketHeader::build(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                        QUICPacketNumber base_packet_number, QUICVersion version, ats_unique_buf payload, size_t len)
{
  QUICPacketLongHeader *long_header = quicPacketLongHeaderAllocator.alloc();
  new (long_header) QUICPacketLongHeader(type, connection_id, packet_number, base_packet_number, version, std::move(payload), len);
  return long_header;
}

QUICPacketHeader *
QUICPacketHeader::build(QUICPacketType type, QUICKeyPhase key_phase, QUICPacketNumber packet_number,
                        QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len)
{
  QUICPacketShortHeader *short_header = quicPacketShortHeaderAllocator.alloc();
  new (short_header) QUICPacketShortHeader(type, key_phase, packet_number, base_packet_number, std::move(payload), len);
  return short_header;
}

QUICPacketHeader *
QUICPacketHeader::build(QUICPacketType type, QUICKeyPhase key_phase, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                        QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len)
{
  QUICPacketShortHeader *short_header = quicPacketShortHeaderAllocator.alloc();
  new (short_header)
    QUICPacketShortHeader(type, key_phase, connection_id, packet_number, base_packet_number, std::move(payload), len);
  return short_header;
}

QUICPacketHeader *
QUICPacketHeader::clone() const
{
  return nullptr;
}

//
// QUICPacketLongHeader
//
QUICPacketLongHeader::QUICPacketLongHeader(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                                           QUICPacketNumber base_packet_number, QUICVersion version, ats_unique_buf buf, size_t len)
{
  this->_type               = type;
  this->_has_connection_id  = true;
  this->_connection_id      = connection_id;
  this->_packet_number      = packet_number;
  this->_base_packet_number = base_packet_number;
  this->_has_version        = true;
  this->_version            = version;
  this->_payload            = std::move(buf);
  this->_payload_len        = len;
}

QUICPacketType
QUICPacketLongHeader::type() const
{
  if (this->_buf) {
    uint8_t type = this->_buf[0] & 0x7F;
    if (this->version() == 0x00) {
      return QUICPacketType::VERSION_NEGOTIATION;
    } else {
      // any other version-specific type?
      return static_cast<QUICPacketType>(type);
    }
  } else {
    return this->_type;
  }
}

QUICConnectionId
QUICPacketLongHeader::connection_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICConnectionId(this->_buf + LONG_HDR_OFFSET_CONNECTION_ID, 8);
  } else {
    return this->_connection_id;
  }
}

QUICPacketNumber
QUICPacketLongHeader::packet_number() const
{
  if (this->_buf) {
    const uint8_t packet_number_len = 4;
    QUICPacketNumber src = QUICTypeUtil::read_QUICPacketNumber(this->_buf + LONG_HDR_OFFSET_PACKET_NUMBER, packet_number_len);
    QUICPacketNumber dst = 0;
    QUICPacket::decode_packet_number(dst, src, packet_number_len, this->_base_packet_number);

    return dst;
  } else {
    return this->_packet_number;
  }
}

bool
QUICPacketLongHeader::has_version() const
{
  return true;
}

QUICVersion
QUICPacketLongHeader::version() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVersion(this->_buf + LONG_HDR_OFFSET_VERSION);
  } else {
    return this->_version;
  }
}

bool
QUICPacketLongHeader::has_connection_id() const
{
  return true;
}

const uint8_t *
QUICPacketLongHeader::payload() const
{
  if (this->_buf) {
    return this->_buf + LONG_HDR_OFFSET_PAYLOAD;
  } else {
    return this->_payload.get();
  }
}

uint16_t
QUICPacketHeader::payload_size() const
{
  if (this->_buf) {
    return this->_buf_len - this->size();
  } else {
    return this->_payload_len;
  }
}

bool
QUICPacketLongHeader::has_key_phase() const
{
  return false;
}

QUICKeyPhase
QUICPacketLongHeader::key_phase() const
{
  // TODO LongHeader will also be used for 0-RTT packets
  return QUICKeyPhase::CLEARTEXT;
}

uint16_t
QUICPacketLongHeader::size() const
{
  if (this->type() == QUICPacketType::VERSION_NEGOTIATION) {
    return VERSION_NEGOTIATION_PKT_HEADER_LENGTH;
  } else {
    return LONG_HDR_LENGTH;
  }
}

void
QUICPacketLongHeader::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  *len   = 0;
  buf[0] = 0x80;
  buf[0] += static_cast<uint8_t>(this->_type);
  if (this->_type == QUICPacketType::VERSION_NEGOTIATION) {
    buf[0] |= rand();
  }
  *len += 1;

  QUICTypeUtil::write_QUICConnectionId(this->_connection_id, 8, buf + *len, &n);
  *len += n;

  QUICTypeUtil::write_QUICVersion(this->_version, buf + *len, &n);
  *len += n;

  QUICPacketNumber dst = 0;
  size_t dst_len       = 4;
  QUICPacket::encode_packet_number(dst, this->_packet_number, dst_len);
  QUICTypeUtil::write_QUICPacketNumber(dst, dst_len, buf + *len, &n);
  *len += n;
}

//
// QUICPacketShortHeader
//
QUICPacketShortHeader::QUICPacketShortHeader(QUICPacketType type, QUICKeyPhase key_phase, QUICPacketNumber packet_number,
                                             QUICPacketNumber base_packet_number, ats_unique_buf buf, size_t len)
{
  this->_type               = type;
  this->_has_key_phase      = true;
  this->_key_phase          = key_phase;
  this->_packet_number      = packet_number;
  this->_base_packet_number = base_packet_number;
  this->_packet_number_type = this->_discover_packet_number_type(packet_number, base_packet_number);
  this->_payload            = std::move(buf);
  this->_payload_len        = len;
}

QUICPacketShortHeader::QUICPacketShortHeader(QUICPacketType type, QUICKeyPhase key_phase, QUICConnectionId connection_id,
                                             QUICPacketNumber packet_number, QUICPacketNumber base_packet_number,
                                             ats_unique_buf buf, size_t len)
{
  this->_type               = type;
  this->_has_key_phase      = true;
  this->_key_phase          = key_phase;
  this->_has_connection_id  = true;
  this->_connection_id      = connection_id;
  this->_packet_number      = packet_number;
  this->_base_packet_number = base_packet_number;
  this->_packet_number_type = this->_discover_packet_number_type(packet_number, base_packet_number);
  this->_payload            = std::move(buf);
  this->_payload_len        = len;
}

QUICPacketType
QUICPacketShortHeader::type() const
{
  QUICKeyPhase key_phase = this->key_phase();

  switch (key_phase) {
  case QUICKeyPhase::PHASE_0: {
    return QUICPacketType::PROTECTED;
  }
  case QUICKeyPhase::PHASE_1: {
    return QUICPacketType::PROTECTED;
  }
  default:
    return QUICPacketType::STATELESS_RESET;
  }
}

QUICConnectionId
QUICPacketShortHeader::connection_id() const
{
  if (this->_buf) {
    ink_release_assert(this->has_connection_id());
    return QUICTypeUtil::read_QUICConnectionId(this->_buf + SHORT_HDR_OFFSET_CONNECTION_ID, 8);
  } else {
    return _connection_id;
  }
}

QUICPacketNumber
QUICPacketShortHeader::packet_number() const
{
  if (this->_buf) {
    int n      = this->_packet_number_len();
    int offset = SHORT_HDR_OFFSET_CONNECTION_ID;
    if (this->has_connection_id()) {
      offset += 8;
    }

    QUICPacketNumber src = QUICTypeUtil::read_QUICPacketNumber(this->_buf + offset, n);
    QUICPacketNumber dst = 0;
    QUICPacket::decode_packet_number(dst, src, n, this->_base_packet_number);

    return dst;
  } else {
    return this->_packet_number;
  }
}

bool
QUICPacketShortHeader::has_version() const
{
  return false;
}

QUICVersion
QUICPacketShortHeader::version() const
{
  return 0;
}

int
QUICPacketShortHeader::_packet_number_len() const
{
  QUICPacketShortHeaderType type;
  if (this->_buf) {
    type = static_cast<QUICPacketShortHeaderType>(this->_buf[0] & 0x1F);
  } else {
    type = this->_packet_number_type;
  }

  switch (type) {
  case QUICPacketShortHeaderType::ONE:
    return 1;
  case QUICPacketShortHeaderType::TWO:
    return 2;
  case QUICPacketShortHeaderType::THREE:
    return 4;
  default:
    ink_assert(false);
    return 0;
  }
}

QUICPacketShortHeaderType
QUICPacketShortHeader::_discover_packet_number_type(QUICPacketNumber packet_number, QUICPacketNumber base_packet_number) const
{
  uint64_t d = (packet_number - base_packet_number) * 2;

  if (d > 0xFFFF) {
    return QUICPacketShortHeaderType::THREE;
  } else if (d > 0xFF) {
    return QUICPacketShortHeaderType::TWO;
  } else {
    return QUICPacketShortHeaderType::ONE;
  }
}

bool
QUICPacketShortHeader::has_connection_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::has_connection_id(this->_buf);
  } else {
    return this->_has_connection_id;
  }
}

const uint8_t *
QUICPacketShortHeader::payload() const
{
  if (this->_buf) {
    return this->_buf + this->size();
  } else {
    return this->_payload.get();
  }
}

bool
QUICPacketShortHeader::has_key_phase() const
{
  return true;
}

QUICKeyPhase
QUICPacketShortHeader::key_phase() const
{
  if (this->_buf) {
    if (this->_buf[0] & 0x20) {
      return QUICKeyPhase::PHASE_1;
    } else {
      return QUICKeyPhase::PHASE_0;
    }
  } else {
    return _key_phase;
  }
}

/**
 * Header Length (doesn't include payload length)
 */
uint16_t
QUICPacketShortHeader::size() const
{
  uint16_t len = 1;

  if (this->has_connection_id()) {
    len += 8;
  }
  len += this->_packet_number_len();

  return len;
}

void
QUICPacketShortHeader::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  *len   = 0;
  buf[0] = 0x00;
  if (!this->_has_connection_id) {
    buf[0] += 0x40;
  }
  if (this->_key_phase == QUICKeyPhase::PHASE_1) {
    buf[0] += 0x20;
  }
  buf[0] += static_cast<uint8_t>(this->_packet_number_type);
  *len += 1;
  if (this->_has_connection_id) {
    QUICTypeUtil::write_QUICConnectionId(this->_connection_id, 8, buf + *len, &n);
    *len += n;
  }

  QUICPacketNumber dst = 0;
  size_t dst_len       = this->_packet_number_len();
  QUICPacket::encode_packet_number(dst, this->_packet_number, dst_len);
  QUICTypeUtil::write_QUICPacketNumber(dst, dst_len, buf + *len, &n);

  *len += n;
}

//
// QUICPacket
//

QUICPacket::QUICPacket(QUICPacketHeader *header, ats_unique_buf payload, size_t payload_len)
{
  this->_header       = header;
  this->_payload      = std::move(payload);
  this->_payload_size = payload_len;
}

QUICPacket::QUICPacket(QUICPacketHeader *header, ats_unique_buf payload, size_t payload_len, bool retransmittable)
{
  this->_header             = header;
  this->_payload            = std::move(payload);
  this->_payload_size       = payload_len;
  this->_is_retransmittable = retransmittable;
}

QUICPacket::~QUICPacket()
{
  if (this->_header->has_version()) {
    quicPacketLongHeaderAllocator.free(static_cast<QUICPacketLongHeader *>(this->_header));
  } else {
    quicPacketShortHeaderAllocator.free(static_cast<QUICPacketShortHeader *>(this->_header));
  }
}

/**
 * When packet is "Short Header Packet", QUICPacket::type() will return 1-RTT Protected (key phase 0)
 * or 1-RTT Protected (key phase 1)
 */
QUICPacketType
QUICPacket::type() const
{
  return this->_header->type();
}

QUICConnectionId
QUICPacket::connection_id() const
{
  return this->_header->connection_id();
}

QUICPacketNumber
QUICPacket::packet_number() const
{
  return this->_header->packet_number();
}

const QUICPacketHeader *
QUICPacket::header() const
{
  return this->_header;
}

const uint8_t *
QUICPacket::payload() const
{
  return this->_payload.get();
}

QUICVersion
QUICPacket::version() const
{
  return this->_header->version();
}

bool
QUICPacket::is_retransmittable() const
{
  return this->_is_retransmittable;
}

uint16_t
QUICPacket::size() const
{
  // This includes not only header size and payload size but also AEAD tag length
  uint16_t size = this->_header->packet_size();
  if (size == 0) {
    size = this->header_size() + this->payload_size();
  }
  return size;
}

uint16_t
QUICPacket::header_size() const
{
  return this->_header->size();
}

uint16_t
QUICPacket::payload_size() const
{
  return this->_payload_size;
}

QUICKeyPhase
QUICPacket::key_phase() const
{
  return this->_header->key_phase();
}

void
QUICPacket::store(uint8_t *buf, size_t *len) const
{
  memcpy(buf, this->_header->buf(), this->_header->size());
  memcpy(buf + this->_header->size(), this->payload(), this->payload_size());
  *len = this->_header->size() + this->payload_size();
}

uint8_t
QUICPacket::calc_packet_number_len(QUICPacketNumber num, QUICPacketNumber base)
{
  ink_assert(num > base);

  uint64_t d  = (num - base) * 2;
  uint8_t len = 0;

  if (d > 0xFFFF) {
    len = 4;
  } else if (d > 0xFF) {
    len = 2;
  } else {
    len = 1;
  }

  return len;
}

bool
QUICPacket::encode_packet_number(QUICPacketNumber &dst, QUICPacketNumber src, size_t len)
{
  ink_assert(len == 1 || len == 2 || len == 4);

  uint64_t mask = (1ULL << (len * 8)) - 1;
  dst           = src & mask;
  return true;
}

bool
QUICPacket::decode_packet_number(QUICPacketNumber &dst, QUICPacketNumber src, size_t len, QUICPacketNumber largest_acked)
{
  ink_assert(len == 1 || len == 2 || len == 4);

  uint64_t maximum_diff       = 1ULL << (len * 8);
  QUICPacketNumber base       = largest_acked & (~(maximum_diff - 1));
  QUICPacketNumber candidate1 = base + src;
  QUICPacketNumber candidate2 = base + src + maximum_diff;

  if (((candidate1 > largest_acked) ? (candidate1 - largest_acked) : (largest_acked - candidate1)) <
      ((candidate2 > largest_acked) ? (candidate2 - largest_acked) : (largest_acked - candidate2))) {
    dst = candidate1;
  } else {
    dst = candidate2;
  }

  return true;
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
QUICPacketFactory::create(ats_unique_buf buf, size_t len, QUICPacketNumber base_packet_number, QUICPacketCreationResult &result)
{
  size_t max_plain_txt_len = 2048;
  ats_unique_buf plain_txt = ats_unique_malloc(max_plain_txt_len);
  size_t plain_txt_len     = 0;

  QUICPacketHeader *header = QUICPacketHeader::load(buf.release(), len, base_packet_number);

  switch (header->type()) {
  case QUICPacketType::VERSION_NEGOTIATION:
  case QUICPacketType::STATELESS_RESET:
    // These packets are unprotected. Just copy the payload
    memcpy(plain_txt.get(), header->payload(), header->payload_size());
    plain_txt_len = header->payload_size();
    result        = QUICPacketCreationResult::SUCCESS;
    break;
  case QUICPacketType::PROTECTED:
    if (this->_crypto->is_handshake_finished()) {
      if (this->_crypto->decrypt(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(), header->payload_size(),
                                 header->packet_number(), header->buf(), header->size(), header->key_phase())) {
        result = QUICPacketCreationResult::SUCCESS;
      } else {
        result = QUICPacketCreationResult::FAILED;
      }
    } else {
      result = QUICPacketCreationResult::NOT_READY;
    }
    break;
  case QUICPacketType::INITIAL:
    if (!this->_crypto->is_handshake_finished()) {
      if (QUICTypeUtil::is_supported_version(header->version())) {
        if (this->_crypto->decrypt(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(), header->payload_size(),
                                   header->packet_number(), header->buf(), header->size(), QUICKeyPhase::CLEARTEXT)) {
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
    if (!this->_crypto->is_handshake_finished()) {
      if (this->_crypto->decrypt(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(), header->payload_size(),
                                 header->packet_number(), header->buf(), header->size(), QUICKeyPhase::CLEARTEXT)) {
        result = QUICPacketCreationResult::SUCCESS;
      } else {
        result = QUICPacketCreationResult::FAILED;
      }
    } else {
      result = QUICPacketCreationResult::IGNORED;
    }
    break;
  default:
    result = QUICPacketCreationResult::FAILED;
    break;
  }

  QUICPacket *packet = nullptr;
  if (result == QUICPacketCreationResult::SUCCESS) {
    packet = quicPacketAllocator.alloc();
    new (packet) QUICPacket(header, std::move(plain_txt), plain_txt_len);
  }

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_packet);
}

QUICPacketUPtr
QUICPacketFactory::create_version_negotiation_packet(const QUICPacket *packet_sent_by_client, QUICPacketNumber base_packet_number)
{
  size_t len = sizeof(QUICVersion) * countof(QUIC_SUPPORTED_VERSIONS);
  ats_unique_buf versions(reinterpret_cast<uint8_t *>(ats_malloc(len)), [](void *p) { ats_free(p); });
  uint8_t *p = versions.get();

  size_t n;
  for (auto v : QUIC_SUPPORTED_VERSIONS) {
    QUICTypeUtil::write_QUICVersion(v, p, &n);
    p += n;
  }

  QUICPacketHeader *header =
    QUICPacketHeader::build(QUICPacketType::VERSION_NEGOTIATION, packet_sent_by_client->connection_id(),
                            packet_sent_by_client->packet_number(), base_packet_number, 0x00, std::move(versions), len);

  return QUICPacketFactory::_create_unprotected_packet(header);
}

QUICPacketUPtr
QUICPacketFactory::create_initial_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number, QUICVersion version,
                                         ats_unique_buf payload, size_t len)
{
  QUICPacketHeader *header = QUICPacketHeader::build(QUICPacketType::INITIAL, connection_id, this->_packet_number_generator.next(),
                                                     base_packet_number, version, std::move(payload), len);
  return this->_create_encrypted_packet(header, true);
}

QUICPacketUPtr
QUICPacketFactory::create_handshake_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number,
                                           ats_unique_buf payload, size_t len, bool retransmittable)
{
  QUICPacketHeader *header =
    QUICPacketHeader::build(QUICPacketType::HANDSHAKE, connection_id, this->_packet_number_generator.next(), base_packet_number,
                            this->_version, std::move(payload), len);
  return this->_create_encrypted_packet(header, retransmittable);
}

QUICPacketUPtr
QUICPacketFactory::create_server_protected_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number,
                                                  ats_unique_buf payload, size_t len, bool retransmittable)
{
  // TODO Key phase should be picked up from QUICCrypto, probably
  QUICPacketHeader *header =
    QUICPacketHeader::build(QUICPacketType::PROTECTED, QUICKeyPhase::PHASE_0, connection_id, this->_packet_number_generator.next(),
                            base_packet_number, std::move(payload), len);
  return this->_create_encrypted_packet(header, retransmittable);
}

QUICPacketUPtr
QUICPacketFactory::create_stateless_reset_packet(QUICConnectionId connection_id, QUICStatelessResetToken stateless_reset_token)
{
  std::random_device rnd;

  uint8_t random_packet_number = static_cast<uint8_t>(rnd() & 0xFF);
  size_t payload_len           = static_cast<uint8_t>(rnd() & 0xFF);
  ats_unique_buf payload       = ats_unique_malloc(payload_len + 16);
  uint8_t *naked_payload       = payload.get();

  // Generate random octets
  for (int i = payload_len - 1; i >= 0; --i) {
    naked_payload[i] = static_cast<uint8_t>(rnd() & 0xFF);
  }
  // Copy stateless reset token into payload
  memcpy(naked_payload + payload_len - 16, stateless_reset_token.buf(), 16);

  // KeyPhase won't be used
  QUICPacketHeader *header = QUICPacketHeader::build(QUICPacketType::STATELESS_RESET, QUICKeyPhase::CLEARTEXT, connection_id,
                                                     random_packet_number, 0, std::move(payload), payload_len);
  return QUICPacketFactory::_create_unprotected_packet(header);
}

QUICPacketUPtr
QUICPacketFactory::_create_unprotected_packet(QUICPacketHeader *header)
{
  ats_unique_buf cleartext = ats_unique_malloc(2048);
  size_t cleartext_len     = header->payload_size();

  memcpy(cleartext.get(), header->payload(), cleartext_len);
  QUICPacket *packet = quicPacketAllocator.alloc();
  new (packet) QUICPacket(header, std::move(cleartext), cleartext_len, false);

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_packet);
}

QUICPacketUPtr
QUICPacketFactory::_create_encrypted_packet(QUICPacketHeader *header, bool retransmittable)
{
  // TODO: use pmtu of UnixNetVConnection
  size_t max_cipher_txt_len = 2048;
  ats_unique_buf cipher_txt = ats_unique_malloc(max_cipher_txt_len);
  size_t cipher_txt_len     = 0;

  QUICPacket *packet = nullptr;
  if (this->_crypto->encrypt(cipher_txt.get(), cipher_txt_len, max_cipher_txt_len, header->payload(), header->payload_size(),
                             header->packet_number(), header->buf(), header->size(), header->key_phase())) {
    packet = quicPacketAllocator.alloc();
    new (packet) QUICPacket(header, std::move(cipher_txt), cipher_txt_len, retransmittable);
  }

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_packet);
}

void
QUICPacketFactory::set_version(QUICVersion negotiated_version)
{
  this->_version = negotiated_version;
}

void
QUICPacketFactory::set_crypto_module(QUICCrypto *crypto)
{
  this->_crypto = crypto;
}

//
// QUICPacketNumberGenerator
//
QUICPacketNumberGenerator::QUICPacketNumberGenerator()
{
  this->randomize();
}

QUICPacketNumber
QUICPacketNumberGenerator::randomize()
{
  std::random_device rnd;
  this->_current = rnd() & 0x7FFFFFFF;

  return this->_current;
}

QUICPacketNumber
QUICPacketNumberGenerator::next()
{
  return this->_current++;
}
