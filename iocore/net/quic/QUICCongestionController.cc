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

#include <ts/Diags.h>
#include <QUICLossDetector.h>

// 4.7.1.  Constants of interest
constexpr static uint16_t DEFAULT_MSS         = 1460;
constexpr static uint32_t INITIAL_WINDOW      = 10 * DEFAULT_MSS;
constexpr static uint32_t MINIMUM_WINDOW      = 2 * DEFAULT_MSS;
constexpr static double LOSS_REDUCTION_FACTOR = 0.5;

QUICCongestionController::QUICCongestionController()
{
  this->_congestion_window = INITIAL_WINDOW;
}

void
QUICCongestionController::on_packet_sent(size_t bytes_sent)
{
  this->_bytes_in_flight += bytes_sent;
}

void
QUICCongestionController::on_packet_acked(QUICPacketNumber acked_packet_number, size_t acked_packet_size)
{
  // Remove from bytes_in_flight.
  this->_bytes_in_flight -= acked_packet_size;
  if (acked_packet_number < this->_end_of_recovery) {
    // Do not increase congestion window in recovery period.
    return;
  }
  if (this->_congestion_window < this->_ssthresh) {
    // Slow start.
    this->_congestion_window += acked_packet_size;
  } else {
    // Congestion avoidance.
    this->_congestion_window += DEFAULT_MSS * acked_packet_size / this->_congestion_window;
  }
}

void
QUICCongestionController::on_packets_lost(std::map<QUICPacketNumber, PacketInfo &> lost_packets)
{
  // Remove lost packets from bytes_in_flight.
  for (auto &lost_packet : lost_packets) {
    this->_bytes_in_flight -= lost_packet.second.bytes;
  }
  QUICPacketNumber largest_lost_packet = lost_packets.rbegin()->first;
  // Start a new recovery epoch if the lost packet is larger
  // than the end of the previous recovery epoch.
  if (this->_end_of_recovery < largest_lost_packet) {
    this->_end_of_recovery = largest_lost_packet;
    this->_congestion_window *= LOSS_REDUCTION_FACTOR;
    this->_congestion_window = std::max(this->_congestion_window, MINIMUM_WINDOW);
    this->_ssthresh          = this->_congestion_window;
  }
}

void
QUICCongestionController::on_retransmission_timeout_verified()
{
  this->_congestion_window = MINIMUM_WINDOW;
}
