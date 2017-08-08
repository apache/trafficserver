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
#include "QUICTypes.h"
#include "QUICFrame.h"

class QUICAckFrameCreator
{
public:
  static const int MAXIMUM_PACKET_COUNT = 256;
  QUICAckFrameCreator(){};

  /*
   * All packet numbers ATS received need to be passed to this method.
   * Returns 0 if updated successfully.
   */
  int update(QUICPacketNumber packet_number, bool acknowledgable);

  /*
   * Returns QUICAckFrame only if ACK frame is able to be sent.
   * Caller must send the ACK frame to the peer if it was returned.
   * Usually you should use create_if_needed() instead, but you may want to
   * call this when ATS receives PING frame.
   */
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> create();

  /*
   * Returns QUICAckFrame only if ACK frame need to be sent,
   * because sending an ACK frame per incoming ACK-able packet isn't sufficient.
   * Caller must send the ACK frame to the peer if it was returned.
   */
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> create_if_needed();

private:
  bool _can_send = false;

  QUICPacketNumber _largest_ack_number  = 0;
  QUICPacketNumber _last_ack_number     = 0;
  ink_hrtime _largest_ack_received_time = 0;

  uint16_t _packet_numbers[MAXIMUM_PACKET_COUNT];
  uint16_t _packet_count = 0;

  void _sort_packet_numbers();
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> _create_ack_frame();
};
