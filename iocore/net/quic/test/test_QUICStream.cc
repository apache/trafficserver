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
  uint8_t payload[]        = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  uint32_t stream_id       = 0x03;
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc();
  memcpy(block->start(), payload, sizeof(payload));
  block->fill(sizeof(payload));

  Ptr<IOBufferBlock> new_block             = make_ptr<IOBufferBlock>(block->clone());
  new_block->_end                          = new_block->_start + 2;
  std::shared_ptr<QUICStreamFrame> frame_1 = std::make_shared<QUICStreamFrame>(new_block, stream_id, 0);
  block->consume(2);

  new_block                                = block->clone();
  new_block->_end                          = new_block->_start + 2;
  std::shared_ptr<QUICStreamFrame> frame_2 = std::make_shared<QUICStreamFrame>(new_block, stream_id, 2);
  block->consume(2);

  new_block                                = block->clone();
  new_block->_end                          = new_block->_start + 2;
  std::shared_ptr<QUICStreamFrame> frame_3 = std::make_shared<QUICStreamFrame>(new_block, stream_id, 4);
  block->consume(2);

  new_block                                = block->clone();
  new_block->_end                          = new_block->_start + 2;
  std::shared_ptr<QUICStreamFrame> frame_4 = std::make_shared<QUICStreamFrame>(new_block, stream_id, 6);
  block->consume(2);

  new_block                                = block->clone();
  new_block->_end                          = new_block->_start + 2;
  std::shared_ptr<QUICStreamFrame> frame_5 = std::make_shared<QUICStreamFrame>(new_block, stream_id, 8);
  block->consume(2);

  new_block                                = block->clone();
  new_block->_end                          = new_block->_start + 2;
  std::shared_ptr<QUICStreamFrame> frame_6 = std::make_shared<QUICStreamFrame>(new_block, stream_id, 10);
  block->consume(2);

  new_block                                = block->clone();
  new_block->_end                          = new_block->_start + 2;
  std::shared_ptr<QUICStreamFrame> frame_7 = std::make_shared<QUICStreamFrame>(new_block, stream_id, 12);
  block->consume(2);

  new_block                                = block->clone();
  new_block->_end                          = new_block->_start + 2;
  std::shared_ptr<QUICStreamFrame> frame_8 = std::make_shared<QUICStreamFrame>(new_block, stream_id, 14);
  block->consume(2);

  SECTION("QUICStream_assembling_byte_stream_1")
  {
    MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *reader = read_buffer->alloc_reader();

    MockQUICRTTProvider rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICStream> stream(new QUICStream(&rtt_provider, &cinfo_provider, stream_id, 1024, 1024));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

    stream->recv(*frame_1);
    stream->recv(*frame_2);
    stream->recv(*frame_3);
    stream->recv(*frame_4);
    stream->recv(*frame_5);
    stream->recv(*frame_6);
    stream->recv(*frame_7);
    stream->recv(*frame_8);

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

    MockQUICRTTProvider rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICStream> stream(new QUICStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

    stream->recv(*frame_8);
    stream->recv(*frame_7);
    stream->recv(*frame_6);
    stream->recv(*frame_5);
    stream->recv(*frame_4);
    stream->recv(*frame_3);
    stream->recv(*frame_2);
    stream->recv(*frame_1);

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

    MockQUICRTTProvider rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICStream> stream(new QUICStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

    stream->recv(*frame_8);
    stream->recv(*frame_7);
    stream->recv(*frame_6);
    stream->recv(*frame_7); // duplicated frame
    stream->recv(*frame_5);
    stream->recv(*frame_3);
    stream->recv(*frame_1);
    stream->recv(*frame_2);
    stream->recv(*frame_4);
    stream->recv(*frame_5); // duplicated frame

    uint8_t buf[32];
    int64_t len = reader->read_avail();
    reader->read(buf, len);

    CHECK(len == 16);
    CHECK(memcmp(buf, payload, len) == 0);
  }

  SECTION("QUICStream_flow_control_local", "[quic]")
  {
    std::unique_ptr<QUICError> error = nullptr;

    MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);

    MockQUICRTTProvider rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICStream> stream(new QUICStream(&rtt_provider, &cinfo_provider, stream_id, 4096, 4096));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

    Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    block->alloc();
    block->fill(1024);

    // Start with 1024 but not 0 so received frames won't be processed
    error = stream->recv(*std::make_shared<QUICStreamFrame>(block, stream_id, 1024));
    CHECK(error == nullptr);
    // duplicate
    error = stream->recv(*std::make_shared<QUICStreamFrame>(block, stream_id, 1024));
    CHECK(error == nullptr);
    error = stream->recv(*std::make_shared<QUICStreamFrame>(block, stream_id, 3072));
    CHECK(error == nullptr);
    // delay
    error = stream->recv(*std::make_shared<QUICStreamFrame>(block, stream_id, 2048));
    CHECK(error == nullptr);
    // all frames should be processed
    error = stream->recv(*std::make_shared<QUICStreamFrame>(block, stream_id, 0));
    CHECK(error == nullptr);
    // start again without the first block
    error = stream->recv(*std::make_shared<QUICStreamFrame>(block, stream_id, 5120));
    CHECK(error == nullptr);
    // this should exceed the limit
    error = stream->recv(*std::make_shared<QUICStreamFrame>(block, stream_id, 8192));
    CHECK(error->cls == QUICErrorClass::TRANSPORT);
    CHECK(error->code == static_cast<uint16_t>(QUICTransErrorCode::FLOW_CONTROL_ERROR));
  }

  SECTION("QUICStream_flow_control_remote", "[quic]")
  {
    std::unique_ptr<QUICError> error = nullptr;

    MIOBuffer *read_buffer              = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();

    MockQUICRTTProvider rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICStream> stream(new QUICStream(&rtt_provider, &cinfo_provider, stream_id, 4096, 4096));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;

    const char data[1024] = {0};
    QUICFrameUPtr frame   = QUICFrameFactory::create_null_frame();

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level) == true);
    frame = stream->generate_frame(level, 4096, 4096);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level) == false);

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level) == true);
    frame = stream->generate_frame(level, 4096, 4096);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level) == false);

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level) == true);
    frame = stream->generate_frame(level, 4096, 4096);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level) == false);

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level) == true);
    frame = stream->generate_frame(level, 4096, 4096);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level) == false);

    // This should not send a frame because of flow control
    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level) == true);
    frame = stream->generate_frame(level, 4096, 4096);
    CHECK(frame);
    CHECK(frame->type() == QUICFrameType::STREAM_BLOCKED);
    CHECK(stream->will_generate_frame(level) == true);

    // Update window
    stream->recv(*std::make_shared<QUICMaxStreamDataFrame>(stream_id, 5120));

    // This should send a frame
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level) == true);
    frame = stream->generate_frame(level, 4096, 4096);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level) == false);

    // Update window
    stream->recv(*std::make_shared<QUICMaxStreamDataFrame>(stream_id, 5632));

    // This should send a frame
    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level) == true);
    frame = stream->generate_frame(level, 4096, 4096);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level) == true);

    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level) == true);
    frame = stream->generate_frame(level, 4096, 4096);
    CHECK(frame->type() == QUICFrameType::STREAM_BLOCKED);

    // Update window
    stream->recv(*std::make_shared<QUICMaxStreamDataFrame>(stream_id, 6144));

    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level) == true);
    frame = stream->generate_frame(level, 4096, 4096);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level) == false);
  }

  /*
   * This test does not pass now
   *
  SECTION("Retransmit STREAM frame")
  {
    MIOBuffer *write_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();

    MockQUICRTTProvider rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICStream> stream(new QUICStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    const char data1[] = "this is a test data";
    const char data2[] = "THIS IS ANOTHER TEST DATA";
    QUICFrameUPtr frame   = QUICFrameFactory::create_null_frame();

    QUICFrameUPtr frame1 = QUICFrameFactory::create_null_frame();
    QUICFrameUPtr frame2 = QUICFrameFactory::create_null_frame();

    // Write data1
    write_buffer->write(data1, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    // Generate STREAM frame
    frame1 = stream->generate_frame(level, 4096, 4096);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->generate_frame(level, 4096, 4096) == nullptr);
    // Write data2
    write_buffer->write(data1, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    // Lost the frame
    stream->on_frame_lost(frame->id());
    // Regenerate a frame
    frame2 = stream->generate_frame(level, 4096, 4096);
    // Lost data should be resent first
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(frame1->offset() == frame2->offset());
    CHECK(frame1->data_length() == frame2->data_length());
    CHECK(memcmp(frame1->data(), frame2->data(), frame1->data_length());
  }
  */

  SECTION("Retransmit RST_STREAM frame")
  {
    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();

    MockQUICRTTProvider rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICStream> stream(new QUICStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    QUICFrameUPtr frame       = QUICFrameFactory::create_null_frame();

    stream->reset(QUICStreamErrorUPtr(new QUICStreamError(stream.get(), QUIC_APP_ERROR_CODE_STOPPING)));
    frame = stream->generate_frame(level, 4096, 4096);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::RST_STREAM);
    // Don't send it again untill it is considers as lost
    CHECK(stream->generate_frame(level, 4096, 4096) == nullptr);
    // Loss the frame
    stream->on_frame_lost(frame->id());
    // After the loss the frame should be regenerated
    frame = stream->generate_frame(level, 4096, 4096);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::RST_STREAM);
  }

  SECTION("Retransmit STOP_SENDING frame")
  {
    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();

    MockQUICRTTProvider rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICStream> stream(new QUICStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    QUICFrameUPtr frame       = QUICFrameFactory::create_null_frame();

    stream->stop_sending(QUICStreamErrorUPtr(new QUICStreamError(stream.get(), QUIC_APP_ERROR_CODE_STOPPING)));
    frame = stream->generate_frame(level, 4096, 4096);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::STOP_SENDING);
    // Don't send it again untill it is considers as lost
    CHECK(stream->generate_frame(level, 4096, 4096) == nullptr);
    // Loss the frame
    stream->on_frame_lost(frame->id());
    // After the loss the frame should be regenerated
    frame = stream->generate_frame(level, 4096, 4096);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::STOP_SENDING);
  }
}
