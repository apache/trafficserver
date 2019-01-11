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
  Ptr<IOBufferBlock> block_4 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_4->alloc();
  block_4->fill(4);
  CHECK(block_4->read_avail() == 4);

  auto stream_frame          = QUICFrameFactory::create_stream_frame(block_4, 1, 0);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(block_4, 1, 0, true);
  auto rst_stream_frame      = QUICFrameFactory::create_rst_stream_frame(0, static_cast<QUICAppErrorCode>(0x01), 0);
  auto stream_blocked_frame  = QUICFrameFactory::create_stream_blocked_frame(0, 0);
  MockQUICTransferProgressProvider pp;

  SECTION("Ready -> Send -> Data Sent -> Data Recvd")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamState ss(nullptr, &pp);
    CHECK(ss.get() == QUICStreamState::State::Ready);

    // Case2. Send STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    ss.update_with_sending_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Send);

    // Case3. Send STREAM_DATA_BLOCKED
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM_DATA_BLOCKED));
    ss.update_with_sending_frame(*stream_blocked_frame);
    CHECK(ss.get() == QUICStreamState::State::Send);

    // Case3. Send FIN in a STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    ss.update_with_sending_frame(*stream_frame_with_fin);
    CHECK(ss.get() == QUICStreamState::State::DataSent);

    // Case4. STREAM is not allowed to send
    CHECK(!ss.is_allowed_to_send(QUICFrameType::STREAM));

    // Case5. Receive all ACKs
    pp.set_transfer_complete(true);
    ss.update_on_ack();
    CHECK(ss.get() == QUICStreamState::State::DataRecvd);
  }

  SECTION("Ready -> Send")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamState ss(nullptr, &pp);
    CHECK(ss.get() == QUICStreamState::State::Ready);

    // Case2. Send STREAM_DATA_BLOCKED
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM_DATA_BLOCKED));
    ss.update_with_sending_frame(*stream_blocked_frame);
    CHECK(ss.get() == QUICStreamState::State::Send);
  }

  SECTION("Ready -> Reset Sent -> Reset Recvd")
  {
    MockQUICTransferProgressProvider pp;

    // Case1. Create Stream (Sending)
    QUICSendStreamState ss(nullptr, &pp);
    CHECK(ss.get() == QUICStreamState::State::Ready);

    // Case2. Send RESET_STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::RESET_STREAM));
    ss.update_with_sending_frame(*rst_stream_frame);
    CHECK(ss.get() == QUICStreamState::State::ResetSent);

    // Case3. Receive ACK for STREAM
    ss.update_on_ack();
    CHECK(ss.get() == QUICStreamState::State::ResetSent);

    // Case4. Receive ACK for RESET_STREAM
    pp.set_cancelled(true);
    ss.update_on_ack();
    CHECK(ss.get() == QUICStreamState::State::ResetRecvd);
  }

  SECTION("Ready -> Send -> Reset Sent -> Reset Recvd")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamState ss(nullptr, &pp);
    CHECK(ss.get() == QUICStreamState::State::Ready);

    // Case2. Send STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    ss.update_with_sending_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Send);

    // Case3. Send RESET_STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::RESET_STREAM));
    ss.update_with_sending_frame(*rst_stream_frame);
    CHECK(ss.get() == QUICStreamState::State::ResetSent);

    // Case4. Receive ACK for STREAM
    ss.update_on_ack();
    CHECK(ss.get() == QUICStreamState::State::ResetSent);

    // Case5. Receive ACK for RESET_STREAM
    pp.set_cancelled(true);
    ss.update_on_ack();
    CHECK(ss.get() == QUICStreamState::State::ResetRecvd);
  }

  SECTION("Ready -> Send -> Data Sent -> Reset Sent -> Reset Recvd")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamState ss(nullptr, &pp);
    CHECK(ss.get() == QUICStreamState::State::Ready);

    // Case2. Send STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    ss.update_with_sending_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Send);

    // Case3. Send STREAM_DATA_BLOCKED
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM_DATA_BLOCKED));
    ss.update_with_sending_frame(*stream_blocked_frame);
    CHECK(ss.get() == QUICStreamState::State::Send);

    // Case3. Send FIN in a STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    ss.update_with_sending_frame(*stream_frame_with_fin);
    CHECK(ss.get() == QUICStreamState::State::DataSent);

    // Case4. STREAM is not allowed to send
    CHECK(!ss.is_allowed_to_send(QUICFrameType::STREAM));

    // Case4. Send RESET_STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::RESET_STREAM));
    ss.update_with_sending_frame(*rst_stream_frame);
    CHECK(ss.get() == QUICStreamState::State::ResetSent);

    // Case5. Receive ACK for STREAM
    ss.update_on_ack();
    CHECK(ss.get() == QUICStreamState::State::ResetSent);

    // Case6. Receive ACK for RESET_STREAM
    pp.set_cancelled(true);
    ss.update_on_ack();
    CHECK(ss.get() == QUICStreamState::State::ResetRecvd);
  }
}

// Unidirectional (receiving)
TEST_CASE("QUICReceiveStreamState", "[quic]")
{
  Ptr<IOBufferBlock> block_4 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_4->alloc();
  block_4->fill(4);
  CHECK(block_4->read_avail() == 4);

  auto stream_frame          = QUICFrameFactory::create_stream_frame(block_4, 1, 0);
  auto stream_frame_delayed  = QUICFrameFactory::create_stream_frame(block_4, 1, 1);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(block_4, 1, 2, true);
  auto rst_stream_frame      = QUICFrameFactory::create_rst_stream_frame(0, static_cast<QUICAppErrorCode>(0x01), 0);
  auto stream_blocked_frame  = QUICFrameFactory::create_stream_blocked_frame(0, 0);

  SECTION("Recv -> Size Known -> Data Recvd -> Data Read")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamState ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(1);
    ss.update_with_receiving_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Recv);

    // Case2. Recv STREAM_DATA_BLOCKED
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM_DATA_BLOCKED));
    ss.update_with_receiving_frame(*stream_blocked_frame);
    CHECK(ss.get() == QUICStreamState::State::Recv);

    // Case3. Recv FIN in a STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_goal(3);
    ss.update_with_receiving_frame(*stream_frame_with_fin);
    CHECK(ss.get() == QUICStreamState::State::SizeKnown);

    // Case4. Recv ALL data
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(3);
    ss.update_with_receiving_frame(*stream_frame_delayed);
    CHECK(ss.get() == QUICStreamState::State::DataRecvd);

    // Case5. Read data
    in_progress.set_transfer_complete(true);
    ss.update_on_read();
    CHECK(ss.get() == QUICStreamState::State::DataRead);
  }

  SECTION("Recv -> Reset Recvd -> Reset Read")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamState ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    ss.update_with_receiving_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Recv);

    // Case2. Recv RESET_STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::RESET_STREAM));
    ss.update_with_receiving_frame(*rst_stream_frame);
    CHECK(ss.get() == QUICStreamState::State::ResetRecvd);

    // Case3. Handle reset
    ss.update_on_eos();
    CHECK(ss.get() == QUICStreamState::State::ResetRead);
  }

  SECTION("Recv -> Size Known -> Reset Recvd")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamState ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    ss.update_with_receiving_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Recv);

    // Case2. Recv FIN in a STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    ss.update_with_receiving_frame(*stream_frame_with_fin);
    CHECK(ss.get() == QUICStreamState::State::SizeKnown);

    // Case3. Recv RESET_STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::RESET_STREAM));
    ss.update_with_receiving_frame(*rst_stream_frame);
    CHECK(ss.get() == QUICStreamState::State::ResetRecvd);
  }

  SECTION("Recv -> Size Known -> Data Recvd -> Reset Recvd")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamState ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(1);
    ss.update_with_receiving_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Recv);

    // Case2. Recv FIN in a STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_goal(3);
    ss.update_with_receiving_frame(*stream_frame_with_fin);
    CHECK(ss.get() == QUICStreamState::State::SizeKnown);

    // Case3. Recv ALL data
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(3);
    ss.update_with_receiving_frame(*stream_frame_delayed);
    CHECK(ss.get() == QUICStreamState::State::DataRecvd);

    // Case4. Recv RESET_STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::RESET_STREAM));
    ss.update_with_receiving_frame(*rst_stream_frame);
    CHECK(ss.get() == QUICStreamState::State::ResetRecvd);
  }

  SECTION("Recv -> Size Known -> Reset Recvd -> Data Recvd")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamState ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(1);
    ss.update_with_receiving_frame(*stream_frame);
    CHECK(ss.get() == QUICStreamState::State::Recv);

    // Case2. Recv FIN in a STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_goal(3);
    ss.update_with_receiving_frame(*stream_frame_with_fin);
    CHECK(ss.get() == QUICStreamState::State::SizeKnown);

    // Case3. Recv RESET_STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::RESET_STREAM));
    ss.update_with_receiving_frame(*rst_stream_frame);
    CHECK(ss.get() == QUICStreamState::State::ResetRecvd);

    // Case4. Recv ALL data
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(3);
    ss.update_with_receiving_frame(*stream_frame_delayed);
    CHECK(ss.get() == QUICStreamState::State::DataRecvd);
  }
}
