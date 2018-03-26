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
#include "ts/ink_hrtime.h"

TEST_CASE("QUICLossDetector_Loss", "[quic]")
{
  MockQUICHandshakeProtocol hs_protocol;
  QUICPacketFactory pf;
  pf.set_hs_protocol(&hs_protocol);

  QUICAckFrameCreator *afc         = new QUICAckFrameCreator();
  QUICConnectionId connection_id   = 1;
  MockQUICPacketTransmitter *tx    = new MockQUICPacketTransmitter();
  MockQUICCongestionController *cc = new MockQUICCongestionController();
  QUICLossDetector detector(tx, cc);
  ats_unique_buf payload              = ats_unique_malloc(16);
  size_t payload_len                  = 16;
  QUICPacketUPtr packet               = QUICPacketFactory::create_null_packet();
  std::shared_ptr<QUICAckFrame> frame = QUICFrameFactory::create_null_ack_frame();
  uint16_t ack_delay                  = 50;

  SECTION("Handshake")
  {
    // Check initial state
    CHECK(tx->retransmitted.size() == 0);

    // Send SERVER_CLEARTEXT (Handshake message)
    uint8_t raw[4]         = {0};
    ats_unique_buf payload = ats_unique_malloc(sizeof(raw));
    memcpy(payload.get(), raw, sizeof(raw));

    QUICPacketHeaderUPtr header = QUICPacketHeader::build(QUICPacketType::HANDSHAKE, 0xffddbb9977553311ULL, 0x00000001, 0,
                                                          0x00112233, std::move(payload), sizeof(raw));
    QUICPacketUPtr packet =
      QUICPacketUPtr(new QUICPacket(std::move(header), std::move(payload), sizeof(raw), true), [](QUICPacket *p) { delete p; });
    detector.on_packet_sent(std::move(packet));
    ink_hrtime_sleep(HRTIME_MSECONDS(1000));
    CHECK(tx->retransmitted.size() > 0);

    // Receive ACK
    std::shared_ptr<QUICAckFrame> frame = std::make_shared<QUICAckFrame>(0x01, 20, 0);
    frame->ack_block_section()->add_ack_block({0, 1ULL});
    detector.handle_frame(frame);
    ink_hrtime_sleep(HRTIME_MSECONDS(1500));
    int retransmit_count = tx->retransmitted.size();
    ink_hrtime_sleep(HRTIME_MSECONDS(1500));
    CHECK(tx->retransmitted.size() == retransmit_count);
  }

  SECTION("1-RTT")
  {
    // Check initial state
    CHECK(tx->retransmitted.size() == 0);

    // Send packet (1) to (7)
    payload                = ats_unique_malloc(16);
    QUICPacketUPtr packet1 = pf.create_server_protected_packet(connection_id, detector.largest_acked_packet_number(),
                                                               std::move(payload), payload_len, true);
    payload                = ats_unique_malloc(16);
    QUICPacketUPtr packet2 = pf.create_server_protected_packet(connection_id, detector.largest_acked_packet_number(),
                                                               std::move(payload), payload_len, true);
    payload                = ats_unique_malloc(16);
    QUICPacketUPtr packet3 = pf.create_server_protected_packet(connection_id, detector.largest_acked_packet_number(),
                                                               std::move(payload), payload_len, true);
    payload                = ats_unique_malloc(16);
    QUICPacketUPtr packet4 = pf.create_server_protected_packet(connection_id, detector.largest_acked_packet_number(),
                                                               std::move(payload), payload_len, true);
    payload                = ats_unique_malloc(16);
    QUICPacketUPtr packet5 = pf.create_server_protected_packet(connection_id, detector.largest_acked_packet_number(),
                                                               std::move(payload), payload_len, true);
    payload                = ats_unique_malloc(16);
    QUICPacketUPtr packet6 = pf.create_server_protected_packet(connection_id, detector.largest_acked_packet_number(),
                                                               std::move(payload), payload_len, true);
    payload                = ats_unique_malloc(16);
    QUICPacketUPtr packet7 = pf.create_server_protected_packet(connection_id, detector.largest_acked_packet_number(),
                                                               std::move(payload), payload_len, true);
    payload                = ats_unique_malloc(16);
    QUICPacketUPtr packet8 = pf.create_server_protected_packet(connection_id, detector.largest_acked_packet_number(),
                                                               std::move(payload), payload_len, true);
    payload                = ats_unique_malloc(16);
    QUICPacketUPtr packet9 = pf.create_server_protected_packet(connection_id, detector.largest_acked_packet_number(),
                                                               std::move(payload), payload_len, true);

    QUICPacketNumber pn1 = packet1->packet_number();
    QUICPacketNumber pn2 = packet2->packet_number();
    QUICPacketNumber pn3 = packet3->packet_number();
    QUICPacketNumber pn4 = packet4->packet_number();
    QUICPacketNumber pn5 = packet5->packet_number();
    QUICPacketNumber pn6 = packet6->packet_number();
    QUICPacketNumber pn7 = packet7->packet_number();
    QUICPacketNumber pn8 = packet8->packet_number();
    QUICPacketNumber pn9 = packet9->packet_number();

    detector.on_packet_sent(std::move(packet1));
    detector.on_packet_sent(std::move(packet2));
    detector.on_packet_sent(std::move(packet3));
    detector.on_packet_sent(std::move(packet4));
    detector.on_packet_sent(std::move(packet5));
    detector.on_packet_sent(std::move(packet6));
    detector.on_packet_sent(std::move(packet7));
    detector.on_packet_sent(std::move(packet8));
    detector.on_packet_sent(std::move(packet9));

    ink_hrtime_sleep(HRTIME_MSECONDS(1000));

    // Receive an ACK for (1) (4) (5) (7) (8) (9)
    afc->update(pn1, false, true);
    afc->update(pn4, false, true);
    afc->update(pn5, false, true);
    afc->update(pn7, false, true);
    afc->update(pn8, false, true);
    afc->update(pn9, false, true);
    ink_hrtime_sleep(HRTIME_MSECONDS(1000));
    frame = afc->create();
    detector.handle_frame(frame);
    ink_hrtime_sleep(HRTIME_MSECONDS(5000));

    CHECK(cc->lost_packets.size() == 3);

    CHECK(cc->lost_packets.find(pn1) == cc->lost_packets.end());
    CHECK(cc->lost_packets.find(pn2) != cc->lost_packets.end());
    CHECK(cc->lost_packets.find(pn3) != cc->lost_packets.end());
    CHECK(cc->lost_packets.find(pn4) == cc->lost_packets.end());
    CHECK(cc->lost_packets.find(pn5) == cc->lost_packets.end());
    CHECK(cc->lost_packets.find(pn6) != cc->lost_packets.end());
    CHECK(cc->lost_packets.find(pn7) == cc->lost_packets.end());
    CHECK(cc->lost_packets.find(pn8) == cc->lost_packets.end());
    CHECK(cc->lost_packets.find(pn9) == cc->lost_packets.end());
  }
}

TEST_CASE("QUICLossDetector_HugeGap", "[quic]")
{
  MockQUICPacketTransmitter *tx    = new MockQUICPacketTransmitter();
  MockQUICCongestionController *cc = new MockQUICCongestionController();
  QUICLossDetector detector(tx, cc);

  // Check initial state
  CHECK(tx->retransmitted.size() == 0);

  auto t1                           = Thread::get_hrtime();
  std::shared_ptr<QUICAckFrame> ack = QUICFrameFactory::create_ack_frame(100000000, 100, 10000000);
  ack->ack_block_section()->add_ack_block({20000000, 30000000});
  detector.handle_frame(ack);
  auto t2 = Thread::get_hrtime();
  CHECK(t2 - t1 < HRTIME_MSECONDS(100));
}
