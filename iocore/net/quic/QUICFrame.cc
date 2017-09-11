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

ClassAllocator<QUICStreamFrame> quicStreamFrameAllocator("quicStreamFrameAllocator");
ClassAllocator<QUICAckFrame> quicAckFrameAllocator("quicAckFrameAllocator");
ClassAllocator<QUICPaddingFrame> quicPaddingFrameAllocator("quicPaddingFrameAllocator");
ClassAllocator<QUICRstStreamFrame> quicRstStreamFrameAllocator("quicRstStreamFrameAllocator");
ClassAllocator<QUICConnectionCloseFrame> quicConnectionCloseFrameAllocator("quicConnectionCloseFrameAllocator");
ClassAllocator<QUICGoawayFrame> quicGoawayFrameAllocator("quicGoawayAllocator");
ClassAllocator<QUICMaxDataFrame> quicMaxDataFrameAllocator("quicMaxDataFrameAllocator");
ClassAllocator<QUICMaxStreamDataFrame> quicMaxStreamDataFrameAllocator("quicMaxStreamDataFrameAllocator");
ClassAllocator<QUICMaxStreamIdFrame> quicMaxStreamIdFrameAllocator("quicMaxStreamDataIdAllocator");
ClassAllocator<QUICPingFrame> quicPingFrameAllocator("quicPingFrameAllocator");
ClassAllocator<QUICBlockedFrame> quicBlockedFrameAllocator("quicBlockedFrameAllocator");
ClassAllocator<QUICStreamBlockedFrame> quicStreamBlockedFrameAllocator("quicStreamBlockedFrameAllocator");
ClassAllocator<QUICStreamIdNeededFrame> quicStreamIdNeededFrameAllocator("quicStreamIdNeededFrameAllocator");
ClassAllocator<QUICNewConnectionIdFrame> quicNewConnectionIdFrameAllocator("quicNewConnectionIdFrameAllocator");
ClassAllocator<QUICRetransmissionFrame> quicRetransmissionFrameAllocator("quicRetransmissionFrameAllocator");

QUICFrameType
QUICFrame::type() const
{
  return QUICFrame::type(this->_buf);
}

QUICFrameType
QUICFrame::type(const uint8_t *buf)
{
  if (buf[0] >= static_cast<uint8_t>(QUICFrameType::STREAM)) {
    return QUICFrameType::STREAM;
  } else if (buf[0] >= static_cast<uint8_t>(QUICFrameType::ACK)) {
    return QUICFrameType::ACK;
  } else if (static_cast<uint8_t>(QUICFrameType::NEW_CONNECTION_ID) < buf[0] && buf[0] < static_cast<uint8_t>(QUICFrameType::ACK)) {
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

QUICStreamFrame::QUICStreamFrame(const uint8_t *data, size_t data_len, QUICStreamId stream_id, QUICOffset offset, bool last)
{
  this->_data      = data;
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
  if (this->_buf) {
    return this->_get_data_offset() + this->data_length();
  } else {
    return 1 + 4 + 8 + 2 + this->data_length();
  }
}

void
QUICStreamFrame::store(uint8_t *buf, size_t *len) const
{
  this->store(buf, len, true);
}

void
QUICStreamFrame::store(uint8_t *buf, size_t *len, bool include_length_field) const
{
  size_t n;
  // Build Frame Type: "11FSSOOD"
  buf[0] = static_cast<uint8_t>(QUICFrameType::STREAM);
  *len   = 1;

  // "F" of "11FSSOOD"
  if (this->has_fin_flag()) {
    buf[0] += (0x01 << 5);
  }

  // "SS" of "11FSSOOD"
  // use 32 bit length for now
  buf[0] += (0x03 << 3);
  QUICTypeUtil::write_QUICStreamId(this->stream_id(), 4, buf + *len, &n);
  *len += n;

  // "OO" of "11FSSOOD"
  // use 64 bit length for now
  buf[0] += (0x03 << 1);
  QUICTypeUtil::write_QUICOffset(this->offset(), 8, buf + *len, &n);
  *len += n;

  // "D" of "11FSSOOD"
  if (include_length_field) {
    buf[0] += 0x01;
    QUICTypeUtil::write_uint_as_nbytes(this->data_length(), 2, buf + *len, &n);
    *len += n;
  }

  memcpy(buf + *len, this->data(), this->data_length());
  *len += this->data_length();
}

QUICStreamId
QUICStreamFrame::stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + this->_get_stream_id_offset(), this->_get_stream_id_len());
  } else {
    return this->_stream_id;
  }
}

QUICOffset
QUICStreamFrame::offset() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICOffset(this->_buf + this->_get_offset_offset(), this->_get_offset_len());
  } else {
    return this->_offset;
  }
}

const uint8_t *
QUICStreamFrame::data() const
{
  if (this->_buf) {
    return this->_buf + this->_get_data_offset();
  } else {
    return this->_data;
  }
}

size_t
QUICStreamFrame::data_length() const
{
  if (this->_buf) {
    if (this->has_data_length_field()) {
      return QUICTypeUtil::read_nbytes_as_uint(this->_buf + this->_get_offset_offset() + this->_get_offset_len(), 2);
    } else {
      return this->_len - this->_get_data_offset();
    }
  } else {
    return this->_data_len;
  }
}

/**
 * "D" of "11FSSOOD"
 */
bool
QUICStreamFrame::has_data_length_field() const
{
  return (this->_buf[0] & 0x01) != 0;
}

/**
 * "F" of "11FSSOOD"
 */
bool
QUICStreamFrame::has_fin_flag() const
{
  if (this->_buf) {
    return (this->_buf[0] & 0x20) != 0;
  } else {
    return this->_fin;
  }
}

size_t
QUICStreamFrame::_get_stream_id_offset() const
{
  return 1;
}

size_t
QUICStreamFrame::_get_offset_offset() const
{
  return this->_get_stream_id_offset() + this->_get_stream_id_len();
}

size_t
QUICStreamFrame::_get_data_offset() const
{
  if (this->_buf) {
    if (this->has_data_length_field()) {
      return this->_get_offset_offset() + this->_get_offset_len() + 2;
    } else {
      return this->_get_offset_offset() + this->_get_offset_len();
    }
  } else {
    return 0;
  }
}

/**
 * "SS" of "11FSSOOD"
 * The value 00, 01, 02, and 03 indicate lengths of 8, 16, 24, and 32 bits long respectively.
 */
size_t
QUICStreamFrame::_get_stream_id_len() const
{
  return ((this->_buf[0] & 0x18) >> 3) + 1;
}

/**
 * "OO" of "11FSSOOD"
 * The values 00, 01, 02, and 03 indicate lengths of 0, 16, 32, and 64 bits long respectively.
 */
size_t
QUICStreamFrame::_get_offset_len() const
{
  int OO_bits = (this->_buf[0] & 0x06) >> 1;
  if (OO_bits == 0) {
    return 0;
  } else {
    return 0x01 << OO_bits;
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
  this->_ack_block_section =
    new AckBlockSection(buf + this->_get_ack_block_section_offset(), this->num_blocks(), this->_get_ack_block_length());
}

QUICFrameType
QUICAckFrame::type() const
{
  return QUICFrameType::ACK;
}

size_t
QUICAckFrame::size() const
{
  if (this->_buf) {
    return this->_get_timestamp_section_offset() + this->timestamp_section()->size();
  } else {
    return 0;
  }
}

void
QUICAckFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;

  // Build Frame Type: "101NLLMM"
  buf[0] = static_cast<uint8_t>(QUICFrameType::ACK);
  p += 1;

  // "N" of "101NLLMM"
  if (this->_ack_block_section->count() > 0) {
    buf[0] += 0x10;
    *p = this->_ack_block_section->count();
    p += 1;
  }

  *p = this->_timestamp_section->count();
  p += 1;

  // "LL" of "101NLLMM"
  if (this->_largest_acknowledged <= 0xff) {
    QUICTypeUtil::write_uint_as_nbytes(this->_largest_acknowledged, 1, p, &n);
  } else if (this->_largest_acknowledged <= 0xffff) {
    buf[0] += 0x01 << 2;
    QUICTypeUtil::write_uint_as_nbytes(this->_largest_acknowledged, 2, p, &n);
  } else if (this->_largest_acknowledged <= 0xffffffff) {
    buf[0] += 0x02 << 2;
    QUICTypeUtil::write_uint_as_nbytes(this->_largest_acknowledged, 4, p, &n);
  } else {
    buf[0] += 0x03 << 2;
    QUICTypeUtil::write_uint_as_nbytes(this->_largest_acknowledged, 8, p, &n);
  }
  p += n;

  QUICTypeUtil::write_uint_as_nbytes(this->_ack_delay, 2, p, &n);
  p += n;

  // "MM" of "101NLLMM"
  // use 32 bit length for now
  // TODO The length should be returned by ackBlockSection
  buf[0] += 0x02;
  this->_ack_block_section->store(p, &n);
  p += n;

  this->_timestamp_section->store(p, &n);
  p += n;

  *len = p - buf;
}

uint8_t
QUICAckFrame::num_blocks() const
{
  if (this->has_ack_blocks()) {
    if (this->_buf) {
      return this->_buf[1];
    } else {
      return this->_ack_block_section->count();
    }
  } else {
    return 0;
  }
}

uint8_t
QUICAckFrame::num_timestamps() const
{
  return this->_buf[this->_get_num_timestamp_offset()];
}

QUICPacketNumber
QUICAckFrame::largest_acknowledged() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICPacketNumber(this->_buf + this->_get_largest_acknowledged_offset(),
                                               this->_get_largest_acknowledged_length());
  } else {
    return this->_largest_acknowledged;
  }
}

uint16_t
QUICAckFrame::ack_delay() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_nbytes_as_uint(this->_buf + this->_get_ack_delay_offset(), 2);
  } else {
    return this->_ack_delay;
  }
}

/**
 * N of 101NLLMM
 */
bool
QUICAckFrame::has_ack_blocks() const
{
  if (this->_buf) {
    return (this->_buf[0] & 0x10) != 0;
  } else {
    return this->_ack_block_section->count() != 0;
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

const QUICAckFrame::TimestampSection *
QUICAckFrame::timestamp_section() const
{
  return this->_timestamp_section;
}

size_t
QUICAckFrame::_get_num_timestamp_offset() const
{
  if (this->has_ack_blocks()) {
    return 2;
  } else {
    return 1;
  }
}

/**
 * LL of 101NLLMM
 */
size_t
QUICAckFrame::_get_largest_acknowledged_length() const
{
  /*
   * 0 -> 1 byte
   * 1 -> 2 byte
   * 2 -> 4 byte
   * 3 -> 8 byte
  */
  int n = (this->_buf[0] & 0x0c) >> 2;
  return 0x01 << n;
}

size_t
QUICAckFrame::_get_largest_acknowledged_offset() const
{
  return this->_get_num_timestamp_offset() + 1;
}

/**
 * MM of 101NLLMM
 */
size_t
QUICAckFrame::_get_ack_block_length() const
{
  /*
   * 0 -> 1 byte
   * 1 -> 2 byte
   * 2 -> 4 byte
   * 3 -> 8 byte
  */
  int n = this->_buf[0] & 0x03;
  return 0x01 << n;
}

size_t
QUICAckFrame::_get_ack_delay_offset() const
{
  return this->_get_largest_acknowledged_offset() + this->_get_largest_acknowledged_length();
}

size_t
QUICAckFrame::_get_ack_block_section_offset() const
{
  return this->_get_ack_delay_offset() + 2;
}

size_t
QUICAckFrame::_get_timestamp_section_offset() const
{
  return this->_get_ack_block_section_offset() + this->ack_block_section()->size();
}

QUICAckFrame::AckBlockSection::AckBlockSection(const uint8_t *buf, uint8_t num_blocks, uint8_t ack_block_length)
{
  this->_buf              = buf;
  this->_num_blocks       = num_blocks;
  this->_ack_block_length = ack_block_length;
}

QUICAckFrame::AckBlockSection::AckBlockSection(uint64_t first_ack_block_length)
{
  this->_first_ack_block_length = first_ack_block_length;
}

QUICAckFrame::AckBlock::AckBlock(const uint8_t *buf, uint8_t ack_block_length)
{
  uint8_t gap     = buf[0];
  uint64_t length = QUICTypeUtil::read_nbytes_as_uint(buf + 1, ack_block_length);
  this->_data     = (static_cast<uint64_t>(gap) << 56) + length;
}

QUICAckFrame::AckBlock::AckBlock(uint8_t gap, uint64_t length)
{
  this->_data = (static_cast<uint64_t>(gap) << 56) + length;
}

uint8_t
QUICAckFrame::AckBlock::gap() const
{
  return this->_data >> 56;
}

uint64_t
QUICAckFrame::AckBlock::length() const
{
  return this->_data & 0x0000FFFFFFFFFFFF;
}

uint8_t
QUICAckFrame::AckBlockSection::count() const
{
  if (this->_buf) {
    return this->_num_blocks;
  } else {
    return this->_ack_blocks.size();
  }
}

size_t
QUICAckFrame::AckBlockSection::size() const
{
  if (this->_buf) {
    return this->_ack_block_length + (this->_ack_block_length + 1) * this->_num_blocks;
  } else {
    // TODO Which block length should we use?
    return 48 + (48 + 1) * this->_ack_blocks.size();
  }
}

void
QUICAckFrame::AckBlockSection::store(uint8_t *buf, size_t *len) const
{
  uint8_t *p = buf;
  size_t dummy;
  QUICTypeUtil::write_uint_as_nbytes(this->_first_ack_block_length, 4, buf, &dummy);
  p += 4;
  for (auto &&block : *this) {
    p[0] = block.gap();
    p += 1;
    QUICTypeUtil::write_uint_as_nbytes(block.length(), 4, buf, &dummy);
    p += 4;
  }
  *len = p - buf;
}

uint64_t
QUICAckFrame::AckBlockSection::first_ack_block_length() const
{
  return this->_first_ack_block_length;
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
    return const_iterator(0, this->_buf, this->_num_blocks, this->_ack_block_length);
  } else {
    return const_iterator(0, &this->_ack_blocks);
  }
}

QUICAckFrame::AckBlockSection::const_iterator
QUICAckFrame::AckBlockSection::end() const
{
  if (this->_buf) {
    return const_iterator(this->_num_blocks, this->_buf, this->_num_blocks, this->_ack_block_length);
  } else {
    return const_iterator(this->_ack_blocks.size(), &this->_ack_blocks);
  }
}

QUICAckFrame::AckBlockSection::const_iterator::const_iterator(uint8_t index, const uint8_t *buf, uint8_t num_blocks,
                                                              uint8_t ack_block_length)
{
  this->_index         = index;
  this->_buf           = buf;
  this->_current_block = AckBlock(buf, ack_block_length);
}

QUICAckFrame::AckBlockSection::const_iterator::const_iterator(uint8_t index, const std::vector<QUICAckFrame::AckBlock> *ack_block)
{
  this->_index      = index;
  this->_ack_blocks = ack_block;
  if (this->_ack_blocks->size()) {
    if (this->_ack_blocks->size() == this->_index) {
      this->_current_block = {static_cast<uint8_t>(0), 0ULL};
    } else {
      this->_current_block = this->_ack_blocks->at(this->_index);
    }
  }
}

uint8_t
QUICAckFrame::TimestampSection::count() const
{
  return 0;
}

size_t
QUICAckFrame::TimestampSection::size() const
{
  return 0;
}

void
QUICAckFrame::TimestampSection::store(uint8_t *buf, size_t *len) const
{
  if (this->count() == 0) {
    *len = 0;
    return;
  }

  // TODO: Store timestamp data
}

//
// RST_STREAM frame
//

QUICRstStreamFrame::QUICRstStreamFrame(QUICErrorCode error_code, QUICStreamId stream_id, QUICOffset final_offset)
  : _error_code(error_code), _stream_id(stream_id), _final_offset(final_offset)
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
  return 17;
}

void
QUICRstStreamFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = 0x01;
  ++p;
  QUICTypeUtil::write_QUICErrorCode(this->_error_code, p, &n);
  p += n;
  QUICTypeUtil::write_QUICStreamId(this->_stream_id, 4, p, &n);
  p += n;
  QUICTypeUtil::write_QUICOffset(this->_final_offset, 8, p, &n);
  p += n;

  *len = p - buf;
}

QUICErrorCode
QUICRstStreamFrame::error_code() const
{
  return QUICTypeUtil::read_QUICErrorCode(this->_buf + 1);
}

QUICStreamId
QUICRstStreamFrame::stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + 5, 4);
  } else {
    return this->_stream_id;
  }
}

QUICOffset
QUICRstStreamFrame::final_offset() const
{
  return QUICTypeUtil::read_QUICOffset(this->_buf + 9, 8);
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
  buf[0] = 0x07;
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
  buf[0] = 0x00;
  *len   = 1;
}

//
// GOAWAY frame
//

QUICGoawayFrame::QUICGoawayFrame(QUICStreamId client_stream_id, QUICStreamId server_stream_id)
{
  this->_client_stream_id = client_stream_id;
  this->_server_stream_id = server_stream_id;
}

QUICFrameType
QUICGoawayFrame::type() const
{
  return QUICFrameType::GOAWAY;
}

size_t
QUICGoawayFrame::size() const
{
  return 9;
}

void
QUICGoawayFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = 0x03;
  ++p;
  QUICTypeUtil::write_QUICStreamId(this->_client_stream_id, 4, p, &n);
  p += n;
  QUICTypeUtil::write_QUICStreamId(this->_server_stream_id, 4, p, &n);
  p += n;
  *len = p - buf;
}

QUICStreamId
QUICGoawayFrame::client_stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + 1, 4);
  } else {
    return this->_client_stream_id;
  }
}

QUICStreamId
QUICGoawayFrame::server_stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + 5, 4);
  } else {
    return this->_server_stream_id;
  }
}

//
// CONNECTION_CLOSE frame
//

QUICConnectionCloseFrame::QUICConnectionCloseFrame(QUICErrorCode error_code, uint16_t reason_phrase_length,
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
  return 7 + this->reason_phrase_length();
}

void
QUICConnectionCloseFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = 0x02;
  ++p;
  QUICTypeUtil::write_QUICErrorCode(this->_error_code, p, &n);
  p += n;
  QUICTypeUtil::write_uint_as_nbytes(this->_reason_phrase_length, 2, p, &n);
  p += n;
  memcpy(p, this->_reason_phrase, this->_reason_phrase_length);
  p += this->_reason_phrase_length;

  *len = p - buf;
}

QUICErrorCode
QUICConnectionCloseFrame::error_code() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICErrorCode(this->_buf + 1);
  } else {
    return this->_error_code;
  }
}

uint16_t
QUICConnectionCloseFrame::reason_phrase_length() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_nbytes_as_uint(this->_buf + 5, 2);
  } else {
    return this->_reason_phrase_length;
  }
}

const char *
QUICConnectionCloseFrame::reason_phrase() const
{
  if (this->_buf) {
    return reinterpret_cast<const char *>(this->_buf + 7);
  } else {
    return this->_reason_phrase;
  }
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
  return 9;
}

void
QUICMaxDataFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = 0x04;
  ++p;
  QUICTypeUtil::write_uint_as_nbytes(this->_maximum_data, 8, p, &n);
  p += n;

  *len = p - buf;
}

uint64_t
QUICMaxDataFrame::maximum_data() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_nbytes_as_uint(this->_buf + 1, 8);
  } else {
    return this->_maximum_data;
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
  return 13;
}

void
QUICMaxStreamDataFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = 0x05;
  ++p;
  QUICTypeUtil::write_uint_as_nbytes(this->_stream_id, 4, p, &n);
  p += n;
  QUICTypeUtil::write_uint_as_nbytes(this->_maximum_stream_data, 8, p, &n);
  p += n;

  *len = p - buf;
}

QUICStreamId
QUICMaxStreamDataFrame::stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_nbytes_as_uint(this->_buf + 1, 4);
  } else {
    return this->_stream_id;
  }
}

uint64_t
QUICMaxStreamDataFrame::maximum_stream_data() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_nbytes_as_uint(this->_buf + 5, 8);
  } else {
    return this->_maximum_stream_data;
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
  return 5;
}

void
QUICMaxStreamIdFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = 0x06;
  ++p;
  QUICTypeUtil::write_uint_as_nbytes(this->_maximum_stream_id, 4, p, &n);
  p += n;

  *len = p - buf;
}

QUICStreamId
QUICMaxStreamIdFrame::maximum_stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_nbytes_as_uint(this->_buf + 1, 4);
  } else {
    return this->_maximum_stream_id;
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
  return 1;
}

void
QUICBlockedFrame::store(uint8_t *buf, size_t *len) const
{
  buf[0] = 0x08;
  *len   = 1;
}

//
// STREAM_BLOCKED frame
//
QUICStreamBlockedFrame::QUICStreamBlockedFrame(QUICStreamId stream_id)
{
  this->_stream_id = stream_id;
}

QUICFrameType
QUICStreamBlockedFrame::type() const
{
  return QUICFrameType::STREAM_BLOCKED;
}

size_t
QUICStreamBlockedFrame::size() const
{
  return 5;
}

void
QUICStreamBlockedFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = 0x09;
  ++p;
  QUICTypeUtil::write_uint_as_nbytes(this->_stream_id, 4, p, &n);
  p += n;

  *len = p - buf;
}

QUICStreamId
QUICStreamBlockedFrame::stream_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICStreamId(this->_buf + 1, 4);
  } else {
    return this->_stream_id;
  }
}

//
// STREAM_ID_NEEDED frame
//
QUICFrameType
QUICStreamIdNeededFrame::type() const
{
  return QUICFrameType::STREAM_ID_NEEDED;
}

size_t
QUICStreamIdNeededFrame::size() const
{
  return 1;
}

void
QUICStreamIdNeededFrame::store(uint8_t *buf, size_t *len) const
{
  buf[0] = 0x0a;
  *len   = 1;
}

//
// NEW_CONNECTION_ID frame
//

QUICNewConnectionIdFrame::QUICNewConnectionIdFrame(uint16_t sequence, QUICConnectionId connection_id)
{
  this->_sequence      = sequence;
  this->_connection_id = connection_id;
}

QUICFrameType
QUICNewConnectionIdFrame::type() const
{
  return QUICFrameType::NEW_CONNECTION_ID;
}

size_t
QUICNewConnectionIdFrame::size() const
{
  return 11;
}

void
QUICNewConnectionIdFrame::store(uint8_t *buf, size_t *len) const
{
  size_t n;
  uint8_t *p = buf;
  *p         = 0x0b;
  ++p;
  QUICTypeUtil::write_uint_as_nbytes(this->_sequence, 2, p, &n);
  p += n;
  QUICTypeUtil::write_QUICConnectionId(this->_connection_id, 8, p, &n);
  p += n;

  *len = p - buf;
}

uint16_t
QUICNewConnectionIdFrame::sequence() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_nbytes_as_uint(this->_buf + 1, 2);
  } else {
    return this->_sequence;
  }
}

QUICConnectionId
QUICNewConnectionIdFrame::connection_id() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICConnectionId(this->_buf + 3, 8);
  } else {
    return this->_connection_id;
  }
}

//
// QUICRetransmissionFrame
//
QUICRetransmissionFrame::QUICRetransmissionFrame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> original_frame,
                                                 const QUICPacket &original_packet)
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

std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create(const uint8_t *buf, size_t len)
{
  QUICFrame *frame;

  switch (QUICFrame::type(buf)) {
  case QUICFrameType::STREAM:
    frame = quicStreamFrameAllocator.alloc();
    new (frame) QUICStreamFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stream_frame);
  case QUICFrameType::ACK:
    frame = quicAckFrameAllocator.alloc();
    new (frame) QUICAckFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_ack_frame);
  case QUICFrameType::PADDING:
    frame = quicPaddingFrameAllocator.alloc();
    new (frame) QUICPaddingFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_padding_frame);
  case QUICFrameType::RST_STREAM:
    frame = quicRstStreamFrameAllocator.alloc();
    new (frame) QUICRstStreamFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_rst_stream_frame);
  case QUICFrameType::CONNECTION_CLOSE:
    frame = quicConnectionCloseFrameAllocator.alloc();
    new (frame) QUICConnectionCloseFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_connection_close_frame);
  case QUICFrameType::GOAWAY:
    frame = quicGoawayFrameAllocator.alloc();
    new (frame) QUICGoawayFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_goaway_frame);
  case QUICFrameType::MAX_DATA:
    frame = quicMaxDataFrameAllocator.alloc();
    new (frame) QUICMaxDataFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_max_data_frame);
  case QUICFrameType::MAX_STREAM_DATA:
    frame = quicMaxStreamDataFrameAllocator.alloc();
    new (frame) QUICMaxStreamDataFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_max_stream_data_frame);
  case QUICFrameType::MAX_STREAM_ID:
    frame = quicMaxStreamIdFrameAllocator.alloc();
    new (frame) QUICMaxStreamIdFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_max_stream_id_frame);
  case QUICFrameType::PING:
    frame = quicPingFrameAllocator.alloc();
    new (frame) QUICPingFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_ping_frame);
  case QUICFrameType::BLOCKED:
    frame = quicBlockedFrameAllocator.alloc();
    new (frame) QUICBlockedFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_blocked_frame);
  case QUICFrameType::STREAM_BLOCKED:
    frame = quicStreamBlockedFrameAllocator.alloc();
    new (frame) QUICStreamBlockedFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stream_blocked_frame);
  case QUICFrameType::STREAM_ID_NEEDED:
    frame = quicStreamIdNeededFrameAllocator.alloc();
    new (frame) QUICStreamIdNeededFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stream_id_needed_frame);
  case QUICFrameType::NEW_CONNECTION_ID:
    frame = quicNewConnectionIdFrameAllocator.alloc();
    new (frame) QUICNewConnectionIdFrame(buf, len);
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_new_connection_id_frame);
  default:
    // Unknown frame
    return std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>(nullptr, &QUICFrameDeleter::delete_null_frame);
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

std::unique_ptr<QUICStreamFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_stream_frame(const uint8_t *data, size_t data_len, QUICStreamId stream_id, QUICOffset offset, bool last)
{
  QUICStreamFrame *frame = quicStreamFrameAllocator.alloc();
  new (frame) QUICStreamFrame(data, data_len, stream_id, offset, last);
  return std::unique_ptr<QUICStreamFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stream_frame);
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_ack_frame(QUICPacketNumber largest_acknowledged, uint16_t ack_delay, uint64_t first_ack_block_length)
{
  QUICAckFrame *frame = quicAckFrameAllocator.alloc();
  new (frame) QUICAckFrame(largest_acknowledged, ack_delay, first_ack_block_length);
  return std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_ack_frame);
}

std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_connection_close_frame(QUICErrorCode error_code, uint16_t reason_phrase_length, const char *reason_phrase)
{
  QUICConnectionCloseFrame *frame = quicConnectionCloseFrameAllocator.alloc();
  new (frame) QUICConnectionCloseFrame(error_code, reason_phrase_length, reason_phrase);
  return std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_connection_close_frame);
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
QUICFrameFactory::create_blocked_frame()
{
  QUICBlockedFrame *frame = quicBlockedFrameAllocator.alloc();
  new (frame) QUICBlockedFrame();
  return std::unique_ptr<QUICBlockedFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_blocked_frame);
}

std::unique_ptr<QUICStreamBlockedFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_stream_blocked_frame(QUICStreamId stream_id)
{
  QUICStreamBlockedFrame *frame = quicStreamBlockedFrameAllocator.alloc();
  new (frame) QUICStreamBlockedFrame(stream_id);
  return std::unique_ptr<QUICStreamBlockedFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_stream_blocked_frame);
}

std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_rst_stream_frame(QUICStreamId stream_id, QUICErrorCode error_code, QUICOffset final_offset)
{
  QUICRstStreamFrame *frame = quicRstStreamFrameAllocator.alloc();
  new (frame) QUICRstStreamFrame(error_code, stream_id, final_offset);
  return std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_rst_stream_frame);
}

std::unique_ptr<QUICRetransmissionFrame, QUICFrameDeleterFunc>
QUICFrameFactory::create_retransmission_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> original_frame,
                                              const QUICPacket &original_packet)
{
  QUICRetransmissionFrame *frame = quicRetransmissionFrameAllocator.alloc();
  new (frame) QUICRetransmissionFrame(std::move(original_frame), original_packet);
  return std::unique_ptr<QUICRetransmissionFrame, QUICFrameDeleterFunc>(frame, &QUICFrameDeleter::delete_retransmission_frame);
}
