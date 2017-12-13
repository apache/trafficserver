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

#include <ts/Diags.h>
#include <QUICCongestionController.h>

static constexpr char tag[] = "quic_congestion_controller";

std::vector<QUICFrameType>
QUICCongestionController::interests()
{
  return {QUICFrameType::ACK, QUICFrameType::STREAM};
}

QUICErrorUPtr
QUICCongestionController::handle_frame(std::shared_ptr<const QUICFrame> frame)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  switch (frame->type()) {
  case QUICFrameType::STREAM:
  case QUICFrameType::ACK:
    break;
  default:
    Debug(tag, "Unexpected frame type: %02x", static_cast<unsigned int>(frame->type()));
    ink_assert(false);
    break;
  }

  return error;
}

void
QUICCongestionController::on_packet_sent()
{
}

void
QUICCongestionController::on_packet_acked()
{
}

void
QUICCongestionController::on_packets_lost(std::set<QUICPacketNumber> packets)
{
}

void
QUICCongestionController::on_rto_verified()
{
}
