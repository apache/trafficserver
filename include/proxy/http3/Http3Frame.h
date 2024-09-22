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

#pragma once

#include "tscore/Allocator.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_assert.h"
#include "iocore/net/quic/QUICApplication.h"
#include "proxy/http3/Http3Types.h"

class Http3Frame
{
public:
  constexpr static size_t MAX_FRAM_HEADER_OVERHEAD = 128; ///< Type (i) + Length (i)

  Http3Frame() {}
  Http3Frame(IOBufferReader &reader);
  Http3Frame(Http3FrameType type);
  virtual ~Http3Frame();

  bool                       is_valid() const;
  uint64_t                   total_length() const;
  uint64_t                   length() const;
  Http3FrameType             type() const;
  bool                       update();
  virtual Ptr<IOBufferBlock> to_io_buffer_block() const;
  virtual void               reset(IOBufferReader &reader);
  static int                 length(const uint8_t *buf, size_t buf_len, uint64_t &length);
  static Http3FrameType      type(const uint8_t *buf, size_t buf_len);

protected:
  IOBufferReader *_reader           = nullptr;
  bool            _finished_reading = false;
  uint64_t        _length           = 0;
  Http3FrameType  _type             = Http3FrameType::UNKNOWN;
  size_t          _payload_offset   = 0;
  bool            _is_valid         = true;

  virtual bool _parse();

private:
  bool _is_ready = false;
};

class Http3UnknownFrame : public Http3Frame
{
public:
  Http3UnknownFrame() : Http3Frame() {}
  Http3UnknownFrame(IOBufferReader &reader);

  Ptr<IOBufferBlock> to_io_buffer_block() const override;

protected:
  const uint8_t *_buf     = nullptr;
  size_t         _buf_len = 0;
};

//
// DATA Frame
//

class Http3DataFrame : public Http3Frame
{
public:
  Http3DataFrame() : Http3Frame() {}
  Http3DataFrame(IOBufferReader &reader);
  Http3DataFrame(IOBufferReader &reader, size_t payload_len);

  Ptr<IOBufferBlock> to_io_buffer_block() const override;
  void               reset(IOBufferReader &reader) override;

  uint64_t payload_length() const;

  IOBufferReader *data() const;

private:
  // Head of IOBufferBlock chain to send
  Ptr<IOBufferBlock> _whole_frame;
  uint64_t           _payload_len = 0;
};

//
// HEADERS Frame
//

class Http3HeadersFrame : public Http3Frame
{
public:
  Http3HeadersFrame() : Http3Frame() {}
  Http3HeadersFrame(IOBufferReader &reader);
  Http3HeadersFrame(ats_unique_buf header_block, size_t header_block_len);
  ~Http3HeadersFrame();

  Ptr<IOBufferBlock> to_io_buffer_block() const override;
  void               reset(IOBufferReader &reader) override;

  const uint8_t *header_block() const;
  uint64_t       header_block_length() const;

protected:
  bool _parse() override;

private:
  uint8_t       *_header_block      = nullptr;
  ats_unique_buf _header_block_uptr = {nullptr};
  size_t         _header_block_len  = 0;
};

//
// SETTINGS Frame
//

class Http3SettingsFrame : public Http3Frame
{
public:
  Http3SettingsFrame() : Http3Frame(Http3FrameType::SETTINGS) {}
  Http3SettingsFrame(IOBufferReader &reader, uint32_t max_settings = 0);

  static constexpr size_t                         MAX_PAYLOAD_SIZE = 60;
  static constexpr std::array<Http3SettingsId, 4> VALID_SETTINGS_IDS{
    Http3SettingsId::HEADER_TABLE_SIZE,
    Http3SettingsId::MAX_FIELD_SECTION_SIZE,
    Http3SettingsId::QPACK_BLOCKED_STREAMS,
    Http3SettingsId::NUM_PLACEHOLDERS,
  };

  Ptr<IOBufferBlock> to_io_buffer_block() const override;
  void               reset(IOBufferReader &reader) override;

  Http3ErrorUPtr get_error() const;

  bool     contains(Http3SettingsId id) const;
  uint64_t get(Http3SettingsId id) const;
  void     set(Http3SettingsId id, uint64_t value);

protected:
  bool _parse() override;

private:
  uint32_t                            _max_settings = 0;
  std::map<Http3SettingsId, uint64_t> _settings;
  Http3ErrorCode                      _error_code;
  const char                         *_error_reason = nullptr;
};

using Http3FrameDeleterFunc  = void (*)(Http3Frame *p);
using Http3FrameUPtr         = std::unique_ptr<Http3Frame, Http3FrameDeleterFunc>;
using Http3DataFrameUPtr     = std::unique_ptr<Http3DataFrame, Http3FrameDeleterFunc>;
using Http3HeadersFrameUPtr  = std::unique_ptr<Http3HeadersFrame, Http3FrameDeleterFunc>;
using Http3SettingsFrameUPtr = std::unique_ptr<Http3SettingsFrame, Http3FrameDeleterFunc>;

using Http3FrameDeleterFunc = void (*)(Http3Frame *p);
using Http3FrameUPtr        = std::unique_ptr<Http3Frame, Http3FrameDeleterFunc>;
using Http3DataFrameUPtr    = std::unique_ptr<Http3DataFrame, Http3FrameDeleterFunc>;
using Http3HeadersFrameUPtr = std::unique_ptr<Http3HeadersFrame, Http3FrameDeleterFunc>;

extern ClassAllocator<Http3Frame>         http3FrameAllocator;
extern ClassAllocator<Http3DataFrame>     http3DataFrameAllocator;
extern ClassAllocator<Http3HeadersFrame>  http3HeadersFrameAllocator;
extern ClassAllocator<Http3SettingsFrame> http3SettingsFrameAllocator;

class Http3FrameDeleter
{
public:
  static void
  delete_null_frame(Http3Frame *frame)
  {
    ink_assert(frame == nullptr);
  }

  static void
  delete_frame(Http3Frame *frame)
  {
    frame->~Http3Frame();
    http3FrameAllocator.free(static_cast<Http3Frame *>(frame));
  }

  static void
  delete_data_frame(Http3Frame *frame)
  {
    frame->~Http3Frame();
    http3DataFrameAllocator.free(static_cast<Http3DataFrame *>(frame));
  }

  static void
  delete_headers_frame(Http3Frame *frame)
  {
    frame->~Http3Frame();
    http3HeadersFrameAllocator.free(static_cast<Http3HeadersFrame *>(frame));
  }

  static void
  delete_settings_frame(Http3Frame *frame)
  {
    frame->~Http3Frame();
    http3SettingsFrameAllocator.free(static_cast<Http3SettingsFrame *>(frame));
  }
};

//
// Http3FrameFactory
//
class Http3FrameFactory
{
public:
  /*
   * This is for an empty Http3FrameUPtr.
   * Empty frames are used for variable initialization and return value of frame creation failure
   */
  static Http3FrameUPtr create_null_frame();

  /*
   * This is used for creating a Http3Frame object based on received data.
   */
  static Http3FrameUPtr create(IOBufferReader &reader);

  /*
   * This works almost the same as create() but it reuses created objects for performance.
   * If you create a frame object which has the same frame type that you created before, the object will be reset by new data.
   */
  std::shared_ptr<Http3Frame> fast_create(IOBufferReader &reader);

  /*
   * Creates a HEADERS frame.
   */
  static Http3HeadersFrameUPtr create_headers_frame(const uint8_t *header_block, size_t header_block_len);
  static Http3HeadersFrameUPtr create_headers_frame(IOBufferReader *header_block_reader, size_t header_block_len);

  /*
   * Creates a DATA frame.
   */
  static Http3DataFrameUPtr create_data_frame(IOBufferReader *reader, size_t data_len);

private:
  std::shared_ptr<Http3Frame> _unknown_frame        = nullptr;
  std::shared_ptr<Http3Frame> _reusable_frames[256] = {nullptr};
};
