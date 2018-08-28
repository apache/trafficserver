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

// Unidirectional (sending)
TEST_CASE("QUICSendStreamState", "[quic]")
{
  auto stream_frame          = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("foo"), 4, 1, 0);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("bar"), 4, 1, 0, true);
  auto rst_stream_frame      = QUICFrameFactory::create_rst_stream_frame(0, static_cast<QUICAppErrorCode>(0x01), 0);
  auto stream_blocked_frame  = QUICFrameFactory::create_stream_blocked_frame(0, 0);

  SECTION("Ready -> Send -> Data Sent")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamState ss(nullptr, nullptr);
    CHECK(ss.get() == QUICStreamState::State::Ready);

    // Case2. Send STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    ss.update_with_sending_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Send);

    // Case3. Send STREAM_BLOCKED
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM_BLOCKED));
    ss.update_with_sending_frame(*stream_blocked_frame);
    CHECK(ss.get() == QUICStreamState::State::Send);

    // Case3. Send FIN in a STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    ss.update_with_sending_frame(*stream_frame_with_fin);
    CHECK(ss.get() == QUICStreamState::State::DataSent);

    // Case4. STREAM is not allowed to send
    CHECK(!ss.is_allowed_to_send(QUICFrameType::STREAM));
  }

  SECTION("Ready -> Reset Sent")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamState ss(nullptr, nullptr);
    CHECK(ss.get() == QUICStreamState::State::Ready);

    // Case2. Send RST_STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::RST_STREAM));
    ss.update_with_sending_frame(*rst_stream_frame);
    CHECK(ss.get() == QUICStreamState::State::ResetSent);
  }
}

// Unidirectional (receiving)
TEST_CASE("QUICReceiveStreamState", "[quic]")
{
  auto stream_frame          = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("foo"), 4, 1, 0);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("bar"), 4, 1, 0, true);
  auto rst_stream_frame      = QUICFrameFactory::create_rst_stream_frame(0, static_cast<QUICAppErrorCode>(0x01), 0);
  auto stream_blocked_frame  = QUICFrameFactory::create_stream_blocked_frame(0, 0);

  SECTION("Recv -> Size Known -> Data Recvd")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamState ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    ss.update_with_receiving_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Recv);

    // Case2. Recv STREAM_BLOCKED
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM_BLOCKED));
    ss.update_with_receiving_frame(*stream_blocked_frame);
    CHECK(ss.get() == QUICStreamState::State::Recv);

    // Case3. Recv FIN in a STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    ss.update_with_receiving_frame(*stream_frame_with_fin);
    CHECK(ss.get() == QUICStreamState::State::SizeKnown);
  }

  SECTION("Recv -> Reset Recvd")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamState ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    ss.update_with_receiving_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Recv);

    // Case2. Recv RST_STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::RST_STREAM));
    ss.update_with_receiving_frame(*rst_stream_frame);
    CHECK(ss.get() == QUICStreamState::State::ResetRecvd);
  }
}
