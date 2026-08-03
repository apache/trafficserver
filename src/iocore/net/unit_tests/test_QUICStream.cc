/** @file

  Catch based unit tests for QUICStream

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "iocore/net/quic/QUICStream.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("QUICStream::compute_fair_send_budget")
{
  SECTION("No contention (0 or 1 writable streams) returns the max budget")
  {
    CHECK(QUICStream::compute_fair_send_budget(0) == QUICStream::MAX_STREAM_SEND_BYTES_PER_EVENT);
    CHECK(QUICStream::compute_fair_send_budget(1) == QUICStream::MAX_STREAM_SEND_BYTES_PER_EVENT);
  }

  SECTION("Heavy contention clamps to the min budget")
  {
    CHECK(QUICStream::compute_fair_send_budget(100) == QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT);
  }

  SECTION("Mid-range contention divides the max budget evenly")
  {
    CHECK(QUICStream::compute_fair_send_budget(8) == QUICStream::MAX_STREAM_SEND_BYTES_PER_EVENT / 8);
  }

  SECTION("Floor-transition boundary")
  {
    // MAX / MIN is the exact stream count at which the division result equals the floor.
    const size_t boundary = QUICStream::MAX_STREAM_SEND_BYTES_PER_EVENT / QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT;

    CHECK(QUICStream::compute_fair_send_budget(boundary) == QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT);
    CHECK(QUICStream::compute_fair_send_budget(boundary + 1) == QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT);
  }
}
