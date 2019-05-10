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
#include "QUICEvents.h"
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
  MockQUICCCConfig cc_config;
  MockQUICLDConfig ld_config;
  MockQUICConnectionInfoProvider info;
  MockQUICCongestionController cc(&info, cc_config);
  QUICLossDetector detector(&info, &cc, &rtt_measure, ld_config);
  ats_unique_buf payload = ats_unique_malloc(512);
  size_t payload_len     = 512;
  QUICPacketUPtr packet  = QUICPacketFactory::create_null_packet();
  QUICAckFrame *frame    = nullptr;

  SECTION("Handshake")
  {
    MockQUICFrameGenerator g;
    // Check initial state
    uint8_t frame_buffer[1024] = {0};
    CHECK(g.lost_frame_count == 0);
    QUICFrame *ping_frame = g.generate_frame(frame_buffer, QUICEncryptionLevel::HANDSHAKE, 4, UINT16_MAX, 0);

    uint8_t raw[4];
    size_t len;
    CHECK(ping_frame->store(raw, &len, 10240) < 4);

    // Send SERVER_CLEARTEXT (Handshake message)
    ats_unique_buf payload = ats_unique_malloc(sizeof(raw));
    memcpy(payload.get(), raw, sizeof(raw));

    QUICPacketHeaderUPtr header =
      QUICPacketHeader::build(QUICPacketType::HANDSHAKE, QUICKeyPhase::HANDSHAKE,
                              {reinterpret_cast<const uint8_t *>("\xff\xdd\xbb\x99\x77\x55\x33\x11"), 8},
                              {reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8}, 0x00000001, 0, 0x00112233,
                              false, std::move(payload), sizeof(raw));
    QUICPacketUPtr packet = QUICPacketUPtr(new QUICPacket(std::move(header), std::move(payload), sizeof(raw), true, false),
                                           [](QUICPacket *p) { delete p; });
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{
      packet->packet_number(),
      Thread::get_hrtime(),
      packet->is_ack_eliciting(),
      packet->is_crypto_packet(),
      true,
      packet->size(),
      packet->type(),
      {},
      QUICPacketNumberSpace::Handshake,
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
    QUICPacketNumberSpace pn_space = QUICPacketNumberSpace::ApplicationData;
    QUICEncryptionLevel level      = QUICEncryptionLevel::ONE_RTT;
    payload                        = ats_unique_malloc(payload_len);
    QUICPacketUPtr packet1         = pf.create_protected_packet(connection_id, detector.largest_acked_packet_number(pn_space),
                                                        std::move(payload), payload_len, true, false);
    REQUIRE(packet1 != nullptr);
    payload                 = ats_unique_malloc(payload_len);
    QUICPacketUPtr packet2  = pf.create_protected_packet(connection_id, detector.largest_acked_packet_number(pn_space),
                                                        std::move(payload), payload_len, true, false);
    payload                 = ats_unique_malloc(payload_len);
    QUICPacketUPtr packet3  = pf.create_protected_packet(connection_id, detector.largest_acked_packet_number(pn_space),
                                                        std::move(payload), payload_len, true, false);
    payload                 = ats_unique_malloc(payload_len);
    QUICPacketUPtr packet4  = pf.create_protected_packet(connection_id, detector.largest_acked_packet_number(pn_space),
                                                        std::move(payload), payload_len, true, false);
    payload                 = ats_unique_malloc(payload_len);
    QUICPacketUPtr packet5  = pf.create_protected_packet(connection_id, detector.largest_acked_packet_number(pn_space),
                                                        std::move(payload), payload_len, true, false);
    payload                 = ats_unique_malloc(payload_len);
    QUICPacketUPtr packet6  = pf.create_protected_packet(connection_id, detector.largest_acked_packet_number(pn_space),
                                                        std::move(payload), payload_len, true, false);
    payload                 = ats_unique_malloc(payload_len);
    QUICPacketUPtr packet7  = pf.create_protected_packet(connection_id, detector.largest_acked_packet_number(pn_space),
                                                        std::move(payload), payload_len, true, false);
    payload                 = ats_unique_malloc(payload_len);
    QUICPacketUPtr packet8  = pf.create_protected_packet(connection_id, detector.largest_acked_packet_number(pn_space),
                                                        std::move(payload), payload_len, true, false);
    payload                 = ats_unique_malloc(payload_len);
    QUICPacketUPtr packet9  = pf.create_protected_packet(connection_id, detector.largest_acked_packet_number(pn_space),
                                                        std::move(payload), payload_len, true, false);
    payload                 = ats_unique_malloc(payload_len);
    QUICPacketUPtr packet10 = pf.create_protected_packet(connection_id, detector.largest_acked_packet_number(pn_space),
                                                         std::move(payload), payload_len, true, false);

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

    QUICPacketInfoUPtr packet_info = nullptr;
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{packet1->packet_number(),
                                                                  Thread::get_hrtime(),
                                                                  packet1->is_ack_eliciting(),
                                                                  packet1->is_crypto_packet(),
                                                                  true,
                                                                  packet1->size(),
                                                                  packet1->type(),
                                                                  {},
                                                                  pn_space}));
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{packet2->packet_number(),
                                                                  Thread::get_hrtime(),
                                                                  packet2->is_ack_eliciting(),
                                                                  packet2->is_crypto_packet(),
                                                                  true,
                                                                  packet2->size(),
                                                                  packet2->type(),
                                                                  {},
                                                                  pn_space}));
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{packet3->packet_number(),
                                                                  Thread::get_hrtime(),
                                                                  packet3->is_ack_eliciting(),
                                                                  packet3->is_crypto_packet(),
                                                                  true,
                                                                  packet3->size(),
                                                                  packet3->type(),
                                                                  {},
                                                                  pn_space}));
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{packet4->packet_number(),
                                                                  Thread::get_hrtime(),
                                                                  packet4->is_ack_eliciting(),
                                                                  packet4->is_crypto_packet(),
                                                                  true,
                                                                  packet4->size(),
                                                                  packet4->type(),
                                                                  {},
                                                                  pn_space}));
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{packet5->packet_number(),
                                                                  Thread::get_hrtime(),
                                                                  packet5->is_ack_eliciting(),
                                                                  packet5->is_crypto_packet(),
                                                                  true,
                                                                  packet5->size(),
                                                                  packet5->type(),
                                                                  {},
                                                                  pn_space}));
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{packet6->packet_number(),
                                                                  Thread::get_hrtime(),
                                                                  packet6->is_ack_eliciting(),
                                                                  packet6->is_crypto_packet(),
                                                                  true,
                                                                  packet6->size(),
                                                                  packet6->type(),
                                                                  {},
                                                                  pn_space}));
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{packet7->packet_number(),
                                                                  Thread::get_hrtime(),
                                                                  packet6->is_ack_eliciting(),
                                                                  packet7->is_crypto_packet(),
                                                                  true,
                                                                  packet7->size(),
                                                                  packet7->type(),
                                                                  {},
                                                                  pn_space}));
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{packet8->packet_number(),
                                                                  Thread::get_hrtime(),
                                                                  packet6->is_ack_eliciting(),
                                                                  packet8->is_crypto_packet(),
                                                                  true,
                                                                  packet8->size(),
                                                                  packet8->type(),
                                                                  {},
                                                                  pn_space}));
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{packet9->packet_number(),
                                                                  Thread::get_hrtime(),
                                                                  packet6->is_ack_eliciting(),
                                                                  packet9->is_crypto_packet(),
                                                                  true,
                                                                  packet9->size(),
                                                                  packet9->type(),
                                                                  {},
                                                                  pn_space}));
    detector.on_packet_sent(QUICPacketInfoUPtr(new QUICPacketInfo{packet10->packet_number(),
                                                                  Thread::get_hrtime(),
                                                                  packet10->is_ack_eliciting(),
                                                                  packet10->is_crypto_packet(),
                                                                  true,
                                                                  packet10->size(),
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
    QUICFrame *x = afm.generate_frame(buf, level, 2048, 2048, 0);
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
  }
}

TEST_CASE("QUICLossDetector_HugeGap", "[quic]")
{
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  MockQUICConnectionInfoProvider info;
  MockQUICCCConfig cc_config;
  MockQUICLDConfig ld_config;
  MockQUICCongestionController cc(&info, cc_config);
  QUICRTTMeasure rtt_measure;
  QUICLossDetector detector(&info, &cc, &rtt_measure, ld_config);

  auto t1           = Thread::get_hrtime();
  QUICAckFrame *ack = QUICFrameFactory::create_ack_frame(frame_buf, 100000000, 100, 10000000);
  ack->ack_block_section()->add_ack_block({20000000, 30000000});
  detector.handle_frame(QUICEncryptionLevel::INITIAL, *ack);
  auto t2 = Thread::get_hrtime();
  CHECK(t2 - t1 < HRTIME_MSECONDS(100));
}
