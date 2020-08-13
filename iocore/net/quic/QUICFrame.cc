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

#include "QUICFrame.h"

#include <algorithm>

#include "QUICStream.h"
#include "QUICIntUtil.h"
#include "QUICDebugNames.h"
#include "QUICPacket.h"

#define LEFT_SPACE(pos) ((size_t)(buf + len - pos))
#define FRAME_SIZE(pos) (pos - buf)

// the pos will auto move forward . return true if the data valid
static bool
read_varint(uint8_t *&pos, size_t len, uint64_t &field, size_t &field_len)
{
  if (len < 1) {
    return false;
  }

  field_len = QUICVariableInt::size(pos);
  if (len < field_len) {
    return false;
  }

  field = QUICIntUtil::read_QUICVariableInt(pos, len);
  pos += field_len;
  return true;
}

QUICFrameType
QUICFrame::type() const
{
  ink_assert("should not be called");
  return QUICFrameType::UNKNOWN;
}

bool
QUICFrame::ack_eliciting() const
{
  auto type = this->type();

  return type != QUICFrameType::PADDING && type != QUICFrameType::ACK && type != QUICFrameType::CONNECTION_CLOSE;
}

const QUICPacketR *
QUICFrame::packet() const
{
  return this->_packet;
}

bool
QUICFrame::is_probing_frame() const
{
  return false;
}

bool
QUICFrame::is_flow_controlled() const
{
  return false;
}

QUICFrameId
QUICFrame::id() const
{
  return this->_id;
}

QUICFrameGenerator *
QUICFrame::generated_by()
{
  return this->_owner;
}

QUICFrameType
QUICFrame::type(const uint8_t *buf)
{
  if (buf[0] >= static_cast<uint8_t>(QUICFrameType::UNKNOWN)) {
    return QUICFrameType::UNKNOWN;
  } else if (static_cast<uint8_t>(QUICFrameType::ACK) <= buf[0] && buf[0] < static_cast<uint8_t>(QUICFrameType::RESET_STREAM)) {
    return QUICFrameType::ACK;
  } else if (static_cast<uint8_t>(QUICFrameType::STREAM) <= buf[0] && buf[0] < static_cast<uint8_t>(QUICFrameType::MAX_DATA)) {
    return QUICFrameType::STREAM;
  } else if (static_cast<uint8_t>(QUICFrameType::MAX_STREAMS) <= buf[0] &&
             buf[0] < static_cast<uint8_t>(QUICFrameType::DATA_BLOCKED)) {
    return QUICFrameType::MAX_STREAMS;
  } else if (static_cast<uint8_t>(QUICFrameType::STREAMS_BLOCKED) <= buf[0] &&
             buf[0] < static_cast<uint8_t>(QUICFrameType::NEW_CONNECTION_ID)) {
    return QUICFrameType::STREAMS_BLOCKED;
  } else if (static_cast<uint8_t>(QUICFrameType::CONNECTION_CLOSE) <= buf[0] &&
             buf[0] < static_cast<uint8_t>(QUICFrameType::HANDSHAKE_DONE)) {
    return QUICFrameType::CONNECTION_CLOSE;
  } else {
    return static_cast<QUICFrameType>(buf[0]);
  }
}

int
QUICFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "%s size=%zu", QUICDebugNames::frame_type(this->type()), this->size());
}

bool
QUICFrame::valid() const
{
  return this->_valid;
}

//
// STREAM Frame
//

QUICStreamFrame::QUICStreamFrame(Ptr<IOBufferBlock> &block, QUICStreamId stream_id, QUICOffset offset, bool last,
                                 bool has_offset_field, bool has_length_field, QUICFrameId id, QUICFrameGenerator *owner)
  : QUICFrame(id, owner),
    _block(block),
    _stream_id(stream_id),
    _offset(offset),
    _fin(last),
    _has_offset_field(has_offset_field),
    _has_length_field(has_length_field)
{
}

QUICStreamFrame::QUICStreamFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet) : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

QUICStreamFrame::QUICStreamFrame(const QUICStreamFrame &o)
  : QUICFrame(o),
    _block(make_ptr<IOBufferBlock>(o._block->clone())),
    _stream_id(o._stream_id),
    _offset(o._offset),
    _fin(o._fin),
    _has_offset_field(o._has_offset_field),
    _has_length_field(o._has_length_field)
{
}

void
QUICStreamFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;

  uint8_t *pos            = const_cast<uint8_t *>(buf);
  this->_has_offset_field = (buf[0] & 0x04) != 0; // "O" of "0b00010OLF"
  this->_has_length_field = (buf[0] & 0x02) != 0; // "L" of "0b00010OLF"
  this->_fin              = (buf[0] & 0x01) != 0; // "F" of "0b00010OLF"
  pos += 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_stream_id, field_len)) {
    return;
  }

  if (this->_has_offset_field && !read_varint(pos, LEFT_SPACE(pos), this->_offset, field_len)) {
    return;
  }

  uint64_t data_len = 0;
  if (this->_has_length_field && !read_varint(pos, LEFT_SPACE(pos), data_len, field_len)) {
    return;
  }

  if (!this->_has_length_field) {
    data_len = LEFT_SPACE(pos);
  }
  if (LEFT_SPACE(pos) < data_len) {
    return;
  }

  this->_valid = true;
  this->_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  this->_block->alloc(BUFFER_SIZE_INDEX_32K);
  ink_assert(static_cast<uint64_t>(this->_block->write_avail()) > data_len);
  memcpy(this->_block->start(), pos, data_len);
  this->_block->fill(data_len);
  pos += data_len;
  this->_size = FRAME_SIZE(pos);
}

void
QUICStreamFrame::_reset()
{
  this->_block            = nullptr;
  this->_fin              = false;
  this->_has_length_field = true;
  this->_has_offset_field = true;
  this->_offset           = 0;
  this->_stream_id        = 0;
  this->_owner            = nullptr;
  this->_id               = 0;
  this->_valid            = false;
  this->_size             = 0;
}

QUICFrameType
QUICStreamFrame::type() const
{
  return QUICFrameType::STREAM;
}

size_t
QUICStreamFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  size_t size     = 1;
  size_t data_len = 0;
  if (this->_block.get() != nullptr) {
    data_len = this->_block->read_avail();
  }

  size += QUICVariableInt::size(this->_stream_id);
  if (this->_has_offset_field) {
    size += QUICVariableInt::size(this->_offset);
  }

  if (this->_has_length_field) {
    size += QUICVariableInt::size(data_len);
    size += data_len;
  }

  return size;
}

bool
QUICStreamFrame::is_flow_controlled() const
{
  return true;
}

int
QUICStreamFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "STREAM size=%zu id=%" PRIu64 " offset=%" PRIu64 " data_len=%" PRIu64 " fin=%d", this->size(),
                  this->stream_id(), this->offset(), this->data_length(), this->has_fin_flag());
}

Ptr<IOBufferBlock>
QUICStreamFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> header;

  if (limit < this->size()) {
    return header;
  }

  // Create header block
  size_t written_len = 0;
  header             = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  header->alloc(iobuffer_size_to_index(MAX_HEADER_SIZE, BUFFER_SIZE_INDEX_32K));
  this->_store_header(reinterpret_cast<uint8_t *>(header->start()), &written_len, true);
  header->fill(written_len);

  // Append payload block to a chain
  ink_assert(written_len + this->data_length() <= limit);
  header->next = this->data();

  // Return the chain
  return header;
}

size_t
QUICStreamFrame::_store_header(uint8_t *buf, size_t *len, bool include_length_field) const
{
  // Build Frame Type: "0b0010OLF"
  buf[0] = static_cast<uint8_t>(QUICFrameType::STREAM);
  *len   = 1;

  size_t n;

  // Stream ID (i)
  QUICTypeUtil::write_QUICStreamId(this->stream_id(), buf + *len, &n);
  *len += n;

  // [Offset (i)] "O" of "0b0010OLF"
  if (this->has_offset_field()) {
    QUICTypeUtil::write_QUICOffset(this->offset(), buf + *len, &n);
    *len += n;
    buf[0] += 0x04;
  }

  // [Length (i)] "L of "0b0010OLF"
  if (include_length_field) {
    QUICIntUtil::write_QUICVariableInt(this->data_length(), buf + *len, &n);
    *len += n;
    buf[0] += 0x02;
  }

  // "F" of "0b0010OLF"
  if (this->has_fin_flag()) {
    buf[0] += 0x01;
  }

  return *len;
}

QUICStreamId
QUICStreamFrame::stream_id() const
{
  return this->_stream_id;
}

QUICOffset
QUICStreamFrame::offset() const
{
  if (this->has_offset_field()) {
    return this->_offset;
  }

  return 0;
}

uint64_t
QUICStreamFrame::data_length() const
{
  return this->_block->read_avail();
}

IOBufferBlock *
QUICStreamFrame::data() const
{
  return this->_block.get();
}

/**
 * "O" of "0b00010OLF"
 */
bool
QUICStreamFrame::has_offset_field() const
{
  return this->_has_offset_field;
}

/**
 * "L" of "0b00010OLF"
 */
bool
QUICStreamFrame::has_length_field() const
{
  // This depends on `include_length_field` arg of QUICStreamFrame::store.
  // Returning true for just in case.
  return this->_has_length_field;
}

/**
 * "F" of "0b00010OLF"
 */
bool
QUICStreamFrame::has_fin_flag() const
{
  return this->_fin;
}

//
// CRYPTO frame
//

QUICCryptoFrame::QUICCryptoFrame(Ptr<IOBufferBlock> &block, QUICOffset offset, QUICFrameId id, QUICFrameGenerator *owner)
  : QUICFrame(id, owner), _offset(offset), _block(block)
{
}

QUICCryptoFrame::QUICCryptoFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet) : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

QUICCryptoFrame::QUICCryptoFrame(const QUICCryptoFrame &o)
  : QUICFrame(o), _offset(o._offset), _block(make_ptr<IOBufferBlock>(o._block->clone()))
{
}

void
QUICCryptoFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_offset, field_len)) {
    return;
  }

  uint64_t data_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), data_len, field_len)) {
    return;
  }

  if (LEFT_SPACE(pos) < data_len) {
    return;
  }

  this->_valid = true;
  this->_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  this->_block->alloc(BUFFER_SIZE_INDEX_32K);
  ink_assert(static_cast<uint64_t>(this->_block->write_avail()) > data_len);
  memcpy(this->_block->start(), pos, data_len);
  this->_block->fill(data_len);
  pos += data_len;
  this->_size = FRAME_SIZE(pos);
}

void
QUICCryptoFrame::_reset()
{
  this->_block  = nullptr;
  this->_offset = 0;
  this->_owner  = nullptr;
  this->_id     = 0;
  this->_valid  = false;
  this->_size   = 0;
}

// QUICFrame *
// QUICCryptoFrame::clone(uint8_t *buf) const
// {
//   Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(this->_block->clone());
//   return QUICFrameFactory::create_crypto_frame(buf, block, this->offset(), this->_id, this->_owner);
// }

QUICFrameType
QUICCryptoFrame::type() const
{
  return QUICFrameType::CRYPTO;
}

size_t
QUICCryptoFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return 1 + this->_block->read_avail() + QUICVariableInt::size(this->_offset) + QUICVariableInt::size(this->_block->read_avail());
}

int
QUICCryptoFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "CRYPTO size=%zu offset=%" PRIu64 " data_len=%" PRIu64, this->size(), this->offset(),
                  this->data_length());
}

Ptr<IOBufferBlock>
QUICCryptoFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> header;

  if (limit < this->size()) {
    return header;
  }

  // Create header block
  size_t written_len = 0;
  header             = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  header->alloc(iobuffer_size_to_index(MAX_HEADER_SIZE, BUFFER_SIZE_INDEX_32K));
  this->_store_header(reinterpret_cast<uint8_t *>(header->start()), &written_len);
  header->fill(written_len);

  // Append payload block to a chain
  ink_assert(written_len + this->data_length() <= limit);
  header->next = this->data();

  // Return the chain
  return header;
}

size_t
QUICCryptoFrame::_store_header(uint8_t *buf, size_t *len) const
{
  // Type
  buf[0] = static_cast<uint8_t>(QUICFrameType::CRYPTO);
  *len   = 1;

  size_t n;

  // Offset (i)
  QUICTypeUtil::write_QUICOffset(this->offset(), buf + *len, &n);
  *len += n;

  // Length (i)
  QUICIntUtil::write_QUICVariableInt(this->data_length(), buf + *len, &n);
  *len += n;

  return *len;
}

QUICOffset
QUICCryptoFrame::offset() const
{
  return this->_offset;
}

uint64_t
QUICCryptoFrame::data_length() const
{
  return this->_block->read_avail();
}

IOBufferBlock *
QUICCryptoFrame::data() const
{
  return this->_block.get();
}

//
// ACK frame
//

std::set<QUICAckFrame::PacketNumberRange>
QUICAckFrame::ranges() const
{
  std::set<QUICAckFrame::PacketNumberRange> numbers;
  QUICPacketNumber x = this->largest_acknowledged();
  numbers.insert({x, static_cast<uint64_t>(x) - this->ack_block_section()->first_ack_block()});
  x -= this->ack_block_section()->first_ack_block() + 1;
  for (auto &&block : *(this->ack_block_section())) {
    x -= block.gap() + 1;
    numbers.insert({x, static_cast<uint64_t>(x) - block.length()});
    x -= block.length() + 1;
  }

  return numbers;
}

QUICAckFrame::QUICAckFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet) : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICAckFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;
  bool has_ecn  = (buf[0] == static_cast<uint8_t>(QUICFrameType::ACK_WITH_ECN));

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_largest_acknowledged, field_len)) {
    return;
  }

  if (!read_varint(pos, LEFT_SPACE(pos), this->_ack_delay, field_len)) {
    return;
  }

  uint64_t ack_block_count = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), ack_block_count, field_len)) {
    return;
  }

  uint64_t first_ack_block = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), first_ack_block, field_len)) {
    return;
  }

  this->_ack_block_section = new AckBlockSection(first_ack_block);
  for (size_t i = 0; i < ack_block_count; i++) {
    uint64_t gap           = 0;
    uint64_t add_ack_block = 0;

    if (!read_varint(pos, LEFT_SPACE(pos), gap, field_len)) {
      return;
    }

    if (!read_varint(pos, LEFT_SPACE(pos), add_ack_block, field_len)) {
      return;
    }

    this->_ack_block_section->add_ack_block({gap, add_ack_block});
  }

  if (has_ecn) {
    this->_ecn_section = new EcnSection(pos, LEFT_SPACE(pos));
    if (!this->_ecn_section->valid()) {
      return;
    }
    pos += this->_ecn_section->size();
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICAckFrame::QUICAckFrame(QUICPacketNumber largest_acknowledged, uint64_t ack_delay, uint64_t first_ack_block, QUICFrameId id,
                           QUICFrameGenerator *owner)
  : QUICFrame(id, owner)
{
  this->_largest_acknowledged = largest_acknowledged;
  this->_ack_delay            = ack_delay;
  this->_ack_block_section    = new AckBlockSection(first_ack_block);
}

void
QUICAckFrame::_reset()
{
  if (this->_ack_block_section) {
    delete this->_ack_block_section;
    this->_ack_block_section = nullptr;
  }
  if (this->_ecn_section) {
    delete this->_ecn_section;
    this->_ecn_section = nullptr;
  }

  this->_largest_acknowledged = 0;
  this->_ack_delay            = 0;
  this->_owner                = nullptr;
  this->_id                   = 0;
  this->_valid                = false;
  this->_size                 = 0;
}

QUICAckFrame::~QUICAckFrame()
{
  if (this->_ack_block_section) {
    delete this->_ack_block_section;
    this->_ack_block_section = nullptr;
  }
  if (this->_ecn_section) {
    delete this->_ecn_section;
    this->_ecn_section = nullptr;
  }
}

QUICFrameType
QUICAckFrame::type() const
{
  // TODO ECN
  return QUICFrameType::ACK;
}

size_t
QUICAckFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  size_t pre_len = 1 + QUICVariableInt::size(this->_largest_acknowledged) + QUICVariableInt::size(this->_ack_delay) +
                   QUICVariableInt::size(this->_ack_block_section->count());
  if (this->_ack_block_section) {
    pre_len += this->_ack_block_section->size();
  }

  if (this->_ecn_section) {
    return pre_len + this->_ecn_section->size();
  }

  return pre_len;
}

Ptr<IOBufferBlock>
QUICAckFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + 24, BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::ACK);
  n += 1;

  // Largest Acknowledged (i)
  QUICIntUtil::write_QUICVariableInt(this->_largest_acknowledged, block_start + n, &written_len);
  n += written_len;

  // Ack Delay (i)
  QUICIntUtil::write_QUICVariableInt(this->_ack_delay, block_start + n, &written_len);
  n += written_len;

  // Ack Range Count (i)
  QUICIntUtil::write_QUICVariableInt(this->ack_block_count(), block_start + n, &written_len);
  n += written_len;

  block->fill(n);

  // First Ack Range (i) + Ack Ranges (*)
  block->next = this->_ack_block_section->to_io_buffer_block(limit - n);

  return block;
}

int
QUICAckFrame::debug_msg(char *msg, size_t msg_len) const
{
  int len = snprintf(msg, msg_len, "ACK size=%zu largest_acked=%" PRIu64 " delay=%" PRIu64 " block_count=%" PRIu64, this->size(),
                     this->largest_acknowledged(), this->ack_delay(), this->ack_block_count());
  msg_len -= len;

  if (this->ack_block_section()) {
    len += snprintf(msg + len, msg_len, " first_ack_block=%" PRIu64, this->ack_block_section()->first_ack_block());
  }

  return len;
}

QUICPacketNumber
QUICAckFrame::largest_acknowledged() const
{
  return this->_largest_acknowledged;
}

uint64_t
QUICAckFrame::ack_delay() const
{
  return this->_ack_delay;
}

uint64_t
QUICAckFrame::ack_block_count() const
{
  return this->_ack_block_section->count();
}

QUICAckFrame::AckBlockSection *
QUICAckFrame::ack_block_section()
{
  return this->_ack_block_section;
}

const QUICAckFrame::AckBlockSection *
QUICAckFrame::ack_block_section() const
{
  return this->_ack_block_section;
}

QUICAckFrame::EcnSection *
QUICAckFrame::ecn_section()
{
  return this->_ecn_section;
}

const QUICAckFrame::EcnSection *
QUICAckFrame::ecn_section() const
{
  return this->_ecn_section;
}

//
// QUICAckFrame::PacketNumberRange
//
QUICAckFrame::PacketNumberRange::PacketNumberRange(PacketNumberRange &&a) noexcept
{
  this->_first = a._first;
  this->_last  = a._last;
}

uint64_t
QUICAckFrame::PacketNumberRange::first() const
{
  return this->_first;
}

uint64_t
QUICAckFrame::PacketNumberRange::last() const
{
  return this->_last;
}

uint64_t
QUICAckFrame::PacketNumberRange::size() const
{
  return this->_first - this->_last;
}

bool
QUICAckFrame::PacketNumberRange::contains(QUICPacketNumber x) const
{
  return static_cast<uint64_t>(this->_last) <= static_cast<uint64_t>(x) &&
         static_cast<uint64_t>(x) <= static_cast<uint64_t>(this->_first);
}

//
// QUICAckFrame::AckBlock
//
uint64_t
QUICAckFrame::AckBlock::gap() const
{
  return this->_gap;
}

uint64_t
QUICAckFrame::AckBlock::length() const
{
  return this->_length;
}

size_t
QUICAckFrame::AckBlock::size() const
{
  return QUICVariableInt::size(this->_gap) + QUICVariableInt::size(this->_length);
}

//
// QUICAckFrame::AckBlockSection
//
uint8_t
QUICAckFrame::AckBlockSection::count() const
{
  return this->_ack_blocks.size();
}

size_t
QUICAckFrame::AckBlockSection::size() const
{
  size_t n = 0;

  n += QUICVariableInt::size(this->_first_ack_block);

  for (auto &&block : *this) {
    n += block.size();
  }

  return n;
}

Ptr<IOBufferBlock>
QUICAckFrame::AckBlockSection::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(limit, BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  QUICIntUtil::write_QUICVariableInt(this->_first_ack_block, block_start + n, &written_len);
  n += written_len;

  for (auto &&block : *this) {
    QUICIntUtil::write_QUICVariableInt(block.gap(), block_start + n, &written_len);
    n += written_len;
    QUICIntUtil::write_QUICVariableInt(block.length(), block_start + n, &written_len);
    n += written_len;
  }

  block->fill(n);
  return block;
}

uint64_t
QUICAckFrame::AckBlockSection::first_ack_block() const
{
  return this->_first_ack_block;
}

void
QUICAckFrame::AckBlockSection::add_ack_block(const AckBlock &block)
{
  this->_ack_blocks.push_back(block);
}

QUICAckFrame::AckBlockSection::const_iterator
QUICAckFrame::AckBlockSection::begin() const
{
  return const_iterator(0, &this->_ack_blocks);
}

QUICAckFrame::AckBlockSection::const_iterator
QUICAckFrame::AckBlockSection::end() const
{
  return const_iterator(this->_ack_blocks.size(), &this->_ack_blocks);
}

QUICAckFrame::AckBlockSection::const_iterator::const_iterator(uint8_t index, const std::vector<QUICAckFrame::AckBlock> *ack_blocks)
  : _index(index), _ack_blocks(ack_blocks)
{
  if (this->_ack_blocks->size()) {
    if (this->_ack_blocks->size() == this->_index) {
      this->_current_block = {UINT64_C(0), UINT64_C(0)};
    } else {
      this->_current_block = this->_ack_blocks->at(this->_index);
    }
  }
}

// FIXME: something wrong with clang-format?
const QUICAckFrame::AckBlock &
QUICAckFrame::AckBlockSection::const_iterator::operator++()
{
  ++(this->_index);

  if (this->_ack_blocks->size() == this->_index) {
    this->_current_block = {UINT64_C(0), UINT64_C(0)};
  } else {
    this->_current_block = this->_ack_blocks->at(this->_index);
  }

  return this->_current_block;
}

const bool
QUICAckFrame::AckBlockSection::const_iterator::operator!=(const const_iterator &ite) const
{
  return this->_index != ite._index;
}

const bool
QUICAckFrame::AckBlockSection::const_iterator::operator==(const const_iterator &ite) const
{
  return this->_index == ite._index;
}

QUICAckFrame::EcnSection::EcnSection(const uint8_t *buf, size_t len)
{
  uint8_t *pos = const_cast<uint8_t *>(buf);

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_ect0_count, field_len)) {
    return;
  }

  if (!read_varint(pos, LEFT_SPACE(pos), this->_ect1_count, field_len)) {
    return;
  }

  if (!read_varint(pos, LEFT_SPACE(pos), this->_ecn_ce_count, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

bool
QUICAckFrame::EcnSection::valid() const
{
  return this->_valid;
}

size_t
QUICAckFrame::EcnSection::size() const
{
  return QUICVariableInt::size(this->_ect0_count) + QUICVariableInt::size(this->_ect1_count) +
         QUICVariableInt::size(this->_ecn_ce_count);
}

uint64_t
QUICAckFrame::EcnSection::ect0_count() const
{
  return this->_ect0_count;
}

uint64_t
QUICAckFrame::EcnSection::ect1_count() const
{
  return this->_ect1_count;
}

uint64_t
QUICAckFrame::EcnSection::ecn_ce_count() const
{
  return this->_ecn_ce_count;
}

//
// RESET_STREAM frame
//

QUICRstStreamFrame::QUICRstStreamFrame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICOffset final_offset, QUICFrameId id,
                                       QUICFrameGenerator *owner)
  : QUICFrame(id, owner), _stream_id(stream_id), _error_code(error_code), _final_offset(final_offset)
{
}

QUICRstStreamFrame::QUICRstStreamFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet) : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICRstStreamFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = 1 + const_cast<uint8_t *>(buf);

  size_t field_len = 0;

  // Stream ID (i)
  if (!read_varint(pos, LEFT_SPACE(pos), this->_stream_id, field_len)) {
    return;
  }

  // Error Code (i)
  if (LEFT_SPACE(pos) < 1) {
    return;
  }
  if (!read_varint(pos, LEFT_SPACE(pos), this->_error_code, field_len)) {
    return;
  }

  // Final Offset (i)
  if (!read_varint(pos, LEFT_SPACE(pos), this->_final_offset, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

void
QUICRstStreamFrame::_reset()
{
  this->_stream_id    = 0;
  this->_error_code   = 0;
  this->_final_offset = 0;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

QUICFrameType
QUICRstStreamFrame::type() const
{
  return QUICFrameType::RESET_STREAM;
}

size_t
QUICRstStreamFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return 1 + QUICVariableInt::size(this->_stream_id) + QUICVariableInt::size(this->_error_code) +
         QUICVariableInt::size(this->_final_offset);
}

Ptr<IOBufferBlock>
QUICRstStreamFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + 24, BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::RESET_STREAM);
  n += 1;

  // Stream ID (i)
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, block_start + n, &written_len);
  n += written_len;

  // Application Error Code (i)
  QUICTypeUtil::write_QUICAppErrorCode(this->_error_code, block_start + n, &written_len);
  n += written_len;

  // Final Size (i)
  QUICTypeUtil::write_QUICOffset(this->_final_offset, block_start + n, &written_len);
  n += written_len;

  block->fill(n);
  return block;
}

int
QUICRstStreamFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "RESET_STREAM size=%zu stream_id=%" PRIu64 " code=0x%" PRIx64, this->size(), this->stream_id(),
                  this->error_code());
}

QUICStreamId
QUICRstStreamFrame::stream_id() const
{
  return this->_stream_id;
}

QUICAppErrorCode
QUICRstStreamFrame::error_code() const
{
  return this->_error_code;
}

QUICOffset
QUICRstStreamFrame::final_offset() const
{
  return this->_final_offset;
}

//
// PING frame
//

QUICPingFrame::QUICPingFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet) : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICPingFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  this->_reset();
  this->_packet = packet;
  this->_valid  = true;
  this->_size   = 1;
}

QUICFrameType
QUICPingFrame::type() const
{
  return QUICFrameType::PING;
}

size_t
QUICPingFrame::size() const
{
  return 1;
}

Ptr<IOBufferBlock>
QUICPingFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(this->size(), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::PING);
  n += 1;

  block->fill(n);
  return block;
}

//
// PADDING frame
//
QUICPaddingFrame::QUICPaddingFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet) : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICPaddingFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  this->_size   = 0;
  this->_valid  = true;
  // find out how many padding frames in this buf
  for (size_t i = 0; i < len; i++) {
    if (*(buf + i) == static_cast<uint8_t>(QUICFrameType::PADDING)) {
      ++this->_size;
    } else {
      break;
    }
  }
}

QUICFrameType
QUICPaddingFrame::type() const
{
  return QUICFrameType::PADDING;
}

size_t
QUICPaddingFrame::size() const
{
  return this->_size;
}

bool
QUICPaddingFrame::is_probing_frame() const
{
  return true;
}

Ptr<IOBufferBlock>
QUICPaddingFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(this->_size, BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  memset(block_start, 0, this->_size);
  n = this->_size;

  block->fill(n);
  return block;
}

//
// CONNECTION_CLOSE frame
//
QUICConnectionCloseFrame::QUICConnectionCloseFrame(uint64_t error_code, QUICFrameType frame_type, uint64_t reason_phrase_length,
                                                   const char *reason_phrase, QUICFrameId id, QUICFrameGenerator *owner)
  : QUICFrame(id, owner),
    _type(0x1c),
    _error_code(error_code),
    _frame_type(frame_type),
    _reason_phrase_length(reason_phrase_length),
    _reason_phrase(reason_phrase)
{
}

QUICConnectionCloseFrame::QUICConnectionCloseFrame(uint64_t error_code, uint64_t reason_phrase_length, const char *reason_phrase,
                                                   QUICFrameId id, QUICFrameGenerator *owner)
  : QUICFrame(id, owner),
    _type(0x1d),
    _error_code(error_code),
    _reason_phrase_length(reason_phrase_length),
    _reason_phrase(reason_phrase)
{
}

QUICConnectionCloseFrame::QUICConnectionCloseFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICConnectionCloseFrame::_reset()
{
  this->_error_code           = 0;
  this->_reason_phrase_length = 0;
  this->_reason_phrase        = nullptr;
  this->_frame_type           = QUICFrameType::UNKNOWN;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_size  = 0;
  this->_valid = false;
}

void
QUICConnectionCloseFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  this->_type   = buf[0];
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  uint64_t field   = 0;

  // Error Code (i)
  if (LEFT_SPACE(pos) < 1) {
    return;
  }
  read_varint(pos, LEFT_SPACE(pos), field, field_len);
  this->_error_code = field;

  if (this->_type == 0x1c) {
    // Frame Type (i)
    if (!read_varint(pos, LEFT_SPACE(pos), field, field_len)) {
      return;
    }
    this->_frame_type = static_cast<QUICFrameType>(field);

    /**
       Frame Type Field Accessor

       PADDING frame in Frame Type field means frame type that triggered the error is unknown.
       Return QUICFrameType::UNKNOWN when Frame Type field is PADDING (0x0).
     */
    if (this->_frame_type == QUICFrameType::PADDING) {
      this->_frame_type = QUICFrameType::UNKNOWN;
    }
  }

  // Reason Phrase Length (i)
  if (LEFT_SPACE(pos) < 1) {
    return;
  }
  if (!read_varint(pos, LEFT_SPACE(pos), this->_reason_phrase_length, field_len)) {
    return;
  }

  // Reason Phrase
  if (LEFT_SPACE(pos) < this->_reason_phrase_length) {
    return;
  }
  this->_reason_phrase = reinterpret_cast<const char *>(pos);

  this->_valid = true;
  pos += this->_reason_phrase_length;
  this->_size = FRAME_SIZE(pos);
}

QUICFrameType
QUICConnectionCloseFrame::type() const
{
  return QUICFrameType::CONNECTION_CLOSE;
}

size_t
QUICConnectionCloseFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return 1 + QUICVariableInt::size(sizeof(QUICTransErrorCode)) + QUICVariableInt::size(sizeof(QUICFrameType)) +
         QUICVariableInt::size(this->_reason_phrase_length) + this->_reason_phrase_length;
}

/**
   Store CONNECTION_CLOSE frame in buffer.

   PADDING frame in Frame Type field means frame type that triggered the error is unknown.
   When `_frame_type` is QUICFrameType::UNKNOWN, it's converted to QUICFrameType::PADDING (0x0).
 */
Ptr<IOBufferBlock>
QUICConnectionCloseFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> first_block;
  size_t n = 0;

  if (limit < this->size()) {
    return first_block;
  }

  // Create a block for Error Code(i) and Frame Type(i)
  size_t written_len = 0;
  first_block        = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  first_block->alloc(iobuffer_size_to_index(1 + 24, BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(first_block->start());

  // Type
  block_start[0] = this->_type;
  n += 1;

  // Error Code (i)
  QUICIntUtil::write_QUICVariableInt(this->_error_code, block_start + n, &written_len);
  n += written_len;

  // Frame Type (i)
  QUICFrameType frame_type = this->_frame_type;
  if (frame_type == QUICFrameType::UNKNOWN) {
    frame_type = QUICFrameType::PADDING;
  }
  QUICIntUtil::write_QUICVariableInt(static_cast<uint64_t>(frame_type), block_start + n, &written_len);
  n += written_len;

  // Reason Phrase Length (i)
  QUICIntUtil::write_QUICVariableInt(this->_reason_phrase_length, block_start + n, &written_len);
  n += written_len;

  first_block->fill(n);

  // Create a block for reason if necessary
  if (this->_reason_phrase_length != 0) {
    // Reason Phrase (*)
    Ptr<IOBufferBlock> reason_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    reason_block->alloc(iobuffer_size_to_index(this->_reason_phrase_length, BUFFER_SIZE_INDEX_32K));
    memcpy(reinterpret_cast<uint8_t *>(reason_block->start()), this->_reason_phrase, this->_reason_phrase_length);
    reason_block->fill(this->_reason_phrase_length);

    // Append reason block to the first block
    first_block->next = reason_block;
  }

  // Return the chain
  return first_block;
}

int
QUICConnectionCloseFrame::debug_msg(char *msg, size_t msg_len) const
{
  int len;
  if (this->_type == 0x1c) {
    len =
      snprintf(msg, msg_len, "CONNECTION_CLOSE size=%zu code=%s (0x%" PRIx16 ") frame=%s", this->size(),
               QUICDebugNames::error_code(this->error_code()), this->error_code(), QUICDebugNames::frame_type(this->frame_type()));
  } else {
    // Application-specific error. It doesn't have a frame type and we don't know string representations of error codes.
    len = snprintf(msg, msg_len, "CONNECTION_CLOSE size=%zu code=0x%" PRIx16 " ", this->size(), this->error_code());
  }

  if (this->reason_phrase_length() != 0 && this->reason_phrase() != nullptr) {
    memcpy(msg + len, " reason=", 8);
    len += 8;

    int phrase_len = std::min(msg_len - len, static_cast<size_t>(this->reason_phrase_length()));
    memcpy(msg + len, this->reason_phrase(), phrase_len);
    len += phrase_len;
    msg[len] = '\0';
    ++len;
  }

  return len;
}

uint16_t
QUICConnectionCloseFrame::error_code() const
{
  return this->_error_code;
}

QUICFrameType
QUICConnectionCloseFrame::frame_type() const
{
  return this->_frame_type;
}

uint64_t
QUICConnectionCloseFrame::reason_phrase_length() const
{
  return this->_reason_phrase_length;
}

const char *
QUICConnectionCloseFrame::reason_phrase() const
{
  return this->_reason_phrase;
}

//
// MAX_DATA frame
//
QUICMaxDataFrame::QUICMaxDataFrame(uint64_t maximum_data, QUICFrameId id, QUICFrameGenerator *owner) : QUICFrame(id, owner)
{
  this->_maximum_data = maximum_data;
}

void
QUICMaxDataFrame::_reset()
{
  this->_maximum_data = 0;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

QUICMaxDataFrame::QUICMaxDataFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet) : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICMaxDataFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = 1 + const_cast<uint8_t *>(buf);

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_maximum_data, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameType
QUICMaxDataFrame::type() const
{
  return QUICFrameType::MAX_DATA;
}

size_t
QUICMaxDataFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_maximum_data);
}

Ptr<IOBufferBlock>
QUICMaxDataFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + sizeof(size_t), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::MAX_DATA);
  n += 1;

  // Maximum Data (i)
  QUICTypeUtil::write_QUICMaxData(this->_maximum_data, block_start + n, &written_len);
  n += written_len;

  block->fill(n);
  return block;
}

int
QUICMaxDataFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "MAX_DATA size=%zu maximum=%" PRIu64, this->size(), this->maximum_data());
}

uint64_t
QUICMaxDataFrame::maximum_data() const
{
  return this->_maximum_data;
}

//
// MAX_STREAM_DATA
//
QUICMaxStreamDataFrame::QUICMaxStreamDataFrame(QUICStreamId stream_id, uint64_t maximum_stream_data, QUICFrameId id,
                                               QUICFrameGenerator *owner)
  : QUICFrame(id, owner)
{
  this->_stream_id           = stream_id;
  this->_maximum_stream_data = maximum_stream_data;
}

void
QUICMaxStreamDataFrame::_reset()
{
  this->_stream_id           = 0;
  this->_maximum_stream_data = 0;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

QUICMaxStreamDataFrame::QUICMaxStreamDataFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICMaxStreamDataFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_stream_id, field_len)) {
    return;
  }

  if (!read_varint(pos, LEFT_SPACE(pos), this->_maximum_stream_data, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameType
QUICMaxStreamDataFrame::type() const
{
  return QUICFrameType::MAX_STREAM_DATA;
}

size_t
QUICMaxStreamDataFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_maximum_stream_data) + QUICVariableInt::size(this->_stream_id);
}

Ptr<IOBufferBlock>
QUICMaxStreamDataFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + sizeof(uint64_t) + sizeof(size_t), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::MAX_STREAM_DATA);
  n += 1;

  // Stream ID (i)
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, block_start + n, &written_len);
  n += written_len;

  // Maximum Stream Data (i)
  QUICTypeUtil::write_QUICMaxData(this->_maximum_stream_data, block_start + n, &written_len);
  n += written_len;

  block->fill(n);
  return block;
}

int
QUICMaxStreamDataFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "MAX_STREAM_DATA size=%zu id=%" PRIu64 " maximum=%" PRIu64, this->size(), this->stream_id(),
                  this->maximum_stream_data());
}

QUICStreamId
QUICMaxStreamDataFrame::stream_id() const
{
  return this->_stream_id;
}

uint64_t
QUICMaxStreamDataFrame::maximum_stream_data() const
{
  return this->_maximum_stream_data;
}

//
// MAX_STREAMS
//
QUICMaxStreamsFrame::QUICMaxStreamsFrame(QUICStreamId maximum_streams, QUICFrameId id, QUICFrameGenerator *owner)
  : QUICFrame(id, owner)
{
  this->_maximum_streams = maximum_streams;
}

void
QUICMaxStreamsFrame::_reset()
{
  this->_maximum_streams = 0;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

QUICMaxStreamsFrame::QUICMaxStreamsFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet) : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICMaxStreamsFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_maximum_streams, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameType
QUICMaxStreamsFrame::type() const
{
  return QUICFrameType::MAX_STREAMS;
}

size_t
QUICMaxStreamsFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_maximum_streams);
}

Ptr<IOBufferBlock>
QUICMaxStreamsFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + sizeof(size_t), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::MAX_STREAMS);
  n += 1;

  // Maximum Streams (i)
  QUICTypeUtil::write_QUICStreamId(this->_maximum_streams, block_start + n, &written_len);
  n += written_len;

  block->fill(n);
  return block;
}

uint64_t
QUICMaxStreamsFrame::maximum_streams() const
{
  return this->_maximum_streams;
}

//
// DATA_BLOCKED frame
//
QUICDataBlockedFrame::QUICDataBlockedFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICDataBlockedFrame::_reset()
{
  this->_offset = 0;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

void
QUICDataBlockedFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_offset, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

int
QUICDataBlockedFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "DATA_BLOCKED size=%zu offset=%" PRIu64, this->size(), this->offset());
}

QUICFrameType
QUICDataBlockedFrame::type() const
{
  return QUICFrameType::DATA_BLOCKED;
}

size_t
QUICDataBlockedFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->offset());
}

Ptr<IOBufferBlock>
QUICDataBlockedFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + sizeof(size_t), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::DATA_BLOCKED);
  n += 1;

  // Data Limit (i)
  QUICTypeUtil::write_QUICOffset(this->_offset, block_start + n, &written_len);
  n += written_len;

  block->fill(n);
  return block;
}

QUICOffset
QUICDataBlockedFrame::offset() const
{
  return this->_offset;
}

//
// STREAM_DATA_BLOCKED frame
//
QUICStreamDataBlockedFrame::QUICStreamDataBlockedFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICStreamDataBlockedFrame::_reset()
{
  this->_stream_id = 0;
  this->_offset    = 0;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

void
QUICStreamDataBlockedFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_stream_id, field_len)) {
    return;
  }

  if (!read_varint(pos, LEFT_SPACE(pos), this->_offset, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

int
QUICStreamDataBlockedFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "STREAM_DATA_BLOCKED size=%zu id=%" PRIu64 " offset=%" PRIu64, this->size(), this->stream_id(),
                  this->offset());
}

QUICFrameType
QUICStreamDataBlockedFrame::type() const
{
  return QUICFrameType::STREAM_DATA_BLOCKED;
}

size_t
QUICStreamDataBlockedFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_offset) + QUICVariableInt::size(this->_stream_id);
}

Ptr<IOBufferBlock>
QUICStreamDataBlockedFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + sizeof(size_t), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::STREAM_DATA_BLOCKED);
  n += 1;

  // Stream ID (i)
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, block_start + n, &written_len);
  n += written_len;

  // Data Limit (i)
  QUICTypeUtil::write_QUICOffset(this->_offset, block_start + n, &written_len);
  n += written_len;

  block->fill(n);
  return block;
}

QUICStreamId
QUICStreamDataBlockedFrame::stream_id() const
{
  return this->_stream_id;
}

QUICOffset
QUICStreamDataBlockedFrame::offset() const
{
  return this->_offset;
}

//
// STREAMS_BLOCKED frame
//
QUICStreamIdBlockedFrame::QUICStreamIdBlockedFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICStreamIdBlockedFrame::_reset()
{
  this->_stream_id = 0;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_size  = 0;
  this->_valid = false;
}

void
QUICStreamIdBlockedFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_stream_id, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameType
QUICStreamIdBlockedFrame::type() const
{
  return QUICFrameType::STREAMS_BLOCKED;
}

size_t
QUICStreamIdBlockedFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_stream_id);
}

Ptr<IOBufferBlock>
QUICStreamIdBlockedFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + sizeof(size_t), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::STREAMS_BLOCKED);
  n += 1;

  // Stream Limit (i)
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, block_start + n, &written_len);
  n += written_len;

  block->fill(n);
  return block;
}

QUICStreamId
QUICStreamIdBlockedFrame::stream_id() const
{
  return this->_stream_id;
}

//
// NEW_CONNECTION_ID frame
//
QUICNewConnectionIdFrame::QUICNewConnectionIdFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICNewConnectionIdFrame::_reset()
{
  this->_sequence        = 0;
  this->_retire_prior_to = 0;
  this->_connection_id   = QUICConnectionId::ZERO();

  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

void
QUICNewConnectionIdFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  // Sequence Number (i)
  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_sequence, field_len)) {
    return;
  }

  // Retire Prior To (i)
  if (LEFT_SPACE(pos) < 1) {
    return;
  }
  if (!read_varint(pos, LEFT_SPACE(pos), this->_retire_prior_to, field_len)) {
    return;
  }

  // Length (8)
  if (LEFT_SPACE(pos) < 1) {
    return;
  }
  size_t cid_len = *pos;
  pos += 1;

  // Connection ID (8..160)
  if (LEFT_SPACE(pos) < cid_len) {
    return;
  }
  this->_connection_id = QUICTypeUtil::read_QUICConnectionId(pos, cid_len);
  pos += cid_len;

  // Stateless Reset Token (128)
  if (LEFT_SPACE(pos) < QUICStatelessResetToken::LEN) {
    return;
  }

  this->_stateless_reset_token = QUICStatelessResetToken(pos);
  this->_valid                 = true;
  this->_size                  = FRAME_SIZE(pos) + QUICStatelessResetToken::LEN;
}

QUICFrameType
QUICNewConnectionIdFrame::type() const
{
  return QUICFrameType::NEW_CONNECTION_ID;
}

size_t
QUICNewConnectionIdFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_sequence) + QUICVariableInt::size(this->_retire_prior_to) + 1 +
         this->_connection_id.length() + QUICStatelessResetToken::LEN;
}

Ptr<IOBufferBlock>
QUICNewConnectionIdFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + sizeof(uint64_t) + sizeof(uint64_t) + 1 + QUICConnectionId::MAX_LENGTH +
                                        QUICStatelessResetToken::LEN,
                                      BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::NEW_CONNECTION_ID);
  n += 1;

  // Sequence Number (i)
  QUICIntUtil::write_QUICVariableInt(this->_sequence, block_start + n, &written_len);
  n += written_len;

  // Retire Prior To (i)
  QUICIntUtil::write_QUICVariableInt(this->_retire_prior_to, block_start + n, &written_len);
  n += written_len;

  // Length (8)
  *(block_start + n) = this->_connection_id.length();
  n += 1;

  // Connection ID (8..160)
  QUICTypeUtil::write_QUICConnectionId(this->_connection_id, block_start + n, &written_len);
  n += written_len;

  // Stateless Reset Token (128)
  memcpy(block_start + n, this->_stateless_reset_token.buf(), QUICStatelessResetToken::LEN);
  n += QUICStatelessResetToken::LEN;

  block->fill(n);
  return block;
}

int
QUICNewConnectionIdFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "NEW_CONNECTION_ID size=%zu seq=%" PRIu64 " rpt=%" PRIu64 " cid=0x%s srt=%02x%02x%02x%02x",
                  this->size(), this->sequence(), this->retire_prior_to(), this->connection_id().hex().c_str(),
                  this->stateless_reset_token().buf()[0], this->stateless_reset_token().buf()[1],
                  this->stateless_reset_token().buf()[2], this->stateless_reset_token().buf()[3]);
}

uint64_t
QUICNewConnectionIdFrame::sequence() const
{
  return this->_sequence;
}

uint64_t
QUICNewConnectionIdFrame::retire_prior_to() const
{
  return this->_retire_prior_to;
}

QUICConnectionId
QUICNewConnectionIdFrame::connection_id() const
{
  return this->_connection_id;
}

QUICStatelessResetToken
QUICNewConnectionIdFrame::stateless_reset_token() const
{
  return this->_stateless_reset_token;
}

//
// STOP_SENDING frame
//

QUICStopSendingFrame::QUICStopSendingFrame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICFrameId id,
                                           QUICFrameGenerator *owner)
  : QUICFrame(id, owner), _stream_id(stream_id), _error_code(error_code)
{
}

void
QUICStopSendingFrame::_reset()
{
  this->_stream_id  = 0;
  this->_error_code = 0;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

QUICStopSendingFrame::QUICStopSendingFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICStopSendingFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  // Stream ID (i)
  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_stream_id, field_len)) {
    return;
  }

  // Error Code (i)
  if (LEFT_SPACE(pos) < 1) {
    return;
  }
  if (!read_varint(pos, LEFT_SPACE(pos), this->_error_code, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameType
QUICStopSendingFrame::type() const
{
  return QUICFrameType::STOP_SENDING;
}

size_t
QUICStopSendingFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_stream_id) + QUICVariableInt::size(sizeof(QUICAppErrorCode));
}

Ptr<IOBufferBlock>
QUICStopSendingFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + 24, BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::STOP_SENDING);
  n += 1;

  // Stream ID (i)
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, block_start + n, &written_len);
  n += written_len;

  // Application Error Code (i)
  QUICTypeUtil::write_QUICAppErrorCode(this->_error_code, block_start + n, &written_len);
  n += written_len;

  block->fill(n);
  return block;
}

QUICAppErrorCode
QUICStopSendingFrame::error_code() const
{
  return this->_error_code;
}

QUICStreamId
QUICStopSendingFrame::stream_id() const
{
  return this->_stream_id;
}

//
// PATH_CHALLENGE frame
//
QUICPathChallengeFrame::QUICPathChallengeFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICPathChallengeFrame::_reset()
{
  this->_data  = nullptr;
  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

void
QUICPathChallengeFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  if (LEFT_SPACE(pos) < QUICPathChallengeFrame::DATA_LEN) {
    return;
  }

  this->_data = ats_unique_malloc(QUICPathChallengeFrame::DATA_LEN);
  memcpy(this->_data.get(), pos, QUICPathChallengeFrame::DATA_LEN);
  this->_valid = true;
  this->_size  = FRAME_SIZE(pos) + QUICPathChallengeFrame::DATA_LEN;
}

QUICFrameType
QUICPathChallengeFrame::type() const
{
  return QUICFrameType::PATH_CHALLENGE;
}

size_t
QUICPathChallengeFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return 1 + QUICPathChallengeFrame::DATA_LEN;
}

bool
QUICPathChallengeFrame::is_probing_frame() const
{
  return true;
}

Ptr<IOBufferBlock>
QUICPathChallengeFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + QUICPathChallengeFrame::DATA_LEN, BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::PATH_CHALLENGE);
  n += 1;

  // Data (64)
  memcpy(block_start + n, this->data(), QUICPathChallengeFrame::DATA_LEN);
  n += QUICPathChallengeFrame::DATA_LEN;

  block->fill(n);
  return block;
}

int
QUICPathChallengeFrame::debug_msg(char *msg, size_t msg_len) const
{
  auto data = this->data();
  return snprintf(msg, msg_len, "PATH_CHALLENGE size=%zu data=0x%02x%02x%02x%02x%02x%02x%02x%02x", this->size(), data[0], data[1],
                  data[2], data[3], data[4], data[5], data[6], data[7]);
}

const uint8_t *
QUICPathChallengeFrame::data() const
{
  return this->_data.get();
}

//
// PATH_RESPONSE frame
//
QUICPathResponseFrame::QUICPathResponseFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICPathResponseFrame::_reset()
{
  this->_data  = nullptr;
  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

Ptr<IOBufferBlock>
QUICPathResponseFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + QUICPathResponseFrame::DATA_LEN, BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::PATH_RESPONSE);
  n += 1;

  // Data (64)
  memcpy(block_start + n, this->data(), QUICPathChallengeFrame::DATA_LEN);
  n += QUICPathChallengeFrame::DATA_LEN;

  block->fill(n);
  return block;
}

void
QUICPathResponseFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  if (LEFT_SPACE(pos) < QUICPathChallengeFrame::DATA_LEN) {
    return;
  }

  this->_data = ats_unique_malloc(QUICPathChallengeFrame::DATA_LEN);
  memcpy(this->_data.get(), pos, QUICPathChallengeFrame::DATA_LEN);
  this->_valid = true;
  this->_size  = FRAME_SIZE(pos) + QUICPathChallengeFrame::DATA_LEN;
}

QUICFrameType
QUICPathResponseFrame::type() const
{
  return QUICFrameType::PATH_RESPONSE;
}

size_t
QUICPathResponseFrame::size() const
{
  return 1 + 8;
}

bool
QUICPathResponseFrame::is_probing_frame() const
{
  return true;
}

int
QUICPathResponseFrame::debug_msg(char *msg, size_t msg_len) const
{
  auto data = this->data();
  return snprintf(msg, msg_len, "PATH_RESPONSE size=%zu data=0x%02x%02x%02x%02x%02x%02x%02x%02x", this->size(), data[0], data[1],
                  data[2], data[3], data[4], data[5], data[6], data[7]);
}

const uint8_t *
QUICPathResponseFrame::data() const
{
  return this->_data.get();
}

//
// QUICNewTokenFrame
//
QUICNewTokenFrame::QUICNewTokenFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet) : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICNewTokenFrame::_reset()
{
  this->_token        = nullptr;
  this->_token_length = 0;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
}

void
QUICNewTokenFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_token_length, field_len)) {
    return;
  }

  if (LEFT_SPACE(pos) < this->_token_length) {
    return;
  }

  this->_token = ats_unique_malloc(this->_token_length);
  memcpy(this->_token.get(), pos, this->_token_length);
  this->_valid = true;
  this->_size  = FRAME_SIZE(pos) + this->_token_length;
}

QUICFrameType
QUICNewTokenFrame::type() const
{
  return QUICFrameType::NEW_TOKEN;
}

size_t
QUICNewTokenFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return 1 + QUICVariableInt::size(this->_token_length) + this->token_length();
}

Ptr<IOBufferBlock>
QUICNewTokenFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + 24, BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::NEW_TOKEN);
  n += 1;

  // Token Length (i)
  QUICIntUtil::write_QUICVariableInt(this->_token_length, block_start + n, &written_len);
  n += written_len;

  // Token (*)
  memcpy(block_start + n, this->token(), this->token_length());
  n += this->token_length();

  block->fill(n);
  return block;
}

uint64_t
QUICNewTokenFrame::token_length() const
{
  return this->_token_length;
}

const uint8_t *
QUICNewTokenFrame::token() const
{
  return this->_token.get();
}

//
// RETIRE_CONNECTION_ID frame
//
QUICRetireConnectionIdFrame::QUICRetireConnectionIdFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICRetireConnectionIdFrame::_reset()
{
  this->_seq_num = 0;

  this->_owner = nullptr;
  this->_id    = 0;
  this->_valid = false;
  this->_size  = 0;
  this->_size  = 0;
}

void
QUICRetireConnectionIdFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  ink_assert(len >= 1);
  this->_reset();
  this->_packet = packet;
  uint8_t *pos  = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_seq_num, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameType
QUICRetireConnectionIdFrame::type() const
{
  return QUICFrameType::RETIRE_CONNECTION_ID;
}

size_t
QUICRetireConnectionIdFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_seq_num);
}

Ptr<IOBufferBlock>
QUICRetireConnectionIdFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  size_t written_len = 0;
  block              = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(1 + sizeof(uint64_t), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::RETIRE_CONNECTION_ID);
  n += 1;

  // Sequence Number (i)
  QUICIntUtil::write_QUICVariableInt(this->_seq_num, block_start + n, &written_len);
  n += written_len;

  block->fill(n);
  return block;
}

int
QUICRetireConnectionIdFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "RETIRE_CONNECTION_ID size=%zu seq_num=%" PRIu64, this->size(), this->seq_num());
}

uint64_t
QUICRetireConnectionIdFrame::seq_num() const
{
  return this->_seq_num;
}

//
// HANDSHAKE_DONE frame
//

QUICHandshakeDoneFrame::QUICHandshakeDoneFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet)
  : QUICFrame(0, nullptr, packet)
{
  this->parse(buf, len, packet);
}

void
QUICHandshakeDoneFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  this->_reset();
  this->_packet = packet;
  this->_valid  = true;
  this->_size   = 1;
}

QUICFrameType
QUICHandshakeDoneFrame::type() const
{
  return QUICFrameType::HANDSHAKE_DONE;
}

size_t
QUICHandshakeDoneFrame::size() const
{
  return 1;
}

Ptr<IOBufferBlock>
QUICHandshakeDoneFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  size_t n = 0;

  if (limit < this->size()) {
    return block;
  }

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(this->size(), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  // Type
  block_start[0] = static_cast<uint8_t>(QUICFrameType::HANDSHAKE_DONE);
  n += 1;

  block->fill(n);
  return block;
}

//
// UNKNOWN
//
QUICFrameType
QUICUnknownFrame::type() const
{
  return QUICFrameType::UNKNOWN;
}

size_t
QUICUnknownFrame::size() const
{
  // FIXME size should be readable
  return 0;
}

Ptr<IOBufferBlock>
QUICUnknownFrame::to_io_buffer_block(size_t limit) const
{
  Ptr<IOBufferBlock> block;
  return block;
}

void
QUICUnknownFrame::parse(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  this->_packet = packet;
}

int
QUICUnknownFrame::debug_msg(char *msg, size_t msg_len) const
{
  return 0;
}

//
// QUICFrameFactory
//

QUICFrame *
QUICFrameFactory::create(uint8_t *buf, const uint8_t *src, size_t len, const QUICPacketR *packet)
{
  switch (QUICFrame::type(src)) {
  case QUICFrameType::STREAM:
    new (buf) QUICStreamFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::CRYPTO:
    new (buf) QUICCryptoFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::ACK:
    new (buf) QUICAckFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::PADDING:
    new (buf) QUICPaddingFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::RESET_STREAM:
    new (buf) QUICRstStreamFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::CONNECTION_CLOSE:
    new (buf) QUICConnectionCloseFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::MAX_DATA:
    new (buf) QUICMaxDataFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::MAX_STREAM_DATA:
    new (buf) QUICMaxStreamDataFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::MAX_STREAMS:
    new (buf) QUICMaxStreamsFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::PING:
    new (buf) QUICPingFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::DATA_BLOCKED:
    new (buf) QUICDataBlockedFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::STREAM_DATA_BLOCKED:
    new (buf) QUICStreamDataBlockedFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::STREAMS_BLOCKED:
    new (buf) QUICStreamIdBlockedFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::NEW_CONNECTION_ID:
    new (buf) QUICNewConnectionIdFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::STOP_SENDING:
    new (buf) QUICStopSendingFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::PATH_CHALLENGE:
    new (buf) QUICPathChallengeFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::PATH_RESPONSE:
    new (buf) QUICPathResponseFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::NEW_TOKEN:
    new (buf) QUICNewTokenFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::RETIRE_CONNECTION_ID:
    new (buf) QUICRetireConnectionIdFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  case QUICFrameType::HANDSHAKE_DONE:
    new (buf) QUICHandshakeDoneFrame(src, len, packet);
    return reinterpret_cast<QUICFrame *>(buf);
  default:
    // Unknown frame
    Debug("quic_frame_factory", "Unknown frame type %x", src[0]);
    return nullptr;
  }
}

const QUICFrame &
QUICFrameFactory::fast_create(const uint8_t *buf, size_t len, const QUICPacketR *packet)
{
  if (QUICFrame::type(buf) == QUICFrameType::UNKNOWN) {
    return this->_unknown_frame;
  }

  ptrdiff_t type_index = static_cast<ptrdiff_t>(QUICFrame::type(buf));
  QUICFrame *frame     = this->_reusable_frames[type_index];

  if (frame == nullptr) {
    frame = QUICFrameFactory::create(this->_buf_for_fast_create + (type_index * QUICFrame::MAX_INSTANCE_SIZE), buf, len, packet);
    if (frame != nullptr) {
      this->_reusable_frames[static_cast<ptrdiff_t>(QUICFrame::type(buf))] = frame;
    }
  } else {
    frame->parse(buf, len, packet);
  }

  return *frame;
}

QUICStreamFrame *
QUICFrameFactory::create_stream_frame(uint8_t *buf, Ptr<IOBufferBlock> &block, QUICStreamId stream_id, QUICOffset offset, bool last,
                                      bool has_offset_field, bool has_length_field, QUICFrameId id, QUICFrameGenerator *owner)
{
  Ptr<IOBufferBlock> new_block = make_ptr<IOBufferBlock>(block->clone());
  new (buf) QUICStreamFrame(new_block, stream_id, offset, last, has_offset_field, has_length_field, id, owner);
  return reinterpret_cast<QUICStreamFrame *>(buf);
}

QUICCryptoFrame *
QUICFrameFactory::create_crypto_frame(uint8_t *buf, Ptr<IOBufferBlock> &block, QUICOffset offset, QUICFrameId id,
                                      QUICFrameGenerator *owner)
{
  Ptr<IOBufferBlock> new_block = make_ptr<IOBufferBlock>(block->clone());
  new (buf) QUICCryptoFrame(new_block, offset, id, owner);
  return reinterpret_cast<QUICCryptoFrame *>(buf);
}

QUICAckFrame *
QUICFrameFactory::create_ack_frame(uint8_t *buf, QUICPacketNumber largest_acknowledged, uint64_t ack_delay,
                                   uint64_t first_ack_block, QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICAckFrame(largest_acknowledged, ack_delay, first_ack_block, id, owner);
  return reinterpret_cast<QUICAckFrame *>(buf);
}

QUICConnectionCloseFrame *
QUICFrameFactory::create_connection_close_frame(uint8_t *buf, uint16_t error_code, QUICFrameType frame_type,
                                                uint16_t reason_phrase_length, const char *reason_phrase, QUICFrameId id,
                                                QUICFrameGenerator *owner)
{
  new (buf) QUICConnectionCloseFrame(error_code, frame_type, reason_phrase_length, reason_phrase, id, owner);
  return reinterpret_cast<QUICConnectionCloseFrame *>(buf);
}

QUICConnectionCloseFrame *
QUICFrameFactory::create_connection_close_frame(uint8_t *buf, const QUICConnectionError &error, QUICFrameId id,
                                                QUICFrameGenerator *owner)
{
  ink_assert(error.cls == QUICErrorClass::TRANSPORT);
  if (error.msg) {
    return QUICFrameFactory::create_connection_close_frame(buf, error.code, error.frame_type(), strlen(error.msg), error.msg, id,
                                                           owner);
  } else {
    return QUICFrameFactory::create_connection_close_frame(buf, error.code, error.frame_type(), 0, nullptr, id, owner);
  }
}

QUICMaxDataFrame *
QUICFrameFactory::create_max_data_frame(uint8_t *buf, uint64_t maximum_data, QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICMaxDataFrame(maximum_data, id, owner);
  return reinterpret_cast<QUICMaxDataFrame *>(buf);
}
QUICMaxStreamDataFrame *
QUICFrameFactory::create_max_stream_data_frame(uint8_t *buf, QUICStreamId stream_id, uint64_t maximum_data, QUICFrameId id,
                                               QUICFrameGenerator *owner)
{
  new (buf) QUICMaxStreamDataFrame(stream_id, maximum_data, id, owner);
  return reinterpret_cast<QUICMaxStreamDataFrame *>(buf);
}

QUICMaxStreamsFrame *
QUICFrameFactory::create_max_streams_frame(uint8_t *buf, QUICStreamId maximum_streams, QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICMaxStreamsFrame(maximum_streams, id, owner);
  return reinterpret_cast<QUICMaxStreamsFrame *>(buf);
}

QUICPingFrame *
QUICFrameFactory::create_ping_frame(uint8_t *buf, QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICPingFrame(id, owner);
  return reinterpret_cast<QUICPingFrame *>(buf);
}

QUICPaddingFrame *
QUICFrameFactory::create_padding_frame(uint8_t *buf, size_t size, QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICPaddingFrame(size);
  return reinterpret_cast<QUICPaddingFrame *>(buf);
}

QUICPathChallengeFrame *
QUICFrameFactory::create_path_challenge_frame(uint8_t *buf, const uint8_t *data, QUICFrameId id, QUICFrameGenerator *owner)
{
  ats_unique_buf challenge_data = ats_unique_malloc(QUICPathChallengeFrame::DATA_LEN);
  memcpy(challenge_data.get(), data, QUICPathChallengeFrame::DATA_LEN);

  new (buf) QUICPathChallengeFrame(std::move(challenge_data), id, owner);
  return reinterpret_cast<QUICPathChallengeFrame *>(buf);
}

QUICPathResponseFrame *
QUICFrameFactory::create_path_response_frame(uint8_t *buf, const uint8_t *data, QUICFrameId id, QUICFrameGenerator *owner)
{
  ats_unique_buf response_data = ats_unique_malloc(QUICPathResponseFrame::DATA_LEN);
  memcpy(response_data.get(), data, QUICPathResponseFrame::DATA_LEN);

  new (buf) QUICPathResponseFrame(std::move(response_data), id, owner);
  return reinterpret_cast<QUICPathResponseFrame *>(buf);
}

QUICDataBlockedFrame *
QUICFrameFactory::create_data_blocked_frame(uint8_t *buf, QUICOffset offset, QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICDataBlockedFrame(offset, id, owner);
  return reinterpret_cast<QUICDataBlockedFrame *>(buf);
}

QUICStreamDataBlockedFrame *
QUICFrameFactory::create_stream_data_blocked_frame(uint8_t *buf, QUICStreamId stream_id, QUICOffset offset, QUICFrameId id,
                                                   QUICFrameGenerator *owner)
{
  new (buf) QUICStreamDataBlockedFrame(stream_id, offset, id, owner);
  return reinterpret_cast<QUICStreamDataBlockedFrame *>(buf);
}

QUICStreamIdBlockedFrame *
QUICFrameFactory::create_stream_id_blocked_frame(uint8_t *buf, QUICStreamId stream_id, QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICStreamIdBlockedFrame(stream_id, id, owner);
  return reinterpret_cast<QUICStreamIdBlockedFrame *>(buf);
}

QUICRstStreamFrame *
QUICFrameFactory::create_rst_stream_frame(uint8_t *buf, QUICStreamId stream_id, QUICAppErrorCode error_code,
                                          QUICOffset final_offset, QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICRstStreamFrame(stream_id, error_code, final_offset, id, owner);
  return reinterpret_cast<QUICRstStreamFrame *>(buf);
}

QUICRstStreamFrame *
QUICFrameFactory::create_rst_stream_frame(uint8_t *buf, QUICStreamError &error, QUICFrameId id, QUICFrameGenerator *owner)
{
  return QUICFrameFactory::create_rst_stream_frame(buf, error.stream->id(), error.code, error.stream->final_offset(), id, owner);
}

QUICStopSendingFrame *
QUICFrameFactory::create_stop_sending_frame(uint8_t *buf, QUICStreamId stream_id, QUICAppErrorCode error_code, QUICFrameId id,
                                            QUICFrameGenerator *owner)
{
  new (buf) QUICStopSendingFrame(stream_id, error_code, id, owner);
  return reinterpret_cast<QUICStopSendingFrame *>(buf);
}

QUICNewConnectionIdFrame *
QUICFrameFactory::create_new_connection_id_frame(uint8_t *buf, uint64_t sequence, uint64_t retire_prior_to,
                                                 QUICConnectionId connection_id, QUICStatelessResetToken stateless_reset_token,
                                                 QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICNewConnectionIdFrame(sequence, retire_prior_to, connection_id, stateless_reset_token, id, owner);
  return reinterpret_cast<QUICNewConnectionIdFrame *>(buf);
}

QUICNewTokenFrame *
QUICFrameFactory::create_new_token_frame(uint8_t *buf, const QUICResumptionToken &token, QUICFrameId id, QUICFrameGenerator *owner)
{
  uint64_t token_len       = token.length();
  ats_unique_buf token_buf = ats_unique_malloc(token_len);
  memcpy(token_buf.get(), token.buf(), token_len);

  new (buf) QUICNewTokenFrame(std::move(token_buf), token_len, id, owner);
  return reinterpret_cast<QUICNewTokenFrame *>(buf);
}

QUICRetireConnectionIdFrame *
QUICFrameFactory::create_retire_connection_id_frame(uint8_t *buf, uint64_t seq_num, QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICRetireConnectionIdFrame(seq_num, id, owner);
  return reinterpret_cast<QUICRetireConnectionIdFrame *>(buf);
}

QUICHandshakeDoneFrame *
QUICFrameFactory::create_handshake_done_frame(uint8_t *buf, QUICFrameId id, QUICFrameGenerator *owner)
{
  new (buf) QUICHandshakeDoneFrame(id, owner);
  return reinterpret_cast<QUICHandshakeDoneFrame *>(buf);
}
