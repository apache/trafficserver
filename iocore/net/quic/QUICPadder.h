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
#include "QUICFrameHandler.h"
#include "QUICFrameGenerator.h"

#include "I_Lock.h"

class QUICPadder : public QUICFrameOnceGenerator
{
public:
  QUICPadder(NetVConnectionContext_t context) : _mutex(new_ProxyMutex()), _context(context) {}

  void request(QUICEncryptionLevel level);
  void cancel(QUICEncryptionLevel level);
  uint64_t count(QUICEncryptionLevel level);
  void set_av_token_len(uint32_t len);

private:
  uint32_t _minimum_quic_packet_size();

  // QUICFrameGenerator
  bool _will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting) override;
  QUICFrame *_generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                             size_t current_packet_size) override;

  Ptr<ProxyMutex> _mutex;
  std::random_device _rnd;

  uint32_t _av_token_len = 0;
  // Initial, 0/1-RTT, and Handshake
  uint64_t _need_to_fire[4]        = {0};
  NetVConnectionContext_t _context = NET_VCONNECTION_OUT;
};
