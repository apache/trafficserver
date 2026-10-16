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
#include "jsonrpc/ctrl_yaml_codecs.h"

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
  // Not one of the levels: graded as the highest one, so no --error-level lets them through. Without the range check, -252 and
  // -256 would narrow to Warn and Diag.
  for (auto const *severity :
       {"9", "256", "2147483647", "-1", "-252", "-256", "\"warn\"", "\"0\"", "\"4\"", "4.5", "true", "null", "[4]", "{}"}) {
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
}

TEST_CASE("Data that is not a list is one unreadable entry", "[decoder]")
{
  // So the error is still printed as a server error, and always fails.
  for (auto const *data : {R"({"code": 1, "severity": 4, "message": "m"})", R"("text")", "null"}) {
    INFO("data: " << data);
    auto const resp = make_error_response(std::string{R"({"code": 9, "message": "Error during execution", "data": )"} + data + "}");

    shared::rpc::JSONRPCError err;
    REQUIRE_NOTHROW(err = resp.error.as<shared::rpc::JSONRPCError>());
    REQUIRE(err.data.size() == 1);
    CHECK(err.data[0].severity == shared::rpc::JSONRPCError::DataEntry::INVALID_SEVERITY);

    std::ostringstream os;
    os << err;
    CHECK(os.str() == "Server Error found:\n[9] Error during execution\n- [0] Severity(invalid): \n");
    CHECK(exit_code_at(ERRATA_EMERGENCY, resp) == CTRL_EX_ERROR);
  }
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

TEST_CASE("Listing the --error-level values", "[error_level]")
{
  CHECK(error_level_names() == "diag, debug, status, note, warn (or warning), error, fatal, alert, emergency");
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

namespace
{
using ReloadInfo = ConfigReloadResponse::ReloadInfo;
using LogEntry   = ConfigReloadResponse::LogEntry;

ReloadInfo
reload_task(std::string status, std::vector<DiagsLevel> const &levels = {}, std::vector<ReloadInfo> sub_tasks = {})
{
  ReloadInfo task;
  task.status = std::move(status);
  for (auto level : levels) {
    task.logs.push_back({level, "entry"});
  }
  task.sub_tasks = std::move(sub_tasks);
  return task;
}

/// @a task with one more entry whose level the server sent malformed.
ReloadInfo
with_unknown_level(ReloadInfo task)
{
  task.logs.push_back({DL_Undefined, "entry", true});
  return task;
}

int
reload_exit_code_at(swoc::Errata::Severity level, ReloadInfo const &task)
{
  App_Exit_Level_Error = level;
  int const code       = appExitCodeFromReload(task);
  App_Exit_Level_Error = ERRATA_ERROR;
  return code;
}

ConfigReloadResponse
decode_reload(std::string const &json)
{
  return YAML::Load(json).as<ConfigReloadResponse>();
}

LogEntry
decoded_entry(std::string const &level_json)
{
  auto const resp = decode_reload(R"({"tasks": [{"status": "success", "logs": [{"level": )" + level_json + R"(, "text": "t"}]}]})");
  REQUIRE(resp.tasks.size() == 1);
  REQUIRE(resp.tasks[0].logs.size() == 1);
  return resp.tasks[0].logs[0];
}

bool
decodes_as(std::string const &level_json, DiagsLevel level)
{
  auto const entry = decoded_entry(level_json);
  return entry.level == level && !entry.unknown_level;
}

/// Marked unknown for the grade, and printed as @a shown.
bool
decodes_as_unknown(std::string const &level_json, DiagsLevel shown = DL_Undefined)
{
  auto const entry = decoded_entry(level_json);
  return entry.level == shown && entry.unknown_level;
}
} // namespace

TEST_CASE("A finished reload is graded by its most severe entry, inclusive", "[reload]")
{
  auto const task = reload_task("success", {DL_Note, DL_Warning, DL_Note});

  CHECK(reload_exit_code_at(ERRATA_ERROR, task) == CTRL_EX_OK);
  CHECK(reload_exit_code_at(ERRATA_WARN, task) == CTRL_EX_ERROR);
  CHECK(reload_exit_code_at(ERRATA_NOTE, task) == CTRL_EX_ERROR);
}

TEST_CASE("A success without graded entries passes at any level", "[reload]")
{
  CHECK(reload_exit_code_at(ERRATA_DIAG, reload_task("success")) == CTRL_EX_OK);
  CHECK(reload_exit_code_at(ERRATA_DIAG, reload_task("success", {DL_Undefined, DL_Undefined})) == CTRL_EX_OK);
}

TEST_CASE("Severity 0 is still a severity", "[reload]")
{
  CHECK(reload_exit_code_at(ERRATA_DIAG, reload_task("success", {DL_Diag})) == CTRL_EX_ERROR);
  CHECK(reload_exit_code_at(ERRATA_DEBUG, reload_task("success", {DL_Diag})) == CTRL_EX_OK);
}

TEST_CASE("Entries of sub-tasks count, at any depth", "[reload]")
{
  auto const deep = reload_task("success", {DL_Note}, {reload_task("success", {DL_Note}, {reload_task("success", {DL_Warning})})});

  CHECK(reload_exit_code_at(ERRATA_WARN, deep) == CTRL_EX_ERROR);
  CHECK(reload_exit_code_at(ERRATA_ERROR, deep) == CTRL_EX_OK);

  SECTION("A later sibling does not hide an earlier one")
  {
    auto const siblings = reload_task("success", {}, {reload_task("success", {DL_Warning}), reload_task("success", {DL_Note})});
    CHECK(reload_exit_code_at(ERRATA_WARN, siblings) == CTRL_EX_ERROR);
  }

  SECTION("A failed sub-task deep down")
  {
    auto const failed =
      reload_task("fail", {}, {reload_task("success"), reload_task("fail", {}, {reload_task("fail", {DL_Fatal})})});
    CHECK(reload_exit_code_at(ERRATA_FATAL, failed) == CTRL_EX_ERROR);
    CHECK(reload_exit_code_at(ERRATA_ALERT, failed) == CTRL_EX_OK);
  }
}

TEST_CASE("Entries of a task that did not fail count as at most a warning", "[reload]")
{
  for (auto level : {DL_Error, DL_Fatal, DL_Emergency}) {
    CAPTURE(level);
    auto const task = reload_task("success", {DL_Note, level});

    CHECK(reload_exit_code_at(ERRATA_ERROR, task) == CTRL_EX_OK);
    CHECK(reload_exit_code_at(ERRATA_WARN, task) == CTRL_EX_ERROR);
  }

  SECTION("Under a parent that failed, too")
  {
    auto const task = reload_task("fail", {DL_Error}, {reload_task("success", {DL_Fatal})});
    CHECK(reload_exit_code_at(ERRATA_ERROR, task) == CTRL_EX_ERROR);
    CHECK(reload_exit_code_at(ERRATA_FATAL, task) == CTRL_EX_OK);
  }

  SECTION("And of a sub-task still running, or in a state the client does not know, under a finished parent")
  {
    for (auto const *status : {"in_progress", "created", "skipped", ""}) {
      CAPTURE(status);
      auto const task = reload_task("success", {}, {reload_task(status, {DL_Error})});
      CHECK(reload_exit_code_at(ERRATA_ERROR, task) == CTRL_EX_OK);
      CHECK(reload_exit_code_at(ERRATA_WARN, task) == CTRL_EX_ERROR);
      CHECK(reload_exit_code_at(ERRATA_DIAG, reload_task("success", {}, {reload_task(status)})) == CTRL_EX_OK);
    }
  }
}

TEST_CASE("A failed reload counts as at least an error", "[reload]")
{
  CHECK(reload_exit_code_at(ERRATA_ERROR, reload_task("fail")) == CTRL_EX_ERROR);
  CHECK(reload_exit_code_at(ERRATA_ERROR, reload_task("fail", {DL_Note, DL_Undefined})) == CTRL_EX_ERROR);
  CHECK(reload_exit_code_at(ERRATA_FATAL, reload_task("fail", {DL_Error})) == CTRL_EX_OK);
  CHECK(reload_exit_code_at(ERRATA_FATAL, reload_task("fail", {DL_Error}, {reload_task("fail", {DL_Fatal})})) == CTRL_EX_ERROR);
  CHECK(reload_exit_code_at(ERRATA_ALERT, reload_task("fail", {DL_Fatal})) == CTRL_EX_OK);
}

TEST_CASE("A reload that timed out never finished", "[reload]")
{
  SECTION("At or above the level it fails, like a failed reload")
  {
    CHECK(reload_exit_code_at(ERRATA_ERROR, reload_task("timeout")) == CTRL_EX_ERROR);
    CHECK(reload_exit_code_at(ERRATA_ERROR, reload_task("timeout", {DL_Note, DL_Undefined})) == CTRL_EX_ERROR);
    CHECK(reload_exit_code_at(ERRATA_FATAL, reload_task("timeout", {DL_Error}, {reload_task("fail", {DL_Fatal})})) ==
          CTRL_EX_ERROR);
  }

  SECTION("Below the level it is not done, not a success")
  {
    CHECK(reload_exit_code_at(ERRATA_FATAL, reload_task("timeout")) == CTRL_EX_TEMPFAIL);
    CHECK(reload_exit_code_at(ERRATA_FATAL, reload_task("timeout", {DL_Error})) == CTRL_EX_TEMPFAIL);
    CHECK(reload_exit_code_at(ERRATA_ALERT, reload_task("timeout", {DL_Fatal})) == CTRL_EX_TEMPFAIL);
  }

  SECTION("So is a reload with a sub-task that timed out")
  {
    auto const under_success = reload_task("success", {}, {reload_task("success"), reload_task("timeout")});
    CHECK(reload_exit_code_at(ERRATA_ERROR, under_success) == CTRL_EX_ERROR);
    CHECK(reload_exit_code_at(ERRATA_FATAL, under_success) == CTRL_EX_TEMPFAIL);

    auto const under_fail = reload_task("fail", {}, {reload_task("timeout")});
    CHECK(reload_exit_code_at(ERRATA_FATAL, under_fail) == CTRL_EX_TEMPFAIL);
  }

  SECTION("A failure without a timeout below the level is not")
  {
    CHECK(reload_exit_code_at(ERRATA_FATAL, reload_task("fail", {}, {reload_task("fail")})) == CTRL_EX_OK);
  }
}

TEST_CASE("A sub-task that failed counts as at least an error, whatever its parent says", "[reload]")
{
  auto const task = reload_task("success", {}, {reload_task("success"), reload_task("fail")});

  CHECK(reload_exit_code_at(ERRATA_ERROR, task) == CTRL_EX_ERROR);
  CHECK(reload_exit_code_at(ERRATA_FATAL, task) == CTRL_EX_OK);
}

TEST_CASE("A reload that has not finished, or is in a state the client does not know, is not graded", "[reload]")
{
  for (auto const *status : {"created", "in_progress", "invalid", "", "SUCCESS", "FAIL", "skipped"}) {
    CAPTURE(status);
    CHECK(reload_exit_code_at(ERRATA_DIAG, reload_task(status, {DL_Emergency}, {reload_task("fail", {DL_Emergency})})) ==
          CTRL_EX_OK);
    CHECK(reload_exit_code_at(ERRATA_DIAG, reload_task(status, {}, {reload_task("timeout")})) == CTRL_EX_OK);
  }
}

TEST_CASE("An entry whose level the server sent malformed counts as the highest", "[reload]")
{
  SECTION("In a task that failed")
  {
    auto const task = with_unknown_level(reload_task("fail"));
    CHECK(reload_exit_code_at(ERRATA_EMERGENCY, task) == CTRL_EX_ERROR);
  }

  SECTION("In a task that did not fail, as at most a warning")
  {
    auto const task = reload_task("success", {}, {with_unknown_level(reload_task("success"))});
    CHECK(reload_exit_code_at(ERRATA_WARN, task) == CTRL_EX_ERROR);
    CHECK(reload_exit_code_at(ERRATA_ERROR, task) == CTRL_EX_OK);
  }

  SECTION("Next to lower entries, before or after them")
  {
    auto first = with_unknown_level(reload_task("fail"));
    first.logs.push_back({DL_Note, "entry"});
    CHECK(reload_exit_code_at(ERRATA_EMERGENCY, first) == CTRL_EX_ERROR);
    CHECK(reload_exit_code_at(ERRATA_EMERGENCY, with_unknown_level(reload_task("fail", {DL_Note}))) == CTRL_EX_ERROR);
  }
}

TEST_CASE("Decoding the level of a reload log entry", "[reload][decoder]")
{
  CHECK(decodes_as("0", DL_Diag));
  CHECK(decodes_as("4", DL_Warning));
  CHECK(decodes_as("8", DL_Emergency));
  CHECK(decodes_as("9", DL_Undefined));
  // The server sends every scalar quoted.
  CHECK(decodes_as(R"("4")", DL_Warning));
  CHECK(decodes_as(R"("9")", DL_Undefined));

  SECTION("A level the server should never send is marked unknown, and printed as it always was")
  {
    // An integer out of range prints clamped, the way the printer always showed it.
    CHECK(decodes_as_unknown("10", DL_Emergency));
    CHECK(decodes_as_unknown("42", DL_Emergency));
    CHECK(decodes_as_unknown(R"("10")", DL_Emergency));
    CHECK(decodes_as_unknown("-1", DL_Diag));
    CHECK(decodes_as_unknown("-2147483648", DL_Diag));
    // Anything else prints without a tag.
    CHECK(decodes_as_unknown("2147483648"));
    CHECK(decodes_as_unknown(R"("")"));
    CHECK(decodes_as_unknown(R"("warn")"));
    CHECK(decodes_as_unknown("4.5"));
    CHECK(decodes_as_unknown("null"));
    CHECK(decodes_as_unknown("[4]"));
    CHECK(decodes_as_unknown(R"({"v": 4})"));
  }

  SECTION("No level, or an entry that is not a map, is unleveled")
  {
    auto const resp = decode_reload(R"({"tasks": [{"status": "success", "logs": [{"text": "a"}, "b"]}]})");
    REQUIRE(resp.tasks[0].logs.size() == 2);
    CHECK(resp.tasks[0].logs[0].level == DL_Undefined);
    CHECK_FALSE(resp.tasks[0].logs[0].unknown_level);
    CHECK(resp.tasks[0].logs[1].level == DL_Undefined);
    CHECK_FALSE(resp.tasks[0].logs[1].unknown_level);
  }
}

TEST_CASE("A decoded failed reload with a malformed level fails at any level", "[reload][decoder]")
{
  auto const resp = decode_reload(
    R"({"tasks": [{"status": "fail", "logs": [], "sub_tasks": [{"status": "fail", "logs": [{"level": "x", "text": "t"}]}]}]})");
  REQUIRE(resp.tasks.size() == 1);
  CHECK(reload_exit_code_at(ERRATA_EMERGENCY, resp.tasks[0]) == CTRL_EX_ERROR);
}
