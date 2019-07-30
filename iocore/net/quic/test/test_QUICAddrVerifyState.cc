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

#include "P_QUICNetVConnection.h"
#include <memory>

TEST_CASE("QUICAddrVerifyState", "[quic]")
{
  QUICAddrVerifyState state;

  // without consuming
  CHECK(state.windows() == 0);
  state.fill(10240);
  CHECK(state.windows() == 10240 * 3);

  // consume
  CHECK(state.windows() == 10240 * 3);
  state.consume(10240);
  CHECK(state.windows() == 10240 * 2);
  state.consume(10240);
  CHECK(state.windows() == 10240);
  state.consume(10240);
  CHECK(state.windows() == 0);

  // fill
  state.fill(1);
  CHECK(state.windows() == 3);
  state.consume(1);
  CHECK(state.windows() == 2);
  state.consume(1);
  CHECK(state.windows() == 1);
  state.consume(1);
  CHECK(state.windows() == 0);

  // fill overflow
  state.fill(UINT32_MAX);
  state.fill(2);
  CHECK(state.windows() == UINT32_MAX);
}
