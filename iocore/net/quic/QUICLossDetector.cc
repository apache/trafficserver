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

#define QUICLDDebug(fmt, ...)                                                                                                \
  Debug("quic_loss_detector", "[%s] [%s] " fmt, this->_info->cids().data(), QUICDebugNames::pn_space(this->_pn_space_index), \
        ##__VA_ARGS__)
#define QUICLDVDebug(fmt, ...)                                                                                                 \
  Debug("v_quic_loss_detector", "[%s] [%s] " fmt, this->_info->cids().data(), QUICDebugNames::pn_space(this->_pn_space_index), \
        ##__VA_ARGS__)

QUICLossDetector::QUICLossDetector(QUICConnectionInfoProvider *info, QUICCongestionController *cc, QUICRTTMeasure *rtt_measure,
                                   int index)
  : _info(info), _cc(cc), _rtt_measure(rtt_measure), _pn_space_index(index)
{
  this->mutex                 = new_ProxyMutex();
  this->_loss_detection_mutex = new_ProxyMutex();

  QUICConfig::scoped_config params;
  this->_k_packet_threshold = params->ld_packet_threshold();
  this->_k_time_threshold   = params->ld_time_threshold();
  this->_k_granularity      = params->ld_granularity();
  this->_k_initial_rtt      = params->ld_initial_rtt();

  this->reset();

  SET_HANDLER(&QUICLossDetector::event_handler);
}

QUICLossDetector::~QUICLossDetector()
{
  if (this->_loss_detection_timer) {
    this->_loss_detection_timer->cancel();
    this->_loss_detection_timer = nullptr;
  }

  this->_sent_packets.clear();

  this->_cc = nullptr;
}

int
QUICLossDetector::event_handler(int event, Event *edata)
{
  switch (event) {
  case EVENT_INTERVAL: {
    if (this->_loss_detection_alarm_at <= Thread::get_hrtime()) {
      this->_loss_detection_alarm_at = 0;
      this->_smoothed_rtt            = this->_rtt_measure->smoothed_rtt();
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

  if (this->_pn_space_index != QUICTypeUtil::pn_space_index(level)) {
    return error;
  }

  switch (frame.type()) {
  case QUICFrameType::ACK:
    this->_smoothed_rtt = this->_rtt_measure->smoothed_rtt();
    this->_on_ack_received(static_cast<const QUICAckFrame &>(frame));
    break;
  default:
    QUICLDDebug("Unexpected frame type: %02x", static_cast<unsigned int>(frame.type()));
    ink_assert(false);
    break;
  }

  return error;
}

QUICPacketNumber
QUICLossDetector::largest_acked_packet_number()
{
  return this->_largest_acked_packet;
}

void
QUICLossDetector::on_packet_sent(QUICPacket &packet, bool in_flight)
{
  if (packet.type() == QUICPacketType::VERSION_NEGOTIATION) {
    return;
  }

  this->_smoothed_rtt = this->_rtt_measure->smoothed_rtt();
  auto packet_number  = packet.packet_number();
  auto ack_eliciting  = packet.is_ack_eliciting();
  auto crypto         = packet.is_crypto_packet();
  auto sent_bytes     = packet.size();
  auto type           = packet.type();
  auto frames         = std::move(packet.frames());
  this->_on_packet_sent(packet_number, ack_eliciting, in_flight, crypto, sent_bytes, type, frames);
}

void
QUICLossDetector::reset()
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  if (this->_loss_detection_timer) {
    this->_loss_detection_timer->cancel();
    this->_loss_detection_timer = nullptr;
  }

  this->_sent_packets.clear();

  // [draft-17 recovery] 6.4.3.  Initialization
  this->_crypto_count                           = 0;
  this->_pto_count                              = 0;
  this->_loss_time                              = 0;
  this->_smoothed_rtt                           = 0;
  this->_rttvar                                 = 0;
  this->_min_rtt                                = INT64_MAX;
  this->_time_of_last_sent_ack_eliciting_packet = 0;
  this->_time_of_last_sent_crypto_packet        = 0;
  this->_largest_sent_packet                    = 0;
  this->_largest_acked_packet                   = 0;
}

void
QUICLossDetector::update_ack_delay_exponent(uint8_t ack_delay_exponent)
{
  this->_ack_delay_exponent = ack_delay_exponent;
}

void
QUICLossDetector::_on_packet_sent(QUICPacketNumber packet_number, bool ack_eliciting, bool in_flight, bool is_crypto_packet,
                                  size_t sent_bytes, QUICPacketType type, std::vector<QUICFrameInfo> &frames)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  ink_hrtime now             = Thread::get_hrtime();
  this->_largest_sent_packet = packet_number;
  PacketInfoUPtr packet_info = PacketInfoUPtr(new PacketInfo(
    {packet_number, now, ack_eliciting, is_crypto_packet, in_flight, ack_eliciting ? sent_bytes : 0, type, std::move(frames)}));

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
QUICLossDetector::_on_ack_received(const QUICAckFrame &ack_frame)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  this->_largest_acked_packet = std::max(this->_largest_acked_packet, ack_frame.largest_acknowledged());
  // If the largest acknowledged is newly acked and
  //  ack-eliciting, update the RTT.
  auto pi = this->_sent_packets.find(ack_frame.largest_acknowledged());
  if (pi != this->_sent_packets.end()) {
    this->_latest_rtt = Thread::get_hrtime() - pi->second->time_sent;
    // _latest_rtt is nanosecond but ack_frame.ack_delay is microsecond and scaled
    ink_hrtime delay = HRTIME_USECONDS(ack_frame.ack_delay() << this->_ack_delay_exponent);
    this->_update_rtt(this->_latest_rtt, delay);
  }

  QUICLDVDebug("Unacked packets %lu (retransmittable %u, includes %u handshake packets)", this->_sent_packets.size(),
               this->_ack_eliciting_outstanding.load(), this->_crypto_outstanding.load());

  // if (ACK frame contains ECN information):
  //   ProcessECN(ack)
  if (ack_frame.ecn_section() != nullptr && pi != this->_sent_packets.end()) {
    this->_cc->process_ecn(*pi->second, ack_frame.ecn_section(), this->_pto_count);
  }

  // Find all newly acked packets.
  bool newly_acked_packets = false;
  for (auto &&range : this->_determine_newly_acked_packets(ack_frame)) {
    for (auto ite = this->_sent_packets.begin(); ite != this->_sent_packets.end(); /* no increment here*/) {
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

  QUICLDVDebug("Unacked packets %lu (retransmittable %u, includes %u handshake packets)", this->_sent_packets.size(),
               this->_ack_eliciting_outstanding.load(), this->_crypto_outstanding.load());

  this->_detect_lost_packets();

  this->_crypto_count = 0;
  this->_pto_count    = 0;

  QUICLDDebug("Unacked packets %lu (retransmittable %u, includes %u handshake packets)", this->_sent_packets.size(),
              this->_ack_eliciting_outstanding.load(), this->_crypto_outstanding.load());

  this->_set_loss_detection_timer();
}

void
QUICLossDetector::_update_rtt(ink_hrtime latest_rtt, ink_hrtime ack_delay)
{
  // min_rtt ignores ack delay.
  this->_min_rtt = std::min(this->_min_rtt, latest_rtt);
  // Limit ack_delay by max_ack_delay
  ack_delay = std::min(ack_delay, this->_max_ack_delay);
  // Adjust for ack delay if it's plausible.
  if (latest_rtt - this->_min_rtt > ack_delay) {
    latest_rtt -= ack_delay;
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

  this->_rtt_measure->update_smoothed_rtt(this->_smoothed_rtt);
}

void
QUICLossDetector::_on_packet_acked(const PacketInfo &acked_packet)
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

  this->_remove_from_sent_packet_list(acked_packet.packet_number);
}

void
QUICLossDetector::_set_loss_detection_timer()
{
  ink_hrtime timeout;

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

  if (this->_crypto_outstanding) {
    // Handshake retransmission alarm.
    if (this->_smoothed_rtt == 0) {
      timeout = 2 * this->_k_initial_rtt;
    } else {
      timeout = 2 * this->_smoothed_rtt;
    }
    timeout = std::max(timeout, this->_k_granularity);
    timeout = timeout * (1 << this->_crypto_count);

    this->_loss_detection_alarm_at = this->_time_of_last_sent_crypto_packet + timeout;
    QUICLDDebug("crypto packet alarm will be set: %" PRId64, this->_loss_detection_alarm_at);
    // -- ADDITIONAL CODE --
    // In psudocode returning here, but we don't do for scheduling _loss_detection_alarm event.
    // -- END OF ADDITIONAL CODE --
  } else {
    if (this->_loss_time != 0) {
      // Time threshold loss detection.
      this->_loss_detection_alarm_at = this->_loss_time;
      QUICLDDebug("time threshold loss detection timer: %" PRId64, this->_loss_detection_alarm_at);

    } else {
      // PTO Duration
      timeout                        = this->_smoothed_rtt + 4 * this->_rttvar + this->_max_ack_delay;
      timeout                        = std::max(timeout, this->_k_granularity);
      timeout                        = timeout * (1 << this->_pto_count);
      this->_loss_detection_alarm_at = this->_time_of_last_sent_ack_eliciting_packet + timeout;
      QUICLDDebug("PTO timeout will be set: %" PRId64, this->_loss_detection_alarm_at);
    }

    QUICLDDebug("Loss detection alarm has been set to %" PRId64 "ms", timeout / HRTIME_MSECOND);

    if (!this->_loss_detection_timer) {
      this->_loss_detection_timer = eventProcessor.schedule_every(this, HRTIME_MSECONDS(25));
    }
  }
}

void
QUICLossDetector::_on_loss_detection_timeout()
{
  if (this->_crypto_outstanding) {
    // Handshake retransmission alarm.
    this->_retransmit_all_unacked_crypto_data();
    this->_crypto_count++;
  } else if (this->_loss_time != 0) {
    // Early retransmit or Time Loss Detection
    this->_detect_lost_packets();
  } else {
    QUICLDVDebug("PTO");
    this->_send_two_packets();
    this->_pto_count++;
  }

  QUICLDDebug("Unacked packets %lu (retransmittable %u, includes %u handshake packets)", this->_sent_packets.size(),
              this->_ack_eliciting_outstanding.load(), this->_crypto_outstanding.load());

  if (is_debug_tag_set("v_quic_loss_detector")) {
    for (auto &unacked : this->_sent_packets) {
      QUICLDVDebug("#%" PRIu64 " is_crypto=%i ack_eliciting=%i size=%zu", unacked.first, unacked.second->is_crypto_packet,
                   unacked.second->ack_eliciting, unacked.second->sent_bytes);
    }
  }

  this->_set_loss_detection_timer();
}

void
QUICLossDetector::_detect_lost_packets()
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  this->_loss_time      = 0;
  ink_hrtime loss_delay = this->_k_time_threshold * std::max(this->_latest_rtt, this->_smoothed_rtt);
  std::map<QUICPacketNumber, PacketInfo *> lost_packets;

  // Packets sent before this time are deemed lost.
  ink_hrtime lost_send_time = Thread::get_hrtime() - loss_delay;

  // Packets with packet numbers before this are deemed lost.
  QUICPacketNumber lost_pn = this->_largest_acked_packet - this->_k_packet_threshold;

  for (auto it = this->_sent_packets.begin(); it != this->_sent_packets.end(); ++it) {
    if (it->first >= this->_largest_acked_packet) {
      break;
    }

    auto &unacked = it->second;

    // Mark packet as lost, or set time when it should be marked.
    if (unacked->time_sent < lost_send_time || unacked->packet_number < lost_pn) {
      if (unacked->time_sent < lost_send_time) {
        QUICLDDebug("Lost: time since sent is too long (#%" PRId64 " sent=%" PRId64 ", delay=%" PRId64
                    ", fraction=%lf, lrtt=%" PRId64 ", srtt=%" PRId64 ")",
                    it->first, unacked->time_sent, lost_send_time, this->_k_time_threshold, this->_latest_rtt, this->_smoothed_rtt);
      } else {
        QUICLDDebug("Lost: packet delta is too large (#%" PRId64 " largest=%" PRId64 " threshold=%" PRId32 ")", it->first,
                    this->_largest_acked_packet, this->_k_packet_threshold);
      }

      if (unacked->in_flight) {
        lost_packets.insert({it->first, it->second.get()});
      } else if (this->_loss_time == 0) {
        this->_loss_time = unacked->time_sent + loss_delay;
      } else {
        this->_loss_time = std::min(this->_loss_time, unacked->time_sent + loss_delay);
      }
    }
  }

  // Inform the congestion controller of lost packets and
  // lets it decide whether to retransmit immediately.
  if (!lost_packets.empty()) {
    this->_cc->on_packets_lost(lost_packets, this->_pto_count);
    for (auto lost_packet : lost_packets) {
      // -- ADDITIONAL CODE --
      // Not sure how we can get feedback from congestion control and when we should retransmit the lost packets but we need to send
      // them somewhere.
      // I couldn't find the place so just send them here for now.
      this->_retransmit_lost_packet(*lost_packet.second);
      // -- END OF ADDITIONAL CODE --
      // -- ADDITIONAL CODE --
      this->_remove_from_sent_packet_list(lost_packet.first);
      // -- END OF ADDITIONAL CODE --
    }
  }
}

// ===== Functions below are used on the spec but there're no pseudo code  =====

void
QUICLossDetector::_retransmit_all_unacked_crypto_data()
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  std::set<QUICPacketNumber> retransmitted_crypto_packets;
  std::map<QUICPacketNumber, PacketInfo *> lost_packets;

  for (auto &info : this->_sent_packets) {
    if (info.second->is_crypto_packet) {
      retransmitted_crypto_packets.insert(info.first);
      this->_retransmit_lost_packet(*info.second);
      lost_packets.insert({info.first, info.second.get()});
    }
  }

  this->_cc->on_packets_lost(lost_packets, this->_pto_count);
  for (auto packet_number : retransmitted_crypto_packets) {
    this->_remove_from_sent_packet_list(packet_number);
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
QUICLossDetector::_retransmit_lost_packet(PacketInfo &packet_info)
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
QUICLossDetector::_add_to_sent_packet_list(QUICPacketNumber packet_number, PacketInfoUPtr packet_info)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  // Add to the list
  this->_sent_packets.insert(std::pair<QUICPacketNumber, PacketInfoUPtr>(packet_number, std::move(packet_info)));

  // Increment counters
  auto ite = this->_sent_packets.find(packet_number);
  if (ite != this->_sent_packets.end()) {
    if (ite->second->is_crypto_packet) {
      ++this->_crypto_outstanding;
      ink_assert(this->_crypto_outstanding.load() > 0);
    }
    if (!ite->second->ack_eliciting) {
      ++this->_ack_eliciting_outstanding;
      ink_assert(this->_ack_eliciting_outstanding.load() > 0);
    }
  }
}

void
QUICLossDetector::_remove_from_sent_packet_list(QUICPacketNumber packet_number)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  auto ite = this->_sent_packets.find(packet_number);
  this->_decrement_outstanding_counters(ite);
  this->_sent_packets.erase(packet_number);
}

std::map<QUICPacketNumber, PacketInfoUPtr>::iterator
QUICLossDetector::_remove_from_sent_packet_list(std::map<QUICPacketNumber, PacketInfoUPtr>::iterator it)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  this->_decrement_outstanding_counters(it);
  return this->_sent_packets.erase(it);
}

void
QUICLossDetector::_decrement_outstanding_counters(std::map<QUICPacketNumber, PacketInfoUPtr>::iterator it)
{
  if (it != this->_sent_packets.end()) {
    // Decrement counters
    if (it->second->is_crypto_packet) {
      ink_assert(this->_crypto_outstanding.load() > 0);
      --this->_crypto_outstanding;
    }
    if (!it->second->ack_eliciting) {
      ink_assert(this->_ack_eliciting_outstanding.load() > 0);
      --this->_ack_eliciting_outstanding;
    }
  }
}

ink_hrtime
QUICLossDetector::current_rto_period()
{
  ink_hrtime alarm_duration;
  alarm_duration = this->_smoothed_rtt + 4 * this->_rttvar + this->_max_ack_delay;
  alarm_duration = std::max(alarm_duration, this->_k_granularity);
  alarm_duration = alarm_duration * (1 << this->_pto_count);
  return alarm_duration;
}

//
// QUICRTTMeasure
//
ink_hrtime
QUICRTTMeasure::smoothed_rtt() const
{
  return this->_smoothed_rtt.load();
}

void
QUICRTTMeasure::update_smoothed_rtt(ink_hrtime rtt)
{
  this->_smoothed_rtt.store(rtt);
}
