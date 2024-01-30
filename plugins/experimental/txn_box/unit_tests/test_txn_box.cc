/** @file

    Unit tests for HttpReplay.h.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
 */

#include "catch.hpp"

#include "txn_box/yaml_util.h"
using namespace swoc::literals;

TEST_CASE("YAML special features", "[yaml]")
{
#if 0
  // Verify I understand how quoted values are distinguished.
  swoc::TextView quoted=R"(key: "value")";
  swoc::TextView unquoted=R"(key: value)";
  YAML::Node n1 = YAML::Load(quoted.data());
  REQUIRE(n1["key"].Tag() == "!");
  YAML::Node n2 = YAML::Load(unquoted.data());
  REQUIRE(n2["key"].Tag() == "?");
  YAML::Node n = YAML::Load("key: true");
  REQUIRE(n["key"].Tag() == "?");

  n = YAML::Load("key: null");
  REQUIRE(n["key"].Tag().empty() == true);
  REQUIRE(n["key"].IsNull() == true);
  REQUIRE(n["key"].Scalar().empty() == true);
  n = YAML::Load(R"(key: "null")");
  REQUIRE(n["key"].Tag() == "!");
  REQUIRE(n["key"].IsNull() == false);
  REQUIRE(n["key"].Scalar() == "null");
  n = YAML::Load(R"(key:)");
  REQUIRE(n["key"].Tag().empty() == true);
  REQUIRE(n["key"].IsNull() == true);
  REQUIRE(n["key"].Scalar().empty() == true);
  n = YAML::Load(R"(key: "")");
  REQUIRE(n["key"].Tag() == "!");
  REQUIRE(n["key"].IsNull() == false);
  REQUIRE(n["key"].Scalar().empty() == true);
#endif
}
