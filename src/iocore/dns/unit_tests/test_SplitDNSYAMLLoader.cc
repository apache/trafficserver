/** @file

  Catch based unit tests for SplitDNS

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

#include "iocore/dns/SplitDNSYAMLLoader.h"
#include "P_SplitDNSProcessor.h"

#include <catch.hpp>

#include <sstream>

TEST_CASE("loading a YAML config file")
{
  SplitDNS got;
  std::stringstream errorstream;

  SECTION("Given does-not-exist.yaml does not exist, "
          "when we try to load the SplitDNS from it, "
          "we should get \"Failed to load does-not-exist.yaml.\"")
  {
    dns::yaml::load("does-not-exist.yaml", got, errorstream);
    CHECK(errorstream.str() == "Failed to load does-not-exist.yaml.");
  }

  SECTION("Given does-not-exist-also.yaml does not exist, "
          "when we try to load the SplitDNS from it, "
          "we should get \"Failed to load does-not-exist-also.yaml.\"")
  {
    dns::yaml::load("does-not-exist-also.yaml", got, errorstream);
    CHECK(errorstream.str() == "Failed to load does-not-exist-also.yaml.");
  }
}
