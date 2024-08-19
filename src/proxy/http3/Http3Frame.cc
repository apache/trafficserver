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
#include "iocore/net/quic/QUICIntUtil.h"
#include "proxy/http3/Http3Frame.h"
#include "proxy/http3/Http3Config.h"

ClassAllocator<Http3Frame>         http3FrameAllocator("http3FrameAllocator");
ClassAllocator<Http3DataFrame>     http3DataFrameAllocator("http3DataFrameAllocator");
ClassAllocator<Http3HeadersFrame>  http3HeadersFrameAllocator("http3HeadersFrameAllocator");
ClassAllocator<Http3SettingsFrame> http3SettingsFrameAllocator("http3SettingsFrameAllocator");

namespace
{
// The frame type is a variable integer as defined in QUIC (RFC 9000).
constexpr int FRAME_TYPE_MAX_BYTES = 8;
constexpr int HEADER_OVERHEAD      = 10; // This should work as long as a payload length is less than 64 bits

DbgCtl dbg_ctl_http3_frame_factory{"http3_frame_factory"};

} // end anonymous namespace

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
  uint64_t type              = 0;
  size_t   type_field_length = 0;
  int      ret               = QUICVariableInt::decode(type, type_field_length, buf, buf_len);
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

Http3Frame::Http3Frame(IOBufferReader &reader) : _reader(&reader)
{
  // Type field and Length field are variable-length integers, which can be 64 bits for each.
  // 16 bytes (128 bits) is large enough to contain the two fields.
  constexpr int MAX_HEADER_SIZE = 16;
  uint8_t       header[MAX_HEADER_SIZE];
  uint8_t      *p          = reinterpret_cast<uint8_t *>(reader.memcpy(header, MAX_HEADER_SIZE));
  uint64_t      header_len = p - header;

  // Type
  size_t   type_field_length = 0;
  uint64_t type              = 0;
  // Ideally we'd simply pass this->_type to decode, but arm compilers complain with:
  // error: dereferencing type-punned pointer will break strict-aliasing rules
  int ret = QUICVariableInt::decode(type, type_field_length, header, header_len);
  if (ret != 0) {
    this->_is_valid = false;
    return;
  }
  this->_type = static_cast<Http3FrameType>(type);

  // Check if we can proceed to read Length field
  if (header_len <= type_field_length) {
    this->_is_valid = false;
    return;
  }

  // Length
  size_t length_field_length = 0;
  ret = QUICVariableInt::decode(this->_length, length_field_length, header + type_field_length, header_len - type_field_length);
  if (ret != 0) {
    this->_is_valid = false;
    return;
  }

  // Rest of the data is Frame Payload
  this->_reader->consume(type_field_length + length_field_length);
  this->_payload_offset = type_field_length + length_field_length;
}

Http3Frame::Http3Frame(Http3FrameType type) : _type(type) {}

Http3Frame::~Http3Frame()
{
  if (this->_reader) {
    this->_reader->dealloc();
  }
}

bool
Http3Frame::is_valid() const
{
  return this->_is_valid;
}

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

bool
Http3Frame::update()
{
  if (!this->_is_ready) {
    this->_is_ready = this->_parse();
  }

  return this->_is_ready;
}

bool
Http3Frame::_parse()
{
  return true;
}

Ptr<IOBufferBlock>
Http3Frame::to_io_buffer_block() const
{
  Ptr<IOBufferBlock> block;
  return block;
}

void
Http3Frame::reset(IOBufferReader &reader)
{
  this->~Http3Frame();
  new (this) Http3Frame(reader);
}

//
// UNKNOWN Frame
//
Http3UnknownFrame::Http3UnknownFrame(IOBufferReader &reader) : Http3Frame(reader) {}

Ptr<IOBufferBlock>
Http3UnknownFrame::to_io_buffer_block() const
{
  Ptr<IOBufferBlock> block;
  size_t             n = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(HEADER_OVERHEAD + this->length(), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());
  memcpy(block_start, this->_buf, this->_buf_len);
  n += this->_buf_len;

  block->fill(n);
  return block;
}

//
// DATA Frame
//
Http3DataFrame::Http3DataFrame(IOBufferReader &reader) : Http3Frame(reader)
{
  this->_payload_len = this->_length;
}

Http3DataFrame::Http3DataFrame(ats_unique_buf payload, size_t payload_len)
  : Http3Frame(Http3FrameType::DATA), _payload_uptr(std::move(payload)), _payload_len(payload_len)
{
  this->_length  = this->_payload_len;
  this->_payload = this->_payload_uptr.get();
}

Ptr<IOBufferBlock>
Http3DataFrame::to_io_buffer_block() const
{
  Ptr<IOBufferBlock> block;
  size_t             n       = 0;
  size_t             written = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(HEADER_OVERHEAD + this->length(), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  QUICVariableInt::encode(block_start, UINT64_MAX, n, static_cast<uint64_t>(this->_type));
  written += n;
  QUICVariableInt::encode(block_start + written, UINT64_MAX, n, this->_length);
  written += n;
  memcpy(block_start + written, this->_payload, this->_payload_len);
  written += this->_payload_len;

  block->fill(written);
  return block;
}

void
Http3DataFrame::reset(IOBufferReader &reader)
{
  this->~Http3DataFrame();
  new (this) Http3DataFrame(reader);
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

IOBufferReader *
Http3DataFrame::data() const
{
  return _reader;
}

//
// HEADERS Frame
//
Http3HeadersFrame::Http3HeadersFrame(IOBufferReader &reader) : Http3Frame(reader) {}

Http3HeadersFrame::Http3HeadersFrame(ats_unique_buf header_block, size_t header_block_len)
  : Http3Frame(Http3FrameType::HEADERS), _header_block_uptr(std::move(header_block)), _header_block_len(header_block_len)
{
  this->_length       = header_block_len;
  this->_header_block = this->_header_block_uptr.get();
}

Http3HeadersFrame::~Http3HeadersFrame()
{
  if (this->_header_block_uptr == nullptr) {
    ats_free(this->_header_block);
  }
}

Ptr<IOBufferBlock>
Http3HeadersFrame::to_io_buffer_block() const
{
  Ptr<IOBufferBlock> block;
  size_t             n       = 0;
  size_t             written = 0;

  block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(HEADER_OVERHEAD + this->length(), BUFFER_SIZE_INDEX_32K));
  uint8_t *block_start = reinterpret_cast<uint8_t *>(block->start());

  QUICVariableInt::encode(block_start, UINT64_MAX, n, static_cast<uint64_t>(this->_type));
  written += n;
  QUICVariableInt::encode(block_start + written, UINT64_MAX, n, this->_length);
  written += n;
  memcpy(block_start + written, this->_header_block, this->_header_block_len);
  written += this->_header_block_len;

  block->fill(written);
  return block;
}

void
Http3HeadersFrame::reset(IOBufferReader &reader)
{
  this->~Http3HeadersFrame();
  new (this) Http3HeadersFrame(reader);
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

bool
Http3HeadersFrame::_parse()
{
  if (this->_reader->read_avail() != static_cast<int64_t>(this->_length)) {
    // Whole payload is not received yet
    return false;
  }

  this->_header_block_len = this->_length;
  this->_header_block     = static_cast<uint8_t *>(ats_malloc(this->_header_block_len));
  this->_reader->memcpy(this->_header_block, this->_header_block_len);

  return true;
}

//
// SETTINGS Frame
//

Http3SettingsFrame::Http3SettingsFrame(IOBufferReader &reader, uint32_t max_settings)
  : Http3Frame(reader), _max_settings(max_settings)
{
}

Ptr<IOBufferBlock>
Http3SettingsFrame::to_io_buffer_block() const
{
  Ptr<IOBufferBlock> header_block;
  Ptr<IOBufferBlock> payload_block;
  size_t             n       = 0;
  size_t             written = 0;

  payload_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  payload_block->alloc(iobuffer_size_to_index(Http3SettingsFrame::MAX_PAYLOAD_SIZE, BUFFER_SIZE_INDEX_32K));
  uint8_t *payload_block_start = reinterpret_cast<uint8_t *>(payload_block->start());

  for (auto &it : this->_settings) {
    QUICIntUtil::write_QUICVariableInt(static_cast<uint64_t>(it.first), payload_block_start + written, &n);
    written += n;
    QUICIntUtil::write_QUICVariableInt(it.second, payload_block_start + written, &n);
    written += n;
  }

  // Exercise the requirement that unknown identifiers be ignored. - 4.2.5.1.
  QUICIntUtil::write_QUICVariableInt(static_cast<uint64_t>(Http3SettingsId::UNKNOWN), payload_block_start + written, &n);
  written += n;
  QUICIntUtil::write_QUICVariableInt(0, payload_block_start + written, &n);
  written += n;
  payload_block->fill(written);

  size_t payload_len = written;
  written            = 0;

  header_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  header_block->alloc(iobuffer_size_to_index(HEADER_OVERHEAD, BUFFER_SIZE_INDEX_32K));
  uint8_t *header_block_start = reinterpret_cast<uint8_t *>(header_block->start());

  QUICVariableInt::encode(header_block_start, UINT64_MAX, n, static_cast<uint64_t>(this->_type));
  written += n;
  QUICVariableInt::encode(header_block_start + written, UINT64_MAX, n, payload_len);
  written += n;
  header_block->fill(written);

  header_block->next = payload_block;

  return header_block;
}

void
Http3SettingsFrame::reset(IOBufferReader &reader)
{
  this->~Http3SettingsFrame();
  new (this) Http3SettingsFrame(reader);
}

Http3ErrorUPtr
Http3SettingsFrame::get_error() const
{
  return std::make_unique<Http3Error>(Http3ErrorClass::CONNECTION, this->_error_code, this->_error_reason);
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

bool
Http3SettingsFrame::_parse()
{
  if (this->_reader->read_avail() != static_cast<int64_t>(this->_length)) {
    // Whole payload is not received yet
    return false;
  }

  uint8_t *buf = static_cast<uint8_t *>(ats_malloc(this->_length));
  this->_reader->memcpy(buf, this->_length);

  size_t   len       = 0;
  uint32_t nsettings = 0;

  while (len < this->_length) {
    if (nsettings >= this->_max_settings) {
      this->_error_code   = Http3ErrorCode::H3_EXCESSIVE_LOAD;
      this->_error_reason = reinterpret_cast<const char *>("too many settings");
      this->_is_valid     = false;
      break;
    }

    size_t id_len = QUICVariableInt::size(buf + len);
    if ((len + id_len) >=
        this->_length) { // if the id is larger than the buffer or at the boundary of the buffer (i.e. no value), it is invalid
      this->_error_code   = Http3ErrorCode::H3_SETTINGS_ERROR;
      this->_error_reason = reinterpret_cast<const char *>("invalid SETTINGS frame");
      break;
    }
    uint16_t id  = QUICIntUtil::read_QUICVariableInt(buf + len, this->_length - len);
    len         += id_len;

    size_t value_len = QUICVariableInt::size(buf + len);
    if ((len + value_len) > this->_length) {
      this->_error_code   = Http3ErrorCode::H3_SETTINGS_ERROR;
      this->_error_reason = reinterpret_cast<const char *>("invalid SETTINGS frame");
      break;
    }
    uint64_t value  = QUICIntUtil::read_QUICVariableInt(buf + len, this->_length - len);
    len            += value_len;

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
    ++nsettings;
  }

  if (len != this->_length) {
    // TODO: make connection error with HTTP_MALFORMED_FRAME
    this->_is_valid = false;
  }

  ats_free(buf);

  return true;
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
Http3FrameFactory::create(IOBufferReader &reader)
{
  ts::Http3Config::scoped_config params;
  Http3Frame                    *frame = nullptr;

  uint8_t type_buf[FRAME_TYPE_MAX_BYTES] = {0};
  reader.memcpy(type_buf, sizeof(type_buf));
  Http3FrameType type = Http3Frame::type(type_buf, sizeof(type_buf));

  switch (type) {
  case Http3FrameType::HEADERS:
    frame = http3HeadersFrameAllocator.alloc();
    new (frame) Http3HeadersFrame(reader);
    return Http3FrameUPtr(frame, &Http3FrameDeleter::delete_headers_frame);
  case Http3FrameType::DATA:
    frame = http3DataFrameAllocator.alloc();
    new (frame) Http3DataFrame(reader);
    return Http3FrameUPtr(frame, &Http3FrameDeleter::delete_data_frame);
  case Http3FrameType::SETTINGS:
    frame = http3SettingsFrameAllocator.alloc();
    new (frame) Http3SettingsFrame(reader, params->max_settings());
    return Http3FrameUPtr(frame, &Http3FrameDeleter::delete_settings_frame);
  default:
    // Unknown frame
    Dbg(dbg_ctl_http3_frame_factory, "Unknown frame type %hhx", static_cast<uint8_t>(type));
    frame = http3FrameAllocator.alloc();
    new (frame) Http3Frame(reader);
    return Http3FrameUPtr(frame, &Http3FrameDeleter::delete_frame);
  }
}

std::shared_ptr<Http3Frame>
Http3FrameFactory::fast_create(IOBufferReader &reader)
{
  uint8_t type_buf[FRAME_TYPE_MAX_BYTES]{};
  reader.memcpy(type_buf, sizeof(type_buf));
  Http3FrameType type = Http3Frame::type(type_buf, sizeof(type_buf));
  if (type == Http3FrameType::UNKNOWN) {
    if (!this->_unknown_frame) {
      this->_unknown_frame = Http3FrameFactory::create(reader);
    } else {
      this->_unknown_frame->reset(reader);
    }
    return this->_unknown_frame;
  }

  std::shared_ptr<Http3Frame> frame = this->_reusable_frames[static_cast<uint8_t>(type)];

  if (frame == nullptr) {
    frame = Http3FrameFactory::create(reader);
    if (frame != nullptr) {
      this->_reusable_frames[static_cast<uint8_t>(type)] = frame;
    }
  } else {
    frame->reset(reader);
  }

  return frame;
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

Http3HeadersFrameUPtr
Http3FrameFactory::create_headers_frame(IOBufferReader *header_block_reader, size_t header_block_len)
{
  ats_unique_buf buf = ats_unique_malloc(header_block_len);

  int64_t nread;
  while ((nread = header_block_reader->read(buf.get(), header_block_len)) > 0) {
    ;
  }

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

// TODO: This should clone IOBufferBlock chain to avoid memcpy
Http3DataFrameUPtr
Http3FrameFactory::create_data_frame(IOBufferReader *reader, size_t payload_len)
{
  ats_unique_buf buf     = ats_unique_malloc(payload_len);
  size_t         written = 0;

  while (written < payload_len) {
    int64_t len = reader->block_read_avail();

    if (written + len > payload_len) {
      len = payload_len - written;
    }

    memcpy(buf.get() + written, reinterpret_cast<uint8_t *>(reader->start()), len);
    reader->consume(len);
    written += len;
  }

  ink_assert(written == payload_len);

  Http3DataFrame *frame = http3DataFrameAllocator.alloc();
  new (frame) Http3DataFrame(std::move(buf), payload_len);

  return Http3DataFrameUPtr(frame, &Http3FrameDeleter::delete_data_frame);
}
