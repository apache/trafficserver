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
#include "QUICStream.h"

ClassAllocator<QUICStreamFrame> quicStreamFrameAllocator("quicStreamFrameAllocator");
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
ClassAllocator<QUICRetransmissionFrame> quicRetransmissionFrameAllocator("quicRetransmissionFrameAllocator");

QUICFrameType
QUICFrame::type() const
{
  return QUICFrame::type(this->_buf);
}

QUICFrameType
QUICFrame::type(const uint8_t *buf)
{
  if (buf[0] >= static_cast<uint8_t>(QUICFrameType::UNKNOWN)) {
    return QUICFrameType::UNKNOWN;
  } else if (buf[0] >= static_cast<uint8_t>(QUICFrameType::STREAM)) {
    return QUICFrameType::STREAM;
  } else if (buf[0] > static_cast<uint8_t>(QUICFrameType::ACK)) {
    return QUICFrameType::UNKNOWN;
  } else {
    return static_cast<QUICFrameType>(buf[0]);
  }
}

void
QUICFrame::reset(const uint8_t *buf, size_t len)
{
  this->_buf = buf;
  this->_len = len;
}

//
// STREAM Frame
//

QUICStreamFrame::QUICStreamFrame(ats_unique_buf data, size_t data_len, QUICStreamId stream_id, QUICOffset offset, bool last)
{
  this->_data      = std::move(data);
  this->_data_len  = data_len;
  this->_stream_id = stream_id;
  this->_offset    = offset;
  this->_fin       = last;
}

QUICFrameType
QUICStreamFrame::type() const
{
  return QUICFrameType::STREAM;
}

size_t
QUICStreamFrame::size() const
{
  return this->_get_data_field_offset() + this->data_length();
}

void
QUICStreamFrame::store(uint8_t *buf, size_t *len) const
{
  this->store(buf, len, true);
}

void
QUICStreamFrame::store(uint8_t *buf, size_t *len, bool include_length_field) const
{
  // Build Frame Type: "0b0010OLF"
  buf[0] = static_cast<uint8_t>(QUICFrameType::STREAM);
  *len   = 1;

  size_t n;

  // Stream ID (i)
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, buf + *len, &n);
  *len += n;

  // [Offset (i)] "O" of "0b0010OLF"
  if (this->has_offset_field()) {
    QUICTypeUtil::write_QUICOffset(this->_offset, buf + *len, &n);
    *len += n;
    buf[0] += 0x04;
  }

  // [Length (i)] "L of "0b0010OLF"
  if (include_length_field) {
    QUICTypeUtil::write_QUICVariableInt(this->_data_len, buf + *len, &n);
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
}

QUICStreamId
QUICStreamFrame::stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + _get_stream_id_field_offset());
  } else {
    return this->_stream_id;
  }
}

QUICOffset
QUICStreamFrame::offset() const
{
  if (this->_buf) {
    if (this->has_offset_field()) {
      return QUICTypeUtil::read_QUICOffset(this->_buf + _get_offset_field_offset());
    } else {
      return 0;
    }
  } else {
    return this->_offset;
  }
}

uint64_t
QUICStreamFrame::data_length() const
{
  if (this->_buf) {
    if (this->has_length_field()) {
      return QUICTypeUtil::read_QUICVariableInt(this->_buf + this->_get_length_field_offset());
    } else {
      return this->_len - this->_get_data_field_offset();
    }
  } else {
    return this->_data_len;
  }
}

const uint8_t *
QUICStreamFrame::data() const
{
  if (this->_buf) {
    return this->_buf + this->_get_data_field_offset();
  } else {
    return this->_data.get();
  }
}

/**
 * "O" of "0b00010OLF"
 */
bool
QUICStreamFrame::has_offset_field() const
{
  if (this->_buf) {
    return (this->_buf[0] & 0x04) != 0;
  } else {
    return this->_offset != 0;
  }
}

/**
 * "L" of "0b00010OLF"
 */
bool
QUICStreamFrame::has_length_field() const
{
  if (this->_buf) {
    return (this->_buf[0] & 0x02) != 0;
  } else {
    // This depends on `include_length_field` arg of QUICStreamFrame::store.
    // Returning true for just in case.
    return true;
  }
}

/**
 * "F" of "0b00010OLF"
 */
bool
QUICStreamFrame::has_fin_flag() const
{
  if (this->_buf) {
    return (this->_buf[0] & 0x01) != 0;
  } else {
    return this->_fin;
  }
}

size_t
QUICStreamFrame::_get_stream_id_field_offset() const
{
  return 1;
}

size_t
QUICStreamFrame::_get_offset_field_offset() const
{
  size_t offset_field_offset = this->_get_stream_id_field_offset();
  offset_field_offset += this->_get_stream_id_field_len();

  return offset_field_offset;
}

size_t
QUICStreamFrame::_get_length_field_offset() const
{
  size_t length_field_offset = this->_get_stream_id_field_offset();
  length_field_offset += this->_get_stream_id_field_len();
  length_field_offset += this->_get_offset_field_len();

  return length_field_offset;
}

size_t
QUICStreamFrame::_get_data_field_offset() const
{
  size_t data_field_offset = this->_get_stream_id_field_offset();
  data_field_offset += this->_get_stream_id_field_len();
  data_field_offset += this->_get_offset_field_len();
  data_field_offset += this->_get_length_field_len();

  return data_field_offset;
}

size_t
QUICStreamFrame::_get_stream_id_field_len() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_stream_id_field_offset());
  } else {
    return QUICVariableInt::size(this->_stream_id);
  }
}

size_t
QUICStreamFrame::_get_offset_field_len() const
{
  if (this->_buf) {
    if (this->has_offset_field()) {
      return QUICVariableInt::size(this->_buf + this->_get_offset_field_offset());
    } else {
      return 0;
    }
  } else {
    return QUICVariableInt::size(this->_offset);
  }
}

size_t
QUICStreamFrame::_get_length_field_len() const
{
  if (this->_buf) {
    if (this->has_length_field()) {
      return QUICVariableInt::size(this->_buf + this->_get_length_field_offset());
    } else {
      return 0;
    }
  } else {
    return QUICVariableInt::size(this->_data_len);
  }
}

//
// ACK frame
//

QUICAckFrame::QUICAckFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len)
{
  this->reset(buf, len);
}

QUICAckFrame::QUICAckFrame(QUICPacketNumber largest_acknowledged, uint16_t ack_delay, uint64_t first_ack_block_length)
{
  this->_largest_acknowledged = largest_acknowledged;
  this->_ack_delay            = ack_delay;
  this->_ack_block_section    = new AckBlockSection(first_ack_block_length);
}

QUICAckFrame::~QUICAckFrame()
{
  if (this->_ack_block_section) {
    delete this->_ack_block_section;
    this->_ack_block_section = nullptr;
  }
}

void
QUICAckFrame::reset(const uint8_t *buf, size_t len)
{
  QUICFrame::reset(buf, len);
  this->_ack_block_section = new AckBlockSection(buf + this->_get_ack_block_section_offset(), this->ack_block_count());
}

QUICFrameType
QUICAckFrame::type() const
{
  return QUICFrameType::ACK;
}

size_t
QUICAckFrame::size() const
{
  return this->_get_ack_block_section_offset() + this->_ack_block_section->size();
}

void
QUICAckFrame::store(uint8_t *buf, size_t *len) const
{
  uint8_t *p = buf;
  size_t n;
  *p = static_cast<uint8_t>(QUICFrameType::ACK);
  ++p;

  QUICTypeUtil::write_QUICVariableInt(this->_largest_acknowledged, p, &n);
  p += n;
  QUICTypeUtil::write_QUICVariableInt(this->_ack_delay, p, &n);
  p += n;
  QUICTypeUtil::write_QUICVariableInt(this->ack_block_count(), p, &n);
  p += n;
  this->_ack_block_section->store(p, &n);
  p += n;

  *len = p - buf;

  return;
}

QUICPacketNumber
QUICAckFrame::largest_acknowledged() const
{
  if (this->_buf) {
    uint64_t largest_acknowledged;
    size_t encoded_len;
    QUICVariableInt::decode(largest_acknowledged, encoded_len, this->_buf + this->_get_largest_acknowledged_offset(),
                            this->_len - this->_get_largest_acknowledged_offset());
    return largest_acknowledged;
  } else {
    return this->_largest_acknowledged;
  }
}

uint64_t
QUICAckFrame::ack_delay() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVariableInt(this->_buf + this->_get_ack_delay_offset());
  } else {
    return this->_ack_delay;
  }
}

uint64_t
QUICAckFrame::ack_block_count() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVariableInt(this->_buf + this->_get_ack_block_count_offset());
  } else {
    return this->_ack_block_section->count();
  }
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

size_t
QUICAckFrame::_get_largest_acknowledged_offset() const
{
  return sizeof(QUICFrameType);
}

size_t
QUICAckFrame::_get_largest_acknowledged_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_largest_acknowledged_offset());
  } else {
    return QUICVariableInt::size(this->_largest_acknowledged);
  }
}

size_t
QUICAckFrame::_get_ack_delay_offset() const
{
  return this->_get_largest_acknowledged_offset() + this->_get_largest_acknowledged_length();
}

size_t
QUICAckFrame::_get_ack_delay_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_ack_delay_offset());
  } else {
    return QUICVariableInt::size(this->_ack_delay);
  }
}

size_t
QUICAckFrame::_get_ack_block_count_offset() const
{
  return this->_get_ack_delay_offset() + this->_get_ack_delay_length();
}

size_t
QUICAckFrame::_get_ack_block_count_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_ack_block_count_offset());
  } else {
    return QUICVariableInt::size(this->ack_block_count());
  }
}

size_t
QUICAckFrame::_get_ack_block_section_offset() const
{
  return this->_get_ack_block_count_offset() + this->_get_ack_block_count_length();
}

//
// QUICAckFrame::AckBlock
//
uint64_t
QUICAckFrame::AckBlock::gap() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVariableInt(this->_buf);
  } else {
    return this->_gap;
  }
}

uint64_t
QUICAckFrame::AckBlock::length() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVariableInt(this->_buf + this->_get_gap_size());
  } else {
    return this->_length;
  }
}

size_t
QUICAckFrame::AckBlock::size() const
{
  return this->_get_gap_size() + this->_get_length_size();
}

const uint8_t *
QUICAckFrame::AckBlock::buf() const
{
  return this->_buf;
}

size_t
QUICAckFrame::AckBlock::_get_gap_size() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf);
  } else {
    return QUICVariableInt::size(this->_gap);
  }
}

size_t
QUICAckFrame::AckBlock::_get_length_size() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_gap_size());
  } else {
    return QUICVariableInt::size(this->_gap);
  }
}

//
// QUICAckFrame::AckBlockSection
//
uint8_t
QUICAckFrame::AckBlockSection::count() const
{
  if (this->_buf) {
    return this->_ack_block_count;
  } else {
    return this->_ack_blocks.size();
  }
}

size_t
QUICAckFrame::AckBlockSection::size() const
{
  size_t n = 0;

  n += this->_get_first_ack_block_length_size();

  for (auto &&block : *this) {
    n += block.size();
  }

  return n;
}

void
QUICAckFrame::AckBlockSection::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;

  QUICTypeUtil::write_QUICVariableInt(this->_first_ack_block_length, p, &n);
  p += n;

  for (auto &&block : *this) {
    QUICTypeUtil::write_QUICVariableInt(block.gap(), p, &n);
    p += n;
    QUICTypeUtil::write_QUICVariableInt(block.length(), p, &n);
    p += n;
  }

  *len = p - buf;
}

uint64_t
QUICAckFrame::AckBlockSection::first_ack_block_length() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVariableInt(this->_buf);
  } else {
    return this->_first_ack_block_length;
  }
}

void
QUICAckFrame::AckBlockSection::add_ack_block(AckBlock block)
{
  this->_ack_blocks.push_back(block);
}

QUICAckFrame::AckBlockSection::const_iterator
QUICAckFrame::AckBlockSection::begin() const
{
  if (this->_buf) {
    return const_iterator(0, this->_buf + this->_get_first_ack_block_length_size(), this->_ack_block_count);
  } else {
    return const_iterator(0, &this->_ack_blocks);
  }
}

QUICAckFrame::AckBlockSection::const_iterator
QUICAckFrame::AckBlockSection::end() const
{
  if (this->_buf) {
    return const_iterator(this->_ack_block_count, this->_buf, this->_ack_block_count);
  } else {
    return const_iterator(this->_ack_blocks.size(), &this->_ack_blocks);
  }
}

size_t
QUICAckFrame::AckBlockSection::_get_first_ack_block_length_size() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf);
  } else {
    return QUICVariableInt::size(this->_first_ack_block_length);
  }
}

//
// QUICAckFrame::AckBlockSection::const_iterator
//
QUICAckFrame::AckBlockSection::const_iterator::const_iterator(uint8_t index, const uint8_t *buf, uint8_t ack_block_count)
  : _index(index), _buf(buf)
{
  if (index == 0) {
    this->_current_block = AckBlock(this->_buf);
  } else if (index < ack_block_count) {
    this->_current_block = AckBlock(this->_current_block.buf() + this->_current_block.size());
  } else {
    this->_current_block = {UINT64_C(0), UINT64_C(0)};
  }
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
const QUICAckFrame::AckBlock &QUICAckFrame::AckBlockSection::const_iterator::operator++()
{
  ++(this->_index);

  if (this->_buf) {
    this->_current_block = AckBlock(this->_current_block.buf() + this->_current_block.size());
  } else {
    if (this->_ack_blocks->size() == this->_index) {
      this->_current_block = {UINT64_C(0), UINT64_C(0)};
    } else {
      this->_current_block = this->_ack_blocks->at(this->_index);
    }
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

//
// RST_STREAM frame
//

QUICRstStreamFrame::QUICRstStreamFrame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICOffset final_offset)
  : _stream_id(stream_id), _error_code(error_code), _final_offset(final_offset)
{
}

QUICFrameType
QUICRstStreamFrame::type() const
{
  return QUICFrameType::RST_STREAM;
}

size_t
QUICRstStreamFrame::size() const
{
  return 1 + this->_get_stream_id_field_length() + sizeof(QUICAppErrorCode) + this->_get_final_offset_field_length();
}

void
QUICRstStreamFrame::store(uint8_t *buf, size_t *len) const
{
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
}

QUICStreamId
QUICRstStreamFrame::stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + this->_get_stream_id_field_offset());
  } else {
    return this->_stream_id;
  }
}

QUICAppErrorCode
QUICRstStreamFrame::error_code() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICAppErrorCode(this->_buf + this->_get_error_code_field_offset());
  } else {
    return this->_error_code;
  }
}

QUICOffset
QUICRstStreamFrame::final_offset() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICOffset(this->_buf + this->_get_final_offset_field_offset());
  } else {
    return this->_final_offset;
  }
}

size_t
QUICRstStreamFrame::_get_stream_id_field_offset() const
{
  return 1;
}

size_t
QUICRstStreamFrame::_get_stream_id_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_stream_id_field_offset());
  } else {
    return QUICVariableInt::size(this->_stream_id);
  }
}

size_t
QUICRstStreamFrame::_get_error_code_field_offset() const
{
  return this->_get_stream_id_field_offset() + this->_get_stream_id_field_length();
}

size_t
QUICRstStreamFrame::_get_final_offset_field_offset() const
{
  return this->_get_error_code_field_offset() + sizeof(QUICAppErrorCode);
}

size_t
QUICRstStreamFrame::_get_final_offset_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_final_offset_field_offset());
  } else {
    return QUICVariableInt::size(this->_final_offset);
  }
}

//
// PING frame
//
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

void
QUICPingFrame::store(uint8_t *buf, size_t *len) const
{
  buf[0] = static_cast<uint8_t>(QUICFrameType::PING);
  *len   = 1;
}

//
// PADDING frame
//
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

void
QUICPaddingFrame::store(uint8_t *buf, size_t *len) const
{
  buf[0] = static_cast<uint8_t>(QUICFrameType::PADDING);
  *len   = 1;
}

//
// CONNECTION_CLOSE frame
//
QUICConnectionCloseFrame::QUICConnectionCloseFrame(QUICTransErrorCode error_code, uint64_t reason_phrase_length,
                                                   const char *reason_phrase)
{
  this->_error_code           = error_code;
  this->_reason_phrase_length = reason_phrase_length;
  this->_reason_phrase        = reason_phrase;
}

QUICFrameType
QUICConnectionCloseFrame::type() const
{
  return QUICFrameType::CONNECTION_CLOSE;
}

size_t
QUICConnectionCloseFrame::size() const
{
  return sizeof(QUICFrameType) + sizeof(QUICTransErrorCode) + this->_get_reason_phrase_length_field_length() +
         this->reason_phrase_length();
}

void
QUICConnectionCloseFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::CONNECTION_CLOSE);
  ++p;
  QUICTypeUtil::write_QUICTransErrorCode(this->_error_code, p, &n);
  p += n;
  QUICTypeUtil::write_QUICVariableInt(this->_reason_phrase_length, p, &n);
  p += n;
  if (this->_reason_phrase_length > 0) {
    memcpy(p, this->_reason_phrase, this->_reason_phrase_length);
    p += this->_reason_phrase_length;
  }

  *len = p - buf;
}

QUICTransErrorCode
QUICConnectionCloseFrame::error_code() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICTransErrorCode(this->_buf + 1);
  } else {
    return this->_error_code;
  }
}

uint64_t
QUICConnectionCloseFrame::reason_phrase_length() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVariableInt(this->_buf + this->_get_reason_phrase_length_field_offset());
  } else {
    return this->_reason_phrase_length;
  }
}

const char *
QUICConnectionCloseFrame::reason_phrase() const
{
  if (this->_buf) {
    return reinterpret_cast<const char *>(this->_buf + this->_get_reason_phrase_field_offset());
  } else {
    return this->_reason_phrase;
  }
}

size_t
QUICConnectionCloseFrame::_get_reason_phrase_length_field_offset() const
{
  return sizeof(QUICFrameType) + sizeof(QUICTransErrorCode);
}

size_t
QUICConnectionCloseFrame::_get_reason_phrase_length_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_reason_phrase_length_field_offset());
  } else {
    return QUICVariableInt::size(this->_reason_phrase_length);
  }
}

size_t
QUICConnectionCloseFrame::_get_reason_phrase_field_offset() const
{
  return this->_get_reason_phrase_length_field_offset() + this->_get_reason_phrase_length_field_length();
}

//
// APPLICATION_CLOSE frame
//
QUICApplicationCloseFrame::QUICApplicationCloseFrame(QUICAppErrorCode error_code, uint64_t reason_phrase_length,
                                                     const char *reason_phrase)
{
  this->_error_code           = error_code;
  this->_reason_phrase_length = reason_phrase_length;
  this->_reason_phrase        = reason_phrase;
}

QUICFrameType
QUICApplicationCloseFrame::type() const
{
  return QUICFrameType::APPLICATION_CLOSE;
}

size_t
QUICApplicationCloseFrame::size() const
{
  return sizeof(QUICFrameType) + sizeof(QUICAppErrorCode) + this->_get_reason_phrase_length_field_length() +
         this->reason_phrase_length();
}

void
QUICApplicationCloseFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::APPLICATION_CLOSE);
  ++p;
  QUICTypeUtil::write_QUICAppErrorCode(this->_error_code, p, &n);
  p += n;
  QUICTypeUtil::write_QUICVariableInt(this->_reason_phrase_length, p, &n);
  p += n;
  if (this->_reason_phrase_length > 0) {
    memcpy(p, this->_reason_phrase, this->_reason_phrase_length);
    p += this->_reason_phrase_length;
  }

  *len = p - buf;
}

QUICAppErrorCode
QUICApplicationCloseFrame::error_code() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICAppErrorCode(this->_buf + 1);
  } else {
    return this->_error_code;
  }
}

uint64_t
QUICApplicationCloseFrame::reason_phrase_length() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVariableInt(this->_buf + this->_get_reason_phrase_length_field_offset());
  } else {
    return this->_reason_phrase_length;
  }
}

const char *
QUICApplicationCloseFrame::reason_phrase() const
{
  if (this->_buf) {
    return reinterpret_cast<const char *>(this->_buf + this->_get_reason_phrase_field_offset());
  } else {
    return this->_reason_phrase;
  }
}

size_t
QUICApplicationCloseFrame::_get_reason_phrase_length_field_offset() const
{
  return sizeof(QUICFrameType) + sizeof(QUICTransErrorCode);
}

size_t
QUICApplicationCloseFrame::_get_reason_phrase_length_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_reason_phrase_length_field_offset());
  } else {
    return QUICVariableInt::size(this->_reason_phrase_length);
  }
}

size_t
QUICApplicationCloseFrame::_get_reason_phrase_field_offset() const
{
  return this->_get_reason_phrase_length_field_offset() + this->_get_reason_phrase_length_field_length();
}

//
// MAX_DATA frame
//
QUICMaxDataFrame::QUICMaxDataFrame(uint64_t maximum_data)
{
  this->_maximum_data = maximum_data;
}

QUICFrameType
QUICMaxDataFrame::type() const
{
  return QUICFrameType::MAX_DATA;
}

size_t
QUICMaxDataFrame::size() const
{
  return sizeof(QUICFrameType) + this->_get_max_data_field_length();
}

void
QUICMaxDataFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::MAX_DATA);
  ++p;
  QUICTypeUtil::write_QUICMaxData(this->_maximum_data, p, &n);
  p += n;

  *len = p - buf;
}

uint64_t
QUICMaxDataFrame::maximum_data() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICMaxData(this->_buf + sizeof(QUICFrameType));
  } else {
    return this->_maximum_data;
  }
}

size_t
QUICMaxDataFrame::_get_max_data_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + sizeof(QUICFrameType));
  } else {
    return QUICVariableInt::size(this->_maximum_data);
  }
}

//
// MAX_STREAM_DATA
//
QUICMaxStreamDataFrame::QUICMaxStreamDataFrame(QUICStreamId stream_id, uint64_t maximum_stream_data)
{
  this->_stream_id           = stream_id;
  this->_maximum_stream_data = maximum_stream_data;
}

QUICFrameType
QUICMaxStreamDataFrame::type() const
{
  return QUICFrameType::MAX_STREAM_DATA;
}

size_t
QUICMaxStreamDataFrame::size() const
{
  return sizeof(QUICFrameType) + this->_get_stream_id_field_length() + this->_get_max_stream_data_field_length();
}

void
QUICMaxStreamDataFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::MAX_STREAM_DATA);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, p, &n);
  p += n;
  QUICTypeUtil::write_QUICMaxData(this->_maximum_stream_data, p, &n);
  p += n;

  *len = p - buf;
}

QUICStreamId
QUICMaxStreamDataFrame::stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + sizeof(QUICFrameType));
  } else {
    return this->_stream_id;
  }
}

uint64_t
QUICMaxStreamDataFrame::maximum_stream_data() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICMaxData(this->_buf + this->_get_max_stream_data_field_offset());
  } else {
    return this->_maximum_stream_data;
  }
}

size_t
QUICMaxStreamDataFrame::_get_stream_id_field_offset() const
{
  return sizeof(QUICFrameType);
}

size_t
QUICMaxStreamDataFrame::_get_stream_id_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_stream_id_field_offset());
  } else {
    return QUICVariableInt::size(this->_stream_id);
  }
}

size_t
QUICMaxStreamDataFrame::_get_max_stream_data_field_offset() const
{
  return sizeof(QUICFrameType) + this->_get_stream_id_field_length();
}

size_t
QUICMaxStreamDataFrame::_get_max_stream_data_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_max_stream_data_field_offset());
  } else {
    return QUICVariableInt::size(this->_stream_id);
  }
}

//
// MAX_STREAM_ID
//
QUICMaxStreamIdFrame::QUICMaxStreamIdFrame(QUICStreamId maximum_stream_id)
{
  this->_maximum_stream_id = maximum_stream_id;
}

QUICFrameType
QUICMaxStreamIdFrame::type() const
{
  return QUICFrameType::MAX_STREAM_ID;
}

size_t
QUICMaxStreamIdFrame::size() const
{
  return sizeof(QUICFrameType) + this->_get_max_stream_id_field_length();
}

void
QUICMaxStreamIdFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::MAX_STREAM_ID);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_maximum_stream_id, p, &n);
  p += n;

  *len = p - buf;
}

QUICStreamId
QUICMaxStreamIdFrame::maximum_stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + sizeof(QUICFrameType));
  } else {
    return this->_maximum_stream_id;
  }
}

size_t
QUICMaxStreamIdFrame::_get_max_stream_id_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + sizeof(QUICFrameType));
  } else {
    return QUICVariableInt::size(this->_maximum_stream_id);
  }
}

//
// BLOCKED frame
//
QUICFrameType
QUICBlockedFrame::type() const
{
  return QUICFrameType::BLOCKED;
}

size_t
QUICBlockedFrame::size() const
{
  return sizeof(QUICFrameType) + this->_get_offset_field_length();
}

void
QUICBlockedFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;

  *p = static_cast<uint8_t>(QUICFrameType::BLOCKED);
  ++p;
  QUICTypeUtil::write_QUICOffset(this->_offset, p, &n);
  p += n;

  *len = p - buf;
}

QUICOffset
QUICBlockedFrame::offset() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICOffset(this->_buf + sizeof(QUICFrameType));
  } else {
    return this->_offset;
  }
}

size_t
QUICBlockedFrame::_get_offset_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + sizeof(QUICFrameType));
  } else {
    return QUICVariableInt::size(this->_offset);
  }
}

//
// STREAM_BLOCKED frame
//
QUICFrameType
QUICStreamBlockedFrame::type() const
{
  return QUICFrameType::STREAM_BLOCKED;
}

size_t
QUICStreamBlockedFrame::size() const
{
  return sizeof(QUICFrameType) + this->_get_stream_id_field_length() + _get_offset_field_length();
}

void
QUICStreamBlockedFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::STREAM_BLOCKED);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, p, &n);
  p += n;
  QUICTypeUtil::write_QUICOffset(this->_offset, p, &n);
  p += n;

  *len = p - buf;
}

QUICStreamId
QUICStreamBlockedFrame::stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + sizeof(QUICFrameType));
  } else {
    return this->_stream_id;
  }
}

QUICOffset
QUICStreamBlockedFrame::offset() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICOffset(this->_buf + this->_get_offset_field_offset());
  } else {
    return this->_offset;
  }
}

size_t
QUICStreamBlockedFrame::_get_stream_id_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + sizeof(QUICFrameType));
  } else {
    return QUICVariableInt::size(this->_stream_id);
  }
}

size_t
QUICStreamBlockedFrame::_get_offset_field_offset() const
{
  return sizeof(QUICFrameType) + this->_get_stream_id_field_length();
}

size_t
QUICStreamBlockedFrame::_get_offset_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + this->_get_offset_field_offset());
  } else {
    return QUICVariableInt::size(this->_stream_id);
  }
}

//
// STREAM_ID_BLOCKED frame
//
QUICFrameType
QUICStreamIdBlockedFrame::type() const
{
  return QUICFrameType::STREAM_ID_BLOCKED;
}

size_t
QUICStreamIdBlockedFrame::size() const
{
  return sizeof(QUICFrameType) + this->_get_stream_id_field_length();
}

void
QUICStreamIdBlockedFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;

  *p = static_cast<uint8_t>(QUICFrameType::STREAM_ID_BLOCKED);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, p, &n);
  p += n;

  *len = p - buf;
}

QUICStreamId
QUICStreamIdBlockedFrame::stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + sizeof(QUICFrameType));
  } else {
    return this->_stream_id;
  }
}

size_t
QUICStreamIdBlockedFrame::_get_stream_id_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + sizeof(QUICFrameType));
  } else {
    return QUICVariableInt::size(this->_stream_id);
  }
}

//
// NEW_CONNECTION_ID frame
//

QUICFrameType
QUICNewConnectionIdFrame::type() const
{
  return QUICFrameType::NEW_CONNECTION_ID;
}

size_t
QUICNewConnectionIdFrame::size() const
{
  return sizeof(QUICFrameType) + this->_get_sequence_field_length() + sizeof(QUICConnectionId) + 16;
}

void
QUICNewConnectionIdFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::NEW_CONNECTION_ID);
  ++p;
  QUICTypeUtil::write_QUICVariableInt(this->_sequence, p, &n);
  p += n;
  QUICTypeUtil::write_QUICConnectionId(this->_connection_id, 8, p, &n);
  p += n;
  memcpy(p, this->_stateless_reset_token.buf(), QUICStatelessResetToken::LEN);
  p += QUICStatelessResetToken::LEN;

  *len = p - buf;
}

uint64_t
QUICNewConnectionIdFrame::sequence() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVariableInt(this->_buf + sizeof(QUICFrameType));
  } else {
    return this->_sequence;
  }
}

QUICConnectionId
QUICNewConnectionIdFrame::connection_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICConnectionId(this->_buf + this->_get_connection_id_field_offset(), 8);
  } else {
    return this->_connection_id;
  }
}

QUICStatelessResetToken
QUICNewConnectionIdFrame::stateless_reset_token() const
{
  if (this->_buf) {
    return QUICStatelessResetToken(this->_buf + this->_get_connection_id_field_offset() + sizeof(QUICConnectionId));
  } else {
    return this->_stateless_reset_token;
  }
}

size_t
QUICNewConnectionIdFrame::_get_sequence_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + sizeof(QUICFrameType));
  } else {
    return QUICVariableInt::size(this->_sequence);
  }
}

size_t
QUICNewConnectionIdFrame::_get_connection_id_field_offset() const
{
  return sizeof(QUICFrameType) + this->_get_sequence_field_length();
}

//
// STOP_SENDING frame
//

QUICStopSendingFrame::QUICStopSendingFrame(QUICStreamId stream_id, QUICAppErrorCode error_code)
  : _stream_id(stream_id), _error_code(error_code)
{
}

QUICFrameType
QUICStopSendingFrame::type() const
{
  return QUICFrameType::STOP_SENDING;
}

size_t
QUICStopSendingFrame::size() const
{
  return sizeof(QUICFrameType) + this->_get_stream_id_field_length() + sizeof(QUICAppErrorCode);
}

void
QUICStopSendingFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = static_cast<uint8_t>(QUICFrameType::STOP_SENDING);
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, p, &n);
  p += n;
  QUICTypeUtil::write_QUICAppErrorCode(this->_error_code, p, &n);
  p += n;

  *len = p - buf;
}

QUICAppErrorCode
QUICStopSendingFrame::error_code() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICAppErrorCode(this->_buf + this->_get_error_code_field_offset());
  } else {
    return this->_error_code;
  }
}

QUICStreamId
QUICStopSendingFrame::stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + sizeof(QUICFrameType));
  } else {
    return this->_stream_id;
  }
}

size_t
QUICStopSendingFrame::_get_stream_id_field_length() const
{
  if (this->_buf) {
    return QUICVariableInt::size(this->_buf + sizeof(QUICFrameType));
  } else {
    return QUICVariableInt::size(this->_stream_id);
  }
}

size_t
QUICStopSendingFrame::_get_error_code_field_offset() const
{
  return sizeof(QUICFrameType) + this->_get_stream_id_field_length();
}

//
// QUICRetransmissionFrame
//
QUICRetransmissionFrame::QUICRetransmissionFrame(QUICFrameUPtr original_frame, const QUICPacket &original_packet)
  : QUICFrame(), _packet_type(original_packet.type())
{
  size_t dummy;
  this->_size = original_frame->size();
  this->_data = ats_unique_malloc(this->_size);
  this->_buf  = this->_data.get();
  original_frame->store(this->_data.get(), &dummy);
}

size_t
QUICRetransmissionFrame::size() const
{
  return this->_size;
}

void
QUICRetransmissionFrame::store(uint8_t *buf, size_t *len) const
{
  memcpy(buf, this->_data.get(), this->_size);
  *len = this->_size;
}

QUICPacketType
QUICRetransmissionFrame::packet_type() const
{
  return this->_packet_type;
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
    frame->reset(buf, len);
  }

  return frame;
}

QUICStreamFrameUPtr
QUICFrameFactory::create_stream_frame(const uint8_t *data, size_t data_len, QUICStreamId stream_id, QUICOffset offset, bool last)
{
  ats_unique_buf buf = ats_unique_malloc(data_len);
  memcpy(buf.get(), data, data_len);

  QUICStreamFrame *frame = quicStreamFrameAllocator.alloc();
  new (frame) QUICStreamFrame(std::move(buf), data_len, stream_id, offset, last);
  return QUICStreamFrameUPtr(frame, &QUICFrameDeleter::delete_stream_frame);
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_ack_frame(QUICPacketNumber largest_acknowledged, uint16_t ack_delay, uint64_t first_ack_block_length)
{
  QUICAckFrame *frame = quicAckFrameAllocator.alloc();
  new (frame) QUICAckFrame(largest_acknowledged, ack_delay, first_ack_block_length);
  return std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_ack_frame);
}

std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_connection_close_frame(QUICTransErrorCode error_code, uint16_t reason_phrase_length,
                                                const char *reason_phrase)
{
  QUICConnectionCloseFrame *frame = quicConnectionCloseFrameAllocator.alloc();
  new (frame) QUICConnectionCloseFrame(error_code, reason_phrase_length, reason_phrase);
  return std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_connection_close_frame);
}

std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_connection_close_frame(QUICConnectionErrorUPtr error)
{
  ink_assert(error->cls == QUICErrorClass::TRANSPORT);
  if (error->msg) {
    return QUICFrameFactory::create_connection_close_frame(error->trans_error_code, strlen(error->msg), error->msg);
  } else {
    return QUICFrameFactory::create_connection_close_frame(error->trans_error_code);
  }
}

std::unique_ptr<QUICApplicationCloseFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_application_close_frame(QUICAppErrorCode error_code, uint16_t reason_phrase_length,
                                                 const char *reason_phrase)
{
  QUICApplicationCloseFrame *frame = quicApplicationCloseFrameAllocator.alloc();
  new (frame) QUICApplicationCloseFrame(error_code, reason_phrase_length, reason_phrase);
  return std::unique_ptr<QUICApplicationCloseFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_connection_close_frame);
}

std::unique_ptr<QUICApplicationCloseFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_application_close_frame(QUICConnectionErrorUPtr error)
{
  ink_assert(error->cls == QUICErrorClass::APPLICATION);
  if (error->msg) {
    return QUICFrameFactory::create_application_close_frame(error->app_error_code, strlen(error->msg), error->msg);
  } else {
    return QUICFrameFactory::create_application_close_frame(error->app_error_code);
  }
}

std::unique_ptr<QUICMaxDataFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_max_data_frame(uint64_t maximum_data)
{
  QUICMaxDataFrame *frame = quicMaxDataFrameAllocator.alloc();
  new (frame) QUICMaxDataFrame(maximum_data);
  return std::unique_ptr<QUICMaxDataFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_max_data_frame);
}

std::unique_ptr<QUICMaxStreamDataFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_max_stream_data_frame(QUICStreamId stream_id, uint64_t maximum_data)
{
  QUICMaxStreamDataFrame *frame = quicMaxStreamDataFrameAllocator.alloc();
  new (frame) QUICMaxStreamDataFrame(stream_id, maximum_data);
  return std::unique_ptr<QUICMaxStreamDataFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_max_stream_data_frame);
}

std::unique_ptr<QUICBlockedFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_blocked_frame(QUICOffset offset)
{
  QUICBlockedFrame *frame = quicBlockedFrameAllocator.alloc();
  new (frame) QUICBlockedFrame(offset);
  return std::unique_ptr<QUICBlockedFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_blocked_frame);
}

std::unique_ptr<QUICStreamBlockedFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_stream_blocked_frame(QUICStreamId stream_id, QUICOffset offset)
{
  QUICStreamBlockedFrame *frame = quicStreamBlockedFrameAllocator.alloc();
  new (frame) QUICStreamBlockedFrame(stream_id, offset);
  return std::unique_ptr<QUICStreamBlockedFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stream_blocked_frame);
}

std::unique_ptr<QUICStreamIdBlockedFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_stream_id_blocked_frame(QUICStreamId stream_id)
{
  QUICStreamIdBlockedFrame *frame = quicStreamIdBlockedFrameAllocator.alloc();
  new (frame) QUICStreamIdBlockedFrame(stream_id);
  return std::unique_ptr<QUICStreamIdBlockedFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stream_id_blocked_frame);
}

std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_rst_stream_frame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICOffset final_offset)
{
  QUICRstStreamFrame *frame = quicRstStreamFrameAllocator.alloc();
  new (frame) QUICRstStreamFrame(stream_id, error_code, final_offset);
  return std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_rst_stream_frame);
}

std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_rst_stream_frame(QUICStreamErrorUPtr error)
{
  return QUICFrameFactory::create_rst_stream_frame(error->stream->id(), error->app_error_code, error->stream->final_offset());
}

std::unique_ptr<QUICStopSendingFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_stop_sending_frame(QUICStreamId stream_id, QUICAppErrorCode error_code)
{
  QUICStopSendingFrame *frame = quicStopSendingFrameAllocator.alloc();
  new (frame) QUICStopSendingFrame(stream_id, error_code);
  return std::unique_ptr<QUICStopSendingFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stop_sending_frame);
}

std::unique_ptr<QUICNewConnectionIdFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_new_connection_id_frame(uint32_t sequence, QUICConnectionId connectoin_id,
                                                 QUICStatelessResetToken stateless_reset_token)
{
  QUICNewConnectionIdFrame *frame = quicNewConnectionIdFrameAllocator.alloc();
  new (frame) QUICNewConnectionIdFrame(sequence, connectoin_id, stateless_reset_token);
  return std::unique_ptr<QUICNewConnectionIdFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_new_connection_id_frame);
}

std::unique_ptr<QUICRetransmissionFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_retransmission_frame(QUICFrameUPtr original_frame, const QUICPacket &original_packet)
{
  QUICRetransmissionFrame *frame = quicRetransmissionFrameAllocator.alloc();
  new (frame) QUICRetransmissionFrame(std::move(original_frame), original_packet);
  return std::unique_ptr<QUICRetransmissionFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_retransmission_frame);
}
