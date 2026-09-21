/*
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

/**
 * @file test_access_control.cc
 * @brief Unit tests for functions implementing access control
 */

#include <catch2/catch_test_macros.hpp> /* catch unit-test framework */
#include "../access_control.h"          /* access_control utility */

/* AccessToken ***************************************************************************************************************** */

StringMap secrets = {
  {"1", "1234567890"}
};

bool enableDebug = true;

TEST_CASE("AssetToken: simple test", "[AssetToken][access_control][utility]")
{
  KvpAccessTokenConfig tokenConfig;

  class UnitKvpAccessToken : public KvpAccessToken
  {
  public:
    using KvpAccessToken::KvpAccessToken;
    using KvpAccessToken::validateSemantics;
  };

  KvpAccessTokenBuilder atb(tokenConfig, secrets);
  atb.addSubject("ABCDEFG");
  atb.addExpiration(1234567);
  atb.addNotBefore(2345678);
  atb.addIssuedAt(3456789);
  atb.addTokenId("tokenidvalue");
  atb.addVersion("1");
  atb.addScope("scopevalue");
  atb.sign("1", WDN_HASH_SHA256);

  UnitKvpAccessToken token(tokenConfig, secrets, enableDebug);
  CHECK(VALID == token.parse(atb.get()));
  CHECK(VALID == token.validateSemantics());
  CHECK(VALID == token.getState());

  CHECK(token.getSubject() == "ABCDEFG");
  CHECK(token.getExpiration() == 1234567);
  CHECK(token.getNotBefore() == 2345678);
  CHECK(token.getIssuedAt() == 3456789);
  CHECK(token.getTokenId() == "tokenidvalue");
  CHECK(token.getVersion() == "1");
  CHECK(token.getScope() == "scopevalue");
  CHECK(token.getKeyId() == "1");
  CHECK(token.getHashFunction() == WDN_HASH_SHA256);

  // DEBUG_OUT(token);
}

TEST_CASE("AssetToken: empty token", "[AssetToken][access_control][utility]")
{
  KvpAccessTokenConfig tokenConfig;
  KvpAccessToken       token(tokenConfig, secrets, enableDebug);
  CHECK(INVALID_SYNTAX == token.parse(""));
}

TEST_CASE("AssetToken: invalid field", "[AssetToken][access_control][utility]")
{
  KvpAccessTokenConfig tokenConfig;

  KvpAccessToken token(tokenConfig, secrets, enableDebug);
  CHECK(INVALID_FIELD == token.parse("NOTVALID=1234567"));
}

TEST_CASE("AssetToken: empty field", "[AssetToken][access_control][utility]")
{
  KvpAccessTokenConfig  tokenConfig;
  KvpAccessTokenBuilder atb(tokenConfig, secrets);
  atb.addSubject("ABCDEFG");
  atb.addExpiration(1234567);

  /* prepend a key-value-pair separator to a valid token */
  KvpAccessToken token1(tokenConfig, secrets, enableDebug);
  CHECK(INVALID_SYNTAX == token1.parse(String(tokenConfig.pairDelimiter).append(atb.get())));

  KvpAccessToken token2(tokenConfig, secrets, enableDebug);
  CHECK(INVALID_SYNTAX == token1.parse(String(atb.get()).append(tokenConfig.pairDelimiter)));

  KvpAccessToken token3(tokenConfig, secrets, enableDebug);
  CHECK(INVALID_SYNTAX == token1.parse(tokenConfig.pairDelimiter));
}

TEST_CASE("AssetToken: missing required fields", "[AssetToken][access_control][utility]")
{
  KvpAccessTokenConfig  tokenConfig;
  KvpAccessTokenBuilder atb(tokenConfig, secrets);

  class UnitKvpAccessToken : public KvpAccessToken
  {
  public:
    using KvpAccessToken::KvpAccessToken;
    using KvpAccessToken::validateSemantics;
  };

  UnitKvpAccessToken token(tokenConfig, secrets, enableDebug);
  CHECK(MISSING_REQUIRED_FIELD == token.validateSemantics());

  /* add subject */
  atb.addSubject("ABCDEFG");
  CHECK(VALID == token.parse(atb.get()));
  CHECK(MISSING_REQUIRED_FIELD == token.validateSemantics());

  /* add expiration */
  atb.addExpiration(1234567);
  CHECK(VALID == token.parse(atb.get()));
  CHECK(MISSING_REQUIRED_FIELD == token.validateSemantics());

  /* add kid and md */
  atb.sign("1", WDN_HASH_SHA256);
  CHECK(VALID == token.parse(atb.get()));
  CHECK(VALID == token.validateSemantics());
}

TEST_CASE("AssetToken: simple HMAC SHA256 signature test", "[AssetToken][access_control][utility]")
{
  KvpAccessTokenConfig tokenConfig;

  KvpAccessTokenBuilder atb(tokenConfig, secrets);
  atb.addSubject("ABCDEFG");
  atb.addExpiration(1234567);
  atb.addNotBefore(2345678);
  atb.addIssuedAt(3456789);
  atb.addTokenId("tokenidvalue");
  atb.addVersion("1");
  atb.addScope("scopevalue");
  atb.sign("1", WDN_HASH_SHA256);

  class UnitKvpAccessToken : public KvpAccessToken
  {
  public:
    using KvpAccessToken::_messageDigest;
    using KvpAccessToken::KvpAccessToken;
    using KvpAccessToken::validateSemantics;
    using KvpAccessToken::validateSignature;
  };

  UnitKvpAccessToken token(tokenConfig, secrets, enableDebug);
  CHECK(VALID == token.parse(atb.get()));
  CHECK(VALID == token.validateSignature());

  // DEBUG_OUT(token);

  /* Now break the signature and test for failure */
  token._messageDigest = "invalid12345";
  CHECK_FALSE(MISSING_REQUIRED_FIELD == token.validateSemantics());
  CHECK(INVALID_SIGNATURE == token.validateSignature());
  // DEBUG_OUT("Dumping token" << std::endl << token);
}

TEST_CASE("AccessToken: scope validation", "[AccessToken][access_control][scope]")
{
  SECTION("empty scope allows any request path")
  {
    CHECK(validateScope("/reports/2026/", "") == true);
    CHECK(validateScope("/any/path/", "") == true);
    CHECK(validateScope("/", "") == true);
  }

  SECTION("exact match")
  {
    CHECK(validateScope("/reports", "/reports") == true);
    CHECK(validateScope("/reports/", "/reports/") == true);
    CHECK(validateScope("/reports", "/reports/") == true);
    CHECK(validateScope("/reports/", "/reports") == true);
  }

  SECTION("valid subpath matching")
  {
    CHECK(validateScope("/reports/2026/", "/reports/") == true);
    CHECK(validateScope("/reports/2026/annual.pdf", "/reports") == true);
    CHECK(validateScope("/api/v1/users/123", "/api/v1/users") == true);
  }

  SECTION("segment boundary enforcement")
  {
    CHECK(validateScope("/reports2/", "/reports/") == false);
    CHECK(validateScope("/reports2", "/reports") == false);
    CHECK(validateScope("/reports_backup/2026", "/reports") == false);
    CHECK(validateScope("/api/v1/users_admin", "/api/v1/users") == false);
  }

  SECTION("mismatched paths")
  {
    CHECK(validateScope("/other/", "/reports/") == false);
    CHECK(validateScope("/other/path", "/reports") == false);
    CHECK(validateScope("/reports", "/reports/2026") == false);
    CHECK(validateScope("/reports/", "/reports/2026/") == false);
  }

  SECTION("normalization and edge cases")
  {
    CHECK(validateScope("/anything", "/") == true);
    CHECK(validateScope("/", "/") == true);
    CHECK(validateScope("reports/2026/", "/reports/") == true);
    CHECK(validateScope("reports/2026/", "reports/") == true);
    CHECK(validateScope("//reports///2026//", "/reports") == true);
  }

  SECTION("path traversal security")
  {
    CHECK(validateScope("/reports/../hr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/../../etc/passwd", "/reports/") == false);
    CHECK(validateScope("/reports/%2e%2e/hr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/%2E%2E/hr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/.%2e/hr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/%2e./hr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/./2026/", "/reports/") == true);
    CHECK(validateScope("/reports/2026/../2026/annual.pdf", "/reports") == true);
    CHECK(validateScope("/reports/%2e/2026/", "/reports/") == true);
    CHECK(validateScope("/reports/%2e%2e%2fhr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/%2e%2e%2Fhr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/..%2fhr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/..%2Fhr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/%2e%2e%5chr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/%2e%2e%5Chr/payroll", "/reports/") == false);
    CHECK(validateScope("/reports/..\\hr/payroll", "/reports/") == false);
  }
}

TEST_CASE("AccessToken: token scope claim integration", "[AccessToken][access_control][scope]")
{
  KvpAccessTokenConfig tokenConfig;

  KvpAccessTokenBuilder atb(tokenConfig, secrets);
  atb.addSubject("FinanceUser");
  atb.addExpiration(1234567);
  atb.addNotBefore(2345678);
  atb.addIssuedAt(3456789);
  atb.addTokenId("tokenidvalue");
  atb.addVersion("1");
  atb.addScope("/finance/");
  atb.sign("1", WDN_HASH_SHA256);

  KvpAccessToken token(tokenConfig, secrets, enableDebug);
  CHECK(VALID == token.parse(atb.get()));
  CHECK(token.getScope() == "/finance/");

  CHECK(validateScope("/finance/q4_report.pdf", token.getScope()) == true);
  CHECK(validateScope("/hr/payroll", token.getScope()) == false);
}
