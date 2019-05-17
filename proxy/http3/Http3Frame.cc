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

#include "tscore/Diags.h"
#include "quic/QUICIntUtil.h"
#include "Http3Frame.h"

ClassAllocator<Http3Frame> http3FrameAllocator("http3FrameAllocator");
ClassAllocator<Http3DataFrame> http3DataFrameAllocator("http3DataFrameAllocator");
ClassAllocator<Http3HeadersFrame> http3HeadersFrameAllocator("http3HeadersFrameAllocator");
ClassAllocator<Http3SettingsFrame> http3SettingsFrameAllocator("http3SettingsFrameAllocator");

//
// Static functions
//

int
Http3Frame::length(const uint8_t *buf, size_t buf_len, uint64_t &length)
{
  size_t length_field_length = 0;
  return QUICVariableInt::decode(length, length_field_length, buf, buf_len);
}

Http3FrameType
Http3Frame::type(const uint8_t *buf, size_t buf_len)
{
  uint64_t type            = 0;
  size_t type_field_length = 0;
  int ret                  = QUICVariableInt::decode(type, type_field_length, buf, buf_len);
  ink_assert(ret != 1);
  if (type <= static_cast<uint64_t>(Http3FrameType::X_MAX_DEFINED)) {
    return static_cast<Http3FrameType>(type);
  } else {
    return Http3FrameType::UNKNOWN;
  }
}

//
// Generic Frame
//

Http3Frame::Http3Frame(const uint8_t *buf, size_t buf_len)
{
  // Type
  size_t type_field_length = 0;
  int ret                  = QUICVariableInt::decode(reinterpret_cast<uint64_t &>(this->_type), type_field_length, buf, buf_len);
  ink_assert(ret != 1);

  // Length
  size_t length_field_length = 0;
  ret = QUICVariableInt::decode(this->_length, length_field_length, buf + type_field_length, buf_len - type_field_length);
  ink_assert(ret != 1);

  // Payload offset
  this->_payload_offset = type_field_length + length_field_length;
}

Http3Frame::Http3Frame(Http3FrameType type) : _type(type) {}

uint64_t
Http3Frame::total_length() const
{
  return this->_payload_offset + this->length();
}

uint64_t
Http3Frame::length() const
{
  return this->_length;
}

Http3FrameType
Http3Frame::type() const
{
  if (static_cast<uint64_t>(this->_type) <= static_cast<uint64_t>(Http3FrameType::X_MAX_DEFINED)) {
    return this->_type;
  } else {
    return Http3FrameType::UNKNOWN;
  }
}

int64_t
Http3Frame::store(QUICStreamIO *stream_io)
{
  // If you really need this, you should keep the data passed to its constructor
  ink_assert(!"Not supported");
  return 0;
}

void
Http3Frame::reset(const uint8_t *buf, size_t len)
{
  this->~Http3Frame();
  new (this) Http3Frame(buf, len);
}

//
// UNKNOWN Frame
//
Http3UnknownFrame::Http3UnknownFrame(const uint8_t *buf, size_t buf_len) : Http3Frame(buf, buf_len), _buf(buf), _buf_len(buf_len) {}

int64_t
Http3UnknownFrame::store(QUICStreamIO *stream_io)
{
  stream_io->write(this->_buf, this->_buf_len);
  return this->_buf_len;
}

//
// DATA Frame
//
Http3DataFrame::Http3DataFrame(const uint8_t *buf, size_t buf_len) : Http3Frame(buf, buf_len)
{
  this->_payload_len = buf_len - this->_payload_offset;
  this->_write_buffer.write(buf + this->_payload_offset, this->_payload_len);
}

Http3DataFrame::Http3DataFrame(IOBufferReader *reader, size_t payload_len)
  : Http3Frame(Http3FrameType::DATA), _payload_len(payload_len)
{
  this->_length = this->_payload_len;
  this->_write_buffer.write(reader, payload_len);
}

int64_t
Http3DataFrame::store(QUICStreamIO *stream_io)
{
  size_t written = 0;
  size_t n;

  IOBufferBlock *block = new_IOBufferBlock();
  block->alloc(BUFFER_SIZE_INDEX_128);

  QUICVariableInt::encode(reinterpret_cast<uint8_t *>(block->start()), UINT64_MAX, n, static_cast<uint64_t>(this->_type));
  written += n;
  QUICVariableInt::encode(reinterpret_cast<uint8_t *>(block->start() + written), UINT64_MAX, n, this->_length);
  written += n;

  block->fill(written);

  stream_io->write(block);

  IOBufferReader *reader = this->_write_buffer.alloc_reader();
  stream_io->write(reader, this->_payload_len);
  this->_write_buffer.dealloc_reader(reader);

  return written + this->_payload_len;
}

void
Http3DataFrame::reset(const uint8_t *buf, size_t len)
{
  this->~Http3DataFrame();
  new (this) Http3DataFrame(buf, len);
}

IOBufferReader *
Http3DataFrame::payload()
{
  return this->_write_buffer.alloc_reader();
}

uint64_t
Http3DataFrame::payload_length() const
{
  return this->_payload_len;
}

//
// HEADERS Frame
//
Http3HeadersFrame::Http3HeadersFrame(const uint8_t *buf, size_t buf_len) : Http3Frame(buf, buf_len)
{
  this->_header_block_len = buf_len - this->_payload_offset;
  this->_write_buffer.write(buf + this->_payload_offset, this->_header_block_len);
}

Http3HeadersFrame::Http3HeadersFrame(IOBufferReader *reader, size_t header_block_len)
  : Http3Frame(Http3FrameType::HEADERS), _header_block_len(header_block_len)
{
  this->_length = header_block_len;
  this->_write_buffer.write(reader, header_block_len);
}

int64_t
Http3HeadersFrame::store(QUICStreamIO *stream_io)
{
  size_t written = 0;
  size_t n;

  IOBufferBlock *block = new_IOBufferBlock();
  block->alloc(BUFFER_SIZE_INDEX_128);

  QUICVariableInt::encode(reinterpret_cast<uint8_t *>(block->start()), UINT64_MAX, n, static_cast<uint64_t>(this->_type));
  written += n;
  QUICVariableInt::encode(reinterpret_cast<uint8_t *>(block->start() + written), UINT64_MAX, n, this->_length);
  written += n;

  block->fill(written);

  stream_io->write(block);

  IOBufferReader *reader = this->_write_buffer.alloc_reader();
  stream_io->write(reader, this->_header_block_len);
  this->_write_buffer.dealloc_reader(reader);

  return written + this->_header_block_len;
}

void
Http3HeadersFrame::reset(const uint8_t *buf, size_t len)
{
  this->~Http3HeadersFrame();
  new (this) Http3HeadersFrame(buf, len);
}

IOBufferReader *
Http3HeadersFrame::header_block()
{
  return this->_write_buffer.alloc_reader();
}

uint64_t
Http3HeadersFrame::header_block_length() const
{
  return this->_header_block_len;
}

//
// SETTINGS Frame
//

Http3SettingsFrame::Http3SettingsFrame(const uint8_t *buf, size_t buf_len) : Http3Frame(buf, buf_len)
{
  size_t len = this->_payload_offset;

  while (len < buf_len) {
    size_t id_len = QUICVariableInt::size(buf + len);
    uint16_t id   = QUICIntUtil::read_QUICVariableInt(buf + len);
    len += id_len;

    size_t value_len = QUICVariableInt::size(buf + len);
    uint64_t value   = QUICIntUtil::read_QUICVariableInt(buf + len);
    len += value_len;

    // Ignore any SETTINGS identifier it does not understand.
    bool ignore = true;
    for (const auto &known_id : Http3SettingsFrame::VALID_SETTINGS_IDS) {
      if (id == static_cast<uint64_t>(known_id)) {
        ignore = false;
        break;
      }
    }

    if (ignore) {
      continue;
    }

    this->_settings.insert(std::make_pair(static_cast<Http3SettingsId>(id), value));
  }

  if (len == buf_len) {
    this->_valid = true;
  }
}

int64_t
Http3SettingsFrame::store(QUICStreamIO *stream_io)
{
  // header + payload
  uint8_t header[128]                                   = {0};
  uint8_t payload[Http3SettingsFrame::MAX_PAYLOAD_SIZE] = {0};
  uint8_t *p                                            = payload;
  size_t l                                              = 0;

  for (auto &it : this->_settings) {
    QUICIntUtil::write_QUICVariableInt(static_cast<uint64_t>(it.first), p, &l);
    p += l;
    QUICIntUtil::write_QUICVariableInt(it.second, p, &l);
    p += l;
  }

  // Exercise the requirement that unknown identifiers be ignored. - 4.2.5.1.
  QUICIntUtil::write_QUICVariableInt(static_cast<uint64_t>(Http3SettingsId::UNKNOWN), p, &l);
  p += l;
  QUICIntUtil::write_QUICVariableInt(0, p, &l);
  p += l;

  size_t written     = 0;
  size_t payload_len = p - payload;

  size_t n;
  QUICVariableInt::encode(header, UINT64_MAX, n, static_cast<uint64_t>(this->_type));
  written += n;
  QUICVariableInt::encode(header + written, UINT64_MAX, n, payload_len);
  written += n;

  // Payload
  stream_io->write(header, written);
  stream_io->write(payload, payload_len);

  return written + payload_len;
}

void
Http3SettingsFrame::reset(const uint8_t *buf, size_t len)
{
  this->~Http3SettingsFrame();
  new (this) Http3SettingsFrame(buf, len);
}

bool
Http3SettingsFrame::is_valid() const
{
  return this->_valid;
}

bool
Http3SettingsFrame::contains(Http3SettingsId id) const
{
  auto p = this->_settings.find(id);
  return (p != this->_settings.end());
}

uint64_t
Http3SettingsFrame::get(Http3SettingsId id) const
{
  auto p = this->_settings.find(id);
  if (p != this->_settings.end()) {
    return p->second;
  }

  return 0;
}

void
Http3SettingsFrame::set(Http3SettingsId id, uint64_t value)
{
  this->_settings[id] = value;
}

//
// Http3FrameFactory
//
Http3FrameUPtr
Http3FrameFactory::create_null_frame()
{
  return {nullptr, &Http3FrameDeleter::delete_null_frame};
}

Http3FrameUPtr
Http3FrameFactory::create(const uint8_t *buf, size_t len)
{
  Http3Frame *frame   = nullptr;
  Http3FrameType type = Http3Frame::type(buf, len);

  switch (type) {
  case Http3FrameType::HEADERS:
    frame = http3HeadersFrameAllocator.alloc();
    new (frame) Http3HeadersFrame(buf, len);
    return Http3FrameUPtr(frame, &Http3FrameDeleter::delete_headers_frame);
  case Http3FrameType::DATA:
    frame = http3DataFrameAllocator.alloc();
    new (frame) Http3DataFrame(buf, len);
    return Http3FrameUPtr(frame, &Http3FrameDeleter::delete_data_frame);
  case Http3FrameType::SETTINGS:
    frame = http3SettingsFrameAllocator.alloc();
    new (frame) Http3SettingsFrame(buf, len);
    return Http3FrameUPtr(frame, &Http3FrameDeleter::delete_settings_frame);
  default:
    // Unknown frame
    Debug("http3_frame_factory", "Unknown frame type %hhx", static_cast<uint8_t>(type));
    frame = http3FrameAllocator.alloc();
    new (frame) Http3Frame(buf, len);
    return Http3FrameUPtr(frame, &Http3FrameDeleter::delete_frame);
  }
}

std::shared_ptr<Http3Frame>
Http3FrameFactory::fast_create(const uint8_t *buf, size_t len)
{
  uint64_t frame_length = 0;
  if (Http3Frame::length(buf, len, frame_length) == -1 || frame_length > len) {
    return nullptr;
  }
  Http3FrameType type = Http3Frame::type(buf, len);
  if (type == Http3FrameType::UNKNOWN) {
    if (!this->_unknown_frame) {
      this->_unknown_frame = Http3FrameFactory::create(buf, len);
    } else {
      this->_unknown_frame->reset(buf, len);
    }
    return this->_unknown_frame;
  }

  std::shared_ptr<Http3Frame> frame = this->_reusable_frames[static_cast<uint8_t>(type)];

  if (frame == nullptr) {
    frame = Http3FrameFactory::create(buf, len);
    if (frame != nullptr) {
      this->_reusable_frames[static_cast<uint8_t>(type)] = frame;
    }
  } else {
    frame->reset(buf, len);
  }

  return frame;
}

std::shared_ptr<Http3Frame>
Http3FrameFactory::fast_create(QUICStreamIO &stream_io, size_t frame_len)
{
  uint8_t buf[65536];

  // FIXME DATA frames can be giga bytes
  ink_assert(sizeof(buf) > frame_len);

  if (stream_io.peek(buf, frame_len) < static_cast<int64_t>(frame_len)) {
    // Return if whole frame data is not available
    return nullptr;
  }
  return this->fast_create(buf, frame_len);
}

Http3HeadersFrameUPtr
Http3FrameFactory::create_headers_frame(const uint8_t *header_block, size_t header_block_len)
{
  MIOBuffer buffer;
  buffer.write(header_block, header_block_len);

  return Http3FrameFactory::create_headers_frame(buffer.alloc_reader(), header_block_len);
}

Http3HeadersFrameUPtr
Http3FrameFactory::create_headers_frame(IOBufferReader *header_block_reader, size_t header_block_len)
{
  Http3HeadersFrame *frame = http3HeadersFrameAllocator.alloc();
  new (frame) Http3HeadersFrame(header_block_reader, header_block_len);

  header_block_reader->consume(header_block_len);

  return Http3HeadersFrameUPtr(frame, &Http3FrameDeleter::delete_headers_frame);
}

Http3DataFrameUPtr
Http3FrameFactory::create_data_frame(const uint8_t *payload, size_t payload_len)
{
  MIOBuffer buffer;
  buffer.write(payload, payload_len);

  return Http3FrameFactory::create_data_frame(buffer.alloc_reader(), payload_len);
}

// TODO: This should clone IOBufferBlock chain to avoid memcpy
Http3DataFrameUPtr
Http3FrameFactory::create_data_frame(IOBufferReader *reader, size_t payload_len)
{
  Http3DataFrame *frame = http3DataFrameAllocator.alloc();
  new (frame) Http3DataFrame(reader, payload_len);

  reader->consume(payload_len);

  return Http3DataFrameUPtr(frame, &Http3FrameDeleter::delete_data_frame);
}
