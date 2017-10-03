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

#include "QUICAckFrameCreator.h"
#include <algorithm>

int
QUICAckFrameCreator::update(QUICPacketNumber packet_number, bool acknowledgable)
{
  if (this->_packet_count == MAXIMUM_PACKET_COUNT) {
    return -1;
  }
  if (packet_number > this->_largest_ack_number) {
    this->_largest_ack_number        = packet_number;
    this->_largest_ack_received_time = Thread::get_hrtime();
  }
  this->_packet_numbers[this->_packet_count++] = packet_number - this->_last_ack_number;
  if (acknowledgable && !this->_can_send) {
    this->_can_send = true;
  }

  return 0;
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICAckFrameCreator::create()
{
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> ack_frame = {nullptr, QUICFrameDeleter::delete_null_frame};
  if (this->_can_send) {
    ack_frame              = this->_create_ack_frame();
    this->_last_ack_number = this->_largest_ack_number;
    this->_can_send        = false;
    this->_packet_count    = 0;
  }
  return ack_frame;
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICAckFrameCreator::create_if_needed()
{
  // TODO What would be criteria?
  return this->create();
}

void
QUICAckFrameCreator::_sort_packet_numbers()
{
  // TODO Find more smart way
  std::sort(this->_packet_numbers, this->_packet_numbers + this->_packet_count);
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICAckFrameCreator::_create_ack_frame()
{
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> ack_frame = {nullptr, QUICFrameDeleter::delete_null_frame};
  this->_sort_packet_numbers();
  uint16_t start = this->_packet_numbers[0];
  uint8_t gap = 0;
  int i;
  uint64_t length = 0;
  for (i = 0, length = 0; i < this->_packet_count; ++i, ++length) {
    if (this->_packet_numbers[i] == start + length) {
      continue;
    }
    if (ack_frame) {
      ack_frame->ack_block_section()->add_ack_block({gap, length});
    } else {
      uint16_t delay = (Thread::get_hrtime() - this->_largest_ack_received_time) / 1000; // TODO Milliseconds?
      ack_frame      = QUICFrameFactory::create_ack_frame(this->_largest_ack_number, delay, length);
    }
    gap    = this->_packet_numbers[i] - this->_packet_numbers[i - 1] - 1;
    start  = this->_packet_numbers[i];
    length = 0;
  }
  if (ack_frame) {
    ack_frame->ack_block_section()->add_ack_block({gap, length});
  } else {
    uint16_t delay = (Thread::get_hrtime() - this->_largest_ack_received_time) / 1000; // TODO Milliseconds?
    ack_frame      = QUICFrameFactory::create_ack_frame(this->_largest_ack_number, delay, length);
  }
  return ack_frame;
}
