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
#include "QUICPacket.h"

ClassAllocator<QUICPacket> quicPacketAllocator("quicPacketAllocator");
ClassAllocator<QUICPacketLongHeader> quicPacketLongHeaderAllocator("quicPacketLongHeaderAllocator");
ClassAllocator<QUICPacketShortHeader> quicPacketShortHeaderAllocator("quicPacketShortHeaderAllocator");

static constexpr int OFFSET_CONNECTION_ID = 1;
static constexpr int OFFSET_PACKET_NUMBER = 9;
static constexpr int OFFSET_VERSION       = 13;
static constexpr int OFFSET_PAYLOAD       = 17;
static constexpr int LONGHEADER_LENGTH    = 17;
static constexpr int FNV1A_HASH_LEN       = 8;

//
// QUICPacketHeader
//
const uint8_t *
QUICPacketHeader::buf()
{
  return this->_buf;
}

QUICPacketHeader *
QUICPacketHeader::load(const uint8_t *buf, size_t len, QUICPacketNumber base)
{
  if (QUICTypeUtil::hasLongHeader(buf)) {
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
QUICPacketHeader::build(QUICPacketType type, QUICPacketNumber packet_number, QUICPacketNumber base_packet_number,
                        ats_unique_buf payload, size_t len)
{
  QUICPacketShortHeader *short_header = quicPacketShortHeaderAllocator.alloc();
  new (short_header) QUICPacketShortHeader(type, packet_number, base_packet_number, std::move(payload), len);
  return short_header;
}

QUICPacketHeader *
QUICPacketHeader::build(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                        QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len)
{
  QUICPacketShortHeader *short_header = quicPacketShortHeaderAllocator.alloc();
  new (short_header) QUICPacketShortHeader(type, connection_id, packet_number, base_packet_number, std::move(payload), len);
  return short_header;
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
    int type = this->_buf[0] & 0x7F;
    if (type < static_cast<int>(QUICPacketType::UNINITIALIZED)) {
      return static_cast<QUICPacketType>(type);
    } else {
      return QUICPacketType::UNINITIALIZED;
    }
  } else {
    return this->_type;
  }
}

QUICConnectionId
QUICPacketLongHeader::connection_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICConnectionId(this->_buf + OFFSET_CONNECTION_ID, 8);
  } else {
    return this->_connection_id;
  }
}

QUICPacketNumber
QUICPacketLongHeader::packet_number() const
{
  if (this->_buf) {
    const uint8_t packet_number_len = 4;
    QUICPacketNumber src            = QUICTypeUtil::read_QUICPacketNumber(this->_buf + OFFSET_PACKET_NUMBER, packet_number_len);
    QUICPacketNumber dst            = 0;
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
    return QUICTypeUtil::read_QUICVersion(this->_buf + OFFSET_VERSION);
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
    return this->_buf + OFFSET_PAYLOAD;
  } else {
    return this->_payload.get();
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
  return QUICKeyPhase::PHASE_0;
}

uint16_t
QUICPacketLongHeader::length() const
{
  return LONGHEADER_LENGTH;
}

void
QUICPacketLongHeader::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  *len   = 0;
  buf[0] = 0x80;
  buf[0] += static_cast<uint8_t>(this->_type);
  *len += 1;
  QUICTypeUtil::write_QUICConnectionId(this->_connection_id, 8, buf + *len, &n);
  *len += n;

  QUICPacketNumber dst = 0;
  size_t dst_len       = 4;
  QUICPacket::encode_packet_number(dst, this->_packet_number, dst_len);
  QUICTypeUtil::write_QUICPacketNumber(dst, dst_len, buf + *len, &n);
  *len += n;

  QUICTypeUtil::write_QUICVersion(this->_version, buf + *len, &n);
  *len += n;
}

//
// QUICPacketShortHeader
//
QUICPacketShortHeader::QUICPacketShortHeader(QUICPacketType type, QUICPacketNumber packet_number,
                                             QUICPacketNumber base_packet_number, ats_unique_buf buf, size_t len)
{
  this->_type               = type;
  this->_has_key_phase      = true;
  this->_packet_number      = packet_number;
  this->_base_packet_number = base_packet_number;
  this->_packet_number_type = this->_discover_packet_number_type(packet_number, base_packet_number);
  this->_payload            = std::move(buf);
  this->_payload_len        = len;

  if (type == QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0) {
    this->_key_phase = QUICKeyPhase::PHASE_0;
  } else if (type == QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_1) {
    this->_key_phase = QUICKeyPhase::PHASE_1;
  } else {
    ink_assert(false);
    this->_key_phase = QUICKeyPhase::PHASE_UNINITIALIZED;
  }
}

QUICPacketShortHeader::QUICPacketShortHeader(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                                             QUICPacketNumber base_packet_number, ats_unique_buf buf, size_t len)
{
  this->_type               = type;
  this->_has_key_phase      = true;
  this->_has_connection_id  = true;
  this->_connection_id      = connection_id;
  this->_packet_number      = packet_number;
  this->_base_packet_number = base_packet_number;
  this->_packet_number_type = this->_discover_packet_number_type(packet_number, base_packet_number);
  this->_payload            = std::move(buf);
  this->_payload_len        = len;

  if (type == QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0) {
    this->_key_phase = QUICKeyPhase::PHASE_0;
  } else if (type == QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_1) {
    this->_key_phase = QUICKeyPhase::PHASE_1;
  } else {
    ink_assert(false);
    this->_key_phase = QUICKeyPhase::PHASE_UNINITIALIZED;
  }
}

QUICPacketType
QUICPacketShortHeader::type() const
{
  QUICKeyPhase key_phase = this->key_phase();

  switch (key_phase) {
  case QUICKeyPhase::PHASE_0: {
    return QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0;
  }
  case QUICKeyPhase::PHASE_1: {
    return QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_1;
  }
  default:
    ink_assert(false);
    return QUICPacketType::UNINITIALIZED;
  }
}

QUICConnectionId
QUICPacketShortHeader::connection_id() const
{
  if (this->_buf) {
    ink_release_assert(this->has_connection_id());
    return QUICTypeUtil::read_QUICConnectionId(this->_buf + OFFSET_CONNECTION_ID, 8);
  } else {
    return _connection_id;
  }
}

QUICPacketNumber
QUICPacketShortHeader::packet_number() const
{
  if (this->_buf) {
    int n      = this->_packet_number_len();
    int offset = 1;
    if (this->has_connection_id()) {
      offset = OFFSET_PACKET_NUMBER;
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
    return (this->_buf[0] & 0x40) != 0;
  } else {
    return this->_has_connection_id;
  }
}

const uint8_t *
QUICPacketShortHeader::payload() const
{
  if (this->_buf) {
    return this->_buf + length();
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
QUICPacketShortHeader::length() const
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
  if (this->_has_connection_id) {
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
QUICPacket::QUICPacket(IOBufferBlock *block, QUICPacketNumber base_packet_number) : _block(block)
{
  this->_size = block->size();
  this->_header =
    QUICPacketHeader::load(reinterpret_cast<const uint8_t *>(this->_block->buf()), this->_block->size(), base_packet_number);
}

QUICPacket::QUICPacket(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                       QUICPacketNumber base_packet_number, QUICVersion version, ats_unique_buf payload, size_t len,
                       bool retransmittable)
{
  this->_header = QUICPacketHeader::build(type, connection_id, packet_number, base_packet_number, version, std::move(payload), len);
  this->_size   = this->_header->length() + len;
  if (type != QUICPacketType::ZERO_RTT_PROTECTED && type != QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0 &&
      type != QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_1) {
    this->_size += FNV1A_HASH_LEN;
  }
  this->_is_retransmittable = retransmittable;
}

QUICPacket::QUICPacket(QUICPacketType type, QUICPacketNumber packet_number, QUICPacketNumber base_packet_number,
                       ats_unique_buf payload, size_t len, bool retransmittable)
{
  this->_header = QUICPacketHeader::build(type, packet_number, base_packet_number, std::move(payload), len);
  this->_size   = this->_header->length() + len;
  if (type != QUICPacketType::ZERO_RTT_PROTECTED && type != QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0 &&
      type != QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_1) {
    this->_size += FNV1A_HASH_LEN;
  }
  this->_is_retransmittable = retransmittable;
}

QUICPacket::QUICPacket(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                       QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len, bool retransmittable)
{
  this->_header = QUICPacketHeader::build(type, connection_id, packet_number, base_packet_number, std::move(payload), len);
  this->_size   = this->_header->length() + len;
  if (type != QUICPacketType::ZERO_RTT_PROTECTED && type != QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0 &&
      type != QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_1) {
    this->_size += FNV1A_HASH_LEN;
  }
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

const uint8_t *
QUICPacket::header() const
{
  return this->_header->buf();
}

const uint8_t *
QUICPacket::payload() const
{
  return this->_header->payload();
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
  return this->_size;
}

uint16_t
QUICPacket::header_size() const
{
  return this->_header->length();
}

uint16_t
QUICPacket::payload_size() const
{
  // FIXME Protected packets may / may not contain something at the end
  if (this->type() != QUICPacketType::ZERO_RTT_PROTECTED && this->type() != QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0 &&
      this->type() != QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_1) {
    return this->_size - this->_header->length() - FNV1A_HASH_LEN;
  } else {
    return this->_size - this->_header->length();
  }
}

QUICKeyPhase
QUICPacket::key_phase() const
{
  return this->_header->key_phase();
}

void
QUICPacket::store(uint8_t *buf, size_t *len) const
{
  this->_header->store(buf, len);
  ink_assert(this->size() >= *len);

  if (this->type() != QUICPacketType::ZERO_RTT_PROTECTED && this->type() != QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0 &&
      this->type() != QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_1) {
    memcpy(buf + *len, this->payload(), this->payload_size());
    *len += this->payload_size();

    fnv1a(buf, *len, buf + *len);
    *len += FNV1A_HASH_LEN;
  } else {
    ink_assert(this->_protected_payload);
    memcpy(buf + *len, this->_protected_payload.get(), this->_protected_payload_size);
    *len += this->_protected_payload_size;
  }
}

void
QUICPacket::store_header(uint8_t *buf, size_t *len) const
{
  this->_header->store(buf, len);
}

bool
QUICPacket::has_valid_fnv1a_hash() const
{
  uint8_t hash[FNV1A_HASH_LEN];
  fnv1a(reinterpret_cast<const uint8_t *>(this->_block->buf()), this->_block->size() - FNV1A_HASH_LEN, hash);
  return memcmp(this->_block->buf() + this->_block->size() - FNV1A_HASH_LEN, hash, 8) == 0;
}

void
QUICPacket::set_protected_payload(ats_unique_buf cipher_txt, size_t cipher_txt_len)
{
  this->_protected_payload      = std::move(cipher_txt);
  this->_protected_payload_size = cipher_txt_len;
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
QUICPacket::decode_packet_number(QUICPacketNumber &dst, QUICPacketNumber src, size_t len, QUICPacketNumber base)
{
  ink_assert(len == 1 || len == 2 || len == 4);

  QUICPacketNumber expected = base + 1;

  uint64_t p              = 1ULL << (len * 8);
  QUICPacketNumber masked = base & (~(p - 1));
  dst                     = masked + src;

  if (dst >= expected) {
    return true;
  }

  dst += p;
  return true;
}

//
// QUICPacketFactory
//
std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>
QUICPacketFactory::create(IOBufferBlock *block, QUICPacketNumber base_packet_number)
{
  QUICPacket *packet = quicPacketAllocator.alloc();
  new (packet) QUICPacket(block, base_packet_number);
  return std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>(packet, &QUICPacketDeleter::delete_packet);
}

std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>
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

  QUICPacket *packet = quicPacketAllocator.alloc();
  new (packet)
    QUICPacket(QUICPacketType::VERSION_NEGOTIATION, packet_sent_by_client->connection_id(), packet_sent_by_client->packet_number(),
               base_packet_number, packet_sent_by_client->version(), std::move(versions), len, false);
  return std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>(packet, QUICPacketDeleter::delete_packet);
}

std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>
QUICPacketFactory::create_server_cleartext_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number,
                                                  ats_unique_buf payload, size_t len, bool retransmittable)
{
  QUICPacket *p = quicPacketAllocator.alloc();
  new (p) QUICPacket(QUICPacketType::SERVER_CLEARTEXT, connection_id, this->_packet_number_generator.next(), base_packet_number,
                     this->_version, std::move(payload), len, retransmittable);
  return std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>(p, &QUICPacketDeleter::delete_packet);
}

std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>
QUICPacketFactory::create_server_protected_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number,
                                                  ats_unique_buf payload, size_t len, bool retransmittable)
{
  // TODO Key phase should be picked up from QUICCrypto, probably
  QUICPacket *p = quicPacketAllocator.alloc();
  new (p) QUICPacket(QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0, connection_id, this->_packet_number_generator.next(),
                     base_packet_number, std::move(payload), len, retransmittable);
  auto packet = std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>(p, &QUICPacketDeleter::delete_packet);

  // TODO: use pmtu of UnixNetVConnection
  size_t max_cipher_txt_len = 2048;
  ats_unique_buf cipher_txt = ats_unique_malloc(max_cipher_txt_len);
  size_t cipher_txt_len     = 0;

  // TODO: do not dump header twice
  uint8_t ad[17] = {0};
  size_t ad_len  = 0;
  packet->store_header(ad, &ad_len);

  if (this->_crypto->encrypt(cipher_txt.get(), cipher_txt_len, max_cipher_txt_len, packet->payload(), packet->payload_size(),
                             packet->packet_number(), ad, ad_len, packet->key_phase())) {
    packet->set_protected_payload(std::move(cipher_txt), cipher_txt_len);
    Debug("quic_packet_factory", "Encrypt Packet, pkt_num: %" PRIu64 ", header_len: %zu payload: %zu", packet->packet_number(),
          ad_len, cipher_txt_len);
    return packet;
  } else {
    Debug("quic_packet_factory", "CRYPTOGRAPHIC Error");
    return std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>(nullptr, &QUICPacketDeleter::delete_null_packet);
  }
}

std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>
QUICPacketFactory::create_client_initial_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number,
                                                QUICVersion version, ats_unique_buf payload, size_t len)
{
  QUICPacket *packet = quicPacketAllocator.alloc();
  new (packet) QUICPacket(QUICPacketType::CLIENT_INITIAL, connection_id, this->_packet_number_generator.next(), base_packet_number,
                          version, std::move(payload), len, true);
  return std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>(packet, &QUICPacketDeleter::delete_packet);
}

void
QUICPacketFactory::set_version(QUICVersion negotiated_version)
{
  ink_assert(this->_version == 0);
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
