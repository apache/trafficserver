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

#include "QUICLossDetector.h"
#include "QUICEvents.h"
#include "Mock.h"

TEST_CASE("QUICLossDetector_Loss_in_Handshake", "[quic]")
{
  MockQUICPacketTransmitter *tx = new MockQUICPacketTransmitter();
  QUICLossDetector detector(tx);

  // Check initial state
  CHECK(tx->_retransmit_count == 0);

  // Send SERVER_CLEARTEXT (Handshake message)
  uint8_t raw[4]         = {0};
  ats_unique_buf payload = ats_unique_malloc(sizeof(raw));
  memcpy(payload.get(), raw, sizeof(raw));

  QUICPacketUPtr packet = QUICPacketUPtr(new QUICPacket(QUICPacketType::SERVER_CLEARTEXT, 0xffddbb9977553311ULL, 0x00000001, 0,
                                                        0x00112233, std::move(payload), sizeof(raw), true),
                                         [](QUICPacket *p) { delete p; });
  detector.on_packet_sent(std::move(packet));
  ink_hrtime_sleep(HRTIME_MSECONDS(1000));
  CHECK(tx->_retransmit_count > 0);

  // Receive ACK
  std::shared_ptr<QUICAckFrame> frame = std::make_shared<QUICAckFrame>(0x01, 20, 0);
  frame->ack_block_section()->add_ack_block({0, 1ULL});
  detector.handle_frame(frame);
  ink_hrtime_sleep(HRTIME_MSECONDS(1500));
  int retransmit_count = tx->_retransmit_count;
  ink_hrtime_sleep(HRTIME_MSECONDS(1500));
  CHECK(tx->_retransmit_count == retransmit_count);
}
