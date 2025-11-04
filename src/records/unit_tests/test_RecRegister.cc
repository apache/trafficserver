/** @file

   Catch-based tests for RecRegister.cc

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */
#include <catch2/catch_test_macros.hpp>
#include "records/RecCore.h"
#include "iocore/eventsystem/EventSystem.h"
#include "iocore/eventsystem/RecProcess.h"
#include "tscore/Layout.h"
#include "test_Diags.h"

TEST_CASE("RecRegisterConfig - Type Dispatch", "[librecords][RecConfig]")
{
  SECTION("RecRegisterConfigInt")
  {
    RecErrT err = RecRegisterConfigInt(RECT_CONFIG, "proxy.test.int_value", 42, RECU_DYNAMIC, RECC_NULL, nullptr, REC_SOURCE_NULL);
    REQUIRE(err == REC_ERR_OKAY);

    RecInt value = RecGetRecordInt("proxy.test.int_value").value_or(0);
    REQUIRE(value == 42);
  }

  SECTION("RecRegisterConfigFloat")
  {
    RecErrT err =
      RecRegisterConfigFloat(RECT_CONFIG, "proxy.test.float_value", 3.14f, RECU_DYNAMIC, RECC_NULL, nullptr, REC_SOURCE_NULL);
    REQUIRE(err == REC_ERR_OKAY);

    RecFloat value = RecGetRecordFloat("proxy.test.float_value").value_or(0.0f);
    REQUIRE(value == 3.14f);
  }

  SECTION("RecRegisterConfigString")
  {
    RecErrT err =
      RecRegisterConfigString(RECT_CONFIG, "proxy.test.string_value", "hello", RECU_DYNAMIC, RECC_NULL, nullptr, REC_SOURCE_NULL);
    REQUIRE(err == REC_ERR_OKAY);

    char             value_buf[6];
    std::string_view value = RecGetRecordString("proxy.test.string_value", value_buf, sizeof(value_buf)).value_or("");
    REQUIRE(value.empty() == false);
    REQUIRE(value == "hello");
  }
}

TEST_CASE("RecRegisterStat - Type Dispatch", "[librecords][RecStat]")
{
  SECTION("RecRegisterStatInt")
  {
    RecErrT err = RecRegisterStatInt(RECT_NODE, "proxy.node.test.int", 99, RECP_NON_PERSISTENT);
    REQUIRE(err == REC_ERR_OKAY);

    RecInt value = RecGetRecordInt("proxy.node.test.int").value_or(0);
    REQUIRE(value == 99);
  }

  SECTION("RecRegisterStatFloat")
  {
    RecErrT err = RecRegisterStatFloat(RECT_NODE, "proxy.node.test.float", 2.71f, RECP_NON_PERSISTENT);
    REQUIRE(err == REC_ERR_OKAY);

    RecFloat value = RecGetRecordFloat("proxy.node.test.float").value_or(0.0f);
    REQUIRE(value == 2.71f);
  }

  SECTION("RecRegisterStatCounter")
  {
    RecErrT err = RecRegisterStatCounter(RECT_NODE, "proxy.node.test.counter", 500, RECP_NON_PERSISTENT);
    REQUIRE(err == REC_ERR_OKAY);

    RecCounter value = RecGetRecordCounter("proxy.node.test.counter").value_or(0);
    REQUIRE(value == 500);
  }
}
