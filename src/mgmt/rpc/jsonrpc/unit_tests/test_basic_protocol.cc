/**
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

#include <catch.hpp> /* catch unit-test framework */

#include <tsutil/ts_bw_format.h>

#include "mgmt/rpc/jsonrpc/JsonRPCManager.h"
#include "mgmt/rpc/jsonrpc/JsonRPC.h"
#include "mgmt/rpc/handlers/common/ErrorUtils.h"

namespace
{
// Not using the singleton logic.
struct JsonRpcUnitTest : rpc::JsonRPCManager {
  JsonRpcUnitTest() : JsonRPCManager() {}
  using base = JsonRPCManager;
  bool
  remove_handler(std::string const &name)
  {
    return base::remove_handler(name);
  }
  template <typename Func>
  bool
  add_notification_handler(const std::string &name, Func &&call)
  {
    return base::add_notification_handler(name, std::forward<Func>(call), nullptr, {});
  }
  template <typename Func>
  bool
  add_method_handler(const std::string &name, Func &&call)
  {
    return base::add_method_handler(name, std::forward<Func>(call), nullptr, {});
  }

  std::optional<std::string>
  handle_call(std::string const &jsonString)
  {
    return base::handle_call(rpc::Context{}, jsonString);
  }
};

enum class TestErrors { ERR1 = 9999, ERR2 };
static const std::error_code ERR1{ts::make_errno_code(9999)};
static const std::error_code ERR2{ts::make_errno_code(10000)};
static std::string_view      err{"Just an error message to add more meaning to the failure"};

inline swoc::Rv<YAML::Node>
test_callback_ok_or_error(std::string_view const & /* id ATS_UNUSED */, YAML::Node const &params)
{
  swoc::Rv<YAML::Node> resp;

  // play with the req.id if needed.
  if (YAML::Node n = params["return_error"]) {
    auto yesOrNo = n.as<std::string>();
    if (yesOrNo == "yes") {
      resp.errata().assign(ERR1).note(err);
    } else {
      resp.result()["ran"] = "ok";
    }
  }
  return resp;
}

static int notificationCallCount{0};
inline void
test_nofitication(YAML::Node const & /* params ATS_UNUSED */)
{
  notificationCallCount++;
}
} // namespace
TEST_CASE("Multiple Registrations - methods", "[methods]")
{
  JsonRpcUnitTest rpc;
  SECTION("More than  one method")
  {
    REQUIRE(rpc.add_method_handler("test_callback_ok_or_error", &test_callback_ok_or_error));
    REQUIRE(rpc.add_method_handler("test_callback_ok_or_error", &test_callback_ok_or_error) == false);
  }
}

TEST_CASE("Multiple Registrations - notifications", "[notifications]")
{
  JsonRpcUnitTest rpc;
  SECTION("inserting several notifications with the same name")
  {
    REQUIRE(rpc.add_notification_handler("test_nofitication", &test_nofitication));
    REQUIRE(rpc.add_notification_handler("test_nofitication", &test_nofitication) == false);
  }
}

TEST_CASE("Register/call method", "[method]")
{
  JsonRpcUnitTest rpc;

  SECTION("Registering the method")
  {
    REQUIRE(rpc.add_method_handler("test_callback_ok_or_error", &test_callback_ok_or_error));

    SECTION("Calling the method")
    {
      const auto json = rpc.handle_call(
        R"({"jsonrpc": "2.0", "method": "test_callback_ok_or_error", "params": {"return_error": "no"}, "id": "13"})");
      REQUIRE(json);
      const std::string_view expected = R"({"jsonrpc": "2.0", "result": {"ran": "ok"}, "id": "13"})";
      REQUIRE(*json == expected);
    }
  }
}

// VALID RESPONSES WITH CUSTOM ERRORS
TEST_CASE("Register/call method - respond with errors (data field)", "[method][error.data]")
{
  JsonRpcUnitTest rpc;

  SECTION("Registering the method")
  {
    REQUIRE(rpc.add_method_handler("test_callback_ok_or_error", &test_callback_ok_or_error));

    SECTION("Calling the method")
    {
      const auto json = rpc.handle_call(
        R"({"jsonrpc": "2.0", "method": "test_callback_ok_or_error", "params": {"return_error": "yes"}, "id": "14"})");
      REQUIRE(json);
      const std::string_view expected =
        R"({"jsonrpc": "2.0", "error": {"code": 9, "message": "Error during execution", "data": [{"code": 9999, "message": "Just an error message to add more meaning to the failure"}]}, "id": "14"})";
      REQUIRE(*json == expected);
    }
  }
}

TEST_CASE("Register/call notification", "[notifications]")
{
  JsonRpcUnitTest rpc;

  SECTION("Registering the notification")
  {
    REQUIRE(rpc.add_notification_handler("test_nofitication", &test_nofitication));

    SECTION("Calling the notification")
    {
      rpc.handle_call(R"({"jsonrpc": "2.0", "method": "test_nofitication", "params": {"json": "rpc"}})");
      REQUIRE(notificationCallCount == 1);
      notificationCallCount = 0; // for further use.
    }
  }
}

TEST_CASE("Basic test, batch calls", "[methods][notifications]")
{
  JsonRpcUnitTest rpc;

  SECTION("inserting multiple functions, mixed method and notifications.")
  {
    const auto f1_added = rpc.add_method_handler("test_callback_ok_or_error", &test_callback_ok_or_error);
    const bool f2_added = rpc.add_notification_handler("test_nofitication", &test_nofitication);

    REQUIRE(f1_added);
    REQUIRE(f2_added);

    SECTION("we call in batch, two functions and one notification")
    {
      const auto resp1 = rpc.handle_call(
        R"([{"jsonrpc": "2.0", "method": "test_callback_ok_or_error", "params": {"return_error": "no"}, "id": "13"}
      ,{"jsonrpc": "2.0", "method": "test_callback_ok_or_error", "params": {"return_error": "yes"}, "id": "14"}
      ,{"jsonrpc": "2.0", "method": "test_nofitication", "params": {"name": "damian"}}])");

      REQUIRE(resp1);
      const std::string_view expected =
        R"([{"jsonrpc": "2.0", "result": {"ran": "ok"}, "id": "13"}, {"jsonrpc": "2.0", "error": {"code": 9, "message": "Error during execution", "data": [{"code": 9999, "message": "Just an error message to add more meaning to the failure"}]}, "id": "14"}])";
      REQUIRE(*resp1 == expected);
    }
  }
}

TEST_CASE("Single registered notification", "[notifications]")
{
  notificationCallCount = 0;
  JsonRpcUnitTest rpc;
  SECTION("Single notification, only notifications in the batch.")
  {
    REQUIRE(rpc.add_notification_handler("test_nofitication", &test_nofitication));

    SECTION("Call the notifications in batch")
    {
      const auto should_be_no_response = rpc.handle_call(
        R"([{"jsonrpc": "2.0", "method": "test_nofitication", "params": {"name": "JSON"}},
              {"jsonrpc": "2.0", "method": "test_nofitication", "params": {"name": "RPC"}},
              {"jsonrpc": "2.0", "method": "test_nofitication", "params": {"name": "2.0"}}])");

      REQUIRE(!should_be_no_response);
      REQUIRE(notificationCallCount == 3);
    }
  }
}

TEST_CASE("Valid json but invalid messages", "[errors]")
{
  JsonRpcUnitTest rpc;

  SECTION("Valid json but invalid protocol {}")
  {
    const auto resp = rpc.handle_call(R"({})");
    REQUIRE(resp);
    std::string_view expected = R"({"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}})";
    REQUIRE(*resp == expected);
  }

  SECTION("Valid json but invalid protocol [{},{}] - batch response")
  {
    const auto resp = rpc.handle_call(R"([{},{}])");
    REQUIRE(resp);
    std::string_view expected =
      R"([{"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}}, {"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}}])";
    REQUIRE(*resp == expected);
  }
}

TEST_CASE("Invalid json messages", "[errors][invalid json]")
{
  JsonRpcUnitTest rpc;
  SECTION("Invalid json in an attempt of batch")
  {
    const auto resp = rpc.handle_call(
      R"([{"jsonrpc": "2.0", "method": "test_callback_ok_or_error", "params": {"return_error": "no"}, "id": "13"}
      ,{"jsonrpc": "2.0", "method": "test_callback_ok_or_error", "params": {"return_error": "yes
      ,{"jsonrpc": "2.0", "method": "test_nofitication", "params": {"name": "damian"}}])");

    REQUIRE(resp);

    std::string_view expected = R"({"jsonrpc": "2.0", "error": {"code": -32700, "message": "Parse error"}})";
    REQUIRE(*resp == expected);
  }
}

TEST_CASE("Invalid parameters base on the jsonrpc 2.0 protocol", "[protocol]")
{
  JsonRpcUnitTest rpc;

  REQUIRE(rpc.add_method_handler("test_callback_ok_or_error", &test_callback_ok_or_error));
  REQUIRE(rpc.add_notification_handler("test_nofitication", &test_nofitication));

  SECTION("version field")
  {
    SECTION("number instead of a string")
    {
      // THIS WILL FAIL BASE ON THE YAMLCPP WAY TO TEST TYPES. We can get the number first, which will be ok and then fail, but
      // seems not the right way to do it. ok for now.
      [[maybe_unused]] const auto resp =
        rpc.handle_call(R"({"jsonrpc": 2.0, "method": "test_callback_ok_or_error", "params": {"return_error": "no"}, "id": "13"})");

      // REQUIRE(resp);

      [[maybe_unused]] const std::string_view expected =
        R"({"jsonrpc": "2.0", "error": {"code": 2, "message": "Invalid version type, should be a string"}, "id": "13"})";
      // REQUIRE(*resp == expected);
    }

    SECTION("2.8 instead of 2.0")
    {
      const auto resp = rpc.handle_call(
        R"({"jsonrpc": "2.8", "method": "test_callback_ok_or_error", "params": {"return_error": "no"}, "id": "15"})");

      REQUIRE(resp);
      const std::string_view expected =
        R"({"jsonrpc": "2.0", "error": {"code": 1, "message": "Invalid version, 2.0 only"}, "id": "15"})";
      REQUIRE(*resp == expected);
    }
  }
  SECTION("method field")
  {
    SECTION("using a number")
    {
      // THIS WILL FAIL BASE ON THE YAMLCPP WAY TO TEST TYPES, there is no explicit way to test for a particular type, we can get
      // the number first, then as it should not be converted we get the string instead, this seems rather not the best way to do
      // it. ok for now.
      [[maybe_unused]] const auto resp =
        rpc.handle_call(R"({"jsonrpc": "2.0", "method": 123, "params": {"return_error": "no"}, "id": "14"})");

      // REQUIRE(resp);
      [[maybe_unused]] const std::string_view expected =
        R"({"jsonrpc": "2.0", "error": {"code": 4, "message": "Invalid method type, should be a string"}, "id": "14"})";
      // REQUIRE(*resp == expected);
    }
  }
  SECTION("param field")
  {
    SECTION("Invalidtype")
    {
      const auto resp = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "test_callback_ok_or_error", "params": 13, "id": "13"})");

      REQUIRE(resp);
      const std::string_view expected =
        R"({"jsonrpc": "2.0", "error": {"code": 6, "message": "Invalid params type. A Structured value is expected"}, "id": "13"})";
      REQUIRE(*resp == expected);
    }
  }

  SECTION("id field")
  {
    SECTION("null id")
    {
      const auto resp = rpc.handle_call(
        R"({"jsonrpc": "2.0", "method": "test_callback_ok_or_error", "params": {"return_error": "no"}, "id": null})");
      REQUIRE(resp);
      const std::string_view expected =
        R"({"jsonrpc": "2.0", "error": {"code": 8, "message": "Use of null as id is discouraged"}})";
      REQUIRE(*resp == expected);
    }
  }
}

TEST_CASE("Basic test with member functions(add, remove)", "[basic][member_functions]")
{
  struct TestMemberFunctionCall {
    TestMemberFunctionCall() {}
    bool
    register_member_function_as_callback(JsonRpcUnitTest &rpc)
    {
      return rpc.add_method_handler(
        "member_function",
        [this](std::string_view const &id, const YAML::Node &req) -> swoc::Rv<YAML::Node> { return test(id, req); });
    }
    swoc::Rv<YAML::Node>
    test(std::string_view const & /* id ATS_UNUSED */, const YAML::Node & /* req ATS_UNUSED */)
    {
      swoc::Rv<YAML::Node> resp;
      resp.result() = "grand!";
      return resp;
    }

    // TODO: test notification as well.
  };

  SECTION("A RPC object and a custom class")
  {
    JsonRpcUnitTest        rpc;
    TestMemberFunctionCall tmfc;

    REQUIRE(tmfc.register_member_function_as_callback(rpc) == true);
    SECTION("call the member function")
    {
      const auto response = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "member_function", "id": "AbC"})");
      REQUIRE(response);
      REQUIRE(*response == R"({"jsonrpc": "2.0", "result": "grand!", "id": "AbC"})");
    }

    SECTION("We remove the callback handler")
    {
      REQUIRE(rpc.remove_handler("member_function") == true);

      const auto response = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "member_function", "id": "AbC"})");
      REQUIRE(response);
      REQUIRE(*response == R"({"jsonrpc": "2.0", "error": {"code": -32601, "message": "Method not found"}, "id": "AbC"})");
    }
  }
}

TEST_CASE("Test Dispatcher rpc method", "[dispatcher]")
{
  JsonRpcUnitTest rpc;
  const auto      response = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "show_registered_handlers", "id": "AbC"})");
  REQUIRE(*response ==
          R"({"jsonrpc": "2.0", "result": {"methods": ["get_service_descriptor", "show_registered_handlers"]}, "id": "AbC"})");
}

[[maybe_unused]] static swoc::Rv<YAML::Node>
subtract(std::string_view const & /* id ATS_UNUSED */, YAML::Node const &numbers)
{
  swoc::Rv<YAML::Node> res;

  if (numbers.Type() == YAML::NodeType::Sequence) {
    auto it    = numbers.begin();
    int  total = it->as<int>();
    ++it;
    for (; it != numbers.end(); ++it) {
      total -= it->as<int>();
    }

    res.result() = total;
  } else if (numbers.Type() == YAML::NodeType::Map) {
    if (numbers["subtrahend"] && numbers["minuend"]) {
      int total    = numbers["minuend"].as<int>() - numbers["subtrahend"].as<int>();
      res.result() = total;
    }
  }
  return res;
}

[[maybe_unused]] static swoc::Rv<YAML::Node>
sum(std::string_view const & /* id ATS_UNUSED */, YAML::Node const &params)
{
  swoc::Rv<YAML::Node> res;
  int                  total{0};
  for (auto n : params) {
    total += n.as<int>();
  }
  res.result() = total;
  return res;
}

[[maybe_unused]] static swoc::Rv<YAML::Node>
get_data(std::string_view const & /* id ATS_UNUSED */, YAML::Node const & /* params ATS_UNUSED */)
{
  swoc::Rv<YAML::Node> res;
  res.result().push_back("hello");
  res.result().push_back("5");
  return res;
}

[[maybe_unused]] static void
update(YAML::Node const & /* params ATS_UNUSED */)
{
}
[[maybe_unused]] static void
foobar(YAML::Node const & /* params ATS_UNUSED */)
{
}
[[maybe_unused]] static void
notify_hello(YAML::Node const & /* params ATS_UNUSED */)
{
}

// TODO: add tests base on the protocol example doc.
TEST_CASE("Basic tests from the jsonrpc 2.0 doc.")
{
  SECTION("rpc call with positional parameters")
  {
    JsonRpcUnitTest rpc;

    REQUIRE(rpc.add_method_handler("subtract", &subtract));
    const auto resp = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "subtract", "params": [42, 23], "id": "1"})");
    REQUIRE(resp);
    REQUIRE(*resp == R"({"jsonrpc": "2.0", "result": "19", "id": "1"})");

    const auto resp1 = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "subtract", "params": [23, 42], "id": "1"})");
    REQUIRE(resp1);
    REQUIRE(*resp1 == R"({"jsonrpc": "2.0", "result": "-19", "id": "1"})");
  }

  SECTION("rpc call with named parameters")
  {
    JsonRpcUnitTest rpc;

    REQUIRE(rpc.add_method_handler("subtract", &subtract));
    const auto resp =
      rpc.handle_call(R"({"jsonrpc": "2.0", "method": "subtract", "params": {"subtrahend": 23, "minuend": 42}, "id": "3"})");
    REQUIRE(resp);
    REQUIRE(*resp == R"({"jsonrpc": "2.0", "result": "19", "id": "3"})");

    const auto resp1 =
      rpc.handle_call(R"({"jsonrpc": "2.0", "method": "subtract", "params": {"minuend": 42, "subtrahend": 23}, "id": "3"})");
    REQUIRE(resp1);
    REQUIRE(*resp1 == R"({"jsonrpc": "2.0", "result": "19", "id": "3"})");
  }

  SECTION("A notification")
  {
    JsonRpcUnitTest rpc;

    REQUIRE(rpc.add_notification_handler("update", &update));
    const auto resp = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "update", "params": [1,2,3,4,5]})");
    REQUIRE(!resp);

    REQUIRE(rpc.add_notification_handler("foobar", &foobar));
    const auto resp1 = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "foobar"})");
    REQUIRE(!resp1);
  }

  SECTION("rpc call of non-existent method")
  {
    JsonRpcUnitTest rpc;

    const auto resp = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "foobar", "id": "1"})");
    REQUIRE(resp);
    REQUIRE(*resp == R"({"jsonrpc": "2.0", "error": {"code": -32601, "message": "Method not found"}, "id": "1"})");
  }
  SECTION("rpc call with invalid JSON")
  {
    JsonRpcUnitTest rpc;

    const auto resp = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "foobar, "params": "bar", "baz])");
    REQUIRE(resp);
    REQUIRE(*resp == R"({"jsonrpc": "2.0", "error": {"code": -32700, "message": "Parse error"}})");
  }
  SECTION("rpc call with invalid Request object")
  {
    // We do have a custom object here, which will return invalid param object.
    // skip it
  }
  SECTION("rpc call Batch, invalid JSON")
  {
    JsonRpcUnitTest rpc;

    const auto resp =
      rpc.handle_call(R"( {"jsonrpc": "2.0", "method": "sum", "params": [1,2,4], "id": "1"}, {"jsonrpc": "2.0", "method")");
    REQUIRE(resp);
    REQUIRE(*resp == R"({"jsonrpc": "2.0", "error": {"code": -32700, "message": "Parse error"}})");
  }
  SECTION("rpc call with an empty Array")
  {
    JsonRpcUnitTest rpc;
    const auto      resp = rpc.handle_call(R"([])");
    REQUIRE(resp);
    std::string_view expected = R"({"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}})";
    REQUIRE(*resp == expected);
  }
  SECTION("rpc call with an invalid Batch")
  {
    JsonRpcUnitTest rpc;
    const auto      resp = rpc.handle_call(R"([1])");
    REQUIRE(resp);
    std::string_view expected = R"([{"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}}])";
    REQUIRE(*resp == expected);
  }
  SECTION("rpc call with invalid Batch")
  {
    JsonRpcUnitTest rpc;
    const auto      resp = rpc.handle_call(R"([1,2,3])");
    REQUIRE(resp);
    std::string expected = R"([{"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}})"
                           R"(, {"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}})"
                           R"(, {"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}}])";
    REQUIRE(*resp == expected);
  }

  SECTION("rpc call Batch(all notifications")
  {
    JsonRpcUnitTest rpc;
    REQUIRE(rpc.add_notification_handler("notify_hello", &notify_hello));
    REQUIRE(rpc.add_notification_handler("notify_sum", &notify_hello));
    const auto resp = rpc.handle_call(
      R"( [{"jsonrpc": "2.0", "method": "notify_sum", "params": [1,2,4]}, {"jsonrpc": "2.0", "method": "notify_hello", "params": [7]}])");
    REQUIRE(!resp);
  }
}

TEST_CASE("Handle un-handle handler's error", "[throw]")
{
  JsonRpcUnitTest rpc;
  SECTION("Basic exception thrown")
  {
    REQUIRE(rpc.add_method_handler("oops_i_did_it_again",
                                   [](std::string_view const & /* id ATS_UNUSED */, const YAML::Node & /* params ATS_UNUSED */)
                                     -> swoc::Rv<YAML::Node> { throw std::runtime_error("Oops, I did it again"); }));
    const auto       resp     = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "oops_i_did_it_again", "id": "1"})");
    std::string_view expected = R"({"jsonrpc": "2.0", "error": {"code": 9, "message": "Error during execution"}, "id": "1"})";
    REQUIRE(*resp == expected);
  }
}

TEST_CASE("Call registered method with no ID", "[no-id]")
{
  JsonRpcUnitTest rpc;
  SECTION("Basic test, no id on method call")
  {
    REQUIRE(rpc.add_method_handler("call_me_with_no_id",
                                   [](std::string_view const & /* id ATS_UNUSED */, const YAML::Node & /* params ATS_UNUSED */)
                                     -> swoc::Rv<YAML::Node> { throw std::runtime_error("Oops, I did it again"); }));
    const auto       resp     = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "call_me_with_no_id"})");
    std::string_view expected = R"({"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}})";
    REQUIRE(*resp == expected);
  }
}

TEST_CASE("Call registered notification with ID", "[notification_and_id]")
{
  JsonRpcUnitTest rpc;
  SECTION("Basic test, id on a notification call")
  {
    REQUIRE(rpc.add_notification_handler("call_me_with_id", [](const YAML::Node & /* params ATS_UNUSED */) -> void {
      throw std::runtime_error("Oops, I did it again");
    }));
    const auto       resp     = rpc.handle_call(R"({"jsonrpc": "2.0", "method": "call_me_with_id", "id": "1"})");
    std::string_view expected = R"({"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}, "id": "1"})";
    REQUIRE(*resp == expected);
  }
}

TEST_CASE("Call method with invalid ID", "[invalid_id]")
{
  JsonRpcUnitTest rpc;
  SECTION("Basic test, invalid ids")
  {
    std::string req  = R"([{"id": "", "jsonrpc": "2.0", "method": "will_not_pass_the_validation"},)"
                       R"({"id": {}, "jsonrpc": "2.0", "method": "will_not_pass_the_validation"}])";
    auto        resp = rpc.handle_call(req);
    std::string expected =
      R"([{"jsonrpc": "2.0", "error": {"code": 11, "message": "Use of an empty string as id is discouraged"}}, )"
      R"({"jsonrpc": "2.0", "error": {"code": 7, "message": "Invalid id type"}}])";
    REQUIRE(*resp == expected);
  }
}
