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
#include <QUICCongestionController.h>
#include <QUICNewRenoCongestionController.h>

#define QUICCCDebug(fmt, ...)                                                                                               \
  Debug("quic_cc",                                                                                                          \
        "[%s] "                                                                                                             \
        "window:%" PRIu32 " in-flight:%" PRIu32 " ssthresh:%" PRIu32 " extra:%" PRIu32 " " fmt,                             \
        this->_context.connection_info()->cids().data(), this->_congestion_window, this->_bytes_in_flight, this->_ssthresh, \
        this->_extra_packets_count, ##__VA_ARGS__)
#define QUICCCVDebug(fmt, ...)                                                                                              \
  Debug("v_quic_cc",                                                                                                        \
        "[%s] "                                                                                                             \
        "window:%" PRIu32 " in-flight:%" PRIu32 " ssthresh:%" PRIu32 " extra:%" PRIu32 " " fmt,                             \
        this->_context.connection_info()->cids().data(), this->_congestion_window, this->_bytes_in_flight, this->_ssthresh, \
        this->_extra_packets_count, ##__VA_ARGS__)

#define QUICCCError(fmt, ...)                                                                                               \
  Error("quic_cc",                                                                                                          \
        "[%s] "                                                                                                             \
        "window:%" PRIu32 " in-flight:%" PRIu32 " ssthresh:%" PRIu32 " extra:%" PRIu32 " " fmt,                             \
        this->_context.connection_info()->cids().data(), this->_congestion_window, this->_bytes_in_flight, this->_ssthresh, \
        this->_extra_packets_count, ##__VA_ARGS__)

QUICNewRenoCongestionController::QUICNewRenoCongestionController(QUICContext &context)
  : _cc_mutex(new_ProxyMutex()), _context(context)
{
  auto &cc_config                          = context.cc_config();
  this->_k_initial_window                  = cc_config.initial_window();
  this->_k_minimum_window                  = cc_config.minimum_window();
  this->_k_loss_reduction_factor           = cc_config.loss_reduction_factor();
  this->_k_persistent_congestion_threshold = cc_config.persistent_congestion_threshold();

  this->reset();
}

void
QUICNewRenoCongestionController::on_packet_sent(size_t bytes_sent)
{
  SCOPED_MUTEX_LOCK(lock, this->_cc_mutex, this_ethread());
  if (this->_extra_packets_count > 0) {
    --this->_extra_packets_count;
  }

  this->_bytes_in_flight += bytes_sent;
}

bool
QUICNewRenoCongestionController::_in_congestion_recovery(ink_hrtime sent_time) const
{
  return sent_time <= this->_congestion_recovery_start_time;
}

bool
QUICNewRenoCongestionController::_is_app_or_flow_control_limited()
{
  // FIXME : don't known how does app worked here
  return false;
}

void
QUICNewRenoCongestionController::_maybe_send_one_packet()
{
  // TODO Implement _maybe_send_one_packet
}

bool
QUICNewRenoCongestionController::_are_all_packets_lost(const std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> &lost_packets,
                                                       const QUICSentPacketInfoUPtr &largest_lost_packet, ink_hrtime period) const
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

void
QUICNewRenoCongestionController::_congestion_event(ink_hrtime sent_time)
{
  // Start a new congestion event if packet was sent after the
  // start of the previous congestion recovery period.
  if (!this->_in_congestion_recovery(sent_time)) {
    this->_congestion_recovery_start_time = Thread::get_hrtime();
    this->_congestion_window *= this->_k_loss_reduction_factor;
    this->_congestion_window = std::max(this->_congestion_window, this->_k_minimum_window);
    this->_ssthresh          = this->_congestion_window;
    this->_context.trigger(QUICContext::CallbackEvent::CONGESTION_STATE_CHANGED, QUICCongestionController::State::RECOVERY);
    this->_context.trigger(QUICContext::CallbackEvent::METRICS_UPDATE, this->_congestion_window, this->_bytes_in_flight,
                           this->_ssthresh);
    // A packet can be sent to speed up loss recovery.
    this->_maybe_send_one_packet();
  }
}

void
QUICNewRenoCongestionController::process_ecn(const QUICAckFrame &ack_frame, QUICPacketNumberSpace pn_space,
                                             ink_hrtime largest_acked_time_sent)
{
  // If the ECN-CE counter reported by the peer has increased,
  // this could be a new congestion event.
  if (ack_frame.ecn_section()->ecn_ce_count() > this->_ecn_ce_counters[static_cast<int>(pn_space)]) {
    this->_ecn_ce_counters[static_cast<int>(pn_space)] = ack_frame.ecn_section()->ecn_ce_count();
    // Start a new congestion event if the last acknowledged
    // packet was sent after the start of the previous
    // recovery epoch.
    this->_congestion_event(largest_acked_time_sent);
  }
}

bool
QUICNewRenoCongestionController::_in_persistent_congestion(const std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> &lost_packets,
                                                           const QUICSentPacketInfoUPtr &largest_lost_packet)
{
  ink_hrtime congestion_period = this->_context.rtt_provider()->congestion_period(this->_k_persistent_congestion_threshold);
  // Determine if all packets in the time period before the
  // largest newly lost packet, including the edges, are
  // marked lost
  return this->_are_all_packets_lost(lost_packets, largest_lost_packet, congestion_period);
}

void
QUICNewRenoCongestionController::on_packets_acked(const std::vector<QUICSentPacketInfoUPtr> &packets)
{
  SCOPED_MUTEX_LOCK(lock, this->_cc_mutex, this_ethread());

  for (auto &packet : packets) {
    // Remove from bytes_in_flight.
    this->_bytes_in_flight -= packet->sent_bytes;
    if (this->_in_congestion_recovery(packet->time_sent)) {
      // Do not increase congestion window in recovery period.
      continue;
    }
    if (this->_is_app_or_flow_control_limited()) {
      // Do not increase congestion_window if application
      // limited or flow control limited.
      continue;
    }
    if (this->_congestion_window < this->_ssthresh) {
      // Slow start.
      this->_context.trigger(QUICContext::CallbackEvent::CONGESTION_STATE_CHANGED, QUICCongestionController::State::SLOW_START);
      this->_congestion_window += packet->sent_bytes;
      QUICCCVDebug("slow start window changed");
      continue;
    }
    // Congestion avoidance.
    this->_context.trigger(QUICContext::CallbackEvent::CONGESTION_STATE_CHANGED,
                           QUICCongestionController::State::CONGESTION_AVOIDANCE);
    this->_congestion_window += this->_max_datagram_size * static_cast<double>(packet->sent_bytes) / this->_congestion_window;
    QUICCCVDebug("Congestion avoidance window changed");
  }
}

// additional code
// the original one is:
//   OnPacketsLost(lost_packets):
void
QUICNewRenoCongestionController::on_packets_lost(const std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> &lost_packets)
{
  SCOPED_MUTEX_LOCK(lock, this->_cc_mutex, this_ethread());

  // Remove lost packets from bytes_in_flight.
  for (auto &lost_packet : lost_packets) {
    this->_bytes_in_flight -= lost_packet.second->sent_bytes;
  }
  const auto &largest_lost_packet = lost_packets.rbegin()->second;
  this->_congestion_event(largest_lost_packet->time_sent);

  // Collapse congestion window if persistent congestion
  if (this->_in_persistent_congestion(lost_packets, largest_lost_packet)) {
    this->_congestion_window = this->_k_minimum_window;
  }
}

void
QUICNewRenoCongestionController::on_packet_number_space_discarded(size_t bytes_in_flight)
{
  this->_bytes_in_flight -= bytes_in_flight;
}

bool
QUICNewRenoCongestionController::_check_credit() const
{
  if (this->_bytes_in_flight >= this->_congestion_window) {
    QUICCCDebug("Congestion control pending");
  }

  return this->_bytes_in_flight < this->_congestion_window;
}

uint32_t
QUICNewRenoCongestionController::credit() const
{
  if (this->_extra_packets_count) {
    return UINT32_MAX;
  }

  if (this->_check_credit()) {
    return this->_congestion_window - this->_bytes_in_flight;
  } else {
    return 0;
  }
}

uint32_t
QUICNewRenoCongestionController::bytes_in_flight() const
{
  return this->_bytes_in_flight;
}

uint32_t
QUICNewRenoCongestionController::congestion_window() const
{
  return this->_congestion_window;
}

uint32_t
QUICNewRenoCongestionController::current_ssthresh() const
{
  return this->_ssthresh;
}

// [draft-17 recovery] 7.9.3.  Initialization
void
QUICNewRenoCongestionController::reset()
{
  SCOPED_MUTEX_LOCK(lock, this->_cc_mutex, this_ethread());

  this->_congestion_window              = this->_k_initial_window;
  this->_bytes_in_flight                = 0;
  this->_congestion_recovery_start_time = 0;
  this->_ssthresh                       = UINT32_MAX;
  for (int i = 0; i < QUIC_N_PACKET_SPACES; ++i) {
    this->_ecn_ce_counters[i] = 0;
  }
}

void
QUICNewRenoCongestionController::add_extra_credit()
{
  ++this->_extra_packets_count;
}
