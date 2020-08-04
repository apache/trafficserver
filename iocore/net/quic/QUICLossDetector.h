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

// TODO Using STL Map because ts/Map lacks remove method
#include <map>
#include <set>

#include "I_EventSystem.h"
#include "I_Action.h"
#include "tscore/ink_hrtime.h"
#include "I_VConnection.h"
#include "QUICTypes.h"
#include "QUICPacket.h"
#include "QUICFrame.h"
#include "QUICFrameHandler.h"
#include "QUICConnection.h"
#include "QUICContext.h"
#include "QUICCongestionController.h"

class QUICPadder;
class QUICPinger;
class QUICLossDetector;
class QUICRTTMeasure;

class QUICLossDetector : public Continuation, public QUICFrameHandler
{
public:
  QUICLossDetector(QUICContext &context, QUICCongestionController *cc, QUICRTTMeasure *rtt_measure, QUICPinger *pinger,
                   QUICPadder *padder);
  ~QUICLossDetector();

  int event_handler(int event, Event *edata);

  // QUICFrameHandler interface
  std::vector<QUICFrameType> interests() override;
  virtual QUICConnectionErrorUPtr handle_frame(QUICEncryptionLevel level, const QUICFrame &frame) override;

  void on_packet_sent(QUICSentPacketInfoUPtr packet_info, bool in_flight = true);
  void on_datagram_received();
  // OnPacketNumberSpaceDiscarded is on Congestion Control section but having it here makes more sense because most processes are
  // for LD.
  void on_packet_number_space_discarded(QUICPacketNumberSpace pn_space);
  QUICPacketNumber largest_acked_packet_number(QUICPacketNumberSpace pn_space) const;
  void update_ack_delay_exponent(uint8_t ack_delay_exponent);
  void reset();

private:
  Ptr<ProxyMutex> _loss_detection_mutex;

  uint8_t _ack_delay_exponent = 3;

  // Recovery A.2. Constants of Interest
  // Values will be loaded from records.config via QUICConfig at constructor
  uint32_t _k_packet_threshold = 0;
  float _k_time_threshold      = 0.0;
  // kGranularity, kInitialRtt are defined in QUICRTTMeasure
  QUICRTTMeasure *_rtt_measure = nullptr;
  // kPacketNumberSpace is defined as QUICPacketNumberSpace on QUICTypes.h

  // Recovery A.3. Variables of interest
  // Keep the order as the same as the spec so that we can see the difference easily.
  // latest_rtt, smoothed_rtt, rttvar, min_rtt and max_ack_delay are defined in QUICRttMeasure
  Action *_loss_detection_timer = nullptr;
  // pto_count is defined in QUICRttMeasure
  ink_hrtime _time_of_last_ack_eliciting_packet[QUIC_N_PACKET_SPACES] = {0};
  QUICPacketNumber _largest_acked_packet[QUIC_N_PACKET_SPACES]        = {0};
  ink_hrtime _loss_time[QUIC_N_PACKET_SPACES]                         = {0};
  std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> _sent_packets[QUIC_N_PACKET_SPACES];

  // These are not defined on the spec but expected to be count
  // These counter have to be updated when inserting / erasing packets from _sent_packets with following functions.
  std::atomic<uint32_t> _ack_eliciting_outstanding;
  std::atomic<uint32_t> _num_packets_in_flight[QUIC_N_PACKET_SPACES];
  void _add_to_sent_packet_list(QUICPacketNumber packet_number, std::unique_ptr<QUICSentPacketInfo> packet_info);
  std::pair<QUICSentPacketInfoUPtr, std::map<QUICPacketNumber, QUICSentPacketInfoUPtr>::iterator> _remove_from_sent_packet_list(
    std::map<QUICPacketNumber, QUICSentPacketInfoUPtr>::iterator it, QUICPacketNumberSpace pn_space);
  void _decrement_counters(std::map<QUICPacketNumber, QUICSentPacketInfoUPtr>::iterator it, QUICPacketNumberSpace pn_space);

  /*
   * Because this alarm will be reset on every packet transmission, to reduce number of events,
   * Loss Detector uses schedule_every() and checks if it has to be triggered.
   */
  ink_hrtime _loss_detection_alarm_at = 0;

  void _on_ack_received(const QUICAckFrame &ack_frame, QUICPacketNumberSpace pn_space);
  void _on_packet_acked(const QUICSentPacketInfo &acked_packet);
  std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> _detect_and_remove_lost_packets(QUICPacketNumberSpace pn_space);
  void _set_loss_detection_timer();
  void _on_loss_detection_timeout();
  void _retransmit_lost_packet(const QUICSentPacketInfo &packet_info);

  ink_hrtime _get_loss_time_and_space(QUICPacketNumberSpace &space);
  ink_hrtime _get_pto_time_and_space(QUICPacketNumberSpace &space);
  bool _peer_completed_address_validation() const;

  std::vector<QUICSentPacketInfoUPtr> _detect_and_remove_acked_packets(const QUICAckFrame &ack_frame,
                                                                       QUICPacketNumberSpace pn_space);
  bool _include_ack_eliciting(const std::vector<QUICSentPacketInfoUPtr> &acked_packets) const;

  void _send_one_or_two_ack_eliciting_packet(QUICPacketNumberSpace pn_space);
  void _send_one_ack_eliciting_handshake_packet();
  void _send_one_ack_eliciting_padded_initial_packet();

  void _send_packet(QUICEncryptionLevel level, bool padded = false);

  bool _is_client_without_one_rtt_key() const;

  QUICPinger *_pinger           = nullptr;
  QUICPadder *_padder           = nullptr;
  QUICCongestionController *_cc = nullptr;

  QUICContext &_context;
};

class QUICRTTMeasure : public QUICRTTProvider
{
public:
  // use `friend` so ld can access RTTMeasure.
  // friend QUICLossDetector;

  QUICRTTMeasure(const QUICLDConfig &ld_config);
  QUICRTTMeasure() = default;

  void init(const QUICLDConfig &ld_config);

  // period
  ink_hrtime current_pto_period() const;
  ink_hrtime congestion_period(uint32_t threshold) const override;

  // get members
  ink_hrtime smoothed_rtt() const override;
  ink_hrtime rttvar() const override;
  ink_hrtime latest_rtt() const override;

  uint32_t pto_count() const;
  ink_hrtime max_ack_delay() const;

  void set_pto_count(uint32_t count);
  void set_max_ack_delay(ink_hrtime max_ack_delay);

  void update_rtt(ink_hrtime latest_rtt, ink_hrtime ack_delay);
  void reset();

  ink_hrtime k_granularity() const;

private:
  bool _is_first_sample = false;

  // A.3. Variables of interest
  ink_hrtime _latest_rtt   = 0;
  ink_hrtime _smoothed_rtt = 0;
  ink_hrtime _rttvar       = 0;
  ink_hrtime _min_rtt      = INT64_MAX;
  // FIXME should be set by transport parameters
  ink_hrtime _max_ack_delay = HRTIME_MSECONDS(25);
  uint32_t _pto_count       = 0;

  // Recovery A.2.  Constants of Interest
  ink_hrtime _k_granularity = 0;
  ink_hrtime _k_initial_rtt = HRTIME_MSECONDS(500);
};
