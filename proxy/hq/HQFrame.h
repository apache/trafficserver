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

#include "ts/Allocator.h"
#include "ts/ink_memory.h"
#include "ts/ink_assert.h"
#include "HQTypes.h"

class HQFrame
{
public:
  HQFrame() {}
  HQFrame(const uint8_t *buf, size_t len);
  HQFrame(HQFrameType type);
  virtual ~HQFrame() {}

  uint64_t total_length() const;
  uint64_t length() const;
  HQFrameType type() const;
  uint8_t flags() const;
  virtual void store(uint8_t *buf, size_t *len) const;
  virtual void reset(const uint8_t *buf, size_t len);
  static int length(const uint8_t *buf, size_t buf_len, uint64_t &length);
  static HQFrameType type(const uint8_t *buf, size_t buf_len);

protected:
  uint64_t _length       = 0;
  HQFrameType _type      = HQFrameType::UNKNOWN;
  uint8_t _flags         = 0;
  size_t _payload_offset = 0;
};

class HQUnknownFrame : public HQFrame
{
public:
  HQUnknownFrame() : HQFrame() {}
  HQUnknownFrame(const uint8_t *buf, size_t len);

  void store(uint8_t *buf, size_t *len) const override;

protected:
  const uint8_t *_buf = nullptr;
  size_t _buf_len     = 0;
};

//
// DATA Frame
//

class HQDataFrame : public HQFrame
{
public:
  HQDataFrame() : HQFrame() {}
  HQDataFrame(const uint8_t *buf, size_t len);
  HQDataFrame(ats_unique_buf payload, size_t payload_len);

  void store(uint8_t *buf, size_t *len) const override;
  void reset(const uint8_t *buf, size_t len) override;

  const uint8_t *payload() const;
  uint64_t payload_length() const;

private:
  const uint8_t *_payload      = nullptr;
  ats_unique_buf _payload_uptr = {nullptr, [](void *p) { ats_free(p); }};
  size_t _payload_len          = 0;
};

//
// HEADERS Frame
//

class HQHeadersFrame : public HQFrame
{
public:
  HQHeadersFrame() : HQFrame() {}
  HQHeadersFrame(const uint8_t *buf, size_t len);
  HQHeadersFrame(ats_unique_buf header_block, size_t header_block_len);

  void store(uint8_t *buf, size_t *len) const override;
  void reset(const uint8_t *buf, size_t len) override;

  const uint8_t *header_block() const;
  uint64_t header_block_length() const;

private:
  const uint8_t *_header_block      = nullptr;
  ats_unique_buf _header_block_uptr = {nullptr, [](void *p) { ats_free(p); }};
  size_t _header_block_len          = 0;
};

using HQFrameDeleterFunc = void (*)(HQFrame *p);
using HQFrameUPtr        = std::unique_ptr<HQFrame, HQFrameDeleterFunc>;
using HQDataFrameUPtr    = std::unique_ptr<HQDataFrame, HQFrameDeleterFunc>;
using HQHeadersFrameUPtr = std::unique_ptr<HQHeadersFrame, HQFrameDeleterFunc>;

extern ClassAllocator<HQFrame> hqFrameAllocator;
extern ClassAllocator<HQDataFrame> hqDataFrameAllocator;
extern ClassAllocator<HQHeadersFrame> hqHeadersFrameAllocator;

class HQFrameDeleter
{
public:
  static void
  delete_null_frame(HQFrame *frame)
  {
    ink_assert(frame == nullptr);
  }

  static void
  delete_frame(HQFrame *frame)
  {
    frame->~HQFrame();
    hqFrameAllocator.free(static_cast<HQFrame *>(frame));
  }

  static void
  delete_data_frame(HQFrame *frame)
  {
    frame->~HQFrame();
    hqDataFrameAllocator.free(static_cast<HQDataFrame *>(frame));
  }

  static void
  delete_headers_frame(HQFrame *frame)
  {
    frame->~HQFrame();
    hqHeadersFrameAllocator.free(static_cast<HQHeadersFrame *>(frame));
  }
};

//
// HQFrameFactory
//
class HQFrameFactory
{
public:
  /*
   * This is for an empty HQFrameUPtr.
   * Empty frames are used for variable initialization and return value of frame creation failure
   */
  static HQFrameUPtr create_null_frame();

  /*
   * This is used for creating a HQFrame object based on received data.
   */
  static HQFrameUPtr create(const uint8_t *buf, size_t len);

  /*
   * This works almost the same as create() but it reuses created objects for performance.
   * If you create a frame object which has the same frame type that you created before, the object will be reset by new data.
   */
  std::shared_ptr<const HQFrame> fast_create(const uint8_t *buf, size_t len);

  /*
   * Creates a HEADERS frame.
   */
  static HQHeadersFrameUPtr create_headers_frame(const uint8_t *header_block, size_t header_block_len);

  /*
   * Creates a DATA frame.
   */
  static HQDataFrameUPtr create_data_frame(const uint8_t *data, size_t data_len);

private:
  std::shared_ptr<HQFrame> _unknown_frame        = nullptr;
  std::shared_ptr<HQFrame> _reusable_frames[256] = {nullptr};
};
