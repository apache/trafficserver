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
  MockQUICFrameTransmitter tx;
  QUICApplicationMap app_map;
  MockQUICApplication mock_app;
  app_map.set_default(&mock_app);
  QUICStreamManager sm(0, &tx, &app_map);
  std::shared_ptr<QUICTransportParameters> local_tp = std::make_shared<QUICTransportParametersInEncryptedExtensions>();
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(static_cast<QUICVersion>(0), static_cast<QUICVersion>(0));
  sm.init_flow_control_params(local_tp, remote_tp);

  // STREAM frames create new streams
  std::shared_ptr<QUICFrame> stream_frame_0 =
    QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("abc"), 3, 0, 0);
  std::shared_ptr<QUICFrame> stream_frame_1 =
    QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("abc"), 3, 1, 0);
  CHECK(sm.stream_count() == 0);
  sm.handle_frame(stream_frame_0);
  CHECK(sm.stream_count() == 1);
  sm.handle_frame(stream_frame_1);
  CHECK(sm.stream_count() == 2);

  // RST_STREAM frames create new streams
  std::shared_ptr<QUICFrame> rst_stream_frame =
    QUICFrameFactory::create_rst_stream_frame(2, static_cast<QUICAppErrorCode>(0x01), 0);
  sm.handle_frame(rst_stream_frame);
  CHECK(sm.stream_count() == 3);

  // MAX_STREAM_DATA frames create new streams
  std::shared_ptr<QUICFrame> max_stream_data_frame = QUICFrameFactory::create_max_stream_data_frame(3, 0);
  sm.handle_frame(max_stream_data_frame);
  CHECK(sm.stream_count() == 4);

  // STREAM_BLOCKED frames create new streams
  std::shared_ptr<QUICFrame> stream_blocked_frame = QUICFrameFactory::create_stream_blocked_frame(4);
  sm.handle_frame(stream_blocked_frame);
  CHECK(sm.stream_count() == 5);

  // Set local maximum stream id
  sm.set_max_stream_id(4);
  std::shared_ptr<QUICFrame> stream_blocked_frame_x = QUICFrameFactory::create_stream_blocked_frame(5);
  sm.handle_frame(stream_blocked_frame_x);
  CHECK(sm.stream_count() == 5);
}

TEST_CASE("QUICStreamManager_first_initial_map", "[quic]")
{
  MockQUICFrameTransmitter tx;
  QUICApplicationMap app_map;
  MockQUICApplication mock_app;
  app_map.set_default(&mock_app);
  QUICStreamManager sm(0, &tx, &app_map);
  std::shared_ptr<QUICTransportParameters> local_tp = std::make_shared<QUICTransportParametersInEncryptedExtensions>();
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(static_cast<QUICVersion>(0), static_cast<QUICVersion>(0));
  sm.init_flow_control_params(local_tp, remote_tp);

  // STREAM frames create new streams
  std::shared_ptr<QUICFrame> stream_frame_0 =
    QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("abc"), 3, 0, 7);

  sm.handle_frame(stream_frame_0);
  CHECK("succeed");
}

TEST_CASE("QUICStreamManager_total_offset_received", "[quic]")
{
  MockQUICFrameTransmitter tx;
  QUICApplicationMap app_map;
  MockQUICApplication mock_app;
  app_map.set_default(&mock_app);
  QUICStreamManager sm(0, &tx, &app_map);
  std::shared_ptr<QUICTransportParameters> local_tp = std::make_shared<QUICTransportParametersInEncryptedExtensions>();
  local_tp->add(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA,
                std::unique_ptr<QUICTransportParameterValue>(new QUICTransportParameterValue(4096, 4)));
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(static_cast<QUICVersion>(0), static_cast<QUICVersion>(0));
  sm.init_flow_control_params(local_tp, remote_tp);
  uint8_t data[1024] = {0};

  // Create a stream with STREAM_BLOCKED (== noop)
  std::shared_ptr<QUICFrame> stream_blocked_frame_0 = QUICFrameFactory::create_stream_blocked_frame(0);
  std::shared_ptr<QUICFrame> stream_blocked_frame_1 = QUICFrameFactory::create_stream_blocked_frame(1);
  sm.handle_frame(stream_blocked_frame_0);
  sm.handle_frame(stream_blocked_frame_1);
  CHECK(sm.stream_count() == 2);
  CHECK(sm.total_offset_received() == 0);

  // Stream 0 shoud be out of flow control
  std::shared_ptr<QUICFrame> stream_frame_0 = QUICFrameFactory::create_stream_frame(data, 1024, 0, 0);
  sm.handle_frame(stream_frame_0);
  CHECK(sm.total_offset_received() == 0);

  // total_offset should be a integer in unit of 1024 octets
  std::shared_ptr<QUICFrame> stream_frame_1 = QUICFrameFactory::create_stream_frame(data, 1024, 1, 0);
  sm.handle_frame(stream_frame_1);
  CHECK(sm.total_offset_received() == 1);
}

TEST_CASE("QUICStreamManager_total_offset_sent", "[quic]")
{
  MockQUICFrameTransmitter tx;
  QUICApplicationMap app_map;
  MockQUICApplication mock_app;
  app_map.set_default(&mock_app);
  QUICStreamManager sm(0, &tx, &app_map);
  std::shared_ptr<QUICTransportParameters> local_tp = std::make_shared<QUICTransportParametersInEncryptedExtensions>();
  local_tp->add(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA,
                std::unique_ptr<QUICTransportParameterValue>(new QUICTransportParameterValue(4096, 4)));
  std::shared_ptr<QUICTransportParameters> remote_tp =
    std::make_shared<QUICTransportParametersInClientHello>(static_cast<QUICVersion>(0), static_cast<QUICVersion>(0));
  sm.init_flow_control_params(local_tp, remote_tp);
  uint8_t data[1024] = {0};

  // Create a stream with STREAM_BLOCKED (== noop)
  std::shared_ptr<QUICFrame> stream_frame_0_r =
    QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("abc"), 3, 0, 0);
  std::shared_ptr<QUICFrame> stream_frame_1_r =
    QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("abc"), 3, 1, 0);
  sm.handle_frame(stream_frame_0_r);
  sm.handle_frame(stream_frame_1_r);
  CHECK(sm.stream_count() == 2);
  CHECK(sm.total_offset_sent() == 0);

  // Stream 0 shoud be out of flow control
  QUICFrameUPtr stream_frame_0 = QUICFrameFactory::create_stream_frame(data, 1024, 0, 0);
  mock_app.send(data, 1024, 0);
  sleep(2);
  CHECK(sm.total_offset_sent() == 0);

  // total_offset should be a integer in unit of octets
  QUICFrameUPtr stream_frame_1 = QUICFrameFactory::create_stream_frame(data, 1024, 1, 0);
  mock_app.send(data, 1024, 1);
  sm.add_total_offset_sent(1024);
  sleep(2);
  CHECK(sm.total_offset_sent() == 1024);
}
