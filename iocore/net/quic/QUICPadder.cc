/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "QUICIntUtil.h"
#include "QUICPadder.h"

static constexpr uint32_t MINIMUM_INITIAL_PACKET_SIZE = 1200;
static constexpr uint32_t MIN_PKT_PAYLOAD_LEN         = 3; ///< Minimum payload length for sampling for header protection

void
QUICPadder::request(QUICEncryptionLevel level)
{
  SCOPED_MUTEX_LOCK(lock, this->_mutex, this_ethread());
  if (!this->_is_level_matched(level)) {
    return;
  }
  ++this->_need_to_fire[static_cast<int>(level)];
}

void
QUICPadder::cancel(QUICEncryptionLevel level)
{
  SCOPED_MUTEX_LOCK(lock, this->_mutex, this_ethread());
  if (!this->_is_level_matched(level)) {
    return;
  }

  this->_need_to_fire[static_cast<int>(level)] = 0;
}

uint64_t
QUICPadder::count(QUICEncryptionLevel level)
{
  SCOPED_MUTEX_LOCK(lock, this->_mutex, this_ethread());
  return this->_need_to_fire[static_cast<int>(level)];
}

bool
QUICPadder::will_generate_frame(QUICEncryptionLevel level, uint32_t seq_num)
{
  SCOPED_MUTEX_LOCK(lock, this->_mutex, this_ethread());
  if (seq_num == this->_latest_seq_num) {
    return false;
  }

  this->_latest_seq_num = seq_num;
  return true;
}

QUICFrame *
QUICPadder::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                           size_t current_packet_size, uint32_t seq_num)
{
  SCOPED_MUTEX_LOCK(lock, this->_mutex, this_ethread());
  QUICFrame *frame = nullptr;

  // no extre padding packet
  if (current_packet_size == 0 && this->_need_to_fire[static_cast<int>(level)] == 0) {
    return frame;
  }

  uint64_t min_size = 0;
  if (level == QUICEncryptionLevel::INITIAL && this->_context == NET_VCONNECTION_OUT) {
    min_size = this->_minimum_quic_packet_size();
    if (this->_av_token_len && min_size > (QUICVariableInt::size(this->_av_token_len) + this->_av_token_len)) {
      min_size -= (QUICVariableInt::size(this->_av_token_len) + this->_av_token_len);
    }
  } else {
    min_size = MIN_PKT_PAYLOAD_LEN;
  }

  if (min_size > current_packet_size) { // ignore if we don't need to pad.
    frame = QUICFrameFactory::create_padding_frame(
      buf, 0, std::min(min_size - current_packet_size, static_cast<uint64_t>(maximum_frame_size)));
  }

  this->_need_to_fire[static_cast<int>(level)] = 0;
  this->_latest_seq_num                        = seq_num;
  return frame;
}

uint32_t
QUICPadder::_minimum_quic_packet_size()
{
  SCOPED_MUTEX_LOCK(lock, this->_mutex, this_ethread());
  if (this->_context == NET_VCONNECTION_OUT) {
    // FIXME Only the first packet need to be 1200 bytes at least
    return MINIMUM_INITIAL_PACKET_SIZE;
  } else {
    // FIXME This size should be configurable and should have some randomness
    // This is just for providing protection against packet analysis for protected packets
    return 32 + (this->_rnd() & 0x3f); // 32 to 96
  }
}

void
QUICPadder::set_av_token_len(uint32_t len)
{
  SCOPED_MUTEX_LOCK(lock, this->_mutex, this_ethread());
  this->_av_token_len = len;
}
