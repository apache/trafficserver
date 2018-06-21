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
#include "QUICConfig.h"

// FIXME: workaround for coalescing packets
static constexpr int LONG_HDR_OFFSET_CONNECTION_ID = 6;

static bool
is_vn(QUICVersion v)
{
  return v == 0x0;
}

static size_t
long_hdr_pkt_len(uint8_t *buf, size_t len)
{
  uint8_t dcil, scil;
  QUICPacketLongHeader::dcil(dcil, buf, len);
  QUICPacketLongHeader::scil(scil, buf, len);

  size_t length_offset = LONG_HDR_OFFSET_CONNECTION_ID + dcil + scil;

  size_t length;
  uint8_t length_field_len;
  QUICPacketLongHeader::length(length, &length_field_len, buf, len);

  return length_offset + length_field_len + length;
}

QUICPacketReceiveQueue::QUICPacketReceiveQueue(QUICPacketFactory &packet_factory, QUICPacketNumberProtector &pn_protector)
  : _packet_factory(packet_factory), _pn_protector(pn_protector)
{
}

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

  if (QUICInvariants::is_long_header(this->_payload.get())) {
    uint8_t *buf         = this->_payload.get() + this->_offset;
    size_t remaining_len = this->_payload_len - this->_offset;

    if (QUICInvariants::is_long_header(buf)) {
      QUICVersion version;
      QUICPacketLongHeader::version(version, buf, remaining_len);
      if (is_vn(version)) {
        pkt_len = remaining_len;
      } else if (!QUICTypeUtil::is_supported_version(version)) {
        result  = QUICPacketCreationResult::UNSUPPORTED;
        pkt_len = remaining_len;
      } else {
        pkt_len = long_hdr_pkt_len(this->_payload.get() + this->_offset, remaining_len);
      }
    } else {
      pkt_len = remaining_len;
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

  if (this->_unprotect_packet_number(pkt.get(), pkt_len)) {
    quic_packet = this->_packet_factory.create(this->_from, std::move(pkt), pkt_len, this->_largest_received_packet_number, result);
  } else {
    result = QUICPacketCreationResult::FAILED;
  }

  if (udp_packet) {
    udp_packet->free();
  }

  switch (result) {
  case QUICPacketCreationResult::NOT_READY:
    // FIXME: unordered packet should be buffered and retried
    if (this->_queue.size > 0) {
      result = QUICPacketCreationResult::IGNORED;
    }

    break;
  case QUICPacketCreationResult::UNSUPPORTED:
    // do nothing - if the packet is unsupported version, we don't know packet number
    break;
  default:
    if (quic_packet && quic_packet->packet_number() > this->_largest_received_packet_number) {
      this->_largest_received_packet_number = quic_packet->packet_number();
    }
  }

  return quic_packet;
}

uint32_t
QUICPacketReceiveQueue::size()
{
  return this->_queue.size;
}

void
QUICPacketReceiveQueue::reset()
{
  this->_largest_received_packet_number = 0;
}

bool
QUICPacketReceiveQueue::_unprotect_packet_number(uint8_t *packet, size_t packet_len)
{
  size_t pn_offset             = 0;
  uint8_t pn_len               = 4;
  size_t sample_offset         = 0;
  uint8_t sample_len           = 0;
  constexpr int aead_expansion = 16; // Currently, AEAD expansion (which is probably AEAD tag) length is always 16
  int connection_id_len        = QUICConfigParams::scid_len();
  QUICKeyPhase phase;

  if (QUICInvariants::is_long_header(packet)) {
    QUICPacketType type;
    QUICPacketLongHeader::type(type, packet, packet_len);
    switch (type) {
    case QUICPacketType::ZERO_RTT_PROTECTED:
      phase = QUICKeyPhase::ZERORTT;
      break;
    default:
      phase = QUICKeyPhase::CLEARTEXT;
      break;
    }

    uint8_t dcil, scil;
    size_t payload_length;
    uint8_t payload_length_field_len;
    if (!QUICPacketLongHeader::dcil(dcil, packet, packet_len) || !QUICPacketLongHeader::scil(scil, packet, packet_len) ||
        !QUICPacketLongHeader::payload_length(payload_length, &payload_length_field_len, packet, packet_len)) {
      return false;
    }
    pn_offset = 6 + dcil + scil + payload_length_field_len;
  } else {
    QUICPacketShortHeader::key_phase(phase, packet, packet_len);
    pn_offset = 1 + connection_id_len;
  }
  sample_offset = std::min(pn_offset + 4, packet_len - aead_expansion);
  sample_len    = 16; // On draft-12, the length is always 16 (See 5.6.1 and 5.6.2)

  uint8_t unprotected_pn[4]  = {0};
  uint8_t unprotected_pn_len = 0;
  if (!this->_pn_protector.unprotect(unprotected_pn, unprotected_pn_len, packet + pn_offset, pn_len, packet + sample_offset,
                                     phase)) {
    return false;
  }
  unprotected_pn_len = QUICTypeUtil::read_QUICPacketNumberLen(unprotected_pn);
  memcpy(packet + pn_offset, unprotected_pn, unprotected_pn_len);
  return true;
}
