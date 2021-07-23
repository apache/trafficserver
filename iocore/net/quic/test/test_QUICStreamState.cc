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
  block_4->alloc(BUFFER_SIZE_INDEX_32K);
  block_4->fill(4);
  CHECK(block_4->read_avail() == 4);

  uint8_t stream_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t stream_frame_with_fin_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t rst_stream_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t stream_data_blocked_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];

  auto stream_frame          = QUICFrameFactory::create_stream_frame(stream_frame_buf, block_4, 1, 0);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(stream_frame_with_fin_buf, block_4, 1, 0, true);
  auto rst_stream_frame =
    QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 0, static_cast<QUICAppErrorCode>(0x01), 0);
  auto stream_data_blocked_frame = QUICFrameFactory::create_stream_data_blocked_frame(stream_data_blocked_frame_buf, 0, 0);
  MockQUICTransferProgressProvider pp;

  SECTION("Ready -> Send -> Data Sent -> Data Recvd")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamStateMachine ss(nullptr, &pp);
    CHECK(ss.get() == QUICSendStreamState::Ready);

    // Case2. Send STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    CHECK(ss.update_with_sending_frame(*stream_frame));
    CHECK(ss.get() == QUICSendStreamState::Send);

    // Case3. Send STREAM_DATA_BLOCKED
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM_DATA_BLOCKED));
    CHECK(!ss.update_with_sending_frame(*stream_data_blocked_frame));
    CHECK(ss.get() == QUICSendStreamState::Send);

    // Case3. Send FIN in a STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    CHECK(ss.update_with_sending_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICSendStreamState::DataSent);

    // Case4. STREAM is not allowed to send
    CHECK(!ss.is_allowed_to_send(QUICFrameType::STREAM));

    // Case5. Receive all ACKs
    pp.set_transfer_complete(true);
    CHECK(ss.update_on_ack());
    CHECK(ss.get() == QUICSendStreamState::DataRecvd);
  }

  SECTION("Ready -> Send")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamStateMachine ss(nullptr, &pp);
    CHECK(ss.get() == QUICSendStreamState::Ready);

    // Case2. Send STREAM_DATA_BLOCKED
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM_DATA_BLOCKED));
    CHECK(ss.update_with_sending_frame(*stream_data_blocked_frame));
    CHECK(ss.get() == QUICSendStreamState::Send);
  }

  SECTION("Ready -> Reset Sent -> Reset Recvd")
  {
    MockQUICTransferProgressProvider pp;

    // Case1. Create Stream (Sending)
    QUICSendStreamStateMachine ss(nullptr, &pp);
    CHECK(ss.get() == QUICSendStreamState::Ready);

    // Case2. Send RESET_STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::RESET_STREAM));
    CHECK(ss.update_with_sending_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICSendStreamState::ResetSent);

    // Case3. Receive ACK for STREAM
    CHECK(ss.get() == QUICSendStreamState::ResetSent);

    // Case4. Receive ACK for RESET_STREAM
    pp.set_cancelled(true);
    CHECK(ss.update_on_ack());
    CHECK(ss.get() == QUICSendStreamState::ResetRecvd);
  }

  SECTION("Ready -> Send -> Reset Sent -> Reset Recvd")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamStateMachine ss(nullptr, &pp);
    CHECK(ss.get() == QUICSendStreamState::Ready);

    // Case2. Send STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    CHECK(ss.update_with_sending_frame(*stream_frame));
    CHECK(ss.get() == QUICSendStreamState::Send);

    // Case3. Send RESET_STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::RESET_STREAM));
    CHECK(ss.update_with_sending_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICSendStreamState::ResetSent);

    // Case4. Receive ACK for STREAM
    CHECK(!ss.update_on_ack());
    CHECK(ss.get() == QUICSendStreamState::ResetSent);

    // Case5. Receive ACK for RESET_STREAM
    pp.set_cancelled(true);
    CHECK(ss.update_on_ack());
    CHECK(ss.get() == QUICSendStreamState::ResetRecvd);
  }

  SECTION("Ready -> Send -> Data Sent -> Reset Sent -> Reset Recvd")
  {
    // Case1. Create Stream (Sending)
    QUICSendStreamStateMachine ss(nullptr, &pp);
    CHECK(ss.get() == QUICSendStreamState::Ready);

    // Case2. Send STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    CHECK(ss.update_with_sending_frame(*stream_frame));
    CHECK(ss.get() == QUICSendStreamState::Send);

    // Case3. Send STREAM_DATA_BLOCKED
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM_DATA_BLOCKED));
    CHECK(!ss.update_with_sending_frame(*stream_data_blocked_frame));
    CHECK(ss.get() == QUICSendStreamState::Send);

    // Case3. Send FIN in a STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    CHECK(ss.update_with_sending_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICSendStreamState::DataSent);

    // Case4. STREAM is not allowed to send
    CHECK(!ss.is_allowed_to_send(QUICFrameType::STREAM));

    // Case4. Send RESET_STREAM
    CHECK(ss.is_allowed_to_send(QUICFrameType::RESET_STREAM));
    CHECK(ss.update_with_sending_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICSendStreamState::ResetSent);

    // Case5. Receive ACK for STREAM
    CHECK(!ss.update_on_ack());
    CHECK(ss.get() == QUICSendStreamState::ResetSent);

    // Case6. Receive ACK for RESET_STREAM
    pp.set_cancelled(true);
    CHECK(ss.update_on_ack());
    CHECK(ss.get() == QUICSendStreamState::ResetRecvd);
  }
}

// Unidirectional (receiving)
TEST_CASE("QUICReceiveStreamState", "[quic]")
{
  Ptr<IOBufferBlock> block_4 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_4->alloc(BUFFER_SIZE_INDEX_32K);
  block_4->fill(4);
  CHECK(block_4->read_avail() == 4);

  uint8_t stream_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t stream_frame_delayed_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t stream_frame_with_fin_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t rst_stream_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t stream_data_blocked_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];

  auto stream_frame          = QUICFrameFactory::create_stream_frame(stream_frame_buf, block_4, 1, 0);
  auto stream_frame_delayed  = QUICFrameFactory::create_stream_frame(stream_frame_delayed_buf, block_4, 1, 1);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(stream_frame_with_fin_buf, block_4, 1, 2, true);
  auto rst_stream_frame =
    QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 0, static_cast<QUICAppErrorCode>(0x01), 0);
  auto stream_data_blocked_frame = QUICFrameFactory::create_stream_data_blocked_frame(stream_data_blocked_frame_buf, 0, 0);

  SECTION("Recv -> Size Known -> Data Recvd -> Data Read")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamStateMachine ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_send(QUICFrameType::MAX_STREAM_DATA) == false);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(1);
    CHECK(ss.update_with_receiving_frame(*stream_frame));
    CHECK(ss.get() == QUICReceiveStreamState::Recv);

    // Case2. Recv STREAM_DATA_BLOCKED
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM_DATA_BLOCKED));
    CHECK(!ss.update_with_receiving_frame(*stream_data_blocked_frame));
    CHECK(ss.get() == QUICReceiveStreamState::Recv);

    // Case3. Recv FIN in a STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_goal(3);
    CHECK(ss.update_with_receiving_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICReceiveStreamState::SizeKnown);

    // Case4. Recv ALL data
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(3);
    CHECK(ss.update_with_receiving_frame(*stream_frame_delayed));
    CHECK(ss.get() == QUICReceiveStreamState::DataRecvd);

    // Case5. Read data
    in_progress.set_transfer_complete(true);
    CHECK(ss.update_on_read());
    CHECK(ss.get() == QUICReceiveStreamState::DataRead);
  }

  SECTION("Recv -> Reset Recvd -> Reset Read")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamStateMachine ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(ss.update_with_receiving_frame(*stream_frame));
    CHECK(ss.get() == QUICReceiveStreamState::Recv);

    // Case2. Recv RESET_STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::RESET_STREAM));
    CHECK(ss.update_with_receiving_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICReceiveStreamState::ResetRecvd);

    // Case3. Handle reset
    CHECK(ss.update_on_eos());
    CHECK(ss.get() == QUICReceiveStreamState::ResetRead);
  }

  SECTION("Recv -> Size Known -> Reset Recvd")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamStateMachine ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(ss.update_with_receiving_frame(*stream_frame));
    CHECK(ss.get() == QUICReceiveStreamState::Recv);

    // Case2. Recv FIN in a STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(ss.update_with_receiving_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICReceiveStreamState::SizeKnown);

    // Case3. Recv RESET_STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::RESET_STREAM));
    CHECK(ss.update_with_receiving_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICReceiveStreamState::ResetRecvd);
  }

  SECTION("Recv -> Size Known -> Data Recvd !-> Reset Recvd")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamStateMachine ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(1);
    CHECK(ss.update_with_receiving_frame(*stream_frame));
    CHECK(ss.get() == QUICReceiveStreamState::Recv);

    // Case2. Recv FIN in a STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_goal(3);
    CHECK(ss.update_with_receiving_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICReceiveStreamState::SizeKnown);

    // Case3. Recv ALL data
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(3);
    CHECK(ss.update_with_receiving_frame(*stream_frame_delayed));
    CHECK(ss.get() == QUICReceiveStreamState::DataRecvd);

    // Case4. Recv RESET_STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::RESET_STREAM));
    CHECK(!ss.update_with_receiving_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICReceiveStreamState::DataRecvd);
  }

  SECTION("Recv -> Size Known -> Reset Recvd !-> Data Recvd")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamStateMachine ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(1);
    CHECK(ss.update_with_receiving_frame(*stream_frame));
    CHECK(ss.get() == QUICReceiveStreamState::Recv);

    // Case2. Recv FIN in a STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_goal(3);
    CHECK(ss.update_with_receiving_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICReceiveStreamState::SizeKnown);

    // Case3. Recv RESET_STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::RESET_STREAM));
    CHECK(ss.update_with_receiving_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICReceiveStreamState::ResetRecvd);

    // Case4. Recv ALL data
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_progress(3);
    CHECK(!ss.update_with_receiving_frame(*stream_frame_delayed));
    CHECK(ss.get() == QUICReceiveStreamState::ResetRecvd);
    CHECK(ss.is_allowed_to_send(QUICFrameType::STOP_SENDING) == false);
  }

  SECTION("Do not discard STREAM and RESET_STREAM in DataRecvd")
  {
    MockQUICTransferProgressProvider in_progress;

    // Case1. Recv STREAM
    QUICReceiveStreamStateMachine ss(&in_progress, nullptr);
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(ss.update_with_receiving_frame(*stream_frame));
    CHECK(ss.get() == QUICReceiveStreamState::Recv);

    // Case2. Recv FIN in a STREAM
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(ss.update_with_receiving_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICReceiveStreamState::SizeKnown);

    // // Case3. Recv ALL data
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    in_progress.set_transfer_complete(true);
    CHECK(ss.update_with_receiving_frame(*stream_frame_delayed));
    // ss.update_on_transport_recv_event();
    CHECK(ss.get() == QUICReceiveStreamState::DataRecvd);

    CHECK(ss.is_allowed_to_receive(QUICFrameType::RESET_STREAM));
    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(ss.is_allowed_to_send(QUICFrameType::STOP_SENDING));
  }
}

TEST_CASE("QUICBidiState", "[quic]")
{
  Ptr<IOBufferBlock> block_4 = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block_4->alloc(BUFFER_SIZE_INDEX_32K);
  block_4->fill(4);
  CHECK(block_4->read_avail() == 4);

  uint8_t stream_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t stream_frame_delayed_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t stream_frame_with_fin_buf[QUICFrame::MAX_INSTANCE_SIZE];
  uint8_t rst_stream_frame_buf[QUICFrame::MAX_INSTANCE_SIZE];

  auto stream_frame          = QUICFrameFactory::create_stream_frame(stream_frame_buf, block_4, 1, 0);
  auto stream_frame_delayed  = QUICFrameFactory::create_stream_frame(stream_frame_delayed_buf, block_4, 1, 1);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(stream_frame_with_fin_buf, block_4, 1, 2, true);
  auto rst_stream_frame =
    QUICFrameFactory::create_rst_stream_frame(rst_stream_frame_buf, 0, static_cast<QUICAppErrorCode>(0x01), 0);

  SECTION("QUICBidiState idle -> open -> HC_R 1")
  {
    MockQUICTransferProgressProvider in_progress;
    MockQUICTransferProgressProvider out_progress;

    QUICBidirectionalStreamStateMachine ss(nullptr, &out_progress, &in_progress, nullptr);
    CHECK(ss.get() == QUICBidirectionalStreamState::Idle);

    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(ss.update_with_receiving_frame(*stream_frame));

    CHECK(ss.get() == QUICBidirectionalStreamState::Open);
    in_progress.set_transfer_complete(true);
    CHECK(ss.update_with_receiving_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICBidirectionalStreamState::HC_R);
  }

  SECTION("QUICBidiState idle -> open -> HC_R 2")
  {
    MockQUICTransferProgressProvider in_progress;
    MockQUICTransferProgressProvider out_progress;

    QUICBidirectionalStreamStateMachine ss(nullptr, &out_progress, &in_progress, nullptr);
    CHECK(ss.get() == QUICBidirectionalStreamState::Idle);

    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(ss.update_with_receiving_frame(*stream_frame));

    CHECK(ss.get() == QUICBidirectionalStreamState::Open);
    CHECK(ss.update_with_receiving_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICBidirectionalStreamState::HC_R);
  }

  SECTION("QUICBidiState idle -> open -> HC_L 1")
  {
    MockQUICTransferProgressProvider in_progress;
    MockQUICTransferProgressProvider out_progress;

    QUICBidirectionalStreamStateMachine ss(nullptr, &out_progress, &in_progress, nullptr);
    CHECK(ss.get() == QUICBidirectionalStreamState::Idle);

    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    CHECK(ss.update_with_sending_frame(*stream_frame));
    CHECK(ss.get() == QUICBidirectionalStreamState::Open);

    CHECK(ss.update_with_sending_frame(*stream_frame_with_fin)); // internal state is changed
    CHECK(ss.get() == QUICBidirectionalStreamState::Open);

    out_progress.set_transfer_complete(true);
    CHECK(ss.update_on_ack());
    CHECK(ss.get() == QUICBidirectionalStreamState::HC_L);

    CHECK(!ss.update_with_sending_frame(*stream_frame_delayed));
    CHECK(ss.get() == QUICBidirectionalStreamState::HC_L);
  }

  SECTION("QUICBidiState idle -> open -> HC_L 2")
  {
    MockQUICTransferProgressProvider in_progress;
    MockQUICTransferProgressProvider out_progress;

    QUICBidirectionalStreamStateMachine ss(nullptr, &out_progress, &in_progress, nullptr);
    CHECK(ss.get() == QUICBidirectionalStreamState::Idle);

    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    CHECK(ss.update_with_sending_frame(*stream_frame));
    CHECK(ss.get() == QUICBidirectionalStreamState::Open);

    CHECK(ss.update_with_sending_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICBidirectionalStreamState::HC_L);
  }

  SECTION("QUICBidiState idle -> open -> closed 1")
  {
    MockQUICTransferProgressProvider in_progress;
    MockQUICTransferProgressProvider out_progress;

    QUICBidirectionalStreamStateMachine ss(nullptr, &out_progress, &in_progress, nullptr);
    CHECK(ss.get() == QUICBidirectionalStreamState::Idle);

    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    CHECK(ss.update_with_sending_frame(*stream_frame));
    CHECK(ss.get() == QUICBidirectionalStreamState::Open);

    CHECK(ss.update_with_sending_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICBidirectionalStreamState::HC_L);

    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(!ss.update_with_receiving_frame(*stream_frame));

    CHECK(ss.update_with_receiving_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICBidirectionalStreamState::Closed);

    CHECK(ss.update_on_eos()); // internal state is changed
    CHECK(ss.get() == QUICBidirectionalStreamState::Closed);
  }

  SECTION("QUICBidiState idle -> open -> closed 2")
  {
    MockQUICTransferProgressProvider in_progress;
    MockQUICTransferProgressProvider out_progress;

    QUICBidirectionalStreamStateMachine ss(nullptr, &out_progress, &in_progress, nullptr);
    CHECK(ss.get() == QUICBidirectionalStreamState::Idle);

    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    CHECK(ss.update_with_sending_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICBidirectionalStreamState::Open);
    out_progress.set_transfer_complete(true);
    CHECK(ss.update_on_ack());
    CHECK(ss.get() == QUICBidirectionalStreamState::HC_L);

    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(!ss.update_with_receiving_frame(*stream_frame));

    CHECK(ss.update_with_receiving_frame(*rst_stream_frame));
    CHECK(ss.get() == QUICBidirectionalStreamState::Closed);

    in_progress.set_transfer_complete(true);
    CHECK(ss.update_on_eos()); // internal state is changed
    CHECK(ss.get() == QUICBidirectionalStreamState::Closed);
  }

  SECTION("QUICBidiState idle -> open -> closed 3")
  {
    MockQUICTransferProgressProvider in_progress;
    MockQUICTransferProgressProvider out_progress;

    QUICBidirectionalStreamStateMachine ss(nullptr, &out_progress, &in_progress, nullptr);
    CHECK(ss.get() == QUICBidirectionalStreamState::Idle);

    CHECK(ss.is_allowed_to_send(QUICFrameType::STREAM));
    CHECK(ss.update_with_sending_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICBidirectionalStreamState::Open);
    out_progress.set_transfer_complete(true);
    CHECK(ss.update_on_ack());
    CHECK(ss.get() == QUICBidirectionalStreamState::HC_L);

    CHECK(ss.is_allowed_to_receive(QUICFrameType::STREAM));
    CHECK(!ss.update_with_receiving_frame(*stream_frame_delayed));

    CHECK(ss.update_with_receiving_frame(*stream_frame_with_fin));
    CHECK(ss.get() == QUICBidirectionalStreamState::HC_L);

    in_progress.set_transfer_complete(true);
    CHECK(ss.update_on_read());
    CHECK(ss.get() == QUICBidirectionalStreamState::Closed);
  }
}
