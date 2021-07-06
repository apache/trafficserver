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

#include "QLog.h"
#include "QLogUtils.h"
#include "QUICPacket.h"
#include "QUICContext.h"

namespace QLog
{
class QLogListener : public QUICCallback
{
public:
  QLogListener(QUICContext &ctx, std::string odcid, std::string title = "", std::string desc = "") : _context(ctx)
  {
    this->_log.new_trace(odcid, title, desc); // initial new trace
  }

  void
  frame_recv_callback(QUICCallbackContext &, const QUICFrame &frame) override
  {
    this->_recv_frames.push_back(QLogFrameFactory::create(static_cast<const QUICFrame *>(&frame)));
  }

  void
  frame_packetize_callback(QUICCallbackContext &, const QUICFrame &frame) override
  {
    this->_send_frames.push_back(QLogFrameFactory::create(static_cast<const QUICFrame *>(&frame)));
  }

  void
  packet_send_callback(QUICCallbackContext &, const QUICPacket &packet) override
  {
    auto qe = std::make_unique<Transport::PacketSent>(PacketTypeToName(packet.type()), QUICPacketToLogPacket(packet));
    for (auto &it : this->_send_frames) {
      qe->append_frames(std::move(it));
    }
    this->_send_frames.clear();
    this->_log.last_trace().push_event(std::move(qe));
  };

  void
  packet_recv_callback(QUICCallbackContext &, const QUICPacket &packet) override
  {
    auto qe = std::make_unique<Transport::PacketReceived>(PacketTypeToName(packet.type()), QUICPacketToLogPacket(packet));
    for (auto &it : this->_recv_frames) {
      qe->append_frames(std::move(it));
    }
    this->_recv_frames.clear();
    this->_log.last_trace().push_event(std::move(qe));
  };

  void
  packet_lost_callback(QUICCallbackContext &, const QUICSentPacketInfo &packet) override
  {
    auto qe = std::make_unique<Recovery::PacketLost>(PacketTypeToName(packet.type), packet.packet_number);
    this->_log.last_trace().push_event(std::move(qe));
  };

  void
  cc_metrics_update_callback(QUICCallbackContext &, uint64_t congestion_window, uint64_t bytes_in_flight, uint64_t sshresh) override
  {
    auto qe = std::make_unique<Recovery::MetricsUpdated>();
    qe->set_congestion_window(static_cast<int>(congestion_window)).set_bytes_in_flight(bytes_in_flight).set_ssthresh(sshresh);
    this->_log.last_trace().push_event(std::move(qe));
  }

  void
  congestion_state_updated_callback(QUICCallbackContext &, QUICCongestionController::State state) override
  {
    if (state != this->_state) {
      this->_log.last_trace().push_event(std::make_unique<Recovery::CongestionStateUpdated>(CongestionStateConvert(state)));
      this->_state = state;
    }
  }

  void
  connection_close_callback(QUICCallbackContext &) override
  {
    this->_log.dump(this->_context.config()->qlog_dir());
  }

  Trace &
  last_trace()
  {
    return this->_log.last_trace();
  }

private:
  QUICCongestionController::State _state = QUICCongestionController::State::SLOW_START;
  std::vector<QLogFrameUPtr> _recv_frames;
  std::vector<QLogFrameUPtr> _send_frames;
  QLog _log;
  QUICContext &_context;
};

} // namespace QLog
