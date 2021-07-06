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

#include "quic/QUICPathValidator.h"
#include "quic/Mock.h"
#include "stdio.h"
#include "stdlib.h"

TEST_CASE("QUICPathValidator", "[quic]")
{
  MockQUICConnectionInfoProvider cinfo_provider;
  QUICPathValidator pv_c(cinfo_provider, [](bool x) {});
  QUICPathValidator pv_s(cinfo_provider, [](bool x) {});

  SECTION("interests")
  {
    auto interests = pv_c.interests();
    CHECK(std::find_if(interests.begin(), interests.end(), [](QUICFrameType t) { return t == QUICFrameType::PATH_CHALLENGE; }) !=
          interests.end());
    CHECK(std::find_if(interests.begin(), interests.end(), [](QUICFrameType t) { return t == QUICFrameType::PATH_RESPONSE; }) !=
          interests.end());
    CHECK(std::find_if(interests.begin(), interests.end(), [](QUICFrameType t) {
            return t != QUICFrameType::PATH_CHALLENGE && t != QUICFrameType::PATH_RESPONSE;
          }) == interests.end());
  }

  SECTION("basic scenario")
  {
    uint8_t frame_buf[1024];
    uint32_t seq_num = 1;
    IpEndpoint local, remote;
    ats_ip_pton("127.0.0.1:4433", &local);
    ats_ip_pton("127.0.0.1:12345", &remote);
    QUICPath path = {local, remote};

    // Send a challenge
    CHECK(!pv_c.is_validating(path));
    CHECK(!pv_c.is_validated(path));
    REQUIRE(!pv_c.will_generate_frame(QUICEncryptionLevel::ONE_RTT, 0, false, seq_num));
    pv_c.validate(path);
    CHECK(pv_c.is_validating(path));
    CHECK(!pv_c.is_validated(path));
    REQUIRE(pv_c.will_generate_frame(QUICEncryptionLevel::ONE_RTT, 0, false, seq_num));
    auto frame = pv_c.generate_frame(frame_buf, QUICEncryptionLevel::ONE_RTT, 1024, 1024, 0, seq_num);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::PATH_CHALLENGE);
    CHECK(pv_c.is_validating(path));
    CHECK(!pv_c.is_validated(path));
    ++seq_num;

    // Receive the challenge and respond
    CHECK(!pv_s.is_validating(path));
    CHECK(!pv_s.is_validated(path));
    REQUIRE(!pv_s.will_generate_frame(QUICEncryptionLevel::ONE_RTT, 0, false, seq_num));
    auto error = pv_s.handle_frame(QUICEncryptionLevel::ONE_RTT, *frame);
    REQUIRE(!error);
    CHECK(!pv_s.is_validating(path));
    CHECK(!pv_s.is_validated(path));
    REQUIRE(pv_s.will_generate_frame(QUICEncryptionLevel::ONE_RTT, 0, false, seq_num));
    frame->~QUICFrame();
    frame = pv_s.generate_frame(frame_buf, QUICEncryptionLevel::ONE_RTT, 1024, 1024, 0, seq_num);
    REQUIRE(frame);
    CHECK(frame->type() == QUICFrameType::PATH_RESPONSE);
    CHECK(!pv_s.is_validating(path));
    CHECK(!pv_s.is_validated(path));
    ++seq_num;

    uint8_t buf[1024];
    size_t len = 0;
    uint8_t received_frame_buf[1024];
    Ptr<IOBufferBlock> ibb = frame->to_io_buffer_block(sizeof(buf));
    for (auto b = ibb; b; b = b->next) {
      memcpy(buf + len, b->start(), b->size());
      len += b->size();
    }
    MockQUICPacketR mock_packet;
    auto received_frame = QUICFrameFactory::create(received_frame_buf, buf, len, &mock_packet);
    mock_packet.set_from(remote);
    mock_packet.set_to(local);

    // Receive the response
    error = pv_c.handle_frame(QUICEncryptionLevel::ONE_RTT, *received_frame);
    REQUIRE(!error);
    CHECK(!pv_c.is_validating(path));
    CHECK(pv_c.is_validated(path));

    frame->~QUICFrame();
    received_frame->~QUICFrame();
  }
}
