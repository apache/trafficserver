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

#pragma once

#include "tscore/ink_hrtime.h"
#include "QUICFrameGenerator.h"
#include "QUICTypes.h"
#include "QUICFrame.h"
#include <list>
#include <set>

class QUICAckPacketNumbers
{
public:
  void push_back(QUICPacketNumber packet_number);
  size_t size();
  void clear();
  void sort();
  void forget(QUICPacketNumber largest_acknowledged);

  const std::list<QUICPacketNumber> list() const;

  QUICPacketNumber largest_ack_number();
  ink_hrtime largest_ack_received_time();

private:
  QUICPacketNumber _largest_ack_number  = 0;
  ink_hrtime _largest_ack_received_time = 0;

  std::list<QUICPacketNumber> _packet_numbers;
};

class QUICAckFrameCreator : public QUICFrameGenerator
{
public:
  static constexpr int MAXIMUM_PACKET_COUNT = 256;
  QUICAckFrameCreator(){};

  void set_ack_delay_exponent(uint8_t ack_delay_exponent);

  /*
   * All packet numbers ATS received need to be passed to this method.
   * Returns 0 if updated successfully.
   */
  int update(QUICEncryptionLevel level, QUICPacketNumber packet_number, bool should_send);

  /*
   * Returns true only if should send ack.
   */
  bool will_generate_frame(QUICEncryptionLevel level) override;

  /*
   * Calls create directly.
   */
  QUICFrameUPtr generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size) override;

private:
  struct AckFrameInfomation {
    QUICPacketNumber largest_acknowledged = 0;
  };

  virtual void _on_frame_acked(QUICFrameInformation info) override;

  /*
   * Returns QUICAckFrame only if ACK frame is able to be sent.
   * Caller must send the ACK frame to the peer if it was returned.
   */
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> _create_ack_frame(QUICEncryptionLevel level);
  uint64_t _calculate_delay(QUICEncryptionLevel level);
  std::vector<QUICEncryptionLevel>
  _encryption_level_filter() override
  {
    return {
      QUICEncryptionLevel::INITIAL,
      QUICEncryptionLevel::ZERO_RTT,
      QUICEncryptionLevel::HANDSHAKE,
      QUICEncryptionLevel::ONE_RTT,
    };
  }

  bool _can_send[4]    = {false};
  bool _should_send[4] = {false};

  // Initial, 0/1-RTT, and Handshake
  QUICAckPacketNumbers _packet_numbers[3];

  uint8_t _ack_delay_exponent = 0;
};
