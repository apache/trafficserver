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

#include "quic/QUICFrame.h"
#include "quic/QUICStreamState.h"
#include "quic/Mock.h"

TEST_CASE("QUICSendStreamState", "[quic]")
{
  auto stream_frame          = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("foo"), 4, 1, 0);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("bar"), 4, 1, 0, true);
  auto rst_stream_frame      = QUICFrameFactory::create_rst_stream_frame(0, static_cast<QUICAppErrorCode>(0x01), 0);
  auto stream_blocked_frame  = QUICFrameFactory::create_stream_blocked_frame(0, 0);

  SECTION("_Init")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamState ss1(nullptr, nullptr);
    CHECK(ss1.get() == QUICStreamState::State::Ready);

    // Case2. Send STREAM
    QUICSendStreamState ss2(nullptr, nullptr);
    ss2.update_with_sending_frame(*stream_frame);
    CHECK(ss2.get() == QUICStreamState::State::Send);

    // Case3. Send RST_STREAM
    QUICSendStreamState ss3(nullptr, nullptr);
    ss3.update_with_sending_frame(*rst_stream_frame);
    CHECK(ss3.get() == QUICStreamState::State::ResetSent);

    // Case4. Send FIN in a STREAM
    QUICSendStreamState ss4(nullptr, nullptr);
    ss4.update_with_sending_frame(*stream_frame_with_fin);
    CHECK(ss4.get() == QUICStreamState::State::DataSent);
  }
}

TEST_CASE("QUICReceiveStreamState", "[quic]")
{
  auto stream_frame          = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("foo"), 4, 1, 0);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("bar"), 4, 1, 0, true);
  auto rst_stream_frame      = QUICFrameFactory::create_rst_stream_frame(0, static_cast<QUICAppErrorCode>(0x01), 0);
  auto max_stream_data_frame = QUICFrameFactory::create_max_stream_data_frame(0, 0);
  auto stream_blocked_frame  = QUICFrameFactory::create_stream_blocked_frame(0, 0);

  SECTION("_Init")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamState ss1(&in_progress, nullptr);
    ss1.update_with_receiving_frame(*stream_frame);
    CHECK(ss1.get() == QUICStreamState::State::Recv);

    // Case2. Recv STREAM_BLOCKED
    QUICReceiveStreamState ss2(&in_progress, nullptr);
    ss2.update_with_receiving_frame(*stream_blocked_frame);
    CHECK(ss2.get() == QUICStreamState::State::Recv);

    // Case3. Recv RST_STREAM
    QUICReceiveStreamState ss3(&in_progress, nullptr);
    ss3.update_with_receiving_frame(*rst_stream_frame);
    CHECK(ss3.get() == QUICStreamState::State::ResetRecvd);

    // Case4. Recv MAX_STREAM_DATA
    QUICReceiveStreamState ss4(&in_progress, nullptr);
    ss4.update_with_receiving_frame(*max_stream_data_frame);
    CHECK(ss4.get() == QUICStreamState::State::Recv);

    // Case5. Recv FIN in a STREAM
    QUICReceiveStreamState ss5(&in_progress, nullptr);
    ss5.update_with_receiving_frame(*stream_frame_with_fin);
    CHECK(ss5.get() == QUICStreamState::State::SizeKnown);
  }
}
