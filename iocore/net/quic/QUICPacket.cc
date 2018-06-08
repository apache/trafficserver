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
#include "QUICIntUtil.h"
#include "QUICPacket.h"
#include "QUICDebugNames.h"

static constexpr std::string_view tag = "quic_packet"sv;

#define QUICDebug(dcid, scid, fmt, ...) \
  Debug(tag.data(), "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__);

ClassAllocator<QUICPacket> quicPacketAllocator("quicPacketAllocator");
ClassAllocator<QUICPacketLongHeader> quicPacketLongHeaderAllocator("quicPacketLongHeaderAllocator");
ClassAllocator<QUICPacketShortHeader> quicPacketShortHeaderAllocator("quicPacketShortHeaderAllocator");

static constexpr int LONG_HDR_OFFSET_CONNECTION_ID = 6;
static constexpr int LONG_HDR_OFFSET_VERSION       = 1;

static constexpr int SHORT_HDR_OFFSET_CONNECTION_ID = 1;

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
    size_t dummy;
    this->store(this->_serialized, &dummy);
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
QUICPacketHeader::load(const IpEndpoint from, ats_unique_buf buf, size_t len, QUICPacketNumber base, uint8_t dcil)
{
  QUICPacketHeaderUPtr header = QUICPacketHeaderUPtr(nullptr, &QUICPacketHeaderDeleter::delete_null_header);
  if (QUICTypeUtil::has_long_header(buf.get())) {
    QUICPacketLongHeader *long_header = quicPacketLongHeaderAllocator.alloc();
    new (long_header) QUICPacketLongHeader(from, std::move(buf), len, base);
    header = QUICPacketHeaderUPtr(long_header, &QUICPacketHeaderDeleter::delete_long_header);
  } else {
    QUICPacketShortHeader *short_header = quicPacketShortHeaderAllocator.alloc();
    new (short_header) QUICPacketShortHeader(from, std::move(buf), len, base, dcil);
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

  uint8_t dcil = (raw_buf[5] >> 4);
  if (dcil) {
    dcil += 3;
  }
  uint8_t scil = (raw_buf[5] & 0x0F);
  if (scil) {
    scil += 3;
  }
  size_t offset          = LONG_HDR_OFFSET_CONNECTION_ID;
  this->_destination_cid = {raw_buf + offset, dcil};
  offset += dcil;
  this->_source_cid = {raw_buf + offset, scil};
  offset += scil;

  if (this->type() == QUICPacketType::VERSION_NEGOTIATION) {
    this->_payload_offset = offset;
    this->_payload_length = len - offset;
  } else {
    this->_payload_length = QUICIntUtil::read_QUICVariableInt(raw_buf + offset);
    offset += QUICVariableInt::size(raw_buf + offset);
    int pn_len = QUICTypeUtil::read_QUICPacketNumberLen(raw_buf + offset);
    QUICPacket::decode_packet_number(this->_packet_number, QUICTypeUtil::read_QUICPacketNumber(raw_buf + offset), pn_len,
                                     this->_base_packet_number);
    this->_payload_offset = offset + pn_len;
  }
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
    this->_buf_len = LONG_HDR_OFFSET_CONNECTION_ID + this->_destination_cid.length() + this->_source_cid.length() +
                     QUICVariableInt::size(this->_payload_length) + 4 + this->_payload_length;
  }
}

QUICPacketType
QUICPacketLongHeader::type() const
{
  if (this->_buf) {
    uint8_t type = this->_buf.get()[0] & 0x7F;
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
    return QUICTypeUtil::read_QUICVersion(this->_buf.get() + LONG_HDR_OFFSET_VERSION);
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
  // TODO LongHeader will also be used for 0-RTT packets
  return QUICKeyPhase::CLEARTEXT;
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
    QUICIntUtil::write_QUICVariableInt(this->_payload_length + 16, buf + *len, &n);
    *len += n;

    QUICPacketNumber dst = 0;
    size_t dst_len       = 4;
    QUICPacket::encode_packet_number(dst, this->_packet_number, dst_len);
    QUICTypeUtil::write_QUICPacketNumber(dst, dst_len, buf + *len, &n);
    *len += n;
  }
}

//
// QUICPacketShortHeader
//

QUICPacketShortHeader::QUICPacketShortHeader(const IpEndpoint from, ats_unique_buf buf, size_t len, QUICPacketNumber base,
                                             uint8_t dcil)
  : QUICPacketHeader(from, std::move(buf), len, base), _dcil(dcil)
{
  this->_connection_id = QUICTypeUtil::read_QUICConnectionId(this->_buf.get() + 1, dcil);

  int offset               = 1 + this->_connection_id.length();
  this->_packet_number_len = QUICTypeUtil::read_QUICPacketNumberLen(this->_buf.get() + offset);
  QUICPacketNumber src     = QUICTypeUtil::read_QUICPacketNumber(this->_buf.get() + offset);
  QUICPacket::decode_packet_number(this->_packet_number, src, this->_packet_number_len, this->_base_packet_number);
  this->_payload_length = len - (1 + this->_dcil + this->_packet_number_len);
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
    return QUICTypeUtil::read_QUICConnectionId(this->_buf.get() + SHORT_HDR_OFFSET_CONNECTION_ID, this->_dcil);
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
    if (this->_buf.get()[0] & 0x40) {
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

QUICConnectionId
QUICPacket::destination_connection_id(const uint8_t *buf)
{
  uint8_t cid_offset;
  uint8_t cid_len;
  QUICConnectionId cid;
  if (QUICTypeUtil::has_long_header(buf)) {
    cid_offset = 6;
    cid_len    = buf[5] >> 4;
    if (cid_len) {
      cid_len += 3;
    }
  } else {
    cid_offset = 1;
    // TODO Read CID length from records.config
    cid_len = 18;
  }
  cid = QUICTypeUtil::read_QUICConnectionId(buf + cid_offset, cid_len);

  return cid;
}

QUICConnectionId
QUICPacket::source_connection_id(const uint8_t *buf)
{
  ink_assert(QUICTypeUtil::has_long_header(buf));

  uint8_t cid_offset = 6;
  uint8_t cid_len    = 0;

  cid_len = buf[5] >> 4;
  if (cid_len) {
    cid_len += 3;
  }
  cid_offset += cid_len;
  cid_len = buf[5] & 0x0F;
  if (cid_len) {
    cid_len += 3;
  }
  return QUICTypeUtil::read_QUICConnectionId(buf + cid_offset, cid_len);
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

  QUICPacketHeaderUPtr header = QUICPacketHeader::load(from, std::move(buf), len, base_packet_number, this->_dcil);

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
QUICPacketFactory::create_version_negotiation_packet(const QUICPacket *packet_sent_by_client)
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
    QUICPacketHeader::build(QUICPacketType::VERSION_NEGOTIATION, packet_sent_by_client->source_cid(),
                            packet_sent_by_client->destination_cid(), 0x00, 0x00, 0x00, std::move(versions), len);

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

void
QUICPacketFactory::set_dcil(uint8_t len)
{
  this->_dcil = len;
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
