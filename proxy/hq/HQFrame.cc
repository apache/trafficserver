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

#include "ts/Diags.h"
#include "quic/QUICIntUtil.h"
#include "HQFrame.h"

ClassAllocator<HQFrame> hqFrameAllocator("hqFrameAllocator");
ClassAllocator<HQDataFrame> hqDataFrameAllocator("hqDataFrameAllocator");
ClassAllocator<HQHeadersFrame> hqHeadersFrameAllocator("hqHeadersFrameAllocator");

//
// Static functions
//

int
HQFrame::length(const uint8_t *buf, size_t buf_len, uint64_t &length)
{
  size_t length_field_length = 0;
  return QUICVariableInt::decode(length, length_field_length, buf, buf_len);
}

HQFrameType
HQFrame::type(const uint8_t *buf, size_t buf_len)
{
  uint64_t length            = 0;
  size_t length_field_length = 0;
  int ret                    = QUICVariableInt::decode(length, length_field_length, buf, buf_len);
  ink_assert(ret != 1);
  if (buf[length_field_length] <= static_cast<uint8_t>(HQFrameType::X_MAX_DEFINED)) {
    return static_cast<HQFrameType>(buf[length_field_length]);
  } else {
    return HQFrameType::UNKNOWN;
  }
}

//
// Generic Frame
//

HQFrame::HQFrame(const uint8_t *buf, size_t buf_len)
{
  // Length
  size_t length_field_length = 0;
  int ret                    = QUICVariableInt::decode(this->_length, length_field_length, buf, buf_len);
  ink_assert(ret != 1);

  // Type
  this->_type = HQFrameType(buf[length_field_length]);

  // Flags
  this->_flags = buf[length_field_length + 1];

  // Payload offset
  this->_payload_offset = length_field_length + 2;
}

HQFrame::HQFrame(HQFrameType type) : _type(type)
{
}

uint64_t
HQFrame::total_length() const
{
  return this->_payload_offset + this->length();
}

uint64_t
HQFrame::length() const
{
  return this->_length;
}

HQFrameType
HQFrame::type() const
{
  return this->_type;
}

uint8_t
HQFrame::flags() const
{
  return this->_flags;
}

void
HQFrame::store(uint8_t *buf, size_t *len) const
{
  // If you really need this, you should keep the data passed to its constructor
  ink_assert(!"Not supported");
}

void
HQFrame::reset(const uint8_t *buf, size_t len)
{
  this->~HQFrame();
  new (this) HQFrame(buf, len);
}

//
// UNKNOWN Frame
//
HQUnknownFrame::HQUnknownFrame(const uint8_t *buf, size_t buf_len) : HQFrame(buf, buf_len), _buf(buf), _buf_len(buf_len)
{
}

void
HQUnknownFrame::store(uint8_t *buf, size_t *len) const
{
  memcpy(buf, this->_buf, this->_buf_len);
  *len = this->_buf_len;
}

//
// DATA Frame
//
HQDataFrame::HQDataFrame(const uint8_t *buf, size_t buf_len) : HQFrame(buf, buf_len)
{
  this->_payload     = buf + this->_payload_offset;
  this->_payload_len = buf_len - this->_payload_offset;
}

HQDataFrame::HQDataFrame(ats_unique_buf payload, size_t payload_len)
  : HQFrame(HQFrameType::DATA), _payload_uptr(std::move(payload)), _payload_len(payload_len)
{
  this->_length  = this->_payload_len;
  this->_payload = this->_payload_uptr.get();
}

void
HQDataFrame::store(uint8_t *buf, size_t *len) const
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
HQDataFrame::reset(const uint8_t *buf, size_t len)
{
  this->~HQDataFrame();
  new (this) HQDataFrame(buf, len);
}

const uint8_t *
HQDataFrame::payload() const
{
  return this->_payload;
}

uint64_t
HQDataFrame::payload_length() const
{
  return this->_payload_len;
}

//
// HEADERS Frame
//
HQHeadersFrame::HQHeadersFrame(const uint8_t *buf, size_t buf_len) : HQFrame(buf, buf_len)
{
  this->_header_block     = buf + this->_payload_offset;
  this->_header_block_len = buf_len - this->_payload_offset;
}

HQHeadersFrame::HQHeadersFrame(ats_unique_buf header_block, size_t header_block_len)
  : HQFrame(HQFrameType::HEADERS), _header_block_uptr(std::move(header_block)), _header_block_len(header_block_len)
{
  this->_length       = header_block_len;
  this->_header_block = this->_header_block_uptr.get();
}

void
HQHeadersFrame::store(uint8_t *buf, size_t *len) const
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
HQHeadersFrame::reset(const uint8_t *buf, size_t len)
{
  this->~HQHeadersFrame();
  new (this) HQHeadersFrame(buf, len);
}

const uint8_t *
HQHeadersFrame::header_block() const
{
  return this->_header_block;
}

uint64_t
HQHeadersFrame::header_block_length() const
{
  return this->_header_block_len;
}

//
// HQFrameFactory
//
HQFrameUPtr
HQFrameFactory::create_null_frame()
{
  return {nullptr, &HQFrameDeleter::delete_null_frame};
}

HQFrameUPtr
HQFrameFactory::create(const uint8_t *buf, size_t len)
{
  HQFrame *frame   = nullptr;
  HQFrameType type = HQFrame::type(buf, len);

  switch (type) {
  case HQFrameType::HEADERS:
    frame = hqHeadersFrameAllocator.alloc();
    new (frame) HQHeadersFrame(buf, len);
    return HQFrameUPtr(frame, &HQFrameDeleter::delete_headers_frame);
  case HQFrameType::DATA:
    frame = hqDataFrameAllocator.alloc();
    new (frame) HQDataFrame(buf, len);
    return HQFrameUPtr(frame, &HQFrameDeleter::delete_data_frame);
  default:
    // Unknown frame
    Debug("hq_frame_factory", "Unknown frame type %hhx", static_cast<uint8_t>(type));
    frame = hqFrameAllocator.alloc();
    new (frame) HQFrame(buf, len);
    return HQFrameUPtr(frame, &HQFrameDeleter::delete_frame);
  }
}

std::shared_ptr<const HQFrame>
HQFrameFactory::fast_create(const uint8_t *buf, size_t len)
{
  uint64_t frame_length = 0;
  if (HQFrame::length(buf, len, frame_length) == -1 || frame_length > len) {
    return nullptr;
  }
  HQFrameType type = HQFrame::type(buf, len);
  if (type == HQFrameType::UNKNOWN) {
    if (!this->_unknown_frame) {
      this->_unknown_frame = HQFrameFactory::create(buf, len);
    } else {
      this->_unknown_frame->reset(buf, len);
    }
    return _unknown_frame;
  }

  std::shared_ptr<HQFrame> frame = this->_reusable_frames[static_cast<uint8_t>(type)];

  if (frame == nullptr) {
    frame = HQFrameFactory::create(buf, len);
    if (frame != nullptr) {
      this->_reusable_frames[static_cast<uint8_t>(type)] = frame;
    }
  } else {
    frame->reset(buf, len);
  }
  fprintf(stderr, "%p\n", frame.get());

  return frame;
}

HQHeadersFrameUPtr
HQFrameFactory::create_headers_frame(const uint8_t *header_block, size_t header_block_len)
{
  ats_unique_buf buf = ats_unique_malloc(header_block_len);
  memcpy(buf.get(), header_block, header_block_len);

  HQHeadersFrame *frame = hqHeadersFrameAllocator.alloc();
  new (frame) HQHeadersFrame(std::move(buf), header_block_len);
  return HQHeadersFrameUPtr(frame, &HQFrameDeleter::delete_headers_frame);
}

HQDataFrameUPtr
HQFrameFactory::create_data_frame(const uint8_t *payload, size_t payload_len)
{
  ats_unique_buf buf = ats_unique_malloc(payload_len);
  memcpy(buf.get(), payload, payload_len);

  HQDataFrame *frame = hqDataFrameAllocator.alloc();
  new (frame) HQDataFrame(std::move(buf), payload_len);
  return HQDataFrameUPtr(frame, &HQFrameDeleter::delete_data_frame);
}
