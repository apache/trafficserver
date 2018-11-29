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

#include "I_EventSystem.h"
#include "quic/QUICAckFrameCreator.h"
#include "Mock.h"

TEST_CASE("QUICAckFrameCreator", "[quic]")
{
  MockQUICConnection qc;
  QUICAckFrameCreator creator(&qc);
  QUICEncryptionLevel level = QUICEncryptionLevel::INITIAL;

  // Initial state
  std::shared_ptr<QUICFrame> ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);
  std::shared_ptr<QUICAckFrame> frame  = std::static_pointer_cast<QUICAckFrame>(ack_frame);
  CHECK(frame == nullptr);

  // One packet
  creator.update(level, 1, 1, false);
  ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);
  frame     = std::static_pointer_cast<QUICAckFrame>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 0);
  CHECK(frame->largest_acknowledged() == 1);
  CHECK(frame->ack_block_section()->first_ack_block() == 0);

  // retry
  CHECK(creator.will_generate_frame(level) == false);

  // Not sequential
  creator.update(level, 2, 1, false);
  creator.update(level, 5, 1, false);
  creator.update(level, 3, 1, false);
  creator.update(level, 4, 1, false);
  ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);
  frame     = std::static_pointer_cast<QUICAckFrame>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 0);
  CHECK(frame->largest_acknowledged() == 5);
  CHECK(frame->ack_block_section()->first_ack_block() == 4);

  // Loss
  creator.update(level, 6, 1, false);
  creator.update(level, 7, 1, false);
  creator.update(level, 10, 1, false);
  ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);
  frame     = std::static_pointer_cast<QUICAckFrame>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 1);
  CHECK(frame->largest_acknowledged() == 10);
  CHECK(frame->ack_block_section()->first_ack_block() == 0);
  CHECK(frame->ack_block_section()->begin()->gap() == 1);

  // on frame acked
  creator.on_frame_acked(frame->id());

  CHECK(creator.will_generate_frame(level) == false);
  ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);
  CHECK(ack_frame == nullptr);

  creator.update(level, 11, 1, false);
  creator.update(level, 12, 1, false);
  creator.update(level, 13, 1, false);
  ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);
  frame     = std::static_pointer_cast<QUICAckFrame>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 0);
  CHECK(frame->largest_acknowledged() == 13);
  CHECK(frame->ack_block_section()->first_ack_block() == 2);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);

  creator.on_frame_acked(frame->id());

  // ack-only
  creator.update(level, 14, 1, true);
  creator.update(level, 15, 1, true);
  creator.update(level, 16, 1, true);
  CHECK(creator.will_generate_frame(level) == false);
  ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);

  creator.update(level, 17, 1, false);
  ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);
  frame     = std::static_pointer_cast<QUICAckFrame>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 0);
  CHECK(frame->largest_acknowledged() == 17);
  CHECK(frame->ack_block_section()->first_ack_block() == 3);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);
}

TEST_CASE("QUICAckFrameCreator should send", "[quic]")
{
  SECTION("QUIC unorder packet", "[quic]")
  {
    MockQUICConnection qc;
    QUICAckFrameCreator creator(&qc);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    creator.update(level, 2, 1, false);
    CHECK(creator.will_generate_frame(level) == true);
  }

  SECTION("QUIC delay ack and unorder packet", "[quic]")
  {
    MockQUICConnection qc;
    QUICAckFrameCreator creator(&qc);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    creator.update(level, 0, 1, false);
    CHECK(creator.will_generate_frame(level) == false);

    creator.update(level, 1, 1, false);
    CHECK(creator.will_generate_frame(level) == false);

    creator.update(level, 3, 1, false);
    CHECK(creator.will_generate_frame(level) == true);
  }

  SECTION("QUIC delay too much time", "[quic]")
  {
    Thread::get_hrtime_updated();
    MockQUICConnection qc;
    QUICAckFrameCreator creator(&qc);

    QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
    creator.update(level, 0, 1, false);
    CHECK(creator.will_generate_frame(level) == false);

    sleep(1);
    Thread::get_hrtime_updated();
    creator.update(level, 1, 1, false);
    CHECK(creator.will_generate_frame(level) == true);
  }

  SECTION("QUIC intial packet", "[quic]")
  {
    MockQUICConnection qc;
    QUICAckFrameCreator creator(&qc);

    QUICEncryptionLevel level = QUICEncryptionLevel::INITIAL;
    creator.update(level, 0, 1, false);
    CHECK(creator.will_generate_frame(level) == true);
  }

  SECTION("QUIC handshake packet", "[quic]")
  {
    MockQUICConnection qc;
    QUICAckFrameCreator creator(&qc);

    QUICEncryptionLevel level = QUICEncryptionLevel::HANDSHAKE;
    creator.update(level, 0, 1, false);
    CHECK(creator.will_generate_frame(level) == true);
  }
}

TEST_CASE("QUICAckFrameCreator_loss_recover", "[quic]")
{
  MockQUICConnection qc;
  QUICAckFrameCreator creator(&qc);
  QUICEncryptionLevel level = QUICEncryptionLevel::INITIAL;

  // Initial state
  std::shared_ptr<QUICFrame> ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);
  std::shared_ptr<QUICAckFrame> frame  = std::static_pointer_cast<QUICAckFrame>(ack_frame);
  CHECK(frame == nullptr);

  creator.update(level, 2, 1, false);
  creator.update(level, 5, 1, false);
  creator.update(level, 6, 1, false);
  creator.update(level, 8, 1, false);
  creator.update(level, 9, 1, false);

  ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);
  frame     = std::static_pointer_cast<QUICAckFrame>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 2);
  CHECK(frame->largest_acknowledged() == 9);
  CHECK(frame->ack_block_section()->first_ack_block() == 1);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);

  CHECK(creator.will_generate_frame(level) == false);

  creator.update(level, 7, 1, false);
  creator.update(level, 4, 1, false);
  ack_frame = creator.generate_frame(level, UINT16_MAX, UINT16_MAX);
  frame     = std::static_pointer_cast<QUICAckFrame>(ack_frame);
  CHECK(frame != nullptr);
  CHECK(frame->ack_block_count() == 1);
  CHECK(frame->largest_acknowledged() == 9);
  CHECK(frame->ack_block_section()->first_ack_block() == 5);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);
}

TEST_CASE("QUICAckFrameCreator_QUICAckPacketNumbers", "[quic]")
{
  MockQUICConnection qc;
  QUICAckFrameCreator creator(&qc);
  QUICAckFrameCreator::QUICAckPacketNumbers packet_numbers(&qc, &creator);
  QUICEncryptionLevel level = QUICEncryptionLevel::INITIAL;

  CHECK(packet_numbers.size() == 0);
  CHECK(packet_numbers.largest_ack_number() == 0);
  CHECK(packet_numbers.largest_ack_received_time() == 0);

  Thread::get_hrtime_updated();

  packet_numbers.push_back(level, 3, 2, false);
  CHECK(packet_numbers.size() == 1);
  CHECK(packet_numbers.largest_ack_number() == 3);

  ink_hrtime ti = packet_numbers.largest_ack_received_time();
  CHECK(packet_numbers.largest_ack_received_time() != 0);

  Thread::get_hrtime_updated();

  packet_numbers.push_back(level, 2, 2, false);
  CHECK(packet_numbers.size() == 2);
  CHECK(packet_numbers.largest_ack_number() == 3);
  CHECK(packet_numbers.largest_ack_received_time() == ti);

  Thread::get_hrtime_updated();

  packet_numbers.push_back(level, 10, 2, false);
  CHECK(packet_numbers.size() == 3);
  CHECK(packet_numbers.largest_ack_number() == 10);
  CHECK(packet_numbers.largest_ack_received_time() > ti);

  Thread::get_hrtime_updated();

  packet_numbers.clear();
  CHECK(packet_numbers.size() == 0);
  CHECK(packet_numbers.largest_ack_number() == 0);
  CHECK(packet_numbers.largest_ack_received_time() == 0);
}
