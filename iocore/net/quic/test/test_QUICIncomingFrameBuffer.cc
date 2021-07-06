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
#include "quic/QUICBidirectionalStream.h"
#include <memory>

TEST_CASE("QUICIncomingStreamFrameBuffer_fin_offset", "[quic]")
{
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICBidirectionalStream *stream = new QUICBidirectionalStream();
  QUICIncomingStreamFrameBuffer buffer;
  QUICErrorUPtr err = nullptr;

  Ptr<IOBufferBlock> block_1024 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_1024->alloc(BUFFER_SIZE_INDEX_32K);
  block_1024->fill(1024);
  CHECK(block_1024->read_avail() == 1024);

  Ptr<IOBufferBlock> block_0 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_0->alloc(BUFFER_SIZE_INDEX_32K);
  CHECK(block_0->read_avail() == 0);

  SECTION("single frame")
  {
    QUICStreamFrame *stream1_frame_0_r = QUICFrameFactory::create_stream_frame(frame_buf, block_1024, 1, 0, true);

    err = buffer.insert(new QUICStreamFrame(*stream1_frame_0_r));
    CHECK(err == nullptr);

    buffer.clear();
  }

  SECTION("multiple frames")
  {
    uint8_t frame_buf0[QUICFrame::MAX_INSTANCE_SIZE];
    uint8_t frame_buf1[QUICFrame::MAX_INSTANCE_SIZE];
    uint8_t frame_buf2[QUICFrame::MAX_INSTANCE_SIZE];
    uint8_t frame_buf3[QUICFrame::MAX_INSTANCE_SIZE];
    uint8_t frame_buf4[QUICFrame::MAX_INSTANCE_SIZE];
    QUICStreamFrame *stream1_frame_0_r = QUICFrameFactory::create_stream_frame(frame_buf0, block_1024, 1, 0);
    QUICStreamFrame *stream1_frame_1_r = QUICFrameFactory::create_stream_frame(frame_buf1, block_1024, 1, 1024);
    QUICStreamFrame *stream1_frame_2_r = QUICFrameFactory::create_stream_frame(frame_buf2, block_1024, 1, 2048, true);
    QUICStreamFrame *stream1_frame_3_r = QUICFrameFactory::create_stream_frame(frame_buf3, block_1024, 1, 3072, true);
    QUICStreamFrame *stream1_frame_4_r = QUICFrameFactory::create_stream_frame(frame_buf4, block_1024, 1, 4096);

    buffer.insert(new QUICStreamFrame(*stream1_frame_0_r));
    buffer.insert(new QUICStreamFrame(*stream1_frame_1_r));
    buffer.insert(new QUICStreamFrame(*stream1_frame_2_r));
    err = buffer.insert(new QUICStreamFrame(*stream1_frame_3_r));
    CHECK(err->cls == QUICErrorClass::TRANSPORT);
    CHECK(err->code == static_cast<uint16_t>(QUICTransErrorCode::FINAL_SIZE_ERROR));

    buffer.clear();

    QUICIncomingStreamFrameBuffer buffer2;

    buffer2.insert(new QUICStreamFrame(*stream1_frame_3_r));
    buffer2.insert(new QUICStreamFrame(*stream1_frame_0_r));
    buffer2.insert(new QUICStreamFrame(*stream1_frame_1_r));
    err = buffer2.insert(new QUICStreamFrame(*stream1_frame_2_r));
    CHECK(err->cls == QUICErrorClass::TRANSPORT);
    CHECK(err->code == static_cast<uint16_t>(QUICTransErrorCode::FINAL_SIZE_ERROR));

    buffer2.clear();

    QUICIncomingStreamFrameBuffer buffer3;

    buffer3.insert(new QUICStreamFrame(*stream1_frame_4_r));
    err = buffer3.insert(new QUICStreamFrame(*stream1_frame_3_r));
    CHECK(err->cls == QUICErrorClass::TRANSPORT);
    CHECK(err->code == static_cast<uint16_t>(QUICTransErrorCode::FINAL_SIZE_ERROR));

    buffer3.clear();
  }

  SECTION("Pure FIN")
  {
    uint8_t frame_buf0[QUICFrame::MAX_INSTANCE_SIZE];
    uint8_t frame_buf1[QUICFrame::MAX_INSTANCE_SIZE];
    uint8_t frame_buf2[QUICFrame::MAX_INSTANCE_SIZE];
    QUICStreamFrame *stream1_frame_0_r      = QUICFrameFactory::create_stream_frame(frame_buf0, block_1024, 1, 0);
    QUICStreamFrame *stream1_frame_empty    = QUICFrameFactory::create_stream_frame(frame_buf1, block_0, 1, 1024);
    QUICStreamFrame *stream1_frame_pure_fin = QUICFrameFactory::create_stream_frame(frame_buf2, block_0, 1, 1024, true);

    err = buffer.insert(new QUICStreamFrame(*stream1_frame_0_r));
    CHECK(err == nullptr);

    err = buffer.insert(new QUICStreamFrame(*stream1_frame_empty));
    CHECK(err == nullptr);

    err = buffer.insert(new QUICStreamFrame(*stream1_frame_pure_fin));
    CHECK(err == nullptr);

    buffer.clear();
  }

  delete stream;
}

TEST_CASE("QUICIncomingStreamFrameBuffer_pop", "[quic]")
{
  QUICBidirectionalStream *stream = new QUICBidirectionalStream();
  QUICIncomingStreamFrameBuffer buffer;
  QUICErrorUPtr err = nullptr;

  Ptr<IOBufferBlock> block_1024 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_1024->alloc(BUFFER_SIZE_INDEX_32K);
  block_1024->fill(1024);
  CHECK(block_1024->read_avail() == 1024);

  Ptr<IOBufferBlock> block_0 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_0->alloc(BUFFER_SIZE_INDEX_32K);
  CHECK(block_0->read_avail() == 0);

  uint8_t frame_buf0[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf1[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf2[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf3[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf4[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf5[QUICFrame::MAX_INSTANCE_SIZE];
  QUICStreamFrame *stream1_frame_0_r   = QUICFrameFactory::create_stream_frame(frame_buf0, block_1024, 1, 0);
  QUICStreamFrame *stream1_frame_1_r   = QUICFrameFactory::create_stream_frame(frame_buf1, block_1024, 1, 1024);
  QUICStreamFrame *stream1_frame_empty = QUICFrameFactory::create_stream_frame(frame_buf2, block_0, 1, 2048);
  QUICStreamFrame *stream1_frame_2_r   = QUICFrameFactory::create_stream_frame(frame_buf3, block_1024, 1, 2048);
  QUICStreamFrame *stream1_frame_3_r   = QUICFrameFactory::create_stream_frame(frame_buf4, block_1024, 1, 3072);
  QUICStreamFrame *stream1_frame_4_r   = QUICFrameFactory::create_stream_frame(frame_buf5, block_1024, 1, 4096, true);

  buffer.insert(new QUICStreamFrame(*stream1_frame_0_r));
  buffer.insert(new QUICStreamFrame(*stream1_frame_1_r));
  buffer.insert(new QUICStreamFrame(*stream1_frame_empty));
  buffer.insert(new QUICStreamFrame(*stream1_frame_2_r));
  buffer.insert(new QUICStreamFrame(*stream1_frame_3_r));
  buffer.insert(new QUICStreamFrame(*stream1_frame_4_r));
  CHECK(!buffer.empty());

  auto frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 0);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 1024);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 2048);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 3072);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 4096);
  delete frame;
  CHECK(buffer.empty());

  buffer.clear();

  buffer.insert(new QUICStreamFrame(*stream1_frame_4_r));
  buffer.insert(new QUICStreamFrame(*stream1_frame_3_r));
  buffer.insert(new QUICStreamFrame(*stream1_frame_2_r));
  buffer.insert(new QUICStreamFrame(*stream1_frame_1_r));
  buffer.insert(new QUICStreamFrame(*stream1_frame_0_r));
  CHECK(!buffer.empty());

  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 0);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 1024);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 2048);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 3072);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 4096);
  delete frame;
  CHECK(buffer.empty());

  delete stream;
}

TEST_CASE("QUICIncomingStreamFrameBuffer_dup_frame", "[quic]")
{
  QUICBidirectionalStream *stream = new QUICBidirectionalStream();
  QUICIncomingStreamFrameBuffer buffer;
  QUICErrorUPtr err = nullptr;

  Ptr<IOBufferBlock> block_1024 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_1024->alloc(BUFFER_SIZE_INDEX_32K);
  block_1024->fill(1024);
  CHECK(block_1024->read_avail() == 1024);

  Ptr<IOBufferBlock> block_0 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_0->alloc(BUFFER_SIZE_INDEX_32K);
  CHECK(block_0->read_avail() == 0);

  uint8_t frame_buf0[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf1[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf2[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf3[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf4[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf5[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf6[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t frame_buf7[QUICFrame::MAX_INSTANCE_SIZE];
  QUICStreamFrame *stream1_frame_0_r = QUICFrameFactory::create_stream_frame(frame_buf0, block_1024, 1, 0);
  QUICStreamFrame *stream1_frame_1_r = QUICFrameFactory::create_stream_frame(frame_buf1, block_1024, 1, 1024);
  QUICStreamFrame *stream1_frame_2_r = QUICFrameFactory::create_stream_frame(frame_buf2, block_1024, 1, 2048, true);
  QUICStreamFrame *stream1_frame_3_r = QUICFrameFactory::create_stream_frame(frame_buf3, block_1024, 1, 2048, true);

  buffer.insert(new QUICStreamFrame(*stream1_frame_0_r));
  buffer.insert(new QUICStreamFrame(*stream1_frame_1_r));
  buffer.insert(new QUICStreamFrame(*stream1_frame_2_r));
  err = buffer.insert(new QUICStreamFrame(*stream1_frame_3_r));
  CHECK(err == nullptr);

  auto frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 0);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 1024);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 2048);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame == nullptr);
  delete frame;
  CHECK(buffer.empty());

  buffer.clear();

  QUICStreamFrame *stream2_frame_0_r = QUICFrameFactory::create_stream_frame(frame_buf4, block_1024, 1, 0);
  QUICStreamFrame *stream2_frame_1_r = QUICFrameFactory::create_stream_frame(frame_buf5, block_1024, 1, 1024);
  QUICStreamFrame *stream2_frame_2_r = QUICFrameFactory::create_stream_frame(frame_buf6, block_1024, 1, 1024);
  QUICStreamFrame *stream2_frame_3_r = QUICFrameFactory::create_stream_frame(frame_buf7, block_1024, 1, 2048, true);

  buffer.insert(new QUICStreamFrame(*stream2_frame_0_r));
  buffer.insert(new QUICStreamFrame(*stream2_frame_1_r));
  buffer.insert(new QUICStreamFrame(*stream2_frame_2_r));
  err = buffer.insert(new QUICStreamFrame(*stream2_frame_3_r));
  CHECK(err == nullptr);

  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 0);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 1024);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame->offset() == 2048);
  delete frame;
  frame = static_cast<const QUICStreamFrame *>(buffer.pop());
  CHECK(frame == nullptr);
  CHECK(buffer.empty());

  delete stream;
}
