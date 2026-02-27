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

#include "cripts/CacheGroup.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

// RAII temp directory that cleans up after each test
struct TempDir {
  std::filesystem::path path;

  TempDir()
  {
    path = std::filesystem::temp_directory_path() /
           ("cg_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }

  ~TempDir() { std::filesystem::remove_all(path); }

  std::string
  str() const
  {
    return path.string();
  }
};

TEST_CASE("CacheGroup: basic insert and lookup", "[cripts][CacheGroup]")
{
  TempDir              dir;
  cripts::Cache::Group g("test", dir.str());

  g.Insert("url1");
  g.Insert("url2");

  auto epoch = cripts::Time::Clock::from_time_t(0);
  CHECK(g.Lookup("url1", epoch));
  CHECK(g.Lookup("url2", epoch));
  CHECK_FALSE(g.Lookup("url3", epoch));
}

TEST_CASE("CacheGroup: persist and reload", "[cripts][CacheGroup]")
{
  TempDir dir;
  auto    epoch = cripts::Time::Clock::from_time_t(0);

  {
    cripts::Cache::Group g("test", dir.str());
    g.Insert("key_a");
    g.Insert("key_b");
    g.WriteToDisk();
  }

  cripts::Cache::Group g2("test", dir.str());
  CHECK(g2.Lookup("key_a", epoch));
  CHECK(g2.Lookup("key_b", epoch));
  CHECK_FALSE(g2.Lookup("key_c", epoch));
}

TEST_CASE("CacheGroup: transaction log replay on restart", "[cripts][CacheGroup]")
{
  TempDir dir;
  auto    epoch = cripts::Time::Clock::from_time_t(0);

  {
    cripts::Cache::Group g("test", dir.str());
    g.Insert("persisted");
    g.WriteToDisk();

    // Insert more keys — these go to the txn log but maps are not re-synced
    g.Insert("in_log_only");
  }
  // The txn log should still exist since WriteToDisk was not called after the second Insert

  // Reload: log should be replayed
  cripts::Cache::Group g2("test", dir.str());
  CHECK(g2.Lookup("persisted", epoch));
  CHECK(g2.Lookup("in_log_only", epoch));
}

TEST_CASE("CacheGroup: corrupt map file is skipped", "[cripts][CacheGroup]")
{
  TempDir dir;
  auto    epoch = cripts::Time::Clock::from_time_t(0);

  {
    cripts::Cache::Group g("test", dir.str(), 1024, 2);
    g.Insert("good_key");
    g.WriteToDisk();
  }

  // Corrupt the first map file
  auto map_path = dir.path / "test" / "map_0.bin";
  if (std::filesystem::exists(map_path)) {
    std::ofstream corrupt(map_path, std::ios::binary | std::ios::trunc);
    corrupt << "JUNK_DATA_GARBAGE";
  }

  // Reload — corrupt map should be skipped; good_key is lost (log was cleared by
  // WriteToDisk), but the group must still accept new inserts without crashing.
  cripts::Cache::Group g2("test", dir.str(), 1024, 2);
  CHECK_FALSE(g2.Lookup("good_key", epoch));
  g2.Insert("new_key");
  CHECK(g2.Lookup("new_key", epoch));
}

TEST_CASE("CacheGroup: truncated map file is handled gracefully", "[cripts][CacheGroup]")
{
  TempDir dir;
  auto    epoch = cripts::Time::Clock::from_time_t(0);

  {
    cripts::Cache::Group g("test", dir.str(), 1024, 2);
    g.Insert("truncated_key");
    g.WriteToDisk();
  }

  // Truncate the map file to just the version field (incomplete header)
  auto map_path = dir.path / "test" / "map_0.bin";
  if (std::filesystem::exists(map_path)) {
    auto size = std::filesystem::file_size(map_path);
    if (size > sizeof(uint64_t)) {
      std::filesystem::resize_file(map_path, sizeof(uint64_t) + 1); // version + 1 byte of header
    }
  }

  // Reload — truncated header should be skipped; truncated_key is lost (log was
  // cleared by WriteToDisk), but the group must recover and accept new inserts.
  cripts::Cache::Group g2("test", dir.str(), 1024, 2);
  CHECK_FALSE(g2.Lookup("truncated_key", epoch));
  g2.Insert("after_truncation");
  CHECK(g2.Lookup("after_truncation", epoch));
}

TEST_CASE("CacheGroup: wrong version map file is skipped", "[cripts][CacheGroup]")
{
  TempDir dir;
  auto    epoch = cripts::Time::Clock::from_time_t(0);

  {
    cripts::Cache::Group g("test", dir.str(), 1024, 2);
    g.Insert("versioned_key");
    g.WriteToDisk();
  }

  // Overwrite the version field with a bad value
  auto map_path = dir.path / "test" / "map_0.bin";
  if (std::filesystem::exists(map_path)) {
    std::fstream f(map_path, std::ios::in | std::ios::out | std::ios::binary);
    uint64_t     bad_version = 0xDEADBEEFCAFEBABEULL;
    f.write(reinterpret_cast<const char *>(&bad_version), sizeof(bad_version));
  }

  // Reload — version mismatch should be skipped; versioned_key is lost (log was
  // cleared by WriteToDisk), but the group must recover and accept new inserts.
  cripts::Cache::Group g2("test", dir.str(), 1024, 2);
  CHECK_FALSE(g2.Lookup("versioned_key", epoch));
  g2.Insert("post_version_check");
  CHECK(g2.Lookup("post_version_check", epoch));
}

TEST_CASE("CacheGroup: WriteToDisk does not clear log on sync failure", "[cripts][CacheGroup]")
{
  TempDir dir;
  auto    epoch = cripts::Time::Clock::from_time_t(0);

  cripts::Cache::Group g("test", dir.str(), 1024, 2);
  g.Insert("before_fail");
  g.WriteToDisk(); // initial successful sync + log clear

  g.Insert("after_initial_sync");

  // Make the map directory read-only so syncMap will fail on rename
  auto group_dir = dir.path / "test";
  std::filesystem::permissions(group_dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);

  g.WriteToDisk(); // should fail to sync; log must NOT be cleared

  // Restore permissions so cleanup works
  std::filesystem::permissions(group_dir, std::filesystem::perms::owner_all);

  // The txn log should still contain "after_initial_sync" — verify via reload
  cripts::Cache::Group g2("test", dir.str(), 1024, 2);
  CHECK(g2.Lookup("before_fail", epoch));
  CHECK(g2.Lookup("after_initial_sync", epoch));
}

TEST_CASE("CacheGroup: map rotation writes empty map to disk", "[cripts][CacheGroup]")
{
  TempDir dir;
  auto    epoch = cripts::Time::Clock::from_time_t(0);

  // max_entries=2 to trigger rotation after 2 inserts
  {
    cripts::Cache::Group g("test", dir.str(), 2, 3);
    g.Insert("key1");
    g.Insert("key2");
    g.Insert("key3"); // triggers rotation
    g.WriteToDisk();
  }

  cripts::Cache::Group g2("test", dir.str(), 2, 3);
  // All three keys were in slot 0 at WriteToDisk time and survive the reload.
  CHECK(g2.Lookup("key1", epoch));
  CHECK(g2.Lookup("key2", epoch));
  CHECK(g2.Lookup("key3", epoch));
}
