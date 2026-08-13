/** @file

  Catch based unit tests for HTTP overridable configuration APIs.

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

#include "iocore/eventsystem/ConfigProcessor.h"
#include "iocore/net/ConnectionTracker.h"
#include "proxy/http/HttpConfig.h"
#include "proxy/http/HttpSM.h"
#include "proxy/http/OverridableConfigDefs.h"
#include "ts/ts.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using namespace std::literals;

namespace
{
struct ConfigDescriptor {
  std::string_view       name;
  TSOverridableConfigKey key;
  TSRecordDataType       type;
};

// clang-format off
static constexpr std::array<ConfigDescriptor, TS_CONFIG_LAST_ENTRY> CONFIG_DESCRIPTORS{{
#define X_CONFIG_DESCRIPTOR(CONFIG_KEY, MEMBER, RECORD_NAME, DATA_TYPE, CONV) \
  {RECORD_NAME, TS_CONFIG_##CONFIG_KEY, TS_RECORDDATATYPE_##DATA_TYPE},
  OVERRIDABLE_CONFIGS(X_CONFIG_DESCRIPTOR)
#undef X_CONFIG_DESCRIPTOR
}};
// clang-format on

class TestHttpTxn
{
public:
  TestHttpTxn()
  {
    if (HttpConfig::m_id == 0) {
      HttpConfig::m_id = configProcessor.set(HttpConfig::m_id, new HttpConfigParams);
    }

    _sm.magic                     = HttpSmMagic_t::ALIVE;
    _sm.t_state.http_config_param = HttpConfig::acquire();
    REQUIRE(_sm.t_state.http_config_param != nullptr);
    _sm.t_state.txn_conf = &_sm.t_state.http_config_param->oride;
  }

  operator TSHttpTxn() { return reinterpret_cast<TSHttpTxn>(&_sm); }

private:
  HttpSM _sm;
};
} // namespace

TEST_CASE("Find HTTP overridable configurations", "[api][overridable-config]")
{
  for (auto const &descriptor : CONFIG_DESCRIPTORS) {
    TSOverridableConfigKey key;
    TSRecordDataType       type;

    INFO(descriptor.name);

    REQUIRE(TSHttpTxnConfigFind(descriptor.name.data(), -1, &key, &type) == TS_SUCCESS);
    CHECK(key == descriptor.key);
    CHECK(type == descriptor.type);

    REQUIRE(TSHttpTxnConfigFind(descriptor.name.data(), descriptor.name.size(), &key, &type) == TS_SUCCESS);
    CHECK(key == descriptor.key);
    CHECK(type == descriptor.type);
  }

  TSOverridableConfigKey key;
  TSRecordDataType       type;

  CHECK(TSHttpTxnConfigFind("proxy.config.invalid", -1, &key, &type) == TS_ERROR);
}

TEST_CASE("Set and get HTTP overridable configurations", "[api][overridable-config]")
{
  TestHttpTxn txn;

  SECTION("integer")
  {
    static constexpr TSMgmtInt expected = 0;
    TSMgmtInt                  actual;

    REQUIRE(TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_CACHE_HTTP, expected) == TS_SUCCESS);
    REQUIRE(TSHttpTxnConfigIntGet(txn, TS_CONFIG_HTTP_CACHE_HTTP, &actual) == TS_SUCCESS);
    CHECK(actual == expected);
  }

  SECTION("float")
  {
    static constexpr TSMgmtFloat expected = 0.25;
    TSMgmtFloat                  actual;

    REQUIRE(TSHttpTxnConfigFloatSet(txn, TS_CONFIG_HTTP_CACHE_HEURISTIC_LM_FACTOR, expected) == TS_SUCCESS);
    REQUIRE(TSHttpTxnConfigFloatGet(txn, TS_CONFIG_HTTP_CACHE_HEURISTIC_LM_FACTOR, &actual) == TS_SUCCESS);
    CHECK(actual == expected);
  }

  SECTION("constrained integer")
  {
    TSMgmtInt actual;

    REQUIRE(TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_PER_SERVER_CONNECTION_MATCH, 95) == TS_SUCCESS);
    REQUIRE(TSHttpTxnConfigIntGet(txn, TS_CONFIG_HTTP_PER_SERVER_CONNECTION_MATCH, &actual) == TS_SUCCESS);
    CHECK(actual == TS_SERVER_OUTBOUND_MATCH_BOTH);
  }

  SECTION("string")
  {
    static constexpr auto expected = "Catch test"sv;
    const char           *actual;
    int                   length;

    REQUIRE(TSHttpTxnConfigStringSet(txn, TS_CONFIG_HTTP_RESPONSE_SERVER_STR, expected.data(), expected.size()) == TS_SUCCESS);
    REQUIRE(TSHttpTxnConfigStringGet(txn, TS_CONFIG_HTTP_RESPONSE_SERVER_STR, &actual, &length) == TS_SUCCESS);
    REQUIRE(actual != nullptr);
    CHECK(std::string_view(actual, length) == expected);
  }
}
