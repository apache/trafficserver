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

#define QUICLDDebug(fmt, ...) Debug("quic_loss_detector", "[%s] " fmt, this->_info->cids().data(), ##__VA_ARGS__)
#define QUICLDVDebug(fmt, ...) Debug("v_quic_loss_detector", "[%s] " fmt, this->_info->cids().data(), ##__VA_ARGS__)

QUICLossDetector::QUICLossDetector(QUICConnectionInfoProvider *info, QUICCongestionController *cc, QUICRTTMeasure *rtt_measure,
                                   const QUICLDConfig &ld_config)
  : _info(info), _rtt_measure(rtt_measure), _cc(cc)
{
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

  for (auto i = 0; i < kPacketNumberSpace; i++) {
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
QUICLossDetector::largest_acked_packet_number(QUICPacketNumberSpace pn_space)
{
  int index = static_cast<int>(pn_space);
  return this->_largest_acked_packet[index];
}

void
QUICLossDetector::on_packet_sent(QUICPacketInfoUPtr packet_info, bool in_flight)
{
  if (packet_info->type == QUICPacketType::VERSION_NEGOTIATION) {
    return;
  }

  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  QUICPacketNumber packet_number = packet_info->packet_number;
  bool ack_eliciting             = packet_info->ack_eliciting;
  bool is_crypto_packet          = packet_info->is_crypto_packet;
  ink_hrtime now                 = packet_info->time_sent;
  size_t sent_bytes              = packet_info->sent_bytes;

  this->_add_to_sent_packet_list(packet_number, std::move(packet_info));

  if (in_flight) {
    if (is_crypto_packet) {
      this->_time_of_last_sent_crypto_packet = now;
    }

    if (ack_eliciting) {
      this->_time_of_last_sent_ack_eliciting_packet = now;
    }
    this->_cc->on_packet_sent(sent_bytes);
    this->_set_loss_detection_timer();
  }
}

void
QUICLossDetector::reset()
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  if (this->_loss_detection_timer) {
    this->_loss_detection_timer->cancel();
    this->_loss_detection_timer = nullptr;
  }

  this->_ack_eliciting_outstanding = 0;
  this->_crypto_outstanding        = 0;

  // [draft-17 recovery] 6.4.3.  Initialization
  this->_time_of_last_sent_ack_eliciting_packet = 0;
  this->_time_of_last_sent_crypto_packet        = 0;
  for (auto i = 0; i < kPacketNumberSpace; i++) {
    this->_largest_acked_packet[i] = 0;
    this->_loss_time[i]            = 0;
    this->_sent_packets[i].clear();
  }

  this->_rtt_measure->reset();
}

void
QUICLossDetector::update_ack_delay_exponent(uint8_t ack_delay_exponent)
{
  this->_ack_delay_exponent = ack_delay_exponent;
}

void
QUICLossDetector::_on_ack_received(const QUICAckFrame &ack_frame, QUICPacketNumberSpace pn_space)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  int index                          = static_cast<int>(pn_space);
  this->_largest_acked_packet[index] = std::max(this->_largest_acked_packet[index], ack_frame.largest_acknowledged());
  // If the largest acknowledged is newly acked and
  //  ack-eliciting, update the RTT.
  auto pi = this->_sent_packets[index].find(ack_frame.largest_acknowledged());
  if (pi != this->_sent_packets[index].end() && pi->second->ack_eliciting) {
    ink_hrtime latest_rtt = Thread::get_hrtime() - pi->second->time_sent;
    // _latest_rtt is nanosecond but ack_frame.ack_delay is microsecond and scaled
    ink_hrtime delay = HRTIME_USECONDS(ack_frame.ack_delay() << this->_ack_delay_exponent);
    this->_rtt_measure->update_rtt(latest_rtt, delay);
  }

  QUICLDVDebug("[%s] Unacked packets %lu (retransmittable %u, includes %u handshake packets)", QUICDebugNames::pn_space(pn_space),
               this->_sent_packets[index].size(), this->_ack_eliciting_outstanding.load(), this->_crypto_outstanding.load());

  // if (ACK frame contains ECN information):
  //   ProcessECN(ack)
  if (ack_frame.ecn_section() != nullptr && pi != this->_sent_packets[index].end()) {
    this->_cc->process_ecn(*pi->second, ack_frame.ecn_section());
  }

  // Find all newly acked packets.
  bool newly_acked_packets = false;
  for (auto &&range : this->_determine_newly_acked_packets(ack_frame)) {
    for (auto ite = this->_sent_packets[index].begin(); ite != this->_sent_packets[index].end(); /* no increment here*/) {
      auto tmp_ite = ite;
      tmp_ite++;
      if (range.contains(ite->first)) {
        newly_acked_packets = true;
        this->_on_packet_acked(*(ite->second));
      }
      ite = tmp_ite;
    }
  }

  if (!newly_acked_packets) {
    return;
  }

  QUICLDVDebug("[%s] Unacked packets %lu (retransmittable %u, includes %u handshake packets)", QUICDebugNames::pn_space(pn_space),
               this->_sent_packets[index].size(), this->_ack_eliciting_outstanding.load(), this->_crypto_outstanding.load());

  this->_detect_lost_packets(pn_space);

  this->_rtt_measure->set_crypto_count(0);
  this->_rtt_measure->set_pto_count(0);

  QUICLDDebug("[%s] Unacked packets %lu (retransmittable %u, includes %u handshake packets)", QUICDebugNames::pn_space(pn_space),
              this->_sent_packets[index].size(), this->_ack_eliciting_outstanding.load(), this->_crypto_outstanding.load());

  this->_set_loss_detection_timer();
}

void
QUICLossDetector::_on_packet_acked(const QUICPacketInfo &acked_packet)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  // QUICLDDebug("Packet number %" PRIu64 " has been acked", acked_packet_number);

  if (acked_packet.ack_eliciting) {
    this->_cc->on_packet_acked(acked_packet);
  }

  for (const QUICFrameInfo &frame_info : acked_packet.frames) {
    auto reactor = frame_info.generated_by();
    if (reactor == nullptr) {
      continue;
    }

    reactor->on_frame_acked(frame_info.id());
  }

  this->_remove_from_sent_packet_list(acked_packet.packet_number, acked_packet.pn_space);
}

ink_hrtime
QUICLossDetector::_get_earliest_loss_time(QUICPacketNumberSpace &pn_space)
{
  ink_hrtime time = this->_loss_time[static_cast<int>(QUICPacketNumberSpace::Initial)];
  pn_space        = QUICPacketNumberSpace::Initial;
  for (auto i = 1; i < kPacketNumberSpace; i++) {
    if (this->_loss_time[i] != 0 && (time != 0 || this->_loss_time[i] < time)) {
      time     = this->_loss_time[i];
      pn_space = static_cast<QUICPacketNumberSpace>(i);
    }
  }

  return time;
}

void
QUICLossDetector::_set_loss_detection_timer()
{
  // Don't arm the alarm if there are no packets with retransmittable data in flight.
  // -- MODIFIED CODE --
  // In psuedocode, `bytes_in_flight` is used, but we're tracking "retransmittable data in flight" by `_ack_eliciting_outstanding`
  if (this->_ack_eliciting_outstanding == 0) {
    if (this->_loss_detection_timer) {
      this->_loss_detection_alarm_at = 0;
      this->_loss_detection_timer->cancel();
      this->_loss_detection_timer = nullptr;
      QUICLDDebug("Loss detection alarm has been unset");
    }

    return;
  }
  // -- END OF MODIFIED CODE --

  QUICPacketNumberSpace pn_space;
  ink_hrtime loss_time = this->_get_earliest_loss_time(pn_space);
  if (loss_time != 0) {
    // Time threshold loss detection.
    this->_loss_detection_alarm_at = loss_time;
    QUICLDDebug("[%s] time threshold loss detection timer: %" PRId64, QUICDebugNames::pn_space(pn_space),
                this->_loss_detection_alarm_at);
  } else if (this->_crypto_outstanding) {
    // Handshake retransmission alarm.
    this->_loss_detection_alarm_at = this->_time_of_last_sent_crypto_packet + this->_rtt_measure->handshake_retransmit_timeout();
    QUICLDDebug("%s crypto packet alarm will be set: %" PRId64, QUICDebugNames::pn_space(pn_space), this->_loss_detection_alarm_at);
    // -- ADDITIONAL CODE --
    // In psudocode returning here, but we don't do for scheduling _loss_detection_alarm event.
    // -- END OF ADDITIONAL CODE --
  } else {
    // PTO Duration
    this->_loss_detection_alarm_at = this->_time_of_last_sent_ack_eliciting_packet + this->_rtt_measure->current_pto_period();
    QUICLDDebug("[%s] PTO timeout will be set: %" PRId64, QUICDebugNames::pn_space(pn_space), this->_loss_detection_alarm_at);
  }

  if (!this->_loss_detection_timer) {
    this->_loss_detection_timer = eventProcessor.schedule_every(this, HRTIME_MSECONDS(25));
  }
}

void
QUICLossDetector::_on_loss_detection_timeout()
{
  QUICPacketNumberSpace pn_space;
  ink_hrtime loss_time = this->_get_earliest_loss_time(pn_space);
  if (loss_time != 0) {
    // Time threshold loss Detection
    this->_detect_lost_packets(pn_space);
  } else if (this->_crypto_outstanding) {
    // Handshake retransmission alarm.
    this->_retransmit_all_unacked_crypto_data();
    this->_rtt_measure->set_crypto_count(this->_rtt_measure->crypto_count() + 1);
  } else {
    QUICLDVDebug("PTO");
    this->_send_two_packets();
    this->_rtt_measure->set_pto_count(this->_rtt_measure->pto_count() + 1);
  }

  QUICLDDebug("[%s] Unacked packets %lu (retransmittable %u, includes %u handshake packets)", QUICDebugNames::pn_space(pn_space),
              this->_sent_packets[static_cast<int>(pn_space)].size(), this->_ack_eliciting_outstanding.load(),
              this->_crypto_outstanding.load());

  if (is_debug_tag_set("v_quic_loss_detector")) {
    for (auto i = 0; i < 3; i++) {
      for (auto &unacked : this->_sent_packets[i]) {
        QUICLDVDebug("[%s] #%" PRIu64 " is_crypto=%i ack_eliciting=%i size=%zu %u %u",
                     QUICDebugNames::pn_space(static_cast<QUICPacketNumberSpace>(i)), unacked.first,
                     unacked.second->is_crypto_packet, unacked.second->ack_eliciting, unacked.second->sent_bytes,
                     this->_ack_eliciting_outstanding.load(), this->_crypto_outstanding.load());
      }
    }
  }

  this->_set_loss_detection_timer();
}

void
QUICLossDetector::_detect_lost_packets(QUICPacketNumberSpace pn_space)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  this->_loss_time[static_cast<int>(pn_space)] = 0;
  ink_hrtime loss_delay = this->_k_time_threshold * std::max(this->_rtt_measure->latest_rtt(), this->_rtt_measure->smoothed_rtt());
  std::map<QUICPacketNumber, QUICPacketInfo *> lost_packets;

  // Packets sent before this time are deemed lost.
  ink_hrtime lost_send_time = Thread::get_hrtime() - loss_delay;

  // Packets with packet numbers before this are deemed lost.
  QUICPacketNumber lost_pn = this->_largest_acked_packet[static_cast<int>(pn_space)] - this->_k_packet_threshold;

  for (auto it = this->_sent_packets[static_cast<int>(pn_space)].begin();
       it != this->_sent_packets[static_cast<int>(pn_space)].end(); ++it) {
    if (it->first > this->_largest_acked_packet[static_cast<int>(pn_space)]) {
      // the spec uses continue but we can break here because the _sent_packets is sorted by packet_number.
      break;
    }

    auto &unacked = it->second;

    // Mark packet as lost, or set time when it should be marked.
    if (unacked->time_sent < lost_send_time || unacked->packet_number < lost_pn) {
      if (unacked->time_sent < lost_send_time) {
        QUICLDDebug("[%s] Lost: time since sent is too long (#%" PRId64 " sent=%" PRId64 ", delay=%" PRId64
                    ", fraction=%lf, lrtt=%" PRId64 ", srtt=%" PRId64 ")",
                    QUICDebugNames::pn_space(pn_space), it->first, unacked->time_sent, lost_send_time, this->_k_time_threshold,
                    this->_rtt_measure->latest_rtt(), this->_rtt_measure->smoothed_rtt());
      } else {
        QUICLDDebug("[%s] Lost: packet delta is too large (#%" PRId64 " largest=%" PRId64 " threshold=%" PRId32 ")",
                    QUICDebugNames::pn_space(pn_space), it->first, this->_largest_acked_packet[static_cast<int>(pn_space)],
                    this->_k_packet_threshold);
      }

      if (unacked->in_flight) {
        lost_packets.insert({it->first, it->second.get()});
      } else if (this->_loss_time[static_cast<int>(pn_space)] == 0) {
        this->_loss_time[static_cast<int>(pn_space)] = unacked->time_sent + loss_delay;
      } else {
        this->_loss_time[static_cast<int>(pn_space)] =
          std::min(this->_loss_time[static_cast<int>(pn_space)], unacked->time_sent + loss_delay);
      }
    }
  }

  // Inform the congestion controller of lost packets and
  // lets it decide whether to retransmit immediately.
  if (!lost_packets.empty()) {
    this->_cc->on_packets_lost(lost_packets);
    for (auto lost_packet : lost_packets) {
      // -- ADDITIONAL CODE --
      // Not sure how we can get feedback from congestion control and when we should retransmit the lost packets but we need to send
      // them somewhere.
      // I couldn't find the place so just send them here for now.
      this->_retransmit_lost_packet(*lost_packet.second);
      // -- END OF ADDITIONAL CODE --
      // -- ADDITIONAL CODE --
      this->_remove_from_sent_packet_list(lost_packet.first, pn_space);
      // -- END OF ADDITIONAL CODE --
    }
  }
}

// ===== Functions below are used on the spec but there're no pseudo code  =====

void
QUICLossDetector::_retransmit_all_unacked_crypto_data()
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  for (auto i = 0; i < kPacketNumberSpace; i++) {
    std::set<QUICPacketNumber> retransmitted_crypto_packets;
    std::map<QUICPacketNumber, QUICPacketInfo *> lost_packets;
    for (auto &info : this->_sent_packets[i]) {
      if (info.second->is_crypto_packet) {
        retransmitted_crypto_packets.insert(info.first);
        this->_retransmit_lost_packet(*info.second);
        lost_packets.insert({info.first, info.second.get()});
      }
    }

    this->_cc->on_packets_lost(lost_packets);
    for (auto packet_number : retransmitted_crypto_packets) {
      this->_remove_from_sent_packet_list(packet_number, static_cast<QUICPacketNumberSpace>(i));
    }
  }
}

void
QUICLossDetector::_send_two_packets()
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  // TODO sent ping
}

// ===== Functions below are helper functions =====

void
QUICLossDetector::_retransmit_lost_packet(QUICPacketInfo &packet_info)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  QUICLDDebug("Retransmit %s packet #%" PRIu64, QUICDebugNames::packet_type(packet_info.type), packet_info.packet_number);
  for (QUICFrameInfo &frame_info : packet_info.frames) {
    auto reactor = frame_info.generated_by();
    if (reactor == nullptr) {
      continue;
    }

    reactor->on_frame_lost(frame_info.id());
  }
}

std::set<QUICAckFrame::PacketNumberRange>
QUICLossDetector::_determine_newly_acked_packets(const QUICAckFrame &ack_frame)
{
  std::set<QUICAckFrame::PacketNumberRange> numbers;
  QUICPacketNumber x = ack_frame.largest_acknowledged();
  numbers.insert({x, static_cast<uint64_t>(x) - ack_frame.ack_block_section()->first_ack_block()});
  x -= ack_frame.ack_block_section()->first_ack_block() + 1;
  for (auto &&block : *(ack_frame.ack_block_section())) {
    x -= block.gap() + 1;
    numbers.insert({x, static_cast<uint64_t>(x) - block.length()});
    x -= block.length() + 1;
  }

  return numbers;
}

void
QUICLossDetector::_add_to_sent_packet_list(QUICPacketNumber packet_number, QUICPacketInfoUPtr packet_info)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  // Add to the list
  int index = static_cast<int>(packet_info->pn_space);
  this->_sent_packets[index].insert(std::pair<QUICPacketNumber, QUICPacketInfoUPtr>(packet_number, std::move(packet_info)));

  // Increment counters
  auto ite = this->_sent_packets[index].find(packet_number);
  if (ite != this->_sent_packets[index].end()) {
    if (ite->second->is_crypto_packet) {
      ++this->_crypto_outstanding;
      ink_assert(this->_crypto_outstanding.load() > 0);
    }
    if (ite->second->ack_eliciting) {
      ++this->_ack_eliciting_outstanding;
      ink_assert(this->_ack_eliciting_outstanding.load() > 0);
    }
  }
}

void
QUICLossDetector::_remove_from_sent_packet_list(QUICPacketNumber packet_number, QUICPacketNumberSpace pn_space)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  auto ite = this->_sent_packets[static_cast<int>(pn_space)].find(packet_number);
  this->_decrement_outstanding_counters(ite, pn_space);
  this->_sent_packets[static_cast<int>(pn_space)].erase(packet_number);
}

std::map<QUICPacketNumber, QUICPacketInfoUPtr>::iterator
QUICLossDetector::_remove_from_sent_packet_list(std::map<QUICPacketNumber, QUICPacketInfoUPtr>::iterator it,
                                                QUICPacketNumberSpace pn_space)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  this->_decrement_outstanding_counters(it, pn_space);
  return this->_sent_packets[static_cast<int>(pn_space)].erase(it);
}

void
QUICLossDetector::_decrement_outstanding_counters(std::map<QUICPacketNumber, QUICPacketInfoUPtr>::iterator it,
                                                  QUICPacketNumberSpace pn_space)
{
  if (it != this->_sent_packets[static_cast<int>(pn_space)].end()) {
    // Decrement counters
    if (it->second->is_crypto_packet) {
      ink_assert(this->_crypto_outstanding.load() > 0);
      --this->_crypto_outstanding;
    }
    if (it->second->ack_eliciting) {
      ink_assert(this->_ack_eliciting_outstanding.load() > 0);
      --this->_ack_eliciting_outstanding;
    }
  }
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
  // additional code
  this->_latest_rtt = latest_rtt;

  // min_rtt ignores ack delay.
  this->_min_rtt = std::min(this->_min_rtt, latest_rtt);
  // Limit ack_delay by max_ack_delay
  ack_delay = std::min(ack_delay, this->_max_ack_delay);
  // Adjust for ack delay if it's plausible.
  if (this->_latest_rtt - this->_min_rtt > ack_delay) {
    this->_latest_rtt -= ack_delay;

    // the newest spec has removed the max_ack_delay assignment. but we need to assign it in somewhere
    // this code is from draft-19
    this->_max_ack_delay = std::max(ack_delay, this->_max_ack_delay);
  }
  // Based on {{RFC6298}}.
  if (this->_smoothed_rtt == 0) {
    this->_smoothed_rtt = latest_rtt;
    this->_rttvar       = latest_rtt / 2.0;
  } else {
    double rttvar_sample = ABS(this->_smoothed_rtt - latest_rtt);
    this->_rttvar        = 3.0 / 4.0 * this->_rttvar + 1.0 / 4.0 * rttvar_sample;
    this->_smoothed_rtt  = 7.0 / 8.0 * this->_smoothed_rtt + 1.0 / 8.0 * latest_rtt;
  }
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
  return pto * (1 << (threshold - 1));
}

ink_hrtime
QUICRTTMeasure::handshake_retransmit_timeout() const
{
  // Handshake retransmission alarm.
  ink_hrtime timeout = 0;
  if (this->_smoothed_rtt == 0) {
    timeout = 2 * this->_k_initial_rtt;
  } else {
    timeout = 2 * this->_smoothed_rtt;
  }
  timeout = std::max(timeout, this->_k_granularity);
  timeout = timeout * (1 << this->_crypto_count);

  return timeout;
}

void
QUICRTTMeasure::set_crypto_count(uint32_t count)
{
  this->_crypto_count = count;
}

void
QUICRTTMeasure::set_pto_count(uint32_t count)
{
  this->_pto_count = count;
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
QUICRTTMeasure::crypto_count() const
{
  return this->_crypto_count;
}

uint32_t
QUICRTTMeasure::pto_count() const
{
  return this->_pto_count;
}

void
QUICRTTMeasure::reset()
{
  this->_crypto_count = 0;
  this->_pto_count    = 0;
  this->_smoothed_rtt = 0;
  this->_rttvar       = 0;
  this->_min_rtt      = INT64_MAX;
}
