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

#include "QUICFrame.h"

struct QUICPacketInfo {
  // 6.3.1.  Sent Packet Fields
  QUICPacketNumber packet_number;
  ink_hrtime time_sent;
  bool ack_eliciting;
  bool is_crypto_packet;
  bool in_flight;
  size_t sent_bytes;

  // addition
  QUICPacketType type;
  std::vector<QUICFrameInfo> frames;
  QUICPacketNumberSpace pn_space;
  // end
};

class QUICCongestionController
{
public:
  virtual ~QUICCongestionController() {}
  virtual void on_packet_sent(size_t bytes_sent)                                                                    = 0;
  virtual void on_packet_acked(const QUICPacketInfo &acked_packet)                                                  = 0;
  virtual void process_ecn(const QUICPacketInfo &acked_largest_packet, const QUICAckFrame::EcnSection *ecn_section) = 0;
  virtual void on_packets_lost(const std::map<QUICPacketNumber, QUICPacketInfo *> &packets)                         = 0;
  virtual void add_extra_credit()                                                                                   = 0;
  virtual void reset()                                                                                              = 0;
  virtual uint32_t credit() const                                                                                   = 0;
};
