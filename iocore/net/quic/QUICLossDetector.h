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
#include "P_Net.h"
#include "QUICTypes.h"
#include "QUICPacket.h"
#include "QUICFrame.h"
#include "QUICFrameHandler.h"
#include "QUICPacketTransmitter.h"

class QUICLossDetector : public Continuation, public QUICFrameHandler
{
public:
  QUICLossDetector(QUICPacketTransmitter *transmitter);

  int event_handler(int event, Event *edata);

  std::vector<QUICFrameType> interests() override;
  virtual QUICErrorUPtr handle_frame(std::shared_ptr<const QUICFrame>) override;
  void on_packet_sent(QUICPacketUPtr packet);
  QUICPacketNumber largest_acked_packet_number();

private:
  QUICConnectionId _connection_id = 0;

  struct PacketInfo {
    QUICPacketNumber packet_number;
    ink_hrtime time;
    bool retransmittable;
    bool handshake;
    size_t bytes;
    QUICPacketUPtr packet;
  };

  bool _time_loss_detection = false;

  // TODO QUICCongestionController *cc = nullptr;

  // 3.2.1.  Constants of interest
  uint32_t _MAX_TLPS               = 2;
  uint32_t _REORDERING_THRESHOLD   = 3;
  double _TIME_REORDERING_FRACTION = 1 / 8;
  ink_hrtime _MIN_TLP_TIMEOUT      = HRTIME_MSECONDS(10);
  ink_hrtime _MIN_RTO_TIMEOUT      = HRTIME_MSECONDS(200);
  ink_hrtime _DELAYED_ACK_TIMEOUT  = HRTIME_MSECONDS(25);
  ink_hrtime _DEFAULT_INITIAL_RTT  = HRTIME_MSECONDS(100);

  // 3.2.2.  Variables of interest
  Action *_loss_detection_alarm      = nullptr;
  uint32_t _handshake_count          = 0;
  uint32_t _tlp_count                = 0;
  uint32_t _rto_count                = 0;
  uint32_t _largest_sent_before_rto  = 0;
  uint32_t _largest_sent_packet      = 0;
  uint32_t _largest_acked_packet     = 0;
  uint32_t _time_of_last_sent_packet = 0;
  ink_hrtime _latest_rtt             = 0;
  ink_hrtime _smoothed_rtt           = 0;
  ink_hrtime _rttvar                 = 0;
  uint32_t _reordering_threshold;
  double _time_reordering_fraction;
  ink_hrtime _loss_time = 0;
  std::map<QUICPacketNumber, std::unique_ptr<PacketInfo>> _sent_packets;

  uint32_t _handshake_outstanding       = 0;
  uint32_t _retransmittable_outstanding = 0;
  void _decrement_packet_count(QUICPacketNumber packet_number);

  /*
   * Because this alarm will be reset on every packet transmission, to reduce number of events,
   * Loss Detector uses schedule_every() and checks if it has to be triggered.
   */
  ink_hrtime _loss_detection_alarm_at = 0;

  void _on_packet_sent(QUICPacketNumber packet_number, bool is_retransmittable, bool is_handshake, size_t sent_bytes,
                       QUICPacketUPtr packet);
  void _on_ack_received(const std::shared_ptr<const QUICAckFrame> &ack_frame);
  void _on_packet_acked(QUICPacketNumber acked_packet_number);
  void _update_rtt(ink_hrtime latest_rtt);
  void _detect_lost_packets(QUICPacketNumber largest_acked);
  void _set_loss_detection_alarm();
  void _on_loss_detection_alarm();

  std::set<QUICPacketNumber> _determine_newly_acked_packets(const QUICAckFrame &ack_frame);

  void _retransmit_handshake_packets();

  QUICPacketTransmitter *_transmitter = nullptr;
};
