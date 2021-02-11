/**
  @file Test for Errata

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

#include "catch.hpp"

#include "tscore/Errata.h"

TEST_CASE("Basic Errata with text only", "[errata]")
{
  ts::Errata err;
  std::string text{"Some error text"};
  err.push(text);
  REQUIRE(err.isOK()); // as code is 0 by default.
  REQUIRE(err.top().text() == text);
}

TEST_CASE("Basic Errata test with id and text", "[errata]")
{
  ts::Errata err;
  int id{1};
  std::string text{"Some error text"};

  err.push(id, text);

  REQUIRE(err.isOK()); // as code is 0 by default.
  REQUIRE(err.top().text() == text);
}

TEST_CASE("Basic Errata test with id,code and text", "[errata]")
{
  ts::Errata err;
  int id{1};
  unsigned int code{2};
  std::string text{"Some error text"};

  err.push(id, code, text);

  REQUIRE(!err.isOK()); // This should not be ok as code now is 2
  REQUIRE(err.top().getCode() == code);
  REQUIRE(err.top().text() == text);
}
