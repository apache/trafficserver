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

#include <algorithm>

#include <tscore/ink_assert.h>
#include <tscore/Diags.h>

#include "QUICIntUtil.h"
#include "QUICDebugNames.h"
#include "QUICRetryIntegrityTag.h"

using namespace std::literals;
static constexpr uint64_t aead_tag_len             = 16;
static constexpr int LONG_HDR_OFFSET_CONNECTION_ID = 6;
static constexpr int LONG_HDR_OFFSET_VERSION       = 1;

#define QUICDebug(dcid, scid, fmt, ...) \
  Debug(tag.data(), "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__);

//
// QUICPacket
//
QUICPacket::QUICPacket() {}

QUICPacket::QUICPacket(bool ack_eliciting, bool probing) : _is_ack_eliciting(ack_eliciting), _is_probing_packet(probing) {}

QUICPacket::~QUICPacket() {}

QUICKeyPhase
QUICPacket::key_phase() const
{
  ink_assert(!"This function should not be called");
  return QUICKeyPhase::INITIAL;
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
QUICPacket::header_size() const
{
  uint16_t size = 0;

  for (auto b = this->header_block(); b; b = b->next) {
    size += b->size();
  }

  return size;
}

uint16_t
QUICPacket::payload_length() const
{
  uint16_t size = 0;

  for (auto b = this->payload_block(); b; b = b->next) {
    size += b->size();
  }

  return size;
}

uint16_t
QUICPacket::size() const
{
  return this->header_size() + this->payload_length();
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
// QUICPacketR
//

QUICPacketR::QUICPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to) : _udp_con(udp_con), _from(from), _to(to) {}

UDPConnection *
QUICPacketR::udp_con() const
{
  return this->_udp_con;
}

const IpEndpoint &
QUICPacketR::from() const
{
  return this->_from;
}

const IpEndpoint &
QUICPacketR::to() const
{
  return this->_to;
}

bool
QUICPacketR::type(QUICPacketType &type, const uint8_t *packet, size_t packet_len)
{
  if (packet_len < 1) {
    return false;
  }

  if (QUICInvariants::is_long_header(packet)) {
    return QUICLongHeaderPacketR::type(type, packet, packet_len);
  } else {
    type = QUICPacketType::PROTECTED;
    return true;
  }
}

bool
QUICPacketR::read_essential_info(Ptr<IOBufferBlock> block, QUICPacketType &type, QUICVersion &version, QUICConnectionId &dcid,
                                 QUICConnectionId &scid, QUICPacketNumber &packet_number, QUICPacketNumber base_packet_number,
                                 QUICKeyPhase &key_phase)
{
  uint8_t tmp[47 + 64];
  IOBufferReader reader;
  reader.block = block;
  int64_t len  = std::min(static_cast<int64_t>(sizeof(tmp)), reader.read_avail());

  if (len < 10) {
    return false;
  }

  reader.memcpy(tmp, 1, 0);
  if (QUICInvariants::is_long_header(tmp)) {
    reader.memcpy(tmp, len, 0);
    type = static_cast<QUICPacketType>((0x30 & tmp[0]) >> 4);
    QUICInvariants::version(version, tmp, len);
    if (version == 0x00) {
      type = QUICPacketType::VERSION_NEGOTIATION;
    }
    if (!QUICInvariants::dcid(dcid, tmp, len) || !QUICInvariants::scid(scid, tmp, len)) {
      return false;
    }
    if (type != QUICPacketType::RETRY) {
      int packet_number_len = QUICTypeUtil::read_QUICPacketNumberLen(tmp);
      size_t length_offset  = 7 + dcid.length() + scid.length();
      if (length_offset >= static_cast<uint64_t>(len)) {
        return false;
      }
      uint64_t value;
      size_t field_len;
      QUICVariableInt::decode(value, field_len, tmp + length_offset);
      switch (type) {
      case QUICPacketType::INITIAL:
        length_offset += field_len + value;
        if (length_offset >= static_cast<uint64_t>(len)) {
          return false;
        }
        QUICVariableInt::decode(value, field_len, tmp + length_offset);
        if (length_offset + field_len >= static_cast<uint64_t>(len)) {
          return false;
        }
        if (length_offset + field_len + packet_number_len > static_cast<uint64_t>(len)) {
          return false;
        }
        packet_number = QUICTypeUtil::read_QUICPacketNumber(tmp + length_offset + field_len, packet_number_len);
        key_phase     = QUICKeyPhase::INITIAL;
        break;
      case QUICPacketType::ZERO_RTT_PROTECTED:
        if (length_offset + field_len + packet_number_len >= static_cast<uint64_t>(len)) {
          return false;
        }
        packet_number = QUICTypeUtil::read_QUICPacketNumber(tmp + length_offset + field_len, packet_number_len);
        key_phase     = QUICKeyPhase::ZERO_RTT;
        break;
      case QUICPacketType::HANDSHAKE:
        if (length_offset + field_len + packet_number_len >= static_cast<uint64_t>(len)) {
          return false;
        }
        packet_number = QUICTypeUtil::read_QUICPacketNumber(tmp + length_offset + field_len, packet_number_len);
        key_phase     = QUICKeyPhase::INITIAL;
        break;
      case QUICPacketType::VERSION_NEGOTIATION:
        break;
      default:
        break;
      }
    } else {
      packet_number = 0;
    }
  } else {
    len = std::min(static_cast<int64_t>(25), len);
    reader.memcpy(tmp, len, 0);
    type = QUICPacketType::PROTECTED;
    QUICInvariants::dcid(dcid, tmp, len);
    int packet_number_len = QUICTypeUtil::read_QUICPacketNumberLen(tmp);
    if (tmp[0] & 0x04) {
      key_phase = QUICKeyPhase::PHASE_1;
    } else {
      key_phase = QUICKeyPhase::PHASE_0;
    }
    packet_number = QUICTypeUtil::read_QUICPacketNumber(tmp + 1 + dcid.length(), packet_number_len);
  }
  return true;
}

//
// QUICLongHeaderPacket
//
QUICLongHeaderPacket::QUICLongHeaderPacket(QUICVersion version, const QUICConnectionId &dcid, const QUICConnectionId &scid,
                                           bool ack_eliciting, bool probing, bool crypto)
  : QUICPacket(ack_eliciting, probing), _version(version), _dcid(dcid), _scid(scid), _is_crypto_packet(crypto)
{
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

uint16_t
QUICLongHeaderPacket::payload_length() const
{
  return this->_payload_length;
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
// QUICLongHeaderPacketR
//
QUICLongHeaderPacketR::QUICLongHeaderPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks)
  : QUICPacketR(udp_con, from, to)
{
  IOBufferReader reader;
  uint8_t data[47];

  reader.block     = blocks;
  int64_t data_len = reader.read(data, sizeof(data));

  QUICLongHeaderPacketR::version(this->_version, data, data_len);
}

QUICVersion
QUICLongHeaderPacketR::version() const
{
  return this->_version;
}

QUICConnectionId
QUICLongHeaderPacketR::source_cid() const
{
  return this->_scid;
}

QUICConnectionId
QUICLongHeaderPacketR::destination_cid() const
{
  return this->_dcid;
}

bool
QUICLongHeaderPacketR::type(QUICPacketType &type, const uint8_t *packet, size_t packet_len)
{
  if (packet_len < 1) {
    return false;
  }

  QUICVersion version;
  if (QUICLongHeaderPacketR::version(version, packet, packet_len) && version == 0x00) {
    type = QUICPacketType::VERSION_NEGOTIATION;
  } else {
    uint8_t raw_type = (packet[0] & 0x30) >> 4;
    type             = static_cast<QUICPacketType>(raw_type);
  }
  return true;
}

bool
QUICLongHeaderPacketR::version(QUICVersion &version, const uint8_t *packet, size_t packet_len)
{
  if (packet_len < 5) {
    return false;
  }

  version = QUICTypeUtil::read_QUICVersion(packet + LONG_HDR_OFFSET_VERSION);
  return true;
}

bool
QUICLongHeaderPacketR::key_phase(QUICKeyPhase &phase, const uint8_t *packet, size_t packet_len)
{
  QUICPacketType type = QUICPacketType::UNINITIALIZED;
  QUICLongHeaderPacketR::type(type, packet, packet_len);
  phase = QUICTypeUtil::key_phase(type);
  return true;
}

bool
QUICLongHeaderPacketR::length(size_t &length, uint8_t &length_field_len, size_t &length_field_offset, const uint8_t *packet,
                              size_t packet_len)
{
  // FIXME This is not great because each packet types have different formats.
  // We should remove this function and have length() on each packet type classes instead.

  uint8_t dcil;
  if (!QUICInvariants::dcil(dcil, packet, packet_len)) {
    return false;
  }

  uint8_t scil;
  if (!QUICInvariants::scil(scil, packet, packet_len)) {
    return false;
  }

  length_field_offset = LONG_HDR_OFFSET_CONNECTION_ID + dcil + 1 + scil;

  QUICPacketType type = QUICPacketType::UNINITIALIZED;
  QUICLongHeaderPacketR::type(type, packet, packet_len);
  if (type == QUICPacketType::INITIAL) {
    // Token Length (i) + Token (*) (for INITIAL packet)
    size_t token_length              = 0;
    uint8_t token_length_field_len   = 0;
    size_t token_length_field_offset = 0;
    if (!QUICInitialPacketR::token_length(token_length, token_length_field_len, token_length_field_offset, packet, packet_len)) {
      return false;
    }
    length_field_offset += token_length_field_len + token_length;
  }

  // Length (i)
  if (length_field_offset >= packet_len) {
    return false;
  }

  length_field_len = QUICVariableInt::size(packet + length_field_offset);
  length           = QUICIntUtil::read_QUICVariableInt(packet + length_field_offset, packet_len - length_field_offset);

  return true;
}

bool
QUICLongHeaderPacketR::packet_length(size_t &packet_len, const uint8_t *buf, size_t buf_len)
{
  size_t length;
  uint8_t length_field_len;
  size_t length_field_offset;

  if (!QUICLongHeaderPacketR::length(length, length_field_len, length_field_offset, buf, buf_len)) {
    return false;
  }
  packet_len = length + length_field_offset + length_field_len;

  if (packet_len > buf_len) {
    return false;
  }

  return true;
}

bool
QUICLongHeaderPacketR::packet_number_offset(size_t &pn_offset, const uint8_t *packet, size_t packet_len)
{
  size_t dummy;
  uint8_t length_field_len;
  size_t length_field_offset;

  if (!QUICLongHeaderPacketR::length(dummy, length_field_len, length_field_offset, packet, packet_len)) {
    return false;
  }
  pn_offset = length_field_offset + length_field_len;

  if (pn_offset >= packet_len) {
    return false;
  }

  return true;
}

//
// QUICShortHeaderPacket
//
QUICShortHeaderPacket::QUICShortHeaderPacket(const QUICConnectionId &dcid, QUICPacketNumber packet_number,
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
QUICShortHeaderPacket::payload_length() const
{
  return this->_payload_length;
}

Ptr<IOBufferBlock>
QUICShortHeaderPacket::header_block() const
{
  Ptr<IOBufferBlock> block;
  size_t written_len = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + QUICConnectionId::MAX_LENGTH + 4, BUFFER_SIZE_INDEX_32K));
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
// QUICShortHeaderPacketR
//
QUICShortHeaderPacketR::QUICShortHeaderPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks,
                                               QUICPacketNumber base_packet_number)
  : QUICPacketR(udp_con, from, to)
{
  size_t len = 0;
  for (auto b = blocks; b; b = b->next) {
    len += b->size();
  }

  Ptr<IOBufferBlock> concatenated_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  concatenated_block->alloc(iobuffer_size_to_index(len, BUFFER_SIZE_INDEX_32K));
  concatenated_block->fill(len);

  uint8_t *raw_buf = reinterpret_cast<uint8_t *>(concatenated_block->start());

  size_t copied_len = 0;
  for (auto b = blocks; b; b = b->next) {
    memcpy(raw_buf + copied_len, b->start(), b->size());
    copied_len += b->size();
  }

  if (raw_buf[0] & 0x04) {
    this->_key_phase = QUICKeyPhase::PHASE_1;
  } else {
    this->_key_phase = QUICKeyPhase::PHASE_0;
  }

  QUICInvariants::dcid(this->_dcid, raw_buf, len);

  int offset               = 1 + this->_dcid.length();
  this->_packet_number_len = QUICTypeUtil::read_QUICPacketNumberLen(raw_buf);
  QUICPacketNumber src     = QUICTypeUtil::read_QUICPacketNumber(raw_buf + offset, this->_packet_number_len);
  QUICPacket::decode_packet_number(this->_packet_number, src, this->_packet_number_len, base_packet_number);
  offset += this->_packet_number_len;

  this->_header_block          = concatenated_block->clone();
  this->_header_block->_end    = this->_header_block->_start + offset;
  this->_header_block->next    = nullptr;
  this->_payload_block         = concatenated_block->clone();
  this->_payload_block->_start = this->_payload_block->_start + offset;
}

QUICPacketType
QUICShortHeaderPacketR::type() const
{
  return QUICPacketType::PROTECTED;
}

QUICKeyPhase
QUICShortHeaderPacketR::key_phase() const
{
  return this->_key_phase;
}

QUICPacketNumber
QUICShortHeaderPacketR::packet_number() const
{
  return this->_packet_number;
}

QUICConnectionId
QUICShortHeaderPacketR::destination_cid() const
{
  return this->_dcid;
}

Ptr<IOBufferBlock>
QUICShortHeaderPacketR::header_block() const
{
  return this->_header_block;
}

Ptr<IOBufferBlock>
QUICShortHeaderPacketR::payload_block() const
{
  return this->_payload_block;
}

void
QUICShortHeaderPacketR::attach_payload(Ptr<IOBufferBlock> payload, bool unprotected)
{
  this->_payload_block = payload;
}

bool
QUICShortHeaderPacketR::packet_number_offset(size_t &pn_offset, const uint8_t *packet, size_t packet_len, int dcil)
{
  pn_offset = 1 + dcil;
  return true;
}

//
// QUICStatelessResetPacket
//
QUICStatelessResetPacket::QUICStatelessResetPacket(QUICStatelessResetToken token, size_t maximum_size)
  : QUICPacket(), _token(token), _maximum_size(maximum_size)
{
}

QUICPacketType
QUICStatelessResetPacket::type() const
{
  return QUICPacketType::STATELESS_RESET;
}

QUICConnectionId
QUICStatelessResetPacket::destination_cid() const
{
  ink_assert(!"You should not need DCID of Stateless Reset Packet");
  return QUICConnectionId::ZERO();
}

Ptr<IOBufferBlock>
QUICStatelessResetPacket::header_block() const
{
  // Required shortest length is 38 bits however less than 41 bytes in total indicates this is stateless reset.
  constexpr uint8_t MIN_UNPREDICTABLE_FIELD_LEN = 5 + 20;

  std::random_device rnd;

  Ptr<IOBufferBlock> block;
  size_t written_len = 0;

  size_t random_extra_length = rnd() & 0x07; // Extra 0 to 7 bytes

  if (MIN_UNPREDICTABLE_FIELD_LEN + random_extra_length > this->_maximum_size) {
    return block;
  }

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(MIN_UNPREDICTABLE_FIELD_LEN + random_extra_length, BUFFER_SIZE_INDEX_32K));
  uint8_t *buf = reinterpret_cast<uint8_t *>(block->start());

  // Generate random octets
  for (int i = 0; i < MIN_UNPREDICTABLE_FIELD_LEN; ++i) {
    buf[i] = static_cast<uint8_t>(rnd() & 0xFF);
  }
  buf[0] = (buf[0] | 0x40) & 0x7f;
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
  block->alloc(iobuffer_size_to_index(QUICStatelessResetToken::LEN, BUFFER_SIZE_INDEX_32K));
  uint8_t *buf = reinterpret_cast<uint8_t *>(block->start());

  memcpy(buf, this->_token.buf(), QUICStatelessResetToken::LEN);
  written_len += QUICStatelessResetToken::LEN;

  block->fill(written_len);

  return block;
}

QUICPacketNumber
QUICStatelessResetPacket::packet_number() const
{
  ink_assert(!"You should not need packet number of Stateless Reset Packet");
  return 0;
}

QUICStatelessResetToken
QUICStatelessResetPacket::token() const
{
  return this->_token;
}

//
// QUICStatelessResetPacketR
//
QUICStatelessResetPacketR::QUICStatelessResetPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to,
                                                     Ptr<IOBufferBlock> blocks)
  : QUICPacketR(udp_con, from, to)
{
}

QUICPacketType
QUICStatelessResetPacketR::type() const
{
  return QUICPacketType::STATELESS_RESET;
}

QUICPacketNumber
QUICStatelessResetPacketR::packet_number() const
{
  ink_assert(!"You should not need packet number of Stateless Reset Packet");
  return 0;
}

QUICConnectionId
QUICStatelessResetPacketR::destination_cid() const
{
  ink_assert(!"You should not need DCID of Stateless Reset Packet");
  return QUICConnectionId::ZERO();
}

//
// QUICVersionNegotiationPacket
//

QUICVersionNegotiationPacket::QUICVersionNegotiationPacket(const QUICConnectionId &dcid, const QUICConnectionId &scid,
                                                           const QUICVersion versions[], int nversions,
                                                           QUICVersion version_in_initial)
  : QUICLongHeaderPacket(0, dcid, scid, false, false, false),
    _versions(versions),
    _nversions(nversions),
    _version_in_initial(version_in_initial)
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

QUICPacketNumber
QUICVersionNegotiationPacket::packet_number() const
{
  ink_assert(!"You should not need packet number of Version Negotiation Packet");
  return 0;
}

uint16_t
QUICVersionNegotiationPacket::payload_length() const
{
  uint16_t size = 0;

  for (auto b = this->payload_block(); b; b = b->next) {
    size += b->size();
  }

  return size;
}

Ptr<IOBufferBlock>
QUICVersionNegotiationPacket::header_block() const
{
  Ptr<IOBufferBlock> block;
  size_t written_len = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(2048, BUFFER_SIZE_INDEX_32K));
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
  block->alloc(iobuffer_size_to_index(sizeof(QUICVersion) * (this->_nversions + 1), BUFFER_SIZE_INDEX_32K));
  buf = reinterpret_cast<uint8_t *>(block->start());

  for (auto i = 0; i < this->_nversions; ++i) {
    QUICTypeUtil::write_QUICVersion(*(this->_versions + i), buf + written_len, &n);
    written_len += n;
  }

  // [draft-18] 6.3. Using Reserved Versions
  // To help ensure this, a server SHOULD include a reserved version (see Section 15) while generating a
  // Version Negotiation packet.
  QUICVersion exersice_version = QUIC_EXERCISE_VERSION1;
  if (this->_version_in_initial == QUIC_EXERCISE_VERSION1) {
    exersice_version = QUIC_EXERCISE_VERSION2;
  }
  QUICTypeUtil::write_QUICVersion(exersice_version, buf + written_len, &n);
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
  return this->_nversions;
}

//
// QUICVersionNegotiationPacketR
//
QUICVersionNegotiationPacketR::QUICVersionNegotiationPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to,
                                                             Ptr<IOBufferBlock> blocks)
  : QUICLongHeaderPacketR(udp_con, from, to, blocks)
{
  size_t len = 0;
  for (auto b = blocks; b; b = b->next) {
    len += b->size();
  }

  Ptr<IOBufferBlock> concatenated_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  concatenated_block->alloc(iobuffer_size_to_index(len, BUFFER_SIZE_INDEX_32K));
  concatenated_block->fill(len);

  uint8_t *raw_buf = reinterpret_cast<uint8_t *>(concatenated_block->start());

  size_t copied_len = 0;
  for (auto b = blocks; b; b = b->next) {
    memcpy(raw_buf + copied_len, b->start(), b->size());
    copied_len += b->size();
  }

  uint8_t dcil = 0;
  uint8_t scil = 0;
  QUICInvariants::dcil(dcil, raw_buf, len);
  QUICInvariants::scil(scil, raw_buf, len);

  size_t offset = LONG_HDR_OFFSET_CONNECTION_ID;
  this->_dcid   = {raw_buf + offset, dcil};
  offset += dcil + 1;
  this->_scid = {raw_buf + offset, scil};
  offset += scil;

  this->_versions  = raw_buf + offset;
  this->_nversions = (len - offset) / sizeof(QUICVersion);

  this->_header_block          = concatenated_block->clone();
  this->_header_block->_end    = this->_header_block->_start + offset;
  this->_header_block->next    = nullptr;
  this->_payload_block         = concatenated_block->clone();
  this->_payload_block->_start = this->_payload_block->_start + offset;
}

QUICPacketType
QUICVersionNegotiationPacketR::type() const
{
  return QUICPacketType::VERSION_NEGOTIATION;
}

QUICPacketNumber
QUICVersionNegotiationPacketR::packet_number() const
{
  ink_assert(!"You should not need packet number of Version Negotiation Packet");
  return 0;
}

QUICConnectionId
QUICVersionNegotiationPacketR::destination_cid() const
{
  return this->_dcid;
}

Ptr<IOBufferBlock>
QUICVersionNegotiationPacketR::header_block() const
{
  return this->_header_block;
}

Ptr<IOBufferBlock>
QUICVersionNegotiationPacketR::payload_block() const
{
  return this->_payload_block;
}

const QUICVersion
QUICVersionNegotiationPacketR::supported_version(uint8_t index) const
{
  return QUICTypeUtil::read_QUICVersion(this->_versions + sizeof(QUICVersion) * index);
}

int
QUICVersionNegotiationPacketR::nversions() const
{
  return this->_nversions;
}

//
// QUICInitialPacket
//
QUICInitialPacket::QUICInitialPacket(QUICVersion version, const QUICConnectionId &dcid, const QUICConnectionId &scid,
                                     size_t token_len, ats_unique_buf token, size_t length, QUICPacketNumber packet_number,
                                     bool ack_eliciting, bool probing, bool crypto)
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
  block->alloc(iobuffer_size_to_index(2048, BUFFER_SIZE_INDEX_32K));
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
// QUICInitialPacketR
//
QUICInitialPacketR::QUICInitialPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks,
                                       QUICPacketNumber base_packet_number)
  : QUICLongHeaderPacketR(udp_con, from, to, blocks)
{
  size_t len = 0;
  for (auto b = blocks; b; b = b->next) {
    len += b->size();
  }

  Ptr<IOBufferBlock> concatenated_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  concatenated_block->alloc(iobuffer_size_to_index(len, BUFFER_SIZE_INDEX_32K));
  concatenated_block->fill(len);

  uint8_t *raw_buf = reinterpret_cast<uint8_t *>(concatenated_block->start());

  size_t copied_len = 0;
  for (auto b = blocks; b; b = b->next) {
    memcpy(raw_buf + copied_len, b->start(), b->size());
    copied_len += b->size();
  }

  uint8_t dcil = 0;
  uint8_t scil = 0;
  QUICInvariants::dcil(dcil, raw_buf, len);
  QUICInvariants::scil(scil, raw_buf, len);

  size_t offset = LONG_HDR_OFFSET_CONNECTION_ID;
  this->_dcid   = {raw_buf + offset, dcil};
  offset += dcil + 1;
  this->_scid = {raw_buf + offset, scil};
  offset += scil;

  // Token Length Field
  uint64_t token_len = QUICIntUtil::read_QUICVariableInt(raw_buf + offset, len - offset);
  offset += QUICVariableInt::size(raw_buf + offset);

  // Token Field
  if (token_len) {
    this->_token = new QUICAddressValidationToken(raw_buf + offset, token_len);
    offset += token_len;
  } else {
    this->_token = new QUICAddressValidationToken(nullptr, 0);
  }

  // Length Field
  offset += QUICVariableInt::size(raw_buf + offset);

  // PN Field
  int pn_len = QUICTypeUtil::read_QUICPacketNumberLen(raw_buf);
  QUICPacket::decode_packet_number(this->_packet_number, QUICTypeUtil::read_QUICPacketNumber(raw_buf + offset, pn_len), pn_len,
                                   base_packet_number);
  offset += pn_len;

  this->_header_block          = concatenated_block->clone();
  this->_header_block->_end    = this->_header_block->_start + offset;
  this->_header_block->next    = nullptr;
  this->_payload_block         = concatenated_block->clone();
  this->_payload_block->_start = this->_payload_block->_start + offset;
}

QUICInitialPacketR::~QUICInitialPacketR()
{
  delete this->_token;
}

QUICPacketType
QUICInitialPacketR::type() const
{
  return QUICPacketType::INITIAL;
}

QUICPacketNumber
QUICInitialPacketR::packet_number() const
{
  return this->_packet_number;
}

QUICKeyPhase
QUICInitialPacketR::key_phase() const
{
  return QUICKeyPhase::INITIAL;
}

Ptr<IOBufferBlock>
QUICInitialPacketR::header_block() const
{
  return this->_header_block;
}

Ptr<IOBufferBlock>
QUICInitialPacketR::payload_block() const
{
  return this->_payload_block;
}

void
QUICInitialPacketR::attach_payload(Ptr<IOBufferBlock> payload, bool unprotected)
{
  this->_payload_block = payload;
}

const QUICAddressValidationToken &
QUICInitialPacketR::token() const
{
  return *(this->_token);
}

bool
QUICInitialPacketR::token_length(size_t &token_length, uint8_t &field_len, size_t &token_length_filed_offset, const uint8_t *packet,
                                 size_t packet_len)
{
  QUICPacketType type = QUICPacketType::UNINITIALIZED;
  QUICPacketR::type(type, packet, packet_len);

  ink_assert(type == QUICPacketType::INITIAL);

  uint8_t dcil, scil;
  QUICInvariants::dcil(dcil, packet, packet_len);
  QUICInvariants::scil(scil, packet, packet_len);

  token_length_filed_offset = LONG_HDR_OFFSET_CONNECTION_ID + dcil + 1 + scil;
  if (token_length_filed_offset >= packet_len) {
    return false;
  }

  token_length = QUICIntUtil::read_QUICVariableInt(packet + token_length_filed_offset, packet_len - token_length_filed_offset);
  field_len    = QUICVariableInt::size(packet + token_length_filed_offset);

  return true;
}

//
// QUICZeroRttPacket
//
QUICZeroRttPacket::QUICZeroRttPacket(QUICVersion version, const QUICConnectionId &dcid, const QUICConnectionId &scid, size_t length,
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
  block->alloc(iobuffer_size_to_index(2048, BUFFER_SIZE_INDEX_32K));
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
// QUICZeroRttPacketR
//
QUICZeroRttPacketR::QUICZeroRttPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks,
                                       QUICPacketNumber base_packet_number)
  : QUICLongHeaderPacketR(udp_con, from, to, blocks)
{
  size_t len = 0;
  for (auto b = blocks; b; b = b->next) {
    len += b->size();
  }

  Ptr<IOBufferBlock> concatenated_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  concatenated_block->alloc(iobuffer_size_to_index(len, BUFFER_SIZE_INDEX_32K));
  concatenated_block->fill(len);

  uint8_t *raw_buf = reinterpret_cast<uint8_t *>(concatenated_block->start());

  size_t copied_len = 0;
  for (auto b = blocks; b; b = b->next) {
    memcpy(raw_buf + copied_len, b->start(), b->size());
    copied_len += b->size();
  }

  uint8_t dcil = 0;
  uint8_t scil = 0;
  QUICInvariants::dcil(dcil, raw_buf, len);
  QUICInvariants::scil(scil, raw_buf, len);

  size_t offset = LONG_HDR_OFFSET_CONNECTION_ID;
  this->_dcid   = {raw_buf + offset, dcil};
  offset += dcil + 1;
  this->_scid = {raw_buf + offset, scil};
  offset += scil;

  // Length Field
  offset += QUICVariableInt::size(raw_buf + offset);

  // PN Field
  int pn_len = QUICTypeUtil::read_QUICPacketNumberLen(raw_buf);
  QUICPacket::decode_packet_number(this->_packet_number, QUICTypeUtil::read_QUICPacketNumber(raw_buf + offset, pn_len), pn_len,
                                   base_packet_number);
  offset += pn_len;

  this->_header_block          = concatenated_block->clone();
  this->_header_block->_end    = this->_header_block->_start + offset;
  this->_header_block->next    = nullptr;
  this->_payload_block         = concatenated_block->clone();
  this->_payload_block->_start = this->_payload_block->_start + offset;
}

QUICPacketType
QUICZeroRttPacketR::type() const
{
  return QUICPacketType::ZERO_RTT_PROTECTED;
}

QUICPacketNumber
QUICZeroRttPacketR::packet_number() const
{
  return this->_packet_number;
}

QUICKeyPhase
QUICZeroRttPacketR::key_phase() const
{
  return QUICKeyPhase::ZERO_RTT;
}

Ptr<IOBufferBlock>
QUICZeroRttPacketR::header_block() const
{
  return this->_header_block;
}

Ptr<IOBufferBlock>
QUICZeroRttPacketR::payload_block() const
{
  return this->_payload_block;
}

void
QUICZeroRttPacketR::attach_payload(Ptr<IOBufferBlock> payload, bool unprotected)
{
  this->_payload_block = payload;
}

//
// QUICHandshakePacket
//
QUICHandshakePacket::QUICHandshakePacket(QUICVersion version, const QUICConnectionId &dcid, const QUICConnectionId &scid,
                                         size_t length, QUICPacketNumber packet_number, bool ack_eliciting, bool probing,
                                         bool crypto)
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
  block->alloc(iobuffer_size_to_index(2048, BUFFER_SIZE_INDEX_32K));
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
// QUICHandshakePacketR
//
QUICHandshakePacketR::QUICHandshakePacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks,
                                           QUICPacketNumber base_packet_number)
  : QUICLongHeaderPacketR(udp_con, from, to, blocks)
{
  size_t len = 0;
  for (auto b = blocks; b; b = b->next) {
    len += b->size();
  }

  Ptr<IOBufferBlock> concatenated_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  concatenated_block->alloc(iobuffer_size_to_index(len, BUFFER_SIZE_INDEX_32K));
  concatenated_block->fill(len);

  uint8_t *raw_buf = reinterpret_cast<uint8_t *>(concatenated_block->start());

  size_t copied_len = 0;
  for (auto b = blocks; b; b = b->next) {
    memcpy(raw_buf + copied_len, b->start(), b->size());
    copied_len += b->size();
  }

  uint8_t dcil = 0;
  uint8_t scil = 0;
  QUICInvariants::dcil(dcil, raw_buf, len);
  QUICInvariants::scil(scil, raw_buf, len);

  size_t offset = LONG_HDR_OFFSET_CONNECTION_ID;
  this->_dcid   = {raw_buf + offset, dcil};
  offset += dcil + 1;
  this->_scid = {raw_buf + offset, scil};
  offset += scil;

  // Length Field
  offset += QUICVariableInt::size(raw_buf + offset);

  // PN Field
  int pn_len = QUICTypeUtil::read_QUICPacketNumberLen(raw_buf);
  QUICPacket::decode_packet_number(this->_packet_number, QUICTypeUtil::read_QUICPacketNumber(raw_buf + offset, pn_len), pn_len,
                                   base_packet_number);
  offset += pn_len;

  this->_header_block          = concatenated_block->clone();
  this->_header_block->_end    = this->_header_block->_start + offset;
  this->_header_block->next    = nullptr;
  this->_payload_block         = concatenated_block->clone();
  this->_payload_block->_start = this->_payload_block->_start + offset;
}

QUICPacketType
QUICHandshakePacketR::type() const
{
  return QUICPacketType::HANDSHAKE;
}

QUICKeyPhase
QUICHandshakePacketR::key_phase() const
{
  return QUICKeyPhase::HANDSHAKE;
}

QUICPacketNumber
QUICHandshakePacketR::packet_number() const
{
  return this->_packet_number;
}

Ptr<IOBufferBlock>
QUICHandshakePacketR::header_block() const
{
  return this->_header_block;
}

Ptr<IOBufferBlock>
QUICHandshakePacketR::payload_block() const
{
  return this->_payload_block;
}

void
QUICHandshakePacketR::attach_payload(Ptr<IOBufferBlock> payload, bool unprotected)
{
  this->_payload_block = payload;
}

//
// QUICRetryPacket
//
QUICRetryPacket::QUICRetryPacket(QUICVersion version, const QUICConnectionId &dcid, const QUICConnectionId &scid,
                                 QUICRetryToken &token)
  : QUICLongHeaderPacket(version, dcid, scid, false, false, false), _token(token)
{
}

QUICPacketType
QUICRetryPacket::type() const
{
  return QUICPacketType::RETRY;
}

QUICPacketNumber
QUICRetryPacket::packet_number() const
{
  ink_assert(!"You should not need packet number of Retry Packet");
  return 0;
}

uint16_t
QUICRetryPacket::payload_length() const
{
  uint16_t size = 0;

  for (auto b = this->payload_block(); b; b = b->next) {
    size += b->size();
  }

  return size;
}

Ptr<IOBufferBlock>
QUICRetryPacket::header_block() const
{
  Ptr<IOBufferBlock> block;
  size_t written_len = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(2048, BUFFER_SIZE_INDEX_32K));
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

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(QUICConnectionId::MAX_LENGTH + this->_token.length() + QUICRetryIntegrityTag::LEN,
                                      BUFFER_SIZE_INDEX_32K));
  buf = reinterpret_cast<uint8_t *>(block->start());

  // Retry Token
  memcpy(buf + written_len, this->_token.buf(), this->_token.length());
  written_len += this->_token.length();
  block->fill(written_len);

  // Retry Integrity Tag
  QUICRetryIntegrityTag::compute(buf + written_len, this->version(), this->_token.original_dcid(), this->header_block(), block);
  written_len += QUICRetryIntegrityTag::LEN;
  block->fill(QUICRetryIntegrityTag::LEN);

  return block;
}

const QUICRetryToken &
QUICRetryPacket::token() const
{
  return this->_token;
}

//
// QUICRetryPacketR
//
QUICRetryPacketR::QUICRetryPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks)
  : QUICLongHeaderPacketR(udp_con, from, to, blocks)
{
  size_t len = 0;
  for (auto b = blocks; b; b = b->next) {
    len += b->size();
  }

  Ptr<IOBufferBlock> concatenated_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  concatenated_block->alloc(iobuffer_size_to_index(len, BUFFER_SIZE_INDEX_32K));
  concatenated_block->fill(len);

  uint8_t *raw_buf = reinterpret_cast<uint8_t *>(concatenated_block->start());

  size_t copied_len = 0;
  for (auto b = blocks; b; b = b->next) {
    memcpy(raw_buf + copied_len, b->start(), b->size());
    copied_len += b->size();
  }

  uint8_t dcil = 0;
  uint8_t scil = 0;
  QUICInvariants::dcil(dcil, raw_buf, len);
  QUICInvariants::scil(scil, raw_buf, len);

  size_t offset = LONG_HDR_OFFSET_CONNECTION_ID;
  this->_dcid   = {raw_buf + offset, dcil};
  offset += dcil + 1;
  this->_scid = {raw_buf + offset, scil};
  offset += scil;

  // Retry Token field
  this->_token = new QUICRetryToken(raw_buf + offset, len - offset - QUICRetryIntegrityTag::LEN);
  offset += this->_token->length();

  // Retry Integrity Tag
  memcpy(this->_integrity_tag, raw_buf + offset, QUICRetryIntegrityTag::LEN);

  this->_header_block                    = concatenated_block->clone();
  this->_header_block->_end              = this->_header_block->_start + offset;
  this->_header_block->next              = nullptr;
  this->_payload_block                   = concatenated_block->clone();
  this->_payload_block->_start           = this->_payload_block->_start + offset;
  this->_payload_block_without_tag       = this->_payload_block->clone();
  this->_payload_block_without_tag->_end = this->_payload_block_without_tag->_end - QUICRetryIntegrityTag::LEN;
}

QUICRetryPacketR::~QUICRetryPacketR()
{
  delete this->_token;
}

Ptr<IOBufferBlock>
QUICRetryPacketR::header_block() const
{
  return this->_header_block;
}

Ptr<IOBufferBlock>
QUICRetryPacketR::payload_block() const
{
  return this->_payload_block;
}

QUICPacketType
QUICRetryPacketR::type() const
{
  return QUICPacketType::RETRY;
}

QUICPacketNumber
QUICRetryPacketR::packet_number() const
{
  return 0;
}

const QUICAddressValidationToken &
QUICRetryPacketR::token() const
{
  return *(this->_token);
}

bool
QUICRetryPacketR::has_valid_tag(const QUICConnectionId &odcid) const
{
  uint8_t tag_computed[QUICRetryIntegrityTag::LEN];
  QUICRetryIntegrityTag::compute(tag_computed, this->version(), odcid, this->_header_block, this->_payload_block_without_tag);

  return memcmp(this->_integrity_tag, tag_computed, QUICRetryIntegrityTag::LEN) == 0;
}
