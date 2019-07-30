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

#include <tscore/Diags.h>
#include <QUICLossDetector.h>

#define QUICCCDebug(fmt, ...)                                                \
  Debug("quic_cc",                                                           \
        "[%s] "                                                              \
        "window: %" PRIu32 " bytes: %" PRIu32 " ssthresh: %" PRIu32 " " fmt, \
        this->_info->cids().data(), this->_congestion_window, this->_bytes_in_flight, this->_ssthresh, ##__VA_ARGS__)

#define QUICCCError(fmt, ...)                                                \
  Error("quic_cc",                                                           \
        "[%s] "                                                              \
        "window: %" PRIu32 " bytes: %" PRIu32 " ssthresh: %" PRIu32 " " fmt, \
        this->_info->cids().data(), this->_congestion_window, this->_bytes_in_flight, this->_ssthresh, ##__VA_ARGS__)

QUICCongestionController::QUICCongestionController(const QUICRTTProvider &rtt_provider, QUICConnectionInfoProvider *info,
                                                   const QUICCCConfig &cc_config)
  : _cc_mutex(new_ProxyMutex()), _info(info), _rtt_provider(rtt_provider)
{
  this->_k_max_datagram_size               = cc_config.max_datagram_size();
  this->_k_initial_window                  = cc_config.initial_window();
  this->_k_minimum_window                  = cc_config.minimum_window();
  this->_k_loss_reduction_factor           = cc_config.loss_reduction_factor();
  this->_k_persistent_congestion_threshold = cc_config.persistent_congestion_threshold();

  this->reset();
}

void
QUICCongestionController::on_packet_sent(size_t bytes_sent)
{
  SCOPED_MUTEX_LOCK(lock, this->_cc_mutex, this_ethread());
  this->_bytes_in_flight += bytes_sent;
}

bool
QUICCongestionController::_in_recovery(ink_hrtime sent_time)
{
  return sent_time <= this->_recovery_start_time;
}

bool
QUICCongestionController::is_app_limited()
{
  // FIXME : don't known how does app worked here
  return false;
}

void
QUICCongestionController::on_packet_acked(const QUICPacketInfo &acked_packet)
{
  // Remove from bytes_in_flight.
  SCOPED_MUTEX_LOCK(lock, this->_cc_mutex, this_ethread());
  this->_bytes_in_flight -= acked_packet.sent_bytes;
  if (this->_in_recovery(acked_packet.time_sent)) {
    // Do not increase congestion window in recovery period.
    return;
  }

  if (this->is_app_limited()) {
    // Do not increase congestion_window if application
    // limited.
    return;
  }

  if (this->_congestion_window < this->_ssthresh) {
    // Slow start.
    this->_congestion_window += acked_packet.sent_bytes;
    QUICCCDebug("slow start window chaged");
  } else {
    // Congestion avoidance.
    this->_congestion_window += this->_k_max_datagram_size * acked_packet.sent_bytes / this->_congestion_window;
    QUICCCDebug("Congestion avoidance window changed");
  }
}

// addtional code
// the original one is:
//   CongestionEvent(sent_time):
void
QUICCongestionController::_congestion_event(ink_hrtime sent_time)
{
  // Start a new congestion event if the sent time is larger
  // than the start time of the previous recovery epoch.
  if (!this->_in_recovery(sent_time)) {
    this->_recovery_start_time = Thread::get_hrtime();
    this->_congestion_window *= this->_k_loss_reduction_factor;
    this->_congestion_window = std::max(this->_congestion_window, this->_k_minimum_window);
    this->_ssthresh          = this->_congestion_window;
  }
}

// additional code
// the original one is:
//   ProcessECN(ack):
void
QUICCongestionController::process_ecn(const QUICPacketInfo &acked_largest_packet, const QUICAckFrame::EcnSection *ecn_section)
{
  // If the ECN-CE counter reported by the peer has increased,
  // this could be a new congestion event.
  if (ecn_section->ecn_ce_count() > this->_ecn_ce_counter) {
    this->_ecn_ce_counter = ecn_section->ecn_ce_count();
    // Start a new congestion event if the last acknowledged
    // packet was sent after the start of the previous
    // recovery epoch.
    this->_congestion_event(acked_largest_packet.time_sent);
  }
}

bool
QUICCongestionController::_in_persistent_congestion(const std::map<QUICPacketNumber, QUICPacketInfo *> &lost_packets,
                                                    QUICPacketInfo *largest_lost_packet)
{
  ink_hrtime period = this->_rtt_provider.congestion_period(this->_k_persistent_congestion_threshold);
  // Determine if all packets in the window before the
  // newest lost packet, including the edges, are marked
  // lost
  return this->_in_window_lost(lost_packets, largest_lost_packet, period);
}

// additional code
// the original one is:
//   OnPacketsLost(lost_packets):
void
QUICCongestionController::on_packets_lost(const std::map<QUICPacketNumber, QUICPacketInfo *> &lost_packets)
{
  if (lost_packets.empty()) {
    return;
  }

  SCOPED_MUTEX_LOCK(lock, this->_cc_mutex, this_ethread());
  // Remove lost packets from bytes_in_flight.
  for (auto &lost_packet : lost_packets) {
    this->_bytes_in_flight -= lost_packet.second->sent_bytes;
  }
  QUICPacketInfo *largest_lost_packet = lost_packets.rbegin()->second;
  // Start a new recovery epoch if the lost packet is larger
  // than the end of the previous recovery epoch.
  this->_congestion_event(largest_lost_packet->time_sent);

  // Collapse congestion window if persistent congestion
  if (this->_in_persistent_congestion(lost_packets, largest_lost_packet)) {
    this->_congestion_window = this->_k_minimum_window;
  }
}

bool
QUICCongestionController::check_credit() const
{
  if (this->_bytes_in_flight >= this->_congestion_window) {
    QUICCCDebug("Congestion control pending");
  }

  return this->_bytes_in_flight < this->_congestion_window;
}

uint32_t
QUICCongestionController::open_window() const
{
  if (this->check_credit()) {
    return this->_congestion_window - this->_bytes_in_flight;
  } else {
    return 0;
  }
}

uint32_t
QUICCongestionController::bytes_in_flight() const
{
  return this->_bytes_in_flight;
}

uint32_t
QUICCongestionController::congestion_window() const
{
  return this->_congestion_window;
}

uint32_t
QUICCongestionController::current_ssthresh() const
{
  return this->_ssthresh;
}

// [draft-17 recovery] 7.9.3.  Initialization
void
QUICCongestionController::reset()
{
  SCOPED_MUTEX_LOCK(lock, this->_cc_mutex, this_ethread());

  this->_bytes_in_flight     = 0;
  this->_congestion_window   = this->_k_initial_window;
  this->_recovery_start_time = 0;
  this->_ssthresh            = UINT32_MAX;
}

bool
QUICCongestionController::_in_window_lost(const std::map<QUICPacketNumber, QUICPacketInfo *> &lost_packets,
                                          QUICPacketInfo *largest_lost_packet, ink_hrtime period) const
{
  // check whether packets are continuous. return true if all continuous packets are in period
  QUICPacketNumber next_expected = UINT64_MAX;
  for (auto &it : lost_packets) {
    if (it.second->time_sent >= largest_lost_packet->time_sent - period) {
      if (next_expected == UINT64_MAX) {
        next_expected = it.second->packet_number + 1;
        continue;
      }

      if (next_expected != it.second->packet_number) {
        return false;
      }

      next_expected = it.second->packet_number + 1;
    }
  }

  return next_expected == UINT64_MAX ? false : true;
}
