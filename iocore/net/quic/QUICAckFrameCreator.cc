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
#include <P_QUICNetVConnection.h>

QUICAckFrameCreator::QUICAckFrameCreator(QUICNetVConnection *qc)
{
  for (auto i = 0; i < 3; i++) {
    this->_packet_numbers[i] = std::make_unique<QUICAckPacketNumbers>(qc, this);
  }
}

void
QUICAckFrameCreator::set_ack_delay_exponent(uint8_t ack_delay_exponent)
{
  // This function should be called only once
  ink_assert(this->_ack_delay_exponent == 0);
  this->_ack_delay_exponent = ack_delay_exponent;
}

int
QUICAckFrameCreator::update(QUICEncryptionLevel level, QUICPacketNumber packet_number, size_t size, bool ack_only)
{
  if (!this->_is_level_matched(level)) {
    return 0;
  }

  int index            = QUICTypeUtil::pn_space_index(level);
  auto &packet_numbers = this->_packet_numbers[index];
  packet_numbers->push_back(level, packet_number, size, ack_only);
  return 0;
}

QUICFrameUPtr
QUICAckFrameCreator::generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size)
{
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> ack_frame = QUICFrameFactory::create_null_ack_frame();

  if (!this->_is_level_matched(level) || level == QUICEncryptionLevel::ZERO_RTT) {
    return ack_frame;
  }

  int index           = QUICTypeUtil::pn_space_index(level);
  auto &packet_number = this->_packet_numbers[index];
  ack_frame           = packet_number->create_ack_frame(level, maximum_frame_size);

  if (ack_frame != nullptr) {
    QUICFrameInformation info;
    AckFrameInfomation *ack_info   = reinterpret_cast<AckFrameInfomation *>(info.data);
    ack_info->largest_acknowledged = ack_frame->largest_acknowledged();

    info.level = level;
    info.type  = ack_frame->type();
    this->_records_frame(ack_frame->id(), info);

    this->_latest_sent_time = Thread::get_hrtime();
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
  return this->_packet_numbers[index]->should_send();
}

void
QUICAckFrameCreator::_on_frame_acked(QUICFrameInformation info)
{
  ink_assert(info.type == QUICFrameType::ACK);
  AckFrameInfomation *ack_info = reinterpret_cast<AckFrameInfomation *>(info.data);
  int index                    = QUICTypeUtil::pn_space_index(info.level);
  this->_packet_numbers[index]->forget(ack_info->largest_acknowledged);
}

QUICFrameId
QUICAckFrameCreator::issue_frame_id()
{
  return this->_issue_frame_id();
}

uint8_t
QUICAckFrameCreator::ack_delay_exponent() const
{
  return this->_ack_delay_exponent;
}

//
// QUICAckFrameCreator::QUICAckPacketNumbers
//
void
QUICAckFrameCreator::QUICAckPacketNumbers::set_creator(QUICAckFrameCreator *creator)
{
  this->_ack_creator = creator;
}

void
QUICAckFrameCreator::QUICAckPacketNumbers::forget(QUICPacketNumber largest_acknowledged)
{
  this->_available = false;
  this->sort();
  std::list<RecvdPacket> remove_list;
  for (auto it = this->_packet_numbers.begin(); it != this->_packet_numbers.end(); it++) {
    this->_available |= !(*it).ack_only;
    if ((*it).packet_number == largest_acknowledged) {
      remove_list.splice(remove_list.begin(), this->_packet_numbers, it, this->_packet_numbers.end());
      break;
    }
  }

  if (this->_packet_numbers.size() == 0 || !this->_available) {
    this->_should_send = false;
    this->_cancel_timer();
  }
}

void
QUICAckFrameCreator::QUICAckPacketNumbers::push_back(QUICEncryptionLevel level, QUICPacketNumber packet_number, size_t size,
                                                     bool ack_only)
{
  if (packet_number == 0 || packet_number > this->_largest_ack_number) {
    this->_largest_ack_received_time = Thread::get_hrtime();
    this->_largest_ack_number        = packet_number;
  }

  // delay too much time
  if (this->_largest_ack_received_time + QUIC_ACK_CREATOR_MAX_DELAY <= Thread::get_hrtime()) {
    this->_should_send = true;
  }

  // unorder packet should send ack immediately to accellerate the recovery
  if (this->_expect_next != packet_number) {
    this->_expect_next = std::max(this->_expect_next, packet_number + 1);
    this->_should_send = true;
  }

  // every 2 full-packet should send a ack frame like tcp
  this->_size_unsend += size;
  if (this->_size_unsend > 2 * 1480) {
    this->_size_unsend = 0;
    this->_should_send = true;
  }

  // can not delay handshake packet
  if (level == QUICEncryptionLevel::INITIAL || level == QUICEncryptionLevel::HANDSHAKE) {
    this->_should_send = true;
  }

  if (!ack_only) {
    this->_available = true;
  } else {
    this->_should_send = this->_available ? this->_should_send : false;
  }

  if (!this->_should_send && this->_available) {
    this->_start_timer();
  }

  this->_packet_numbers.push_back({ack_only, packet_number});
}

size_t
QUICAckFrameCreator::QUICAckPacketNumbers::size()
{
  return this->_packet_numbers.size();
}

void
QUICAckFrameCreator::QUICAckPacketNumbers::clear()
{
  this->_packet_numbers.clear();
  this->_largest_ack_number        = 0;
  this->_largest_ack_received_time = 0;
}

QUICPacketNumber
QUICAckFrameCreator::QUICAckPacketNumbers::largest_ack_number()
{
  return this->_largest_ack_number;
}

ink_hrtime
QUICAckFrameCreator::QUICAckPacketNumbers::largest_ack_received_time()
{
  return this->_largest_ack_received_time;
}

void
QUICAckFrameCreator::QUICAckPacketNumbers::sort()
{
  //  TODO Find more smart way
  this->_packet_numbers.sort([](const RecvdPacket &a, const RecvdPacket &b) -> bool { return a.packet_number > b.packet_number; });
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICAckFrameCreator::QUICAckPacketNumbers::create_ack_frame(QUICEncryptionLevel level, uint16_t maximum_frame_size)
{
  auto ack_frame = QUICFrameFactory::create_null_ack_frame();
  if (!this->_available) {
    this->_should_send = false;
    this->_cancel_timer();
    return ack_frame;
  }

  ack_frame = this->_create_ack_frame(level);
  if (ack_frame && ack_frame->size() > maximum_frame_size) {
    // Cancel generating frame
    ack_frame = QUICFrameFactory::create_null_ack_frame();
  } else {
    this->_available   = false;
    this->_should_send = false;
    this->_cancel_timer();
  }

  return ack_frame;
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICAckFrameCreator::QUICAckPacketNumbers::_create_ack_frame(QUICEncryptionLevel level)
{
  ink_assert(this->_packet_numbers.size() > 0);
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> ack_frame = QUICFrameFactory::create_null_ack_frame();
  this->sort();
  std::list<RecvdPacket> &list = this->_packet_numbers;

  uint8_t gap     = 0;
  uint64_t length = 0;
  auto it         = list.begin();

  // skip ack_only packets
  for (; it != list.end(); it++) {
    if (!(*it).ack_only) {
      break;
    }
  }

  if (it == list.end()) {
    return ack_frame;
  }

  QUICPacketNumber largest_ack_number = (*it).packet_number;
  QUICPacketNumber last_ack_number    = largest_ack_number;

  while (it != list.end()) {
    QUICPacketNumber pn = (*it).packet_number;
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
      ack_frame = QUICFrameFactory::create_ack_frame(largest_ack_number, delay, length - 1, this->_ack_creator->issue_frame_id(),
                                                     this->_ack_creator);
    }

    gap             = last_ack_number - pn;
    last_ack_number = pn;
    length          = 0;
  }

  if (ack_frame) {
    ack_frame->ack_block_section()->add_ack_block({static_cast<uint8_t>(gap - 1), length - 1});
  } else {
    uint64_t delay = this->_calculate_delay(level);
    ack_frame      = QUICFrameFactory::create_ack_frame(largest_ack_number, delay, length - 1, this->_ack_creator->issue_frame_id(),
                                                   this->_ack_creator);
  }

  return ack_frame;
}

uint64_t
QUICAckFrameCreator::QUICAckPacketNumbers::_calculate_delay(QUICEncryptionLevel level)
{
  // Ack delay is in microseconds and scaled
  ink_hrtime now             = Thread::get_hrtime();
  uint64_t delay             = (now - this->_largest_ack_received_time) / 1000;
  uint8_t ack_delay_exponent = 3;
  if (level != QUICEncryptionLevel::INITIAL && level != QUICEncryptionLevel::HANDSHAKE) {
    ack_delay_exponent = this->_ack_creator->ack_delay_exponent();
  }
  return delay >> ack_delay_exponent;
}

int
QUICAckFrameCreator::QUICAckPacketNumbers::timer_fired(int event, Event *ev)
{
  this->_event       = nullptr;
  this->_should_send = true;
  this->_qc->common_send_packet();
  return 0;
}

bool
QUICAckFrameCreator::QUICAckPacketNumbers::available() const
{
  return this->_available;
}

bool
QUICAckFrameCreator::QUICAckPacketNumbers::should_send() const
{
  return this->_should_send;
}

void
QUICAckFrameCreator::QUICAckPacketNumbers::_start_timer()
{
  if (this->_event == nullptr) {
    this->_event = this_ethread()->schedule_at(this, Thread::get_hrtime() + QUIC_ACK_CREATOR_MAX_DELAY);
  }
}

void
QUICAckFrameCreator::QUICAckPacketNumbers::_cancel_timer()
{
  if (this->_event) {
    this->_event->cancel();
    this->_event = nullptr;
  }
}

QUICAckFrameCreator::QUICAckPacketNumbers::QUICAckPacketNumbers(QUICNetVConnection *qc, QUICAckFrameCreator *ack_creator)
  : _qc(qc), _ack_creator(ack_creator)
{
  SET_HANDLER(&QUICAckFrameCreator::QUICAckPacketNumbers::timer_fired);
}

QUICAckFrameCreator::QUICAckPacketNumbers::~QUICAckPacketNumbers()
{
  this->_cancel_timer();
}
