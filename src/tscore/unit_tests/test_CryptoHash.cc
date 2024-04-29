/**
  @file Test for CryptoHash

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

#include <array>
#include <string_view>

#include "tscore/ink_assert.h"
#include "tscore/ink_defs.h"
#include "tscore/CryptoHash.h"
#include "catch.hpp"

TEST_CASE("CrypoHash", "[libts][CrypoHash]")
{
  CryptoHash        hash;
  ts::CryptoContext ctx;
  std::string_view  test   = "asdfsfsdfljhasdfkjasdkfuy239874kasjdf";
  std::string_view  sha256 = "2602CBA2CC0331EB7C455E9F36030B32CE9BB432A90759075F5A702772BE123B";
  std::string_view  md5    = "480AEF8C24AA94B80DC6214ECEC8CD1A";

  // Hash the test data
  ctx.update(test.data(), test.size());
  ctx.finalize(hash);

  // Write the output to a string
  char buffer[(CRYPTO_HASH_SIZE * 2) + 1];
  hash.toHexStr(buffer);

  // Compair to a known hash value
  if (CryptoContext::Setting == CryptoContext::SHA256) {
    REQUIRE(strlen(buffer) == sha256.size());
    if (strlen(buffer) == sha256.size()) {
      REQUIRE(memcmp(sha256.data(), buffer, sha256.size()) == 0);
    }
  } else {
    REQUIRE(strlen(buffer) == md5.size());
    REQUIRE(memcmp(md5.data(), buffer, md5.size()) == 0);
  }
}
