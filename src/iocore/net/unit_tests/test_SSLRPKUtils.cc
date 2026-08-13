/** @file

  Catch based unit tests for SSLRPKUtils

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

#ifndef LIBINKNET_UNIT_TEST_DIR
#error please set LIBINKNET_UNIT_TEST_DIR
#endif

#define _STR(s)  #s
#define _XSTR(s) _STR(s)

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "tscore/ink_config.h"

#include <openssl/bio.h>
#include <openssl/pem.h>

#include "../SSLRPKUtils.h"

#if TS_USE_RPK

namespace
{
std::string
fixture(const char *name)
{
  return std::string{_XSTR(LIBINKNET_UNIT_TEST_DIR)} + "/" + name;
}

/// Load a bare public key PEM the way a peer's offered key would arrive, for comparison.
EVP_PKEY *
load_pubkey(const char *name)
{
  BIO *bio = BIO_new_file(fixture(name).c_str(), "r");
  if (bio == nullptr) {
    return nullptr;
  }
  EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  return pkey;
}

} // namespace

TEST_CASE("SSLRPKUtils loads a single trusted key", "[rpk]")
{
  SSLRPKUtils::TrustedKeySet keys;
  REQUIRE(SSLRPKUtils::loadTrustedKeys(fixture("rpk_single.pem").c_str(), keys));
  CHECK(keys.size() == 1);
}

TEST_CASE("SSLRPKUtils loads every key in a multi-key file", "[rpk]")
{
  // Key rotation relies on more than one key being accepted from one file.
  SSLRPKUtils::TrustedKeySet keys;
  REQUIRE(SSLRPKUtils::loadTrustedKeys(fixture("rpk_multi.pem").c_str(), keys));
  CHECK(keys.size() == 3);
}

TEST_CASE("SSLRPKUtils reports a missing trusted key file", "[rpk]")
{
  SSLRPKUtils::TrustedKeySet keys;
  CHECK_FALSE(SSLRPKUtils::loadTrustedKeys(fixture("rpk_does_not_exist.pem").c_str(), keys));
}

TEST_CASE("SSLRPKUtils reports a malformed trusted key file", "[rpk]")
{
  // A malformed file must fail rather than silently yielding an empty (accept-nothing) set.
  SSLRPKUtils::TrustedKeySet keys;
  CHECK_FALSE(SSLRPKUtils::loadTrustedKeys(fixture("rpk_malformed.pem").c_str(), keys));
}

TEST_CASE("SSLRPKUtils matches a pinned key", "[rpk]")
{
  SSLRPKUtils::TrustedKeySet keys;
  REQUIRE(SSLRPKUtils::loadTrustedKeys(fixture("rpk_single.pem").c_str(), keys));

  EVP_PKEY *peer = load_pubkey("rpk_single.pem");
  REQUIRE(peer != nullptr);
  CHECK(SSLRPKUtils::pinnedKeyMatches(peer, keys));
  EVP_PKEY_free(peer);
}

TEST_CASE("SSLRPKUtils rejects a key that is not pinned", "[rpk]")
{
  SSLRPKUtils::TrustedKeySet keys;
  REQUIRE(SSLRPKUtils::loadTrustedKeys(fixture("rpk_single.pem").c_str(), keys));

  EVP_PKEY *peer = load_pubkey("rpk_other.pem");
  REQUIRE(peer != nullptr);
  CHECK_FALSE(SSLRPKUtils::pinnedKeyMatches(peer, keys));
  EVP_PKEY_free(peer);
}

TEST_CASE("SSLRPKUtils matches any key in a rotation set", "[rpk]")
{
  SSLRPKUtils::TrustedKeySet keys;
  REQUIRE(SSLRPKUtils::loadTrustedKeys(fixture("rpk_multi.pem").c_str(), keys));

  // Both the first and a later entry must match, including across key algorithms.
  for (auto const *name : {"rpk_single.pem", "rpk_other.pem"}) {
    EVP_PKEY *peer = load_pubkey(name);
    REQUIRE(peer != nullptr);
    CHECK(SSLRPKUtils::pinnedKeyMatches(peer, keys));
    EVP_PKEY_free(peer);
  }
}

TEST_CASE("SSLRPKUtils rejects a null or empty peer key", "[rpk]")
{
  SSLRPKUtils::TrustedKeySet keys;
  REQUIRE(SSLRPKUtils::loadTrustedKeys(fixture("rpk_single.pem").c_str(), keys));

  CHECK_FALSE(SSLRPKUtils::pinnedKeyMatches(static_cast<EVP_PKEY *>(nullptr), keys));
  CHECK_FALSE(SSLRPKUtils::pinnedKeyMatches(nullptr, 0, keys));
}

TEST_CASE("SSLRPKUtils rejects everything when no keys are trusted", "[rpk]")
{
  // An empty trust set must never accept a peer -- this is the fail-closed case.
  SSLRPKUtils::TrustedKeySet empty;
  EVP_PKEY                  *peer = load_pubkey("rpk_single.pem");
  REQUIRE(peer != nullptr);
  CHECK_FALSE(SSLRPKUtils::pinnedKeyMatches(peer, empty));
  EVP_PKEY_free(peer);
}

#endif // TS_USE_RPK
