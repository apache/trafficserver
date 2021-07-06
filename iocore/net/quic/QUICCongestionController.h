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

class QUICCongestionController
{
public:
  enum class State : uint8_t {
    RECOVERY,
    CONGESTION_AVOIDANCE,
    SLOW_START,
    APPLICATION_LIMITED,
  };

  virtual ~QUICCongestionController() {}
  // Appendix B.  Congestion Control Pseudocode
  virtual void on_packet_sent(size_t bytes_sent)                                                                               = 0;
  virtual void on_packets_acked(const std::vector<QUICSentPacketInfoUPtr> &packets)                                            = 0;
  virtual void process_ecn(const QUICAckFrame &ack, QUICPacketNumberSpace pn_space, ink_hrtime largest_acked_packet_time_sent) = 0;
  virtual void on_packets_lost(const std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> &packets)                              = 0;
  // The function signature is different from the pseudo code because LD takes care of most of the processes
  virtual void on_packet_number_space_discarded(size_t bytes_in_flight) = 0;

  // These are additional and not on the spec
  virtual void add_extra_credit()          = 0;
  virtual void reset()                     = 0;
  virtual uint32_t credit() const          = 0;
  virtual uint32_t bytes_in_flight() const = 0;

  // Debug
  virtual uint32_t congestion_window() const = 0;
  virtual uint32_t current_ssthresh() const  = 0;
};
