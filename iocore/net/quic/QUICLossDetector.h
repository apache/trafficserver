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

#include "../../eventsystem/I_EventSystem.h"
#include "../../eventsystem/I_Action.h"
#include "ts/ink_hrtime.h"
#include "I_VConnection.h"
#include "QUICTypes.h"
#include "QUICPacket.h"
#include "QUICFrame.h"
#include "QUICFrameHandler.h"
#include "QUICPacketTransmitter.h"
#include "QUICConnection.h"

class QUICLossDetector;

struct PacketInfo {
  QUICPacketNumber packet_number;
  ink_hrtime time;
  bool ack_only;
  bool handshake;
  size_t bytes;
  QUICPacketUPtr packet;
};

class QUICRTTProvider
{
public:
  virtual ink_hrtime smoothed_rtt() const = 0;
};

class QUICCongestionController
{
public:
  QUICCongestionController(QUICConnectionInfoProvider *info);
  virtual ~QUICCongestionController() {}
  void on_packet_sent(size_t bytes_sent);
  void on_packet_acked(QUICPacketNumber acked_packet_number, size_t acked_packet_size);
  virtual void on_packets_lost(std::map<QUICPacketNumber, PacketInfo *> &packets);
  void on_retransmission_timeout_verified();
  bool check_credit() const;

  // Debug
  uint32_t credit() const;
  uint32_t bytes_in_flight() const;
  uint32_t congestion_window() const;
  uint32_t current_ssthresh() const;

private:
  // 4.7.1. Constants of interest (draft-10)
  // Values will be loaded from records.config via QUICConfig at constructor
  uint32_t _k_default_mss        = 0;
  uint32_t _k_initial_window     = 0;
  uint32_t _k_minimum_window     = 0;
  float _k_loss_reduction_factor = 0.0;

  // 4.7.2. Variables of interest
  uint32_t _bytes_in_flight         = 0;
  uint32_t _congestion_window       = 0;
  QUICPacketNumber _end_of_recovery = 0;
  uint32_t _ssthresh                = UINT32_MAX;

  QUICConnectionInfoProvider *_info = nullptr;

  bool _in_recovery(QUICPacketNumber packet_number);
};

class QUICLossDetector : public Continuation, public QUICFrameHandler, public QUICRTTProvider
{
public:
  QUICLossDetector(QUICPacketTransmitter *transmitter, QUICConnectionInfoProvider *info, QUICCongestionController *cc);
  ~QUICLossDetector();

  int event_handler(int event, Event *edata);

  std::vector<QUICFrameType> interests() override;
  virtual QUICErrorUPtr handle_frame(std::shared_ptr<const QUICFrame>) override;
  void on_packet_sent(QUICPacketUPtr packet);
  QUICPacketNumber largest_acked_packet_number();
  void reset();
  ink_hrtime current_rto_period();
  ink_hrtime smoothed_rtt() const override;

private:
  Ptr<ProxyMutex> _loss_detection_mutex;

  // TODO QUICCongestionController *cc = nullptr;

  // 3.4.1.  Constants of interest (draft-10)
  // Values will be loaded from records.config via QUICConfig at constructor
  uint32_t _k_max_tlps              = 0;
  uint32_t _k_reordering_threshold  = 0;
  float _k_time_reordering_fraction = 0.0;
  bool _k_using_time_loss_detection = false;
  ink_hrtime _k_min_tlp_timeout     = 0;
  ink_hrtime _k_min_rto_timeout     = 0;
  ink_hrtime _k_delayed_ack_timeout = 0;
  ink_hrtime _k_default_initial_rtt = 0;

  // 3.4.2.  Variables of interest (draft-10)
  // Keep the order as the same as the spec so that we can see the difference easily.
  Action *_loss_detection_alarm        = nullptr;
  uint32_t _handshake_count            = 0;
  uint32_t _tlp_count                  = 0;
  uint32_t _rto_count                  = 0;
  uint32_t _largest_sent_before_rto    = 0;
  ink_hrtime _time_of_last_sent_packet = 0;
  uint32_t _largest_sent_packet        = 0;
  uint32_t _largest_acked_packet       = 0;
  ink_hrtime _latest_rtt               = 0;
  ink_hrtime _smoothed_rtt             = 0;
  ink_hrtime _rttvar                   = 0;
  ink_hrtime _min_rtt                  = 0;
  ink_hrtime _max_ack_delay            = 0;
  uint32_t _reordering_threshold       = 0;
  double _time_reordering_fraction     = 0.0;
  ink_hrtime _loss_time                = 0;
  std::map<QUICPacketNumber, std::unique_ptr<PacketInfo>> _sent_packets;

  // These are not defined on the spec but expected to be count
  // These counter have to be updated when inserting / erasing packets from _sent_packets with following functions.
  std::atomic<uint32_t> _handshake_outstanding;
  std::atomic<uint32_t> _retransmittable_outstanding;
  void _add_to_sent_packet_list(QUICPacketNumber packet_number, std::unique_ptr<PacketInfo> packet_info);
  void _remove_from_sent_packet_list(QUICPacketNumber packet_number);

  /*
   * Because this alarm will be reset on every packet transmission, to reduce number of events,
   * Loss Detector uses schedule_every() and checks if it has to be triggered.
   */
  ink_hrtime _loss_detection_alarm_at = 0;

  void _on_packet_sent(QUICPacketNumber packet_number, bool is_ack_only, bool is_handshake, size_t sent_bytes,
                       QUICPacketUPtr packet);
  void _on_ack_received(const std::shared_ptr<const QUICAckFrame> &ack_frame);
  void _on_packet_acked(QUICPacketNumber acked_packet_number, size_t acked_packet_size);
  void _update_rtt(ink_hrtime latest_rtt, ink_hrtime ack_delay, QUICPacketNumber largest_acked);
  void _detect_lost_packets(QUICPacketNumber largest_acked);
  void _set_loss_detection_alarm();
  void _on_loss_detection_alarm();
  void _retransmit_lost_packet(const QUICPacket &packet);

  std::set<QUICAckFrame::PacketNumberRange> _determine_newly_acked_packets(const QUICAckFrame &ack_frame);

  void _retransmit_handshake_packets();
  void _send_one_packet();
  void _send_two_packets();

  QUICPacketTransmitter *_transmitter = nullptr;
  QUICConnectionInfoProvider *_info   = nullptr;
  QUICCongestionController *_cc       = nullptr;
};
