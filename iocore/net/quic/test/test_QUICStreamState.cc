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

TEST_CASE("QUICStreamState_Idle", "[quic]")
{
  auto stream_frame          = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("foo"), 4, 1, 0);
  auto rst_stream_frame      = QUICFrameFactory::create_rst_stream_frame(0, QUICErrorCode::NO_ERROR, 0);
  auto max_stream_data_frame = QUICFrameFactory::create_max_stream_data_frame(0, 0);
  auto stream_blocked_frame  = QUICFrameFactory::create_stream_blocked_frame(0);

  // Case1. Send STREAM
  QUICStreamState ss1;
  ss1.update_with_sent_frame(*stream_frame);
  CHECK(ss1.get() == QUICStreamState::State::open);

  // Case2. Send RST_STREAM
  QUICStreamState ss2;
  ss2.update_with_sent_frame(*rst_stream_frame);
  CHECK(ss2.get() == QUICStreamState::State::half_closed_local);

  // Case3. Recv STREAM
  QUICStreamState ss3;
  ss3.update_with_received_frame(*stream_frame);
  CHECK(ss3.get() == QUICStreamState::State::open);

  // Case4. Recv RST_STREAM
  QUICStreamState ss4;
  ss4.update_with_received_frame(*rst_stream_frame);
  CHECK(ss4.get() == QUICStreamState::State::half_closed_remote);

  // Case5. Recv MAX_STREAM_DATA
  QUICStreamState ss5;
  ss5.update_with_received_frame(*max_stream_data_frame);
  CHECK(ss5.get() == QUICStreamState::State::open);

  // Case6. Recv STREAM_BLOCKED
  QUICStreamState ss6;
  ss6.update_with_received_frame(*stream_blocked_frame);
  CHECK(ss6.get() == QUICStreamState::State::open);
}

TEST_CASE("QUICStreamState_Open", "[quic]")
{
  auto stream_frame          = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("foo"), 4, 1, 0);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("bar"), 4, 1, 0, true);
  auto rst_stream_frame      = QUICFrameFactory::create_rst_stream_frame(0, QUICErrorCode::NO_ERROR, 0);

  // Case1. Send FIN in a STREAM
  QUICStreamState ss1;
  ss1.update_with_sent_frame(*stream_frame); // OPEN
  CHECK(ss1.get() == QUICStreamState::State::open);
  ss1.update_with_sent_frame(*stream_frame_with_fin);
  CHECK(ss1.get() == QUICStreamState::State::half_closed_local);

  // Case2. Send RST_STREAM
  QUICStreamState ss2;
  ss2.update_with_sent_frame(*stream_frame); // OPEN
  CHECK(ss2.get() == QUICStreamState::State::open);
  ss2.update_with_sent_frame(*rst_stream_frame);
  CHECK(ss2.get() == QUICStreamState::State::half_closed_local);

  // Case3. Recv FIN in a STREAM
  QUICStreamState ss3;
  ss3.update_with_received_frame(*stream_frame); // OPEN
  CHECK(ss3.get() == QUICStreamState::State::open);
  ss3.update_with_received_frame(*stream_frame_with_fin);
  CHECK(ss3.get() == QUICStreamState::State::half_closed_remote);

  // Case4. Recv RST_STREAM
  QUICStreamState ss4;
  ss4.update_with_received_frame(*stream_frame); // OPEN
  CHECK(ss4.get() == QUICStreamState::State::open);
  ss4.update_with_received_frame(*rst_stream_frame);
  CHECK(ss4.get() == QUICStreamState::State::half_closed_remote);
}

TEST_CASE("QUICStreamState_Half_Closed_Remote", "[quic]")
{
  auto stream_frame          = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("foo"), 4, 1, 0);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("bar"), 4, 1, 0, true);
  auto rst_stream_frame      = QUICFrameFactory::create_rst_stream_frame(0, QUICErrorCode::NO_ERROR, 0);

  // Case1. Send FIN in a STREAM
  QUICStreamState ss1;
  ss1.update_with_received_frame(*stream_frame_with_fin); // HALF CLOSED REMOTE
  CHECK(ss1.get() == QUICStreamState::State::half_closed_remote);
  ss1.update_with_sent_frame(*stream_frame_with_fin);
  CHECK(ss1.get() == QUICStreamState::State::closed);

  // Case2. Send RST
  QUICStreamState ss2;
  ss2.update_with_received_frame(*stream_frame_with_fin); // HALF CLOSED REMOTE
  CHECK(ss2.get() == QUICStreamState::State::half_closed_remote);
  ss2.update_with_sent_frame(*rst_stream_frame);
  CHECK(ss2.get() == QUICStreamState::State::closed);
}

TEST_CASE("QUICStreamState_Half_Closed_Local", "[quic]")
{
  auto stream_frame          = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("foo"), 4, 1, 0);
  auto stream_frame_with_fin = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>("bar"), 4, 1, 0, true);
  auto rst_stream_frame      = QUICFrameFactory::create_rst_stream_frame(0, QUICErrorCode::NO_ERROR, 0);

  // Case1. Recv FIN in a STREAM
  QUICStreamState ss1;
  ss1.update_with_sent_frame(*stream_frame_with_fin); // HALF CLOSED LOCAL
  CHECK(ss1.get() == QUICStreamState::State::half_closed_local);
  ss1.update_with_received_frame(*stream_frame_with_fin);
  CHECK(ss1.get() == QUICStreamState::State::closed);

  // Case2. Recv RST
  QUICStreamState ss2;
  ss2.update_with_sent_frame(*stream_frame_with_fin); // HALF CLOSED LOCAL
  CHECK(ss2.get() == QUICStreamState::State::half_closed_local);
  ss2.update_with_received_frame(*rst_stream_frame);
  CHECK(ss2.get() == QUICStreamState::State::closed);
}
