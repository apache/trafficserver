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

#include "QUICTypes.h"
#include "QUICContext.h"
#include "QUICCongestionController.h"

class QUICNewRenoCongestionController : public QUICCongestionController
{
public:
  QUICNewRenoCongestionController(QUICContext &context);
  virtual ~QUICNewRenoCongestionController() {}

  void on_packet_sent(size_t bytes_sent) override;
  void on_packets_acked(const std::vector<QUICSentPacketInfoUPtr> &packets) override;
  virtual void on_packets_lost(const std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> &packets) override;
  void on_packet_number_space_discarded(size_t bytes_in_flight) override;
  void process_ecn(const QUICAckFrame &ack, QUICPacketNumberSpace pn_space, ink_hrtime largest_acked_packet_time_sent) override;
  uint32_t credit() const override;
  void reset() override;

  // Debug
  uint32_t bytes_in_flight() const override;
  uint32_t congestion_window() const override;
  uint32_t current_ssthresh() const override;

  void add_extra_credit() override;

private:
  Ptr<ProxyMutex> _cc_mutex;
  uint32_t _extra_packets_count = 0;
  QUICContext &_context;
  bool _check_credit() const;

  // Appendix B.  Congestion Control Pseudocode
  bool _in_congestion_recovery(ink_hrtime sent_time) const;
  void _congestion_event(ink_hrtime sent_time);
  bool _in_persistent_congestion(const std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> &lost_packets,
                                 const QUICSentPacketInfoUPtr &largest_lost_packet);
  bool _is_app_or_flow_control_limited();
  void _maybe_send_one_packet();
  bool _are_all_packets_lost(const std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> &lost_packets,
                             const QUICSentPacketInfoUPtr &largest_lost_packet, ink_hrtime period) const;

  // Recovery B.1. Constants of interest
  // Values will be loaded from records.config via QUICConfig at constructor
  uint32_t _k_initial_window                  = 0;
  uint32_t _k_minimum_window                  = 0;
  float _k_loss_reduction_factor              = 0.0;
  uint32_t _k_persistent_congestion_threshold = 0;

  // B.2. Variables of interest
  uint32_t _max_datagram_size                     = 0;
  uint32_t _ecn_ce_counters[QUIC_N_PACKET_SPACES] = {0};
  uint32_t _bytes_in_flight                       = 0;
  uint32_t _congestion_window                     = 0;
  ink_hrtime _congestion_recovery_start_time      = 0;
  uint32_t _ssthresh                              = UINT32_MAX;
};
