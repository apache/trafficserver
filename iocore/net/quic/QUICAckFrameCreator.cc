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
#include "QUICConfig.h"
#include <algorithm>

QUICAckFrameManager::QUICAckFrameManager()
{
  for (auto level : QUIC_PN_SPACES) {
    int index                 = QUICTypeUtil::pn_space_index(level);
    this->_ack_creator[index] = std::make_unique<QUICAckFrameCreator>(level, this);
  }
}

QUICAckFrameManager::~QUICAckFrameManager() {}

void
QUICAckFrameManager::set_ack_delay_exponent(uint8_t ack_delay_exponent)
{
  // This function should be called only once
  ink_assert(this->_ack_delay_exponent == 0);
  this->_ack_delay_exponent = ack_delay_exponent;
}

int
QUICAckFrameManager::update(QUICEncryptionLevel level, QUICPacketNumber packet_number, size_t size, bool ack_only)
{
  if (!this->_is_level_matched(level)) {
    return 0;
  }

  int index         = QUICTypeUtil::pn_space_index(level);
  auto &ack_creator = this->_ack_creator[index];
  ack_creator->push_back(packet_number, size, ack_only);
  return 0;
}

QUICFrameUPtr
QUICAckFrameManager::generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size)
{
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> ack_frame = QUICFrameFactory::create_null_ack_frame();

  if (!this->_is_level_matched(level) || level == QUICEncryptionLevel::ZERO_RTT) {
    return ack_frame;
  }

  int index         = QUICTypeUtil::pn_space_index(level);
  auto &ack_creator = this->_ack_creator[index];
  ack_frame         = ack_creator->generate_ack_frame(maximum_frame_size);

  if (ack_frame != nullptr) {
    QUICFrameInformation info;
    AckFrameInfomation *ack_info   = reinterpret_cast<AckFrameInfomation *>(info.data);
    ack_info->largest_acknowledged = ack_frame->largest_acknowledged();

    info.level = level;
    info.type  = ack_frame->type();
    this->_records_frame(ack_frame->id(), info);
  }

  return ack_frame;
}

bool
QUICAckFrameManager::will_generate_frame(QUICEncryptionLevel level)
{
  // No ACK frame on ZERO_RTT level
  if (!this->_is_level_matched(level) || level == QUICEncryptionLevel::ZERO_RTT) {
    return false;
  }

  int index = QUICTypeUtil::pn_space_index(level);
  return this->_ack_creator[index]->is_ack_frame_ready();
}

void
QUICAckFrameManager::_on_frame_acked(QUICFrameInformation info)
{
  ink_assert(info.type == QUICFrameType::ACK);
  AckFrameInfomation *ack_info = reinterpret_cast<AckFrameInfomation *>(info.data);
  int index                    = QUICTypeUtil::pn_space_index(info.level);
  this->_ack_creator[index]->forget(ack_info->largest_acknowledged);
}

QUICFrameId
QUICAckFrameManager::issue_frame_id()
{
  return this->_issue_frame_id();
}

uint8_t
QUICAckFrameManager::ack_delay_exponent() const
{
  return this->_ack_delay_exponent;
}

int
QUICAckFrameManager::timer_fired()
{
  for (auto level : QUIC_PN_SPACES) {
    int index = QUICTypeUtil::pn_space_index(level);
    this->_ack_creator[index]->refresh_frame();
  }

  return 0;
}

void
QUICAckFrameManager::set_force_to_send(bool on)
{
  this->_force_to_send = on;
}

bool
QUICAckFrameManager::force_to_send() const
{
  return this->_force_to_send;
}

//
// QUICAckFrameManager::QUICAckFrameCreator
//
void
QUICAckFrameManager::QUICAckFrameCreator::refresh_frame()
{
  if (this->_available) {
    // make sure we have the new ack_frame to override the old one.
    this->_ack_frame = this->create_ack_frame();
  }
}

void
QUICAckFrameManager::QUICAckFrameCreator::forget(QUICPacketNumber largest_acknowledged)
{
  this->_available = false;
  this->sort();
  std::list<RecvdPacket> remove_list;
  for (auto it = this->_packet_numbers.begin(); it != this->_packet_numbers.end(); it++) {
    if ((*it).packet_number == largest_acknowledged) {
      remove_list.splice(remove_list.begin(), this->_packet_numbers, it, this->_packet_numbers.end());
      break;
    }
    this->_available |= !(*it).ack_only;
  }

  if (this->_packet_numbers.size() == 0 || !this->_available) {
    this->_should_send = false;
  }
}

void
QUICAckFrameManager::QUICAckFrameCreator::push_back(QUICPacketNumber packet_number, size_t size, bool ack_only)
{
  QUICConfig::scoped_config params;

  if (packet_number == 0 || packet_number > this->_largest_ack_number) {
    this->_largest_ack_received_time = Thread::get_hrtime();
    this->_largest_ack_number        = packet_number;
  }

  if (!this->_latest_packet_received_time) {
    this->_latest_packet_received_time = Thread::get_hrtime();
  }

  // delay too much time
  if (this->_latest_packet_received_time + params->max_ack_delay_in() <= Thread::get_hrtime()) {
    this->_should_send = true;
  }

  // unorder packet should send ack immediately to accellerate the recovery
  if (this->_expect_next != packet_number) {
    this->_should_send = true;
  }

  // every 2 full-packet should send a ack frame like tcp
  this->_size_unsend += size;
  // FIXME: this size should be fixed with PMTU
  if (this->_size_unsend > 2 * 1480) {
    this->_size_unsend = 0;
    this->_should_send = true;
  }

  // can not delay handshake packet
  if (this->_level == QUICEncryptionLevel::INITIAL || this->_level == QUICEncryptionLevel::HANDSHAKE) {
    this->_should_send = true;
  }

  if (this->_ack_manager->force_to_send()) {
    this->_should_send = true;
  }

  if (!ack_only) {
    this->_available = true;
  } else {
    this->_should_send = this->_available ? this->_should_send : false;
  }

  this->_expect_next = packet_number + 1;
  this->_packet_numbers.push_back({ack_only, packet_number});

  if (this->_should_send && this->_available) {
    this->_ack_frame = this->create_ack_frame();
  }
}

size_t
QUICAckFrameManager::QUICAckFrameCreator::size()
{
  return this->_packet_numbers.size();
}

void
QUICAckFrameManager::QUICAckFrameCreator::clear()
{
  this->_packet_numbers.clear();
  this->_largest_ack_number          = 0;
  this->_largest_ack_received_time   = 0;
  this->_latest_packet_received_time = 0;
  this->_size_unsend                 = 0;
  this->_should_send                 = false;
  this->_available                   = false;
}

QUICPacketNumber
QUICAckFrameManager::QUICAckFrameCreator::largest_ack_number()
{
  return this->_largest_ack_number;
}

ink_hrtime
QUICAckFrameManager::QUICAckFrameCreator::largest_ack_received_time()
{
  return this->_largest_ack_received_time;
}

void
QUICAckFrameManager::QUICAckFrameCreator::sort()
{
  //  TODO Find more smart way
  this->_packet_numbers.sort([](const RecvdPacket &a, const RecvdPacket &b) -> bool { return a.packet_number > b.packet_number; });
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICAckFrameManager::QUICAckFrameCreator::generate_ack_frame(uint16_t maximum_frame_size)
{
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> ack_frame = QUICFrameFactory::create_null_ack_frame();
  if (this->_ack_frame && this->_ack_frame->size() <= maximum_frame_size) {
    ack_frame        = std::move(this->_ack_frame);
    this->_ack_frame = nullptr;
  }

  return ack_frame;
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICAckFrameManager::QUICAckFrameCreator::create_ack_frame()
{
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> ack_frame = QUICFrameFactory::create_null_ack_frame();
  if (!this->_available) {
    this->_should_send = false;
    return ack_frame;
  }

  ack_frame = this->_create_ack_frame();
  if (ack_frame != nullptr) {
    this->_available                   = false;
    this->_should_send                 = false;
    this->_latest_packet_received_time = 0;
  }

  return ack_frame;
}

std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc>
QUICAckFrameManager::QUICAckFrameCreator::_create_ack_frame()
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
      uint64_t delay = this->_calculate_delay();
      ack_frame = QUICFrameFactory::create_ack_frame(largest_ack_number, delay, length - 1, this->_ack_manager->issue_frame_id(),
                                                     this->_ack_manager);
    }

    gap             = last_ack_number - pn;
    last_ack_number = pn;
    length          = 0;
  }

  if (ack_frame) {
    ack_frame->ack_block_section()->add_ack_block({static_cast<uint8_t>(gap - 1), length - 1});
  } else {
    uint64_t delay = this->_calculate_delay();
    ack_frame      = QUICFrameFactory::create_ack_frame(largest_ack_number, delay, length - 1, this->_ack_manager->issue_frame_id(),
                                                   this->_ack_manager);
  }

  return ack_frame;
}

uint64_t
QUICAckFrameManager::QUICAckFrameCreator::_calculate_delay()
{
  // Ack delay is in microseconds and scaled
  ink_hrtime now             = Thread::get_hrtime();
  uint64_t delay             = (now - this->_largest_ack_received_time) / 1000;
  uint8_t ack_delay_exponent = 3;
  if (this->_level != QUICEncryptionLevel::INITIAL && this->_level != QUICEncryptionLevel::HANDSHAKE) {
    ack_delay_exponent = this->_ack_manager->ack_delay_exponent();
  }
  return delay >> ack_delay_exponent;
}

bool
QUICAckFrameManager::QUICAckFrameCreator::available() const
{
  return this->_available;
}

bool
QUICAckFrameManager::QUICAckFrameCreator::is_ack_frame_ready() const
{
  return this->_ack_frame != nullptr;
}

QUICAckFrameManager::QUICAckFrameCreator::QUICAckFrameCreator(QUICEncryptionLevel level, QUICAckFrameManager *ack_manager)
  : _ack_manager(ack_manager), _level(level)
{
}

QUICAckFrameManager::QUICAckFrameCreator::~QUICAckFrameCreator() {}
