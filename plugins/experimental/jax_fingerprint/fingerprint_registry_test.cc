/** @file

  Tests for the versioned JAx fingerprint registry contract.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements. See the NOTICE file distributed with this work for additional information regarding
  copyright ownership. Licensed under the Apache License, Version 2.0 (the "License"); you may not
  use this file except in compliance with the License. You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

#include "fingerprint_registry.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("JAx fingerprint registry validates and finds length-delimited values", "[jax_fingerprint][registry]")
{
  static constexpr jax_fingerprint::RegistryEntryV1 entries[] = {
    {"JA3",         "0123456789abcdef", 3,  16},
    {"SITE_METHOD", "opaque-value",     11, 12},
  };
  constexpr jax_fingerprint::RegistryV1 registry{
    jax_fingerprint::REGISTRY_MAGIC,
    jax_fingerprint::REGISTRY_ABI_VERSION,
    sizeof(jax_fingerprint::RegistryV1),
    sizeof(jax_fingerprint::RegistryEntryV1),
    0,
    2,
    entries,
  };

  CHECK(jax_fingerprint::is_valid(&registry));
  CHECK(jax_fingerprint::find(&registry, "JA3") == "0123456789abcdef");
  CHECK(jax_fingerprint::find(&registry, "SITE_METHOD") == "opaque-value");
  CHECK(jax_fingerprint::find(&registry, "missing").empty());
}

TEST_CASE("JAx fingerprint registry rejects incompatible versions", "[jax_fingerprint][registry]")
{
  constexpr jax_fingerprint::RegistryV1 registry{
    jax_fingerprint::REGISTRY_MAGIC,
    jax_fingerprint::REGISTRY_ABI_VERSION + 1,
    sizeof(jax_fingerprint::RegistryV1),
    sizeof(jax_fingerprint::RegistryEntryV1),
    0,
    0,
    nullptr,
  };

  CHECK_FALSE(jax_fingerprint::is_valid(&registry));
}

TEST_CASE("JAx fingerprint registry honors a producer's entry stride", "[jax_fingerprint][registry]")
{
  struct ExtendedEntry {
    jax_fingerprint::RegistryEntryV1 entry;
    uint64_t                         extension;
  };

  static constexpr ExtendedEntry entries[] = {
    {{"ONE", "first", 3, 5},  1},
    {{"TWO", "second", 3, 6}, 2},
  };
  constexpr jax_fingerprint::RegistryV1 registry{
    jax_fingerprint::REGISTRY_MAGIC,
    jax_fingerprint::REGISTRY_ABI_VERSION,
    sizeof(jax_fingerprint::RegistryV1),
    sizeof(ExtendedEntry),
    0,
    2,
    &entries[0].entry,
  };

  CHECK(jax_fingerprint::find(&registry, "TWO") == "second");
}
