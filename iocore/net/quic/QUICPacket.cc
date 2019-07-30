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
    if (this->_buf_len > MAX_PACKET_HEADER_LEN) {
      ink_assert(!"Serialized packet header is too long");
    }

    this->_buf_len += this->_payload_length;
    return this->_serialized;
  }
}

const IpEndpoint &
QUICPacketHeader::from() const
{
  return this->_from;
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
QUICPacketHeader::build(QUICPacketType type, QUICKeyPhase key_phase, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                        QUICPacketNumber packet_number, QUICPacketNumber base_packet_number, QUICVersion version, bool crypto,
                        ats_unique_buf payload, size_t len)
{
  QUICPacketLongHeader *long_header = quicPacketLongHeaderAllocator.alloc();
  new (long_header) QUICPacketLongHeader(type, key_phase, destination_cid, source_cid, packet_number, base_packet_number, version,
                                         crypto, std::move(payload), len);
  return QUICPacketHeaderUPtr(long_header, &QUICPacketHeaderDeleter::delete_long_header);
}

QUICPacketHeaderUPtr
QUICPacketHeader::build(QUICPacketType type, QUICKeyPhase key_phase, QUICConnectionId destination_cid, QUICConnectionId source_cid,
                        QUICPacketNumber packet_number, QUICPacketNumber base_packet_number, QUICVersion version, bool crypto,
                        ats_unique_buf payload, size_t len, ats_unique_buf token, size_t token_len)
{
  QUICPacketLongHeader *long_header = quicPacketLongHeaderAllocator.alloc();
  new (long_header) QUICPacketLongHeader(type, key_phase, destination_cid, source_cid, packet_number, base_packet_number, version,
                                         crypto, std::move(payload), len, std::move(token), token_len);
  return QUICPacketHeaderUPtr(long_header, &QUICPacketHeaderDeleter::delete_long_header);
}

QUICPacketHeaderUPtr
QUICPacketHeader::build(QUICPacketType type, QUICKeyPhase key_phase, QUICVersion version, QUICConnectionId destination_cid,
                        QUICConnectionId source_cid, QUICConnectionId original_dcid, ats_unique_buf retry_token,
                        size_t retry_token_len)
{
  QUICPacketLongHeader *long_header = quicPacketLongHeaderAllocator.alloc();
  new (long_header) QUICPacketLongHeader(type, key_phase, version, destination_cid, source_cid, original_dcid,
                                         std::move(retry_token), retry_token_len);
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
  this->_key_phase = QUICTypeUtil::key_phase(this->type());
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

  if (this->type() != QUICPacketType::VERSION_NEGOTIATION) {
    if (this->type() == QUICPacketType::RETRY) {
      uint8_t odcil        = (raw_buf[0] & 0x0f) + 3;
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

QUICPacketLongHeader::QUICPacketLongHeader(QUICPacketType type, QUICKeyPhase key_phase, const QUICConnectionId &destination_cid,
                                           const QUICConnectionId &source_cid, QUICPacketNumber packet_number,
                                           QUICPacketNumber base_packet_number, QUICVersion version, bool crypto,
                                           ats_unique_buf buf, size_t len, ats_unique_buf token, size_t token_len)
  : QUICPacketHeader(type, packet_number, base_packet_number, true, version, std::move(buf), len, key_phase),
    _destination_cid(destination_cid),
    _source_cid(source_cid),
    _token_len(token_len),
    _token(std::move(token)),
    _is_crypto_packet(crypto)
{
  if (this->_type == QUICPacketType::VERSION_NEGOTIATION) {
    this->_buf_len =
      LONG_HDR_OFFSET_CONNECTION_ID + this->_destination_cid.length() + this->_source_cid.length() + this->_payload_length;
  } else {
    this->buf();
  }
}

QUICPacketLongHeader::QUICPacketLongHeader(QUICPacketType type, QUICKeyPhase key_phase, QUICVersion version,
                                           const QUICConnectionId &destination_cid, const QUICConnectionId &source_cid,
                                           const QUICConnectionId &original_dcid, ats_unique_buf retry_token,
                                           size_t retry_token_len)
  : QUICPacketHeader(type, 0, 0, true, version, std::move(retry_token), retry_token_len, key_phase),
    _destination_cid(destination_cid),
    _source_cid(source_cid),
    _original_dcid(original_dcid)

{
  // this->_buf_len will be set
  this->buf();
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
    if (dcil != 0) {
      dcil += 3;
    }
    return true;
  } else {
    return false;
  }
}

bool
QUICPacketLongHeader::scil(uint8_t &scil, const uint8_t *packet, size_t packet_len)
{
  if (QUICInvariants::scil(scil, packet, packet_len)) {
    if (scil != 0) {
      scil += 3;
    }
    return true;
  } else {
    return false;
  }
}

bool
QUICPacketLongHeader::token_length(size_t &token_length, uint8_t *field_len, const uint8_t *packet, size_t packet_len)
{
  QUICPacketType type = QUICPacketType::UNINITIALIZED;
  QUICPacketLongHeader::type(type, packet, packet_len);

  if (type != QUICPacketType::INITIAL) {
    token_length = 0;
    if (field_len) {
      *field_len = 0;
    }
    return true;
  }

  uint8_t dcil, scil;
  QUICPacketLongHeader::dcil(dcil, packet, packet_len);
  QUICPacketLongHeader::scil(scil, packet, packet_len);

  size_t offset = LONG_HDR_OFFSET_CONNECTION_ID + dcil + scil;
  if (offset >= packet_len) {
    return false;
  }

  if (offset > packet_len) {
    return false;
  }

  token_length = QUICIntUtil::read_QUICVariableInt(packet + offset);
  if (field_len) {
    *field_len = QUICVariableInt::size(packet + offset);
  }

  return true;
}

bool
QUICPacketLongHeader::length(size_t &length, uint8_t *field_len, const uint8_t *packet, size_t packet_len)
{
  uint8_t dcil, scil;
  QUICPacketLongHeader::dcil(dcil, packet, packet_len);
  QUICPacketLongHeader::scil(scil, packet, packet_len);

  // Token Length (i) + Token (*) (for INITIAL packet)
  size_t token_length            = 0;
  uint8_t token_length_field_len = 0;
  if (!QUICPacketLongHeader::token_length(token_length, &token_length_field_len, packet, packet_len)) {
    return false;
  }

  // Length (i)
  size_t length_offset = LONG_HDR_OFFSET_CONNECTION_ID + dcil + scil + token_length_field_len + token_length;
  if (length_offset >= packet_len) {
    return false;
  }
  length = QUICIntUtil::read_QUICVariableInt(packet + length_offset);
  if (field_len) {
    *field_len = QUICVariableInt::size(packet + length_offset);
  }
  return true;
}

bool
QUICPacketLongHeader::packet_number_offset(uint8_t &pn_offset, const uint8_t *packet, size_t packet_len)
{
  QUICPacketType type;
  QUICPacketLongHeader::type(type, packet, packet_len);

  uint8_t dcil, scil;
  size_t token_length;
  uint8_t token_length_field_len;
  size_t length;
  uint8_t length_field_len;
  if (!QUICPacketLongHeader::dcil(dcil, packet, packet_len) || !QUICPacketLongHeader::scil(scil, packet, packet_len) ||
      !QUICPacketLongHeader::token_length(token_length, &token_length_field_len, packet, packet_len) ||
      !QUICPacketLongHeader::length(length, &length_field_len, packet, packet_len)) {
    return false;
  }
  pn_offset = 6 + dcil + scil + token_length_field_len + token_length + length_field_len;

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

void
QUICPacketLongHeader::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  *len   = 0;
  buf[0] = 0xC0;
  buf[0] += static_cast<uint8_t>(this->_type) << 4;
  if (this->_type == QUICPacketType::VERSION_NEGOTIATION) {
    buf[0] |= rand();
  }
  *len += 1;

  QUICTypeUtil::write_QUICVersion(this->_version, buf + *len, &n);
  *len += n;

  buf[*len] = this->_destination_cid == QUICConnectionId::ZERO() ? 0 : (this->_destination_cid.length() - 3) << 4;
  buf[*len] += this->_source_cid == QUICConnectionId::ZERO() ? 0 : this->_source_cid.length() - 3;
  *len += 1;

  if (this->_destination_cid != QUICConnectionId::ZERO()) {
    QUICTypeUtil::write_QUICConnectionId(this->_destination_cid, buf + *len, &n);
    *len += n;
  }
  if (this->_source_cid != QUICConnectionId::ZERO()) {
    QUICTypeUtil::write_QUICConnectionId(this->_source_cid, buf + *len, &n);
    *len += n;
  }

  if (this->_type != QUICPacketType::VERSION_NEGOTIATION) {
    if (this->_type == QUICPacketType::RETRY) {
      // Original Destination Connection ID
      if (this->_original_dcid != QUICConnectionId::ZERO()) {
        QUICTypeUtil::write_QUICConnectionId(this->_original_dcid, buf + *len, &n);
        *len += n;
      }
      // ODCIL
      buf[0] |= this->_original_dcid.length() - 3;
    } else {
      if (this->_type == QUICPacketType::INITIAL) {
        // Token Length Field
        QUICIntUtil::write_QUICVariableInt(this->_token_len, buf + *len, &n);
        *len += n;

        // Token Field
        memcpy(buf + *len, this->token(), this->token_len());
        *len += this->token_len();
      }

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

      if (this->_type != QUICPacketType::RETRY) {
        // PN Len field
        QUICTypeUtil::write_QUICPacketNumberLen(pn_len, buf);
      }

      // Length Field
      QUICIntUtil::write_QUICVariableInt(pn_len + this->_payload_length + aead_tag_len, buf + *len, &n);
      *len += n;

      // PN Field
      QUICTypeUtil::write_QUICPacketNumber(pn, pn_len, buf + *len, &n);
      *len += n;
    }

    // Payload will be stored
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
  this->_packet_number_len = QUICTypeUtil::read_QUICPacketNumberLen(this->_buf.get());
  QUICPacketNumber src     = QUICTypeUtil::read_QUICPacketNumber(this->_buf.get() + offset, this->_packet_number_len);
  QUICPacket::decode_packet_number(this->_packet_number, src, this->_packet_number_len, this->_base_packet_number);
  this->_payload_length = len - (1 + QUICConnectionId::SCID_LEN + this->_packet_number_len);
}

QUICPacketShortHeader::QUICPacketShortHeader(QUICPacketType type, QUICKeyPhase key_phase, QUICPacketNumber packet_number,
                                             QUICPacketNumber base_packet_number, ats_unique_buf buf, size_t len)
{
  this->_type               = type;
  this->_key_phase          = key_phase;
  this->_packet_number      = packet_number;
  this->_base_packet_number = base_packet_number;
  this->_packet_number_len  = QUICPacket::calc_packet_number_len(packet_number, base_packet_number);
  this->_payload            = std::move(buf);
  this->_payload_length     = len;
}

QUICPacketShortHeader::QUICPacketShortHeader(QUICPacketType type, QUICKeyPhase key_phase, const QUICConnectionId &connection_id,
                                             QUICPacketNumber packet_number, QUICPacketNumber base_packet_number,
                                             ats_unique_buf buf, size_t len)
{
  this->_type               = type;
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
QUICPacketShortHeader::packet_number_offset(uint8_t &pn_offset, const uint8_t *packet, size_t packet_len, int dcil)
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

void
QUICPacketShortHeader::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  *len   = 0;
  buf[0] = 0x40;
  if (this->_key_phase == QUICKeyPhase::PHASE_1) {
    buf[0] |= 0x04;
  }
  *len += 1;

  if (this->_connection_id != QUICConnectionId::ZERO()) {
    QUICTypeUtil::write_QUICConnectionId(this->_connection_id, buf + *len, &n);
    *len += n;
  }

  QUICPacketNumber dst = 0;
  size_t dst_len       = this->_packet_number_len;
  QUICPacket::encode_packet_number(dst, this->_packet_number, dst_len);
  QUICTypeUtil::write_QUICPacketNumber(dst, dst_len, buf + *len, &n);
  *len += n;

  QUICTypeUtil::write_QUICPacketNumberLen(n, buf);
}

//
// QUICPacket
//

QUICPacket::QUICPacket() {}

QUICPacket::QUICPacket(UDPConnection *udp_con, QUICPacketHeaderUPtr header, ats_unique_buf payload, size_t payload_len)
  : _udp_con(udp_con), _header(std::move(header)), _payload(std::move(payload)), _payload_size(payload_len)
{
}

QUICPacket::QUICPacket(QUICPacketHeaderUPtr header, ats_unique_buf payload, size_t payload_len, bool ack_eliciting, bool probing)
  : _header(std::move(header)),
    _payload(std::move(payload)),
    _payload_size(payload_len),
    _is_ack_eliciting(ack_eliciting),
    _is_probing_packet(probing)
{
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
  memcpy(buf, this->_header->buf(), this->_header->size());
  memcpy(buf + this->_header->size(), this->payload(), this->payload_length());
  *len = this->_header->size() + this->payload_length();
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
