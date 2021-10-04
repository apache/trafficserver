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
#include <fstream>

#include <tscore/BufferWriter.h>
#include <tscore/ts_file.h>
#include "ts/ts.h"

#include "rpc/jsonrpc/JsonRPC.h"
#include "rpc/server/RPCServer.h"
#include "rpc/server/IPCSocketServer.h"

#include "shared/rpc/IPCSocketClient.h"
#include "I_EventSystem.h"
#include "tscore/I_Layout.h"
#include "diags.i"

#define DEFINE_JSONRPC_PROTO_FUNCTION(fn) ts::Rv<YAML::Node> fn(std::string_view const &id, const YAML::Node &params)

namespace rpc
{
bool
test_remove_handler(std::string const &name)
{
  return rpc::JsonRPCManager::instance().remove_handler(name);
}
} // namespace rpc
static const std::string sockPath{"/tmp/jsonrpc20_test.sock"};
static const std::string lockPath{"/tmp/jsonrpc20_test.lock"};
static constexpr int default_backlog{5};
static constexpr int default_maxRetriesOnTransientErrors{64};
static constexpr auto logTag{"rpc.test.client"};

struct RPCServerTestListener : Catch::TestEventListenerBase {
  using TestEventListenerBase::TestEventListenerBase; // inherit constructor
  ~RPCServerTestListener();

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

    rpc::config::RPCConfig serverConfig;

    auto confStr{R"({"rpc": { "enabled": true, "unix": { "lock_path_name": ")" + lockPath + R"(", "sock_path_name": ")" + sockPath +
                 R"(",  "backlog": 5,"max_retry_on_transient_errors": 64 }}})"};
    YAML::Node configNode = YAML::Load(confStr);
    serverConfig.load(configNode["rpc"]);
    try {
      jsonrpcServer = new rpc::RPCServer(serverConfig);

      jsonrpcServer->start_thread();
    } catch (std::exception const &ex) {
      Debug(logTag, "Oops: %s", ex.what());
    }
  }

  // The whole test run ending
  void
  testRunEnded(Catch::TestRunStats const &testRunStats) override
  {
    // jsonrpcServer->stop_thread();
    // delete main_thread;
    if (jsonrpcServer) {
      delete jsonrpcServer;
    }
  }

private:
  // std::unique_ptr<rpc::RPCServer> jrpcServer;
  std::unique_ptr<EThread> main_thread;
};
CATCH_REGISTER_LISTENER(RPCServerTestListener)

RPCServerTestListener::~RPCServerTestListener() {}

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
namespace
{
// Handy class to avoid manually disconecting the socket.
// TODO: should it also connect?
struct ScopedLocalSocket : shared::rpc::IPCSocketClient {
  using super = shared::rpc::IPCSocketClient;
  // TODO, use another path.
  ScopedLocalSocket() : IPCSocketClient(sockPath) {}
  ~ScopedLocalSocket() { IPCSocketClient::disconnect(); }

  template <std::size_t N>
  void
  send_in_chunks(std::string_view data, int disconnect_after_chunk_n = -1)
  {
    int chunk_number{1};
    auto chunks = chunk<N>(data);
    for (auto &&part : chunks) {
      if (::write(_sock, part.c_str(), part.size()) < 0) {
        Debug(logTag, "error sending message :%s", std ::strerror(errno));
        break;
      }

      if (disconnect_after_chunk_n == chunk_number) {
        Debug(logTag, "Disconnecting it after chunk %d", chunk_number);
        super::disconnect();
        return;
      }
      ++chunk_number;
    }
  }

  // basic read, if fail, why it fail is irrelevant in this test.
  std::string
  read()
  {
    ts::LocalBufferWriter<32000> bw;
    auto ret = super::read_all(bw);
    if (ret == ReadStatus::NO_ERROR) {
      return {bw.data(), bw.size()};
    }
    return {};
  }
  // small wrapper function to deal with the bw.
  std::string
  query(std::string_view msg)
  {
    ts::LocalBufferWriter<32000> bw;
    auto ret = connect().send(msg).read_all(bw);
    if (ret == ReadStatus::NO_ERROR) {
      return {bw.data(), bw.size()};
    }

    return {};
  }

private:
  template <typename Iter, std::size_t N>
  std::array<std::string, N>
  chunk_impl(Iter from, Iter to)
  {
    const std::size_t size = std::distance(from, to);
    if (size <= N) {
      return {std::string{from, to}};
    }
    std::size_t index{0};
    std::array<std::string, N> ret;
    const std::size_t each_part = size / N;
    const std::size_t remainder = size % N;

    for (auto it = from; it != to;) {
      if (std::size_t rem = std::distance(it, to); rem == (each_part + remainder)) {
        ret[index++] = std::string{it, it + rem};
        break;
      }
      ret[index++] = std::string{it, it + each_part};
      std::advance(it, each_part);
    }

    return ret;
  }

  template <std::size_t N>
  auto
  chunk(std::string_view v)
  {
    return chunk_impl<std::string_view::const_iterator, N>(v.begin(), v.end());
  }
};

// helper function to send a request and update the promise when the response is done.
// This is to be used in a multithread test.
void
send_request(std::string json, std::promise<std::string> p)
{
  ScopedLocalSocket rpc_client;
  auto resp = rpc_client.query(json);
  p.set_value(resp);
}
} // namespace
TEST_CASE("Sending 'concurrent' requests to the rpc server.", "[thread]")
{
  SECTION("A registered handlers")
  {
    rpc::add_method_handler("some_foo", &some_foo);
    rpc::add_method_handler("some_foo2", &some_foo);

    std::promise<std::string> p1;
    std::promise<std::string> p2;
    auto fut1 = p1.get_future();
    auto fut2 = p2.get_future();

    REQUIRE_NOTHROW([&]() {
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
    }());
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
  REQUIRE(rpc::add_method_handler("do_nothing", &do_nothing));
  SECTION("Basic single request to the rpc server")
  {
    const int S{500};
    auto json{R"({"jsonrpc": "2.0", "method": "do_nothing", "params": {"msg":")" + random_string(S) + R"("}, "id":"EfGh-1"})"};
    REQUIRE_NOTHROW([&]() {
      ScopedLocalSocket rpc_client;
      auto resp = rpc_client.query(json);

      REQUIRE(resp == R"({"jsonrpc": "2.0", "result": {"size": ")" + std::to_string(S) + R"("}, "id": "EfGh-1"})");
    }());
  }
  REQUIRE(rpc::test_remove_handler("do_nothing"));
}

TEST_CASE("Sending a message bigger than the internal server's buffer. 32000", "[buffer][error]")
{
  REQUIRE(rpc::add_method_handler("do_nothing", &do_nothing));

  SECTION("Message larger than the the accepted size.")
  {
    const int S{32000}; // + the rest of the json message.
    auto json{R"({"jsonrpc": "2.0", "method": "do_nothing", "params": {"msg":")" + random_string(S) + R"("}, "id":"EfGh-1"})"};
    REQUIRE_NOTHROW([&]() {
      ScopedLocalSocket rpc_client;
      auto resp = rpc_client.query(json);
      REQUIRE(resp.empty());
    }());
  }

  REQUIRE(rpc::test_remove_handler("do_nothing"));
}

TEST_CASE("Test with invalid json message", "[socket]")
{
  REQUIRE(rpc::add_method_handler("do_nothing", &do_nothing));

  SECTION("A rpc server")
  {
    const int S{10};
    auto json{R"({"jsonrpc": "2.0", "method": "do_nothing", "params": { "msg": ")" + random_string(S) + R"("}, "id": "EfGh})"};
    REQUIRE_NOTHROW([&]() {
      ScopedLocalSocket rpc_client;
      auto resp = rpc_client.query(json);

      CHECK(resp == R"({"jsonrpc": "2.0", "error": {"code": -32700, "message": "Parse error"}})");
    }());
  }
  REQUIRE(rpc::test_remove_handler("do_nothing"));
}

TEST_CASE("Test with chunks", "[socket][chunks]")
{
  REQUIRE(rpc::add_method_handler("do_nothing", &do_nothing));

  SECTION("Sending request by chunks")
  {
    const int S{10};
    auto json{R"({"jsonrpc": "2.0", "method": "do_nothing", "params": { "msg": ")" + random_string(S) +
              R"("}, "id": "chunk-parts-3"})"};

    REQUIRE_NOTHROW([&]() {
      ScopedLocalSocket rpc_client;
      using namespace std::chrono_literals;
      rpc_client.connect();
      rpc_client.send_in_chunks<3>(json);
      auto resp = rpc_client.read();
      REQUIRE(resp == R"({"jsonrpc": "2.0", "result": {"size": ")" + std::to_string(S) + R"("}, "id": "chunk-parts-3"})");
    }());
  }
  REQUIRE(rpc::test_remove_handler("do_nothing"));
}

TEST_CASE("Test with chunks - disconnect after second part", "[socket][chunks]")
{
  REQUIRE(rpc::add_method_handler("do_nothing", &do_nothing));

  SECTION("Sending request by chunks")
  {
    const int S{4000};
    auto json{R"({"jsonrpc": "2.0", "method": "do_nothing", "params": { "msg": ")" + random_string(S) +
              R"("}, "id": "chunk-parts-3-2"})"};

    REQUIRE_NOTHROW([&]() {
      ScopedLocalSocket rpc_client;
      using namespace std::chrono_literals;
      rpc_client.connect();
      rpc_client.send_in_chunks<3>(json, 2);
      // read will fail.
      auto resp = rpc_client.read();
      REQUIRE(resp == "");
    }());
  }
  REQUIRE(rpc::test_remove_handler("do_nothing"));
}

TEST_CASE("Test with chunks - incomplete message", "[socket][chunks]")
{
  REQUIRE(rpc::add_method_handler("do_nothing", &do_nothing));

  SECTION("Sending request by chunks, broken message")
  {
    const int S{50};
    auto json{R"({"jsonrpc": "2.0", "method": "do_nothing", "params": { "msg": ")" + random_string(S) +
              R"("}, "id": "chunk-parts-3)"};
    //                                  ^  missing-> "}

    REQUIRE_NOTHROW([&]() {
      ScopedLocalSocket rpc_client;
      using namespace std::chrono_literals;
      rpc_client.connect();
      rpc_client.send_in_chunks<3>(json);
      auto resp = rpc_client.read();
      REQUIRE(resp == R"({"jsonrpc": "2.0", "error": {"code": -32700, "message": "Parse error"}})");
    }());
  }
  REQUIRE(rpc::test_remove_handler("do_nothing"));
}

// Enable toggle
TEST_CASE("Test rpc enable toggle feature - default enabled.", "[default values]")
{
  rpc::config::RPCConfig serverConfig;
  REQUIRE(serverConfig.is_enabled() == true);
}

TEST_CASE("Test rpc enable toggle feature. Enabled by configuration", "[rpc][enabled]")
{
  rpc::config::RPCConfig serverConfig;

  auto confStr{R"({"rpc": {"enabled": true}})"};
  std::cout << "'" << confStr << "'" << std::endl;
  YAML::Node configNode = YAML::Load(confStr);
  serverConfig.load(configNode["rpc"]);
  REQUIRE(serverConfig.is_enabled() == true);
}

TEST_CASE("Test rpc  enable toggle feature. Disabled by configuration", "[rpc][disabled]")
{
  rpc::config::RPCConfig serverConfig;

  auto confStr{R"({"rpc": {"enabled":false}})"};

  REQUIRE_NOTHROW([&]() {
    YAML::Node configNode = YAML::Load(confStr);
    serverConfig.load(configNode["rpc"]);
  }());
  REQUIRE(serverConfig.is_enabled() == false);
}

// TEST UDS Server configuration
namespace
{
namespace trp = rpc::comm;
// This class is defined to get access to the protected config object inside the IPCSocketServer class.
struct LocalSocketTest : public trp::IPCSocketServer {
  inline static const std::string _name = "LocalSocketTest";
  bool
  configure(YAML::Node const &params) override
  {
    return trp::IPCSocketServer::configure(params);
  }
  void
  run() override
  {
  }
  std::error_code
  init() override
  {
    return trp::IPCSocketServer::init();
  }
  bool
  stop() override
  {
    return true;
  }
  std::string const &
  name() const override
  {
    return _name;
  }
  trp::IPCSocketServer::Config const &
  get_conf() const
  {
    return _conf;
  }
};
} // namespace

TEST_CASE("Test configuration parsing. UDS values", "[string]")
{
  rpc::config::RPCConfig serverConfig;

  auto confStr{R"({"rpc": { "enabled": true, "unix": { "lock_path_name": ")" + lockPath + R"(", "sock_path_name": ")" + sockPath +
               R"(",  "backlog": 5,"max_retry_on_transient_errors": 64 }}})"};
  YAML::Node configNode = YAML::Load(confStr);
  serverConfig.load(configNode["rpc"]);

  REQUIRE(serverConfig.get_comm_type() == rpc::config::RPCConfig::CommType::UNIX);

  auto socket    = std::make_unique<LocalSocketTest>();
  auto const ret = socket->configure(serverConfig.get_comm_config_params());
  REQUIRE(ret);
  REQUIRE(socket->get_conf().backlog == default_backlog);
  REQUIRE(socket->get_conf().maxRetriesOnTransientErrors == default_maxRetriesOnTransientErrors);
  REQUIRE(socket->get_conf().sockPathName == sockPath);
  REQUIRE(socket->get_conf().lockPathName == lockPath);
}

TEST_CASE("Test configuration parsing from a file. UDS Server", "[file]")
{
  namespace fs = ts::file;

  fs::path sandboxDir = fs::temp_directory_path();
  fs::path configPath = sandboxDir / "jsonrpc.yaml";

  // define here to later compare.
  std::string sockPathName{configPath.string() + "jsonrpc20_test2.sock"};
  std::string lockPathName{configPath.string() + "jsonrpc20_test2.lock"};

  auto confStr{R"({"rpc": { "enabled": true, "unix": { "lock_path_name": ")" + lockPathName + R"(", "sock_path_name": ")" +
               sockPathName + R"(",  "backlog": 5,"max_retry_on_transient_errors": 64 }}})"};
  // write the config.
  std::ofstream ofs(configPath.string(), std::ofstream::out);
  // Yes, we write json into the yaml, remember, YAML is a superset of JSON, yaml parser can handle this.
  ofs << confStr;
  ofs.close();

  rpc::config::RPCConfig serverConfig;
  // on any error reading the file, default values will be used.
  serverConfig.load_from_file(configPath.string());

  REQUIRE(serverConfig.get_comm_type() == rpc::config::RPCConfig::CommType::UNIX);

  auto socket     = std::make_unique<LocalSocketTest>();
  auto const &ret = socket->configure(serverConfig.get_comm_config_params());
  REQUIRE(ret);
  REQUIRE(socket->get_conf().backlog == 5);
  REQUIRE(socket->get_conf().maxRetriesOnTransientErrors == 64);
  REQUIRE(socket->get_conf().sockPathName == sockPathName);
  REQUIRE(socket->get_conf().lockPathName == lockPathName);

  std::error_code ec;
  REQUIRE(fs::remove(sandboxDir, ec));
}
