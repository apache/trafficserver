/** @file

  Unit tests for ServerSessionPool::acquireSession.

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

#include "proxy/http/HttpSessionManager.h"
#include "proxy/http/HttpConfig.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <vector>

namespace
{

/** A minimal PoolableSession that can be pooled and matched.
 *
 * Only the remote address and the hostname hash participate in the match
 * paths exercised here, so no NetVConnection is required. The session must
 * not be multiplexing, otherwise acquireSession consults the HttpSM.
 */
class TestPoolableSession : public PoolableSession
{
public:
  TestPoolableSession(char const *addr_str, char const *hostname)
  {
    ink_release_assert(ats_ip_pton(addr_str, &_remote_addr) == 0);
    this->attach_hostname(hostname);
  }

  void
  new_connection(NetVConnection *, MIOBuffer *, IOBufferReader *) override
  {
  }
  void
  start() override
  {
  }
  void
  release(ProxyTransaction *) override
  {
  }
  void
  destroy() override
  {
  }
  void
  free() override
  {
  }
  void
  increment_current_active_connections_stat() override
  {
  }
  void
  decrement_current_active_connections_stat() override
  {
  }

  int
  get_transact_count() const override
  {
    return 0;
  }

  const char *
  get_protocol_string() const override
  {
    return "test";
  }

  IOBufferReader *
  get_remote_reader() override
  {
    return nullptr;
  }

  void
  do_io_close(int /* lerrno ATS_UNUSED */ = -1) override
  {
    ++close_count;
  }

  sockaddr const *
  get_remote_addr() const override
  {
    return &_remote_addr.sa;
  }

  int close_count = 0;

private:
  IpEndpoint _remote_addr;
};

/// Owns the test sessions and hands raw pointers to the pool under test.
class SessionFactory
{
public:
  TestPoolableSession *
  make(char const *addr_str, char const *hostname)
  {
    return _sessions.emplace_back(std::make_unique<TestPoolableSession>(addr_str, hostname)).get();
  }

private:
  std::vector<std::unique_ptr<TestPoolableSession>> _sessions;
};

/// The pool bookkeeping updates this gauge, which is normally initialized via HttpConfig.
/// Create it here (when needed) and reset it between Catch2 runs so tests don't leak state.
void
init_metrics()
{
  if (http_rsb.pooled_server_connections == nullptr) {
    http_rsb.pooled_server_connections = Metrics::Gauge::createPtr("proxy.process.http.pooled_server_connections");
  }
  Metrics::Gauge::store(http_rsb.pooled_server_connections, 0);
}

CryptoHash
hash_of(char const *hostname)
{
  CryptoHash hash;

  CryptoContext().hash_immediate(hash, static_cast<unsigned char const *>(static_cast<void const *>(hostname)),
                                 std::strlen(hostname));
  return hash;
}

/// A sockaddr that can be passed inline to acquireSession.
struct Addr {
  Addr(char const *addr_str) { ink_release_assert(ats_ip_pton(addr_str, &_addr) == 0); }

  operator sockaddr const *() const { return &_addr.sa; }

private:
  IpEndpoint _addr;
};

constexpr auto MATCH_IP       = TS_SERVER_SESSION_SHARING_MATCH_MASK_IP;
constexpr auto MATCH_HOSTONLY = TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY;
constexpr auto MATCH_BOTH     = static_cast<TSServerSessionSharingMatchMask>(TS_SERVER_SESSION_SHARING_MATCH_MASK_IP |
                                                                             TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY);

} // namespace

TEST_CASE("ServerSessionPool::acquireSession", "[session_pool]")
{
  init_metrics();

  // Declared before the pool so the sessions outlive it.
  SessionFactory    factory;
  ServerSessionPool pool;

  PoolableSession *acquired = nullptr;

  SECTION("empty pool finds nothing")
  {
    CHECK(pool.acquireSession(Addr{"10.0.0.1:80"}, hash_of("one.example.com"), MATCH_IP, nullptr, acquired) ==
          HSMresult_t::NOT_FOUND);
    CHECK(acquired == nullptr);
  }

  SECTION("a match mask with neither IP nor host disables sharing")
  {
    pool.addSession(factory.make("10.0.0.1:80", "one.example.com"));

    CHECK(pool.acquireSession(Addr{"10.0.0.1:80"}, hash_of("one.example.com"), TS_SERVER_SESSION_SHARING_MATCH_MASK_NONE, nullptr,
                              acquired) == HSMresult_t::NOT_FOUND);
    CHECK(acquired == nullptr);
    CHECK(pool.count() == 1);
  }

  SECTION("match on IP")
  {
    auto *session = factory.make("10.0.0.1:80", "one.example.com");

    pool.addSession(session);

    SECTION("address and port match")
    {
      CHECK(pool.acquireSession(Addr{"10.0.0.1:80"}, hash_of("other.example.com"), MATCH_IP, nullptr, acquired) ==
            HSMresult_t::DONE);
      CHECK(acquired == session);

      // A non-multiplexing session is handed off, not shared: it leaves both pools.
      CHECK(pool.count() == 0);
      CHECK(pool.acquireSession(Addr{"10.0.0.1:80"}, hash_of("one.example.com"), MATCH_HOSTONLY, nullptr, acquired) ==
            HSMresult_t::NOT_FOUND);
    }

    SECTION("a different address does not match")
    {
      CHECK(pool.acquireSession(Addr{"10.0.0.2:80"}, hash_of("one.example.com"), MATCH_IP, nullptr, acquired) ==
            HSMresult_t::NOT_FOUND);
      CHECK(acquired == nullptr);
      CHECK(pool.count() == 1);
    }

    SECTION("a different port does not match")
    {
      CHECK(pool.acquireSession(Addr{"10.0.0.1:81"}, hash_of("one.example.com"), MATCH_IP, nullptr, acquired) ==
            HSMresult_t::NOT_FOUND);
      CHECK(acquired == nullptr);
      CHECK(pool.count() == 1);
    }
  }

  SECTION("match on IP returns the most recently pooled session")
  {
    auto *first  = factory.make("10.0.0.1:80", "one.example.com");
    auto *second = factory.make("10.0.0.1:80", "two.example.com");

    pool.addSession(first);
    pool.addSession(second);

    CHECK(pool.acquireSession(Addr{"10.0.0.1:80"}, hash_of("one.example.com"), MATCH_IP, nullptr, acquired) == HSMresult_t::DONE);
    CHECK(acquired == second);

    CHECK(pool.acquireSession(Addr{"10.0.0.1:80"}, hash_of("one.example.com"), MATCH_IP, nullptr, acquired) == HSMresult_t::DONE);
    CHECK(acquired == first);
  }

  SECTION("match on host only ignores the address but not the port")
  {
    auto *session = factory.make("10.0.0.1:80", "one.example.com");

    pool.addSession(session);

    SECTION("a different address with the same host and port matches")
    {
      CHECK(pool.acquireSession(Addr{"192.168.1.1:80"}, hash_of("one.example.com"), MATCH_HOSTONLY, nullptr, acquired) ==
            HSMresult_t::DONE);
      CHECK(acquired == session);
      CHECK(pool.count() == 0);
    }

    SECTION("a different host does not match")
    {
      CHECK(pool.acquireSession(Addr{"10.0.0.1:80"}, hash_of("other.example.com"), MATCH_HOSTONLY, nullptr, acquired) ==
            HSMresult_t::NOT_FOUND);
      CHECK(acquired == nullptr);
      CHECK(pool.count() == 1);
    }

    SECTION("a different port does not match")
    {
      CHECK(pool.acquireSession(Addr{"10.0.0.1:81"}, hash_of("one.example.com"), MATCH_HOSTONLY, nullptr, acquired) ==
            HSMresult_t::NOT_FOUND);
      CHECK(acquired == nullptr);
      CHECK(pool.count() == 1);
    }
  }

  SECTION("match on host only returns the most recently pooled session")
  {
    auto *first  = factory.make("10.0.0.1:80", "one.example.com");
    auto *second = factory.make("10.0.0.2:80", "one.example.com");

    pool.addSession(first);
    pool.addSession(second);

    CHECK(pool.acquireSession(Addr{"10.0.0.3:80"}, hash_of("one.example.com"), MATCH_HOSTONLY, nullptr, acquired) ==
          HSMresult_t::DONE);
    CHECK(acquired == second);

    CHECK(pool.acquireSession(Addr{"10.0.0.3:80"}, hash_of("one.example.com"), MATCH_HOSTONLY, nullptr, acquired) ==
          HSMresult_t::DONE);
    CHECK(acquired == first);
  }

  SECTION("match on both IP and host requires both to match")
  {
    auto *session = factory.make("10.0.0.1:80", "one.example.com");

    pool.addSession(session);

    SECTION("both match")
    {
      CHECK(pool.acquireSession(Addr{"10.0.0.1:80"}, hash_of("one.example.com"), MATCH_BOTH, nullptr, acquired) ==
            HSMresult_t::DONE);
      CHECK(acquired == session);
    }

    SECTION("the address matches but the host does not")
    {
      CHECK(pool.acquireSession(Addr{"10.0.0.1:80"}, hash_of("other.example.com"), MATCH_BOTH, nullptr, acquired) ==
            HSMresult_t::NOT_FOUND);
      CHECK(acquired == nullptr);
      CHECK(pool.count() == 1);
    }

    SECTION("the host matches but the address does not")
    {
      CHECK(pool.acquireSession(Addr{"192.168.1.1:80"}, hash_of("one.example.com"), MATCH_BOTH, nullptr, acquired) ==
            HSMresult_t::NOT_FOUND);
      CHECK(acquired == nullptr);
      CHECK(pool.count() == 1);
    }
  }

  SECTION("only sessions in the requested address bucket are considered")
  {
    auto *wrong_addr = factory.make("10.0.0.2:80", "one.example.com");
    auto *right_addr = factory.make("10.0.0.1:80", "one.example.com");

    pool.addSession(wrong_addr);
    pool.addSession(right_addr);

    CHECK(pool.acquireSession(Addr{"10.0.0.1:80"}, hash_of("one.example.com"), MATCH_BOTH, nullptr, acquired) == HSMresult_t::DONE);
    CHECK(acquired == right_addr);
    CHECK(pool.count() == 1);
  }

  pool.purge();
}
