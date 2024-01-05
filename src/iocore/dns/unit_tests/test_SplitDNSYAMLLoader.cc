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
#include <swoc/bwf_base.h>

#include <sstream>
#include <string>
#include <string_view>

static void
check_error_contains(splitdns::yaml::err_type const &error, std::string_view substr)
{
  std::stringstream errorstream;
  errorstream << error;
  std::string info_msg;
  swoc::bwprint(info_msg, "Looking for \"{}\" in:\n\"\n{}\"", substr, errorstream.str());
  INFO(info_msg);
  // Expression is kept outside CHECK macro so error message is clean.
  bool found{errorstream.str().find(substr) != std::string_view::npos};
  CHECK(found);
}

TEST_CASE("loading a YAML config file")
{
  SplitDNS got;
  std::stringstream errorstream;

  SECTION("Given does-not-exist.yaml does not exist, "
          "when we try to load the SplitDNS from it, "
          "we should get \"Failed to load does-not-exist.yaml.\"")
  {
    auto zret{splitdns::yaml::load("does-not-exist.yaml", got)};
    check_error_contains(zret, "Failed to load does-not-exist.yaml");
  }

  SECTION("Given does-not-exist-also.yaml does not exist, "
          "when we try to load the SplitDNS from it, "
          "we should get \"Failed to load does-not-exist-also.yaml.\"")
  {
    auto zret{splitdns::yaml::load("does-not-exist-also.yaml", got)};
    check_error_contains(zret, "Failed to load does-not-exist-also.yaml");
  }

  SECTION("Given wrong-root.yaml does not have root 'dns', "
          "when we try to load the SplitDNS from it, "
          "we should get the specified error.")
  {
    auto zret{splitdns::yaml::load("wrong-root.yaml", got)};
    check_error_contains(zret, "Root tag 'dns' not found");
    check_error_contains(zret, "Line 0");
    check_error_contains(zret, "While loading wrong-root.yaml");
  }

  SECTION("Given wrong-subroot.yaml does not have subroot 'split', "
          "when we try to load the SplitDNS from it, "
          "we should get the specified error.")
  {
    auto zret{splitdns::yaml::load("wrong-subroot.yaml", got)};
    check_error_contains(zret, "Tag 'split' not found");
    check_error_contains(zret, "Line 1");
    check_error_contains(zret, "While loading wrong-subroot.yaml");
  }
}
