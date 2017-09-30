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

TEST_CASE("QUICFlowController_Local_Connection", "[quic]")
{
  std::unique_ptr<QUICError> error = nullptr;
  MockQUICFrameTransmitter tx;
  QUICLocalConnectionFlowController fc(1024, &tx);

  // Check initial state
  CHECK(fc.current_offset() == 0);
  CHECK(fc.current_limit() == 1024);

  error = fc.update(256);
  CHECK(fc.current_offset() == 256);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  error = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Retransmit
  error = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  error = fc.update(1024);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Delay
  error = fc.update(512);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Exceed limit
  error = fc.update(1280);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->code == QUICErrorCode::FLOW_CONTROL_ERROR);

  // MAX_STREAM_DATA
  CHECK(tx.frameCount[static_cast<int>(QUICFrameType::MAX_DATA)] == 0);
  fc.forward_limit(2048);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 2048);
  CHECK(tx.frameCount[static_cast<int>(QUICFrameType::MAX_DATA)] == 1);

  error = fc.update(1280);
  CHECK(fc.current_offset() == 1280);
  CHECK(fc.current_limit() == 2048);
  CHECK(error->cls == QUICErrorClass::NONE);
}

TEST_CASE("QUICFlowController_Remote_Connection", "[quic]")
{
  std::unique_ptr<QUICError> error = nullptr;
  MockQUICFrameTransmitter tx;
  QUICRemoteConnectionFlowController fc(1024, &tx);

  // Check initial state
  CHECK(fc.current_offset() == 0);
  CHECK(fc.current_limit() == 1024);

  error = fc.update(256);
  CHECK(fc.current_offset() == 256);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  error = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Retransmit
  error = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  error = fc.update(1024);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Delay
  error = fc.update(512);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Exceed limit
  CHECK(tx.frameCount[static_cast<int>(QUICFrameType::BLOCKED)] == 0);
  error = fc.update(1280);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls != QUICErrorClass::NONE);
  CHECK(tx.frameCount[static_cast<int>(QUICFrameType::BLOCKED)] == 1);

  // MAX_STREAM_DATA
  fc.forward_limit(2048);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 2048);

  error = fc.update(1280);
  CHECK(fc.current_offset() == 1280);
  CHECK(fc.current_limit() == 2048);
  CHECK(error->cls == QUICErrorClass::NONE);
}

TEST_CASE("QUICFlowController_Local_Stream", "[quic]")
{
  std::unique_ptr<QUICError> error = nullptr;
  MockQUICFrameTransmitter tx;
  QUICLocalStreamFlowController fc(1024, &tx, 0);

  // Check initial state
  CHECK(fc.current_offset() == 0);
  CHECK(fc.current_limit() == 1024);

  error = fc.update(256);
  CHECK(fc.current_offset() == 256);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  error = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Retransmit
  error = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  error = fc.update(1024);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Delay
  error = fc.update(512);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Exceed limit
  error = fc.update(1280);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->code == QUICErrorCode::FLOW_CONTROL_ERROR);

  // MAX_STREAM_DATA
  CHECK(tx.frameCount[static_cast<int>(QUICFrameType::MAX_STREAM_DATA)] == 0);
  fc.forward_limit(2048);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 2048);
  CHECK(tx.frameCount[static_cast<int>(QUICFrameType::MAX_STREAM_DATA)] == 1);

  error = fc.update(1280);
  CHECK(fc.current_offset() == 1280);
  CHECK(fc.current_limit() == 2048);
  CHECK(error->cls == QUICErrorClass::NONE);
}

TEST_CASE("QUICFlowController_Remote_Stream", "[quic]")
{
  std::unique_ptr<QUICError> error = nullptr;
  MockQUICFrameTransmitter tx;
  QUICRemoteStreamFlowController fc(1024, &tx, 0);

  // Check initial state
  CHECK(fc.current_offset() == 0);
  CHECK(fc.current_limit() == 1024);

  error = fc.update(256);
  CHECK(fc.current_offset() == 256);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  error = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Retransmit
  error = fc.update(512);
  CHECK(fc.current_offset() == 512);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  error = fc.update(1024);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Delay
  error = fc.update(512);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls == QUICErrorClass::NONE);

  // Exceed limit
  CHECK(tx.frameCount[static_cast<int>(QUICFrameType::STREAM_BLOCKED)] == 0);
  error = fc.update(1280);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 1024);
  CHECK(error->cls != QUICErrorClass::NONE);
  CHECK(tx.frameCount[static_cast<int>(QUICFrameType::STREAM_BLOCKED)] == 1);

  // MAX_STREAM_DATA
  fc.forward_limit(2048);
  CHECK(fc.current_offset() == 1024);
  CHECK(fc.current_limit() == 2048);

  error = fc.update(1280);
  CHECK(fc.current_offset() == 1280);
  CHECK(fc.current_limit() == 2048);
  CHECK(error->cls == QUICErrorClass::NONE);
}
