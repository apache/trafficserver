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

#include "quic/QUICFlowController.h"
#include "quic/Mock.h"
#include <memory>

static constexpr int DEFAULT_RTT = 1 * HRTIME_SECOND;

class MockRTTProvider : public QUICRTTProvider
{
public:
  ink_hrtime
  smoothed_rtt() const override
  {
    return this->_smoothed_rtt;
  }

  MockRTTProvider(ink_hrtime rtt) : _smoothed_rtt(rtt) {}
  void
  set_smoothed_rtt(ink_hrtime rtt)
  {
    this->_smoothed_rtt = rtt;
  }

  ink_hrtime
  latest_rtt() const override
  {
    return HRTIME_MSECONDS(1);
  }

  ink_hrtime
  rttvar() const override
  {
    return HRTIME_MSECONDS(1);
  }

  ink_hrtime
  congestion_period(uint32_t period) const override
  {
    return HRTIME_MSECONDS(1);
  }

private:
  ink_hrtime _smoothed_rtt = 0;
};

TEST_CASE("QUICFlowController_Local_Connection", "[quic]")
{
  int ret = 0;
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  MockRTTProvider rp(DEFAULT_RTT);
  QUICLocalConnectionFlowController fc(&rp, 1024);

  // Check initial state
  CHECK(fc.current_offset() == 0);
  CHECK(fc.current_limit() == 1024);

  ret = fc.update(256);
  CHECK(fc.current_offset() == 256);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  ret = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  // Retransmit
  ret = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  ret = fc.update(1024);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  // Delay
  ret = fc.update(512);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);
  Thread::get_hrtime_updated();

  // Exceed limit
  ret = fc.update(1280);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret != 0);

  // MAX_STREAM_DATA
  fc.forward_limit(2048);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 2048);
  QUICFrame *frame = fc.generate_frame(frame_buf, QUICEncryptionLevel::ONE_RTT, 0, 1024, 0);
  CHECK(frame);
  CHECK(frame->type() == QUICFrameType::MAX_DATA);

  ret = fc.update(1280);
  CHECK(fc.current_offset() == 1280);
  CHECK(fc.current_limit() == 2048);
  CHECK(ret == 0);
}

TEST_CASE("QUICFlowController_Remote_Connection", "[quic]")
{
  int ret = 0;
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICRemoteConnectionFlowController fc(1024);

  // Check initial state
  CHECK(fc.current_offset() == 0);
  CHECK(fc.current_limit() == 1024);

  ret = fc.update(256);
  CHECK(fc.current_offset() == 256);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  ret = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  // Retransmit
  ret = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  ret = fc.update(1000);
  CHECK(fc.current_offset() == 1000);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  // Delay
  ret = fc.update(512);
  CHECK(fc.current_offset() == 1000);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  // Exceed limit
  ret = fc.update(1280);
  CHECK(fc.current_offset() == 1000);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret != 0);
  QUICFrame *frame = fc.generate_frame(frame_buf, QUICEncryptionLevel::ONE_RTT, 0, 1024, 0);
  CHECK(frame);
  CHECK(frame->type() == QUICFrameType::DATA_BLOCKED);

  // MAX_STREAM_DATA
  fc.forward_limit(2048);
  CHECK(fc.current_offset() == 1000);
  CHECK(fc.current_limit() == 2048);

  ret = fc.update(1280);
  CHECK(fc.current_offset() == 1280);
  CHECK(fc.current_limit() == 2048);
  CHECK(ret == 0);
}

TEST_CASE("QUICFlowController_Remote_Connection_ZERO_Credit", "[quic]")
{
  int ret = 0;
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICRemoteConnectionFlowController fc(1024);

  // Check initial state
  CHECK(fc.current_offset() == 0);
  CHECK(fc.current_limit() == 1024);

  // Zero credit
  ret = fc.update(1024);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  CHECK(fc.will_generate_frame(QUICEncryptionLevel::ONE_RTT, 0));
  // if there're anything to send
  QUICFrame *frame = fc.generate_frame(frame_buf, QUICEncryptionLevel::ONE_RTT, 0, 1024, 0);
  CHECK(frame);
  CHECK(frame->type() == QUICFrameType::DATA_BLOCKED);

  // MAX_STREAM_DATA
  fc.forward_limit(2048);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 2048);

  ret = fc.update(1280);
  CHECK(fc.current_offset() == 1280);
  CHECK(fc.current_limit() == 2048);
  CHECK(ret == 0);
}

TEST_CASE("QUICFlowController_Local_Stream", "[quic]")
{
  int ret = 0;
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  MockRTTProvider rp(DEFAULT_RTT);
  QUICLocalStreamFlowController fc(&rp, 1024, 0);

  // Check initial state
  CHECK(fc.current_offset() == 0);
  CHECK(fc.current_limit() == 1024);

  ret = fc.update(256);
  CHECK(fc.current_offset() == 256);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  ret = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  // Retransmit
  ret = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  ret = fc.update(1024);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  // Delay
  ret = fc.update(512);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);
  Thread::get_hrtime_updated();

  // Exceed limit
  ret = fc.update(1280);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret != 0);

  // MAX_STREAM_DATA
  fc.forward_limit(2048);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 2048);
  QUICFrame *frame = fc.generate_frame(frame_buf, QUICEncryptionLevel::ONE_RTT, 0, 1024, 0);
  CHECK(frame);
  CHECK(frame->type() == QUICFrameType::MAX_STREAM_DATA);

  ret = fc.update(1280);
  CHECK(fc.current_offset() == 1280);
  CHECK(fc.current_limit() == 2048);
  CHECK(ret == 0);
}

TEST_CASE("QUICFlowController_Remote_Stream", "[quic]")
{
  int ret = 0;
  QUICRemoteStreamFlowController fc(1024, 0);

  // Check initial state
  CHECK(fc.current_offset() == 0);
  CHECK(fc.current_limit() == 1024);

  ret = fc.update(256);
  CHECK(fc.current_offset() == 256);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  ret = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  // Retransmit
  ret = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  ret = fc.update(1024);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  CHECK(fc.credit() == 0);
  CHECK(fc.will_generate_frame(QUICEncryptionLevel::ONE_RTT, 0));

  // Delay
  ret = fc.update(512);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret == 0);

  // Exceed limit
  ret = fc.update(1280);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(ret != 0);

  // MAX_STREAM_DATA
  fc.forward_limit(2048);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 2048);

  ret = fc.update(1280);
  CHECK(fc.current_offset() == 1280);
  CHECK(fc.current_limit() == 2048);
  CHECK(ret == 0);
}

TEST_CASE("Frame retransmission", "[quic]")
{
  QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;

  SECTION("BLOCKED frame")
  {
    int ret = 0;
    uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
    QUICRemoteConnectionFlowController fc(1024);

    // Check initial state
    auto frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    CHECK(!frame);

    ret = fc.update(1024);
    CHECK(ret == 0);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICDataBlockedFrame *>(frame)->offset() == 1024);
    QUICFrameId id = frame->id();

    // Don't retransmit unless the frame is lost
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(!frame);

    // Retransmit
    fc.on_frame_lost(id);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICDataBlockedFrame *>(frame)->offset() == 1024);

    // Don't send if it was not blocked
    fc.on_frame_lost(frame->id());
    fc.forward_limit(2048);
    ret   = fc.update(1536);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    CHECK(!frame);

    // This should not be retransmition
    ret   = fc.update(2048);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICDataBlockedFrame *>(frame)->offset() == 2048);
  }

  SECTION("STREAM_DATA_BLOCKED frame")
  {
    int ret = 0;
    uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
    QUICRemoteStreamFlowController fc(1024, 0);

    // Check initial state
    auto frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    CHECK(!frame);

    ret = fc.update(1024);
    CHECK(ret == 0);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICStreamDataBlockedFrame *>(frame)->offset() == 1024);
    QUICFrameId id = frame->id();

    // Don't retransmit unless the frame is lost
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(!frame);

    // Retransmit
    fc.on_frame_lost(id);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICStreamDataBlockedFrame *>(frame)->offset() == 1024);

    // Don't send if it was not blocked
    fc.on_frame_lost(frame->id());
    fc.forward_limit(2048);
    ret   = fc.update(1536);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    CHECK(!frame);

    // This should not be retransmition
    ret   = fc.update(2048);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICStreamDataBlockedFrame *>(frame)->offset() == 2048);
  }

  SECTION("MAX_DATA frame")
  {
    uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
    MockRTTProvider rp(DEFAULT_RTT);
    QUICLocalConnectionFlowController fc(&rp, 1024);

    // Check initial state
    auto frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    CHECK(!frame);

    fc.update(1024);
    fc.forward_limit(1024);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICMaxDataFrame *>(frame)->maximum_data() == 1024);
    QUICFrameId id = frame->id();

    // Don't retransmit unless the frame is lost
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(!frame);

    // Retransmit
    fc.on_frame_lost(id);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICMaxDataFrame *>(frame)->maximum_data() == 1024);

    // Send new one if it was updated
    fc.on_frame_lost(id);
    fc.forward_limit(2048);
    fc.update(2048);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICMaxDataFrame *>(frame)->maximum_data() == 2048);
  }

  SECTION("MAX_STREAM_DATA frame")
  {
    uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
    MockRTTProvider rp(DEFAULT_RTT);
    QUICLocalStreamFlowController fc(&rp, 1024, 0);

    // Check initial state
    auto frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    CHECK(!frame);

    fc.update(1024);
    fc.forward_limit(1024);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICMaxStreamDataFrame *>(frame)->maximum_stream_data() == 1024);
    QUICFrameId id = frame->id();

    // Don't retransmit unless the frame is lost
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(!frame);

    // Retransmit
    fc.on_frame_lost(id);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICMaxStreamDataFrame *>(frame)->maximum_stream_data() == 1024);

    // Send new one if it was updated
    fc.on_frame_lost(id);
    fc.forward_limit(2048);
    fc.update(2048);
    frame = fc.generate_frame(frame_buf, level, 1024, 1024, 0);
    REQUIRE(frame);
    CHECK(static_cast<QUICMaxStreamDataFrame *>(frame)->maximum_stream_data() == 2048);
  }
}
