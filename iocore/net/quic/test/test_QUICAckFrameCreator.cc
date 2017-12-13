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

TEST_CASE("QUICAckFrameCreator", "[quic]")
{
  QUICAckFrameCreator creator;
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> frame = {nullptr, nullptr};

  // Initial state
  frame = creator.create();
  CHECK(frame == nullptr);

  // One packet
  creator.update(1, true);
  frame = creator.create();
  CHECK(frame != nullptr);
  CHECK(frame->num_blocks() == 0);
  CHECK(frame->largest_acknowledged() == 1);
  CHECK(frame->ack_block_section()->first_ack_block_length() == 0);

  frame = creator.create();
  CHECK(frame == nullptr);

  // Not sequential
  creator.update(2, true);
  creator.update(5, true);
  creator.update(3, true);
  creator.update(4, true);
  frame = creator.create();
  CHECK(frame != nullptr);
  CHECK(frame->num_blocks() == 0);
  CHECK(frame->largest_acknowledged() == 5);
  CHECK(frame->ack_block_section()->first_ack_block_length() == 3);

  // Loss
  creator.update(6, true);
  creator.update(7, true);
  creator.update(10, true);
  frame = creator.create();
  CHECK(frame != nullptr);
  CHECK(frame->num_blocks() == 1);
  CHECK(frame->largest_acknowledged() == 10);
  CHECK(frame->ack_block_section()->first_ack_block_length() == 0);
  CHECK(frame->ack_block_section()->begin()->gap() == 1);
}

TEST_CASE("QUICAckFrameCreator_loss_recover", "[quic]")
{
  QUICAckFrameCreator creator;
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> frame = {nullptr, nullptr};

  // Initial state
  frame = creator.create();
  CHECK(frame == nullptr);

  creator.update(2, true);
  creator.update(5, true);
  creator.update(6, true);
  creator.update(8, true);
  creator.update(9, true);

  frame = creator.create();
  CHECK(frame != nullptr);
  CHECK(frame->num_blocks() == 2);
  CHECK(frame->largest_acknowledged() == 9);
  CHECK(frame->ack_block_section()->first_ack_block_length() == 1);
  CHECK(frame->ack_block_section()->begin()->gap() == 0);

  frame = creator.create();
  CHECK(frame == nullptr);

  creator.update(7, true);
  creator.update(4, true);
  frame = creator.create();
  CHECK(frame != nullptr);
  CHECK(frame->num_blocks() == 1);
  CHECK(frame->largest_acknowledged() == 7);
  CHECK(frame->ack_block_section()->first_ack_block_length() == 0);
  CHECK(frame->ack_block_section()->begin()->gap() == 1);
}

TEST_CASE("QUICAckFrameCreator_QUICAckPacketNumbers", "[quic]")
{
  QUICAckPacketNumbers packet_numbers;

  CHECK(packet_numbers.size() == 0);
  CHECK(packet_numbers.largest_ack_number() == 0);
  CHECK(packet_numbers.largest_ack_received_time() == 0);

  Thread::get_hrtime_updated();

  packet_numbers.push_back(3);
  CHECK(packet_numbers.size() == 1);
  CHECK(packet_numbers.largest_ack_number() == 3);

  ink_hrtime ti = packet_numbers.largest_ack_received_time();
  CHECK(packet_numbers.largest_ack_received_time() != 0);

  Thread::get_hrtime_updated();

  packet_numbers.push_back(2);
  CHECK(packet_numbers.size() == 2);
  CHECK(packet_numbers.largest_ack_number() == 3);
  CHECK(packet_numbers.largest_ack_received_time() == ti);

  Thread::get_hrtime_updated();

  packet_numbers.push_back(10);
  CHECK(packet_numbers.size() == 3);
  CHECK(packet_numbers.largest_ack_number() == 10);
  CHECK(packet_numbers.largest_ack_received_time() > ti);

  Thread::get_hrtime_updated();

  packet_numbers.clear();
  CHECK(packet_numbers.size() == 0);
  CHECK(packet_numbers.largest_ack_number() == 0);
  CHECK(packet_numbers.largest_ack_received_time() == 0);
}
