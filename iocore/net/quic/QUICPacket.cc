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

#include <tscore/ink_assert.h>
#include <tscore/Diags.h>

#include "QUICIntUtil.h"
#include "QUICDebugNames.h"

using namespace std::literals;
static constexpr std::string_view tag  = "quic_packet"sv;
static constexpr uint64_t aead_tag_len = 16;

#define QUICDebug(dcid, scid, fmt, ...) \
  Debug(tag.data(), "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__);

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
  return this->_buf.get();
}

const IpEndpoint &
QUICPacketHeader::from() const
{
  return this->_from;
}

const IpEndpoint &
QUICPacketHeader::to() const
{
  return this->_to;
}

bool
QUICPacketHeader::is_crypto_packet() const
{
  return false;
}

uint16_t
QUICPacketHeader::packet_size() const
{
  return this->_buf_len;
}

QUICPacketHeaderUPtr
QUICPacketHeader::load(const IpEndpoint from, const IpEndpoint to, ats_unique_buf buf, size_t len, QUICPacketNumber base)
{
  QUICPacketHeaderUPtr header = QUICPacketHeaderUPtr(nullptr, &QUICPacketHeaderDeleter::delete_null_header);
  if (QUICInvariants::is_long_header(buf.get())) {
    QUICPacketLongHeader *long_header = quicPacketLongHeaderAllocator.alloc();
    new (long_header) QUICPacketLongHeader(from, to, std::move(buf), len, base);
    header = QUICPacketHeaderUPtr(long_header, &QUICPacketHeaderDeleter::delete_long_header);
  } else {
    QUICPacketShortHeader *short_header = quicPacketShortHeaderAllocator.alloc();
    new (short_header) QUICPacketShortHeader(from, to, std::move(buf), len, base);
    header = QUICPacketHeaderUPtr(short_header, &QUICPacketHeaderDeleter::delete_short_header);
  }
  return header;
}

//
// QUICPacketLongHeader
//

QUICPacketLongHeader::QUICPacketLongHeader(const IpEndpoint from, const IpEndpoint to, ats_unique_buf buf, size_t len,
                                           QUICPacketNumber base)
  : QUICPacketHeader(from, to, std::move(buf), len, base)
{
  this->_key_phase = QUICTypeUtil::key_phase(this->type());
  uint8_t *raw_buf = this->_buf.get();

  uint8_t dcil = 0;
  uint8_t scil = 0;
  QUICPacketLongHeader::dcil(dcil, raw_buf, len);
  QUICPacketLongHeader::scil(scil, raw_buf, len);

  size_t offset          = LONG_HDR_OFFSET_CONNECTION_ID;
  this->_destination_cid = {raw_buf + offset, dcil};
  offset += dcil + 1;
  this->_source_cid = {raw_buf + offset, scil};
  offset += scil;

  if (this->type() != QUICPacketType::VERSION_NEGOTIATION) {
    if (this->type() == QUICPacketType::RETRY) {
      uint8_t odcil = raw_buf[offset];
      offset += 1;

      this->_original_dcid = {raw_buf + offset, odcil};
      offset += odcil;
    } else {
      if (this->type() == QUICPacketType::INITIAL) {
        // Token Length Field
        this->_token_len = QUICIntUtil::read_QUICVariableInt(raw_buf + offset);
        offset += QUICVariableInt::size(raw_buf + offset);
        // Token Field
        this->_token_offset = offset;
        offset += this->_token_len;
      }

      // Length Field
      offset += QUICVariableInt::size(raw_buf + offset);

      // PN Field
      int pn_len = QUICTypeUtil::read_QUICPacketNumberLen(raw_buf);
      QUICPacket::decode_packet_number(this->_packet_number, QUICTypeUtil::read_QUICPacketNumber(raw_buf + offset, pn_len), pn_len,
                                       this->_base_packet_number);
      offset += pn_len;
    }
  }

  this->_payload_offset = offset;
  this->_payload_length = len - this->_payload_offset;
}

QUICPacketType
QUICPacketLongHeader::type() const
{
  if (this->_buf) {
    QUICPacketType type = QUICPacketType::UNINITIALIZED;
    QUICPacketLongHeader::type(type, this->_buf.get(), this->_buf_len);
    return type;
  } else {
    return this->_type;
  }
}

bool
QUICPacketLongHeader::is_crypto_packet() const
{
  return this->_is_crypto_packet;
}

bool
QUICPacketLongHeader::type(QUICPacketType &type, const uint8_t *packet, size_t packet_len)
{
  if (packet_len < 1) {
    return false;
  }

  QUICVersion version;
  if (QUICPacketLongHeader::version(version, packet, packet_len) && version == 0x00) {
    type = QUICPacketType::VERSION_NEGOTIATION;
  } else {
    uint8_t raw_type = (packet[0] & 0x30) >> 4;
    type             = static_cast<QUICPacketType>(raw_type);
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
    return true;
  } else {
    return false;
  }
}

bool
QUICPacketLongHeader::scil(uint8_t &scil, const uint8_t *packet, size_t packet_len)
{
  if (QUICInvariants::scil(scil, packet, packet_len)) {
    return true;
  } else {
    return false;
  }
}

bool
QUICPacketLongHeader::token_length(size_t &token_length, uint8_t &field_len, size_t &token_length_filed_offset,
                                   const uint8_t *packet, size_t packet_len)
{
  QUICPacketType type = QUICPacketType::UNINITIALIZED;
  QUICPacketLongHeader::type(type, packet, packet_len);

  if (type != QUICPacketType::INITIAL) {
    token_length = 0;
    field_len    = 0;

    return true;
  }

  uint8_t dcil, scil;
  QUICPacketLongHeader::dcil(dcil, packet, packet_len);
  QUICPacketLongHeader::scil(scil, packet, packet_len);

  token_length_filed_offset = LONG_HDR_OFFSET_CONNECTION_ID + dcil + 1 + scil;
  if (token_length_filed_offset >= packet_len) {
    return false;
  }

  token_length = QUICIntUtil::read_QUICVariableInt(packet + token_length_filed_offset);
  field_len    = QUICVariableInt::size(packet + token_length_filed_offset);

  return true;
}

bool
QUICPacketLongHeader::length(size_t &length, uint8_t &length_field_len, size_t &length_field_offset, const uint8_t *packet,
                             size_t packet_len)
{
  uint8_t dcil;
  if (!QUICPacketLongHeader::dcil(dcil, packet, packet_len)) {
    return false;
  }

  uint8_t scil;
  if (!QUICPacketLongHeader::scil(scil, packet, packet_len)) {
    return false;
  }

  // Token Length (i) + Token (*) (for INITIAL packet)
  size_t token_length              = 0;
  uint8_t token_length_field_len   = 0;
  size_t token_length_field_offset = 0;
  if (!QUICPacketLongHeader::token_length(token_length, token_length_field_len, token_length_field_offset, packet, packet_len)) {
    return false;
  }

  // Length (i)
  length_field_offset = LONG_HDR_OFFSET_CONNECTION_ID + dcil + 1 + scil + token_length_field_len + token_length;
  if (length_field_offset >= packet_len) {
    return false;
  }

  length_field_len = QUICVariableInt::size(packet + length_field_offset);
  length           = QUICIntUtil::read_QUICVariableInt(packet + length_field_offset);

  return true;
}

bool
QUICPacketLongHeader::packet_number_offset(size_t &pn_offset, const uint8_t *packet, size_t packet_len)
{
  size_t length;
  uint8_t length_field_len;
  size_t length_field_offset;

  if (!QUICPacketLongHeader::length(length, length_field_len, length_field_offset, packet, packet_len)) {
    return false;
  }
  pn_offset = length_field_offset + length_field_len;

  if (pn_offset >= packet_len) {
    return false;
  }

  return true;
}

bool
QUICPacketLongHeader::packet_length(size_t &packet_len, const uint8_t *buf, size_t buf_len)
{
  size_t length;
  uint8_t length_field_len;
  size_t length_field_offset;

  if (!QUICPacketLongHeader::length(length, length_field_len, length_field_offset, buf, buf_len)) {
    return false;
  }
  packet_len = length + length_field_offset + length_field_len;

  if (packet_len > buf_len) {
    return false;
  }

  return true;
}

bool
QUICPacketLongHeader::key_phase(QUICKeyPhase &phase, const uint8_t *packet, size_t packet_len)
{
  QUICPacketType type = QUICPacketType::UNINITIALIZED;
  QUICPacketLongHeader::type(type, packet, packet_len);
  phase = QUICTypeUtil::key_phase(type);
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

QUICConnectionId
QUICPacketLongHeader::original_dcid() const
{
  return this->_original_dcid;
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
    QUICVersion version = 0;
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

const uint8_t *
QUICPacketLongHeader::token() const
{
  if (this->_buf) {
    uint8_t *raw = this->_buf.get();
    return raw + this->_token_offset;
  } else {
    return this->_token.get();
  }
}

size_t
QUICPacketLongHeader::token_len() const
{
  return this->_token_len;
}

QUICKeyPhase
QUICPacketLongHeader::key_phase() const
{
  return this->_key_phase;
}

uint16_t
QUICPacketLongHeader::size() const
{
  return this->_buf_len - this->_payload_length;
}

//
// QUICPacketShortHeader
//

QUICPacketShortHeader::QUICPacketShortHeader(const IpEndpoint from, const IpEndpoint to, ats_unique_buf buf, size_t len,
                                             QUICPacketNumber base)
  : QUICPacketHeader(from, to, std::move(buf), len, base)
{
  QUICInvariants::dcid(this->_connection_id, this->_buf.get(), len);

  int offset               = 1 + this->_connection_id.length();
  this->_packet_number_len = QUICTypeUtil::read_QUICPacketNumberLen(this->_buf.get());
  QUICPacketNumber src     = QUICTypeUtil::read_QUICPacketNumber(this->_buf.get() + offset, this->_packet_number_len);
  QUICPacket::decode_packet_number(this->_packet_number, src, this->_packet_number_len, this->_base_packet_number);
  this->_payload_length = len - (1 + QUICConnectionId::SCID_LEN + this->_packet_number_len);
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

QUICKeyPhase
QUICPacketShortHeader::key_phase() const
{
  if (this->_buf) {
    QUICKeyPhase phase = QUICKeyPhase::INITIAL;
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
  if (packet[0] & 0x04) {
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
  if (this->_connection_id != QUICConnectionId::ZERO()) {
    len += this->_connection_id.length();
  }
  len += this->_packet_number_len;

  return len;
}

//
// QUICPacket
//
QUICPacket::QUICPacket() {}

QUICPacket::QUICPacket(UDPConnection *udp_con, QUICPacketHeaderUPtr header, ats_unique_buf payload, size_t payload_len)
  : _udp_con(udp_con), _header(std::move(header)), _payload(std::move(payload)), _payload_size(payload_len)
{
}

QUICPacket::QUICPacket(bool ack_eliciting, bool probing) : _is_ack_eliciting(ack_eliciting), _is_probing_packet(probing) {}

QUICPacket::~QUICPacket()
{
  this->_header = nullptr;
}

const IpEndpoint &
QUICPacket::from() const
{
  return this->_header->from();
}

const IpEndpoint &
QUICPacket::to() const
{
  return this->_header->to();
}

UDPConnection *
QUICPacket::udp_con() const
{
  return this->_udp_con;
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

bool
QUICPacket::is_crypto_packet() const
{
  return this->_header->is_crypto_packet();
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
QUICPacket::is_ack_eliciting() const
{
  return this->_is_ack_eliciting;
}

bool
QUICPacket::is_probing_packet() const
{
  return this->_is_probing_packet;
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
  size_t written = 0;
  Ptr<IOBufferBlock> block;

  block = this->header_block();
  while (block) {
    memcpy(buf + written, block->start(), block->size());
    written += block->size();
    block = block->next;
  }

  block = this->payload_block();
  while (block) {
    memcpy(buf + written, block->start(), block->size());
    written += block->size();
    block = block->next;
  }

  *len = written;
}

uint8_t
QUICPacket::calc_packet_number_len(QUICPacketNumber num, QUICPacketNumber base)
{
  uint64_t d  = (num - base) * 2;
  uint8_t len = 0;

  if (d > 0xFFFFFF) {
    len = 4;
  } else if (d > 0xFFFF) {
    len = 3;
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
  uint64_t mask = 0;
  switch (len) {
  case 1:
    mask = 0xFF;
    break;
  case 2:
    mask = 0xFFFF;
    break;
  case 3:
    mask = 0xFFFFFF;
    break;
  case 4:
    mask = 0xFFFFFFFF;
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
  ink_assert(len == 1 || len == 2 || len == 3 || len == 4);

  uint64_t maximum_diff = 0;
  switch (len) {
  case 1:
    maximum_diff = 0x100;
    break;
  case 2:
    maximum_diff = 0x10000;
    break;
  case 3:
    maximum_diff = 0x1000000;
    break;
  case 4:
    maximum_diff = 0x100000000;
    break;
  default:
    ink_assert(!"len must be 1, 2, 3 or 4");
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

//
// QUICLongHeaderPacket
//
QUICLongHeaderPacket::QUICLongHeaderPacket(QUICVersion version, QUICConnectionId dcid, QUICConnectionId scid, bool ack_eliciting,
                                           bool probing, bool crypto)
  : QUICPacket(ack_eliciting, probing), _version(version), _dcid(dcid), _scid(scid), _is_crypto_packet(crypto)
{
}

uint16_t
QUICLongHeaderPacket::size() const
{
  size_t header_size     = 0;
  Ptr<IOBufferBlock> tmp = this->header_block();
  while (tmp) {
    header_size += tmp->size();
    tmp = tmp->next;
  }
  return header_size + this->_payload_length;
}

QUICConnectionId
QUICLongHeaderPacket::destination_cid() const
{
  return this->_dcid;
}

QUICConnectionId
QUICLongHeaderPacket::source_cid() const
{
  return this->_scid;
}

QUICVersion
QUICLongHeaderPacket::version() const
{
  return this->_version;
}

size_t
QUICLongHeaderPacket::_write_common_header(uint8_t *buf) const
{
  size_t n;
  size_t len = 0;

  buf[0] = 0xC0;
  buf[0] += static_cast<uint8_t>(this->type()) << 4;
  len += 1;

  QUICTypeUtil::write_QUICVersion(this->_version, buf + len, &n);
  len += n;

  // DICD
  if (this->_dcid != QUICConnectionId::ZERO()) {
    // Len
    buf[len] = this->_dcid.length();
    len += 1;

    // ID
    QUICTypeUtil::write_QUICConnectionId(this->_dcid, buf + len, &n);
    len += n;
  } else {
    buf[len] = 0;
    len += 1;
  }

  // SCID
  if (this->_scid != QUICConnectionId::ZERO()) {
    // Len
    buf[len] = this->_scid.length();
    len += 1;

    // ID
    QUICTypeUtil::write_QUICConnectionId(this->_scid, buf + len, &n);
    len += n;
  } else {
    buf[len] = 0;
    len += 1;
  }

  return len;
}

bool
QUICLongHeaderPacket::is_crypto_packet() const
{
  return this->_is_crypto_packet;
}

//
// QUICShortHeaderPacket
//
QUICShortHeaderPacket::QUICShortHeaderPacket(QUICConnectionId dcid, QUICPacketNumber packet_number,
                                             QUICPacketNumber base_packet_number, QUICKeyPhase key_phase, bool ack_eliciting,
                                             bool probing)
  : QUICPacket(ack_eliciting, probing), _dcid(dcid), _packet_number(packet_number), _key_phase(key_phase)
{
  this->_packet_number_len = QUICPacket::calc_packet_number_len(packet_number, base_packet_number);
}

QUICPacketType
QUICShortHeaderPacket::type() const
{
  return QUICPacketType::PROTECTED;
}

QUICKeyPhase
QUICShortHeaderPacket::key_phase() const
{
  return this->_key_phase;
}

QUICConnectionId
QUICShortHeaderPacket::destination_cid() const
{
  return this->_dcid;
}

QUICPacketNumber
QUICShortHeaderPacket::packet_number() const
{
  return this->_packet_number;
}

uint16_t
QUICShortHeaderPacket::size() const
{
  size_t header_size     = 0;
  Ptr<IOBufferBlock> tmp = this->header_block();
  while (tmp) {
    header_size += tmp->size();
    tmp = tmp->next;
  }
  return header_size + this->_payload_length;
}

bool
QUICShortHeaderPacket::is_crypto_packet() const
{
  return false;
}

Ptr<IOBufferBlock>
QUICShortHeaderPacket::header_block() const
{
  Ptr<IOBufferBlock> block;
  size_t written_len = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + QUICConnectionId::MAX_LENGTH + 4));
  uint8_t *buf = reinterpret_cast<uint8_t *>(block->start());

  size_t n;
  buf[0] = 0x40;

  // Type
  buf[0] = 0x40;

  // TODO Spin Bit

  // KeyPhase
  if (this->_key_phase == QUICKeyPhase::PHASE_1) {
    buf[0] |= 0x04;
  }

  written_len += 1;

  // Destination Connection ID
  if (this->_dcid != QUICConnectionId::ZERO()) {
    QUICTypeUtil::write_QUICConnectionId(this->_dcid, buf + written_len, &n);
    written_len += n;
  }

  // Packet Number
  QUICPacketNumber dst = 0;
  size_t dst_len       = this->_packet_number_len;
  QUICPacket::encode_packet_number(dst, this->_packet_number, dst_len);
  QUICTypeUtil::write_QUICPacketNumber(dst, dst_len, buf + written_len, &n);
  written_len += n;

  // Packet Number Length
  QUICTypeUtil::write_QUICPacketNumberLen(n, buf);

  block->fill(written_len);

  return block;
}

Ptr<IOBufferBlock>
QUICShortHeaderPacket::payload_block() const
{
  return this->_payload_block;
}

void
QUICShortHeaderPacket::attach_payload(Ptr<IOBufferBlock> payload, bool unprotected)
{
  this->_payload_block   = payload;
  this->_payload_length  = 0;
  Ptr<IOBufferBlock> tmp = payload;
  while (tmp) {
    this->_payload_length += tmp->size();
    tmp = tmp->next;
  }
  if (unprotected) {
    this->_payload_length += aead_tag_len;
  }
}

//
// QUICStatelessResetPacket
//
QUICStatelessResetPacket::QUICStatelessResetPacket(QUICStatelessResetToken token) : QUICPacket(), _token(token) {}

QUICPacketType
QUICStatelessResetPacket::type() const
{
  return QUICPacketType::STATELESS_RESET;
}

Ptr<IOBufferBlock>
QUICStatelessResetPacket::header_block() const
{
  constexpr uint8_t MIN_UNPREDICTABLE_FIELD_LEN = 5;
  std::random_device rnd;

  Ptr<IOBufferBlock> block;
  size_t written_len = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(MIN_UNPREDICTABLE_FIELD_LEN));
  uint8_t *buf = reinterpret_cast<uint8_t *>(block->start());

  // Generate random octets
  for (int i = 0; i < MIN_UNPREDICTABLE_FIELD_LEN; ++i) {
    buf[i] = static_cast<uint8_t>(rnd() & 0xFF);
  }
  written_len += MIN_UNPREDICTABLE_FIELD_LEN;

  block->fill(written_len);

  return block;
}

Ptr<IOBufferBlock>
QUICStatelessResetPacket::payload_block() const
{
  Ptr<IOBufferBlock> block;
  size_t written_len = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(QUICStatelessResetToken::LEN));
  uint8_t *buf = reinterpret_cast<uint8_t *>(block->start());

  memcpy(buf, this->_token.buf(), QUICStatelessResetToken::LEN);
  written_len += QUICStatelessResetToken::LEN;

  block->fill(written_len);

  return block;
}

QUICStatelessResetToken
QUICStatelessResetPacket::token() const
{
  return this->_token;
}

//
// QUICVersionNegotiationPacket
//
QUICVersionNegotiationPacket::QUICVersionNegotiationPacket(QUICConnectionId dcid, QUICConnectionId scid,
                                                           const QUICVersion versions[], int nversions)
  : QUICLongHeaderPacket(0, dcid, scid, false, false, false), _versions(versions), _nversions(nversions)
{
}

QUICPacketType
QUICVersionNegotiationPacket::type() const
{
  return QUICPacketType::VERSION_NEGOTIATION;
}

QUICVersion
QUICVersionNegotiationPacket::version() const
{
  return 0;
}

Ptr<IOBufferBlock>
QUICVersionNegotiationPacket::header_block() const
{
  Ptr<IOBufferBlock> block;
  size_t written_len = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(2048));
  uint8_t *buf = reinterpret_cast<uint8_t *>(block->start());

  // Common Long Header
  written_len += this->_write_common_header(buf + written_len);

  // Overwrite the first byte
  buf[0] = 0x80 | rand();

  block->fill(written_len);

  return block;
}

Ptr<IOBufferBlock>
QUICVersionNegotiationPacket::payload_block() const
{
  Ptr<IOBufferBlock> block;
  uint8_t *buf;
  size_t written_len = 0;
  size_t n;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(sizeof(QUICVersion) * (this->_nversions + 1)));
  buf = reinterpret_cast<uint8_t *>(block->start());

  for (auto i = 0; i < this->_nversions; ++i) {
    QUICTypeUtil::write_QUICVersion(*(this->_versions + i), buf + written_len, &n);
    written_len += n;
  }

  // [draft-18] 6.3. Using Reserved Versions
  // To help ensure this, a server SHOULD include a reserved version (see Section 15) while generating a
  // Version Negotiation packet.
  QUICTypeUtil::write_QUICVersion(QUIC_EXERCISE_VERSION, buf + written_len, &n);
  written_len += n;

  block->fill(written_len);

  return block;
}

const QUICVersion *
QUICVersionNegotiationPacket::versions() const
{
  return this->_versions;
}

int
QUICVersionNegotiationPacket::nversions() const
{
  return _nversions;
}

//
// QUICInitialPacket
//
QUICInitialPacket::QUICInitialPacket(QUICVersion version, QUICConnectionId dcid, QUICConnectionId scid, size_t token_len,
                                     ats_unique_buf token, size_t length, QUICPacketNumber packet_number, bool ack_eliciting,
                                     bool probing, bool crypto)
  : QUICLongHeaderPacket(version, dcid, scid, ack_eliciting, probing, crypto),
    _token_len(token_len),
    _token(std::move(token)),
    _packet_number(packet_number)
{
}

QUICPacketType
QUICInitialPacket::type() const
{
  return QUICPacketType::INITIAL;
}

QUICKeyPhase
QUICInitialPacket::key_phase() const
{
  return QUICKeyPhase::INITIAL;
}

QUICPacketNumber
QUICInitialPacket::packet_number() const
{
  return this->_packet_number;
}

Ptr<IOBufferBlock>
QUICInitialPacket::header_block() const
{
  Ptr<IOBufferBlock> block;
  size_t written_len = 0;
  size_t n;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(2048));
  uint8_t *buf = reinterpret_cast<uint8_t *>(block->start());

  // Common Long Header
  written_len += this->_write_common_header(buf + written_len);

  // Token Length
  QUICIntUtil::write_QUICVariableInt(this->_token_len, buf + written_len, &n);
  written_len += n;

  // Token
  memcpy(buf + written_len, this->_token.get(), this->_token_len);
  written_len += this->_token_len;

  QUICPacketNumber pn = 0;
  size_t pn_len       = 4;
  QUICPacket::encode_packet_number(pn, this->_packet_number, pn_len);

  if (pn > 0x7FFFFF) {
    pn_len = 4;
  } else if (pn > 0x7FFF) {
    pn_len = 3;
  } else if (pn > 0x7F) {
    pn_len = 2;
  } else {
    pn_len = 1;
  }

  // PN Len
  QUICTypeUtil::write_QUICPacketNumberLen(pn_len, buf);

  // Length
  QUICIntUtil::write_QUICVariableInt(pn_len + this->_payload_length, buf + written_len, &n);
  written_len += n;

  // PN Field
  QUICTypeUtil::write_QUICPacketNumber(pn, pn_len, buf + written_len, &n);
  written_len += n;

  block->fill(written_len);

  return block;
}

Ptr<IOBufferBlock>
QUICInitialPacket::payload_block() const
{
  return this->_payload_block;
}

void
QUICInitialPacket::attach_payload(Ptr<IOBufferBlock> payload, bool unprotected)
{
  this->_payload_block   = payload;
  this->_payload_length  = 0;
  Ptr<IOBufferBlock> tmp = payload;
  while (tmp) {
    this->_payload_length += tmp->size();
    tmp = tmp->next;
  }
  if (unprotected) {
    this->_payload_length += aead_tag_len;
  }
}

//
// QUICZeroRttPacket
//
QUICZeroRttPacket::QUICZeroRttPacket(QUICVersion version, QUICConnectionId dcid, QUICConnectionId scid, size_t length,
                                     QUICPacketNumber packet_number, bool ack_eliciting, bool probing)
  : QUICLongHeaderPacket(version, dcid, scid, ack_eliciting, probing, false), _packet_number(packet_number)
{
}

QUICPacketType
QUICZeroRttPacket::type() const
{
  return QUICPacketType::ZERO_RTT_PROTECTED;
}

QUICKeyPhase
QUICZeroRttPacket::key_phase() const
{
  return QUICKeyPhase::ZERO_RTT;
}

QUICPacketNumber
QUICZeroRttPacket::packet_number() const
{
  return this->_packet_number;
}

Ptr<IOBufferBlock>
QUICZeroRttPacket::header_block() const
{
  Ptr<IOBufferBlock> block;
  size_t written_len = 0;
  size_t n;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(2048));
  uint8_t *buf = reinterpret_cast<uint8_t *>(block->start());

  // Common Long Header
  written_len += this->_write_common_header(buf + written_len);

  QUICPacketNumber pn = 0;
  size_t pn_len       = 4;
  QUICPacket::encode_packet_number(pn, this->_packet_number, pn_len);

  if (pn > 0x7FFFFF) {
    pn_len = 4;
  } else if (pn > 0x7FFF) {
    pn_len = 3;
  } else if (pn > 0x7F) {
    pn_len = 2;
  } else {
    pn_len = 1;
  }

  // PN Len
  QUICTypeUtil::write_QUICPacketNumberLen(pn_len, buf);

  // Length
  QUICIntUtil::write_QUICVariableInt(pn_len + this->_payload_length, buf + written_len, &n);
  written_len += n;

  // PN Field
  QUICTypeUtil::write_QUICPacketNumber(pn, pn_len, buf + written_len, &n);
  written_len += n;

  block->fill(written_len);

  return block;
}

Ptr<IOBufferBlock>
QUICZeroRttPacket::payload_block() const
{
  return this->_payload_block;
}

void
QUICZeroRttPacket::attach_payload(Ptr<IOBufferBlock> payload, bool unprotected)
{
  this->_payload_block   = payload;
  this->_payload_length  = 0;
  Ptr<IOBufferBlock> tmp = payload;
  while (tmp) {
    this->_payload_length += tmp->size();
    tmp = tmp->next;
  }
  if (unprotected) {
    this->_payload_length += aead_tag_len;
  }
}

//
// QUICHandshakePacket
//
QUICHandshakePacket::QUICHandshakePacket(QUICVersion version, QUICConnectionId dcid, QUICConnectionId scid, size_t length,
                                         QUICPacketNumber packet_number, bool ack_eliciting, bool probing, bool crypto)
  : QUICLongHeaderPacket(version, dcid, scid, ack_eliciting, probing, crypto), _packet_number(packet_number)
{
}

QUICPacketType
QUICHandshakePacket::type() const
{
  return QUICPacketType::HANDSHAKE;
}

QUICKeyPhase
QUICHandshakePacket::key_phase() const
{
  return QUICKeyPhase::HANDSHAKE;
}

QUICPacketNumber
QUICHandshakePacket::packet_number() const
{
  return this->_packet_number;
}

Ptr<IOBufferBlock>
QUICHandshakePacket::header_block() const
{
  Ptr<IOBufferBlock> block;
  size_t written_len = 0;
  size_t n;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(2048));
  uint8_t *buf = reinterpret_cast<uint8_t *>(block->start());

  // Common Long Header
  written_len += this->_write_common_header(buf + written_len);

  QUICPacketNumber pn = 0;
  size_t pn_len       = 4;
  QUICPacket::encode_packet_number(pn, this->_packet_number, pn_len);

  if (pn > 0x7FFFFF) {
    pn_len = 4;
  } else if (pn > 0x7FFF) {
    pn_len = 3;
  } else if (pn > 0x7F) {
    pn_len = 2;
  } else {
    pn_len = 1;
  }

  // PN Len
  QUICTypeUtil::write_QUICPacketNumberLen(pn_len, buf);

  // Length
  QUICIntUtil::write_QUICVariableInt(pn_len + this->_payload_length, buf + written_len, &n);
  written_len += n;

  // PN Field
  QUICTypeUtil::write_QUICPacketNumber(pn, pn_len, buf + written_len, &n);
  written_len += n;

  block->fill(written_len);

  return block;
}

Ptr<IOBufferBlock>
QUICHandshakePacket::payload_block() const
{
  return this->_payload_block;
}

void
QUICHandshakePacket::attach_payload(Ptr<IOBufferBlock> payload, bool unprotected)
{
  this->_payload_block   = payload;
  this->_payload_length  = 0;
  Ptr<IOBufferBlock> tmp = payload;
  while (tmp) {
    this->_payload_length += tmp->size();
    tmp = tmp->next;
  }
  if (unprotected) {
    this->_payload_length += aead_tag_len;
  }
}

//
// QUICRetryPacket
//
QUICRetryPacket::QUICRetryPacket(QUICVersion version, QUICConnectionId dcid, QUICConnectionId scid, QUICConnectionId ocid,
                                 QUICRetryToken &token)
  : QUICLongHeaderPacket(version, dcid, scid, false, false, false), _ocid(ocid), _token(token)
{
}

QUICPacketType
QUICRetryPacket::type() const
{
  return QUICPacketType::RETRY;
}

uint16_t
QUICRetryPacket::size() const
{
  size_t size            = 0;
  Ptr<IOBufferBlock> tmp = this->header_block();
  while (tmp) {
    size += tmp->size();
    tmp = tmp->next;
  }

  tmp = this->payload_block();
  while (tmp) {
    size += tmp->size();
    tmp = tmp->next;
  }

  return size;
}

Ptr<IOBufferBlock>
QUICRetryPacket::header_block() const
{
  Ptr<IOBufferBlock> block;
  size_t written_len = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(2048));
  uint8_t *buf = reinterpret_cast<uint8_t *>(block->start());

  // Common Long Header
  written_len += this->_write_common_header(buf + written_len);

  block->fill(written_len);

  return block;
}

Ptr<IOBufferBlock>
QUICRetryPacket::payload_block() const
{
  Ptr<IOBufferBlock> block;
  uint8_t *buf;
  size_t written_len = 0;
  size_t n;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(QUICConnectionId::MAX_LENGTH + this->_token.length()));
  buf = reinterpret_cast<uint8_t *>(block->start());

  // Original Destination Connection ID
  if (this->_ocid != QUICConnectionId::ZERO()) {
    // Len
    buf[written_len] = this->_ocid.length();
    written_len += 1;

    // ID
    QUICTypeUtil::write_QUICConnectionId(this->_ocid, buf + written_len, &n);
    written_len += n;
  } else {
    buf[written_len] = 0;
    written_len += 1;
  }

  // Retry Token
  memcpy(buf + written_len, this->_token.buf(), this->_token.length());
  written_len += this->_token.length();

  block->fill(written_len);

  return block;
}

QUICConnectionId
QUICRetryPacket::original_dcid() const
{
  return this->_ocid;
}

QUICRetryToken
QUICRetryPacket::token() const
{
  return this->_token;
}
