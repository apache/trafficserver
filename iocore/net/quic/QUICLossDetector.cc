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

#include "QUICLossDetector.h"

#include "tscore/ink_assert.h"

#include "QUICConfig.h"
#include "QUICEvents.h"
#include "QUICDebugNames.h"
#include "QUICFrameGenerator.h"
#include "QUICPinger.h"
#include "QUICPadder.h"
#include "QUICPacketProtectionKeyInfo.h"

#define QUICLDDebug(fmt, ...) \
  Debug("quic_loss_detector", "[%s] " fmt, this->_context.connection_info()->cids().data(), ##__VA_ARGS__)
#define QUICLDVDebug(fmt, ...) \
  Debug("v_quic_loss_detector", "[%s] " fmt, this->_context.connection_info()->cids().data(), ##__VA_ARGS__)

QUICLossDetector::QUICLossDetector(QUICContext &context, QUICCongestionController *cc, QUICRTTMeasure *rtt_measure,
                                   QUICPinger *pinger, QUICPadder *padder)
  : _rtt_measure(rtt_measure), _pinger(pinger), _padder(padder), _cc(cc), _context(context)
{
  auto &ld_config             = _context.ld_config();
  this->mutex                 = new_ProxyMutex();
  this->_loss_detection_mutex = new_ProxyMutex();

  this->_k_packet_threshold = ld_config.packet_threshold();
  this->_k_time_threshold   = ld_config.time_threshold();

  this->reset();

  SET_HANDLER(&QUICLossDetector::event_handler);
}

QUICLossDetector::~QUICLossDetector()
{
  if (this->_loss_detection_timer) {
    this->_loss_detection_timer->cancel();
    this->_loss_detection_timer = nullptr;
  }

  for (auto i = 0; i < QUIC_N_PACKET_SPACES; i++) {
    this->_sent_packets[i].clear();
  }
}

int
QUICLossDetector::event_handler(int event, Event *edata)
{
  switch (event) {
  case EVENT_INTERVAL: {
    if (this->_loss_detection_alarm_at <= Thread::get_hrtime()) {
      this->_loss_detection_alarm_at = 0;
      this->_on_loss_detection_timeout();
    }
    break;
  }
  case QUIC_EVENT_LD_SHUTDOWN: {
    SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
    QUICLDDebug("Shutdown");

    if (this->_loss_detection_timer) {
      this->_loss_detection_timer->cancel();
      this->_loss_detection_timer = nullptr;
    }
    break;
  }
  default:
    break;
  }
  return EVENT_CONT;
}

std::vector<QUICFrameType>
QUICLossDetector::interests()
{
  return {QUICFrameType::ACK};
}

QUICConnectionErrorUPtr
QUICLossDetector::handle_frame(QUICEncryptionLevel level, const QUICFrame &frame)
{
  QUICConnectionErrorUPtr error = nullptr;

  switch (frame.type()) {
  case QUICFrameType::ACK:
    this->_on_ack_received(static_cast<const QUICAckFrame &>(frame), QUICTypeUtil::pn_space(level));
    break;
  default:
    QUICLDDebug("Unexpected frame type: %02x", static_cast<unsigned int>(frame.type()));
    ink_assert(false);
    break;
  }

  return error;
}

QUICPacketNumber
QUICLossDetector::largest_acked_packet_number(QUICPacketNumberSpace pn_space) const
{
  int index = static_cast<int>(pn_space);
  return this->_largest_acked_packet[index];
}

void
QUICLossDetector::on_packet_sent(QUICSentPacketInfoUPtr packet_info, bool in_flight)
{
  // ADDITIONAL CODE
  if (packet_info->type == QUICPacketType::VERSION_NEGOTIATION) {
    return;
  }
  // END OF ADDITIONAL CODE

  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  QUICPacketNumber packet_number = packet_info->packet_number;
  bool ack_eliciting             = packet_info->ack_eliciting;
  ink_hrtime now                 = packet_info->time_sent;
  size_t sent_bytes              = packet_info->sent_bytes;
  QUICPacketNumberSpace pn_space = packet_info->pn_space;

  QUICLDVDebug("%s packet sent : %" PRIu64 " bytes: %lu ack_eliciting: %d", QUICDebugNames::pn_space(packet_info->pn_space),
               packet_number, sent_bytes, ack_eliciting);

  this->_add_to_sent_packet_list(packet_number, std::move(packet_info));

  if (in_flight) {
    if (ack_eliciting) {
      this->_time_of_last_ack_eliciting_packet[static_cast<int>(pn_space)] = now;
    }
    this->_cc->on_packet_sent(sent_bytes);
    this->_set_loss_detection_timer();
  }
}

void
QUICLossDetector::on_datagram_received()
{
  if (this->_context.connection_info()->is_at_anti_amplification_limit()) {
    this->_set_loss_detection_timer();
  }
}

void
QUICLossDetector::on_packet_number_space_discarded(QUICPacketNumberSpace pn_space)
{
  ink_assert(pn_space != QUICPacketNumberSpace::APPLICATION_DATA);
  size_t bytes_in_flight = 0;
  for (auto it = this->_sent_packets[static_cast<int>(pn_space)].begin();
       it != this->_sent_packets[static_cast<int>(pn_space)].end();) {
    auto ret = this->_remove_from_sent_packet_list(it, pn_space);
    auto &pi = ret.first;
    if (pi->in_flight) {
      bytes_in_flight += pi->sent_bytes;
    }
    it = ret.second;
  }
  this->_cc->on_packet_number_space_discarded(bytes_in_flight);
  // Reset the loss detection and PTO timer
  this->_time_of_last_ack_eliciting_packet[static_cast<int>(pn_space)] = 0;
  this->_loss_time[static_cast<int>(pn_space)]                         = 0;
  this->_rtt_measure->set_pto_count(0);
  this->_set_loss_detection_timer();
  QUICLDDebug("[%s] Packets have been discarded because keys for the space are discarded", QUICDebugNames::pn_space(pn_space));
}

void
QUICLossDetector::reset()
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  // A.4.  Initialization
  if (this->_loss_detection_timer) {
    this->_loss_detection_timer->cancel();
    this->_loss_detection_timer = nullptr;
  }
  this->_rtt_measure->reset();
  for (auto i = 0; i < QUIC_N_PACKET_SPACES; i++) {
    this->_largest_acked_packet[i]              = UINT64_MAX;
    this->_time_of_last_ack_eliciting_packet[i] = 0;
    this->_loss_time[i]                         = 0;
    this->_sent_packets[i].clear();
    //  ADDITIONAL CODE
    this->_num_packets_in_flight[i] = 0;
    // END OF ADDITIONAL CODE
  }

  //  ADDITIONAL CODE
  this->_ack_eliciting_outstanding = 0;
  // END OF ADDITIONAL CODE
}

void
QUICLossDetector::update_ack_delay_exponent(uint8_t ack_delay_exponent)
{
  this->_ack_delay_exponent = ack_delay_exponent;
}

bool
QUICLossDetector::_include_ack_eliciting(const std::vector<QUICSentPacketInfoUPtr> &acked_packets) const
{
  // Find out ack_elicting packet.
  // FIXME: this loop is the same as _on_ack_received's loop it would better
  // to combine it.
  for (const auto &packet : acked_packets) {
    if (packet->ack_eliciting) {
      return true;
    }
  }

  return false;
}

void
QUICLossDetector::_on_ack_received(const QUICAckFrame &ack_frame, QUICPacketNumberSpace pn_space)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  int index = static_cast<int>(pn_space);
  if (this->_largest_acked_packet[index] == UINT64_MAX) {
    this->_largest_acked_packet[index] = ack_frame.largest_acknowledged();
  } else {
    this->_largest_acked_packet[index] = std::max(this->_largest_acked_packet[index], ack_frame.largest_acknowledged());
  }

  auto newly_acked_packets = this->_detect_and_remove_acked_packets(ack_frame, pn_space);
  if (newly_acked_packets.empty()) {
    return;
  }

  // If the largest acknowledged is newly acked and
  //  ack-eliciting, update the RTT.
  const auto &largest_acked = newly_acked_packets[0];
  if (largest_acked->packet_number == ack_frame.largest_acknowledged() && this->_include_ack_eliciting(newly_acked_packets)) {
    ink_hrtime latest_rtt = Thread::get_hrtime() - largest_acked->time_sent;
    // _latest_rtt is nanosecond but ack_frame.ack_delay is microsecond and scaled
    ink_hrtime ack_delay = 0;
    if (pn_space == QUICPacketNumberSpace::APPLICATION_DATA) {
      ack_delay = HRTIME_USECONDS(ack_frame.ack_delay() << this->_ack_delay_exponent);
    }
    this->_rtt_measure->update_rtt(latest_rtt, ack_delay);
  }

  // if (ACK frame contains ECN information):
  //   ProcessECN(ack)
  if (ack_frame.ecn_section() != nullptr) {
    this->_cc->process_ecn(ack_frame, pn_space, largest_acked->time_sent);
  }

  // ADDITIONAL CODE
  // Find all newly acked packets.
  for (const auto &info : newly_acked_packets) {
    this->_on_packet_acked(*info);
  }
  // END OF ADDITIONAL CODE

  auto lost_packets = this->_detect_and_remove_lost_packets(pn_space);
  if (!lost_packets.empty()) {
    this->_cc->on_packets_lost(lost_packets);
  }
  this->_cc->on_packets_acked(newly_acked_packets);

  QUICLDVDebug("[%s] Newly acked:%lu Lost:%lu Unacked packets:%lu (%u ack eliciting)", QUICDebugNames::pn_space(pn_space),
               newly_acked_packets.size(), lost_packets.size(), this->_sent_packets[index].size(),
               this->_ack_eliciting_outstanding.load());

  if (this->_peer_completed_address_validation()) {
    this->_rtt_measure->set_pto_count(0);
  }
  this->_set_loss_detection_timer();
}

void
QUICLossDetector::_on_packet_acked(const QUICSentPacketInfo &acked_packet)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  QUICLDVDebug("[%s] Packet number %" PRIu64 " has been acked", QUICDebugNames::pn_space(acked_packet.pn_space),
               acked_packet.packet_number);

  for (const QUICSentPacketInfo::FrameInfo &frame_info : acked_packet.frames) {
    auto reactor = frame_info.generated_by();
    if (reactor == nullptr) {
      continue;
    }

    reactor->on_frame_acked(frame_info.id());
  }
}

ink_hrtime
QUICLossDetector::_get_loss_time_and_space(QUICPacketNumberSpace &pn_space)
{
  ink_hrtime time = this->_loss_time[static_cast<int>(QUICPacketNumberSpace::INITIAL)];
  pn_space        = QUICPacketNumberSpace::INITIAL;
  for (auto i = 1; i < QUIC_N_PACKET_SPACES; i++) {
    if (time == 0 || this->_loss_time[i] < time) {
      time     = this->_loss_time[i];
      pn_space = static_cast<QUICPacketNumberSpace>(i);
    }
  }

  return time;
}

ink_hrtime
QUICLossDetector::_get_pto_time_and_space(QUICPacketNumberSpace &space)
{
  ink_hrtime duration =
    (this->_rtt_measure->smoothed_rtt() + std::max(4 * this->_rtt_measure->rttvar(), this->_rtt_measure->k_granularity())) *
    (1 << this->_rtt_measure->pto_count());

  // Arm PTO from now when there are no inflight packets.
  if (this->_num_packets_in_flight[static_cast<int>(QUICPacketNumberSpace::INITIAL)].load() == 0 &&
      this->_num_packets_in_flight[static_cast<int>(QUICPacketNumberSpace::HANDSHAKE)].load() == 0 &&
      this->_num_packets_in_flight[static_cast<int>(QUICPacketNumberSpace::APPLICATION_DATA)].load() == 0) {
    ink_assert(!this->_peer_completed_address_validation());
    if (this->_context.connection_info()->has_keys_for(QUICPacketNumberSpace::HANDSHAKE)) {
      space = QUICPacketNumberSpace::HANDSHAKE;
      return Thread::get_hrtime() + duration;
    } else {
      space = QUICPacketNumberSpace::INITIAL;
      return Thread::get_hrtime() + duration;
    }
  }
  ink_hrtime pto_timeout          = INT64_MAX;
  QUICPacketNumberSpace pto_space = QUICPacketNumberSpace::INITIAL;
  for (int i = 0; i < QUIC_N_PACKET_SPACES; ++i) {
    if (this->_num_packets_in_flight[i].load() == 0) {
      continue;
    }
    if (i == static_cast<int>(QUICPacketNumberSpace::APPLICATION_DATA)) {
      // Skip ApplicationData until handshake complete.
      if (!this->_context.connection_info()->is_address_validation_completed()) {
        space = pto_space;
        return pto_timeout;
      }
      // Include max_ack_delay and backoff for ApplicationData.
      // FIXME should be set by transport parameters
      duration += this->_rtt_measure->max_ack_delay() * (1 << this->_rtt_measure->pto_count());
    }

    ink_hrtime t = this->_time_of_last_ack_eliciting_packet[i] + duration;
    if (t < pto_timeout) {
      pto_timeout = t;
      pto_space   = QUICPacketNumberSpace(i);
    }
  }
  space = pto_space;
  return pto_timeout;
}

bool
QUICLossDetector::_peer_completed_address_validation() const
{
  return this->_context.connection_info()->is_address_validation_completed();
}

void
QUICLossDetector::_set_loss_detection_timer()
{
  std::function<void(ink_hrtime)> update_timer = [this](ink_hrtime time) {
    this->_loss_detection_alarm_at = time;
    if (!this->_loss_detection_timer) {
      this->_loss_detection_timer = eventProcessor.schedule_every(this, HRTIME_MSECONDS(25));
    }
  };

  std::function<void(void)> cancel_timer = [this]() {
    this->_loss_detection_alarm_at = 0;
    this->_loss_detection_timer->cancel();
    this->_loss_detection_timer = nullptr;
  };

  QUICPacketNumberSpace pn_space;
  ink_hrtime earliest_loss_time = this->_get_loss_time_and_space(pn_space);
  if (earliest_loss_time != 0) {
    update_timer(earliest_loss_time);
    QUICLDDebug("[%s] time threshold loss detection timer: %" PRId64 "ms", QUICDebugNames::pn_space(pn_space),
                (this->_loss_detection_alarm_at - Thread::get_hrtime()) / HRTIME_MSECOND);
    return;
  }

  if (this->_context.connection_info()->is_at_anti_amplification_limit()) {
    if (this->_loss_detection_timer) {
      cancel_timer();
      QUICLDDebug("Loss detection alarm has been unset because of anti-amplification limit");
      return;
    }
  }

  // Don't arm the alarm if there are no packets with retransmittable data in flight.
  if (this->_ack_eliciting_outstanding == 0 && this->_peer_completed_address_validation()) {
    if (this->_loss_detection_timer) {
      cancel_timer();
      QUICLDDebug("Loss detection alarm has been unset because of no ack eliciting packets outstanding");
    }
    return;
  }

  // PTO Duration
  ink_hrtime timeout = this->_get_pto_time_and_space(pn_space);
  update_timer(timeout);
  QUICLDVDebug("[%s] PTO timeout has been set: %" PRId64 "ms", QUICDebugNames::pn_space(pn_space),
               (timeout - this->_time_of_last_ack_eliciting_packet[static_cast<int>(pn_space)]) / HRTIME_MSECOND);
}

void
QUICLossDetector::_on_loss_detection_timeout()
{
  QUICPacketNumberSpace pn_space;
  ink_hrtime earliest_loss_time = this->_get_loss_time_and_space(pn_space);
  if (earliest_loss_time != 0) {
    // Time threshold loss Detection
    auto lost_packets = this->_detect_and_remove_lost_packets(pn_space);
    ink_assert(!lost_packets.empty());
    this->_cc->on_packets_lost(lost_packets);
    this->_set_loss_detection_timer();
    return;
  }

  if (this->_cc->bytes_in_flight() > 0) {
    // PTO. Send new data if available, else retransmit old data.
    // If neither is available, send a single PING frame.
    QUICPacketNumberSpace pns;
    this->_get_pto_time_and_space(pns);
    this->_send_one_or_two_ack_eliciting_packet(pns);
  } else {
    // This assertion is on draft-29 but not correct
    // Keep it as a comment for now to not add it back
    // ink_assert(this->_is_client_without_one_rtt_key());

    // Client sends an anti-deadlock packet: Initial is padded
    // to earn more anti-amplification credit,
    // a Handshake packet proves address ownership.
    if (this->_context.key_info()->is_encryption_key_available(QUICKeyPhase::HANDSHAKE)) {
      this->_send_one_ack_eliciting_handshake_packet();
    } else {
      this->_send_one_ack_eliciting_padded_initial_packet();
    }
  }

  this->_rtt_measure->set_pto_count(this->_rtt_measure->pto_count() + 1);
  this->_set_loss_detection_timer();

  QUICLDDebug("[%s] Unacked packets %lu (ack_eliciting %u)", QUICDebugNames::pn_space(pn_space),
              this->_sent_packets[static_cast<int>(pn_space)].size(), this->_ack_eliciting_outstanding.load());

  if (is_debug_tag_set("v_quic_loss_detector")) {
    for (auto i = 0; i < 3; i++) {
      for (auto &unacked : this->_sent_packets[i]) {
        QUICLDVDebug("[%s] #%" PRIu64 " ack_eliciting=%i size=%zu %u",
                     QUICDebugNames::pn_space(static_cast<QUICPacketNumberSpace>(i)), unacked.first, unacked.second->ack_eliciting,
                     unacked.second->sent_bytes, this->_ack_eliciting_outstanding.load());
      }
    }
  }
}

std::map<QUICPacketNumber, QUICSentPacketInfoUPtr>
QUICLossDetector::_detect_and_remove_lost_packets(QUICPacketNumberSpace pn_space)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  ink_assert(this->_largest_acked_packet[static_cast<int>(pn_space)] != UINT64_MAX);

  this->_loss_time[static_cast<int>(pn_space)] = 0;
  std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> lost_packets;
  ink_hrtime loss_delay = this->_k_time_threshold * std::max(this->_rtt_measure->latest_rtt(), this->_rtt_measure->smoothed_rtt());

  // Minimum time of kGranularity before packets are deemed lost.
  loss_delay = std::max(loss_delay, this->_rtt_measure->k_granularity());

  // Packets sent before this time are deemed lost.
  ink_hrtime lost_send_time = Thread::get_hrtime() - loss_delay;

  // Packets with packet numbers before this are deemed lost.
  //  QUICPacketNumber lost_pn = this->_largest_acked_packet[static_cast<int>(pn_space)] - this->_k_packet_threshold;

  for (auto it = this->_sent_packets[static_cast<int>(pn_space)].begin();
       it != this->_sent_packets[static_cast<int>(pn_space)].end();) {
    if (it->first > this->_largest_acked_packet[static_cast<int>(pn_space)]) {
      // the spec uses continue but we can break here because the _sent_packets is sorted by packet_number.
      break;
    }

    auto &unacked = it->second;

    // Mark packet as lost, or set time when it should be marked.
    if (unacked->time_sent <= lost_send_time ||
        this->_largest_acked_packet[static_cast<int>(pn_space)] >= unacked->packet_number + this->_k_packet_threshold) {
      if (unacked->time_sent <= lost_send_time) {
        QUICLDDebug("[%s] Lost: time since sent is too long (#%" PRId64 " sent=%" PRId64 ", delay=%" PRId64
                    ", fraction=%lf, lrtt=%" PRId64 ", srtt=%" PRId64 ")",
                    QUICDebugNames::pn_space(pn_space), it->first, unacked->time_sent, lost_send_time, this->_k_time_threshold,
                    this->_rtt_measure->latest_rtt(), this->_rtt_measure->smoothed_rtt());
      } else {
        QUICLDDebug("[%s] Lost: packet delta is too large (#%" PRId64 " largest=%" PRId64 " threshold=%" PRId32 ")",
                    QUICDebugNames::pn_space(pn_space), it->first, this->_largest_acked_packet[static_cast<int>(pn_space)],
                    this->_k_packet_threshold);
      }

      auto ret = this->_remove_from_sent_packet_list(it, pn_space);
      auto pi  = std::move(ret.first);
      it       = ret.second;
      if (pi->in_flight) {
        this->_context.trigger(QUICContext::CallbackEvent::PACKET_LOST, *pi);
        lost_packets.emplace(pi->packet_number, std::move(pi));
      }

    } else {
      if (this->_loss_time[static_cast<int>(pn_space)] == 0) {
        this->_loss_time[static_cast<int>(pn_space)] = unacked->time_sent + loss_delay;
      } else {
        this->_loss_time[static_cast<int>(pn_space)] =
          std::min(this->_loss_time[static_cast<int>(pn_space)], unacked->time_sent + loss_delay);
      }
      ++it;
    }
  }

  // -- ADDITIONAL CODE --
  // Not sure how we can get feedback from congestion control and when we should retransmit the lost packets but we need to send
  // them somewhere.
  // I couldn't find the place so just send them here for now.
  if (!lost_packets.empty()) {
    for (const auto &lost_packet : lost_packets) {
      this->_retransmit_lost_packet(*lost_packet.second);
    }
  }
  // -- END OF ADDITIONAL CODE --

  return lost_packets;
}

// ===== Functions below are used on the spec but there're no pseudo code  =====

void
QUICLossDetector::_send_packet(QUICEncryptionLevel level, bool padded)
{
  if (padded) {
    this->_padder->request(level);
  } else {
    this->_pinger->request(level);
  }
  this->_cc->add_extra_credit();
}

void
QUICLossDetector::_send_one_or_two_ack_eliciting_packet(QUICPacketNumberSpace pn_space)
{
  this->_send_packet(QUICEncryptionLevel::ONE_RTT);
  this->_send_packet(QUICEncryptionLevel::ONE_RTT);
  ink_assert(this->_pinger->count(QUICEncryptionLevel::ONE_RTT) >= 2);
  QUICLDDebug("[%s] send ping frame %" PRIu64, QUICDebugNames::encryption_level(QUICEncryptionLevel::ONE_RTT),
              this->_pinger->count(QUICEncryptionLevel::ONE_RTT));
}

void
QUICLossDetector::_send_one_ack_eliciting_handshake_packet()
{
  this->_send_packet(QUICEncryptionLevel::HANDSHAKE);
  QUICLDDebug("[%s] send handshake packet: ping count=%" PRIu64, QUICDebugNames::encryption_level(QUICEncryptionLevel::HANDSHAKE),
              this->_pinger->count(QUICEncryptionLevel::HANDSHAKE));
}

void
QUICLossDetector::_send_one_ack_eliciting_padded_initial_packet()
{
  this->_send_packet(QUICEncryptionLevel::INITIAL, true);
  QUICLDDebug("[%s] send PADDING frame: ping count=%" PRIu64, QUICDebugNames::encryption_level(QUICEncryptionLevel::INITIAL),
              this->_pinger->count(QUICEncryptionLevel::INITIAL));
}

// ===== Functions below are helper functions =====

void
QUICLossDetector::_retransmit_lost_packet(const QUICSentPacketInfo &packet_info)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  QUICLDDebug("Retransmit %s packet #%" PRIu64, QUICDebugNames::packet_type(packet_info.type), packet_info.packet_number);
  for (const QUICSentPacketInfo::FrameInfo &frame_info : packet_info.frames) {
    auto reactor = frame_info.generated_by();
    if (reactor == nullptr) {
      continue;
    }

    reactor->on_frame_lost(frame_info.id());
  }
}

std::vector<QUICSentPacketInfoUPtr>
QUICLossDetector::_detect_and_remove_acked_packets(const QUICAckFrame &ack_frame, QUICPacketNumberSpace pn_space)
{
  std::vector<QUICSentPacketInfoUPtr> packets;
  std::set<QUICAckFrame::PacketNumberRange> numbers;
  int index = static_cast<int>(pn_space);

  QUICPacketNumber x = ack_frame.largest_acknowledged();
  numbers.insert({x, static_cast<uint64_t>(x) - ack_frame.ack_block_section()->first_ack_block()});
  x -= ack_frame.ack_block_section()->first_ack_block() + 1;
  for (auto &&block : *(ack_frame.ack_block_section())) {
    x -= block.gap() + 1;
    numbers.insert({x, static_cast<uint64_t>(x) - block.length()});
    x -= block.length() + 1;
  }

  for (auto &&range : numbers) {
    for (auto ite = this->_sent_packets[index].begin(); ite != this->_sent_packets[index].end();) {
      if (range.contains(ite->first)) {
        auto ret = this->_remove_from_sent_packet_list(ite, pn_space);
        packets.push_back(std::move(ret.first));
        ite = ret.second;
      } else {
        ++ite;
      }
    }
  }

  return packets;
}

void
QUICLossDetector::_add_to_sent_packet_list(QUICPacketNumber packet_number, QUICSentPacketInfoUPtr packet_info)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  // Add to the list
  int index = static_cast<int>(packet_info->pn_space);
  this->_sent_packets[index].insert(std::pair<QUICPacketNumber, QUICSentPacketInfoUPtr>(packet_number, std::move(packet_info)));

  // Increment counters
  auto ite = this->_sent_packets[index].find(packet_number);
  if (ite != this->_sent_packets[index].end()) {
    if (ite->second->ack_eliciting) {
      ++this->_ack_eliciting_outstanding;
      ink_assert(this->_ack_eliciting_outstanding.load() > 0);
    }
    if (ite->second->in_flight) {
      ++this->_num_packets_in_flight[index];
    }
  }
}

std::pair<QUICSentPacketInfoUPtr, std::map<QUICPacketNumber, QUICSentPacketInfoUPtr>::iterator>
QUICLossDetector::_remove_from_sent_packet_list(std::map<QUICPacketNumber, QUICSentPacketInfoUPtr>::iterator it,
                                                QUICPacketNumberSpace pn_space)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  this->_decrement_counters(it, pn_space);
  auto pi = std::move(it->second);
  return {std::move(pi), this->_sent_packets[static_cast<int>(pn_space)].erase(it)};
}

void
QUICLossDetector::_decrement_counters(std::map<QUICPacketNumber, QUICSentPacketInfoUPtr>::iterator it,
                                      QUICPacketNumberSpace pn_space)
{
  if (it != this->_sent_packets[static_cast<int>(pn_space)].end()) {
    if (it->second->ack_eliciting) {
      ink_assert(this->_ack_eliciting_outstanding.load() > 0);
      --this->_ack_eliciting_outstanding;
    }
    --this->_num_packets_in_flight[static_cast<int>(pn_space)];
  }
}

bool
QUICLossDetector::_is_client_without_one_rtt_key() const
{
  return this->_context.connection_info()->direction() == NET_VCONNECTION_OUT &&
         !((this->_context.key_info()->is_encryption_key_available(QUICKeyPhase::PHASE_1) &&
            this->_context.key_info()->is_decryption_key_available(QUICKeyPhase::PHASE_1)) ||
           (this->_context.key_info()->is_encryption_key_available(QUICKeyPhase::PHASE_0) &&
            this->_context.key_info()->is_decryption_key_available(QUICKeyPhase::PHASE_0)));
}

//
// QUICRTTMeasure
//
QUICRTTMeasure::QUICRTTMeasure(const QUICLDConfig &ld_config)
  : _k_granularity(ld_config.granularity()), _k_initial_rtt(ld_config.initial_rtt())
{
}

void
QUICRTTMeasure::init(const QUICLDConfig &ld_config)
{
  this->_k_granularity = ld_config.granularity();
  this->_k_initial_rtt = ld_config.initial_rtt();
}

ink_hrtime
QUICRTTMeasure::smoothed_rtt() const
{
  return this->_smoothed_rtt;
}

void
QUICRTTMeasure::update_rtt(ink_hrtime latest_rtt, ink_hrtime ack_delay)
{
  this->_latest_rtt = latest_rtt;

  if (this->_is_first_sample) {
    this->_min_rtt         = this->_latest_rtt;
    this->_smoothed_rtt    = this->_latest_rtt;
    this->_rttvar          = this->_latest_rtt / 2;
    this->_is_first_sample = false;
    return;
  }

  // min_rtt ignores ack delay.
  this->_min_rtt = std::min(this->_min_rtt, latest_rtt);
  // Limit ack_delay by max_ack_delay
  ack_delay = std::min(ack_delay, this->_max_ack_delay);
  // Adjust for ack delay if it's plausible.
  auto adjusted_rtt = this->_latest_rtt;
  if (adjusted_rtt > this->_min_rtt + ack_delay) {
    adjusted_rtt -= ack_delay;
  }

  // Based on {{RFC6298}}.
  this->_rttvar       = 3.0 / 4.0 * this->_rttvar + 1.0 / 4.0 * ABS(this->_smoothed_rtt - adjusted_rtt);
  this->_smoothed_rtt = 7.0 / 8.0 * this->_smoothed_rtt + 1.0 / 8.0 * adjusted_rtt;
}

ink_hrtime
QUICRTTMeasure::current_pto_period() const
{
  // PTO timeout
  ink_hrtime alarm_duration;
  alarm_duration = this->_smoothed_rtt + 4 * this->_rttvar + this->_max_ack_delay;
  alarm_duration = std::max(alarm_duration, this->_k_granularity);
  alarm_duration = alarm_duration * (1 << this->_pto_count);
  return alarm_duration;
}

ink_hrtime
QUICRTTMeasure::congestion_period(uint32_t threshold) const
{
  ink_hrtime pto = this->_smoothed_rtt + std::max(this->_rttvar * 4, this->_k_granularity);
  return pto * threshold;
}

void
QUICRTTMeasure::set_pto_count(uint32_t count)
{
  this->_pto_count = count;
}

void
QUICRTTMeasure::set_max_ack_delay(ink_hrtime max_ack_delay)
{
  this->_max_ack_delay = max_ack_delay;
}

ink_hrtime
QUICRTTMeasure::rttvar() const
{
  return this->_rttvar;
}

ink_hrtime
QUICRTTMeasure::latest_rtt() const
{
  return this->_latest_rtt;
}

uint32_t
QUICRTTMeasure::pto_count() const
{
  return this->_pto_count;
}

ink_hrtime
QUICRTTMeasure::max_ack_delay() const
{
  return this->_max_ack_delay;
}

ink_hrtime
QUICRTTMeasure::k_granularity() const
{
  return this->_k_granularity;
}

void
QUICRTTMeasure::reset()
{
  // A.4.  Initialization
  this->_pto_count    = 0;
  this->_latest_rtt   = 0;
  this->_smoothed_rtt = this->_k_initial_rtt;
  this->_rttvar       = this->_k_initial_rtt / 2.0;
  this->_min_rtt      = 0;
}
