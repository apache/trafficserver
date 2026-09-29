/** @file

  Unit tests for the traffic_ctl exit status of server errors.

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

#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "TrafficCtlStatus.h"

int                         App_Exit_Status_Code = CTRL_EX_OK;
swoc::Errata::severity_type App_Exit_Level_Error = ERRATA_ERROR;

namespace
{
shared::rpc::JSONRPCResponse
make_error_response(std::string const &json_error)
{
  shared::rpc::JSONRPCResponse resp;
  resp.error = YAML::Load(json_error);
  return resp;
}

// A method handler failure (ExecutionError) carrying the given data entries.
shared::rpc::JSONRPCResponse
execution_error(std::string const &data)
{
  return make_error_response(R"({"code": 9, "message": "Error during execution", "data": [)" + data + "]}");
}

int
exit_code_at(swoc::Errata::Severity level, shared::rpc::JSONRPCResponse const &resp)
{
  App_Exit_Level_Error = level;
  int const code       = appExitCodeFromResponse(resp);
  App_Exit_Level_Error = ERRATA_ERROR;
  return code;
}

std::string
printed(shared::rpc::JSONRPCError const &err)
{
  std::ostringstream os;
  os << err;
  return os.str();
}
} // namespace

TEST_CASE("A success response exits 0 at any level", "[exit_code]")
{
  shared::rpc::JSONRPCResponse resp;
  resp.result = YAML::Load(R"({"status": "ok"})");

  CHECK(exit_code_at(ERRATA_DIAG, resp) == CTRL_EX_OK);
  CHECK(exit_code_at(ERRATA_ERROR, resp) == CTRL_EX_OK);
}

TEST_CASE("An annotation without severity is graded as an error", "[exit_code]")
{
  auto const resp = execution_error(R"({"code": 2009, "message": "Record is read-only."})");

  CHECK(exit_code_at(ERRATA_DIAG, resp) == CTRL_EX_ERROR);
  CHECK(exit_code_at(ERRATA_WARN, resp) == CTRL_EX_ERROR);
  CHECK(exit_code_at(ERRATA_ERROR, resp) == CTRL_EX_ERROR);
  CHECK(exit_code_at(ERRATA_FATAL, resp) == CTRL_EX_OK);
}

TEST_CASE("An explicit severity is compared with the level, inclusive", "[exit_code]")
{
  auto const warn = execution_error(R"({"code": 3000, "severity": 4, "message": "Server already draining."})");

  CHECK(exit_code_at(ERRATA_NOTE, warn) == CTRL_EX_ERROR);
  CHECK(exit_code_at(ERRATA_WARN, warn) == CTRL_EX_ERROR);
  CHECK(exit_code_at(ERRATA_ERROR, warn) == CTRL_EX_OK);
  CHECK(exit_code_at(ERRATA_FATAL, warn) == CTRL_EX_OK);

  auto const fatal = execution_error(R"({"code": 1, "severity": 6, "message": "fatal"})");

  CHECK(exit_code_at(ERRATA_FATAL, fatal) == CTRL_EX_ERROR);
  CHECK(exit_code_at(ERRATA_ALERT, fatal) == CTRL_EX_OK);

  auto const diag = execution_error(R"({"code": 1, "severity": 0, "message": "diag"})");

  CHECK(exit_code_at(ERRATA_DIAG, diag) == CTRL_EX_ERROR);
  CHECK(exit_code_at(ERRATA_DEBUG, diag) == CTRL_EX_OK);
}

TEST_CASE("The most severe annotation decides", "[exit_code]")
{
  SECTION("A warning next to an annotation without severity still fails")
  {
    auto const resp =
      execution_error(R"({"code": 3000, "severity": 4, "message": "benign"}, {"code": 3000, "message": "real failure"})");

    CHECK(exit_code_at(ERRATA_ERROR, resp) == CTRL_EX_ERROR);
    CHECK(exit_code_at(ERRATA_FATAL, resp) == CTRL_EX_OK);
  }

  SECTION("The worst entry is found wherever it sits")
  {
    auto const resp =
      execution_error(R"({"code": 1, "severity": 3, "message": "note"}, {"code": 1, "severity": 7, "message": "alert"},)"
                      R"( {"code": 1, "severity": 4, "message": "warn"})");

    CHECK(exit_code_at(ERRATA_ALERT, resp) == CTRL_EX_ERROR);
    CHECK(exit_code_at(ERRATA_EMERGENCY, resp) == CTRL_EX_OK);
  }
}

TEST_CASE("Severities the server should never send always fail", "[exit_code]")
{
  // Not one of the levels: graded as the highest one, so no --error-level lets them through.
  for (auto const *severity : {"9", "256", "2147483647", "-1", "\"warn\"", "\"0\"", "\"4\"", "4.5", "true", "null", "[4]", "{}"}) {
    INFO("severity: " << severity);
    auto const resp = execution_error(std::string{R"({"code": 1, "severity": )"} + severity + R"(, "message": "m"})");

    CHECK(exit_code_at(ERRATA_EMERGENCY, resp) == CTRL_EX_ERROR);
  }
}

TEST_CASE("Errors other than a handler failure always exit 2", "[exit_code]")
{
  SECTION("Protocol error, no data")
  {
    auto const resp = make_error_response(R"({"code": -32601, "message": "Method not found"})");

    CHECK(exit_code_at(ERRATA_EMERGENCY, resp) == CTRL_EX_ERROR);
  }

  SECTION("Unauthorized carries data, but a low severity there does not count")
  {
    auto const resp = make_error_response(
      R"({"code": 10, "message": "Unauthorized action", "data": [{"code": 1, "severity": 4, "message": "Denied privileged API access"}]})");

    CHECK(exit_code_at(ERRATA_EMERGENCY, resp) == CTRL_EX_ERROR);
  }

  SECTION("Handler failure without annotations")
  {
    CHECK(exit_code_at(ERRATA_EMERGENCY, make_error_response(R"({"code": 9, "message": "Error during execution"})")) ==
          CTRL_EX_ERROR);
    CHECK(exit_code_at(ERRATA_EMERGENCY, execution_error("")) == CTRL_EX_ERROR);
  }

  SECTION("An error object that is not a map")
  {
    CHECK(exit_code_at(ERRATA_EMERGENCY, make_error_response(R"("boom")")) == CTRL_EX_ERROR);
  }
}

TEST_CASE("Data entries that are not maps always fail", "[exit_code]")
{
  for (auto const *data : {R"("just text")", "null", "[4]", "3"}) {
    INFO("data entry: " << data);
    CHECK(exit_code_at(ERRATA_EMERGENCY, execution_error(data)) == CTRL_EX_ERROR);
  }

  // "data" given as a map: each member is read as an entry, none of them a map.
  auto const map_data =
    make_error_response(R"({"code": 9, "message": "Error during execution", "data": {"code": 1, "severity": 8, "message": "m"}})");

  CHECK(exit_code_at(ERRATA_EMERGENCY, map_data) == CTRL_EX_ERROR);
}

TEST_CASE("Decoding the severity of a data entry", "[decoder]")
{
  auto const err = YAML::Load(R"({"code": 9, "message": "err", "data": [)"
                              R"({"code": 100, "severity": 5, "message": "has severity"},)"
                              R"({"code": 200, "message": "no severity"},)"
                              R"({"code": 300, "severity": 0, "message": "diag"},)"
                              R"({"code": 400, "severity": "high", "message": "not a number"},)"
                              R"({"code": 500, "severity": -3, "message": "negative"},)"
                              R"({"code": 600, "severity": "4", "message": "quoted"}]})")
                     .as<shared::rpc::JSONRPCError>();

  constexpr auto INVALID = shared::rpc::JSONRPCError::DataEntry::INVALID_SEVERITY;

  REQUIRE(err.data.size() == 6);
  CHECK(err.data[0].code == 100);
  CHECK(err.data[0].severity == 5);
  CHECK(err.data[0].message == "has severity");
  CHECK_FALSE(err.data[1].severity.has_value());
  CHECK(err.data[1].message == "no severity");
  CHECK(err.data[2].severity == 0);
  CHECK(err.data[3].severity == INVALID);
  CHECK(err.data[3].message == "not a number");
  CHECK(err.data[4].severity == -3);
  CHECK(err.data[5].severity == INVALID);
}

TEST_CASE("Decoding leaves the received error untouched", "[decoder]")
{
  // json output prints this same tree after the exit status is graded from it.
  char const *const received = R"({"code": 9, "message": "err", "data": [null, [1, 2], "text", {"code": 1, "message": "m"}]})";
  auto const        node     = YAML::Load(received);
  auto const        before   = YAML::Dump(node);

  auto const err = node.as<shared::rpc::JSONRPCError>();

  CHECK(err.data.size() == 4);
  CHECK(YAML::Dump(node) == before);
}

TEST_CASE("Printing a server error", "[printer]")
{
  shared::rpc::JSONRPCError err;
  err.code    = 9;
  err.message = "Error during execution";

  SECTION("Without severity, as before severities were sent")
  {
    err.data.push_back({3000, std::nullopt, "Server already draining."});
    CHECK(printed(err) == "Server Error found:\n[9] Error during execution\n- [3000] Server already draining.\n");
  }

  SECTION("With severity")
  {
    err.data.push_back({3000, 4, "Server already draining."});
    err.data.push_back({3000, 8, "last"});
    err.data.push_back({3000, 0, "lowest"});
    CHECK(printed(err) == "Server Error found:\n[9] Error during execution\n- [3000] Warn: Server already draining.\n- [3000] "
                          "Emergency: last\n- [3000] Diag: lowest\n");
  }

  SECTION("With a severity outside the known levels")
  {
    err.data.push_back({1, 9, "above"});
    err.data.push_back({1, -1, "below"});
    err.data.push_back({1, shared::rpc::JSONRPCError::DataEntry::INVALID_SEVERITY, "unreadable"});
    CHECK(printed(err) == "Server Error found:\n[9] Error during execution\n- [1] Severity(9): above\n- [1] Severity(-1): below\n"
                          "- [1] Severity(invalid): unreadable\n");
  }
}

TEST_CASE("Parsing --error-level", "[error_level]")
{
  CHECK(parse_error_level("diag") == swoc::Errata::severity_type(ERRATA_DIAG));
  CHECK(parse_error_level("debug") == swoc::Errata::severity_type(ERRATA_DEBUG));
  CHECK(parse_error_level("status") == swoc::Errata::severity_type(ERRATA_STATUS));
  CHECK(parse_error_level("note") == swoc::Errata::severity_type(ERRATA_NOTE));
  CHECK(parse_error_level("warn") == swoc::Errata::severity_type(ERRATA_WARN));
  CHECK(parse_error_level("error") == swoc::Errata::severity_type(ERRATA_ERROR));
  CHECK(parse_error_level("fatal") == swoc::Errata::severity_type(ERRATA_FATAL));
  CHECK(parse_error_level("alert") == swoc::Errata::severity_type(ERRATA_ALERT));
  CHECK(parse_error_level("emergency") == swoc::Errata::severity_type(ERRATA_EMERGENCY));

  CHECK(parse_error_level("WARN") == swoc::Errata::severity_type(ERRATA_WARN));
  CHECK(parse_error_level("Emergency") == swoc::Errata::severity_type(ERRATA_EMERGENCY));
  CHECK(parse_error_level("warning") == swoc::Errata::severity_type(ERRATA_WARN));
  CHECK(parse_error_level("WARNING") == swoc::Errata::severity_type(ERRATA_WARN));

  CHECK_FALSE(parse_error_level(""));
  CHECK_FALSE(parse_error_level("war"));
  CHECK_FALSE(parse_error_level("warnings"));
  CHECK_FALSE(parse_error_level("error "));
  CHECK_FALSE(parse_error_level("5"));
  CHECK_FALSE(parse_error_level("err"));
}
