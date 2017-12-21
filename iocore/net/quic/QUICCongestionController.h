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

#include <set>
#include "QUICTypes.h"
#include "QUICFrameHandler.h"

// TODO Implement congestion controll.
// Congestion controller will be required after the 2nd implementation draft.
class QUICCongestionController : public QUICFrameHandler
{
public:
  virtual std::vector<QUICFrameType> interests() override;
  virtual QUICErrorUPtr handle_frame(std::shared_ptr<const QUICFrame>) override;

  void on_packet_sent(size_t sent_bytes);
  void on_packet_acked(QUICPacketNumber acked_packet_number);
  virtual void on_packets_lost(std::set<QUICPacketNumber> packets);
  void on_rto_verified();
  void on_retransmission_timeout_verified();

private:
};
