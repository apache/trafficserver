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

#include "catch.hpp"

#include "quic/QUICIncomingFrameBuffer.h"
#include "quic/QUICStream.h"
#include <memory>

TEST_CASE("QUICIncomingStreamFrameBuffer_fin_offset", "[quic]")
{
  QUICStream *stream = new QUICStream();
  QUICIncomingStreamFrameBuffer buffer;
  QUICErrorUPtr err = nullptr;

  Ptr<IOBufferBlock> block_1024 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_1024->alloc();
  block_1024->fill(1024);
  CHECK(block_1024->read_avail() == 1024);

  Ptr<IOBufferBlock> block_0 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_0->alloc();
  CHECK(block_0->read_avail() == 0);

  SECTION("single frame")
  {
    std::shared_ptr<QUICStreamFrame> stream1_frame_0_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 0, true);

    err = buffer.insert(*stream1_frame_0_r);
    CHECK(err == nullptr);
  }

  SECTION("multiple frames")
  {
    std::shared_ptr<QUICStreamFrame> stream1_frame_0_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 0);
    std::shared_ptr<QUICStreamFrame> stream1_frame_1_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 1024);
    std::shared_ptr<QUICStreamFrame> stream1_frame_2_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 2048, true);
    std::shared_ptr<QUICStreamFrame> stream1_frame_3_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 3072, true);
    std::shared_ptr<QUICStreamFrame> stream1_frame_4_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 4096);

    buffer.insert(*stream1_frame_0_r);
    buffer.insert(*stream1_frame_1_r);
    buffer.insert(*stream1_frame_2_r);
    err = buffer.insert(*stream1_frame_3_r);
    CHECK(err->cls == QUICErrorClass::TRANSPORT);
    CHECK(err->code == static_cast<uint16_t>(QUICTransErrorCode::FINAL_OFFSET_ERROR));

    QUICIncomingStreamFrameBuffer buffer2;

    buffer2.insert(*stream1_frame_3_r);
    buffer2.insert(*stream1_frame_0_r);
    buffer2.insert(*stream1_frame_1_r);
    err = buffer2.insert(*stream1_frame_2_r);
    CHECK(err->cls == QUICErrorClass::TRANSPORT);
    CHECK(err->code == static_cast<uint16_t>(QUICTransErrorCode::FINAL_OFFSET_ERROR));

    QUICIncomingStreamFrameBuffer buffer3;

    buffer3.insert(*stream1_frame_4_r);
    err = buffer3.insert(*stream1_frame_3_r);
    CHECK(err->cls == QUICErrorClass::TRANSPORT);
    CHECK(err->code == static_cast<uint16_t>(QUICTransErrorCode::FINAL_OFFSET_ERROR));
  }

  SECTION("Pure FIN")
  {
    std::shared_ptr<QUICStreamFrame> stream1_frame_0_r      = QUICFrameFactory::create_stream_frame(block_1024, 1, 0);
    std::shared_ptr<QUICStreamFrame> stream1_frame_empty    = QUICFrameFactory::create_stream_frame(block_0, 1, 1024);
    std::shared_ptr<QUICStreamFrame> stream1_frame_pure_fin = QUICFrameFactory::create_stream_frame(block_0, 1, 1024, true);

    err = buffer.insert(*stream1_frame_0_r);
    CHECK(err == nullptr);

    err = buffer.insert(*stream1_frame_empty);
    CHECK(err == nullptr);

    err = buffer.insert(*stream1_frame_pure_fin);
    CHECK(err == nullptr);
  }

  delete stream;
}

TEST_CASE("QUICIncomingStreamFrameBuffer_pop", "[quic]")
{
  QUICStream *stream = new QUICStream();
  QUICIncomingStreamFrameBuffer buffer;
  QUICErrorUPtr err = nullptr;

  Ptr<IOBufferBlock> block_1024 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_1024->alloc();
  block_1024->fill(1024);
  CHECK(block_1024->read_avail() == 1024);

  Ptr<IOBufferBlock> block_0 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_0->alloc();
  CHECK(block_0->read_avail() == 0);

  std::shared_ptr<QUICStreamFrame> stream1_frame_0_r   = QUICFrameFactory::create_stream_frame(block_1024, 1, 0);
  std::shared_ptr<QUICStreamFrame> stream1_frame_1_r   = QUICFrameFactory::create_stream_frame(block_1024, 1, 1024);
  std::shared_ptr<QUICStreamFrame> stream1_frame_empty = QUICFrameFactory::create_stream_frame(block_0, 1, 2048);
  std::shared_ptr<QUICStreamFrame> stream1_frame_2_r   = QUICFrameFactory::create_stream_frame(block_1024, 1, 2048);
  std::shared_ptr<QUICStreamFrame> stream1_frame_3_r   = QUICFrameFactory::create_stream_frame(block_1024, 1, 3072);
  std::shared_ptr<QUICStreamFrame> stream1_frame_4_r   = QUICFrameFactory::create_stream_frame(block_1024, 1, 4096, true);

  buffer.insert(*stream1_frame_0_r);
  buffer.insert(*stream1_frame_1_r);
  buffer.insert(*stream1_frame_empty);
  buffer.insert(*stream1_frame_2_r);
  buffer.insert(*stream1_frame_3_r);
  buffer.insert(*stream1_frame_4_r);
  CHECK(!buffer.empty());

  auto frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 0);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 1024);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 2048);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 3072);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 4096);
  CHECK(buffer.empty());

  buffer.clear();

  buffer.insert(*stream1_frame_4_r);
  buffer.insert(*stream1_frame_3_r);
  buffer.insert(*stream1_frame_2_r);
  buffer.insert(*stream1_frame_1_r);
  buffer.insert(*stream1_frame_0_r);
  CHECK(!buffer.empty());

  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 0);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 1024);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 2048);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 3072);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 4096);
  CHECK(buffer.empty());

  delete stream;
}

TEST_CASE("QUICIncomingStreamFrameBuffer_dup_frame", "[quic]")
{
  QUICStream *stream = new QUICStream();
  QUICIncomingStreamFrameBuffer buffer;
  QUICErrorUPtr err = nullptr;

  Ptr<IOBufferBlock> block_1024 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_1024->alloc();
  block_1024->fill(1024);
  CHECK(block_1024->read_avail() == 1024);

  Ptr<IOBufferBlock> block_0 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_0->alloc();
  CHECK(block_0->read_avail() == 0);

  std::shared_ptr<QUICStreamFrame> stream1_frame_0_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 0);
  std::shared_ptr<QUICStreamFrame> stream1_frame_1_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 1024);
  std::shared_ptr<QUICStreamFrame> stream1_frame_2_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 2048, true);
  std::shared_ptr<QUICStreamFrame> stream1_frame_3_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 2048, true);

  buffer.insert(*stream1_frame_0_r);
  buffer.insert(*stream1_frame_1_r);
  buffer.insert(*stream1_frame_2_r);
  err = buffer.insert(*stream1_frame_3_r);
  CHECK(err == nullptr);

  auto frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 0);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 1024);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 2048);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame == nullptr);
  CHECK(buffer.empty());

  buffer.clear();

  std::shared_ptr<QUICStreamFrame> stream2_frame_0_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 0);
  std::shared_ptr<QUICStreamFrame> stream2_frame_1_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 1024);
  std::shared_ptr<QUICStreamFrame> stream2_frame_2_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 1024);
  std::shared_ptr<QUICStreamFrame> stream2_frame_3_r = QUICFrameFactory::create_stream_frame(block_1024, 1, 2048, true);

  buffer.insert(*stream2_frame_0_r);
  buffer.insert(*stream2_frame_1_r);
  buffer.insert(*stream2_frame_2_r);
  err = buffer.insert(*stream2_frame_3_r);
  CHECK(err == nullptr);

  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 0);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 1024);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame->offset() == 2048);
  frame = std::static_pointer_cast<const QUICStreamFrame>(buffer.pop());
  CHECK(frame == nullptr);
  CHECK(buffer.empty());

  delete stream;
}
