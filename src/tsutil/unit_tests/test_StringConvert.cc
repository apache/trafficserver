/** @file

    Unit tests for StringConvert

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

#include <catch.hpp>

#include <tsutil/StringConvert.h>

TEST_CASE("StringConvert", "[STE]")
{
  REQUIRE(ts::hex("01") == "3031");
  REQUIRE(ts::unhex("3031") == "01");
  REQUIRE(ts::hex("5pn2QM") == "35706e32514d");
}
