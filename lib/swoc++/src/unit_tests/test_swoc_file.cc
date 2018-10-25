/** @file

    swoc::file unit tests.

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

#include <iostream>

#include "swoc/swoc_file.h"
#include "swoc/ext/catch.hpp"

using swoc::file::path;

// --------------------
TEST_CASE("swoc_file", "[libts][swoc_file]")
{
  path p1("/home");
  REQUIRE(p1.string() == "/home");
  auto p2 = p1 / "bob";
  REQUIRE(p2.string() == "/home/bob");
  p2 = p2 / "git/ats/";
  REQUIRE(p2.string() == "/home/bob/git/ats/");
  p2 /= "lib/ts";
  REQUIRE(p2.string() == "/home/bob/git/ats/lib/ts");
  p2 /= "/home/dave";
  REQUIRE(p2.string() == "/home/dave");
  path p3 = path("/home/dave") / "git/tools";
  REQUIRE(p3.string() == "/home/dave/git/tools");
}

TEST_CASE("swoc_file_io", "[libts][swoc_file_io]")
{
  path file("src/unit_tests/test_swoc_file.cc");
  std::error_code ec;
  std::string content = swoc::file::load(file, ec);
  REQUIRE(ec.value() == 0);
  REQUIRE(content.size() > 0);
  REQUIRE(content.find("swoc::file::path") != content.npos);

  // Check some file properties.
  REQUIRE(swoc::file::is_readable(file) == true);
  auto fs = swoc::file::status(file, ec);
  REQUIRE(ec.value() == 0);
  REQUIRE(swoc::file::is_dir(fs) == false);
  REQUIRE(swoc::file::is_regular_file(fs) == true);

  // Failure case.
  file    = "unit-tests/no_such_file.txt";
  content = swoc::file::load(file, ec);
  REQUIRE(ec.value() == 2);
  REQUIRE(swoc::file::is_readable(file) == false);
}
