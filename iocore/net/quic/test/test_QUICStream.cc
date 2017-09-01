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

namespace
{
// Test Data
uint8_t payload[]  = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
uint32_t stream_id = 0x03;

std::shared_ptr<QUICStreamFrame> frame_1 = std::make_shared<QUICStreamFrame>(payload, 2, stream_id, 0);
std::shared_ptr<QUICStreamFrame> frame_2 = std::make_shared<QUICStreamFrame>(payload + 2, 2, stream_id, 2);
std::shared_ptr<QUICStreamFrame> frame_3 = std::make_shared<QUICStreamFrame>(payload + 4, 2, stream_id, 4);
std::shared_ptr<QUICStreamFrame> frame_4 = std::make_shared<QUICStreamFrame>(payload + 6, 2, stream_id, 6);
std::shared_ptr<QUICStreamFrame> frame_5 = std::make_shared<QUICStreamFrame>(payload + 8, 2, stream_id, 8);
std::shared_ptr<QUICStreamFrame> frame_6 = std::make_shared<QUICStreamFrame>(payload + 10, 2, stream_id, 10);
std::shared_ptr<QUICStreamFrame> frame_7 = std::make_shared<QUICStreamFrame>(payload + 12, 2, stream_id, 12);
std::shared_ptr<QUICStreamFrame> frame_8 = std::make_shared<QUICStreamFrame>(payload + 14, 2, stream_id, 14);

MockQUICStreamManager *manager = new MockQUICStreamManager();

TEST_CASE("QUICStream_assembling_byte_stream_1", "[quic]")
{
  MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  IOBufferReader *reader = read_buffer->alloc_reader();
  MockQUICFrameTransmitter tx;

  std::unique_ptr<QUICStream> stream(new QUICStream());
  stream->init(manager, &tx, stream_id, 1024, 1024);
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

TEST_CASE("QUICStream_assembling_byte_stream_2", "[quic]")
{
  MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  IOBufferReader *reader = read_buffer->alloc_reader();
  MockQUICFrameTransmitter tx;

  std::unique_ptr<QUICStream> stream(new QUICStream());
  stream->init(manager, &tx, stream_id);
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

TEST_CASE("QUICStream_assembling_byte_stream_3", "[quic]")
{
  MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  IOBufferReader *reader = read_buffer->alloc_reader();
  MockQUICFrameTransmitter tx;

  std::unique_ptr<QUICStream> stream(new QUICStream());
  stream->init(manager, &tx, stream_id);
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
