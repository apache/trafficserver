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
  for (auto i = 0; i < kPacketNumberSpace; i++) {
    this->_ack_creator[i] = std::make_unique<QUICAckFrameCreator>(static_cast<QUICPacketNumberSpace>(i), this);
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

  auto index        = QUICTypeUtil::pn_space(level);
  auto &ack_creator = this->_ack_creator[static_cast<int>(index)];
  ack_creator->push_back(packet_number, size, ack_only);
  return 0;
}

/**
 * @param connection_credit This is not used. Because ACK frame is not flow-controlled
 */
QUICFrame *
QUICAckFrameManager::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t /* connection_credit */,
                                    uint16_t maximum_frame_size, ink_hrtime timestamp)
{
  QUICAckFrame *ack_frame = nullptr;

  if (!this->_is_level_matched(level) || level == QUICEncryptionLevel::ZERO_RTT) {
    return ack_frame;
  }

  auto index        = QUICTypeUtil::pn_space(level);
  auto &ack_creator = this->_ack_creator[static_cast<int>(index)];
  ack_frame         = ack_creator->generate_ack_frame(buf, maximum_frame_size);

  if (ack_frame != nullptr) {
    QUICFrameInformationUPtr info  = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
    AckFrameInfo *ack_info         = reinterpret_cast<AckFrameInfo *>(info->data);
    ack_info->largest_acknowledged = ack_frame->largest_acknowledged();

    info->level = level;
    info->type  = ack_frame->type();
    this->_records_frame(ack_frame->id(), std::move(info));
  }

  return ack_frame;
}

bool
QUICAckFrameManager::will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp)
{
  // No ACK frame on ZERO_RTT level
  if (!this->_is_level_matched(level) || level == QUICEncryptionLevel::ZERO_RTT) {
    return false;
  }

  auto index = QUICTypeUtil::pn_space(level);
  return this->_ack_creator[static_cast<int>(index)]->is_ack_frame_ready();
}

void
QUICAckFrameManager::_on_frame_acked(QUICFrameInformationUPtr &info)
{
  ink_assert(info->type == QUICFrameType::ACK);
  AckFrameInfo *ack_info = reinterpret_cast<AckFrameInfo *>(info->data);
  auto index             = QUICTypeUtil::pn_space(info->level);
  this->_ack_creator[static_cast<int>(index)]->forget(ack_info->largest_acknowledged);
}

void
QUICAckFrameManager::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  ink_assert(info->type == QUICFrameType::ACK);
  auto index = QUICTypeUtil::pn_space(info->level);
  // when ack frame lost. Force to refresh the frame.
  this->_ack_creator[static_cast<int>(index)]->refresh_state();
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

void
QUICAckFrameManager::set_max_ack_delay(uint16_t delay)
{
  for (auto i = 0; i < kPacketNumberSpace; i++) {
    this->_ack_creator[i]->set_max_ack_delay(delay);
  }
}

//
// QUICAckFrameManager::QUICAckFrameCreator
//
void
QUICAckFrameManager::QUICAckFrameCreator::refresh_state()
{
  if (this->_packet_numbers.empty() || !this->_available) {
    return;
  }

  // we have something to send
  this->_should_send = true;
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

  if (this->_packet_numbers.empty() || !this->_available) {
    this->_should_send = false;
  }
}

void
QUICAckFrameManager::QUICAckFrameCreator::push_back(QUICPacketNumber packet_number, size_t size, bool ack_only)
{
  if (packet_number == 0 || packet_number > this->_largest_ack_number) {
    this->_largest_ack_received_time = Thread::get_hrtime();
    this->_largest_ack_number        = packet_number;
  }

  if (!this->_latest_packet_received_time) {
    this->_latest_packet_received_time = Thread::get_hrtime();
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
  if ((this->_pn_space == QUICPacketNumberSpace::Initial || this->_pn_space == QUICPacketNumberSpace::Handshake) && !ack_only) {
    this->_should_send = true;
  }

  if (!ack_only) {
    this->_available    = true;
    this->_has_new_data = true;
  } else {
    this->_should_send = this->_available ? this->_should_send : false;
  }

  this->_expect_next = packet_number + 1;
  this->_packet_numbers.push_back({ack_only, packet_number});
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

QUICAckFrame *
QUICAckFrameManager::QUICAckFrameCreator::generate_ack_frame(uint8_t *buf, uint16_t maximum_frame_size)
{
  QUICAckFrame *ack_frame = nullptr;
  if (!this->_available) {
    this->_should_send = false;
    return ack_frame;
  }

  ack_frame = this->_create_ack_frame(buf);
  if (ack_frame == nullptr || ack_frame->size() < maximum_frame_size) {
    this->_should_send                 = false;
    this->_latest_packet_received_time = 0;
  } else {
    return nullptr;
  }

  return ack_frame;
}

QUICAckFrame *
QUICAckFrameManager::QUICAckFrameCreator::_create_ack_frame(uint8_t *buf)
{
  ink_assert(!this->_packet_numbers.empty());
  QUICAckFrame *ack_frame = nullptr;
  this->sort();
  std::list<RecvdPacket> &list = this->_packet_numbers;

  this->_has_new_data = false;

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
      ack_frame      = QUICFrameFactory::create_ack_frame(buf, largest_ack_number, delay, length - 1,
                                                     this->_ack_manager->issue_frame_id(), this->_ack_manager);
    }

    gap             = last_ack_number - pn;
    last_ack_number = pn;
    length          = 0;
  }

  if (ack_frame) {
    ack_frame->ack_block_section()->add_ack_block({static_cast<uint8_t>(gap - 1), length - 1});
  } else {
    uint64_t delay = this->_calculate_delay();
    ack_frame = QUICFrameFactory::create_ack_frame(buf, largest_ack_number, delay, length - 1, this->_ack_manager->issue_frame_id(),
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
  if (this->_pn_space != QUICPacketNumberSpace::Initial && this->_pn_space != QUICPacketNumberSpace::Handshake) {
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
QUICAckFrameManager::QUICAckFrameCreator::is_ack_frame_ready()
{
  if (this->_available && this->_has_new_data && !this->_packet_numbers.empty() &&
      this->_latest_packet_received_time + this->_max_ack_delay * HRTIME_MSECOND <= Thread::get_hrtime()) {
    // when we has new data and the data is available to send (not ack only). and we delay for too much time. Send it out
    this->_should_send = true;
  }

  return this->_should_send && this->_available && !this->_packet_numbers.empty();
}

void
QUICAckFrameManager::QUICAckFrameCreator::set_max_ack_delay(uint16_t delay)
{
  this->_max_ack_delay = delay;
}

QUICAckFrameManager::QUICAckFrameCreator::QUICAckFrameCreator(QUICPacketNumberSpace pn_space, QUICAckFrameManager *ack_manager)
  : _ack_manager(ack_manager), _pn_space(pn_space)
{
}

QUICAckFrameManager::QUICAckFrameCreator::~QUICAckFrameCreator() {}

/*
   No limit of encryption level.
   ```
   std::array<QUICEncryptionLevel, 4> _encryption_level_filter = {
     QUICEncryptionLevel::INITIAL,
     QUICEncryptionLevel::ZERO_RTT,
     QUICEncryptionLevel::HANDSHAKE,
     QUICEncryptionLevel::ONE_RTT,
   };
   ```
*/
bool
QUICAckFrameManager::_is_level_matched(QUICEncryptionLevel level)
{
  return true;
}
