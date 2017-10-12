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

#include "quic/QUICStream.h"
#include "quic/Mock.h"

TEST_CASE("QUICStream", "[quic]")
{
  // Test Data
  uint8_t payload[]  = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  uint32_t stream_id = 0x03;

  ats_unique_buf payload1 = ats_unique_malloc(2);
  memcpy(payload1.get(), payload, 2);
  std::shared_ptr<QUICStreamFrame> frame_1 = std::make_shared<QUICStreamFrame>(std::move(payload1), 2, stream_id, 0);

  ats_unique_buf payload2 = ats_unique_malloc(2);
  memcpy(payload2.get(), payload + 2, 2);
  std::shared_ptr<QUICStreamFrame> frame_2 = std::make_shared<QUICStreamFrame>(std::move(payload2), 2, stream_id, 2);

  ats_unique_buf payload3 = ats_unique_malloc(2);
  memcpy(payload3.get(), payload + 4, 2);
  std::shared_ptr<QUICStreamFrame> frame_3 = std::make_shared<QUICStreamFrame>(std::move(payload3), 2, stream_id, 4);

  ats_unique_buf payload4 = ats_unique_malloc(2);
  memcpy(payload4.get(), payload + 6, 2);
  std::shared_ptr<QUICStreamFrame> frame_4 = std::make_shared<QUICStreamFrame>(std::move(payload4), 2, stream_id, 6);

  ats_unique_buf payload5 = ats_unique_malloc(2);
  memcpy(payload5.get(), payload + 8, 2);
  std::shared_ptr<QUICStreamFrame> frame_5 = std::make_shared<QUICStreamFrame>(std::move(payload5), 2, stream_id, 8);

  ats_unique_buf payload6 = ats_unique_malloc(2);
  memcpy(payload6.get(), payload + 10, 2);
  std::shared_ptr<QUICStreamFrame> frame_6 = std::make_shared<QUICStreamFrame>(std::move(payload6), 2, stream_id, 10);

  ats_unique_buf payload7 = ats_unique_malloc(2);
  memcpy(payload7.get(), payload + 12, 2);
  std::shared_ptr<QUICStreamFrame> frame_7 = std::make_shared<QUICStreamFrame>(std::move(payload7), 2, stream_id, 12);

  ats_unique_buf payload8 = ats_unique_malloc(2);
  memcpy(payload8.get(), payload + 14, 2);
  std::shared_ptr<QUICStreamFrame> frame_8 = std::make_shared<QUICStreamFrame>(std::move(payload8), 2, stream_id, 14);

  SECTION("QUICStream_assembling_byte_stream_1")
  {
    MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *reader = read_buffer->alloc_reader();
    MockQUICFrameTransmitter tx;

    std::unique_ptr<QUICStream> stream(new QUICStream());
    stream->init(&tx, 0, stream_id, 1024, 1024);
    stream->do_io_read(nullptr, 0, read_buffer);

    stream->recv(frame_1);
    stream->recv(frame_2);
    stream->recv(frame_3);
    stream->recv(frame_4);
    stream->recv(frame_5);
    stream->recv(frame_6);
    stream->recv(frame_7);
    stream->recv(frame_8);

    uint8_t buf[32];
    int64_t len = reader->read_avail();
    reader->read(buf, len);

    CHECK(len == 16);
    CHECK(memcmp(buf, payload, len) == 0);
  }

  SECTION("QUICStream_assembling_byte_stream_2")
  {
    MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *reader = read_buffer->alloc_reader();
    MockQUICFrameTransmitter tx;

    std::unique_ptr<QUICStream> stream(new QUICStream());
    stream->init(&tx, 0, stream_id);
    stream->do_io_read(nullptr, 0, read_buffer);

    stream->recv(frame_8);
    stream->recv(frame_7);
    stream->recv(frame_6);
    stream->recv(frame_5);
    stream->recv(frame_4);
    stream->recv(frame_3);
    stream->recv(frame_2);
    stream->recv(frame_1);

    uint8_t buf[32];
    int64_t len = reader->read_avail();
    reader->read(buf, len);

    CHECK(len == 16);
    CHECK(memcmp(buf, payload, len) == 0);
  }

  SECTION("QUICStream_assembling_byte_stream_3")
  {
    MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *reader = read_buffer->alloc_reader();
    MockQUICFrameTransmitter tx;

    std::unique_ptr<QUICStream> stream(new QUICStream());
    stream->init(&tx, 0, stream_id);
    stream->do_io_read(nullptr, 0, read_buffer);

    stream->recv(frame_8);
    stream->recv(frame_7);
    stream->recv(frame_6);
    stream->recv(frame_7); // duplicated frame
    stream->recv(frame_5);
    stream->recv(frame_3);
    stream->recv(frame_1);
    stream->recv(frame_2);
    stream->recv(frame_4);
    stream->recv(frame_5); // duplicated frame

    uint8_t buf[32];
    int64_t len = reader->read_avail();
    reader->read(buf, len);

    CHECK(len == 16);
    CHECK(memcmp(buf, payload, len) == 0);
  }
}
