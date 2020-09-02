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
#define CATCH_CONFIG_EXTERNAL_INTERFACES

#include <catch.hpp> /* catch unit-test framework */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>

#include <thread>
#include <future>
#include <chrono>

#include <tscore/BufferWriter.h>

#include "rpc/jsonrpc/JsonRpc.h"
#include "rpc/server/RpcServer.h"
#include "rpc/common/JsonRpcApi.h"

#include "LocalSocketClient.h"

#include "I_EventSystem.h"
#include "tscore/I_Layout.h"
#include "diags.i"

static const std::string sockPath{"/tmp/jsonrpc20_test.sock"};

struct RpcServerTestListener : Catch::TestEventListenerBase {
  using TestEventListenerBase::TestEventListenerBase; // inherit constructor
  ~RpcServerTestListener();

  // The whole test run starting
  void
  testRunStarting(Catch::TestRunInfo const &testRunInfo) override
  {
    Layout::create();
    init_diags("rpc|rpc.test", nullptr);
    RecProcessInit(RECM_STAND_ALONE);

    signal(SIGPIPE, SIG_IGN);

    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    eventProcessor.start(2, 1048576);

    // EThread *main_thread = new EThread;
    main_thread = std::make_unique<EThread>();
    main_thread->set_specific();

    auto serverConfig = rpc::config::RPCConfig{};

    auto confStr{R"({transport_type": 1, "transport_config": {"lock_path_name": "/tmp/jsonrpc20_test.lock", "sock_path_name": ")" +
                 sockPath + R"(", "backlog": 5, "max_retry_on_transient_errors": 64 }})"};

    YAML::Node configNode = YAML::Load(confStr);
    serverConfig.load(configNode);
    jrpcServer = std::make_unique<rpc::RpcServer>(serverConfig);

    jrpcServer->thread_start();
  }

  // The whole test run ending
  void
  testRunEnded(Catch::TestRunStats const &testRunStats) override
  {
    jrpcServer->stop();
    // delete main_thread;
  }

private:
  std::unique_ptr<rpc::RpcServer> jrpcServer;
  std::unique_ptr<EThread> main_thread;
};
CATCH_REGISTER_LISTENER(RpcServerTestListener)

RpcServerTestListener::~RpcServerTestListener() {}

DEFINE_JSONRPC_PROTO_FUNCTION(some_foo) // id, params
{
  ts::Rv<YAML::Node> resp;
  int dur{1};
  try {
    dur = params["duration"].as<int>();
  } catch (...) {
  }
  INFO("Sleeping for " << dur << "s");
  std::this_thread::sleep_for(std::chrono::seconds(dur));
  resp.result()["res"]      = "ok";
  resp.result()["duration"] = dur;

  INFO("Done sleeping");
  return resp;
}

// Handy class to avoid disconecting the socket.
// TODO: should it also connect?
struct ScopedLocalSocket : LocalSocketClient {
  // TODO, use another path.
  ScopedLocalSocket() : LocalSocketClient(sockPath) {}
  ~ScopedLocalSocket() { LocalSocketClient::disconnect(); }
};

void
send_request(std::string json, std::promise<std::string> p)
{
  ScopedLocalSocket rpc_client;
  auto resp = rpc_client.connect().send(json).read();
  p.set_value(resp);
}

TEST_CASE("Sending 'concurrent' requests to the rpc server.", "[thread]")
{
  SECTION("A registered handlers")
  {
    rpc::JsonRpc::instance().add_handler("some_foo", &some_foo);
    rpc::JsonRpc::instance().add_handler("some_foo2", &some_foo);

    std::promise<std::string> p1;
    std::promise<std::string> p2;
    auto fut1 = p1.get_future();
    auto fut2 = p2.get_future();

    // Two different clients, on the same server, as the server is an Unix Domain Socket, it should handle all this
    // properly, in any case we just run the basic smoke test for our server.
    auto t1 = std::thread(&send_request, R"({"jsonrpc": "2.0", "method": "some_foo", "params": {"duration": 1}, "id": "aBcD"})",
                          std::move(p1));
    auto t2 = std::thread(&send_request, R"({"jsonrpc": "2.0", "method": "some_foo", "params": {"duration": 1}, "id": "eFgH"})",
                          std::move(p2));
    // wait to get the promise set.
    fut1.wait();
    fut2.wait();

    // the expected
    std::string_view expected1{R"({"jsonrpc": "2.0", "result": {"res": "ok", "duration": "1"}, "id": "aBcD"})"};
    std::string_view expected2{R"({"jsonrpc": "2.0", "result": {"res": "ok", "duration": "1"}, "id": "eFgH"})"};

    CHECK(fut1.get() == expected1);
    CHECK(fut2.get() == expected2);

    t1.join();
    t2.join();
  }
}

std::string
random_string(std::string::size_type length)
{
  auto randchar = []() -> char {
    const char charset[] = "0123456789"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
  };
  std::string str(length, 0);
  std::generate_n(str.begin(), length, randchar);
  return str;
}

DEFINE_JSONRPC_PROTO_FUNCTION(do_nothing) // id, params, resp
{
  ts::Rv<YAML::Node> resp;
  resp.result()["size"] = params["msg"].as<std::string>().size();
  return resp;
}

TEST_CASE("Basic message sending to a running server", "[socket]")
{
  REQUIRE(rpc::JsonRpc::instance().add_handler("do_nothing", &do_nothing));
  SECTION("Basic single request to the rpc server")
  {
    const int S{500};
    auto json{R"({"jsonrpc": "2.0", "method": "do_nothing", "params": {"msg":")" + random_string(S) + R"("}, "id":"EfGh-1"})"};
    ScopedLocalSocket rpc_client;
    auto resp = rpc_client.connect().send(json).read();

    REQUIRE(resp == R"({"jsonrpc": "2.0", "result": {"size": ")" + std::to_string(S) + R"("}, "id": "EfGh-1"})");
  }
  REQUIRE(rpc::JsonRpc::instance().remove_handler("do_nothing"));
}

TEST_CASE("Sending a message bigger than the internal server's buffer.", "[buffer][error]")
{
  REQUIRE(rpc::JsonRpc::instance().add_handler("do_nothing", &do_nothing));
  std::cout << "Paso aca\n";
  SECTION("The Server message buffer size same as socket buffer")
  {
    const int S{64000};
    auto json{R"({"jsonrpc": "2.0", "method": "do_nothing", "params": {"msg":")" + random_string(S) + R"("}, "id":"EfGh-1"})"};
    ScopedLocalSocket rpc_client;
    auto resp = rpc_client.connect().send(json).read();
    REQUIRE(resp.empty());
    REQUIRE(rpc_client.is_connected() == false);
  }
  std::cout << "Paso aca\n";
  REQUIRE(rpc::JsonRpc::instance().remove_handler("do_nothing"));
}

TEST_CASE("Test with invalid json message", "[socket]")
{
  REQUIRE(rpc::JsonRpc::instance().add_handler("do_nothing", &do_nothing));

  SECTION("A rpc server")
  {
    const int S{10};
    auto json{R"({"jsonrpc": "2.0", "method": "do_nothing", "params": { "msg": ")" + random_string(S) + R"("}, "id": "EfGh})"};
    ScopedLocalSocket rpc_client;
    auto resp = rpc_client.connect().send(json).read();

    CHECK(resp == R"({"jsonrpc": "2.0", "error": {"code": -32700, "message": "Parse error"}})");
  }
  REQUIRE(rpc::JsonRpc::instance().remove_handler("do_nothing"));
}

TEST_CASE("Test with chunks", "[socket][chunks]")
{
  REQUIRE(rpc::JsonRpc::instance().add_handler("do_nothing", &do_nothing));

  SECTION("Sending request by chunks")
  {
    const int S{10};
    auto json{R"({"jsonrpc": "2.0", "method": "do_nothing", "params": { "msg": ")" + random_string(S) +
              R"("}, "id": "chunk-parts-3"})"};

    ScopedLocalSocket rpc_client;
    using namespace std::chrono_literals;
    auto resp = rpc_client.connect().send_in_chunks_with_wait<3>(json, 10ms).read();
    REQUIRE(resp == R"({"jsonrpc": "2.0", "result": {"size": ")" + std::to_string(S) + R"("}, "id": "chunk-parts-3"})");
  }
  REQUIRE(rpc::JsonRpc::instance().remove_handler("do_nothing"));
}
