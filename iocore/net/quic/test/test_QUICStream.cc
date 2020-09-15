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

#include "quic/QUICBidirectionalStream.h"
#include "quic/QUICUnidirectionalStream.h"
#include "quic/Mock.h"

TEST_CASE("QUICBidiStream", "[quic]")
{
  // Test Data
  uint8_t payload[]        = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  uint32_t stream_id       = 0x03;
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(BUFFER_SIZE_INDEX_32K);
  memcpy(block->start(), payload, sizeof(payload));
  block->fill(sizeof(payload));

  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];

  Ptr<IOBufferBlock> new_block = make_ptr<IOBufferBlock>(block->clone());
  new_block->_end              = new_block->_start + 2;
  QUICStreamFrame frame_1(new_block, stream_id, 0);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_2(new_block, stream_id, 2);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_3(new_block, stream_id, 4);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_4(new_block, stream_id, 6);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_5(new_block, stream_id, 8);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_6(new_block, stream_id, 10);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_7(new_block, stream_id, 12);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_8(new_block, stream_id, 14);
  block->consume(2);

  SECTION("QUICStream_assembling_byte_stream_1")
  {
    MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *reader = read_buffer->alloc_reader();

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICBidirectionalStream> stream(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, 1024, 1024));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

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

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICBidirectionalStream> stream(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

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

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICBidirectionalStream> stream(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

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

  SECTION("QUICStream_flow_control_local", "[quic]")
  {
    std::unique_ptr<QUICError> error = nullptr;

    MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICBidirectionalStream> stream(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, 4096, 4096));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

    Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    block->alloc(BUFFER_SIZE_INDEX_32K);
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

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICBidirectionalStream> stream(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, 4096, 4096));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;

    const char data[1024] = {0};
    QUICFrame *frame      = nullptr;

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);

    // This should not send a frame because of flow control
    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame);
    CHECK(frame->type() == QUICFrameType::STREAM_DATA_BLOCKED);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);

    // Update window
    stream->recv(*std::make_shared<QUICMaxStreamDataFrame>(stream_id, 5120));

    // This should send a frame
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);

    // Update window
    stream->recv(*std::make_shared<QUICMaxStreamDataFrame>(stream_id, 5632));

    // This should send a frame
    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);

    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM_DATA_BLOCKED);

    // Update window
    stream->recv(*std::make_shared<QUICMaxStreamDataFrame>(stream_id, 6144));

    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);
  }

  /*
   * This test does not pass now
   */
  SECTION("Retransmit STREAM frame")
  {
    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICBidirectionalStream> stream(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    const char data1[]        = "this is a test data";
    const char data2[]        = "THIS IS ANOTHER TEST DATA";
    QUICFrame *frame          = nullptr;
    QUICStreamFrame *frame1   = nullptr;
    QUICStreamFrame *frame2   = nullptr;
    uint8_t frame_buf2[QUICFrame::MAX_INSTANCE_SIZE];

    // Write data1
    write_buffer->write(data1, sizeof(data1));
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    // Generate STREAM frame
    frame  = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    frame1 = static_cast<QUICStreamFrame *>(frame);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0) == nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);
    stream->on_frame_lost(frame->id());
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);

    // Write data2
    write_buffer->write(data2, sizeof(data2));
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    // Lost the frame
    stream->on_frame_lost(frame->id());
    // Regenerate a frame
    frame = stream->generate_frame(frame_buf2, level, 4096, 4096, 0, 0);
    // Lost data should be resent first
    frame2 = static_cast<QUICStreamFrame *>(frame);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(frame1->offset() == frame2->offset());
    CHECK(frame1->data_length() == frame2->data_length());
    CHECK(memcmp(frame1->data()->buf(), frame2->data()->buf(), frame1->data_length()) == 0);
  }

  SECTION("Retransmit RESET_STREAM frame")
  {
    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICBidirectionalStream> stream(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    QUICFrame *frame          = nullptr;

    stream->reset(QUICStreamErrorUPtr(new QUICStreamError(stream.get(), QUIC_APP_ERROR_CODE_STOPPING)));
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::RESET_STREAM);
    // Don't send it again until it is considers as lost
    CHECK(stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0) == nullptr);
    // Loss the frame
    stream->on_frame_lost(frame->id());
    // After the loss the frame should be regenerated
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::RESET_STREAM);
  }

  SECTION("Retransmit STOP_SENDING frame")
  {
    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICBidirectionalStream> stream(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    QUICFrame *frame          = nullptr;

    stream->stop_sending(QUICStreamErrorUPtr(new QUICStreamError(stream.get(), QUIC_APP_ERROR_CODE_STOPPING)));
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::STOP_SENDING);
    // Don't send it again until it is considers as lost
    CHECK(stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0) == nullptr);
    // Loss the frame
    stream->on_frame_lost(frame->id());
    // After the loss the frame should be regenerated
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::STOP_SENDING);
  }

  SECTION("Insufficient max_frame_size")
  {
    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();
    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    QUICFrame *frame          = nullptr;

    // STOP_SENDING
    std::unique_ptr<QUICBidirectionalStream> stream1(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    MockContinuation mock_cont1(stream1->mutex);
    stream1->do_io_write(&mock_cont1, INT64_MAX, write_buffer_reader);
    SCOPED_MUTEX_LOCK(lock1, stream1->mutex, this_ethread());
    stream1->stop_sending(QUICStreamErrorUPtr(new QUICStreamError(stream1.get(), QUIC_APP_ERROR_CODE_STOPPING)));
    frame = stream1->generate_frame(frame_buf, level, 4096, 0, 0, 0);
    CHECK(frame == nullptr);

    // RESET_STREAM
    std::unique_ptr<QUICBidirectionalStream> stream2(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    MockContinuation mock_cont2(stream2->mutex);
    stream2->do_io_write(&mock_cont2, INT64_MAX, write_buffer_reader);
    SCOPED_MUTEX_LOCK(lock2, stream2->mutex, this_ethread());
    stream2->reset(QUICStreamErrorUPtr(new QUICStreamError(stream2.get(), QUIC_APP_ERROR_CODE_STOPPING)));
    frame = stream2->generate_frame(frame_buf, level, 4096, 0, 0, 0);
    CHECK(frame == nullptr);

    // STREAM
    std::unique_ptr<QUICBidirectionalStream> stream3(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX, UINT64_MAX));
    MockContinuation mock_cont3(stream3->mutex);
    stream3->do_io_write(&mock_cont3, INT64_MAX, write_buffer_reader);
    SCOPED_MUTEX_LOCK(lock3, stream3->mutex, this_ethread());
    const char data[] = "this is a test data";
    write_buffer->write(data, sizeof(data));
    stream3->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    frame = stream3->generate_frame(frame_buf, level, 4096, 0, 0, 0);
    CHECK(frame == nullptr);
  }
}

TEST_CASE("QUIC receive only stream", "[quic]")
{
  // Test Data
  uint8_t payload[]        = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  uint32_t stream_id       = 0x03;
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(BUFFER_SIZE_INDEX_32K);
  memcpy(block->start(), payload, sizeof(payload));
  block->fill(sizeof(payload));

  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];

  Ptr<IOBufferBlock> new_block = make_ptr<IOBufferBlock>(block->clone());
  new_block->_end              = new_block->_start + 2;
  QUICStreamFrame frame_1(new_block, stream_id, 0);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_2(new_block, stream_id, 2);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_3(new_block, stream_id, 4);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_4(new_block, stream_id, 6);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_5(new_block, stream_id, 8);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_6(new_block, stream_id, 10);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_7(new_block, stream_id, 12);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_8(new_block, stream_id, 14);
  block->consume(2);

  SECTION("QUICStream_assembling_byte_stream_1")
  {
    MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *reader = read_buffer->alloc_reader();

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICReceiveStream> stream(new QUICReceiveStream(&rtt_provider, &cinfo_provider, stream_id, 1024));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

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

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICReceiveStream> stream(new QUICReceiveStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

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

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICReceiveStream> stream(new QUICReceiveStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

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

  SECTION("QUICStream_flow_control_local", "[quic]")
  {
    std::unique_ptr<QUICError> error = nullptr;

    MIOBuffer *read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);

    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICReceiveStream> stream(new QUICReceiveStream(&rtt_provider, &cinfo_provider, stream_id, 4096));
    stream->do_io_read(nullptr, INT64_MAX, read_buffer);

    Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    block->alloc(BUFFER_SIZE_INDEX_32K);
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

  SECTION("Retransmit STOP_SENDING frame")
  {
    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICReceiveStream> stream(new QUICReceiveStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    QUICFrame *frame          = nullptr;

    stream->stop_sending(QUICStreamErrorUPtr(new QUICStreamError(stream.get(), QUIC_APP_ERROR_CODE_STOPPING)));
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::STOP_SENDING);
    // Don't send it again until it is considers as lost
    CHECK(stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0) == nullptr);
    // Loss the frame
    stream->on_frame_lost(frame->id());
    // After the loss the frame should be regenerated
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::STOP_SENDING);
  }

  SECTION("Insufficient max_frame_size")
  {
    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    QUICFrame *frame          = nullptr;

    // STOP_SENDING
    std::unique_ptr<QUICReceiveStream> stream1(new QUICReceiveStream(&rtt_provider, &cinfo_provider, stream_id, UINT64_MAX));
    MockContinuation mock_cont1(stream1->mutex);
    SCOPED_MUTEX_LOCK(lock1, stream1->mutex, this_ethread());
    stream1->stop_sending(QUICStreamErrorUPtr(new QUICStreamError(stream1.get(), QUIC_APP_ERROR_CODE_STOPPING)));
    frame = stream1->generate_frame(frame_buf, level, 4096, 0, 0, 0);
    CHECK(frame == nullptr);
  }
}

TEST_CASE("QUIC send only stream", "[quic]")
{
  // Test Data
  uint8_t payload[]        = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  uint32_t stream_id       = 0x03;
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(BUFFER_SIZE_INDEX_32K);
  memcpy(block->start(), payload, sizeof(payload));
  block->fill(sizeof(payload));

  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];

  Ptr<IOBufferBlock> new_block = make_ptr<IOBufferBlock>(block->clone());
  new_block->_end              = new_block->_start + 2;
  QUICStreamFrame frame_1(new_block, stream_id, 0);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_2(new_block, stream_id, 2);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_3(new_block, stream_id, 4);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_4(new_block, stream_id, 6);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_5(new_block, stream_id, 8);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_6(new_block, stream_id, 10);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_7(new_block, stream_id, 12);
  block->consume(2);

  new_block       = block->clone();
  new_block->_end = new_block->_start + 2;
  QUICStreamFrame frame_8(new_block, stream_id, 14);
  block->consume(2);

  SECTION("QUICStream_flow_control_remote", "[quic]")
  {
    std::unique_ptr<QUICError> error = nullptr;

    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();

    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICSendStream> stream(new QUICSendStream(&cinfo_provider, stream_id, 4096));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;

    const char data[1024] = {0};
    QUICFrame *frame      = nullptr;

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);

    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);

    // This should not send a frame because of flow control
    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame);
    CHECK(frame->type() == QUICFrameType::STREAM_DATA_BLOCKED);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);

    // Update window
    stream->recv(*std::make_shared<QUICMaxStreamDataFrame>(stream_id, 5120));

    // This should send a frame
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);

    // Update window
    stream->recv(*std::make_shared<QUICMaxStreamDataFrame>(stream_id, 5632));

    // This should send a frame
    write_buffer->write(data, 1024);
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);

    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM_DATA_BLOCKED);

    // Update window
    stream->recv(*std::make_shared<QUICMaxStreamDataFrame>(stream_id, 6144));

    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);
  }

  /*
   * This test does not pass now
   */
  SECTION("Retransmit STREAM frame")
  {
    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();

    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICSendStream> stream(new QUICSendStream(&cinfo_provider, stream_id, UINT64_MAX));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    const char data1[]        = "this is a test data";
    const char data2[]        = "THIS IS ANOTHER TEST DATA";
    QUICFrame *frame          = nullptr;
    QUICStreamFrame *frame1   = nullptr;
    QUICStreamFrame *frame2   = nullptr;
    uint8_t frame_buf2[QUICFrame::MAX_INSTANCE_SIZE];

    // Write data1
    write_buffer->write(data1, sizeof(data1));
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    // Generate STREAM frame
    frame  = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    frame1 = static_cast<QUICStreamFrame *>(frame);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0) == nullptr);
    CHECK(stream->will_generate_frame(level, 0, false, 0) == false);
    stream->on_frame_lost(frame->id());
    CHECK(stream->will_generate_frame(level, 0, false, 0) == true);

    // Write data2
    write_buffer->write(data2, sizeof(data2));
    stream->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    // Lost the frame
    stream->on_frame_lost(frame->id());
    // Regenerate a frame
    frame = stream->generate_frame(frame_buf2, level, 4096, 4096, 0, 0);
    // Lost data should be resent first
    frame2 = static_cast<QUICStreamFrame *>(frame);
    CHECK(frame->type() == QUICFrameType::STREAM);
    CHECK(frame1->offset() == frame2->offset());
    CHECK(frame1->data_length() == frame2->data_length());
    CHECK(memcmp(frame1->data()->buf(), frame2->data()->buf(), frame1->data_length()) == 0);
  }

  SECTION("Retransmit RESET_STREAM frame")
  {
    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();

    MockQUICConnectionInfoProvider cinfo_provider;
    std::unique_ptr<QUICSendStream> stream(new QUICSendStream(&cinfo_provider, stream_id, UINT64_MAX));
    SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());

    MockContinuation mock_cont(stream->mutex);
    stream->do_io_write(&mock_cont, INT64_MAX, write_buffer_reader);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    QUICFrame *frame          = nullptr;

    stream->reset(QUICStreamErrorUPtr(new QUICStreamError(stream.get(), QUIC_APP_ERROR_CODE_STOPPING)));
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::RESET_STREAM);
    // Don't send it again until it is considers as lost
    CHECK(stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0) == nullptr);
    // Loss the frame
    stream->on_frame_lost(frame->id());
    // After the loss the frame should be regenerated
    frame = stream->generate_frame(frame_buf, level, 4096, 4096, 0, 0);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::RESET_STREAM);
  }

  SECTION("Insufficient max_frame_size")
  {
    MIOBuffer *write_buffer             = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
    IOBufferReader *write_buffer_reader = write_buffer->alloc_reader();
    MockQUICConnectionInfoProvider cinfo_provider;
    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    QUICFrame *frame          = nullptr;

    // RESET_STREAM
    std::unique_ptr<QUICSendStream> stream2(new QUICSendStream(&cinfo_provider, stream_id, UINT64_MAX));
    MockContinuation mock_cont2(stream2->mutex);
    stream2->do_io_write(&mock_cont2, INT64_MAX, write_buffer_reader);
    SCOPED_MUTEX_LOCK(lock2, stream2->mutex, this_ethread());
    stream2->reset(QUICStreamErrorUPtr(new QUICStreamError(stream2.get(), QUIC_APP_ERROR_CODE_STOPPING)));
    frame = stream2->generate_frame(frame_buf, level, 4096, 0, 0, 0);
    CHECK(frame == nullptr);

    // STREAM
    std::unique_ptr<QUICSendStream> stream3(new QUICSendStream(&cinfo_provider, stream_id, UINT64_MAX));
    MockContinuation mock_cont3(stream3->mutex);
    stream3->do_io_write(&mock_cont3, INT64_MAX, write_buffer_reader);
    SCOPED_MUTEX_LOCK(lock3, stream3->mutex, this_ethread());
    const char data[] = "this is a test data";
    write_buffer->write(data, sizeof(data));
    stream3->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    frame = stream3->generate_frame(frame_buf, level, 4096, 0, 0, 0);
    CHECK(frame == nullptr);
  }
}

TEST_CASE("will_generate_frame", "[quic]")
{
  SECTION("Return false if a stream has not initialized for IO")
  {
    QUICRTTMeasure rtt_provider;
    MockQUICConnectionInfoProvider cinfo_provider;
    uint8_t buf[128];

    std::unique_ptr<QUICBidirectionalStream> stream_bidi(
      new QUICBidirectionalStream(&rtt_provider, &cinfo_provider, 0, 1024, 1024));
    CHECK(stream_bidi->will_generate_frame(QUICEncryptionLevel::ONE_RTT, 0, false, 0) == false);
    CHECK(stream_bidi->generate_frame(buf, QUICEncryptionLevel::ONE_RTT, 1024, 1024, 0, 0) == nullptr);

    std::unique_ptr<QUICSendStream> stream_uni1(new QUICSendStream(&cinfo_provider, 2, 1024));
    CHECK(stream_uni1->will_generate_frame(QUICEncryptionLevel::ONE_RTT, 0, false, 0) == false);
    CHECK(stream_uni1->generate_frame(buf, QUICEncryptionLevel::ONE_RTT, 1024, 1024, 0, 0) == nullptr);

    std::unique_ptr<QUICReceiveStream> stream_uni2(new QUICReceiveStream(&rtt_provider, &cinfo_provider, 3, 1024));
    CHECK(stream_uni2->will_generate_frame(QUICEncryptionLevel::ONE_RTT, 0, false, 0) == false);
    CHECK(stream_uni2->generate_frame(buf, QUICEncryptionLevel::ONE_RTT, 1024, 1024, 0, 0) == nullptr);
  }
}
