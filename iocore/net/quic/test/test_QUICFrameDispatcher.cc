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
  uint8_t payload[] = {0x01};
  QUICStreamFrame streamFrame(payload, 1, 0x03, 0);

  auto connectionManager    = std::make_shared<MockQUICConnectionManager>();
  auto streamManager        = std::make_shared<MockQUICStreamManager>();
  auto flowController       = std::make_shared<MockQUICFlowController>();
  auto congestionController = std::make_shared<MockQUICCongestionController>();
  auto lossDetector         = std::make_shared<MockQUICLossDetector>();
  QUICFrameDispatcher quicFrameDispatcher(connectionManager, streamManager, flowController, congestionController, lossDetector);

  // Initial state
  CHECK(connectionManager->getTotalFrameCount() == 0);
  CHECK(streamManager->getTotalFrameCount() == 0);
  CHECK(flowController->getTotalFrameCount() == 0);
  CHECK(congestionController->getTotalFrameCount() == 0);

  // STREAM frame
  uint8_t buf[4096] = {0};
  size_t len        = 0;
  streamFrame.store(buf, &len);
  quicFrameDispatcher.receive_frames(buf, len);
  CHECK(connectionManager->getTotalFrameCount() == 0);
  CHECK(streamManager->getTotalFrameCount() == 1);
  CHECK(flowController->getTotalFrameCount() == 1);
  CHECK(congestionController->getTotalFrameCount() == 1);
}

// Stubs
QUICApplication *QUICNetVConnection::get_application(QUICStreamId)
{
  return nullptr;
}

QUICCrypto *
QUICNetVConnection::get_crypto()
{
  return nullptr;
}

void QUICNetVConnection::close(QUICError)
{
  return;
}
