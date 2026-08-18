/** @file

  Catch based unit tests for NetHandler

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

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <set>

#include "iocore/net/NetHandler.h"

// The switch in Config::operator[] is checked for exhaustiveness by the
// compiler, but not for correctness: a case returning the wrong member still
// builds. That would make a record update write to the wrong config value.
TEST_CASE("Config indexing reaches the intended member", "[net][nethandler]")
{
  using Index = NetHandler::Config::Index;
  NetHandler::Config config;

  config[Index::MAX_CONNECTIONS_IN]         = 11;
  config[Index::MAX_REQUESTS_IN]            = 22;
  config[Index::DEFAULT_INACTIVITY_TIMEOUT] = 33;

  CHECK(config.max_connections_in == 11);
  CHECK(config.max_requests_in == 22);
  CHECK(config.default_inactivity_timeout == 33);
}

// Distinct addresses alone would not catch two cases returning each other's
// member, but this covers any index added later without updating the test.
TEST_CASE("Every Config index maps to a distinct member", "[net][nethandler]")
{
  NetHandler::Config         config;
  std::set<uint32_t const *> members;

  for (int i = 0; i < NetHandler::CONFIG_ITEM_COUNT; ++i) {
    members.insert(&config[static_cast<NetHandler::Config::Index>(i)]);
  }
  CHECK(members.size() == static_cast<std::size_t>(NetHandler::CONFIG_ITEM_COUNT));
}
