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

#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>

using config::ConfigRegistry;
using config::ConfigSource;

// Shared no-op handler for test registrations
static config::ConfigReloadHandler noop_handler = [](ConfigContext const &) {};

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

// ─── register_plugin_config: filename_record propagation ──────────────────────

TEST_CASE("ConfigRegistry register_plugin_config stores filename_record on the entry",
          "[config][registry][plugin][filename_record]")
{
  auto &reg = ConfigRegistry::Get_Instance();

  // Plugin path: register with a filename_record so an operator can override
  // the active filename via that record at runtime. The plumbing tested here
  // is the only difference between the plugin layer and core register_config:
  // a typo in TSCfgRegister would silently fall back to default_filename.
  reg.register_plugin_config("test_plugin_filename_record", "test_plugin", "default.yaml", "proxy.config.test_plugin.filename",
                             noop_handler, ConfigSource::FileOnly, {}, false);

  auto const *entry = reg.find("test_plugin_filename_record");
  REQUIRE(entry != nullptr);
  CHECK(entry->key == "test_plugin_filename_record");
  CHECK(entry->plugin_name == "test_plugin");
  CHECK_FALSE(entry->plugin_name.empty()); // plugin_name non-empty == plugin entry
  CHECK(entry->default_filename == "default.yaml");
  CHECK(entry->filename_record == "proxy.config.test_plugin.filename");
  CHECK(entry->source == ConfigSource::FileOnly);
  CHECK_FALSE(entry->is_required);
}

TEST_CASE("ConfigRegistry add_file_and_node_dependency leaves no half-state on dep_key collision", "[config][registry][dependency]")
{
  ensure_test_records();
  auto &reg = ConfigRegistry::Get_Instance();

  reg.register_config("test_no_half_state", "", "", noop_handler, ConfigSource::FileAndRpc, {});

  int ret1 = reg.add_file_and_node_dependency("test_no_half_state", "test_no_half_dep", "test.registry.dep.filename1",
                                              "test_sni.yaml", false);
  REQUIRE(ret1 == 0);

  // Duplicate dep_key: must fail without disturbing the live mapping.
  int ret2 = reg.add_file_and_node_dependency("test_no_half_state", "test_no_half_dep", "test.registry.dep.filename1",
                                              "test_sni.yaml", false);
  REQUIRE(ret2 == -1);

  auto [parent_key, entry] = reg.resolve("test_no_half_dep");
  REQUIRE(entry != nullptr);
  REQUIRE(parent_key == "test_no_half_state");
}

TEST_CASE("ConfigRegistry register_plugin_config accepts empty filename_record", "[config][registry][plugin][filename_record]")
{
  auto &reg = ConfigRegistry::Get_Instance();

  // Empty filename_record is the default for plugins that don't expose a
  // runtime-tunable path; resolve_filename() then always uses default_filename.
  reg.register_plugin_config("test_plugin_no_record", "test_plugin", "fixed.yaml", "", noop_handler, ConfigSource::FileOnly, {},
                             false);

  auto const *entry = reg.find("test_plugin_no_record");
  REQUIRE(entry != nullptr);
  CHECK(entry->filename_record.empty());
  CHECK(entry->default_filename == "fixed.yaml");
}

// ─── Duplicate-key registration is reported ───────────────────────────────────

TEST_CASE("ConfigRegistry register_plugin_config reports duplicate-key failure", "[config][registry][plugin][duplicate]")
{
  auto &reg = ConfigRegistry::Get_Instance();

  bool first =
    reg.register_plugin_config("test_plugin_dup_key", "test_plugin", "a.yaml", "", noop_handler, ConfigSource::FileOnly, {}, false);
  REQUIRE(first);

  // A second registration under the same key must be rejected and reported, so
  // TSCfgRegister can surface TS_ERROR instead of a false success.
  bool second = reg.register_plugin_config("test_plugin_dup_key", "other_plugin", "b.yaml", "", noop_handler,
                                           ConfigSource::FileOnly, {}, false);
  REQUIRE_FALSE(second);

  // The original owner is preserved; the losing registration installs nothing.
  auto const *entry = reg.find("test_plugin_dup_key");
  REQUIRE(entry != nullptr);
  CHECK(entry->plugin_name == "test_plugin");
  CHECK(entry->default_filename == "a.yaml");
}

// ─── is_reloadable: handler-less entries are not schedulable ───────────────────

TEST_CASE("ConfigRegistry is_reloadable distinguishes handler-less entries", "[config][registry][reloadable]")
{
  auto &reg = ConfigRegistry::Get_Instance();

  // An entry with a handler is reloadable.
  reg.register_config("test_reloadable_with_handler", "", "", noop_handler, ConfigSource::FileOnly, {});
  CHECK(reg.is_reloadable("test_reloadable_with_handler"));

  // A catalog-only entry (no handler) must NOT be routed to schedule_reload; this
  // is the guard that keeps a storage.config/plugin.config edit from aborting the
  // server via the handler-less reload path.
  reg.register_config("test_reloadable_no_handler", "", "", nullptr, ConfigSource::FileOnly, {});
  CHECK_FALSE(reg.is_reloadable("test_reloadable_no_handler"));

  // An unknown key is not reloadable.
  CHECK_FALSE(reg.is_reloadable("test_reloadable_unknown_xyz"));
}

// ─── ConfigContext content on the execute_reload() path ───────────────────────
//
// No reload is in progress here, so execute_reload() hands the handler a task-less
// context, and the RPC-supplied content is still applied to it.

namespace
{
ConfigContext
reload_with(std::string const &key, YAML::Node const &content)
{
  // Copy-assignment has its own test below, so this helper does not use it: emplace()
  // and reset() only copy-construct and destroy contexts.
  static std::optional<ConfigContext> seen;

  auto &reg = ConfigRegistry::Get_Instance();
  if (!reg.contains(key)) {
    reg.register_config(key, "", "", [](ConfigContext ctx) { seen.emplace(ctx); }, ConfigSource::FileAndRpc, {});
  }
  seen.reset();
  reg.set_passed_config(key, YAML::Clone(content));
  reg.execute_reload(key);
  REQUIRE(seen.has_value());

  ConfigContext ctx{*seen};
  seen.reset();
  return ctx;
}
} // namespace

TEST_CASE("ConfigContext: RPC content and directives reach the handler", "[config][context][payload]")
{
  ConfigContext const ctx = reload_with("test_ctx_payload_both", YAML::Load("{_reload: {id: foo}, data: x}"));

  YAML::Node const content = ctx.supplied_yaml();
  REQUIRE(content);
  REQUIRE(content["data"].as<std::string>() == "x");
  REQUIRE_FALSE(content["_reload"]);

  YAML::Node const directives = ctx.reload_directives();
  REQUIRE(directives);
  REQUIRE(directives["id"].as<std::string>() == "foo");
}

TEST_CASE("ConfigContext: assigning to a copy leaves the original alone", "[config][context][payload]")
{
  ConfigContext const original = reload_with("test_ctx_payload_assign", YAML::Load("{_reload: {id: foo}, data: x}"));

  ConfigContext copy = original;
  copy               = ConfigContext{};
  REQUIRE_FALSE(copy.supplied_yaml());

  YAML::Node const content = original.supplied_yaml();
  REQUIRE(content);
  REQUIRE(content["data"].as<std::string>() == "x");
  YAML::Node const directives = original.reload_directives();
  REQUIRE(directives);
  REQUIRE(directives["id"].as<std::string>() == "foo");

  // The plugin wrapper assigns a handler's context into an empty one it keeps.
  ConfigContext kept;
  kept = original;
  REQUIRE(kept.supplied_yaml().is(content));
  REQUIRE(kept.reload_directives().is(directives));
}

TEST_CASE("ConfigContext: copies share the content", "[config][context][payload]")
{
  ConfigContext const ctx  = reload_with("test_ctx_payload_shared", YAML::Load("{_reload: {id: foo}, data: x}"));
  ConfigContext const copy = ctx;

  REQUIRE(copy.supplied_yaml().is(ctx.supplied_yaml()));
  REQUIRE(copy.reload_directives().is(ctx.reload_directives()));
}

TEST_CASE("ConfigContext: a dependent of a task-less context has no content", "[config][context][payload]")
{
  ConfigContext       ctx   = reload_with("test_ctx_payload_taskless_dep", YAML::Load("{_reload: {id: foo}, data: x}"));
  ConfigContext const child = ctx.add_dependent_ctx("dependent");

  REQUIRE(ctx.supplied_yaml());
  REQUIRE_FALSE(child.supplied_yaml());
  REQUIRE_FALSE(child.reload_directives());
}

TEST_CASE("ConfigContext: a copy keeps the content after the original is gone", "[config][context][payload]")
{
  std::optional<ConfigContext> original{reload_with("test_ctx_payload_outlive", YAML::Load("{_reload: {id: foo}, data: x}"))};
  ConfigContext const          copy = *original;
  original.reset();

  YAML::Node const content = copy.supplied_yaml();
  REQUIRE(content);
  REQUIRE(content["data"].as<std::string>() == "x");
  YAML::Node const directives = copy.reload_directives();
  REQUIRE(directives);
  REQUIRE(directives["id"].as<std::string>() == "foo");
}

TEST_CASE("ConfigContext: content without directives keeps directives absent", "[config][context][payload]")
{
  ConfigContext const ctx = reload_with("test_ctx_payload_content_only", YAML::Load("{data: x}"));
  REQUIRE(ctx.supplied_yaml());

  {
    // A careless handler writing into the result must not give the context directives.
    auto directives = ctx.reload_directives();
    REQUIRE_FALSE(directives);
    directives["id"] = "injected";
  }
  REQUIRE_FALSE(ctx.reload_directives());
}

TEST_CASE("ConfigContext: directives without content keep content absent", "[config][context][payload]")
{
  ConfigContext const ctx = reload_with("test_ctx_payload_directives_only", YAML::Load("{_reload: {id: bar}}"));

  YAML::Node const directives = ctx.reload_directives();
  REQUIRE(directives);
  REQUIRE(directives["id"].as<std::string>() == "bar");

  {
    auto content = ctx.supplied_yaml();
    REQUIRE_FALSE(content);
    content["rules"] = "injected";
  }
  REQUIRE_FALSE(ctx.supplied_yaml());
}
