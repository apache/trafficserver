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

#include "quic/QUICFrameDispatcher.h"
#include "quic/Mock.h"
#include <memory>

TEST_CASE("QUICFrameHandler", "[quic]")
{
  uint8_t raw[]          = {0x01};
  ats_unique_buf payload = ats_unique_malloc(1);
  memcpy(payload.get(), raw, 1);

  QUICStreamFrame streamFrame(std::move(payload), 1, 0x03, 0);

  auto connection    = new MockQUICConnection();
  auto streamManager = new MockQUICStreamManager();
  auto tx            = new MockQUICPacketTransmitter();
  auto info          = new MockQUICConnectionInfoProvider();
  auto cc            = new MockQUICCongestionController(info);
  auto lossDetector  = new MockQUICLossDetector(tx, info, cc);

  QUICFrameDispatcher quicFrameDispatcher(info);
  quicFrameDispatcher.add_handler(connection);
  quicFrameDispatcher.add_handler(streamManager);
  quicFrameDispatcher.add_handler(lossDetector);

  // Initial state
  CHECK(connection->getTotalFrameCount() == 0);
  CHECK(streamManager->getTotalFrameCount() == 0);

  // STREAM frame
  uint8_t buf[4096] = {0};
  size_t len        = 0;
  streamFrame.store(buf, &len, 4096);
  bool should_send_ack;
  quicFrameDispatcher.receive_frames(buf, len, should_send_ack);
  CHECK(connection->getTotalFrameCount() == 0);
  CHECK(streamManager->getTotalFrameCount() == 1);

  // CONNECTION_CLOSE frame
  QUICConnectionCloseFrame connectionCloseFrame({});
  connectionCloseFrame.store(buf, &len, 4096);
  quicFrameDispatcher.receive_frames(buf, len, should_send_ack);
  CHECK(connection->getTotalFrameCount() == 1);
  CHECK(streamManager->getTotalFrameCount() == 1);
}
