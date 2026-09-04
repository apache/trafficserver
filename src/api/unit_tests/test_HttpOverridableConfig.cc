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
#include <string>
#include <string_view>

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

  const HttpForwarded::OptionBitSet &
  forwarded_options() const
  {
    return _sm.t_state.txn_conf->insert_forwarded;
  }

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

    REQUIRE(TSHttpTxnConfigFind(descriptor.name.data(), static_cast<int>(descriptor.name.size()), &key, &type) == TS_SUCCESS);
    CHECK(key == descriptor.key);
    CHECK(type == descriptor.type);
  }

  TSOverridableConfigKey key;
  TSRecordDataType       type;

  CHECK(TSHttpTxnConfigFind("proxy.config.invalid", -1, &key, &type) == TS_ERROR);
}

TEST_CASE("Round trip HTTP overridable configurations", "[api][overridable-config]")
{
  TestHttpTxn txn;

  for (auto const &descriptor : CONFIG_DESCRIPTORS) {
    INFO(descriptor.name);

    switch (descriptor.type) {
    case TS_RECORDDATATYPE_INT: {
      TSMgmtInt expected;
      TSMgmtInt actual;

      REQUIRE(TSHttpTxnConfigIntGet(txn, descriptor.key, &expected) == TS_SUCCESS);
      REQUIRE(TSHttpTxnConfigIntSet(txn, descriptor.key, expected) == TS_SUCCESS);
      REQUIRE(TSHttpTxnConfigIntGet(txn, descriptor.key, &actual) == TS_SUCCESS);
      CHECK(actual == expected);
      break;
    }
    case TS_RECORDDATATYPE_FLOAT: {
      TSMgmtFloat expected;
      TSMgmtFloat actual;

      REQUIRE(TSHttpTxnConfigFloatGet(txn, descriptor.key, &expected) == TS_SUCCESS);
      REQUIRE(TSHttpTxnConfigFloatSet(txn, descriptor.key, expected) == TS_SUCCESS);
      REQUIRE(TSHttpTxnConfigFloatGet(txn, descriptor.key, &actual) == TS_SUCCESS);
      CHECK(actual == expected);
      break;
    }
    case TS_RECORDDATATYPE_STRING: {
      if (descriptor.key == TS_CONFIG_SSL_CERT_FILEPATH) {
        REQUIRE(TSHttpTxnConfigStringSet(txn, descriptor.key, "", 0) == TS_SUCCESS);
        break;
      }

      if (descriptor.key == TS_CONFIG_HTTP_INSERT_FORWARDED) {
        static constexpr std::string_view expected{"none"};

        REQUIRE(TSHttpTxnConfigStringSet(txn, descriptor.key, expected.data(), static_cast<int>(expected.size())) == TS_SUCCESS);
        CHECK(txn.forwarded_options().none());
        break;
      }

      const char *value;
      int         length;

      REQUIRE(TSHttpTxnConfigStringGet(txn, descriptor.key, &value, &length) == TS_SUCCESS);
      REQUIRE(length >= 0);
      REQUIRE((value != nullptr || length == 0));

      std::string expected;

      if (value != nullptr) {
        expected.assign(value, length);
      }

      REQUIRE(TSHttpTxnConfigStringSet(txn, descriptor.key, expected.data(), static_cast<int>(expected.size())) == TS_SUCCESS);

      const char *actual;
      int         actual_length;

      REQUIRE(TSHttpTxnConfigStringGet(txn, descriptor.key, &actual, &actual_length) == TS_SUCCESS);
      REQUIRE(actual_length >= 0);
      REQUIRE((actual != nullptr || actual_length == 0));
      CHECK(std::string_view(actual != nullptr ? actual : "", actual_length) == expected);
      break;
    }
    default:
      FAIL("Unexpected overridable configuration data type");
      break;
    }
  }
}

TEST_CASE("Clamp constrained HTTP overridable configuration", "[api][overridable-config]")
{
  TestHttpTxn txn;
  TSMgmtInt   actual;

  REQUIRE(TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_PER_SERVER_CONNECTION_MATCH, 95) == TS_SUCCESS);
  REQUIRE(TSHttpTxnConfigIntGet(txn, TS_CONFIG_HTTP_PER_SERVER_CONNECTION_MATCH, &actual) == TS_SUCCESS);
  CHECK(actual == TS_SERVER_OUTBOUND_MATCH_BOTH);
}

TEST_CASE("Round trip bounded SSL string overrides", "[api][overridable-config]")
{
  static constexpr std::array SSL_STRING_CONFIGS{
    TS_CONFIG_SSL_CLIENT_VERIFY_SERVER_POLICY, TS_CONFIG_SSL_CLIENT_VERIFY_SERVER_PROPERTIES, TS_CONFIG_SSL_CLIENT_SNI_POLICY,
    TS_CONFIG_SSL_CLIENT_CERT_FILENAME,        TS_CONFIG_SSL_CLIENT_PRIVATE_KEY_FILENAME,     TS_CONFIG_SSL_CLIENT_CA_CERT_FILENAME,
    TS_CONFIG_SSL_CLIENT_CA_CERT_PATH,         TS_CONFIG_SSL_CLIENT_ALPN_PROTOCOLS,
  };
  static constexpr std::array<char, 3> EXPECTED{'a', '\0', 'b'};

  TestHttpTxn txn;

  for (auto const key : SSL_STRING_CONFIGS) {
    const char *actual;
    int         actual_length;

    REQUIRE(TSHttpTxnConfigStringSet(txn, key, EXPECTED.data(), EXPECTED.size()) == TS_SUCCESS);
    REQUIRE(TSHttpTxnConfigStringGet(txn, key, &actual, &actual_length) == TS_SUCCESS);
    REQUIRE(actual != nullptr);
    CHECK(actual_length == EXPECTED.size());
    CHECK(std::string_view(actual, actual_length) == std::string_view(EXPECTED.data(), EXPECTED.size()));

    REQUIRE(TSHttpTxnConfigStringSet(txn, key, nullptr, 0) == TS_SUCCESS);
    REQUIRE(TSHttpTxnConfigStringGet(txn, key, &actual, &actual_length) == TS_SUCCESS);
    CHECK(actual == nullptr);
    CHECK(actual_length == 0);
  }
}
