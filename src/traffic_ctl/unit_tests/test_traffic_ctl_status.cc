/** @file

  Unit tests for appExitCodeFromResponse and related exit-code logic.

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

#include "TrafficCtlStatus.h"
#include "shared/rpc/yaml_codecs.h"
#include "tsutil/ts_errata.h"

int                         App_Exit_Status_Code = CTRL_EX_OK;
swoc::Errata::severity_type App_Exit_Level_Error = ERRATA_ERROR;

namespace
{

shared::rpc::JSONRPCResponse
make_success_response()
{
  shared::rpc::JSONRPCResponse resp;
  resp.result = YAML::Load(R"({"status": "ok"})");
  return resp;
}

shared::rpc::JSONRPCResponse
make_error_response(std::string_view json_error)
{
  shared::rpc::JSONRPCResponse resp;
  resp.error = YAML::Load(std::string{json_error});
  return resp;
}

} // namespace

// ---------------------------------------------------------------------------
// appExitCodeFromResponse
// ---------------------------------------------------------------------------

TEST_CASE("appExitCodeFromResponse - success response", "[exit_code]")
{
  App_Exit_Level_Error = ERRATA_ERROR;
  auto resp            = make_success_response();
  REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
}

TEST_CASE("appExitCodeFromResponse - error with empty data", "[exit_code]")
{
  App_Exit_Level_Error = ERRATA_ERROR;
  auto resp            = make_error_response(R"({"code": 9, "message": "Error during execution"})");
  REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
}

TEST_CASE("appExitCodeFromResponse - annotation without severity defaults to DIAG", "[exit_code]")
{
  auto resp = make_error_response(
    R"({"code": 9, "message": "Error during execution", "data": [{"code": 9999, "message": "something went wrong"}]})");

  SECTION("default threshold (ERROR) - DIAG < ERROR, exit 0")
  {
    App_Exit_Level_Error = ERRATA_ERROR;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
  }

  SECTION("threshold FATAL - DIAG < FATAL, exit 0")
  {
    App_Exit_Level_Error = ERRATA_FATAL;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
  }

  SECTION("threshold DIAG - DIAG >= DIAG, exit 2")
  {
    App_Exit_Level_Error = ERRATA_DIAG;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
  }
}

TEST_CASE("appExitCodeFromResponse - annotation with WARN severity", "[exit_code]")
{
  auto resp = make_error_response(
    R"({"code": 9, "message": "Error during execution", "data": [{"code": 9999, "severity": 4, "message": "Server already draining."}]})");

  SECTION("threshold ERROR - WARN < ERROR, exit 0")
  {
    App_Exit_Level_Error = ERRATA_ERROR;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
  }

  SECTION("threshold WARN - WARN >= WARN, exit 2")
  {
    App_Exit_Level_Error = ERRATA_WARN;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
  }

  SECTION("threshold NOTE - WARN >= NOTE, exit 2")
  {
    App_Exit_Level_Error = ERRATA_NOTE;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
  }

  SECTION("threshold FATAL - WARN < FATAL, exit 0")
  {
    App_Exit_Level_Error = ERRATA_FATAL;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
  }
}

TEST_CASE("appExitCodeFromResponse - annotation with ERROR severity", "[exit_code]")
{
  auto resp = make_error_response(
    R"({"code": 9, "message": "Error during execution", "data": [{"code": 9999, "severity": 5, "message": "hard error"}]})");

  SECTION("threshold ERROR - ERROR >= ERROR, exit 2")
  {
    App_Exit_Level_Error = ERRATA_ERROR;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
  }

  SECTION("threshold FATAL - ERROR < FATAL, exit 0")
  {
    App_Exit_Level_Error = ERRATA_FATAL;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
  }

  SECTION("threshold WARN - ERROR >= WARN, exit 2")
  {
    App_Exit_Level_Error = ERRATA_WARN;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
  }
}

TEST_CASE("appExitCodeFromResponse - mixed severities picks most severe", "[exit_code]")
{
  auto resp = make_error_response(R"({"code": 9, "message": "Error during execution", "data": [)"
                                  R"({"code": 9999, "severity": 4, "message": "warn msg"},)"
                                  R"({"code": 9999, "severity": 5, "message": "error msg"},)"
                                  R"({"code": 9999, "severity": 3, "message": "note msg"}]})");

  SECTION("threshold ERROR - most severe is ERROR, exit 2")
  {
    App_Exit_Level_Error = ERRATA_ERROR;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
  }

  SECTION("threshold FATAL - most severe is ERROR < FATAL, exit 0")
  {
    App_Exit_Level_Error = ERRATA_FATAL;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
  }
}

TEST_CASE("appExitCodeFromResponse - mixed: some with severity, some without", "[exit_code]")
{
  auto resp = make_error_response(R"({"code": 9, "message": "Error during execution", "data": [)"
                                  R"({"code": 9999, "severity": 4, "message": "warn"},)"
                                  R"({"code": 9999, "message": "no severity - defaults to DIAG"}]})");

  SECTION("threshold ERROR - most severe is WARN (4) < ERROR, exit 0")
  {
    App_Exit_Level_Error = ERRATA_ERROR;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
  }

  SECTION("threshold WARN - most severe is WARN (4) >= WARN, exit 2")
  {
    App_Exit_Level_Error = ERRATA_WARN;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
  }

  SECTION("threshold FATAL - most severe is WARN (4) < FATAL, exit 0")
  {
    App_Exit_Level_Error = ERRATA_FATAL;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
  }
}

TEST_CASE("appExitCodeFromResponse - all annotations only WARN", "[exit_code]")
{
  auto resp = make_error_response(R"({"code": 9, "message": "Error during execution", "data": [)"
                                  R"({"code": 9999, "severity": 4, "message": "warn 1"},)"
                                  R"({"code": 9999, "severity": 4, "message": "warn 2"}]})");

  SECTION("threshold ERROR - exit 0")
  {
    App_Exit_Level_Error = ERRATA_ERROR;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
  }

  SECTION("threshold WARN - exit 2")
  {
    App_Exit_Level_Error = ERRATA_WARN;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
  }

  SECTION("threshold DIAG - exit 2")
  {
    App_Exit_Level_Error = ERRATA_DIAG;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
  }
}

TEST_CASE("appExitCodeFromResponse - FATAL severity annotation", "[exit_code]")
{
  auto resp = make_error_response(
    R"({"code": 9, "message": "Error during execution", "data": [{"code": 9999, "severity": 6, "message": "fatal"}]})");

  SECTION("threshold FATAL - FATAL >= FATAL, exit 2")
  {
    App_Exit_Level_Error = ERRATA_FATAL;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_ERROR);
  }

  SECTION("threshold EMERGENCY - FATAL < EMERGENCY, exit 0")
  {
    App_Exit_Level_Error = ERRATA_EMERGENCY;
    REQUIRE(appExitCodeFromResponse(resp) == CTRL_EX_OK);
  }
}

// ---------------------------------------------------------------------------
// YAML::convert<JSONRPCError>::decode
// ---------------------------------------------------------------------------

TEST_CASE("JSONRPCError decode - severity field present", "[decoder]")
{
  auto node = YAML::Load(
    R"({"code": 9, "message": "Error during execution", "data": [{"code": 9999, "severity": 4, "message": "a warning"}]})");

  auto err = node.as<shared::rpc::JSONRPCError>();
  REQUIRE(err.code == 9);
  REQUIRE(err.message == "Error during execution");
  REQUIRE(err.data.size() == 1);
  REQUIRE(err.data[0].code == 9999);
  REQUIRE(err.data[0].severity == 4);
  REQUIRE(err.data[0].message == "a warning");
}

TEST_CASE("JSONRPCError decode - severity field absent", "[decoder]")
{
  auto node = YAML::Load(R"({"code": 9, "message": "Error during execution", "data": [{"code": 9999, "message": "no sev"}]})");

  auto err = node.as<shared::rpc::JSONRPCError>();
  REQUIRE(err.data.size() == 1);
  REQUIRE(err.data[0].severity == 0);
  REQUIRE(err.data[0].code == 9999);
  REQUIRE(err.data[0].message == "no sev");
}

TEST_CASE("JSONRPCError decode - mixed severity present and absent", "[decoder]")
{
  auto node = YAML::Load(R"({"code": 9, "message": "err", "data": [)"
                         R"({"code": 100, "severity": 5, "message": "has sev"},)"
                         R"({"code": 200, "message": "no sev"},)"
                         R"({"code": 300, "severity": 0, "message": "diag level"}]})");

  auto err = node.as<shared::rpc::JSONRPCError>();
  REQUIRE(err.data.size() == 3);

  CHECK(err.data[0].severity == 5);
  CHECK(err.data[0].code == 100);

  CHECK(err.data[1].severity == 0);
  CHECK(err.data[1].code == 200);

  CHECK(err.data[2].severity == 0);
  CHECK(err.data[2].code == 300);
}

TEST_CASE("JSONRPCError decode - no data section", "[decoder]")
{
  auto node = YAML::Load(R"({"code": -32600, "message": "Invalid Request"})");

  auto err = node.as<shared::rpc::JSONRPCError>();
  REQUIRE(err.code == -32600);
  REQUIRE(err.message == "Invalid Request");
  REQUIRE(err.data.empty());
}

TEST_CASE("JSONRPCError decode - empty data array", "[decoder]")
{
  auto node = YAML::Load(R"({"code": 9, "message": "err", "data": []})");

  auto err = node.as<shared::rpc::JSONRPCError>();
  REQUIRE(err.data.empty());
}
