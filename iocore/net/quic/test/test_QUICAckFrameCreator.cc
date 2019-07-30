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

#include "quic/QUICAckFrameCreator.h"

TEST_CASE("QUICAckFrameManager", "[quic]")
{
  QUICAckFrameManager ack_manager;
  QUICEncryptionLevel level = QUICEncryptionLevel::INITIAL;
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];

  // Initial state
  QUICFrame *ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  QUICAckFrame *frame  = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame == nullptr);

  // One packet
  ack_manager.update(level, 1, 1, false);
  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  frame     = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 0);
  CHECK(frame->largest_acknowledged() == 1);
  CHECK(frame->ack_block_section()->first_ack_block() == 0);

  // retry
  CHECK(ack_manager.will_generate_frame(level, 0) == false);

  // Not sequential
  ack_manager.update(level, 2, 1, false);
  ack_manager.update(level, 5, 1, false);
  ack_manager.update(level, 3, 1, false);
  ack_manager.update(level, 4, 1, false);
  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  frame     = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 0);
  CHECK(frame->largest_acknowledged() == 5);
  CHECK(frame->ack_block_section()->first_ack_block() == 4);

  // Loss
  ack_manager.update(level, 6, 1, false);
  ack_manager.update(level, 7, 1, false);
  ack_manager.update(level, 10, 1, false);
  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  frame     = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 1);
  CHECK(frame->largest_acknowledged() == 10);
  CHECK(frame->ack_block_section()->first_ack_block() == 0);
  CHECK(frame->ack_block_section()->begin()->gap() == 1);

  // on frame acked
  ack_manager.on_frame_acked(frame->id());

  CHECK(ack_manager.will_generate_frame(level, 0) == false);
  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  CHECK(ack_frame == nullptr);

  ack_manager.update(level, 11, 1, false);
  ack_manager.update(level, 12, 1, false);
  ack_manager.update(level, 13, 1, false);
  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  frame     = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 0);
  CHECK(frame->largest_acknowledged() == 13);
  CHECK(frame->ack_block_section()->first_ack_block() == 2);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);

  ack_manager.on_frame_acked(frame->id());

  // ack-only
  ack_manager.update(level, 14, 1, true);
  ack_manager.update(level, 15, 1, true);
  ack_manager.update(level, 16, 1, true);
  CHECK(ack_manager.will_generate_frame(level, 0) == false);
  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);

  ack_manager.update(level, 17, 1, false);
  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  frame     = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 0);
  CHECK(frame->largest_acknowledged() == 17);
  CHECK(frame->ack_block_section()->first_ack_block() == 3);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);
}

TEST_CASE("QUICAckFrameManager should send", "[quic]")
{
  SECTION("QUIC unorder packet", "[quic]")
  {
    QUICAckFrameManager ack_manager;

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    ack_manager.update(level, 2, 1, false);
    CHECK(ack_manager.will_generate_frame(level, 0) == true);
  }

  SECTION("QUIC delay ack and unorder packet", "[quic]")
  {
    QUICAckFrameManager ack_manager;

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    ack_manager.update(level, 0, 1, false);
    CHECK(ack_manager.will_generate_frame(level, 0) == false);

    ack_manager.update(level, 1, 1, false);
    CHECK(ack_manager.will_generate_frame(level, 0) == false);

    ack_manager.update(level, 3, 1, false);
    CHECK(ack_manager.will_generate_frame(level, 0) == true);
  }

  SECTION("QUIC delay too much time", "[quic]")
  {
    Thread::get_hrtime_updated();
    QUICAckFrameManager ack_manager;

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    ack_manager.update(level, 0, 1, false);
    CHECK(ack_manager.will_generate_frame(level, 0) == false);

    sleep(1);
    Thread::get_hrtime_updated();
    ack_manager.update(level, 1, 1, false);
    CHECK(ack_manager.will_generate_frame(level, 0) == true);
  }

  SECTION("QUIC intial packet", "[quic]")
  {
    QUICAckFrameManager ack_manager;

    QUICEncryptionLevel level = QUICEncryptionLevel::INITIAL;
    ack_manager.update(level, 0, 1, false);
    CHECK(ack_manager.will_generate_frame(level, 0) == true);
  }

  SECTION("QUIC handshake packet", "[quic]")
  {
    QUICAckFrameManager ack_manager;

    QUICEncryptionLevel level = QUICEncryptionLevel::HANDSHAKE;
    ack_manager.update(level, 0, 1, false);
    CHECK(ack_manager.will_generate_frame(level, 0) == true);
  }

  SECTION("QUIC frame fired", "[quic]")
  {
    QUICAckFrameManager ack_manager;
    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;

    ack_manager.update(level, 0, 1, false);
    CHECK(ack_manager.will_generate_frame(level, 0) == false);

    sleep(1);
    Thread::get_hrtime_updated();
    CHECK(ack_manager.will_generate_frame(level, 0) == true);
  }

  SECTION("QUIC refresh frame", "[quic]")
  {
    QUICAckFrameManager ack_manager;
    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;

    uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
    QUICFrame *ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
    QUICAckFrame *frame  = static_cast<QUICAckFrame *>(ack_frame);
    CHECK(frame == nullptr);

    // unorder frame should sent immediately
    ack_manager.update(level, 1, 1, false);
    CHECK(ack_manager.will_generate_frame(level, 0) == true);

    ack_manager.update(level, 2, 1, false);

    // Delay due to some reason, the frame is not valued any more, but still valued
    sleep(1);
    Thread::get_hrtime_updated();
    CHECK(ack_manager.will_generate_frame(level, 0) == true);
    ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
    frame     = static_cast<QUICAckFrame *>(ack_frame);

    CHECK(frame->ack_block_count() == 0);
    CHECK(frame->largest_acknowledged() == 2);
    CHECK(frame->ack_block_section()->first_ack_block() == 1);
    CHECK(frame->ack_block_section()->begin()->gap() == 0);
  }
}

TEST_CASE("QUICAckFrameManager_loss_recover", "[quic]")
{
  QUICAckFrameManager ack_manager;
  QUICEncryptionLevel level = QUICEncryptionLevel::INITIAL;

  // Initial state
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICFrame *ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  QUICAckFrame *frame  = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame == nullptr);

  ack_manager.update(level, 2, 1, false);
  ack_manager.update(level, 5, 1, false);
  ack_manager.update(level, 6, 1, false);
  ack_manager.update(level, 8, 1, false);
  ack_manager.update(level, 9, 1, false);

  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  frame     = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 2);
  CHECK(frame->largest_acknowledged() == 9);
  CHECK(frame->ack_block_section()->first_ack_block() == 1);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);

  CHECK(ack_manager.will_generate_frame(level, 0) == false);

  ack_manager.update(level, 7, 1, false);
  ack_manager.update(level, 4, 1, false);
  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  frame     = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 1);
  CHECK(frame->largest_acknowledged() == 9);
  CHECK(frame->ack_block_section()->first_ack_block() == 5);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);
}

TEST_CASE("QUICAckFrameManager_QUICAckFrameCreator", "[quic]")
{
  QUICAckFrameManager ack_manager;
  QUICAckFrameManager::QUICAckFrameCreator packet_numbers(QUICPacketNumberSpace::Initial, &ack_manager);

  CHECK(packet_numbers.size() == 0);
  CHECK(packet_numbers.largest_ack_number() == 0);
  CHECK(packet_numbers.largest_ack_received_time() == 0);

  Thread::get_hrtime_updated();

  packet_numbers.push_back(3, 2, false);
  CHECK(packet_numbers.size() == 1);
  CHECK(packet_numbers.largest_ack_number() == 3);

  ink_hrtime ti = packet_numbers.largest_ack_received_time();
  CHECK(packet_numbers.largest_ack_received_time() != 0);

  Thread::get_hrtime_updated();

  packet_numbers.push_back(2, 2, false);
  CHECK(packet_numbers.size() == 2);
  CHECK(packet_numbers.largest_ack_number() == 3);
  CHECK(packet_numbers.largest_ack_received_time() == ti);

  Thread::get_hrtime_updated();

  packet_numbers.push_back(10, 2, false);
  CHECK(packet_numbers.size() == 3);
  CHECK(packet_numbers.largest_ack_number() == 10);
  CHECK(packet_numbers.largest_ack_received_time() > ti);

  Thread::get_hrtime_updated();

  packet_numbers.clear();
  CHECK(packet_numbers.size() == 0);
  CHECK(packet_numbers.largest_ack_number() == 0);
  CHECK(packet_numbers.largest_ack_received_time() == 0);
}

TEST_CASE("QUICAckFrameManager lost_frame", "[quic]")
{
  QUICAckFrameManager ack_manager;
  QUICEncryptionLevel level = QUICEncryptionLevel::INITIAL;
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];

  // Initial state
  QUICFrame *ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  QUICAckFrame *frame  = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame == nullptr);

  ack_manager.update(level, 2, 1, false);
  ack_manager.update(level, 5, 1, false);
  ack_manager.update(level, 6, 1, false);
  ack_manager.update(level, 8, 1, false);
  ack_manager.update(level, 9, 1, false);

  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  frame     = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 2);
  CHECK(frame->largest_acknowledged() == 9);
  CHECK(frame->ack_block_section()->first_ack_block() == 1);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);

  ack_manager.on_frame_lost(frame->id());
  CHECK(ack_manager.will_generate_frame(level, 0) == true);
  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  frame     = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 2);
  CHECK(frame->largest_acknowledged() == 9);
  CHECK(frame->ack_block_section()->first_ack_block() == 1);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);

  CHECK(ack_manager.will_generate_frame(level, 0) == false);
  ack_manager.on_frame_lost(frame->id());
  CHECK(ack_manager.will_generate_frame(level, 0) == true);
  ack_manager.update(level, 7, 1, false);
  ack_manager.update(level, 4, 1, false);

  ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
  frame     = static_cast<QUICAckFrame *>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 1);
  CHECK(frame->largest_acknowledged() == 9);
  CHECK(frame->ack_block_section()->first_ack_block() == 5);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);

  CHECK(ack_manager.will_generate_frame(level, 0) == false);
}

TEST_CASE("QUICAckFrameManager ack only packet", "[quic]")
{
  SECTION("INITIAL")
  {
    QUICAckFrameManager ack_manager;
    QUICEncryptionLevel level = QUICEncryptionLevel::INITIAL;
    uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];

    // Initial state
    QUICFrame *ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
    QUICAckFrame *frame  = static_cast<QUICAckFrame *>(ack_frame);
    CHECK(frame == nullptr);

    ack_manager.update(level, 1, 1, false);
    ack_manager.update(level, 2, 1, false);
    ack_manager.update(level, 3, 1, false);
    ack_manager.update(level, 4, 1, false);
    ack_manager.update(level, 5, 1, false);

    CHECK(ack_manager.will_generate_frame(level, 0) == true);

    ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
    frame     = static_cast<QUICAckFrame *>(ack_frame);
    CHECK(frame != nullptr);
    CHECK(frame->ack_block_count() == 0);
    CHECK(frame->largest_acknowledged() == 5);
    CHECK(frame->ack_block_section()->first_ack_block() == 4);
    CHECK(frame->ack_block_section()->begin()->gap() == 0);

    ack_manager.update(level, 6, 1, true);
    ack_manager.update(level, 7, 1, true);
    CHECK(ack_manager.will_generate_frame(level, 0) == false);
  }

  SECTION("ONE_RTT")
  {
    QUICAckFrameManager ack_manager;
    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];

    // Initial state
    QUICFrame *ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
    QUICAckFrame *frame  = static_cast<QUICAckFrame *>(ack_frame);
    CHECK(frame == nullptr);

    ack_manager.update(level, 1, 1, false);
    ack_manager.update(level, 2, 1, false);
    ack_manager.update(level, 3, 1, false);
    ack_manager.update(level, 4, 1, false);
    ack_manager.update(level, 5, 1, false);

    CHECK(ack_manager.will_generate_frame(level, 0) == true);

    ack_frame = ack_manager.generate_frame(frame_buf, level, UINT16_MAX, UINT16_MAX, 0);
    frame     = static_cast<QUICAckFrame *>(ack_frame);
    CHECK(frame != nullptr);
    CHECK(frame->ack_block_count() == 0);
    CHECK(frame->largest_acknowledged() == 5);
    CHECK(frame->ack_block_section()->first_ack_block() == 4);
    CHECK(frame->ack_block_section()->begin()->gap() == 0);

    ack_manager.update(level, 6, 1, true);
    ack_manager.update(level, 7, 1, true);
    CHECK(ack_manager.will_generate_frame(level, 0) == false);
  }
}
