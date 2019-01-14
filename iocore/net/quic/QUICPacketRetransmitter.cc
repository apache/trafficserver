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

#include "QUICPacketRetransmitter.h"
#include "QUICDebugNames.h"

void
QUICPacketRetransmitter::retransmit_packet(const QUICPacket &packet)
{
  ink_assert(packet.type() != QUICPacketType::VERSION_NEGOTIATION && packet.type() != QUICPacketType::UNINITIALIZED);

  // Get payload from a header because packet.payload() is encrypted
  uint16_t size          = packet.header().payload_size();
  const uint8_t *payload = packet.header().payload();

  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();
  uint16_t cursor     = 0;

  while (cursor < size) {
    frame = QUICFrameFactory::create(payload + cursor, size - cursor);
    cursor += frame->size();

    switch (frame->type()) {
    case QUICFrameType::PADDING:
    case QUICFrameType::ACK:
    case QUICFrameType::PATH_CHALLENGE:
    case QUICFrameType::PATH_RESPONSE:
    case QUICFrameType::RST_STREAM:
    case QUICFrameType::BLOCKED:
    case QUICFrameType::STREAM_BLOCKED:
    case QUICFrameType::MAX_DATA:
    case QUICFrameType::MAX_STREAM_DATA:
    case QUICFrameType::NEW_TOKEN:
    case QUICFrameType::PING:
    case QUICFrameType::STOP_SENDING:
    case QUICFrameType::CONNECTION_CLOSE:
    case QUICFrameType::APPLICATION_CLOSE:
    case QUICFrameType::STREAM:
    case QUICFrameType::CRYPTO:
      break;
    default:
      frame     = QUICFrameFactory::create_retransmission_frame(frame->clone(), packet);
      int index = static_cast<int>(QUICTypeUtil::encryption_level(packet.type()));
      this->_retransmission_frames[index].push(std::move(frame));
      break;
    }
  }
}

void
QUICPacketRetransmitter::reset()
{
  for (auto level : QUIC_ENCRYPTION_LEVELS) {
    int index = static_cast<int>(level);
    while (!this->_retransmission_frames[index].empty()) {
      this->_retransmission_frames[index].pop();
    }
  }
}

bool
QUICPacketRetransmitter::will_generate_frame(QUICEncryptionLevel level)
{
  return !this->_retransmission_frames[static_cast<int>(level)].empty();
}

QUICFrameUPtr
QUICPacketRetransmitter::generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size)
{
  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();
  int index           = static_cast<int>(level);
  if (!this->_retransmission_frames[index].empty()) {
    frame = std::move(this->_retransmission_frames[index].front());
    this->_retransmission_frames[index].pop();

    if (frame->size() > maximum_frame_size) {
      auto new_frame = QUICFrameFactory::split_frame(frame.get(), maximum_frame_size);
      if (new_frame) {
        this->_retransmission_frames[index].push(std::move(new_frame));
      } else {
        // failed to split frame return nullptr
        this->_retransmission_frames[index].push(std::move(frame));
        frame = QUICFrameFactory::create_null_frame();
      }
    }
  }
  return frame;
}
