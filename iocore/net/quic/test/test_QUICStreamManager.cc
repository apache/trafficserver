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

TEST_CASE("QUICStreamManager_NewStream", "[quic]")
{
  QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
  QUICApplicationMap app_map;
  MockQUICConnection connection;
  MockQUICApplication mock_app(&connection);
  app_map.set_default(&mock_app);
  MockQUICConnectionInfoProvider cinfo_provider;
  MockQUICRTTProvider rtt_provider;
  QUICStreamManager sm(&cinfo_provider, &rtt_provider, &app_map);

  uint8_t local_tp_buf[] = {
    0x00, 0x00, 0x00, 0x00, // initial version
    0x00,                   // size of supported versions
    0x00, 0x06,             // size of parameters
    0x00, 0x02,             // parameter id - initial_max_bidi_streams
    0x00, 0x02,             // length of value
    0x00, 0x10              // value
  };
  std::shared_ptr<QUICTransportParameters> local_tp =
    std::make_shared<QUICTransportParametersInEncryptedExtensions>(local_tp_buf, sizeof(local_tp_buf));

  uint8_t remote_tp_buf[] = {
    0x00, 0x00, 0x00, 0x00, // initial version
    0x00, 0x06,             // size of parameters
    0x00, 0x02,             // parameter id - initial_max_bidi_streams
    0x00, 0x02,             // length of value
    0x00, 0x10              // value
  };
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(remote_tp_buf, sizeof(remote_tp_buf));

  sm.init_flow_control_params(local_tp, remote_tp);

  // STREAM frames create new streams
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc();
  block->fill(4);
  CHECK(block->read_avail() == 4);

  std::shared_ptr<QUICFrame> stream_frame_0 = QUICFrameFactory::create_stream_frame(block, 0, 0);
  std::shared_ptr<QUICFrame> stream_frame_4 = QUICFrameFactory::create_stream_frame(block, 4, 0);
  CHECK(sm.stream_count() == 0);
  sm.handle_frame(level, *stream_frame_0);
  CHECK(sm.stream_count() == 1);
  sm.handle_frame(level, *stream_frame_4);
  CHECK(sm.stream_count() == 2);

  // RESET_STREAM frames create new streams
  std::shared_ptr<QUICFrame> rst_stream_frame =
    QUICFrameFactory::create_rst_stream_frame(8, static_cast<QUICAppErrorCode>(0x01), 0);
  sm.handle_frame(level, *rst_stream_frame);
  CHECK(sm.stream_count() == 3);

  // MAX_STREAM_DATA frames create new streams
  std::shared_ptr<QUICFrame> max_stream_data_frame = QUICFrameFactory::create_max_stream_data_frame(0x0c, 0);
  sm.handle_frame(level, *max_stream_data_frame);
  CHECK(sm.stream_count() == 4);

  // STREAM_DATA_BLOCKED frames create new streams
  std::shared_ptr<QUICFrame> stream_blocked_frame = QUICFrameFactory::create_stream_blocked_frame(0x10, 0);
  sm.handle_frame(level, *stream_blocked_frame);
  CHECK(sm.stream_count() == 5);

  // Set local maximum stream id
  sm.set_max_stream_id(0x14);
  std::shared_ptr<QUICFrame> stream_blocked_frame_x = QUICFrameFactory::create_stream_blocked_frame(0x18, 0);
  sm.handle_frame(level, *stream_blocked_frame_x);
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
  MockQUICRTTProvider rtt_provider;
  QUICStreamManager sm(&cinfo_provider, &rtt_provider, &app_map);
  std::shared_ptr<QUICTransportParameters> local_tp =
    std::make_shared<QUICTransportParametersInEncryptedExtensions>(static_cast<QUICVersion>(0));
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(static_cast<QUICVersion>(0));
  sm.init_flow_control_params(local_tp, remote_tp);

  // STREAM frames create new streams
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc();
  block->fill(4);
  CHECK(block->read_avail() == 4);

  std::shared_ptr<QUICFrame> stream_frame_0 = QUICFrameFactory::create_stream_frame(block, 0, 7);

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
  QUICStreamManager sm(new MockQUICConnectionInfoProvider(), new MockQUICRTTProvider(), &app_map);

  uint8_t local_tp_buf[] = {
    0x00, 0x00, 0x00, 0x00, // initial version
    0x00,                   // size of supported versions
    0x00, 0x0e,             // size of parameters
    0x00, 0x02,             // parameter id - initial_max_bidi_streams
    0x00, 0x02,             // length of value
    0x00, 0x10,             // value
    0x00, 0x00,             // parameter id - initial_max_stream_data_bidi_local
    0x00, 0x04,             // length of value
    0xff, 0xff, 0xff, 0xff  // value
  };
  std::shared_ptr<QUICTransportParameters> local_tp =
    std::make_shared<QUICTransportParametersInEncryptedExtensions>(local_tp_buf, sizeof(local_tp_buf));

  uint8_t remote_tp_buf[] = {
    0x00, 0x00, 0x00, 0x00, // initial version
    0x00, 0x0e,             // size of parameters
    0x00, 0x02,             // parameter id - initial_max_bidi_streams
    0x00, 0x02,             // length of value
    0x00, 0x10,             // value
    0x00, 0x0a,             // parameter id - initial_max_stream_data_bidi_remote
    0x00, 0x04,             // length of value
    0xff, 0xff, 0xff, 0xff  // value
  };
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(remote_tp_buf, sizeof(remote_tp_buf));

  sm.init_flow_control_params(local_tp, remote_tp);

  // Create a stream with STREAM_DATA_BLOCKED (== noop)
  std::shared_ptr<QUICFrame> stream_blocked_frame_0 = QUICFrameFactory::create_stream_blocked_frame(0, 0);
  std::shared_ptr<QUICFrame> stream_blocked_frame_1 = QUICFrameFactory::create_stream_blocked_frame(4, 0);
  sm.handle_frame(level, *stream_blocked_frame_0);
  sm.handle_frame(level, *stream_blocked_frame_1);
  CHECK(sm.stream_count() == 2);
  CHECK(sm.total_offset_received() == 0);

  // total_offset should be a integer in unit of 1024 octets
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc();
  block->fill(1024);
  CHECK(block->read_avail() == 1024);

  std::shared_ptr<QUICFrame> stream_frame_1 = QUICFrameFactory::create_stream_frame(block, 8, 0);
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
  QUICStreamManager sm(new MockQUICConnectionInfoProvider(), new MockQUICRTTProvider(), &app_map);

  uint8_t local_tp_buf[] = {
    0x00, 0x00, 0x00, 0x00, // initial version
    0x00,                   // size of supported versions
    0x00, 0x06,             // size of parameters
    0x00, 0x02,             // parameter id - initial_max_bidi_streams
    0x00, 0x02,             // length of value
    0x00, 0x10              // value
  };
  std::shared_ptr<QUICTransportParameters> local_tp =
    std::make_shared<QUICTransportParametersInEncryptedExtensions>(local_tp_buf, sizeof(local_tp_buf));

  uint8_t remote_tp_buf[] = {
    0x00, 0x00, 0x00, 0x00, // initial version
    0x00, 0x06,             // size of parameters
    0x00, 0x02,             // parameter id - initial_max_bidi_streams
    0x00, 0x02,             // length of value
    0x00, 0x10              // value
  };
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(remote_tp_buf, sizeof(remote_tp_buf));

  sm.init_flow_control_params(local_tp, remote_tp);

  // Create a stream with STREAM_DATA_BLOCKED (== noop)
  Ptr<IOBufferBlock> block_3 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_3->alloc();
  block_3->fill(3);
  CHECK(block_3->read_avail() == 3);

  std::shared_ptr<QUICFrame> stream_frame_0_r = QUICFrameFactory::create_stream_frame(block_3, 0, 0);
  std::shared_ptr<QUICFrame> stream_frame_4_r = QUICFrameFactory::create_stream_frame(block_3, 4, 0);
  sm.handle_frame(level, *stream_frame_0_r);
  sm.handle_frame(level, *stream_frame_4_r);
  CHECK(sm.stream_count() == 2);
  CHECK(sm.total_offset_sent() == 0);

  Ptr<IOBufferBlock> block_1024 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_1024->alloc();
  block_1024->fill(1024);
  CHECK(block_1024->read_avail() == 1024);

  // total_offset should be a integer in unit of octets
  QUICFrameUPtr stream_frame_0 = QUICFrameFactory::create_stream_frame(block_1024, 0, 0);
  mock_app.send(reinterpret_cast<uint8_t *>(block_1024->buf()), 1024, 0);
  sm.add_total_offset_sent(1024);
  sleep(2);
  CHECK(sm.total_offset_sent() == 1024);

  // total_offset should be a integer in unit of octets
  QUICFrameUPtr stream_frame_4 = QUICFrameFactory::create_stream_frame(block_1024, 4, 0);
  mock_app.send(reinterpret_cast<uint8_t *>(block_1024->buf()), 1024, 4);
  sm.add_total_offset_sent(1024);
  sleep(2);
  CHECK(sm.total_offset_sent() == 2048);
}
