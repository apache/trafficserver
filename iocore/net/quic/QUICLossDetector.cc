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

#include "ts/ink_assert.h"

#include "QUICConfig.h"
#include "QUICEvents.h"

#define QUICLDDebug(fmt, ...) Debug("quic_loss_detector", "[%s] " fmt, this->_info->cids().data(), ##__VA_ARGS__)

QUICLossDetector::QUICLossDetector(QUICPacketTransmitter *transmitter, QUICConnectionInfoProvider *info,
                                   QUICCongestionController *cc)
  : _transmitter(transmitter), _info(info), _cc(cc)
{
  this->mutex                 = new_ProxyMutex();
  this->_loss_detection_mutex = new_ProxyMutex();

  QUICConfig::scoped_config params;
  this->_k_max_tlps                  = params->ld_max_tlps();
  this->_k_reordering_threshold      = params->ld_reordering_threshold();
  this->_k_time_reordering_fraction  = params->ld_time_reordering_fraction();
  this->_k_using_time_loss_detection = params->ld_time_loss_detection();
  this->_k_min_tlp_timeout           = params->ld_min_tlp_timeout();
  this->_k_min_rto_timeout           = params->ld_min_rto_timeout();
  this->_k_delayed_ack_timeout       = params->ld_delayed_ack_timeout();
  this->_k_default_initial_rtt       = params->ld_default_initial_rtt();

  this->reset();

  SET_HANDLER(&QUICLossDetector::event_handler);
}

QUICLossDetector::~QUICLossDetector()
{
  if (this->_loss_detection_alarm) {
    this->_loss_detection_alarm->cancel();
    this->_loss_detection_alarm = nullptr;
  }

  this->_sent_packets.clear();

  this->_transmitter = nullptr;
  this->_cc          = nullptr;
}

int
QUICLossDetector::event_handler(int event, Event *edata)
{
  switch (event) {
  case EVENT_INTERVAL: {
    if (this->_loss_detection_alarm_at <= Thread::get_hrtime()) {
      this->_loss_detection_alarm_at = 0;
      this->_on_loss_detection_alarm();
    }
    break;
  }
  case QUIC_EVENT_LD_SHUTDOWN: {
    SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
    QUICLDDebug("Shutdown");

    if (this->_loss_detection_alarm) {
      this->_loss_detection_alarm->cancel();
      this->_loss_detection_alarm = nullptr;
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

QUICErrorUPtr
QUICLossDetector::handle_frame(std::shared_ptr<const QUICFrame> frame)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  switch (frame->type()) {
  case QUICFrameType::ACK:
    this->_on_ack_received(std::dynamic_pointer_cast<const QUICAckFrame>(frame));
    break;
  default:
    QUICLDDebug("Unexpected frame type: %02x", static_cast<unsigned int>(frame->type()));
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
QUICLossDetector::on_packet_sent(QUICPacketUPtr packet)
{
  bool is_handshake   = false;
  QUICPacketType type = packet->type();

  if (type == QUICPacketType::VERSION_NEGOTIATION) {
    return;
  }

  if (type == QUICPacketType::INITIAL || type == QUICPacketType::HANDSHAKE) {
    is_handshake = true;
  }

  QUICPacketNumber packet_number = packet->packet_number();
  bool is_ack_only               = !packet->is_retransmittable();
  size_t sent_bytes              = is_ack_only ? 0 : packet->size();
  this->_on_packet_sent(packet_number, is_ack_only, is_handshake, sent_bytes, std::move(packet));
}

void
QUICLossDetector::reset()
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  if (this->_loss_detection_alarm) {
    this->_loss_detection_alarm->cancel();
    this->_loss_detection_alarm = nullptr;
  }

  this->_sent_packets.clear();

  // [draft-11 recovery] 3.5.3.  Initialization
  this->_handshake_outstanding       = 0;
  this->_retransmittable_outstanding = 0;

  if (this->_k_using_time_loss_detection) {
    this->_reordering_threshold     = UINT32_MAX;
    this->_time_reordering_fraction = this->_k_time_reordering_fraction;
  } else {
    this->_reordering_threshold     = this->_k_reordering_threshold;
    this->_time_reordering_fraction = INFINITY;
  }
}

void
QUICLossDetector::_on_packet_sent(QUICPacketNumber packet_number, bool is_ack_only, bool is_handshake, size_t sent_bytes,
                                  QUICPacketUPtr packet)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  ink_hrtime now             = Thread::get_hrtime();
  this->_largest_sent_packet = packet_number;
  // FIXME Should we really keep actual packet object?

  std::unique_ptr<PacketInfo> packet_info(
    new PacketInfo({packet_number, now, is_ack_only, is_handshake, sent_bytes, std::move(packet)}));
  this->_add_to_sent_packet_list(packet_number, std::move(packet_info));

  if (!is_ack_only) {
    if (is_handshake) {
      this->_time_of_last_sent_handshake_packet = now;
    }
    this->_time_of_last_sent_retransmittable_packet = now;
    this->_cc->on_packet_sent(sent_bytes);
    this->_set_loss_detection_alarm();
  }
}

void
QUICLossDetector::_on_ack_received(const std::shared_ptr<const QUICAckFrame> &ack_frame)
{
  SCOPED_MUTEX_LOCK(transmitter_lock, this->_transmitter->get_packet_transmitter_mutex().get(), this_ethread());
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  this->_largest_acked_packet = ack_frame->largest_acknowledged();
  // If the largest acked is newly acked, update the RTT.
  auto pi = this->_sent_packets.find(ack_frame->largest_acknowledged());
  if (pi != this->_sent_packets.end()) {
    this->_latest_rtt = Thread::get_hrtime() - pi->second->time;
    // _latest_rtt is nanosecond but ack_frame->ack_delay is microsecond and scaled
    // FIXME ack delay exponent has to be read from transport parameters
    uint8_t ack_delay_exponent = 3;
    ink_hrtime delay           = HRTIME_USECONDS(ack_frame->ack_delay() << ack_delay_exponent);
    this->_update_rtt(this->_latest_rtt, delay, ack_frame->largest_acknowledged());
  }

  QUICLDDebug("Unacked packets %lu (retransmittable %u, includes %u handshake packets)", this->_sent_packets.size(),
              this->_retransmittable_outstanding.load(), this->_handshake_outstanding.load());

  // Find all newly acked packets.
  for (auto &&range : this->_determine_newly_acked_packets(*ack_frame)) {
    for (auto ite = this->_sent_packets.begin(); ite != this->_sent_packets.end(); /* no increment here*/) {
      auto tmp_ite = ite;
      tmp_ite++;
      if (range.contains(ite->first)) {
        this->_on_packet_acked(ite->first, ite->second->bytes);
      }
      ite = tmp_ite;
    }
  }

  QUICLDDebug("Unacked packets %lu (retransmittable %u, includes %u handshake packets)", this->_sent_packets.size(),
              this->_retransmittable_outstanding.load(), this->_handshake_outstanding.load());

  this->_detect_lost_packets(ack_frame->largest_acknowledged());

  QUICLDDebug("Unacked packets %lu (retransmittable %u, includes %u handshake packets)", this->_sent_packets.size(),
              this->_retransmittable_outstanding.load(), this->_handshake_outstanding.load());

  this->_set_loss_detection_alarm();
}

void
QUICLossDetector::_update_rtt(ink_hrtime latest_rtt, ink_hrtime ack_delay, QUICPacketNumber largest_acked)
{
  // min_rtt ignores ack delay.
  this->_min_rtt = std::min(this->_min_rtt, latest_rtt);
  // Adjust for ack delay if it's plausible.
  if (latest_rtt - this->_min_rtt > ack_delay) {
    latest_rtt -= ack_delay;
    // Only save into max ack delay if it's used for rtt calculation and is not ack only.
    auto pi = this->_sent_packets.find(largest_acked);
    if (pi != this->_sent_packets.end() && !pi->second->ack_only) {
      this->_max_ack_delay = std::max(this->_max_ack_delay, ack_delay);
    }
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

void
QUICLossDetector::_on_packet_acked(QUICPacketNumber acked_packet_number, size_t acked_packet_size)
{
  SCOPED_MUTEX_LOCK(transmitter_lock, this->_transmitter->get_packet_transmitter_mutex().get(), this_ethread());
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  // QUICLDDebug("Packet number %" PRIu64 " has been acked", acked_packet_number);
  this->_cc->on_packet_acked(acked_packet_number, acked_packet_size);
  // If a packet sent prior to RTO was acked, then the RTO
  // was spurious.  Otherwise, inform congestion control.
  if (this->_rto_count > 0 && acked_packet_number > this->_largest_sent_before_rto) {
    this->_cc->on_retransmission_timeout_verified();
  }
  this->_handshake_count = 0;
  this->_tlp_count       = 0;
  this->_rto_count       = 0;
  this->_remove_from_sent_packet_list(acked_packet_number);
}

void
QUICLossDetector::_set_loss_detection_alarm()
{
  ink_hrtime alarm_duration;
  // Don't arm the alarm if there are no packets with
  // retransmittable data in flight.
  if (this->_retransmittable_outstanding == 0 && this->_loss_detection_alarm) {
    this->_loss_detection_alarm_at = 0;
    this->_loss_detection_alarm->cancel();
    this->_loss_detection_alarm = nullptr;
    QUICLDDebug("Loss detection alarm has been unset");

    return;
  }
  if (this->_handshake_outstanding) {
    // Handshake retransmission alarm.
    if (this->_smoothed_rtt == 0) {
      alarm_duration = 2 * this->_k_default_initial_rtt;
    } else {
      alarm_duration = 2 * this->_smoothed_rtt;
    }
    alarm_duration = std::max(alarm_duration + this->_max_ack_delay, this->_k_min_tlp_timeout);
    alarm_duration = alarm_duration * (1 << this->_handshake_count);

    this->_loss_detection_alarm_at = this->_time_of_last_sent_handshake_packet + alarm_duration;
    QUICLDDebug("Handshake retransmission alarm will be set");
  } else if (this->_loss_time != 0) {
    // Early retransmit timer or time loss detection.
    alarm_duration = this->_loss_time - this->_time_of_last_sent_retransmittable_packet;
    QUICLDDebug("Early retransmit timer or time loss detection will be set");
  } else if (this->_tlp_count < this->_k_max_tlps) {
    // Tail Loss Probe
    alarm_duration = std::max(static_cast<ink_hrtime>(1.5 * this->_smoothed_rtt + this->_max_ack_delay), this->_k_min_tlp_timeout);
    QUICLDDebug("TLP alarm will be set");
  } else {
    // RTO alarm
    alarm_duration = this->_smoothed_rtt + 4 * this->_rttvar + this->_max_ack_delay;
    alarm_duration = std::max(alarm_duration, this->_k_min_rto_timeout);
    alarm_duration = alarm_duration * (1 << this->_rto_count);
    QUICLDDebug("RTO alarm will be set");
  }

  // ADDITIONAL CODE
  // alarm_curation can be negative value because _loss_time is updated in _detect_lost_packets()
  // In that case, perhaps we should trigger the alarm immediately.
  if (alarm_duration < 0) {
    alarm_duration = 1;
  }
  // END OF ADDITONAL CODE

  if (this->_loss_detection_alarm_at) {
    this->_loss_detection_alarm_at = std::min(this->_loss_detection_alarm_at, Thread::get_hrtime() + alarm_duration);
  } else {
    this->_loss_detection_alarm_at = this->_time_of_last_sent_retransmittable_packet + alarm_duration;
  }
  QUICLDDebug("Loss detection alarm has been set to %" PRId64 "ms", alarm_duration / HRTIME_MSECOND);

  if (!this->_loss_detection_alarm) {
    this->_loss_detection_alarm = eventProcessor.schedule_every(this, HRTIME_MSECONDS(25));
  }
}

void
QUICLossDetector::_on_loss_detection_alarm()
{
  if (this->_handshake_outstanding) {
    // Handshake retransmission alarm.
    this->_retransmit_handshake_packets();
    this->_handshake_count++;
  } else if (this->_loss_time != 0) {
    // Early retransmit or Time Loss Detection
    this->_detect_lost_packets(this->_largest_acked_packet);
  } else if (this->_tlp_count < this->_k_max_tlps) {
    // Tail Loss Probe.
    QUICLDDebug("TLP");
    // FIXME TLP causes inifinite loop somehow
    // this->_send_one_packet();
    this->_tlp_count++;
  } else {
    // RTO.
    if (this->_rto_count == 0) {
      this->_largest_sent_before_rto = this->_largest_sent_packet;
    }
    QUICLDDebug("RTO");
    this->_send_two_packets();
    this->_rto_count++;
  }
  QUICLDDebug("Unacked packets %lu (retransmittable %u, includes %u handshake packets)", this->_sent_packets.size(),
              this->_retransmittable_outstanding.load(), this->_handshake_outstanding.load());
  this->_set_loss_detection_alarm();
}

void
QUICLossDetector::_detect_lost_packets(QUICPacketNumber largest_acked_packet_number)
{
  SCOPED_MUTEX_LOCK(transmitter_lock, this->_transmitter->get_packet_transmitter_mutex().get(), this_ethread());
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  this->_loss_time = 0;
  std::map<QUICPacketNumber, PacketInfo *> lost_packets;
  double delay_until_lost = INFINITY;

  if (this->_k_using_time_loss_detection) {
    delay_until_lost = (1 + this->_time_reordering_fraction) * std::max(this->_latest_rtt, this->_smoothed_rtt);
  } else if (largest_acked_packet_number == this->_largest_sent_packet) {
    // Early retransmit alarm.
    delay_until_lost = 5.0 / 4.0 * std::max(this->_latest_rtt, this->_smoothed_rtt);
  }

  for (auto &unacked : this->_sent_packets) {
    if (unacked.first >= largest_acked_packet_number) {
      break;
    }
    ink_hrtime time_since_sent = Thread::get_hrtime() - unacked.second->time;
    uint64_t packet_delta      = largest_acked_packet_number - unacked.second->packet_number;
    if (time_since_sent > delay_until_lost) {
      QUICLDDebug("Lost: time since sent is too long (PN=%" PRId64 " sent=%" PRId64 ", delay=%lf, fraction=%lf, lrtt=%" PRId64
                  ", srtt=%" PRId64 ")",
                  unacked.first, time_since_sent, delay_until_lost, this->_time_reordering_fraction, this->_latest_rtt,
                  this->_smoothed_rtt);
      lost_packets.insert({unacked.first, unacked.second.get()});
    } else if (packet_delta > this->_reordering_threshold) {
      QUICLDDebug("Lost: packet delta is too large (PN=%" PRId64 " largest=%" PRId64 " unacked=%" PRId64 " threshold=%" PRId32 ")",
                  unacked.first, largest_acked_packet_number, unacked.second->packet_number, this->_reordering_threshold);
      lost_packets.insert({unacked.first, unacked.second.get()});
    } else if (this->_loss_time == 0 && delay_until_lost != INFINITY) {
      this->_loss_time = Thread::get_hrtime() + delay_until_lost - time_since_sent;
    }
  }

  // Inform the congestion controller of lost packets and
  // lets it decide whether to retransmit immediately.
  if (!lost_packets.empty()) {
    this->_cc->on_packets_lost(lost_packets);
    for (auto lost_packet : lost_packets) {
      // ADDITIONAL CODE
      // Not sure how we can get feedback from congestion control and when we should retransmit the lost packets but we need to send
      // them somewhere.
      // I couldn't find the place so just send them here for now.
      this->_retransmit_lost_packet(*lost_packet.second->packet);
      // END OF ADDITIONAL CODE
      this->_remove_from_sent_packet_list(lost_packet.first);
    }
  }
}

// ===== Functions below are used on the spec but there're no pseudo code  =====

void
QUICLossDetector::_retransmit_handshake_packets()
{
  SCOPED_MUTEX_LOCK(transmitter_lock, this->_transmitter->get_packet_transmitter_mutex().get(), this_ethread());
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  std::set<QUICPacketNumber> retransmitted_handshake_packets;
  std::map<QUICPacketNumber, PacketInfo *> lost_packets;

  for (auto &info : this->_sent_packets) {
    retransmitted_handshake_packets.insert(info.first);
    lost_packets.insert({info.first, info.second.get()});
    this->_transmitter->retransmit_packet(*info.second->packet);
  }

  this->_cc->on_packets_lost(lost_packets);
  for (auto packet_number : retransmitted_handshake_packets) {
    this->_remove_from_sent_packet_list(packet_number);
  }
}

void
QUICLossDetector::_send_one_packet()
{
  SCOPED_MUTEX_LOCK(transmitter_lock, this->_transmitter->get_packet_transmitter_mutex().get(), this_ethread());
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  if (this->_transmitter->transmit_packet() < 1) {
    auto ite = this->_sent_packets.rbegin();
    if (ite != this->_sent_packets.rend()) {
      this->_transmitter->retransmit_packet(*ite->second->packet);
    }
  }
}

void
QUICLossDetector::_send_two_packets()
{
  SCOPED_MUTEX_LOCK(transmitter_lock, this->_transmitter->get_packet_transmitter_mutex().get(), this_ethread());
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  auto ite = this->_sent_packets.rbegin();
  if (ite != this->_sent_packets.rend()) {
    this->_transmitter->retransmit_packet(*ite->second->packet);
    ite++;
    if (ite != this->_sent_packets.rend()) {
      this->_transmitter->retransmit_packet(*ite->second->packet);
    }
  } else {
    this->_transmitter->transmit_packet();
  }
}

// ===== Functions below are helper functions =====

void
QUICLossDetector::_retransmit_lost_packet(const QUICPacket &packet)
{
  SCOPED_MUTEX_LOCK(transmitter_lock, this->_transmitter->get_packet_transmitter_mutex().get(), this_ethread());
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());
  this->_transmitter->retransmit_packet(packet);
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
QUICLossDetector::_add_to_sent_packet_list(QUICPacketNumber packet_number, std::unique_ptr<PacketInfo> packet_info)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  // Add to the list
  this->_sent_packets.insert(std::pair<QUICPacketNumber, std::unique_ptr<PacketInfo>>(packet_number, std::move(packet_info)));

  // Increment counters
  auto ite = this->_sent_packets.find(packet_number);
  if (ite != this->_sent_packets.end()) {
    if (ite->second->handshake) {
      ++this->_handshake_outstanding;
      ink_assert(this->_handshake_outstanding.load() > 0);
    }
    if (!ite->second->ack_only) {
      ++this->_retransmittable_outstanding;
      ink_assert(this->_retransmittable_outstanding.load() > 0);
    }
  }
}

void
QUICLossDetector::_remove_from_sent_packet_list(QUICPacketNumber packet_number)
{
  SCOPED_MUTEX_LOCK(lock, this->_loss_detection_mutex, this_ethread());

  // Decrement counters
  auto ite = this->_sent_packets.find(packet_number);
  if (ite != this->_sent_packets.end()) {
    if (ite->second->handshake) {
      ink_assert(this->_handshake_outstanding.load() > 0);
      --this->_handshake_outstanding;
    }
    if (!ite->second->ack_only) {
      ink_assert(this->_retransmittable_outstanding.load() > 0);
      --this->_retransmittable_outstanding;
    }
  }

  // Remove from the list
  this->_sent_packets.erase(packet_number);
}

ink_hrtime
QUICLossDetector::current_rto_period()
{
  ink_hrtime alarm_duration;
  alarm_duration = this->_smoothed_rtt + 4 * this->_rttvar + this->_max_ack_delay;
  alarm_duration = std::max(alarm_duration, this->_k_min_rto_timeout);
  alarm_duration = alarm_duration * (1 << this->_rto_count);
  return alarm_duration;
}

ink_hrtime
QUICLossDetector::smoothed_rtt() const
{
  return this->_smoothed_rtt;
}
