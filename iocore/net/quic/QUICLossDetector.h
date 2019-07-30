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

class QUICLossDetector;
class QUICRTTMeasure;

struct QUICPacketInfo {
  // 6.3.1.  Sent Packet Fields
  QUICPacketNumber packet_number;
  ink_hrtime time_sent;
  bool ack_eliciting;
  bool is_crypto_packet;
  bool in_flight;
  size_t sent_bytes;

  // addition
  QUICPacketType type;
  std::vector<QUICFrameInfo> frames;
  QUICPacketNumberSpace pn_space;
  // end
};

using QUICPacketInfoUPtr = std::unique_ptr<QUICPacketInfo>;

class QUICRTTProvider
{
public:
  virtual ink_hrtime smoothed_rtt() const = 0;
  virtual ink_hrtime rttvar() const       = 0;
  virtual ink_hrtime latest_rtt() const   = 0;

  virtual ink_hrtime congestion_period(uint32_t threshold) const = 0;
};

class QUICCongestionController
{
public:
  QUICCongestionController(const QUICRTTProvider &rtt_provider, QUICConnectionInfoProvider *info, const QUICCCConfig &cc_config);
  virtual ~QUICCongestionController() {}
  void on_packet_sent(size_t bytes_sent);
  void on_packet_acked(const QUICPacketInfo &acked_packet);
  virtual void on_packets_lost(const std::map<QUICPacketNumber, QUICPacketInfo *> &packets);
  void on_retransmission_timeout_verified();
  void process_ecn(const QUICPacketInfo &acked_largest_packet, const QUICAckFrame::EcnSection *ecn_section);
  bool check_credit() const;
  uint32_t open_window() const;
  void reset();
  bool is_app_limited();

  // Debug
  uint32_t bytes_in_flight() const;
  uint32_t congestion_window() const;
  uint32_t current_ssthresh() const;

private:
  Ptr<ProxyMutex> _cc_mutex;

  void _congestion_event(ink_hrtime sent_time);
  bool _in_persistent_congestion(const std::map<QUICPacketNumber, QUICPacketInfo *> &lost_packets,
                                 QUICPacketInfo *largest_lost_packet);
  bool _in_window_lost(const std::map<QUICPacketNumber, QUICPacketInfo *> &lost_packets, QUICPacketInfo *largest_lost_packet,
                       ink_hrtime period) const;

  // [draft-17 recovery] 7.9.1. Constants of interest
  // Values will be loaded from records.config via QUICConfig at constructor
  uint32_t _k_max_datagram_size               = 0;
  uint32_t _k_initial_window                  = 0;
  uint32_t _k_minimum_window                  = 0;
  float _k_loss_reduction_factor              = 0.0;
  uint32_t _k_persistent_congestion_threshold = 0;

  // [draft-17 recovery] 7.9.2. Variables of interest
  uint32_t _ecn_ce_counter        = 0;
  uint32_t _bytes_in_flight       = 0;
  uint32_t _congestion_window     = 0;
  ink_hrtime _recovery_start_time = 0;
  uint32_t _ssthresh              = UINT32_MAX;

  QUICConnectionInfoProvider *_info = nullptr;
  const QUICRTTProvider &_rtt_provider;

  bool _in_recovery(ink_hrtime sent_time);
};

class QUICLossDetector : public Continuation, public QUICFrameHandler
{
public:
  QUICLossDetector(QUICConnectionInfoProvider *info, QUICCongestionController *cc, QUICRTTMeasure *rtt_measure,
                   const QUICLDConfig &ld_config);
  ~QUICLossDetector();

  int event_handler(int event, Event *edata);

  std::vector<QUICFrameType> interests() override;
  virtual QUICConnectionErrorUPtr handle_frame(QUICEncryptionLevel level, const QUICFrame &frame) override;
  void on_packet_sent(QUICPacketInfoUPtr packet_info, bool in_flight = true);
  QUICPacketNumber largest_acked_packet_number(QUICPacketNumberSpace pn_space);
  void update_ack_delay_exponent(uint8_t ack_delay_exponent);
  void reset();

private:
  Ptr<ProxyMutex> _loss_detection_mutex;

  uint8_t _ack_delay_exponent = 3;

  // [draft-17 recovery] 6.4.1.  Constants of interest
  // Values will be loaded from records.config via QUICConfig at constructor
  uint32_t _k_packet_threshold = 0;
  float _k_time_threshold      = 0.0;

  // [draft-11 recovery] 3.5.2.  Variables of interest
  // Keep the order as the same as the spec so that we can see the difference easily.
  Action *_loss_detection_timer                              = nullptr;
  ink_hrtime _time_of_last_sent_ack_eliciting_packet         = 0;
  ink_hrtime _time_of_last_sent_crypto_packet                = 0;
  ink_hrtime _loss_time[kPacketNumberSpace]                  = {0};
  QUICPacketNumber _largest_acked_packet[kPacketNumberSpace] = {0};
  std::map<QUICPacketNumber, QUICPacketInfoUPtr> _sent_packets[kPacketNumberSpace];

  // These are not defined on the spec but expected to be count
  // These counter have to be updated when inserting / erasing packets from _sent_packets with following functions.
  std::atomic<uint32_t> _crypto_outstanding;
  std::atomic<uint32_t> _ack_eliciting_outstanding;
  void _add_to_sent_packet_list(QUICPacketNumber packet_number, std::unique_ptr<QUICPacketInfo> packet_info);
  void _remove_from_sent_packet_list(QUICPacketNumber packet_number, QUICPacketNumberSpace pn_space);
  std::map<QUICPacketNumber, QUICPacketInfoUPtr>::iterator _remove_from_sent_packet_list(
    std::map<QUICPacketNumber, QUICPacketInfoUPtr>::iterator it, QUICPacketNumberSpace pn_space);
  void _decrement_outstanding_counters(std::map<QUICPacketNumber, QUICPacketInfoUPtr>::iterator it, QUICPacketNumberSpace pn_space);

  /*
   * Because this alarm will be reset on every packet transmission, to reduce number of events,
   * Loss Detector uses schedule_every() and checks if it has to be triggered.
   */
  ink_hrtime _loss_detection_alarm_at = 0;

  void _on_ack_received(const QUICAckFrame &ack_frame, QUICPacketNumberSpace pn_space);
  void _on_packet_acked(const QUICPacketInfo &acked_packet);
  void _detect_lost_packets(QUICPacketNumberSpace pn_space);
  void _set_loss_detection_timer();
  void _on_loss_detection_timeout();
  void _retransmit_lost_packet(QUICPacketInfo &packet_info);

  ink_hrtime _get_earliest_loss_time(QUICPacketNumberSpace &pn_space);

  std::set<QUICAckFrame::PacketNumberRange> _determine_newly_acked_packets(const QUICAckFrame &ack_frame);

  void _retransmit_all_unacked_crypto_data();
  void _send_one_packet();
  void _send_two_packets();

  QUICConnectionInfoProvider *_info = nullptr;
  QUICRTTMeasure *_rtt_measure      = nullptr;
  QUICCongestionController *_cc     = nullptr;
};

class QUICRTTMeasure : public QUICRTTProvider
{
public:
  // use `friend` so ld can acesss RTTMeasure.
  // friend QUICLossDetector;

  QUICRTTMeasure(const QUICLDConfig &ld_config);
  QUICRTTMeasure() = default;

  void init(const QUICLDConfig &ld_config);

  // period
  ink_hrtime handshake_retransmit_timeout() const;
  ink_hrtime current_pto_period() const;
  ink_hrtime congestion_period(uint32_t threshold) const override;

  // get members
  ink_hrtime smoothed_rtt() const override;
  ink_hrtime rttvar() const override;
  ink_hrtime latest_rtt() const override;

  uint32_t pto_count() const;
  uint32_t crypto_count() const;

  void set_crypto_count(uint32_t count);
  void set_pto_count(uint32_t count);

  void update_rtt(ink_hrtime latest_rtt, ink_hrtime ack_delay);
  void reset();

private:
  // related to rtt calculate
  uint32_t _crypto_count    = 0;
  uint32_t _pto_count       = 0;
  ink_hrtime _max_ack_delay = 0;

  // rtt vars
  ink_hrtime _latest_rtt   = 0;
  ink_hrtime _smoothed_rtt = 0;
  ink_hrtime _rttvar       = 0;
  ink_hrtime _min_rtt      = INT64_MAX;

  // config
  ink_hrtime _k_granularity = 0;
  ink_hrtime _k_initial_rtt = 0;
};
