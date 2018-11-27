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

#include "I_EventSystem.h"
#include "QUICAckFrameCreator.h"
#include <algorithm>

void
QUICAckFrameCreator::set_ack_delay_exponent(uint8_t ack_delay_exponent)
{
  // This function should be called only once
  ink_assert(this->_ack_delay_exponent == 0);
  this->_ack_delay_exponent = ack_delay_exponent;
}

int
QUICAckFrameCreator::update(QUICEncryptionLevel level, QUICPacketNumber packet_number, bool should_send)
{
  if (!this->_is_level_matched(level)) {
    return 0;
  }

  int index                            = QUICTypeUtil::pn_space_index(level);
  QUICAckPacketNumbers *packet_numbers = &this->_packet_numbers[index];
  packet_numbers->push_back(packet_number);

  if (packet_numbers->size() == MAXIMUM_PACKET_COUNT) {
    return -1;
  }

  if (!this->_can_send[index]) {
    this->_can_send[index] = true;
  }

  if (should_send) {
    this->_should_send[index] = true;
  }

  return 0;
}

QUICFrameUPtr
QUICAckFrameCreator::generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size)
{
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> ack_frame = QUICFrameFactory::create_null_ack_frame();

  if (!this->_is_level_matched(level) || level == QUICEncryptionLevel::ZERO_RTT) {
    return ack_frame;
  }

  int index = QUICTypeUtil::pn_space_index(level);
  if (this->_can_send[index]) {
    ack_frame = this->_create_ack_frame(level);
    if (ack_frame && ack_frame->size() > maximum_frame_size) {
      // Cancel generating frame
      ack_frame = QUICFrameFactory::create_null_ack_frame();
    } else {
      this->_can_send[index]    = true;
      this->_should_send[index] = false;
    }
  }

  if (ack_frame != nullptr) {
    AckFrameInfomation ack_info;
    ack_info.largest_acknowledged = ack_frame->largest_acknowledged();

    QUICFrameInformation info;
    info.level = level;
    info.type  = ack_frame->type();
    memcpy(info.data, &ack_info, sizeof(ack_info));
    this->_records_frame(ack_frame->id(), info);
  }

  return ack_frame;
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICAckFrameCreator::_create_ack_frame(QUICEncryptionLevel level)
{
  int index = QUICTypeUtil::pn_space_index(level);
  this->_packet_numbers[index].sort();
  auto list = this->_packet_numbers[index].list();

  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> ack_frame = QUICFrameFactory::create_null_ack_frame();
  QUICPacketNumber largest_ack_number                           = list.front();
  QUICPacketNumber last_ack_number                              = largest_ack_number;

  uint8_t gap     = 0;
  uint64_t length = 0;

  auto it = list.begin();
  while (it != list.end()) {
    QUICPacketNumber pn = *it;
    if (pn == last_ack_number) {
      last_ack_number--;
      length++;
      it++;
      continue;
    }

    ink_assert(length > 0);

    if (ack_frame) {
      ack_frame->ack_block_section()->add_ack_block({static_cast<uint8_t>(gap - 1), length - 1});
    } else {
      uint64_t delay = this->_calculate_delay(level);
      ack_frame      = QUICFrameFactory::create_ack_frame(largest_ack_number, delay, length - 1, this->_issue_frame_id(), this);
    }

    gap             = last_ack_number - pn;
    last_ack_number = pn;
    length          = 0;
  }

  if (ack_frame) {
    ack_frame->ack_block_section()->add_ack_block({static_cast<uint8_t>(gap - 1), length - 1});
  } else {
    uint64_t delay = this->_calculate_delay(level);
    ack_frame      = QUICFrameFactory::create_ack_frame(largest_ack_number, delay, length - 1, this->_issue_frame_id(), this);
  }

  return ack_frame;
}

bool
QUICAckFrameCreator::will_generate_frame(QUICEncryptionLevel level)
{
  // No ACK frame on ZERO_RTT level
  if (!this->_is_level_matched(level) || level == QUICEncryptionLevel::ZERO_RTT) {
    return false;
  }

  int index = QUICTypeUtil::pn_space_index(level);
  return this->_should_send[index];
}

uint64_t
QUICAckFrameCreator::_calculate_delay(QUICEncryptionLevel level)
{
  // Ack delay is in microseconds and scaled
  ink_hrtime now             = Thread::get_hrtime();
  int index                  = QUICTypeUtil::pn_space_index(level);
  uint64_t delay             = (now - this->_packet_numbers[index].largest_ack_received_time()) / 1000;
  uint8_t ack_delay_exponent = 3;
  if (level != QUICEncryptionLevel::INITIAL && level != QUICEncryptionLevel::HANDSHAKE) {
    ack_delay_exponent = this->_ack_delay_exponent;
  }
  return delay >> ack_delay_exponent;
}

void
QUICAckFrameCreator::_on_frame_acked(QUICFrameInformation info)
{
  ink_assert(info.type == QUICFrameType::ACK);
  AckFrameInfomation *ack_info = reinterpret_cast<AckFrameInfomation *>(info.data);
  int index                    = QUICTypeUtil::pn_space_index(info.level);
  this->_packet_numbers[index].forget(ack_info->largest_acknowledged);
  if (this->_packet_numbers[index].size() == 0) {
    this->_can_send[index]    = false;
    this->_should_send[index] = false;
  }
}

//
// QUICAckPacketNumbers
//
void
QUICAckPacketNumbers::forget(QUICPacketNumber largest_acknowledged)
{
  this->sort();
  std::list<QUICPacketNumber> remove_list;
  for (auto it = this->_packet_numbers.begin(); it != this->_packet_numbers.end(); it++) {
    if (*it == largest_acknowledged) {
      remove_list.splice(remove_list.begin(), this->_packet_numbers, it, this->_packet_numbers.end());
      return;
    }
  }
}

void
QUICAckPacketNumbers::push_back(QUICPacketNumber packet_number)
{
  if (packet_number == 0 || packet_number > this->_largest_ack_number) {
    this->_largest_ack_received_time = Thread::get_hrtime();
    this->_largest_ack_number        = packet_number;
  }

  this->_packet_numbers.push_back(packet_number);
}

const std::list<QUICPacketNumber>
QUICAckPacketNumbers::list() const
{
  return this->_packet_numbers;
}

size_t
QUICAckPacketNumbers::size()
{
  return this->_packet_numbers.size();
}

void
QUICAckPacketNumbers::clear()
{
  this->_packet_numbers.clear();
  this->_largest_ack_number        = 0;
  this->_largest_ack_received_time = 0;
}

QUICPacketNumber
QUICAckPacketNumbers::largest_ack_number()
{
  return this->_largest_ack_number;
}

ink_hrtime
QUICAckPacketNumbers::largest_ack_received_time()
{
  return this->_largest_ack_received_time;
}

void
QUICAckPacketNumbers::sort()
{
  //  TODO Find more smart way
  this->_packet_numbers.sort([](const QUICPacketNumber a, const QUICPacketNumber b) -> bool { return a > b; });
}
