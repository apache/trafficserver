/** @file

  Unit tests for the TSRemapNextHopStrategy* APIs used during remap rule loading.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more
  contributor license agreements.  See the NOTICE file distributed with
  this work for additional information regarding copyright ownership.
  The ASF licenses this file to you under the Apache License, Version
  2.0 (the "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

 */

#include "proxy/hdrs/HdrHeap.h"
#include "proxy/http/HttpSM.h"
#include "proxy/http/remap/NextHopSelectionStrategy.h"
#include "proxy/http/remap/NextHopStrategyFactory.h"
#include "proxy/http/remap/UrlMapping.h"
#include "proxy/http/remap/UrlRewrite.h"
#include "records/RecordsConfig.h"
#include "ts/ts.h"
#include "tscore/BaseLogFile.h"
#include "iocore/utils/Machine.h"

#include <catch2/catch_test_macros.hpp> /* catch unit-test framework */
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <catch2/interfaces/catch_interfaces_config.hpp>

struct TestListener : Catch::EventListenerBase {
  using EventListenerBase::EventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const & /* testRunInfo ATS_UNUSED */) override
  {
    Thread *main_thread = new EThread();
    main_thread->set_specific();

    DiagsPtr::set(new Diags("test_StrategyApi", "*", "", new BaseLogFile("stderr")));
    diags()->show_location = SHOW_LOCATION_DEBUG;

    url_init();
    mime_init();
    http_init();
    Layout::create();
    RecProcessInit(diags());
    LibRecordsConfigInit();
    Machine::init("localhost", nullptr);
  }
};

CATCH_REGISTER_LISTENER(TestListener);

SCENARIO("TSRemapNextHopStrategy APIs during rule loading", "[strategy][api]")
{
  // strategy.yaml '#include hosts.yaml' resolves relative to the cwd.
  REQUIRE(chdir(TS_SRC_DIR "/..") == 0);

  NextHopStrategyFactory factory(TS_SRC_DIR "/strategy.yaml");
  REQUIRE(factory.strategies_loaded);

  GIVEN("A remap rule being loaded with a strategy factory")
  {
    url_mapping um;
    um.strategyFactory = &factory;

    WHEN("called inside the UrlMappingInstanceScope")
    {
      UrlMappingInstanceScope scope{&um};

      THEN("Find resolves known names and returns nullptr for unknown names")
      {
        CHECK(TSRemapNextHopStrategyFind("strategy-1") != nullptr);
        CHECK(TSRemapNextHopStrategyFind("no-such-strategy") == nullptr);
      }

      THEN("Get reflects the rule's strategy state across Set")
      {
        TSStrategy s1 = TSRemapNextHopStrategyFind("strategy-1");
        REQUIRE(s1 != nullptr);

        CHECK(TSRemapNextHopStrategyGet() == nullptr);
        TSRemapNextHopStrategySet(s1);
        CHECK(TSRemapNextHopStrategyGet() == s1);
        CHECK(um.strategy == reinterpret_cast<NextHopSelectionStrategy *>(s1));
      }

      THEN("Set rejects a handle not owned by the loading factory")
      {
        NextHopStrategyFactory other(TS_SRC_DIR "/strategy.yaml");
        auto                  *foreign = other.strategyInstance("strategy-1");
        REQUIRE(foreign != nullptr);
        REQUIRE(foreign != factory.strategyInstance("strategy-1"));

        TSRemapNextHopStrategySet(reinterpret_cast<TSStrategy>(foreign));
        CHECK(um.strategy == nullptr); // unchanged: rejected
      }

      THEN("Set(nullptr) clears the rule strategy")
      {
        TSRemapNextHopStrategySet(TSRemapNextHopStrategyFind("strategy-1"));
        REQUIRE(um.strategy != nullptr);

        TSRemapNextHopStrategySet(nullptr);
        CHECK(um.strategy == nullptr);
      }

      THEN("NameGet returns the strategy's registered name")
      {
        TSStrategy s2 = TSRemapNextHopStrategyFind("strategy-2");
        REQUIRE(s2 != nullptr);
        CHECK(TSNextHopStrategyNameGet(s2) == std::string("strategy-2"));
      }
    }

    WHEN("called outside any loading rule")
    {
      THEN("Find and Get return nullptr and Set leaves the rule untouched")
      {
        auto *const s1 = factory.strategyInstance("strategy-1");
        REQUIRE(s1 != nullptr);
        um.strategy = s1;

        CHECK(TSRemapNextHopStrategyFind("strategy-1") == nullptr);
        CHECK(TSRemapNextHopStrategyGet() == nullptr);
        TSRemapNextHopStrategySet(nullptr); // logs an error, must not clear
        CHECK(um.strategy == s1);
      }
    }

    WHEN("the loading rule has no strategy factory")
    {
      url_mapping             orphan;
      UrlMappingInstanceScope scope{&orphan};

      THEN("Find returns nullptr and Set rejects handles but allows clearing")
      {
        CHECK(TSRemapNextHopStrategyFind("strategy-1") == nullptr);
        TSRemapNextHopStrategySet(reinterpret_cast<TSStrategy>(factory.strategyInstance("strategy-1")));
        CHECK(orphan.strategy == nullptr); // rejected: rule has no factory

        orphan.strategy = factory.strategyInstance("strategy-1");
        REQUIRE(orphan.strategy != nullptr);
        TSRemapNextHopStrategySet(nullptr);
        CHECK(orphan.strategy == nullptr);
      }
    }
  }

  THEN("NameGet(nullptr) returns nullptr")
  {
    CHECK(TSNextHopStrategyNameGet(nullptr) == nullptr);
  }
}

namespace
{
// Never destroyed: ~HttpSM releases an HttpConfig this test never acquires, which aborts.
HttpSM &
test_sm()
{
  static HttpSM *const sm = new HttpSM;
  return *sm;
}

struct TxnRemapScope {
  HttpSM &sm;

  explicit TxnRemapScope(HttpSM &s, NextHopStrategyFactory *factory) : sm(s)
  {
    sm.magic                     = HttpSmMagic_t::ALIVE;
    sm.t_state.next_hop_strategy = nullptr;
    sm.m_remap                   = std::make_shared<UrlRewrite>();
    sm.m_remap->strategyFactory  = factory; // ~UrlRewrite deletes it
  }

  ~TxnRemapScope()
  {
    sm.t_state.next_hop_strategy = nullptr;
    sm.m_remap.reset();
    sm.magic = HttpSmMagic_t::DEAD;
  }

  TxnRemapScope(TxnRemapScope const &)            = delete;
  TxnRemapScope &operator=(TxnRemapScope const &) = delete;
};
} // namespace

SCENARIO("TSHttpTxnNextHopStrategy Set and Get on a transaction", "[strategy][api]")
{
  REQUIRE(chdir(TS_SRC_DIR "/..") == 0);

  GIVEN("A transaction whose rewrite table owns a loaded strategy factory")
  {
    auto *const   factory = new NextHopStrategyFactory(TS_SRC_DIR "/strategy.yaml");
    TxnRemapScope scope{test_sm(), factory};
    auto const    txnp = reinterpret_cast<TSHttpTxn>(&scope.sm);

    REQUIRE(factory->strategies_loaded);

    TSStrategy const s1 = TSHttpTxnNextHopStrategyFind(txnp, "strategy-1");
    REQUIRE(s1 != nullptr);

    THEN("Set of a live handle is visible to Get")
    {
      TSHttpTxnNextHopStrategySet(txnp, s1);
      CHECK(TSHttpTxnNextHopStrategyGet(txnp) == s1);
    }

    THEN("Set of a foreign handle is rejected and the current strategy is kept")
    {
      NextHopStrategyFactory other(TS_SRC_DIR "/strategy.yaml");
      auto                  *foreign = other.strategyInstance("strategy-1");
      REQUIRE(foreign != nullptr);

      TSHttpTxnNextHopStrategySet(txnp, s1);
      REQUIRE(TSHttpTxnNextHopStrategyGet(txnp) == s1);

      TSHttpTxnNextHopStrategySet(txnp, reinterpret_cast<TSStrategy>(foreign));
      CHECK(TSHttpTxnNextHopStrategyGet(txnp) == s1);
    }

    THEN("Set(nullptr) clears a live strategy")
    {
      TSHttpTxnNextHopStrategySet(txnp, s1);
      REQUIRE(TSHttpTxnNextHopStrategyGet(txnp) == s1);

      TSHttpTxnNextHopStrategySet(txnp, nullptr);
      CHECK(TSHttpTxnNextHopStrategyGet(txnp) == nullptr);
    }
  }
}
