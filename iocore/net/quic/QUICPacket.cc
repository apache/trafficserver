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

#include "QUICPacket.h"

#include <ts/ink_assert.h>
#include <ts/Diags.h>

#include "QUICIntUtil.h"
#include "QUICDebugNames.h"
#include "QUICConfig.h"

static constexpr std::string_view tag  = "quic_packet"sv;
static constexpr uint64_t aead_tag_len = 16;

#define QUICDebug(dcid, scid, fmt, ...) \
  Debug(tag.data(), "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__);

ClassAllocator<QUICPacket> quicPacketAllocator("quicPacketAllocator");
ClassAllocator<QUICPacketLongHeader> quicPacketLongHeaderAllocator("quicPacketLongHeaderAllocator");
ClassAllocator<QUICPacketShortHeader> quicPacketShortHeaderAllocator("quicPacketShortHeaderAllocator");

static constexpr int LONG_HDR_OFFSET_CONNECTION_ID = 6;
static constexpr int LONG_HDR_OFFSET_VERSION       = 1;

//
// QUICPacketHeader
//
const uint8_t *
QUICPacketHeader::buf()
{
  if (this->_buf) {
    return this->_buf.get();
  } else {
    // TODO Reuse serialzied data if nothing has changed
    this->store(this->_serialized, &this->_buf_len);
    this->_buf_len += this->_payload_length;
    return this->_serialized;
  }
}

const IpEndpoint &
QUICPacketHeader::from() const
{
  return this->_from;
}

uint16_t
QUICPacketHeader::packet_size() const
{
  return this->_buf_len;
}

QUICPacketHeaderUPtr
QUICPacketHeader::load(const IpEndpoint from, ats_unique_buf buf, size_t len, QUICPacketNumber base)
{
  QUICPacketHeaderUPtr header = QUICPacketHeaderUPtr(nullptr, &QUICPacketHeaderDeleter::delete_null_header);
  if (QUICInvariants::is_long_header(buf.get())) {
    QUICPacketLongHeader *long_header = quicPacketLongHeaderAllocator.alloc();
    new (long_header) QUICPacketLongHeader(from, std::move(buf), len, base);
    header = QUICPacketHeaderUPtr(long_header, &QUICPacketHeaderDeleter::delete_long_header);
  } else {
    QUICPacketShortHeader *short_header = quicPacketShortHeaderAllocator.alloc();
    new (short_header) QUICPacketShortHeader(from, std::move(buf), len, base);
    header = QUICPacketHeaderUPtr(short_header, &QUICPacketHeaderDeleter::delete_short_header);
  }
  return header;
}

QUICPacketHeaderUPtr
QUICPacketHeader::build(QUICPacketType type, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                        QUICPacketNumber packet_number, QUICPacketNumber base_packet_number, QUICVersion version,
                        ats_unique_buf payload, size_t len)
{
  QUICPacketLongHeader *long_header = quicPacketLongHeaderAllocator.alloc();
  new (long_header)
    QUICPacketLongHeader(type, destination_cid, source_cid, packet_number, base_packet_number, version, std::move(payload), len);
  return QUICPacketHeaderUPtr(long_header, &QUICPacketHeaderDeleter::delete_long_header);
}

QUICPacketHeaderUPtr
QUICPacketHeader::build(QUICPacketType type, QUICKeyPhase key_phase, QUICPacketNumber packet_number,
                        QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len)
{
  QUICPacketShortHeader *short_header = quicPacketShortHeaderAllocator.alloc();
  new (short_header) QUICPacketShortHeader(type, key_phase, packet_number, base_packet_number, std::move(payload), len);
  return QUICPacketHeaderUPtr(short_header, &QUICPacketHeaderDeleter::delete_short_header);
}

QUICPacketHeaderUPtr
QUICPacketHeader::build(QUICPacketType type, QUICKeyPhase key_phase, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                        QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len)
{
  QUICPacketShortHeader *short_header = quicPacketShortHeaderAllocator.alloc();
  new (short_header)
    QUICPacketShortHeader(type, key_phase, connection_id, packet_number, base_packet_number, std::move(payload), len);
  return QUICPacketHeaderUPtr(short_header, &QUICPacketHeaderDeleter::delete_short_header);
}

QUICPacketHeaderUPtr
QUICPacketHeader::clone() const
{
  return QUICPacketHeaderUPtr(nullptr, &QUICPacketHeaderDeleter::delete_null_header);
}

//
// QUICPacketLongHeader
//

QUICPacketLongHeader::QUICPacketLongHeader(const IpEndpoint from, ats_unique_buf buf, size_t len, QUICPacketNumber base)
  : QUICPacketHeader(from, std::move(buf), len, base)
{
  uint8_t *raw_buf = this->_buf.get();

  uint8_t dcil = 0;
  uint8_t scil = 0;
  QUICPacketLongHeader::dcil(dcil, raw_buf, len);
  QUICPacketLongHeader::scil(scil, raw_buf, len);

  size_t offset          = LONG_HDR_OFFSET_CONNECTION_ID;
  this->_destination_cid = {raw_buf + offset, dcil};
  offset += dcil;
  this->_source_cid = {raw_buf + offset, scil};
  offset += scil;

  if (this->type() == QUICPacketType::VERSION_NEGOTIATION) {
    this->_payload_offset = offset;
  } else {
    offset += QUICVariableInt::size(raw_buf + offset);
    int pn_len = QUICTypeUtil::read_QUICPacketNumberLen(raw_buf + offset);
    QUICPacket::decode_packet_number(this->_packet_number, QUICTypeUtil::read_QUICPacketNumber(raw_buf + offset), pn_len,
                                     this->_base_packet_number);
    this->_payload_offset = offset + pn_len;
  }
  this->_payload_length = len - this->_payload_offset;
}

QUICPacketLongHeader::QUICPacketLongHeader(QUICPacketType type, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                           QUICPacketNumber packet_number, QUICPacketNumber base_packet_number, QUICVersion version,
                                           ats_unique_buf buf, size_t len)
{
  this->_type               = type;
  this->_destination_cid    = destination_cid;
  this->_source_cid         = source_cid;
  this->_packet_number      = packet_number;
  this->_base_packet_number = base_packet_number;
  this->_has_version        = true;
  this->_version            = version;
  this->_payload            = std::move(buf);
  this->_payload_length     = len;

  if (this->_type == QUICPacketType::VERSION_NEGOTIATION) {
    this->_buf_len =
      LONG_HDR_OFFSET_CONNECTION_ID + this->_destination_cid.length() + this->_source_cid.length() + this->_payload_length;
  } else {
    this->buf();
  }
}

QUICPacketType
QUICPacketLongHeader::type() const
{
  if (this->_buf) {
    QUICPacketType type;
    QUICPacketLongHeader::type(type, this->_buf.get(), this->_buf_len);
    return type;
  } else {
    return this->_type;
  }
}

bool
QUICPacketLongHeader::type(QUICPacketType &type, const uint8_t *packet, size_t packet_len)
{
  if (packet_len < 1) {
    return false;
  }

  uint8_t raw_type = packet[0] & 0x7F;
  QUICVersion version;
  if (QUICPacketLongHeader::version(version, packet, packet_len) && version == 0x00) {
    type = QUICPacketType::VERSION_NEGOTIATION;
  } else {
    type = static_cast<QUICPacketType>(raw_type);
  }
  return true;
}

bool
QUICPacketLongHeader::version(QUICVersion &version, const uint8_t *packet, size_t packet_len)
{
  if (packet_len < 5) {
    return false;
  }

  version = QUICTypeUtil::read_QUICVersion(packet + LONG_HDR_OFFSET_VERSION);
  return true;
}

bool
QUICPacketLongHeader::dcil(uint8_t &dcil, const uint8_t *packet, size_t packet_len)
{
  if (QUICInvariants::dcil(dcil, packet, packet_len)) {
    dcil += 3;
    return true;
  } else {
    return false;
  }
}

bool
QUICPacketLongHeader::scil(uint8_t &scil, const uint8_t *packet, size_t packet_len)
{
  if (QUICInvariants::scil(scil, packet, packet_len)) {
    scil += 3;
    return true;
  } else {
    return false;
  }
}

bool
QUICPacketLongHeader::length(size_t &length, uint8_t *field_len, const uint8_t *packet, size_t packet_len)
{
  uint8_t dcil, scil;

  QUICPacketLongHeader::dcil(dcil, packet, packet_len);
  QUICPacketLongHeader::scil(scil, packet, packet_len);

  size_t length_offset = LONG_HDR_OFFSET_CONNECTION_ID + dcil + scil;
  length               = QUICIntUtil::read_QUICVariableInt(packet + length_offset);
  if (field_len) {
    *field_len = QUICVariableInt::size(packet + length_offset);
  }
  return true;
}

bool
QUICPacketLongHeader::packet_number_offset(size_t &pn_offset, const uint8_t *packet, size_t packet_len)
{
  uint8_t dcil, scil;
  size_t length;
  uint8_t length_field_len;
  if (!QUICPacketLongHeader::dcil(dcil, packet, packet_len) || !QUICPacketLongHeader::scil(scil, packet, packet_len) ||
      !QUICPacketLongHeader::length(length, &length_field_len, packet, packet_len)) {
    return false;
  }
  pn_offset = 6 + dcil + scil + length_field_len;
  return true;
}

QUICConnectionId
QUICPacketLongHeader::destination_cid() const
{
  return this->_destination_cid;
}

QUICConnectionId
QUICPacketLongHeader::source_cid() const
{
  return this->_source_cid;
}

QUICPacketNumber
QUICPacketLongHeader::packet_number() const
{
  return this->_packet_number;
}

bool
QUICPacketLongHeader::has_version() const
{
  return true;
}

bool
QUICPacketLongHeader::is_valid() const
{
  if (this->_buf && this->_buf_len != this->_payload_offset + this->_payload_length) {
    QUICDebug(this->_source_cid, this->_destination_cid,
              "Invalid packet: packet_size(%zu) should be header_size(%zu) + payload_size(%zu)", this->_buf_len,
              this->_payload_offset, this->_payload_length);
    Warning("Invalid packet: packet_size(%zu) should be header_size(%zu) + payload_size(%zu)", this->_buf_len,
            this->_payload_offset, this->_payload_length);

    return false;
  }

  return true;
}

QUICVersion
QUICPacketLongHeader::version() const
{
  if (this->_buf) {
    QUICVersion version;
    QUICPacketLongHeader::version(version, this->_buf.get(), this->_buf_len);
    return version;
  } else {
    return this->_version;
  }
}

const uint8_t *
QUICPacketLongHeader::payload() const
{
  if (this->_buf) {
    uint8_t *raw = this->_buf.get();
    return raw + this->_payload_offset;
  } else {
    return this->_payload.get();
  }
}

uint16_t
QUICPacketHeader::payload_size() const
{
  return this->_payload_length;
}

bool
QUICPacketLongHeader::has_key_phase() const
{
  return false;
}

QUICKeyPhase
QUICPacketLongHeader::key_phase() const
{
  switch (this->type()) {
  case QUICPacketType::ZERO_RTT_PROTECTED:
    return QUICKeyPhase::ZERORTT;
  default:
    return QUICKeyPhase::CLEARTEXT;
  }
}

uint16_t
QUICPacketLongHeader::size() const
{
  return this->_buf_len - this->_payload_length;
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

  QUICTypeUtil::write_QUICVersion(this->_version, buf + *len, &n);
  *len += n;

  buf[*len] = (this->_destination_cid.length() == 0 ? 0 : this->_destination_cid.length() - 3) << 4;
  buf[*len] += this->_source_cid.length() == 0 ? 0 : this->_source_cid.length() - 3;
  *len += 1;

  QUICTypeUtil::write_QUICConnectionId(this->_destination_cid, buf + *len, &n);
  *len += n;
  QUICTypeUtil::write_QUICConnectionId(this->_source_cid, buf + *len, &n);
  *len += n;

  if (this->_type != QUICPacketType::VERSION_NEGOTIATION) {
    QUICPacketNumber pn = 0;
    size_t pn_len       = 4;
    QUICPacket::encode_packet_number(pn, this->_packet_number, pn_len);

    if (pn > 0x3FFF) {
      pn_len = 4;
    } else if (pn > 0x7F) {
      pn_len = 2;
    } else {
      pn_len = 1;
    }

    QUICIntUtil::write_QUICVariableInt(pn_len + this->_payload_length + aead_tag_len, buf + *len, &n);
    *len += n;

    QUICTypeUtil::write_QUICPacketNumber(pn, pn_len, buf + *len, &n);
    *len += n;
  }
}

//
// QUICPacketShortHeader
//

QUICPacketShortHeader::QUICPacketShortHeader(const IpEndpoint from, ats_unique_buf buf, size_t len, QUICPacketNumber base)
  : QUICPacketHeader(from, std::move(buf), len, base)
{
  QUICInvariants::dcid(this->_connection_id, this->_buf.get(), len);

  int offset               = 1 + this->_connection_id.length();
  this->_packet_number_len = QUICTypeUtil::read_QUICPacketNumberLen(this->_buf.get() + offset);
  QUICPacketNumber src     = QUICTypeUtil::read_QUICPacketNumber(this->_buf.get() + offset);
  QUICPacket::decode_packet_number(this->_packet_number, src, this->_packet_number_len, this->_base_packet_number);
  this->_payload_length = len - (1 + QUICConfigParams::scid_len() + this->_packet_number_len);
}

QUICPacketShortHeader::QUICPacketShortHeader(QUICPacketType type, QUICKeyPhase key_phase, QUICPacketNumber packet_number,
                                             QUICPacketNumber base_packet_number, ats_unique_buf buf, size_t len)
{
  this->_type               = type;
  this->_has_key_phase      = true;
  this->_key_phase          = key_phase;
  this->_packet_number      = packet_number;
  this->_base_packet_number = base_packet_number;
  this->_packet_number_len  = QUICPacket::calc_packet_number_len(packet_number, base_packet_number);
  this->_payload            = std::move(buf);
  this->_payload_length     = len;
}

QUICPacketShortHeader::QUICPacketShortHeader(QUICPacketType type, QUICKeyPhase key_phase, QUICConnectionId connection_id,
                                             QUICPacketNumber packet_number, QUICPacketNumber base_packet_number,
                                             ats_unique_buf buf, size_t len)
{
  this->_type               = type;
  this->_has_key_phase      = true;
  this->_key_phase          = key_phase;
  this->_connection_id      = connection_id;
  this->_packet_number      = packet_number;
  this->_base_packet_number = base_packet_number;
  this->_packet_number_len  = QUICPacket::calc_packet_number_len(packet_number, base_packet_number);
  this->_payload            = std::move(buf);
  this->_payload_length     = len;
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
QUICPacketShortHeader::destination_cid() const
{
  if (this->_buf) {
    QUICConnectionId dcid = QUICConnectionId::ZERO();
    QUICInvariants::dcid(dcid, this->_buf.get(), this->_buf_len);
    return dcid;
  } else {
    return _connection_id;
  }
}

QUICPacketNumber
QUICPacketShortHeader::packet_number() const
{
  return this->_packet_number;
}

bool
QUICPacketShortHeader::has_version() const
{
  return false;
}

bool
QUICPacketShortHeader::is_valid() const
{
  return true;
}

QUICVersion
QUICPacketShortHeader::version() const
{
  return 0;
}

const uint8_t *
QUICPacketShortHeader::payload() const
{
  if (this->_buf) {
    return this->_buf.get() + this->size();
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
    QUICKeyPhase phase;
    QUICPacketShortHeader::key_phase(phase, this->_buf.get(), this->_buf_len);
    return phase;
  } else {
    return this->_key_phase;
  }
}

bool
QUICPacketShortHeader::key_phase(QUICKeyPhase &phase, const uint8_t *packet, size_t packet_len)
{
  if (packet_len < 1) {
    return false;
  }
  if (packet[0] & 0x40) {
    phase = QUICKeyPhase::PHASE_1;
  } else {
    phase = QUICKeyPhase::PHASE_0;
  }
  return true;
}

bool
QUICPacketShortHeader::packet_number_offset(size_t &pn_offset, const uint8_t *packet, size_t packet_len, int dcil)
{
  pn_offset = 1 + dcil;
  return true;
}

/**
 * Header Length (doesn't include payload length)
 */
uint16_t
QUICPacketShortHeader::size() const
{
  uint16_t len = 1;
  len += this->_connection_id.length();
  len += this->_packet_number_len;

  return len;
}

void
QUICPacketShortHeader::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  *len   = 0;
  buf[0] = 0x00;
  if (this->_key_phase == QUICKeyPhase::PHASE_1) {
    buf[0] += 0x40;
  }
  buf[0] += 0x30;
  *len += 1;
  QUICTypeUtil::write_QUICConnectionId(this->_connection_id, buf + *len, &n);
  *len += n;

  QUICPacketNumber dst = 0;
  size_t dst_len       = this->_packet_number_len;
  QUICPacket::encode_packet_number(dst, this->_packet_number, dst_len);
  QUICTypeUtil::write_QUICPacketNumber(dst, dst_len, buf + *len, &n);

  *len += n;
}

//
// QUICPacket
//

QUICPacket::QUICPacket() {}

QUICPacket::QUICPacket(QUICPacketHeaderUPtr header, ats_unique_buf payload, size_t payload_len)
{
  this->_header       = std::move(header);
  this->_payload      = std::move(payload);
  this->_payload_size = payload_len;
}

QUICPacket::QUICPacket(QUICPacketHeaderUPtr header, ats_unique_buf payload, size_t payload_len, bool retransmittable)
{
  this->_header             = std::move(header);
  this->_payload            = std::move(payload);
  this->_payload_size       = payload_len;
  this->_is_retransmittable = retransmittable;
}

QUICPacket::~QUICPacket()
{
  this->_header = nullptr;
}

const IpEndpoint &
QUICPacket::from() const
{
  return this->_header->from();
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
QUICPacket::destination_cid() const
{
  return this->_header->destination_cid();
}

QUICConnectionId
QUICPacket::source_cid() const
{
  return this->_header->source_cid();
}

QUICPacketNumber
QUICPacket::packet_number() const
{
  return this->_header->packet_number();
}

const QUICPacketHeader &
QUICPacket::header() const
{
  return *this->_header;
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
    size = this->header_size() + this->payload_length();
  }
  return size;
}

uint16_t
QUICPacket::header_size() const
{
  return this->_header->size();
}

uint16_t
QUICPacket::payload_length() const
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
  memcpy(buf + this->_header->size(), this->payload(), this->payload_length());
  *len = this->_header->size() + this->payload_length();
}

uint8_t
QUICPacket::calc_packet_number_len(QUICPacketNumber num, QUICPacketNumber base)
{
  uint64_t d  = (num - base) * 2;
  uint8_t len = 0;

  if (d > 0x3FFF) {
    len = 4;
  } else if (d > 0x7F) {
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

  uint64_t mask = 0;
  switch (len) {
  case 1:
    mask = 0x7F;
    break;
  case 2:
    mask = 0x3FFF;
    break;
  case 4:
    mask = 0x3FFFFFFF;
    break;
  default:
    ink_assert(!"len must be 1, 2, or 4");
    return false;
  }
  dst = src & mask;
  return true;
}

bool
QUICPacket::decode_packet_number(QUICPacketNumber &dst, QUICPacketNumber src, size_t len, QUICPacketNumber largest_acked)
{
  ink_assert(len == 1 || len == 2 || len == 4);

  uint64_t maximum_diff = 0;
  switch (len) {
  case 1:
    maximum_diff = 0x80;
    break;
  case 2:
    maximum_diff = 0x4000;
    break;
  case 4:
    maximum_diff = 0x40000000;
    break;
  default:
    ink_assert(!"len must be 1, 2 or 4");
  }
  QUICPacketNumber base       = largest_acked & (~(maximum_diff - 1));
  QUICPacketNumber candidate1 = base + src;
  QUICPacketNumber candidate2 = base + src + maximum_diff;
  QUICPacketNumber expected   = largest_acked + 1;

  if (((candidate1 > expected) ? (candidate1 - expected) : (expected - candidate1)) <
      ((candidate2 > expected) ? (candidate2 - expected) : (expected - candidate2))) {
    dst = candidate1;
  } else {
    dst = candidate2;
  }

  return true;
}

bool
QUICPacket::protect_packet_number(uint8_t *packet, size_t packet_len, const QUICPacketNumberProtector *pn_protector, int dcil)
{
  size_t pn_offset             = 0;
  uint8_t pn_len               = 4;
  size_t sample_offset         = 0;
  constexpr int aead_expansion = 16; // Currently, AEAD expansion (which is probably AEAD tag) length is always 16
  QUICKeyPhase phase;

  if (QUICInvariants::is_long_header(packet)) {
    QUICPacketType type;
    QUICPacketLongHeader::type(type, packet, packet_len);
    switch (type) {
    case QUICPacketType::ZERO_RTT_PROTECTED:
      phase = QUICKeyPhase::ZERORTT;
      break;
    default:
      phase = QUICKeyPhase::CLEARTEXT;
      break;
    }
    QUICPacketLongHeader::packet_number_offset(pn_offset, packet, packet_len);
  } else {
    QUICPacketShortHeader::key_phase(phase, packet, packet_len);
    QUICPacketShortHeader::packet_number_offset(pn_offset, packet, packet_len, dcil);
  }
  sample_offset = std::min(pn_offset + 4, packet_len - aead_expansion);

  uint8_t protected_pn[4]  = {0};
  uint8_t protected_pn_len = 0;
  pn_len                   = QUICTypeUtil::read_QUICPacketNumberLen(packet + pn_offset);
  if (!pn_protector->protect(protected_pn, protected_pn_len, packet + pn_offset, pn_len, packet + sample_offset, phase)) {
    return false;
  }
  memcpy(packet + pn_offset, protected_pn, pn_len);
  return true;
}

bool
QUICPacket::unprotect_packet_number(uint8_t *packet, size_t packet_len, const QUICPacketNumberProtector *pn_protector)
{
  size_t pn_offset             = 0;
  uint8_t pn_len               = 4;
  size_t sample_offset         = 0;
  constexpr int aead_expansion = 16; // Currently, AEAD expansion (which is probably AEAD tag) length is always 16
  QUICKeyPhase phase;

  if (QUICInvariants::is_long_header(packet)) {
#ifdef DEBUG
    QUICVersion version;
    QUICPacketLongHeader::version(version, packet, packet_len);
    ink_assert(version != 0x0);
#endif

    QUICPacketType type;
    QUICPacketLongHeader::type(type, packet, packet_len);
    switch (type) {
    case QUICPacketType::ZERO_RTT_PROTECTED:
      phase = QUICKeyPhase::ZERORTT;
      break;
    default:
      phase = QUICKeyPhase::CLEARTEXT;
      break;
    }
    QUICPacketLongHeader::packet_number_offset(pn_offset, packet, packet_len);

    Debug("quic", "Unprotecting a packet number of %s packet using %s", QUICDebugNames::packet_type(type),
          QUICDebugNames::key_phase(phase));

  } else {
    QUICPacketShortHeader::key_phase(phase, packet, packet_len);
    QUICPacketShortHeader::packet_number_offset(pn_offset, packet, packet_len, QUICConfigParams::scid_len());
  }
  sample_offset = std::min(pn_offset + 4, packet_len - aead_expansion);

  uint8_t unprotected_pn[4]  = {0};
  uint8_t unprotected_pn_len = 0;
  if (!pn_protector->unprotect(unprotected_pn, unprotected_pn_len, packet + pn_offset, pn_len, packet + sample_offset, phase)) {
    return false;
  }
  unprotected_pn_len = QUICTypeUtil::read_QUICPacketNumberLen(unprotected_pn);
  memcpy(packet + pn_offset, unprotected_pn, unprotected_pn_len);
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
QUICPacketFactory::create(IpEndpoint from, ats_unique_buf buf, size_t len, QUICPacketNumber base_packet_number,
                          QUICPacketCreationResult &result)
{
  size_t max_plain_txt_len = 2048;
  ats_unique_buf plain_txt = ats_unique_malloc(max_plain_txt_len);
  size_t plain_txt_len     = 0;

  QUICPacketHeaderUPtr header = QUICPacketHeader::load(from, std::move(buf), len, base_packet_number);

  QUICConnectionId dcid = header->destination_cid();
  QUICConnectionId scid = header->source_cid();
  QUICDebug(scid, dcid, "Decrypting %s packet #%" PRIu64 " using %s", QUICDebugNames::packet_type(header->type()),
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
      // These packets are unprotected. Just copy the payload
      memcpy(plain_txt.get(), header->payload(), header->payload_size());
      plain_txt_len = header->payload_size();
      result        = QUICPacketCreationResult::SUCCESS;
      break;
    case QUICPacketType::PROTECTED:
      if (this->_hs_protocol->is_key_derived(header->key_phase())) {
        if (this->_hs_protocol->decrypt(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(),
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
      if (this->_hs_protocol->is_key_derived(QUICKeyPhase::CLEARTEXT)) {
        if (QUICTypeUtil::is_supported_version(header->version())) {
          if (this->_hs_protocol->decrypt(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(),
                                          header->payload_size(), header->packet_number(), header->buf(), header->size(),
                                          QUICKeyPhase::CLEARTEXT)) {
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
    case QUICPacketType::RETRY:
      if (this->_hs_protocol->is_key_derived(QUICKeyPhase::CLEARTEXT)) {
        if (this->_hs_protocol->decrypt(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(),
                                        header->payload_size(), header->packet_number(), header->buf(), header->size(),
                                        QUICKeyPhase::CLEARTEXT)) {
          result = QUICPacketCreationResult::SUCCESS;
        } else {
          // ignore failure - probably clear text key is already updated
          // FIXME: make sure packet number is smaller than largest sent packet number
          result = QUICPacketCreationResult::IGNORED;
        }
      } else {
        result = QUICPacketCreationResult::IGNORED;
      }
      break;
    case QUICPacketType::HANDSHAKE:
      if (this->_hs_protocol->is_key_derived(QUICKeyPhase::CLEARTEXT)) {
        if (this->_hs_protocol->decrypt(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(),
                                        header->payload_size(), header->packet_number(), header->buf(), header->size(),
                                        QUICKeyPhase::CLEARTEXT)) {
          result = QUICPacketCreationResult::SUCCESS;
        } else {
          result = QUICPacketCreationResult::FAILED;
        }
      } else {
        result = QUICPacketCreationResult::IGNORED;
      }
      break;
    case QUICPacketType::ZERO_RTT_PROTECTED:
      if (this->_hs_protocol->is_key_derived(QUICKeyPhase::ZERORTT)) {
        if (this->_hs_protocol->decrypt(plain_txt.get(), plain_txt_len, max_plain_txt_len, header->payload(),
                                        header->payload_size(), header->packet_number(), header->buf(), header->size(),
                                        QUICKeyPhase::ZERORTT)) {
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
  size_t len = sizeof(QUICVersion) * countof(QUIC_SUPPORTED_VERSIONS);
  ats_unique_buf versions(reinterpret_cast<uint8_t *>(ats_malloc(len)), [](void *p) { ats_free(p); });
  uint8_t *p = versions.get();

  size_t n;
  for (auto v : QUIC_SUPPORTED_VERSIONS) {
    QUICTypeUtil::write_QUICVersion(v, p, &n);
    p += n;
  }

  // VN packet dosen't have packet number field and version field is always 0x00000000
  QUICPacketHeaderUPtr header =
    QUICPacketHeader::build(QUICPacketType::VERSION_NEGOTIATION, dcid, scid, 0x00, 0x00, 0x00, std::move(versions), len);

  return QUICPacketFactory::_create_unprotected_packet(std::move(header));
}

QUICPacketUPtr
QUICPacketFactory::create_initial_packet(QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                         QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len)
{
  QUICPacketHeaderUPtr header =
    QUICPacketHeader::build(QUICPacketType::INITIAL, destination_cid, source_cid, this->_packet_number_generator.next(),
                            base_packet_number, this->_version, std::move(payload), len);
  return this->_create_encrypted_packet(std::move(header), true);
}

QUICPacketUPtr
QUICPacketFactory::create_retry_packet(QUICConnectionId destination_cid, QUICConnectionId source_cid, ats_unique_buf payload,
                                       size_t len, bool retransmittable)
{
  QUICPacketHeaderUPtr header =
    QUICPacketHeader::build(QUICPacketType::RETRY, destination_cid, source_cid, 0, 0, this->_version, std::move(payload), len);
  return this->_create_encrypted_packet(std::move(header), retransmittable);
}

QUICPacketUPtr
QUICPacketFactory::create_handshake_packet(QUICConnectionId destination_cid, QUICConnectionId source_cid,
                                           QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len,
                                           bool retransmittable)
{
  QUICPacketHeaderUPtr header =
    QUICPacketHeader::build(QUICPacketType::HANDSHAKE, destination_cid, source_cid, this->_packet_number_generator.next(),
                            base_packet_number, this->_version, std::move(payload), len);
  return this->_create_encrypted_packet(std::move(header), retransmittable);
}

QUICPacketUPtr
QUICPacketFactory::create_server_protected_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number,
                                                  ats_unique_buf payload, size_t len, bool retransmittable)
{
  // TODO Key phase should be picked up from QUICHandshakeProtocol, probably
  QUICPacketHeaderUPtr header =
    QUICPacketHeader::build(QUICPacketType::PROTECTED, QUICKeyPhase::PHASE_0, connection_id, this->_packet_number_generator.next(),
                            base_packet_number, std::move(payload), len);
  return this->_create_encrypted_packet(std::move(header), retransmittable);
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
  QUICPacketHeaderUPtr header = QUICPacketHeader::build(QUICPacketType::STATELESS_RESET, QUICKeyPhase::CLEARTEXT, connection_id,
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
  new (packet) QUICPacket(std::move(header), std::move(cleartext), cleartext_len, false);

  return QUICPacketUPtr(packet, &QUICPacketDeleter::delete_packet);
}

QUICPacketUPtr
QUICPacketFactory::_create_encrypted_packet(QUICPacketHeaderUPtr header, bool retransmittable)
{
  // TODO: use pmtu of UnixNetVConnection
  size_t max_cipher_txt_len = 2048;
  ats_unique_buf cipher_txt = ats_unique_malloc(max_cipher_txt_len);
  size_t cipher_txt_len     = 0;

  QUICConnectionId dcid = header->destination_cid();
  QUICConnectionId scid = header->source_cid();
  QUICDebug(dcid, scid, "Encrypting %s packet #%" PRIu64 " using %s", QUICDebugNames::packet_type(header->type()),
            header->packet_number(), QUICDebugNames::key_phase(header->key_phase()));

  QUICPacket *packet = nullptr;
  if (this->_hs_protocol->encrypt(cipher_txt.get(), cipher_txt_len, max_cipher_txt_len, header->payload(), header->payload_size(),
                                  header->packet_number(), header->buf(), header->size(), header->key_phase())) {
    packet = quicPacketAllocator.alloc();
    new (packet) QUICPacket(std::move(header), std::move(cipher_txt), cipher_txt_len, retransmittable);
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

void
QUICPacketFactory::set_hs_protocol(QUICHandshakeProtocol *hs_protocol)
{
  this->_hs_protocol = hs_protocol;
}

bool
QUICPacketFactory::is_ready_to_create_protected_packet()
{
  return this->_hs_protocol->is_handshake_finished();
}
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
