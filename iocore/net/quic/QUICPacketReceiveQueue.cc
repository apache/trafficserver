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

#include "QUICPacketReceiveQueue.h"

#include "QUICIntUtil.h"

// FIXME: workaround for coalescing packets
static constexpr int LONG_HDR_OFFSET_CONNECTION_ID = 6;
static constexpr int LONG_HDR_PKT_NUM_LEN          = 4;

static size_t
long_hdr_pkt_len(uint8_t *buf)
{
  uint8_t dcil = (buf[5] >> 4);
  if (dcil) {
    dcil += 3;
  }
  uint8_t scil = (buf[5] & 0x0F);
  if (scil) {
    scil += 3;
  }
  size_t offset = LONG_HDR_OFFSET_CONNECTION_ID;
  offset += dcil;
  offset += scil;

  size_t payload_len = QUICIntUtil::read_QUICVariableInt(buf + offset);
  offset += QUICVariableInt::size(buf + offset);
  offset += LONG_HDR_PKT_NUM_LEN;
  offset += payload_len;

  return offset;
}

QUICPacketReceiveQueue::QUICPacketReceiveQueue(QUICPacketFactory &packet_factory) : _packet_factory(packet_factory) {}

void
QUICPacketReceiveQueue::enqueue(UDPPacket *packet)
{
  this->_queue.enqueue(packet);
}

QUICPacketUPtr
QUICPacketReceiveQueue::dequeue(QUICPacketCreationResult &result)
{
  QUICPacketUPtr quic_packet = QUICPacketFactory::create_null_packet();
  UDPPacket *udp_packet      = nullptr;

  // FIXME: avoid this copy
  // Copy payload of UDP packet to this->_payload once
  if (!this->_payload) {
    udp_packet = this->_queue.dequeue();
    if (!udp_packet) {
      result = QUICPacketCreationResult::NO_PACKET;
      return quic_packet;
    }

    // Create a QUIC packet
    this->_from        = udp_packet->from;
    this->_payload_len = udp_packet->getPktLength();
    this->_payload     = ats_unique_malloc(this->_payload_len);
    IOBufferBlock *b   = udp_packet->getIOBlockChain();
    size_t written     = 0;
    while (b) {
      memcpy(this->_payload.get() + written, b->buf(), b->read_avail());
      written += b->read_avail();
      b = b->next.get();
    }
  }

  ats_unique_buf pkt = {nullptr, [](void *p) { ats_free(p); }};
  size_t pkt_len     = 0;

  if (QUICTypeUtil::has_long_header(this->_payload.get())) {
    if (QUICTypeUtil::has_long_header(this->_payload.get() + this->_offset)) {
      pkt_len = long_hdr_pkt_len(this->_payload.get() + this->_offset);
    } else {
      pkt_len = this->_payload_len - this->_offset;
    }

    if (pkt_len < this->_payload_len) {
      pkt = ats_unique_malloc(pkt_len);
      memcpy(pkt.get(), this->_payload.get() + this->_offset, pkt_len);
      this->_offset += pkt_len;

      if (this->_offset >= this->_payload_len) {
        this->_payload.release();
        this->_payload     = nullptr;
        this->_payload_len = 0;
        this->_offset      = 0;
      }
    } else {
      pkt                = std::move(this->_payload);
      pkt_len            = this->_payload_len;
      this->_payload     = nullptr;
      this->_payload_len = 0;
      this->_offset      = 0;
    }
  } else {
    pkt                = std::move(this->_payload);
    pkt_len            = this->_payload_len;
    this->_payload     = nullptr;
    this->_payload_len = 0;
    this->_offset      = 0;
  }

  quic_packet = this->_packet_factory.create(this->_from, std::move(pkt), pkt_len, this->largest_received_packet_number(), result);

  if (udp_packet) {
    udp_packet->free();
  }

  if (result == QUICPacketCreationResult::NOT_READY) {
    // FIXME: unordered packet should be buffered and retried
    if (this->_queue.size > 0) {
      result = QUICPacketCreationResult::IGNORED;
    }
  }

  if (quic_packet && quic_packet->packet_number() > this->_largest_received_packet_number) {
    this->_largest_received_packet_number = quic_packet->packet_number();
  }

  return quic_packet;
}

uint32_t
QUICPacketReceiveQueue::size()
{
  return this->_queue.size;
}

QUICPacketNumber
QUICPacketReceiveQueue::largest_received_packet_number()
{
  return this->_largest_received_packet_number;
}

void
QUICPacketReceiveQueue::reset()
{
  this->_largest_received_packet_number = 0;
}
