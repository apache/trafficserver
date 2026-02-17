/** @file

  Unit tests for ConfigRegistry: resolve(), add_file_and_node_dependency(), dependency key routing.

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

#include <catch2/catch_test_macros.hpp>

#include "mgmt/config/ConfigRegistry.h"
#include "records/RecCore.h"

using config::ConfigRegistry;
using config::ConfigSource;

// Shared no-op handler for test registrations
static config::ConfigReloadHandler noop_handler = [](ConfigContext &) {};

namespace
{
// Register test-specific records so RecRegisterConfigUpdateCb succeeds.
// Called once; RecRegisterConfigUpdateCb requires the record to exist in g_records_ht.
void
ensure_test_records()
{
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  RecRegisterConfigString(RECT_CONFIG, "test.registry.dep.filename1", const_cast<char *>("test_sni.yaml"), RECU_NULL, RECC_NULL,
                          nullptr, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "test.registry.dep.filename2", const_cast<char *>("test_multicert.config"), RECU_NULL,
                          RECC_NULL, nullptr, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "test.registry.dep.filename3", const_cast<char *>("test_child_a.yaml"), RECU_NULL, RECC_NULL,
                          nullptr, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "test.registry.dep.filename4", const_cast<char *>("test_child_b.config"), RECU_NULL,
                          RECC_NULL, nullptr, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "test.registry.dep.filename5", const_cast<char *>("test_dep_for_b.yaml"), RECU_NULL,
                          RECC_NULL, nullptr, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "test.registry.dep.dup", const_cast<char *>("dup.yaml"), RECU_NULL, RECC_NULL, nullptr,
                          REC_SOURCE_DEFAULT);
}
} // namespace

// ─── Direct entry resolution (no Records/FileManager needed) ──────────────────

TEST_CASE("ConfigRegistry resolve() with direct entries", "[config][registry][resolve]")
{
  auto &reg = ConfigRegistry::Get_Instance();

  // No file, no triggers — pure map operation
  reg.register_config("test_direct_resolve", "", "", noop_handler, ConfigSource::FileOnly, {});

  SECTION("Direct entry found")
  {
    auto [parent_key, entry] = reg.resolve("test_direct_resolve");
    REQUIRE(entry != nullptr);
    REQUIRE(parent_key == "test_direct_resolve");
    REQUIRE(entry->key == "test_direct_resolve");
  }

  SECTION("Unknown key returns nullptr")
  {
    auto [parent_key, entry] = reg.resolve("nonexistent_key_xyz");
    REQUIRE(entry == nullptr);
    REQUIRE(parent_key.empty());
  }
}

// ─── add_file_and_node_dependency: basic ──────────────────────────────────────

TEST_CASE("ConfigRegistry add_file_and_node_dependency resolves to parent", "[config][registry][dependency]")
{
  ensure_test_records();
  auto &reg = ConfigRegistry::Get_Instance();

  reg.register_config("test_coordinator", "", "", noop_handler, ConfigSource::FileAndRpc, {});

  int ret =
    reg.add_file_and_node_dependency("test_coordinator", "test_dep_sni", "test.registry.dep.filename1", "test_sni.yaml", false);
  REQUIRE(ret == 0);

  // The dep_key resolves to the parent entry
  auto [parent_key, entry] = reg.resolve("test_dep_sni");
  REQUIRE(entry != nullptr);
  REQUIRE(parent_key == "test_coordinator");
  REQUIRE(entry->key == "test_coordinator");
  REQUIRE(entry->source == ConfigSource::FileAndRpc);

  // find() and contains() should NOT find dep_keys — only resolve() does
  REQUIRE(reg.find("test_dep_sni") == nullptr);
  REQUIRE_FALSE(reg.contains("test_dep_sni"));
}

// ─── add_file_and_node_dependency: rejection cases ────────────────────────────

TEST_CASE("ConfigRegistry add_file_and_node_dependency rejects duplicates", "[config][registry][dependency]")
{
  ensure_test_records();
  auto &reg = ConfigRegistry::Get_Instance();

  reg.register_config("test_coord_dup", "", "", noop_handler, ConfigSource::FileAndRpc, {});

  int ret1 = reg.add_file_and_node_dependency("test_coord_dup", "test_dup_dep", "test.registry.dep.dup", "dup.yaml", false);
  REQUIRE(ret1 == 0);
  // Same dep_key again should fail
  int ret2 = reg.add_file_and_node_dependency("test_coord_dup", "test_dup_dep", "test.registry.dep.dup", "dup.yaml", false);
  REQUIRE(ret2 == -1);
}

TEST_CASE("ConfigRegistry add_file_and_node_dependency rejects dep colliding with entry", "[config][registry][dependency]")
{
  ensure_test_records();
  auto &reg = ConfigRegistry::Get_Instance();

  reg.register_config("test_coord_coll", "", "", noop_handler, ConfigSource::FileAndRpc, {});
  reg.register_config("test_collision_entry", "", "", noop_handler, ConfigSource::FileOnly, {});

  // Dep_key same name as existing entry should fail
  int ret = reg.add_file_and_node_dependency("test_coord_coll", "test_collision_entry", "test.registry.dep.filename2",
                                             "test_multicert.config", false);
  REQUIRE(ret == -1);
}

TEST_CASE("ConfigRegistry add_file_and_node_dependency rejects unknown parent", "[config][registry][dependency]")
{
  ensure_test_records();
  auto &reg = ConfigRegistry::Get_Instance();

  int ret = reg.add_file_and_node_dependency("nonexistent_parent", "test_orphan_dep", "test.registry.dep.filename1",
                                             "test_sni.yaml", false);
  REQUIRE(ret == -1);
}

// ─── Multiple dep_keys for same parent ────────────────────────────────────────

TEST_CASE("ConfigRegistry multiple dep_keys resolve to same parent", "[config][registry][dependency][grouping]")
{
  ensure_test_records();
  auto &reg = ConfigRegistry::Get_Instance();

  reg.register_config("test_multi_parent", "", "", noop_handler, ConfigSource::FileAndRpc, {});

  int ret1 =
    reg.add_file_and_node_dependency("test_multi_parent", "test_child_a", "test.registry.dep.filename3", "child_a.yaml", false);
  int ret2 =
    reg.add_file_and_node_dependency("test_multi_parent", "test_child_b", "test.registry.dep.filename4", "child_b.config", false);
  REQUIRE(ret1 == 0);
  REQUIRE(ret2 == 0);

  // Both dep_keys resolve to the same parent
  auto [key_a, entry_a] = reg.resolve("test_child_a");
  auto [key_b, entry_b] = reg.resolve("test_child_b");

  REQUIRE(entry_a != nullptr);
  REQUIRE(entry_b != nullptr);
  REQUIRE(key_a == "test_multi_parent");
  REQUIRE(key_b == "test_multi_parent");
  REQUIRE(entry_a == entry_b); // same Entry*

  // Parent itself still resolves directly
  auto [parent_key, entry] = reg.resolve("test_multi_parent");
  REQUIRE(entry != nullptr);
  REQUIRE(parent_key == "test_multi_parent");
}

// ─── resolve() with mixed entries and deps ────────────────────────────────────

TEST_CASE("ConfigRegistry resolve() does not confuse entries and deps", "[config][registry][resolve]")
{
  ensure_test_records();
  auto &reg = ConfigRegistry::Get_Instance();

  reg.register_config("test_entry_a", "", "", noop_handler, ConfigSource::FileOnly, {});
  reg.register_config("test_entry_b", "", "", noop_handler, ConfigSource::FileAndRpc, {});

  int ret = reg.add_file_and_node_dependency("test_entry_b", "test_dep_for_b", "test.registry.dep.filename5", "dep_b.yaml", false);
  REQUIRE(ret == 0);

  // Direct entry resolves to itself
  auto [key_a, entry_a] = reg.resolve("test_entry_a");
  REQUIRE(entry_a != nullptr);
  REQUIRE(key_a == "test_entry_a");

  // Dep key resolves to its parent, not other entries
  auto [key_b, entry_b] = reg.resolve("test_dep_for_b");
  REQUIRE(entry_b != nullptr);
  REQUIRE(key_b == "test_entry_b");
  REQUIRE(entry_b->source == ConfigSource::FileAndRpc);
}
