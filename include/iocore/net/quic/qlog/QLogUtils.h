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

#include "QLog.h"
#include "QUICPacket.h"

namespace QLog
{
inline static const char *
PacketTypeToName(QUICPacketType pt)
{
  switch (pt) {
  case QUICPacketType::INITIAL:
    return "initial";
  case QUICPacketType::HANDSHAKE:
    return "handshake";
  case QUICPacketType::ZERO_RTT_PROTECTED:
    return "0rtt";
  case QUICPacketType::PROTECTED:
    return "1rtt";
  case QUICPacketType::RETRY:
    return "retry";
  case QUICPacketType::VERSION_NEGOTIATION:
    return "version_negotiation";
  case QUICPacketType::STATELESS_RESET:
    return "stateless_reset";
  default:
    return "unknown";
  }
}

inline static Recovery::CongestionStateUpdated::State
CongestionStateConvert(QUICCongestionController::State state)
{
  switch (state) {
  case QUICCongestionController::State::APPLICATION_LIMITED:
    return Recovery::CongestionStateUpdated::State::application_limited;
  case QUICCongestionController::State::SLOW_START:
    return Recovery::CongestionStateUpdated::State::slow_start;
  case QUICCongestionController::State::CONGESTION_AVOIDANCE:
    return Recovery::CongestionStateUpdated::State::congestion_avoidance;
  case QUICCongestionController::State::RECOVERY:
    return Recovery::CongestionStateUpdated::State::recovery;
  default:
    return Recovery::CongestionStateUpdated::State::slow_start;
  }
}

inline static PacketHeader
QUICPacketToLogPacket(const QUICPacket &packet)
{
  PacketHeader ph;
  ph.dcid           = packet.destination_cid().hex();
  ph.packet_number  = std::to_string(packet.packet_number());
  ph.packet_size    = packet.size();
  ph.payload_length = packet.payload_length();
  return ph;
}

} // namespace QLog
