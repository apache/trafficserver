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

#include <vector>
#include "QUICTypes.h"
#include "QUICFrameHandler.h"
#include "QUICFrameGenerator.h"

#include "I_Lock.h"

class QUICPinger : public QUICFrameOnceGenerator
{
public:
  QUICPinger() : _mutex(new_ProxyMutex()) {}

  void request(QUICEncryptionLevel level);
  void cancel(QUICEncryptionLevel level);
  uint64_t count(QUICEncryptionLevel level);

private:
  // QUICFrameGenerator
  bool _will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting) override;
  QUICFrame *_generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                             size_t current_packet_size) override;

  bool _ack_eliciting_packet_out = false;

  Ptr<ProxyMutex> _mutex;
  uint64_t _need_to_fire[4] = {0}; // Initial, 0RTT, HANDSHAKE and 1RTT
};
