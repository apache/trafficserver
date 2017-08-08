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

#include <memory>

#include "quic/QUICFrame.h"
#include "quic/QUICStreamState.h"

TEST_CASE("QUICStreamState_Update", "[quic]")
{
  QUICStreamState ss;

  std::shared_ptr<const QUICStreamFrame> streamFrame =
    std::make_shared<const QUICStreamFrame>(reinterpret_cast<const uint8_t *>("foo"), 4, 1, 0);
  std::shared_ptr<const QUICRstStreamFrame> rstStreamFrame =
    std::make_shared<const QUICRstStreamFrame>(QUICErrorCode::QUIC_TRANSPORT_ERROR, 0, 0);

  CHECK(ss.get() == QUICStreamState::State::idle);

  ss.update_with_received_frame(*streamFrame);
  CHECK(ss.get() == QUICStreamState::State::open);

  ss.update_with_received_frame(*rstStreamFrame);
  CHECK(ss.get() == QUICStreamState::State::closed);
}
