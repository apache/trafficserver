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

ClassAllocator<QUICStreamFrame> quicStreamFrameAllocator("quicStreamFrameAllocator");
ClassAllocator<QUICCryptoFrame> quicCryptoFrameAllocator("quicCryptoFrameAllocator");
ClassAllocator<QUICAckFrame> quicAckFrameAllocator("quicAckFrameAllocator");
ClassAllocator<QUICPaddingFrame> quicPaddingFrameAllocator("quicPaddingFrameAllocator");
ClassAllocator<QUICRstStreamFrame> quicRstStreamFrameAllocator("quicRstStreamFrameAllocator");
ClassAllocator<QUICConnectionCloseFrame> quicConnectionCloseFrameAllocator("quicConnectionCloseFrameAllocator");
ClassAllocator<QUICApplicationCloseFrame> quicApplicationCloseFrameAllocator("quicApplicationCloseFrameAllocator");
ClassAllocator<QUICMaxDataFrame> quicMaxDataFrameAllocator("quicMaxDataFrameAllocator");
ClassAllocator<QUICMaxStreamDataFrame> quicMaxStreamDataFrameAllocator("quicMaxStreamDataFrameAllocator");
ClassAllocator<QUICMaxStreamIdFrame> quicMaxStreamIdFrameAllocator("quicMaxStreamDataIdAllocator");
ClassAllocator<QUICPingFrame> quicPingFrameAllocator("quicPingFrameAllocator");
ClassAllocator<QUICBlockedFrame> quicBlockedFrameAllocator("quicBlockedFrameAllocator");
ClassAllocator<QUICStreamBlockedFrame> quicStreamBlockedFrameAllocator("quicStreamBlockedFrameAllocator");
ClassAllocator<QUICStreamIdBlockedFrame> quicStreamIdBlockedFrameAllocator("quicStreamIdBlockedFrameAllocator");
ClassAllocator<QUICNewConnectionIdFrame> quicNewConnectionIdFrameAllocator("quicNewConnectionIdFrameAllocator");
ClassAllocator<QUICStopSendingFrame> quicStopSendingFrameAllocator("quicStopSendingFrameAllocator");
ClassAllocator<QUICPathChallengeFrame> quicPathChallengeFrameAllocator("quicPathChallengeFrameAllocator");
ClassAllocator<QUICPathResponseFrame> quicPathResponseFrameAllocator("quicPathResponseFrameAllocator");
ClassAllocator<QUICNewTokenFrame> quicNewTokenFrameAllocator("quicNewTokenFrameAllocator");
ClassAllocator<QUICRetireConnectionIdFrame> quicRetireConnectionIdFrameAllocator("quicRetireConnectionIdFrameAllocator");
ClassAllocator<QUICRetransmissionFrame> quicRetransmissionFrameAllocator("quicRetransmissionFrameAllocator");

#define LEFT_SPACE(pos) ((size_t)(buf + len - pos))
#define FRAME_SIZE(pos) (pos - buf)

// the pos will auto move forward . return true if the data vaild
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

  field = QUICIntUtil::read_QUICVariableInt(pos);
  pos += field_len;
  return true;
}

QUICFrameType
QUICFrame::type() const
{
  ink_assert("should no be called");
  return QUICFrameType::UNKNOWN;
}

bool
QUICFrame::is_probing_frame() const
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
  } else if (static_cast<uint8_t>(QUICFrameType::STREAM) <= buf[0] && buf[0] < static_cast<uint8_t>(QUICFrameType::CRYPTO)) {
    return QUICFrameType::STREAM;
  } else if (static_cast<uint8_t>(QUICFrameType::ACK) <= buf[0] && buf[0] < static_cast<uint8_t>(QUICFrameType::UNKNOWN)) {
    return QUICFrameType::ACK;
  } else {
    return static_cast<QUICFrameType>(buf[0]);
  }
}

void
QUICFrame::reset(const uint8_t *buf, size_t len)
{
  ink_assert(0);
}

int
QUICFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "| %s size=%zu", QUICDebugNames::frame_type(this->type()), this->size());
}

QUICFrame *
QUICFrame::split(size_t size)
{
  return nullptr;
}

bool
QUICFrame::valid() const
{
  return this->_valid;
}

//
// STREAM Frame
//

QUICStreamFrame::QUICStreamFrame(ats_unique_buf data, size_t data_len, QUICStreamId stream_id, QUICOffset offset, bool last,
                                 bool has_offset_field, bool has_length_field, QUICFrameId id, QUICFrameGenerator *owner)
  : QUICFrame(id, owner),
    _data(std::move(data)),
    _data_len(data_len),
    _stream_id(stream_id),
    _offset(offset),
    _fin(last),
    _has_offset_field(has_offset_field),
    _has_length_field(has_length_field)
{
}

QUICStreamFrame::QUICStreamFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICStreamFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);

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

  if (this->_has_length_field && !read_varint(pos, LEFT_SPACE(pos), this->_data_len, field_len)) {
    return;
  }

  if (!this->_has_length_field) {
    this->_data_len = LEFT_SPACE(pos);
  }
  if (LEFT_SPACE(pos) < this->_data_len) {
    return;
  }

  this->_valid = true;
  this->_data  = ats_unique_malloc(this->_data_len);
  memcpy(this->_data.get(), pos, this->_data_len);
  pos += this->_data_len;
  this->_size = FRAME_SIZE(pos);
}

QUICFrame *
QUICStreamFrame::split(size_t size)
{
  size_t header_len = 1 + QUICVariableInt::size(this->_stream_id);
  if (this->_has_offset_field) {
    header_len += QUICVariableInt::size(this->_offset);
  }
  if (this->_has_length_field) {
    header_len += QUICVariableInt::size(this->_data_len);
  }

  if (size <= header_len) {
    return nullptr;
  }
  bool fin = this->has_fin_flag();

  ink_assert(size < this->size());

  size_t data_len = size - header_len;
  size_t buf2_len = this->data_length() - data_len;

  ats_unique_buf buf  = ats_unique_malloc(data_len);
  ats_unique_buf buf2 = ats_unique_malloc(buf2_len);
  memcpy(buf.get(), this->data(), data_len);
  memcpy(buf2.get(), this->data() + data_len, buf2_len);

  if (this->has_offset_field()) {
    this->_offset = this->offset();
  }

  if (this->has_length_field()) {
    this->_data_len = data_len;
  }

  this->_fin       = false;
  this->_data      = std::move(buf);
  this->_stream_id = this->stream_id();

  QUICStreamFrame *frame = quicStreamFrameAllocator.alloc();
  new (frame) QUICStreamFrame(std::move(buf2), buf2_len, this->stream_id(), this->offset() + this->data_length(), fin,
                              this->_has_offset_field, this->_has_length_field, this->_id, this->_owner);

  return frame;
}

QUICFrameUPtr
QUICStreamFrame::clone() const
{
  return QUICFrameFactory::create_stream_frame(this->data(), this->data_length(), this->stream_id(), this->offset(),
                                               this->has_fin_flag(), this->has_offset_field(), this->has_length_field(), this->_id,
                                               this->_owner);
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

  size_t size = 1;
  size += QUICVariableInt::size(this->_stream_id);
  if (this->_has_offset_field) {
    size += QUICVariableInt::size(this->_offset);
  }

  if (this->_has_length_field) {
    size += QUICVariableInt::size(this->_data_len);
  }

  size += this->_data_len;
  return size;
}

size_t
QUICStreamFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  return this->store(buf, len, limit, true);
}

int
QUICStreamFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "| STREAM size=%zu id=%" PRIu64 " offset=%" PRIu64 " data_len=%" PRIu64 " fin=%d", this->size(),
                  this->stream_id(), this->offset(), this->data_length(), this->has_fin_flag());
}

size_t
QUICStreamFrame::store(uint8_t *buf, size_t *len, size_t limit, bool include_length_field) const
{
  if (limit < this->size()) {
    return 0;
  }

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

  // Stream Data (*)
  memcpy(buf + *len, this->data(), this->data_length());
  *len += this->data_length();

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
  return this->_data_len;
}

const uint8_t *
QUICStreamFrame::data() const
{
  return this->_data.get();
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

QUICCryptoFrame::QUICCryptoFrame(ats_unique_buf data, size_t data_len, QUICOffset offset, QUICFrameId id, QUICFrameGenerator *owner)
  : QUICFrame(id, owner), _offset(offset), _data_len(data_len), _data(std::move(data))
{
}

QUICCryptoFrame::QUICCryptoFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICCryptoFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_offset, field_len)) {
    return;
  }

  if (!read_varint(pos, LEFT_SPACE(pos), this->_data_len, field_len)) {
    return;
  }

  if (LEFT_SPACE(pos) < this->_data_len) {
    return;
  }

  this->_valid = true;
  this->_data  = ats_unique_malloc(this->_data_len);
  memcpy(this->_data.get(), pos, this->_data_len);
  this->_size = FRAME_SIZE(pos);
}

QUICFrame *
QUICCryptoFrame::split(size_t size)
{
  size_t header_len = 1 + QUICVariableInt::size(this->_offset) + QUICVariableInt::size(this->_data_len);
  if (size <= header_len) {
    return nullptr;
  }

  ink_assert(size < this->size());

  size_t data_len = size - header_len;
  size_t buf2_len = this->data_length() - data_len;

  ats_unique_buf buf  = ats_unique_malloc(data_len);
  ats_unique_buf buf2 = ats_unique_malloc(buf2_len);
  memcpy(buf.get(), this->data(), data_len);
  memcpy(buf2.get(), this->data() + data_len, buf2_len);

  this->_offset   = this->offset();
  this->_data_len = data_len;
  this->_data     = std::move(buf);

  this->reset(nullptr, 0);

  QUICCryptoFrame *frame = quicCryptoFrameAllocator.alloc();
  new (frame) QUICCryptoFrame(std::move(buf2), buf2_len, this->offset() + this->data_length(), this->_id, this->_owner);

  return frame;
}

QUICFrameUPtr
QUICCryptoFrame::clone() const
{
  return QUICFrameFactory::create_crypto_frame(this->data(), this->data_length(), this->offset(), this->_id, this->_owner);
}

QUICFrameType
QUICCryptoFrame::type() const
{
  return QUICFrameType::CRYPTO;
}

size_t
QUICCryptoFrame::size() const
{
  return 1 + this->_data_len + QUICVariableInt::size(this->_offset) + QUICVariableInt::size(this->_data_len);
}

int
QUICCryptoFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "| CRYPTO size=%zu offset=%" PRIu64 " data_len=%" PRIu64, this->size(), this->offset(),
                  this->data_length());
}

size_t
QUICCryptoFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  // Frame Type
  buf[0] = static_cast<uint8_t>(QUICFrameType::CRYPTO);
  *len   = 1;

  size_t n;

  // Offset (i)
  QUICTypeUtil::write_QUICOffset(this->offset(), buf + *len, &n);
  *len += n;

  // Length (i)
  QUICIntUtil::write_QUICVariableInt(this->data_length(), buf + *len, &n);
  *len += n;

  // Crypto Data (*)
  memcpy(buf + *len, this->data(), this->data_length());
  *len += this->data_length();

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
  return this->_data_len;
}

const uint8_t *
QUICCryptoFrame::data() const
{
  return this->_data.get();
}

//
// ACK frame
//

QUICAckFrame::QUICAckFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICAckFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;
  bool has_ecn = (buf[0] == 0x1b);

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_largest_acknowledged, field_len)) {
    return;
  }

  if (!read_varint(pos, LEFT_SPACE(pos), this->_ack_delay, field_len)) {
    return;
  }

  size_t ack_block_count = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), ack_block_count, field_len)) {
    return;
  }

  size_t first_ack_block = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), first_ack_block, field_len)) {
    return;
  }

  this->_ack_block_section = new AckBlockSection(first_ack_block);
  for (size_t i = 0; i < ack_block_count; i++) {
    size_t gap           = 0;
    size_t add_ack_block = 0;

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

QUICFrameUPtr
QUICAckFrame::clone() const
{
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> newframe = QUICFrameFactory::create_ack_frame(
    this->largest_acknowledged(), this->ack_delay(), this->ack_block_section()->first_ack_block(), this->_id, this->_owner);

  for (auto &ack_block : *this->ack_block_section()) {
    newframe->ack_block_section()->add_ack_block({ack_block.gap(), ack_block.length()});
  }

  return newframe;
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

size_t
QUICAckFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  uint8_t *p = buf;
  size_t n;
  *p = static_cast<uint8_t>(QUICFrameType::ACK);
  ++p;

  QUICIntUtil::write_QUICVariableInt(this->_largest_acknowledged, p, &n);
  p += n;
  QUICIntUtil::write_QUICVariableInt(this->_ack_delay, p, &n);
  p += n;
  QUICIntUtil::write_QUICVariableInt(this->ack_block_count(), p, &n);
  p += n;

  ink_assert(limit >= static_cast<size_t>(p - buf));
  limit -= (p - buf);
  this->_ack_block_section->store(p, &n, limit);
  p += n;

  *len = p - buf;

  return *len;
}

int
QUICAckFrame::debug_msg(char *msg, size_t msg_len) const
{
  int len = snprintf(msg, msg_len, "| ACK size=%zu largest_acked=%" PRIu64 " delay=%" PRIu64 " block_count=%" PRIu64, this->size(),
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

size_t
QUICAckFrame::AckBlockSection::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;

  QUICIntUtil::write_QUICVariableInt(this->_first_ack_block, p, &n);
  p += n;

  for (auto &&block : *this) {
    QUICIntUtil::write_QUICVariableInt(block.gap(), p, &n);
    p += n;
    QUICIntUtil::write_QUICVariableInt(block.length(), p, &n);
    p += n;
  }

  *len = p - buf;

  return *len;
}

uint64_t
QUICAckFrame::AckBlockSection::first_ack_block() const
{
  return this->_first_ack_block;
}

void
QUICAckFrame::AckBlockSection::add_ack_block(AckBlock block)
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
// RST_STREAM frame
//

QUICRstStreamFrame::QUICRstStreamFrame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICOffset final_offset, QUICFrameId id,
                                       QUICFrameGenerator *owner)
  : QUICFrame(id, owner), _stream_id(stream_id), _error_code(error_code), _final_offset(final_offset)
{
}

QUICRstStreamFrame::QUICRstStreamFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICRstStreamFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = 1 + const_cast<uint8_t *>(buf);

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_stream_id, field_len)) {
    return;
  }

  if (LEFT_SPACE(pos) < 2) {
    return;
  }

  this->_error_code = QUICIntUtil::read_nbytes_as_uint(pos, 2);
  pos += 2;

  if (!read_varint(pos, LEFT_SPACE(pos), this->_final_offset, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameUPtr
QUICRstStreamFrame::clone() const
{
  return QUICFrameFactory::create_rst_stream_frame(this->stream_id(), this->error_code(), this->final_offset(), this->_id,
                                                   this->_owner);
}

QUICFrameType
QUICRstStreamFrame::type() const
{
  return QUICFrameType::RST_STREAM;
}

size_t
QUICRstStreamFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return 1 + QUICVariableInt::size(this->_stream_id) + sizeof(QUICAppErrorCode) + QUICVariableInt::size(this->_final_offset);
}

size_t
QUICRstStreamFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::RST_STREAM);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, p, &n);
  p += n;
  QUICTypeUtil::write_QUICAppErrorCode(this->_error_code, p, &n);
  p += n;
  QUICTypeUtil::write_QUICOffset(this->_final_offset, p, &n);
  p += n;

  *len = p - buf;

  return *len;
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

QUICFrameUPtr
QUICPingFrame::clone() const
{
  return QUICFrameFactory::create_ping_frame(this->_id, this->_owner);
}

QUICPingFrame::QUICPingFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICPingFrame::parse(const uint8_t *buf, size_t len)
{
  this->_valid = true;
  this->_size  = 1;
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

size_t
QUICPingFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  *len   = this->size();
  buf[0] = static_cast<uint8_t>(QUICFrameType::PING);
  return *len;
}

//
// PADDING frame
//
QUICPaddingFrame::QUICPaddingFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICPaddingFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  this->_valid = true;
  this->_size  = 1;
}

QUICFrameUPtr
QUICPaddingFrame::clone() const
{
  ink_assert(!"You shouldn't clone padding frames");
  return QUICFrameFactory::create_null_frame();
}

QUICFrameType
QUICPaddingFrame::type() const
{
  return QUICFrameType::PADDING;
}

size_t
QUICPaddingFrame::size() const
{
  return 1;
}

bool
QUICPaddingFrame::is_probing_frame() const
{
  return true;
}

size_t
QUICPaddingFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  buf[0] = static_cast<uint8_t>(QUICFrameType::PADDING);
  *len   = 1;
  return *len;
}

//
// CONNECTION_CLOSE frame
//
QUICConnectionCloseFrame::QUICConnectionCloseFrame(uint16_t error_code, QUICFrameType frame_type, uint64_t reason_phrase_length,
                                                   const char *reason_phrase, QUICFrameId id, QUICFrameGenerator *owner)
  : QUICFrame(id, owner),
    _error_code(error_code),
    _frame_type(frame_type),
    _reason_phrase_length(reason_phrase_length),
    _reason_phrase(reason_phrase)
{
}

QUICConnectionCloseFrame::QUICConnectionCloseFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICConnectionCloseFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  if (LEFT_SPACE(pos) < 2) {
    return;
  }

  this->_error_code = QUICIntUtil::read_nbytes_as_uint(pos, 2);
  pos += 2;

  size_t field_len = 0;
  uint64_t field   = 0;
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

  if (!read_varint(pos, LEFT_SPACE(pos), this->_reason_phrase_length, field_len)) {
    return;
  }

  if (LEFT_SPACE(pos) < this->_reason_phrase_length) {
    return;
  }

  this->_valid         = true;
  this->_reason_phrase = reinterpret_cast<const char *>(pos);
  pos += this->_reason_phrase_length;
  this->_size = FRAME_SIZE(pos);
}

QUICFrameUPtr
QUICConnectionCloseFrame::clone() const
{
  return QUICFrameFactory::create_connection_close_frame(this->error_code(), this->frame_type(), this->reason_phrase_length(),
                                                         this->reason_phrase(), this->_id, this->_owner);
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

  return 1 + sizeof(QUICTransErrorCode) + QUICVariableInt::size(sizeof(QUICFrameType)) +
         QUICVariableInt::size(this->_reason_phrase_length) + this->_reason_phrase_length;
}

/**
   Store CONNECTION_CLOSE frame in buffer.

   PADDING frame in Frame Type field means frame type that triggered the error is unknown.
   When `_frame_type` is QUICFrameType::UNKNOWN, it's converted to QUICFrameType::PADDING (0x0).
 */
size_t
QUICConnectionCloseFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::CONNECTION_CLOSE);
  ++p;

  // Error Code (16)
  QUICTypeUtil::write_QUICTransErrorCode(this->_error_code, p, &n);
  p += n;

  // Frame Type (i)
  QUICFrameType frame_type = this->_frame_type;
  if (frame_type == QUICFrameType::UNKNOWN) {
    frame_type = QUICFrameType::PADDING;
  }
  *p = static_cast<uint8_t>(frame_type);
  ++p;

  // Reason Phrase Length (i)
  QUICIntUtil::write_QUICVariableInt(this->_reason_phrase_length, p, &n);
  p += n;

  // Reason Phrase (*)
  if (this->_reason_phrase_length > 0) {
    memcpy(p, this->_reason_phrase, this->_reason_phrase_length);
    p += this->_reason_phrase_length;
  }

  *len = p - buf;
  return *len;
}

int
QUICConnectionCloseFrame::debug_msg(char *msg, size_t msg_len) const
{
  int len =
    snprintf(msg, msg_len, "| CONNECTION_CLOSE size=%zu code=%s (0x%" PRIx16 ") frame=%s", this->size(),
             QUICDebugNames::error_code(this->error_code()), this->error_code(), QUICDebugNames::frame_type(this->frame_type()));

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
// APPLICATION_CLOSE frame
//
QUICApplicationCloseFrame::QUICApplicationCloseFrame(QUICAppErrorCode error_code, uint64_t reason_phrase_length,
                                                     const char *reason_phrase, QUICFrameId id, QUICFrameGenerator *owner)
  : QUICFrame(id, owner)
{
  this->_error_code           = error_code;
  this->_reason_phrase_length = reason_phrase_length;
  this->_reason_phrase        = reason_phrase;
}

QUICApplicationCloseFrame::QUICApplicationCloseFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICApplicationCloseFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  if (LEFT_SPACE(pos) < 2) {
    return;
  }

  this->_error_code = QUICIntUtil::read_nbytes_as_uint(pos, 2);
  pos += 2;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_reason_phrase_length, field_len)) {
    return;
  }

  if (LEFT_SPACE(pos) < this->_reason_phrase_length) {
    return;
  }

  this->_valid         = true;
  this->_reason_phrase = reinterpret_cast<const char *>(pos);
  this->_size          = FRAME_SIZE(pos) + this->_reason_phrase_length;
}

QUICFrameUPtr
QUICApplicationCloseFrame::clone() const
{
  return QUICFrameFactory::create_application_close_frame(this->error_code(), this->reason_phrase_length(), this->reason_phrase(),
                                                          this->_id, this->_owner);
}

QUICFrameType
QUICApplicationCloseFrame::type() const
{
  return QUICFrameType::APPLICATION_CLOSE;
}

size_t
QUICApplicationCloseFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return 1 + sizeof(QUICTransErrorCode) + QUICVariableInt::size(this->_reason_phrase_length) + this->_reason_phrase_length;
}

size_t
QUICApplicationCloseFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::APPLICATION_CLOSE);
  ++p;
  QUICTypeUtil::write_QUICAppErrorCode(this->_error_code, p, &n);
  p += n;
  QUICIntUtil::write_QUICVariableInt(this->_reason_phrase_length, p, &n);
  p += n;
  if (this->_reason_phrase_length > 0) {
    memcpy(p, this->_reason_phrase, this->_reason_phrase_length);
    p += this->_reason_phrase_length;
  }

  *len = p - buf;
  return *len;
}

int
QUICApplicationCloseFrame::debug_msg(char *msg, size_t msg_len) const
{
  int len =
    snprintf(msg, msg_len, "| APPLICATION_CLOSE size=%zu code=%s", this->size(), QUICDebugNames::error_code(this->error_code()));

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

QUICAppErrorCode
QUICApplicationCloseFrame::error_code() const
{
  return this->_error_code;
}

uint64_t
QUICApplicationCloseFrame::reason_phrase_length() const
{
  return this->_reason_phrase_length;
}

const char *
QUICApplicationCloseFrame::reason_phrase() const
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

QUICMaxDataFrame::QUICMaxDataFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICMaxDataFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = 1 + const_cast<uint8_t *>(buf);

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_maximum_data, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameUPtr
QUICMaxDataFrame::clone() const
{
  return QUICFrameFactory::create_max_data_frame(this->maximum_data(), this->_id, this->_owner);
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

size_t
QUICMaxDataFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::MAX_DATA);
  ++p;
  QUICTypeUtil::write_QUICMaxData(this->_maximum_data, p, &n);
  p += n;

  *len = p - buf;
  return *len;
}

int
QUICMaxDataFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "| MAX_DATA size=%zu maximum=%" PRIu64, this->size(), this->maximum_data());
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

QUICMaxStreamDataFrame::QUICMaxStreamDataFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICMaxStreamDataFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

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

QUICFrameUPtr
QUICMaxStreamDataFrame::clone() const
{
  return QUICFrameFactory::create_max_stream_data_frame(this->stream_id(), this->maximum_stream_data(), this->_id, this->_owner);
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

size_t
QUICMaxStreamDataFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }
  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::MAX_STREAM_DATA);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, p, &n);
  p += n;
  QUICTypeUtil::write_QUICMaxData(this->_maximum_stream_data, p, &n);
  p += n;

  *len = p - buf;
  return *len;
}

int
QUICMaxStreamDataFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "| MAX_STREAM_DATA size=%zu maximum=%" PRIu64, this->size(), this->maximum_stream_data());
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
// MAX_STREAM_ID
//
QUICMaxStreamIdFrame::QUICMaxStreamIdFrame(QUICStreamId maximum_stream_id, QUICFrameId id, QUICFrameGenerator *owner)
  : QUICFrame(id, owner)
{
  this->_maximum_stream_id = maximum_stream_id;
}

QUICMaxStreamIdFrame::QUICMaxStreamIdFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICMaxStreamIdFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_maximum_stream_id, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameUPtr
QUICMaxStreamIdFrame::clone() const
{
  return QUICFrameFactory::create_max_stream_id_frame(this->maximum_stream_id(), this->_id, this->_owner);
}

QUICFrameType
QUICMaxStreamIdFrame::type() const
{
  return QUICFrameType::MAX_STREAM_ID;
}

size_t
QUICMaxStreamIdFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_maximum_stream_id);
}

size_t
QUICMaxStreamIdFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::MAX_STREAM_ID);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_maximum_stream_id, p, &n);
  p += n;

  *len = p - buf;
  return *len;
}

QUICStreamId
QUICMaxStreamIdFrame::maximum_stream_id() const
{
  return this->_maximum_stream_id;
}

//
// BLOCKED frame
//
QUICBlockedFrame::QUICBlockedFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICBlockedFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_offset, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameUPtr
QUICBlockedFrame::clone() const
{
  return QUICFrameFactory::create_blocked_frame(this->offset(), this->_id, this->_owner);
}

QUICFrameType
QUICBlockedFrame::type() const
{
  return QUICFrameType::BLOCKED;
}

size_t
QUICBlockedFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->offset());
}

size_t
QUICBlockedFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;

  *p = static_cast<uint8_t>(QUICFrameType::BLOCKED);
  ++p;
  QUICTypeUtil::write_QUICOffset(this->_offset, p, &n);
  p += n;

  *len = p - buf;

  return *len;
}

QUICOffset
QUICBlockedFrame::offset() const
{
  return this->_offset;
}

//
// STREAM_BLOCKED frame
//
QUICStreamBlockedFrame::QUICStreamBlockedFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICStreamBlockedFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

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

QUICFrameUPtr
QUICStreamBlockedFrame::clone() const
{
  return QUICFrameFactory::create_stream_blocked_frame(this->stream_id(), this->offset(), this->_id, this->_owner);
}

QUICFrameType
QUICStreamBlockedFrame::type() const
{
  return QUICFrameType::STREAM_BLOCKED;
}

size_t
QUICStreamBlockedFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_offset) + QUICVariableInt::size(this->_stream_id);
}

size_t
QUICStreamBlockedFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::STREAM_BLOCKED);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, p, &n);
  p += n;
  QUICTypeUtil::write_QUICOffset(this->_offset, p, &n);
  p += n;

  *len = p - buf;

  return *len;
}

QUICStreamId
QUICStreamBlockedFrame::stream_id() const
{
  return this->_stream_id;
}

QUICOffset
QUICStreamBlockedFrame::offset() const
{
  return this->_offset;
}

//
// STREAM_ID_BLOCKED frame
//
QUICStreamIdBlockedFrame::QUICStreamIdBlockedFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICStreamIdBlockedFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_stream_id, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameUPtr
QUICStreamIdBlockedFrame::clone() const
{
  return QUICFrameFactory::create_stream_id_blocked_frame(this->stream_id(), this->_id, this->_owner);
}

QUICFrameType
QUICStreamIdBlockedFrame::type() const
{
  return QUICFrameType::STREAM_ID_BLOCKED;
}

size_t
QUICStreamIdBlockedFrame::size() const
{
  if (this->_size) {
    return this->_size;
  }

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_stream_id);
}

size_t
QUICStreamIdBlockedFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;

  *p = static_cast<uint8_t>(QUICFrameType::STREAM_ID_BLOCKED);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, p, &n);
  p += n;

  *len = p - buf;
  return *len;
}

QUICStreamId
QUICStreamIdBlockedFrame::stream_id() const
{
  return this->_stream_id;
}

//
// NEW_CONNECTION_ID frame
//
QUICNewConnectionIdFrame::QUICNewConnectionIdFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICNewConnectionIdFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  if (LEFT_SPACE(pos) < 1) {
    return;
  }

  size_t cid_len = *pos;
  pos += 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_sequence, field_len)) {
    return;
  }

  if (LEFT_SPACE(pos) < cid_len) {
    return;
  }

  this->_connection_id = QUICTypeUtil::read_QUICConnectionId(pos, cid_len);
  pos += cid_len;

  if (LEFT_SPACE(pos) < 16) {
    return;
  }

  this->_stateless_reset_token = QUICStatelessResetToken(pos);
  this->_valid                 = true;
  this->_size                  = FRAME_SIZE(pos) + 16;
}

QUICFrameUPtr
QUICNewConnectionIdFrame::clone() const
{
  // FIXME: Connection ID and Stateless rese token have to be the same
  return QUICFrameFactory::create_new_connection_id_frame(this->sequence(), this->connection_id(), this->stateless_reset_token(),
                                                          this->_id, this->_owner);
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

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_sequence) + 1 + this->_connection_id.length() + 16;
}

size_t
QUICNewConnectionIdFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::NEW_CONNECTION_ID);
  ++p;
  *p = this->_connection_id.length();
  p += 1;
  QUICIntUtil::write_QUICVariableInt(this->_sequence, p, &n);
  p += n;
  QUICTypeUtil::write_QUICConnectionId(this->_connection_id, p, &n);
  p += n;
  memcpy(p, this->_stateless_reset_token.buf(), QUICStatelessResetToken::LEN);
  p += QUICStatelessResetToken::LEN;

  *len = p - buf;
  return *len;
}

int
QUICNewConnectionIdFrame::debug_msg(char *msg, size_t msg_len) const
{
  char cid_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
  this->connection_id().hex(cid_str, QUICConnectionId::MAX_HEX_STR_LENGTH);

  return snprintf(msg, msg_len, "| NEW_CONNECTION_ID size=%zu seq=%" PRIu64 " cid=0x%s", this->size(), this->sequence(), cid_str);
}

uint64_t
QUICNewConnectionIdFrame::sequence() const
{
  return this->_sequence;
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

QUICStopSendingFrame::QUICStopSendingFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICStopSendingFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_stream_id, field_len)) {
    return;
  }

  if (LEFT_SPACE(pos) < 2) {
    return;
  }

  this->_error_code = static_cast<QUICAppErrorCode>(QUICIntUtil::read_nbytes_as_uint(pos, 2));
  this->_valid      = true;
  this->_size       = FRAME_SIZE(pos) + 2;
}

QUICFrameUPtr
QUICStopSendingFrame::clone() const
{
  return QUICFrameFactory::create_stop_sending_frame(this->stream_id(), this->error_code(), this->_id, this->_owner);
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

  return sizeof(QUICFrameType) + QUICVariableInt::size(this->_stream_id) + sizeof(QUICAppErrorCode);
}

size_t
QUICStopSendingFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::STOP_SENDING);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, p, &n);
  p += n;
  QUICTypeUtil::write_QUICAppErrorCode(this->_error_code, p, &n);
  p += n;

  *len = p - buf;
  return *len;
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
QUICPathChallengeFrame::QUICPathChallengeFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICPathChallengeFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  if (LEFT_SPACE(pos) < QUICPathChallengeFrame::DATA_LEN) {
    return;
  }

  this->_data = ats_unique_malloc(QUICPathChallengeFrame::DATA_LEN);
  memcpy(this->_data.get(), pos, QUICPathChallengeFrame::DATA_LEN);
  this->_valid = true;
  this->_size  = FRAME_SIZE(pos) + QUICPathChallengeFrame::DATA_LEN;
}

QUICFrameUPtr
QUICPathChallengeFrame::clone() const
{
  return QUICFrameFactory::create_path_challenge_frame(this->data(), this->_id, this->_owner);
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

size_t
QUICPathChallengeFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  *len = this->size();

  buf[0] = static_cast<uint8_t>(QUICFrameType::PATH_CHALLENGE);
  memcpy(buf + 1, this->data(), QUICPathChallengeFrame::DATA_LEN);

  return *len;
}

const uint8_t *
QUICPathChallengeFrame::data() const
{
  return this->_data.get();
}

//
// PATH_RESPONSE frame
//
QUICPathResponseFrame::QUICPathResponseFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICPathResponseFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  if (LEFT_SPACE(pos) < QUICPathChallengeFrame::DATA_LEN) {
    return;
  }

  this->_data = ats_unique_malloc(QUICPathChallengeFrame::DATA_LEN);
  memcpy(this->_data.get(), pos, QUICPathChallengeFrame::DATA_LEN);
  this->_valid = true;
  this->_size  = FRAME_SIZE(pos) + QUICPathChallengeFrame::DATA_LEN;
}

QUICFrameUPtr
QUICPathResponseFrame::clone() const
{
  return QUICFrameFactory::create_path_response_frame(this->data(), this->_id, this->_owner);
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

size_t
QUICPathResponseFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  *len = this->size();

  buf[0] = static_cast<uint8_t>(QUICFrameType::PATH_RESPONSE);
  memcpy(buf + 1, this->data(), QUICPathResponseFrame::DATA_LEN);

  return *len;
}

const uint8_t *
QUICPathResponseFrame::data() const
{
  return this->_data.get();
}

//
// QUICNewTokenFrame
//
QUICNewTokenFrame::QUICNewTokenFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICNewTokenFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

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

QUICFrameUPtr
QUICNewTokenFrame::clone() const
{
  // clone() will be removed when all frame generators are responsible for retransmittion
  return QUICFrameFactory::create_null_frame();
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

size_t
QUICNewTokenFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  uint8_t *p = buf;

  // Type (i)
  *p = static_cast<uint8_t>(QUICFrameType::NEW_TOKEN);
  ++p;

  // Token Length (i)
  size_t n;
  QUICIntUtil::write_QUICVariableInt(this->_token_length, p, &n);
  p += n;

  // Token (*)
  memcpy(p, this->token(), this->token_length());
  p += this->token_length();

  *len = p - buf;
  return *len;
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
QUICRetireConnectionIdFrame::QUICRetireConnectionIdFrame(const uint8_t *buf, size_t len)
{
  this->parse(buf, len);
}

void
QUICRetireConnectionIdFrame::parse(const uint8_t *buf, size_t len)
{
  ink_assert(len >= 1);
  uint8_t *pos = const_cast<uint8_t *>(buf) + 1;

  size_t field_len = 0;
  if (!read_varint(pos, LEFT_SPACE(pos), this->_seq_num, field_len)) {
    return;
  }

  this->_valid = true;
  this->_size  = FRAME_SIZE(pos);
}

QUICFrameUPtr
QUICRetireConnectionIdFrame::clone() const
{
  return QUICFrameFactory::create_retire_connection_id_frame(this->seq_num(), this->_id, this->_owner);
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

size_t
QUICRetireConnectionIdFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  if (limit < this->size()) {
    return 0;
  }

  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::RETIRE_CONNECTION_ID);
  ++p;
  QUICIntUtil::write_QUICVariableInt(this->_seq_num, p, &n);
  p += n;

  *len = p - buf;

  return *len;
}

int
QUICRetireConnectionIdFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "| RETIRE_CONNECTION_ID size=%zu seq_num=%" PRIu64, this->size(), this->seq_num());
}

uint64_t
QUICRetireConnectionIdFrame::seq_num() const
{
  return this->_seq_num;
}

//
// QUICRetransmissionFrame
//
QUICRetransmissionFrame::QUICRetransmissionFrame(QUICFrameUPtr original_frame, const QUICPacket &original_packet)
  : QUICFrame(original_frame->id(), original_frame->generated_by()), _packet_type(original_packet.type())
{
  this->_frame = std::move(original_frame);
}

QUICFrameUPtr
QUICRetransmissionFrame::clone() const
{
  ink_assert(!"Retransmission frames shouldn't be cloned");
  return QUICFrameFactory::create_null_frame();
}

QUICFrameType
QUICRetransmissionFrame::type() const
{
  return this->_frame->type();
}

size_t
QUICRetransmissionFrame::size() const
{
  return this->_frame->size();
}

size_t
QUICRetransmissionFrame::store(uint8_t *buf, size_t *len, size_t limit) const
{
  return this->_frame->store(buf, len, limit);
}

int
QUICRetransmissionFrame::debug_msg(char *msg, size_t msg_len) const
{
  return snprintf(msg, msg_len, "| %s size=%zu (retransmission)", QUICDebugNames::frame_type(this->type()), this->size());
}

QUICPacketType
QUICRetransmissionFrame::packet_type() const
{
  return this->_packet_type;
}

QUICFrame *
QUICRetransmissionFrame::split(size_t size)
{
  if (this->_frame->type() != QUICFrameType::STREAM) {
    return nullptr;
  }

  return this->_frame->split(size);
}

//
// QUICFrameFactory
//

QUICFrameUPtr
QUICFrameFactory::create_null_frame()
{
  return {nullptr, &QUICFrameDeleter::delete_null_frame};
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_null_ack_frame()
{
  return {nullptr, &QUICFrameDeleter::delete_null_frame};
}

QUICFrameUPtr
QUICFrameFactory::create(const uint8_t *buf, size_t len)
{
  QUICFrame *frame;

  switch (QUICFrame::type(buf)) {
  case QUICFrameType::STREAM:
    frame = quicStreamFrameAllocator.alloc();
    new (frame) QUICStreamFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_stream_frame);
  case QUICFrameType::CRYPTO:
    frame = quicCryptoFrameAllocator.alloc();
    new (frame) QUICCryptoFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_crypto_frame);
  case QUICFrameType::ACK:
    frame = quicAckFrameAllocator.alloc();
    new (frame) QUICAckFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_ack_frame);
  case QUICFrameType::PADDING:
    frame = quicPaddingFrameAllocator.alloc();
    new (frame) QUICPaddingFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_padding_frame);
  case QUICFrameType::RST_STREAM:
    frame = quicRstStreamFrameAllocator.alloc();
    new (frame) QUICRstStreamFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_rst_stream_frame);
  case QUICFrameType::CONNECTION_CLOSE:
    frame = quicConnectionCloseFrameAllocator.alloc();
    new (frame) QUICConnectionCloseFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_connection_close_frame);
  case QUICFrameType::APPLICATION_CLOSE:
    frame = quicApplicationCloseFrameAllocator.alloc();
    new (frame) QUICApplicationCloseFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_application_close_frame);
  case QUICFrameType::MAX_DATA:
    frame = quicMaxDataFrameAllocator.alloc();
    new (frame) QUICMaxDataFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_max_data_frame);
  case QUICFrameType::MAX_STREAM_DATA:
    frame = quicMaxStreamDataFrameAllocator.alloc();
    new (frame) QUICMaxStreamDataFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_max_stream_data_frame);
  case QUICFrameType::MAX_STREAM_ID:
    frame = quicMaxStreamIdFrameAllocator.alloc();
    new (frame) QUICMaxStreamIdFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_max_stream_id_frame);
  case QUICFrameType::PING:
    frame = quicPingFrameAllocator.alloc();
    new (frame) QUICPingFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_ping_frame);
  case QUICFrameType::BLOCKED:
    frame = quicBlockedFrameAllocator.alloc();
    new (frame) QUICBlockedFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_blocked_frame);
  case QUICFrameType::STREAM_BLOCKED:
    frame = quicStreamBlockedFrameAllocator.alloc();
    new (frame) QUICStreamBlockedFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_stream_blocked_frame);
  case QUICFrameType::STREAM_ID_BLOCKED:
    frame = quicStreamIdBlockedFrameAllocator.alloc();
    new (frame) QUICStreamIdBlockedFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_stream_id_blocked_frame);
  case QUICFrameType::NEW_CONNECTION_ID:
    frame = quicNewConnectionIdFrameAllocator.alloc();
    new (frame) QUICNewConnectionIdFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_new_connection_id_frame);
  case QUICFrameType::STOP_SENDING:
    frame = quicStopSendingFrameAllocator.alloc();
    new (frame) QUICStopSendingFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_stop_sending_frame);
  case QUICFrameType::PATH_CHALLENGE:
    frame = quicPathChallengeFrameAllocator.alloc();
    new (frame) QUICPathChallengeFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_path_challenge_frame);
  case QUICFrameType::PATH_RESPONSE:
    frame = quicPathResponseFrameAllocator.alloc();
    new (frame) QUICPathResponseFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_path_response_frame);
  case QUICFrameType::NEW_TOKEN:
    frame = quicNewTokenFrameAllocator.alloc();
    new (frame) QUICNewTokenFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_new_token_frame);
  case QUICFrameType::RETIRE_CONNECTION_ID:
    frame = quicRetireConnectionIdFrameAllocator.alloc();
    new (frame) QUICRetireConnectionIdFrame(buf, len);
    return QUICFrameUPtr(frame, &QUICFrameDeleter::delete_retire_connection_id_frame);
  default:
    // Unknown frame
    Debug("quic_frame_factory", "Unknown frame type %x", buf[0]);
    return QUICFrameFactory::create_null_frame();
  }
}

std::shared_ptr<const QUICFrame>
QUICFrameFactory::fast_create(const uint8_t *buf, size_t len)
{
  if (QUICFrame::type(buf) == QUICFrameType::UNKNOWN) {
    return nullptr;
  }

  std::shared_ptr<QUICFrame> frame = this->_reusable_frames[static_cast<ptrdiff_t>(QUICFrame::type(buf))];

  if (frame == nullptr) {
    frame = QUICFrameFactory::create(buf, len);
    if (frame != nullptr) {
      this->_reusable_frames[static_cast<ptrdiff_t>(QUICFrame::type(buf))] = frame;
    }
  } else {
    frame->parse(buf, len);
  }

  return frame;
}

QUICStreamFrameUPtr
QUICFrameFactory::create_stream_frame(const uint8_t *data, size_t data_len, QUICStreamId stream_id, QUICOffset offset, bool last,
                                      bool has_offset_field, bool has_length_field, QUICFrameId id, QUICFrameGenerator *owner)
{
  ats_unique_buf buf = ats_unique_malloc(data_len);
  memcpy(buf.get(), data, data_len);

  QUICStreamFrame *frame = quicStreamFrameAllocator.alloc();
  new (frame) QUICStreamFrame(std::move(buf), data_len, stream_id, offset, last, has_offset_field, has_length_field, id, owner);
  return QUICStreamFrameUPtr(frame, &QUICFrameDeleter::delete_stream_frame);
}

QUICCryptoFrameUPtr
QUICFrameFactory::create_crypto_frame(const uint8_t *data, uint64_t data_len, QUICOffset offset, QUICFrameId id,
                                      QUICFrameGenerator *owner)
{
  ats_unique_buf buf = ats_unique_malloc(data_len);
  memcpy(buf.get(), data, data_len);

  QUICCryptoFrame *frame = quicCryptoFrameAllocator.alloc();
  new (frame) QUICCryptoFrame(std::move(buf), data_len, offset, id, owner);
  return QUICCryptoFrameUPtr(frame, &QUICFrameDeleter::delete_crypto_frame);
}

QUICFrameUPtr
QUICFrameFactory::split_frame(QUICFrame *frame, size_t size)
{
  auto new_frame = frame->split(size);
  if (!new_frame) {
    return QUICFrameFactory::create_null_frame();
  }

  return QUICFrameUPtr(new_frame, &QUICFrameDeleter::delete_stream_frame);
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_ack_frame(QUICPacketNumber largest_acknowledged, uint64_t ack_delay, uint64_t first_ack_block,
                                   QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICAckFrame *frame = quicAckFrameAllocator.alloc();
  new (frame) QUICAckFrame(largest_acknowledged, ack_delay, first_ack_block, id, owner);
  return std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_ack_frame);
}

std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_connection_close_frame(uint16_t error_code, QUICFrameType frame_type, uint16_t reason_phrase_length,
                                                const char *reason_phrase, QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICConnectionCloseFrame *frame = quicConnectionCloseFrameAllocator.alloc();
  new (frame) QUICConnectionCloseFrame(error_code, frame_type, reason_phrase_length, reason_phrase, id, owner);
  return std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_connection_close_frame);
}

std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_connection_close_frame(QUICConnectionError &error, QUICFrameId id, QUICFrameGenerator *owner)
{
  ink_assert(error.cls == QUICErrorClass::TRANSPORT);
  if (error.msg) {
    return QUICFrameFactory::create_connection_close_frame(error.code, error.frame_type(), strlen(error.msg), error.msg, id, owner);
  } else {
    return QUICFrameFactory::create_connection_close_frame(error.code, error.frame_type(), 0, nullptr, id, owner);
  }
}

std::unique_ptr<QUICApplicationCloseFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_application_close_frame(QUICAppErrorCode error_code, uint16_t reason_phrase_length,
                                                 const char *reason_phrase, QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICApplicationCloseFrame *frame = quicApplicationCloseFrameAllocator.alloc();
  new (frame) QUICApplicationCloseFrame(error_code, reason_phrase_length, reason_phrase, id, owner);
  return std::unique_ptr<QUICApplicationCloseFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_connection_close_frame);
}

std::unique_ptr<QUICApplicationCloseFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_application_close_frame(QUICConnectionError &error, QUICFrameId id, QUICFrameGenerator *owner)
{
  ink_assert(error.cls == QUICErrorClass::APPLICATION);
  if (error.msg) {
    return QUICFrameFactory::create_application_close_frame(error.code, strlen(error.msg), error.msg, id, owner);
  } else {
    return QUICFrameFactory::create_application_close_frame(error.code, 0, nullptr, id, owner);
  }
}

std::unique_ptr<QUICMaxDataFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_max_data_frame(uint64_t maximum_data, QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICMaxDataFrame *frame = quicMaxDataFrameAllocator.alloc();
  new (frame) QUICMaxDataFrame(maximum_data, id, owner);
  return std::unique_ptr<QUICMaxDataFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_max_data_frame);
}

std::unique_ptr<QUICMaxStreamDataFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_max_stream_data_frame(QUICStreamId stream_id, uint64_t maximum_data, QUICFrameId id,
                                               QUICFrameGenerator *owner)
{
  QUICMaxStreamDataFrame *frame = quicMaxStreamDataFrameAllocator.alloc();
  new (frame) QUICMaxStreamDataFrame(stream_id, maximum_data, id, owner);
  return std::unique_ptr<QUICMaxStreamDataFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_max_stream_data_frame);
}

std::unique_ptr<QUICMaxStreamIdFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_max_stream_id_frame(QUICStreamId maximum_stream_id, QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICMaxStreamIdFrame *frame = quicMaxStreamIdFrameAllocator.alloc();
  new (frame) QUICMaxStreamIdFrame(maximum_stream_id, id, owner);
  return std::unique_ptr<QUICMaxStreamIdFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_max_stream_id_frame);
}

std::unique_ptr<QUICPingFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_ping_frame(QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICPingFrame *frame = quicPingFrameAllocator.alloc();
  new (frame) QUICPingFrame(id, owner);
  return std::unique_ptr<QUICPingFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_ping_frame);
}

std::unique_ptr<QUICPathChallengeFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_path_challenge_frame(const uint8_t *data, QUICFrameId id, QUICFrameGenerator *owner)
{
  ats_unique_buf buf = ats_unique_malloc(QUICPathChallengeFrame::DATA_LEN);
  memcpy(buf.get(), data, QUICPathChallengeFrame::DATA_LEN);

  QUICPathChallengeFrame *frame = quicPathChallengeFrameAllocator.alloc();
  new (frame) QUICPathChallengeFrame(std::move(buf), id, owner);
  return std::unique_ptr<QUICPathChallengeFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_path_challenge_frame);
}

std::unique_ptr<QUICPathResponseFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_path_response_frame(const uint8_t *data, QUICFrameId id, QUICFrameGenerator *owner)
{
  ats_unique_buf buf = ats_unique_malloc(QUICPathResponseFrame::DATA_LEN);
  memcpy(buf.get(), data, QUICPathResponseFrame::DATA_LEN);

  QUICPathResponseFrame *frame = quicPathResponseFrameAllocator.alloc();
  new (frame) QUICPathResponseFrame(std::move(buf), id, owner);
  return std::unique_ptr<QUICPathResponseFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_path_response_frame);
}

std::unique_ptr<QUICBlockedFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_blocked_frame(QUICOffset offset, QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICBlockedFrame *frame = quicBlockedFrameAllocator.alloc();
  new (frame) QUICBlockedFrame(offset, id, owner);
  return std::unique_ptr<QUICBlockedFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_blocked_frame);
}

std::unique_ptr<QUICStreamBlockedFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_stream_blocked_frame(QUICStreamId stream_id, QUICOffset offset, QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICStreamBlockedFrame *frame = quicStreamBlockedFrameAllocator.alloc();
  new (frame) QUICStreamBlockedFrame(stream_id, offset, id, owner);
  return std::unique_ptr<QUICStreamBlockedFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stream_blocked_frame);
}

std::unique_ptr<QUICStreamIdBlockedFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_stream_id_blocked_frame(QUICStreamId stream_id, QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICStreamIdBlockedFrame *frame = quicStreamIdBlockedFrameAllocator.alloc();
  new (frame) QUICStreamIdBlockedFrame(stream_id, id, owner);
  return std::unique_ptr<QUICStreamIdBlockedFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stream_id_blocked_frame);
}

std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_rst_stream_frame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICOffset final_offset,
                                          QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICRstStreamFrame *frame = quicRstStreamFrameAllocator.alloc();
  new (frame) QUICRstStreamFrame(stream_id, error_code, final_offset, id, owner);
  return std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_rst_stream_frame);
}

std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_rst_stream_frame(QUICStreamError &error, QUICFrameId id, QUICFrameGenerator *owner)
{
  return QUICFrameFactory::create_rst_stream_frame(error.stream->id(), error.code, error.stream->final_offset(), id, owner);
}

std::unique_ptr<QUICStopSendingFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_stop_sending_frame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICFrameId id,
                                            QUICFrameGenerator *owner)
{
  QUICStopSendingFrame *frame = quicStopSendingFrameAllocator.alloc();
  new (frame) QUICStopSendingFrame(stream_id, error_code, id, owner);
  return std::unique_ptr<QUICStopSendingFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stop_sending_frame);
}

std::unique_ptr<QUICNewConnectionIdFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_new_connection_id_frame(uint32_t sequence, QUICConnectionId connectoin_id,
                                                 QUICStatelessResetToken stateless_reset_token, QUICFrameId id,
                                                 QUICFrameGenerator *owner)
{
  QUICNewConnectionIdFrame *frame = quicNewConnectionIdFrameAllocator.alloc();
  new (frame) QUICNewConnectionIdFrame(sequence, connectoin_id, stateless_reset_token, id, owner);
  return std::unique_ptr<QUICNewConnectionIdFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_new_connection_id_frame);
}

std::unique_ptr<QUICNewTokenFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_new_token_frame(const QUICResumptionToken &token, QUICFrameId id, QUICFrameGenerator *owner)
{
  uint64_t token_len       = token.length();
  ats_unique_buf token_buf = ats_unique_malloc(token_len);
  memcpy(token_buf.get(), token.buf(), token_len);

  QUICNewTokenFrame *frame = quicNewTokenFrameAllocator.alloc();
  new (frame) QUICNewTokenFrame(std::move(token_buf), token_len, id, owner);
  return std::unique_ptr<QUICNewTokenFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_new_token_frame);
}

std::unique_ptr<QUICRetireConnectionIdFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_retire_connection_id_frame(uint64_t seq_num, QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICRetireConnectionIdFrame *frame = quicRetireConnectionIdFrameAllocator.alloc();
  new (frame) QUICRetireConnectionIdFrame(seq_num, id, owner);
  return std::unique_ptr<QUICRetireConnectionIdFrame, QUICFrameDeleterFunc>(frame,
                                                                            &QUICFrameDeleter::delete_retire_connection_id_frame);
}

std::unique_ptr<QUICRetransmissionFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_retransmission_frame(QUICFrameUPtr original_frame, const QUICPacket &original_packet)
{
  QUICRetransmissionFrame *frame = quicRetransmissionFrameAllocator.alloc();
  new (frame) QUICRetransmissionFrame(std::move(original_frame), original_packet);
  return std::unique_ptr<QUICRetransmissionFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_retransmission_frame);
}

QUICFrameId
QUICFrameInfo::id()
{
  return this->_id;
}

QUICFrameGenerator *
QUICFrameInfo::generated_by()
{
  return this->_generator;
}
