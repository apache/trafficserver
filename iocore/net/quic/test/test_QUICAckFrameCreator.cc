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
  CHECK(frame->ack_block_section()->first_ack_block_length() == 1);

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
  CHECK(frame->ack_block_section()->first_ack_block_length() == 4);

  // Loss
  creator.update(6, true);
  creator.update(7, true);
  creator.update(10, true);
  frame = creator.create();
  CHECK(frame != nullptr);
  CHECK(frame->num_blocks() == 1);
  CHECK(frame->largest_acknowledged() == 10);
  CHECK(frame->ack_block_section()->first_ack_block_length() == 2);
  CHECK(frame->ack_block_section()->begin()->gap() == 2);
}
