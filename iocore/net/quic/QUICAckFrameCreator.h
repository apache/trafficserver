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

#include "ts/ink_hrtime.h"
#include "QUICFrameGenerator.h"
#include "QUICTypes.h"
#include "QUICFrame.h"
#include <vector>
#include <set>

class QUICAckPacketNumbers
{
public:
  void push_back(QUICPacketNumber packet_number);
  QUICPacketNumber front();
  QUICPacketNumber back();
  size_t size();
  void clear();
  void sort();

  QUICPacketNumber largest_ack_number();
  ink_hrtime largest_ack_received_time();

  const QUICPacketNumber &operator[](int i) const { return this->_packet_numbers[i]; }
private:
  QUICPacketNumber _largest_ack_number  = 0;
  ink_hrtime _largest_ack_received_time = 0;

  std::vector<QUICPacketNumber> _packet_numbers;
};

class QUICAckFrameCreator : public QUICFrameGenerator
{
public:
  static constexpr int MAXIMUM_PACKET_COUNT = 256;
  QUICAckFrameCreator(){};

  /*
   * All packet numbers ATS received need to be passed to this method.
   * Returns 0 if updated successfully.
   */
  int update(QUICPacketNumber packet_number, bool protection, bool should_send);

  /*
   * Returns true only if should send ack.
   */
  bool will_generate_frame() override;

  /*
   * Calls create directly.
   */
  QUICFrameUPtr generate_frame(uint16_t connection_credit, uint16_t maximum_frame_size) override;

private:
  /*
   * Returns QUICAckFrame only if ACK frame is able to be sent.
   * Caller must send the ACK frame to the peer if it was returned.
   */
  QUICFrameUPtr _create_frame();

  bool _can_send    = false;
  bool _should_send = false;

  QUICAckPacketNumbers _packet_numbers;
  uint16_t _packet_count = 0;
  std::set<QUICPacketNumber> _unprotected_packets;

  void _sort_packet_numbers();
  QUICFrameUPtr _create_ack_frame();
  uint64_t _calculate_delay();
};
