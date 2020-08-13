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

#include <memory>

#include "quic/QUICStreamManager.h"
#include "quic/QUICFrame.h"
#include "quic/Mock.h"

MockQUICContext context;

TEST_CASE("QUICStreamManager_NewStream", "[quic]")
{
  QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
  QUICApplicationMap app_map;
  MockQUICConnection connection;
  MockQUICApplication mock_app(&connection);
  app_map.set_default(&mock_app);
  MockQUICConnectionInfoProvider cinfo_provider;
  QUICStreamManager sm(&context, &app_map);

  uint8_t local_tp_buf[] = {
    0x08,      // parameter id - initial_max_streams_bidi
    0x02,      // length of value
    0x40, 0x10 // value
  };
  std::shared_ptr<QUICTransportParameters> local_tp =
    std::make_shared<QUICTransportParametersInEncryptedExtensions>(local_tp_buf, sizeof(local_tp_buf), QUIC_SUPPORTED_VERSIONS[0]);

  uint8_t remote_tp_buf[] = {
    0x08,      // parameter id - initial_max_streams_bidi
    0x02,      // length of value
    0x40, 0x10 // value
  };
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(remote_tp_buf, sizeof(remote_tp_buf), QUIC_SUPPORTED_VERSIONS[0]);

  sm.init_flow_control_params(local_tp, remote_tp);

  // STREAM frames create new streams
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(BUFFER_SIZE_INDEX_32K);
  block->fill(4);
  CHECK(block->read_avail() == 4);

  uint8_t stream_frame_0_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t stream_frame_4_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICFrame *stream_frame_0 = QUICFrameFactory::create_stream_frame(stream_frame_0_buf, block, 0, 0);
  QUICFrame *stream_frame_4 = QUICFrameFactory::create_stream_frame(stream_frame_4_buf, block, 4, 0);
  CHECK(sm.stream_count() == 0);
  sm.handle_frame(level, *stream_frame_0);
  CHECK(sm.stream_count() == 1);
  sm.handle_frame(level, *stream_frame_4);
  CHECK(sm.stream_count() == 2);

  // RESET_STREAM frames create new streams
  uint8_t rst_stream_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICFrame *rst_stream_frame =
    QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 8, static_cast<QUICAppErrorCode>(0x01), 0);
  sm.handle_frame(level, *rst_stream_frame);
  CHECK(sm.stream_count() == 3);

  // MAX_STREAM_DATA frames create new streams
  uint8_t max_stream_data_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICFrame *max_stream_data_frame = QUICFrameFactory::create_max_stream_data_frame(max_stream_data_frame_buf, 0x0c, 0);
  sm.handle_frame(level, *max_stream_data_frame);
  CHECK(sm.stream_count() == 4);

  // STREAM_DATA_BLOCKED frames create new streams
  uint8_t stream_blocked_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICFrame *stream_blocked_frame = QUICFrameFactory::create_stream_data_blocked_frame(stream_blocked_frame_buf, 0x10, 0);
  sm.handle_frame(level, *stream_blocked_frame);
  CHECK(sm.stream_count() == 5);
}

TEST_CASE("QUICStreamManager_first_initial_map", "[quic]")
{
  QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
  QUICApplicationMap app_map;
  MockQUICConnection connection;
  MockQUICApplication mock_app(&connection);
  app_map.set_default(&mock_app);
  MockQUICConnectionInfoProvider cinfo_provider;
  QUICStreamManager sm(&context, &app_map);
  std::shared_ptr<QUICTransportParameters> local_tp  = std::make_shared<QUICTransportParametersInEncryptedExtensions>();
  std::shared_ptr<QUICTransportParameters> remote_tp = std::make_shared<QUICTransportParametersInClientHello>();
  sm.init_flow_control_params(local_tp, remote_tp);

  // STREAM frames create new streams
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(BUFFER_SIZE_INDEX_32K);
  block->fill(4);
  CHECK(block->read_avail() == 4);

  uint8_t stream_frame_0_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICFrame *stream_frame_0 = QUICFrameFactory::create_stream_frame(stream_frame_0_buf, block, 0, 7);

  sm.handle_frame(level, *stream_frame_0);
  CHECK("succeed");
}

TEST_CASE("QUICStreamManager_total_offset_received", "[quic]")
{
  QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
  QUICApplicationMap app_map;
  MockQUICConnection connection;
  MockQUICApplication mock_app(&connection);
  app_map.set_default(&mock_app);
  QUICStreamManager sm(&context, &app_map);

  uint8_t local_tp_buf[] = {
    0x08,                  // parameter id - initial_max_streams_bidi
    0x02,                  // length of value
    0x40, 0x10,            // value
    0x05,                  // parameter id - initial_max_stream_data_bidi_local
    0x04,                  // length of value
    0xbf, 0xff, 0xff, 0xff // value
  };
  std::shared_ptr<QUICTransportParameters> local_tp =
    std::make_shared<QUICTransportParametersInEncryptedExtensions>(local_tp_buf, sizeof(local_tp_buf), QUIC_SUPPORTED_VERSIONS[0]);

  uint8_t remote_tp_buf[] = {
    0x08,                  // parameter id - initial_max_streams_bidi
    0x02,                  // length of value
    0x40, 0x10,            // value
    0x06,                  // parameter id - initial_max_stream_data_bidi_remote
    0x04,                  // length of value
    0xbf, 0xff, 0xff, 0xff // value
  };
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(remote_tp_buf, sizeof(remote_tp_buf), QUIC_SUPPORTED_VERSIONS[0]);

  sm.init_flow_control_params(local_tp, remote_tp);

  // Create a stream with STREAM_DATA_BLOCKED (== noop)
  uint8_t stream_blocked_frame_0_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t stream_blocked_frame_1_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICFrame *stream_blocked_frame_0 = QUICFrameFactory::create_stream_data_blocked_frame(stream_blocked_frame_0_buf, 0, 0);
  QUICFrame *stream_blocked_frame_1 = QUICFrameFactory::create_stream_data_blocked_frame(stream_blocked_frame_1_buf, 4, 0);
  sm.handle_frame(level, *stream_blocked_frame_0);
  sm.handle_frame(level, *stream_blocked_frame_1);
  CHECK(sm.stream_count() == 2);
  CHECK(sm.total_offset_received() == 0);

  // total_offset should be a integer in unit of 1024 octets
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(BUFFER_SIZE_INDEX_32K);
  block->fill(1024);
  CHECK(block->read_avail() == 1024);

  uint8_t stream_frame_1_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICFrame *stream_frame_1 = QUICFrameFactory::create_stream_frame(stream_frame_1_buf, block, 8, 0);
  sm.handle_frame(level, *stream_frame_1);
  CHECK(sm.total_offset_received() == 1024);
}

TEST_CASE("QUICStreamManager_total_offset_sent", "[quic]")
{
  QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
  QUICApplicationMap app_map;
  MockQUICConnection connection;
  MockQUICApplication mock_app(&connection);
  app_map.set_default(&mock_app);
  QUICStreamManager sm(&context, &app_map);

  uint8_t local_tp_buf[] = {
    0x08,                  // parameter id - initial_max_streams_bidi
    0x02,                  // length of value
    0x40, 0x10,            // value
    0x05,                  // parameter id - initial_max_stream_data_bidi_local
    0x04,                  // length of value
    0xbf, 0xff, 0xff, 0xff // value
  };
  std::shared_ptr<QUICTransportParameters> local_tp =
    std::make_shared<QUICTransportParametersInEncryptedExtensions>(local_tp_buf, sizeof(local_tp_buf), QUIC_SUPPORTED_VERSIONS[0]);

  uint8_t remote_tp_buf[] = {
    0x08,                  // parameter id - initial_max_streams_bidi
    0x02,                  // length of value
    0x40, 0x10,            // value
    0x06,                  // parameter id - initial_max_stream_data_bidi_remote
    0x04,                  // length of value
    0xbf, 0xff, 0xff, 0xff // value
  };
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(remote_tp_buf, sizeof(remote_tp_buf), QUIC_SUPPORTED_VERSIONS[0]);

  sm.init_flow_control_params(local_tp, remote_tp);

  // Create a stream with STREAM_DATA_BLOCKED (== noop)
  Ptr<IOBufferBlock> block_3 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_3->alloc(BUFFER_SIZE_INDEX_32K);
  block_3->fill(3);
  CHECK(block_3->read_avail() == 3);

  uint8_t stream_frame0_buf_r[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t stream_frame4_buf_r[QUICFrame::MAX_INSTANCE_SIZE];
  QUICFrame *stream_frame_0_r = QUICFrameFactory::create_stream_frame(stream_frame0_buf_r, block_3, 0, 0);
  QUICFrame *stream_frame_4_r = QUICFrameFactory::create_stream_frame(stream_frame4_buf_r, block_3, 4, 0);
  sm.handle_frame(level, *stream_frame_0_r);
  sm.handle_frame(level, *stream_frame_4_r);
  CHECK(sm.stream_count() == 2);
  CHECK(sm.total_offset_sent() == 0);

  Ptr<IOBufferBlock> block_1024 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_1024->alloc(BUFFER_SIZE_INDEX_32K);
  block_1024->fill(1024);
  CHECK(block_1024->read_avail() == 1024);

  // total_offset should be a integer in unit of octets
  uint8_t frame_buf[4096];
  mock_app.send(reinterpret_cast<uint8_t *>(block_1024->buf()), 1024, 0);
  sm.generate_frame(frame_buf, QUICEncryptionLevel::ONE_RTT, 16384, 16384, 0, 0);
  CHECK(sm.total_offset_sent() == 1024);

  // total_offset should be a integer in unit of octets
  mock_app.send(reinterpret_cast<uint8_t *>(block_1024->buf()), 1024, 4);
  sm.generate_frame(frame_buf, QUICEncryptionLevel::ONE_RTT, 16384, 16384, 0, 0);
  CHECK(sm.total_offset_sent() == 2048);

  // Wait for event processing
  sleep(2);
}

TEST_CASE("QUICStreamManager_max_streams", "[quic]")
{
  QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
  QUICApplicationMap app_map;
  MockQUICConnection connection;
  MockQUICApplication mock_app(&connection);
  app_map.set_default(&mock_app);
  QUICStreamManager sm(&context, &app_map);

  uint8_t local_tp_buf[] = {
    0x08, // parameter id - initial_max_streams_bidi
    0x01, // length of value
    0x03, // value
    0x09, // parameter id - initial_max_streams_uni
    0x01, // length of value
    0x03, // value
  };
  std::shared_ptr<QUICTransportParameters> local_tp =
    std::make_shared<QUICTransportParametersInEncryptedExtensions>(local_tp_buf, sizeof(local_tp_buf), QUIC_SUPPORTED_VERSIONS[0]);

  uint8_t remote_tp_buf[] = {
    0x08, // parameter id - initial_max_streams_bidi
    0x01, // length of value
    0x03, // value
    0x09, // parameter id - initial_max_streams_uni
    0x01, // length of value
    0x03, // value
  };
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(remote_tp_buf, sizeof(remote_tp_buf), QUIC_SUPPORTED_VERSIONS[0]);

  sm.init_flow_control_params(local_tp, remote_tp);

  SECTION("local")
  {
    // RESET_STREAM frames create new streams

    // Bidirectional
    uint8_t rst_stream_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
    QUICFrame *rst_stream_frame =
      QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 1, static_cast<QUICAppErrorCode>(0x01), 0);
    sm.handle_frame(level, *rst_stream_frame);
    REQUIRE(sm.stream_count() == 1);

    rst_stream_frame = QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 5, static_cast<QUICAppErrorCode>(0x01), 0);
    sm.handle_frame(level, *rst_stream_frame);
    REQUIRE(sm.stream_count() == 2);

    rst_stream_frame = QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 9, static_cast<QUICAppErrorCode>(0x01), 0);
    sm.handle_frame(level, *rst_stream_frame);
    REQUIRE(sm.stream_count() == 3);

    rst_stream_frame = QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 13, static_cast<QUICAppErrorCode>(0x01), 0);
    sm.handle_frame(level, *rst_stream_frame);
    REQUIRE(sm.stream_count() == 3);

    // Unidirectional
    rst_stream_frame = QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 3, static_cast<QUICAppErrorCode>(0x01), 0);
    sm.handle_frame(level, *rst_stream_frame);
    REQUIRE(sm.stream_count() == 4);

    rst_stream_frame = QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 7, static_cast<QUICAppErrorCode>(0x01), 0);
    sm.handle_frame(level, *rst_stream_frame);
    REQUIRE(sm.stream_count() == 5);

    rst_stream_frame = QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 11, static_cast<QUICAppErrorCode>(0x01), 0);
    sm.handle_frame(level, *rst_stream_frame);
    REQUIRE(sm.stream_count() == 6);

    rst_stream_frame = QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 15, static_cast<QUICAppErrorCode>(0x01), 0);
    sm.handle_frame(level, *rst_stream_frame);
    REQUIRE(sm.stream_count() == 6);
  }

  SECTION("remote")
  {
    // Bidirection
    QUICConnectionErrorUPtr error;
    QUICStreamId id;

    error = sm.create_bidi_stream(id);
    REQUIRE(!error);
    REQUIRE(id == 0);
    REQUIRE(sm.stream_count() == 1);

    error = sm.create_bidi_stream(id);
    REQUIRE(!error);
    REQUIRE(id == 4);
    REQUIRE(sm.stream_count() == 2);

    error = sm.create_bidi_stream(id);
    REQUIRE(!error);
    REQUIRE(id == 8);
    REQUIRE(sm.stream_count() == 3);

    error = sm.create_bidi_stream(id);
    REQUIRE(error);
    REQUIRE(sm.stream_count() == 3);

    // Unidirection
    error = sm.create_uni_stream(id);
    REQUIRE(!error);
    REQUIRE(id == 2);
    REQUIRE(sm.stream_count() == 4);

    error = sm.create_uni_stream(id);
    REQUIRE(!error);
    REQUIRE(id == 6);
    REQUIRE(sm.stream_count() == 5);

    error = sm.create_uni_stream(id);
    REQUIRE(!error);
    REQUIRE(id == 10);
    REQUIRE(sm.stream_count() == 6);

    error = sm.create_uni_stream(id);
    REQUIRE(error);
    REQUIRE(sm.stream_count() == 6);
  }
}
