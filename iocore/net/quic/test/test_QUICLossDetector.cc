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

#include "catch.hpp"

#include "QUICLossDetector.h"
#include "QUICPacketFactory.h"
#include "QUICAckFrameCreator.h"
#include "QUICEvents.h"
#include "QUICPacketFactory.h"
#include "QUICAckFrameCreator.h"
#include "Mock.h"
#include "tscore/ink_hrtime.h"

TEST_CASE("QUICLossDetector_Loss", "[quic]")
{
  MockQUICPacketProtectionKeyInfo pp_key_info;
  pp_key_info.set_encryption_key_available(QUICKeyPhase::PHASE_0);

  QUICPacketFactory pf(pp_key_info);
  QUICRTTMeasure rtt_measure;

  QUICAckFrameManager afm;
  QUICConnectionId connection_id = {reinterpret_cast<const uint8_t *>("\x01"), 1};
  MockQUICContext context;
  QUICPinger pinger;
  QUICPadder padder(NetVConnectionContext_t::NET_VCONNECTION_IN);
  MockQUICCongestionController cc;
  QUICLossDetector detector(context, &cc, &rtt_measure, &pinger, &padder);
  Ptr<IOBufferBlock> payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  payload->alloc(iobuffer_size_to_index(512, BUFFER_SIZE_INDEX_32K));
  size_t payload_len    = 512;
  QUICPacketUPtr packet = QUICPacketFactory::create_null_packet();
  QUICAckFrame *frame   = nullptr;

  SECTION("Handshake")
  {
    MockQUICFrameGenerator g;
    // Check initial state
    uint8_t frame_buffer[1024] = {0};
    CHECK(g.lost_frame_count == 0);
    QUICFrame *ping_frame = g.generate_frame(frame_buffer, QUICEncryptionLevel::HANDSHAKE, 4, UINT16_MAX, 0, 0);

    uint8_t raw[4];
    size_t len             = 0;
    Ptr<IOBufferBlock> ibb = ping_frame->to_io_buffer_block(sizeof(raw));
    for (auto b = ibb; b; b = b->next) {
      memcpy(raw + len, b->start(), b->size());
      len += b->size();
    }
    CHECK(len < 4);

    // Send SERVER_CLEARTEXT (Handshake message)
    payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(sizeof(raw), BUFFER_SIZE_INDEX_32K));
    memcpy(payload->start(), raw, sizeof(raw));
    payload->fill(sizeof(raw));

    QUICHandshakePacket *handshake_packet = new QUICHandshakePacket(
      0x00112233, {reinterpret_cast<const uint8_t *>("\xff\xdd\xbb\x99\x77\x55\x33\x11"), 8},
      {reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8}, sizeof(raw), 0, true, true, false);
    handshake_packet->attach_payload(payload, true);
    QUICPacketUPtr packet = QUICPacketUPtr(handshake_packet, [](QUICPacket *p) { delete p; });
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{
      packet->packet_number(),
      packet->is_ack_eliciting(),
      true,
      packet->size(),
      ink_get_hrtime(),
      packet->type(),
      {},
      QUICPacketNumberSpace::HANDSHAKE,
    }));
    ink_hrtime_sleep(HRTIME_MSECONDS(1000));
    CHECK(g.lost_frame_count >= 0);

    // Receive ACK
    QUICAckFrame frame(0x01, 20, 0);
    frame.ack_block_section()->add_ack_block({0, 1ULL});
    detector.handle_frame(QUICEncryptionLevel::INITIAL, frame);
    ink_hrtime_sleep(HRTIME_MSECONDS(1500));
    int retransmit_count = g.lost_frame_count;
    ink_hrtime_sleep(HRTIME_MSECONDS(1500));
    CHECK(g.lost_frame_count == retransmit_count);
  }

  SECTION("1-RTT")
  {
    // Send packet (1) to (7)
    QUICPacketNumberSpace pn_space = QUICPacketNumberSpace::APPLICATION_DATA;
    QUICEncryptionLevel level      = QUICEncryptionLevel::ONE_RTT;
    payload                        = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(payload_len, BUFFER_SIZE_INDEX_32K));
    payload->fill(payload_len);
    uint8_t packet_1_buf[QUICPacket::MAX_INSTANCE_SIZE];
    QUICPacketUPtr packet1 = pf.create_short_header_packet(
      packet_1_buf, connection_id, detector.largest_acked_packet_number(pn_space), payload, payload_len, true, false);
    REQUIRE(packet1 != nullptr);
    payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(payload_len, BUFFER_SIZE_INDEX_32K));
    payload->fill(payload_len);
    uint8_t packet_2_buf[QUICPacket::MAX_INSTANCE_SIZE];
    QUICPacketUPtr packet2 = pf.create_short_header_packet(
      packet_2_buf, connection_id, detector.largest_acked_packet_number(pn_space), payload, payload_len, true, false);
    payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(payload_len, BUFFER_SIZE_INDEX_32K));
    payload->fill(payload_len);
    uint8_t packet_3_buf[QUICPacket::MAX_INSTANCE_SIZE];
    QUICPacketUPtr packet3 = pf.create_short_header_packet(
      packet_3_buf, connection_id, detector.largest_acked_packet_number(pn_space), payload, payload_len, true, false);
    payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(payload_len, BUFFER_SIZE_INDEX_32K));
    payload->fill(payload_len);
    uint8_t packet_4_buf[QUICPacket::MAX_INSTANCE_SIZE];
    QUICPacketUPtr packet4 = pf.create_short_header_packet(
      packet_4_buf, connection_id, detector.largest_acked_packet_number(pn_space), payload, payload_len, true, false);
    payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(payload_len, BUFFER_SIZE_INDEX_32K));
    payload->fill(payload_len);
    uint8_t packet_5_buf[QUICPacket::MAX_INSTANCE_SIZE];
    QUICPacketUPtr packet5 = pf.create_short_header_packet(
      packet_5_buf, connection_id, detector.largest_acked_packet_number(pn_space), payload, payload_len, true, false);
    payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(payload_len, BUFFER_SIZE_INDEX_32K));
    payload->fill(payload_len);
    uint8_t packet_6_buf[QUICPacket::MAX_INSTANCE_SIZE];
    QUICPacketUPtr packet6 = pf.create_short_header_packet(
      packet_6_buf, connection_id, detector.largest_acked_packet_number(pn_space), payload, payload_len, true, false);
    payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(payload_len, BUFFER_SIZE_INDEX_32K));
    payload->fill(payload_len);
    uint8_t packet_7_buf[QUICPacket::MAX_INSTANCE_SIZE];
    QUICPacketUPtr packet7 = pf.create_short_header_packet(
      packet_7_buf, connection_id, detector.largest_acked_packet_number(pn_space), payload, payload_len, true, false);
    payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(payload_len, BUFFER_SIZE_INDEX_32K));
    payload->fill(payload_len);
    uint8_t packet_8_buf[QUICPacket::MAX_INSTANCE_SIZE];
    QUICPacketUPtr packet8 = pf.create_short_header_packet(
      packet_8_buf, connection_id, detector.largest_acked_packet_number(pn_space), payload, payload_len, true, false);
    payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(payload_len, BUFFER_SIZE_INDEX_32K));
    payload->fill(payload_len);
    uint8_t packet_9_buf[QUICPacket::MAX_INSTANCE_SIZE];
    QUICPacketUPtr packet9 = pf.create_short_header_packet(
      packet_9_buf, connection_id, detector.largest_acked_packet_number(pn_space), payload, payload_len, true, false);
    payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(payload_len, BUFFER_SIZE_INDEX_32K));
    payload->fill(payload_len);
    uint8_t packet_10_buf[QUICPacket::MAX_INSTANCE_SIZE];
    QUICPacketUPtr packet10 = pf.create_short_header_packet(
      packet_10_buf, connection_id, detector.largest_acked_packet_number(pn_space), payload, payload_len, true, false);

    QUICPacketNumber pn1  = packet1->packet_number();
    QUICPacketNumber pn2  = packet2->packet_number();
    QUICPacketNumber pn3  = packet3->packet_number();
    QUICPacketNumber pn4  = packet4->packet_number();
    QUICPacketNumber pn5  = packet5->packet_number();
    QUICPacketNumber pn6  = packet6->packet_number();
    QUICPacketNumber pn7  = packet7->packet_number();
    QUICPacketNumber pn8  = packet8->packet_number();
    QUICPacketNumber pn9  = packet9->packet_number();
    QUICPacketNumber pn10 = packet10->packet_number();

    QUICSentPacketInfoUPtr packet_info = nullptr;
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{packet1->packet_number(),
                                                                          packet1->is_ack_eliciting(),
                                                                          true,
                                                                          packet1->size(),
                                                                          ink_get_hrtime(),
                                                                          packet1->type(),
                                                                          {},
                                                                          pn_space}));
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{packet2->packet_number(),
                                                                          packet2->is_ack_eliciting(),
                                                                          true,
                                                                          packet2->size(),
                                                                          ink_get_hrtime(),
                                                                          packet2->type(),
                                                                          {},
                                                                          pn_space}));
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{packet3->packet_number(),
                                                                          packet3->is_ack_eliciting(),
                                                                          true,
                                                                          packet3->size(),
                                                                          ink_get_hrtime(),
                                                                          packet3->type(),
                                                                          {},
                                                                          pn_space}));
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{packet4->packet_number(),
                                                                          packet4->is_ack_eliciting(),
                                                                          true,
                                                                          packet4->size(),
                                                                          ink_get_hrtime(),
                                                                          packet4->type(),
                                                                          {},
                                                                          pn_space}));
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{packet5->packet_number(),
                                                                          packet5->is_ack_eliciting(),
                                                                          true,
                                                                          packet5->size(),
                                                                          ink_get_hrtime(),
                                                                          packet5->type(),
                                                                          {},
                                                                          pn_space}));
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{packet6->packet_number(),
                                                                          packet6->is_ack_eliciting(),
                                                                          true,
                                                                          packet6->size(),
                                                                          ink_get_hrtime(),
                                                                          packet6->type(),
                                                                          {},
                                                                          pn_space}));
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{packet7->packet_number(),
                                                                          packet6->is_ack_eliciting(),
                                                                          true,
                                                                          packet7->size(),
                                                                          ink_get_hrtime(),
                                                                          packet7->type(),
                                                                          {},
                                                                          pn_space}));
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{packet8->packet_number(),
                                                                          packet6->is_ack_eliciting(),
                                                                          true,
                                                                          packet8->size(),
                                                                          ink_get_hrtime(),
                                                                          packet8->type(),
                                                                          {},
                                                                          pn_space}));
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{packet9->packet_number(),
                                                                          packet6->is_ack_eliciting(),
                                                                          true,
                                                                          packet9->size(),
                                                                          ink_get_hrtime(),
                                                                          packet9->type(),
                                                                          {},
                                                                          pn_space}));
    detector.on_packet_sent(QUICSentPacketInfoUPtr(new QUICSentPacketInfo{packet10->packet_number(),
                                                                          packet10->is_ack_eliciting(),
                                                                          true,
                                                                          packet10->size(),
                                                                          ink_get_hrtime(),
                                                                          packet10->type(),
                                                                          {},
                                                                          pn_space}));

    ink_hrtime_sleep(HRTIME_MSECONDS(2000));
    // Receive an ACK for (1) (4) (5) (7) (8) (9)
    afm.update(level, pn1, payload_len, false);
    afm.update(level, pn4, payload_len, false);
    afm.update(level, pn5, payload_len, false);
    afm.update(level, pn7, payload_len, false);
    afm.update(level, pn8, payload_len, false);
    afm.update(level, pn9, payload_len, false);
    afm.update(level, pn10, payload_len, false);
    uint8_t buf[QUICFrame::MAX_INSTANCE_SIZE];
    QUICFrame *x = afm.generate_frame(buf, level, 2048, 2048, 0, 0);
    frame        = static_cast<QUICAckFrame *>(x);
    ink_hrtime_sleep(HRTIME_MSECONDS(1000));
    detector.handle_frame(level, *frame);

    // Lost because of packet_threshold.
    CHECK(cc.lost_packets.size() == 3);

    CHECK(cc.lost_packets.find(pn1) == cc.lost_packets.end());
    CHECK(cc.lost_packets.find(pn2) != cc.lost_packets.end());
    CHECK(cc.lost_packets.find(pn3) != cc.lost_packets.end());
    CHECK(cc.lost_packets.find(pn4) == cc.lost_packets.end());
    CHECK(cc.lost_packets.find(pn5) == cc.lost_packets.end());
    CHECK(cc.lost_packets.find(pn6) != cc.lost_packets.end());
    CHECK(cc.lost_packets.find(pn7) == cc.lost_packets.end());
    CHECK(cc.lost_packets.find(pn8) == cc.lost_packets.end());
    CHECK(cc.lost_packets.find(pn9) == cc.lost_packets.end());
    CHECK(cc.lost_packets.find(pn9) == cc.lost_packets.end());
    x->~QUICFrame();
  }
}

TEST_CASE("QUICLossDetector_HugeGap", "[quic]")
{
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICRTTMeasure rtt_measure;
  MockQUICContext context;
  QUICPinger pinger;
  QUICPadder padder(NetVConnectionContext_t::NET_VCONNECTION_IN);
  MockQUICCongestionController cc;
  QUICLossDetector detector(context, &cc, &rtt_measure, &pinger, &padder);

  auto t1           = ink_get_hrtime();
  QUICAckFrame *ack = QUICFrameFactory::create_ack_frame(frame_buf, 100000000, 100, 10000000);
  ack->ack_block_section()->add_ack_block({20000000, 30000000});
  detector.handle_frame(QUICEncryptionLevel::INITIAL, *ack);
  auto t2 = ink_get_hrtime();
  CHECK(t2 - t1 < HRTIME_MSECONDS(100));
  ack->~QUICAckFrame();
}
