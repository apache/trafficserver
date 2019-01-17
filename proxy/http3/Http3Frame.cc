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
  uint64_t length            = 0;
  size_t length_field_length = 0;
  int ret                    = QUICVariableInt::decode(length, length_field_length, buf, buf_len);
  ink_assert(ret != 1);
  if (buf[length_field_length] <= static_cast<uint8_t>(Http3FrameType::X_MAX_DEFINED)) {
    return static_cast<Http3FrameType>(buf[length_field_length]);
  } else {
    return Http3FrameType::UNKNOWN;
  }
}

//
// Generic Frame
//

Http3Frame::Http3Frame(const uint8_t *buf, size_t buf_len)
{
  // Length
  size_t length_field_length = 0;
  int ret                    = QUICVariableInt::decode(this->_length, length_field_length, buf, buf_len);
  ink_assert(ret != 1);

  // Type
  this->_type = Http3FrameType(buf[length_field_length]);

  // Flags
  this->_flags = buf[length_field_length + 1];

  // Payload offset
  this->_payload_offset = length_field_length + 2;
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
  return this->_type;
}

uint8_t
Http3Frame::flags() const
{
  return this->_flags;
}

void
Http3Frame::store(uint8_t *buf, size_t *len) const
{
  // If you really need this, you should keep the data passed to its constructor
  ink_assert(!"Not supported");
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

void
Http3UnknownFrame::store(uint8_t *buf, size_t *len) const
{
  memcpy(buf, this->_buf, this->_buf_len);
  *len = this->_buf_len;
}

//
// DATA Frame
//
Http3DataFrame::Http3DataFrame(const uint8_t *buf, size_t buf_len) : Http3Frame(buf, buf_len)
{
  this->_payload     = buf + this->_payload_offset;
  this->_payload_len = buf_len - this->_payload_offset;
}

Http3DataFrame::Http3DataFrame(ats_unique_buf payload, size_t payload_len)
  : Http3Frame(Http3FrameType::DATA), _payload_uptr(std::move(payload)), _payload_len(payload_len)
{
  this->_length  = this->_payload_len;
  this->_payload = this->_payload_uptr.get();
}

void
Http3DataFrame::store(uint8_t *buf, size_t *len) const
{
  size_t written = 0;
  QUICVariableInt::encode(buf, UINT64_MAX, written, this->_length);
  buf[written++] = static_cast<uint8_t>(this->_type);
  buf[written++] = this->_flags;
  memcpy(buf + written, this->_payload, this->_payload_len);
  written += this->_payload_len;
  *len = written;
}

void
Http3DataFrame::reset(const uint8_t *buf, size_t len)
{
  this->~Http3DataFrame();
  new (this) Http3DataFrame(buf, len);
}

const uint8_t *
Http3DataFrame::payload() const
{
  return this->_payload;
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
  this->_header_block     = buf + this->_payload_offset;
  this->_header_block_len = buf_len - this->_payload_offset;
}

Http3HeadersFrame::Http3HeadersFrame(ats_unique_buf header_block, size_t header_block_len)
  : Http3Frame(Http3FrameType::HEADERS), _header_block_uptr(std::move(header_block)), _header_block_len(header_block_len)
{
  this->_length       = header_block_len;
  this->_header_block = this->_header_block_uptr.get();
}

void
Http3HeadersFrame::store(uint8_t *buf, size_t *len) const
{
  size_t written = 0;
  QUICVariableInt::encode(buf, UINT64_MAX, written, this->_length);
  buf[written++] = static_cast<uint8_t>(this->_type);
  buf[written++] = this->_flags;
  memcpy(buf + written, this->_header_block, this->_header_block_len);
  written += this->_header_block_len;
  *len = written;
}

void
Http3HeadersFrame::reset(const uint8_t *buf, size_t len)
{
  this->~Http3HeadersFrame();
  new (this) Http3HeadersFrame(buf, len);
}

const uint8_t *
Http3HeadersFrame::header_block() const
{
  return this->_header_block;
}

uint64_t
Http3HeadersFrame::header_block_length() const
{
  return this->_header_block_len;
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
  default:
    // Unknown frame
    Debug("http3_frame_factory", "Unknown frame type %hhx", static_cast<uint8_t>(type));
    frame = http3FrameAllocator.alloc();
    new (frame) Http3Frame(buf, len);
    return Http3FrameUPtr(frame, &Http3FrameDeleter::delete_frame);
  }
}

std::shared_ptr<const Http3Frame>
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
    return _unknown_frame;
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

std::shared_ptr<const Http3Frame>
Http3FrameFactory::fast_create(QUICStreamIO &stream_io, size_t len)
{
  uint8_t buf[65536];

  // FIXME DATA frames can be giga bytes
  ink_assert(sizeof(buf) > len);

  if (stream_io.peek(buf, sizeof(buf) < len)) {
    // Return if whole frame data is not available
    return nullptr;
  }
  return this->fast_create(buf, len);
}

Http3HeadersFrameUPtr
Http3FrameFactory::create_headers_frame(const uint8_t *header_block, size_t header_block_len)
{
  ats_unique_buf buf = ats_unique_malloc(header_block_len);
  memcpy(buf.get(), header_block, header_block_len);

  Http3HeadersFrame *frame = http3HeadersFrameAllocator.alloc();
  new (frame) Http3HeadersFrame(std::move(buf), header_block_len);
  return Http3HeadersFrameUPtr(frame, &Http3FrameDeleter::delete_headers_frame);
}

Http3DataFrameUPtr
Http3FrameFactory::create_data_frame(const uint8_t *payload, size_t payload_len)
{
  ats_unique_buf buf = ats_unique_malloc(payload_len);
  memcpy(buf.get(), payload, payload_len);

  Http3DataFrame *frame = http3DataFrameAllocator.alloc();
  new (frame) Http3DataFrame(std::move(buf), payload_len);
  return Http3DataFrameUPtr(frame, &Http3FrameDeleter::delete_data_frame);
}
