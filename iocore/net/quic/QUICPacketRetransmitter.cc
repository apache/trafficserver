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
  Debug("quic_con", "Retransmit packet #%" PRIu64 " type %s", packet.packet_number(), QUICDebugNames::packet_type(packet.type()));
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
      break;
    default:
      frame = QUICFrameFactory::create_retransmission_frame(std::move(frame), packet);
      // FIXME We should probably reframe STREAM frames so that it can fit in new packets
      this->_retransmission_frames.push(std::move(frame));
      break;
    }
  }
}

bool
QUICPacketRetransmitter::will_generate_frame()
{
  return !this->_retransmission_frames.empty();
}

QUICFrameUPtr
QUICPacketRetransmitter::generate_frame(uint16_t connection_credit, uint16_t maximum_frame_size)
{
  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();
  if (!this->_retransmission_frames.empty()) {
    frame = std::move(this->_retransmission_frames.front());
    this->_retransmission_frames.pop();
  }

  if (maximum_frame_size >= frame->size()) {
    return frame;
  }

  auto new_frame = QUICFrameFactory::split_frame(frame.get(), maximum_frame_size);
  if (new_frame) {
    this->_retransmission_frames.push(std::move(new_frame));
    return frame;
  }

  // failed to split frame return nullptr
  this->_retransmission_frames.push(std::move(frame));
  return QUICFrameFactory::create_null_frame();
}
