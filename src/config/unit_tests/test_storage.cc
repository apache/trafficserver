/** @file

  Unit tests for storage configuration parsing and marshalling.

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

#include "config/storage.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "swoc/Errata.h"
#include "tsutil/ts_diag_levels.h"

using namespace config;

namespace
{

class TempFile
{
public:
  TempFile(std::string const &filename, std::string const &content)
  {
    _path = std::filesystem::temp_directory_path() / filename;
    std::ofstream ofs(_path);
    ofs << content;
  }

  ~TempFile() { std::filesystem::remove(_path); }

  std::string
  path() const
  {
    return _path.string();
  }

private:
  std::filesystem::path _path;
};

ConfigResult<StorageConfig>
parse_file(std::string const &content, std::string const &filename = "storage.yaml")
{
  TempFile      file(filename, content);
  StorageParser parser;
  return parser.parse(file.path());
}

} // namespace

// ============================================================================
// Spans-only configuration
// ============================================================================

static constexpr char SPANS_ONLY_YAML[] = R"(cache:
  spans:
    - name: span-1
      path: /var/cache/span1
      size: 10G
    - name: span-2
      path: /var/cache/span2
      hash_seed: myseed
)";

TEST_CASE("StorageParser parses spans-only config", "[storage][parser][spans]")
{
  auto result = parse_file(SPANS_ONLY_YAML);

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.size() == 2);
  CHECK(result.value.volumes.empty());

  SECTION("First span")
  {
    auto const &span = result.value.spans[0];
    CHECK(span.name == "span-1");
    CHECK(span.path == "/var/cache/span1");
    CHECK(span.size == 10LL * 1024 * 1024 * 1024);
    CHECK(span.hash_seed.empty());
  }

  SECTION("Second span with hash_seed")
  {
    auto const &span = result.value.spans[1];
    CHECK(span.name == "span-2");
    CHECK(span.path == "/var/cache/span2");
    CHECK(span.hash_seed == "myseed");
  }
}

// ============================================================================
// Volumes-only configuration
// ============================================================================

static constexpr char VOLUMES_ONLY_YAML[] = R"(cache:
  spans:
    - name: span-1
      path: /dev/sdb
  volumes:
    - id: 1
      scheme: http
      size: 60%
    - id: 2
      size: 40%
)";

TEST_CASE("StorageParser parses volumes config", "[storage][parser][volumes]")
{
  auto result = parse_file(VOLUMES_ONLY_YAML);

  REQUIRE(result.ok());
  REQUIRE(result.value.volumes.size() == 2);

  SECTION("First volume")
  {
    auto const &vol = result.value.volumes[0];
    CHECK(vol.id == 1);
    CHECK(vol.scheme == "http");
    CHECK(vol.size.in_percent);
    CHECK(vol.size.percent == 60);
    CHECK(vol.ram_cache);
  }

  SECTION("Second volume")
  {
    auto const &vol = result.value.volumes[1];
    CHECK(vol.id == 2);
    CHECK(vol.size.in_percent);
    CHECK(vol.size.percent == 40);
  }
}

// ============================================================================
// Full configuration (spans + volumes with span refs)
// ============================================================================

static constexpr char FULL_YAML[] = R"(cache:
  spans:
    - name: span-1
      path: /dev/sdb
    - name: span-2
      path: /dev/sdc
      hash_seed: abc123
  volumes:
    - id: 1
      scheme: http
      size: 50%
      ram_cache: true
      ram_cache_size: 1G
      ram_cache_cutoff: 256K
      avg_obj_size: 8K
      fragment_size: 512K
    - id: 2
      spans:
        - use: span-1
          size: 75%
        - use: span-2
)";

TEST_CASE("StorageParser parses full spans+volumes config", "[storage][parser][full]")
{
  auto result = parse_file(FULL_YAML);

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.size() == 2);
  REQUIRE(result.value.volumes.size() == 2);

  SECTION("Span with hash_seed")
  {
    CHECK(result.value.spans[1].hash_seed == "abc123");
  }

  SECTION("Volume 1 fields")
  {
    auto const &vol = result.value.volumes[0];
    CHECK(vol.id == 1);
    CHECK(vol.size.in_percent);
    CHECK(vol.size.percent == 50);
    CHECK(vol.ram_cache);
    CHECK(vol.ram_cache_size == 1LL * 1024 * 1024 * 1024);
    CHECK(vol.ram_cache_cutoff == 256LL * 1024);
    CHECK(vol.avg_obj_size == 8 * 1024);
    CHECK(vol.fragment_size == 512 * 1024);
  }

  SECTION("Volume 2 with span refs")
  {
    auto const &vol = result.value.volumes[1];
    CHECK(vol.id == 2);
    REQUIRE(vol.spans.size() == 2);
    CHECK(vol.spans[0].use == "span-1");
    CHECK(vol.spans[0].size.in_percent);
    CHECK(vol.spans[0].size.percent == 75);
    CHECK(vol.spans[1].use == "span-2");
  }
}

// ============================================================================
// Error cases
// ============================================================================

TEST_CASE("StorageParser returns error for duplicate volume id", "[storage][parser][error]")
{
  static constexpr char YAML[] = R"(cache:
  spans:
    - name: span-1
      path: /dev/sdb
  volumes:
    - id: 1
    - id: 1
)";
  auto                  result = parse_file(YAML);
  CHECK_FALSE(result.ok());
}

TEST_CASE("StorageParser returns error when percent total exceeds 100", "[storage][parser][error]")
{
  static constexpr char YAML[] = R"(cache:
  spans:
    - name: span-1
      path: /dev/sdb
  volumes:
    - id: 1
      size: 70%
    - id: 2
      size: 40%
)";
  auto                  result = parse_file(YAML);
  CHECK_FALSE(result.ok());
}

TEST_CASE("StorageParser returns error for missing file", "[storage][parser][error]")
{
  StorageParser parser;
  auto          result = parser.parse("/nonexistent/path/to/storage.yaml");
  CHECK_FALSE(result.ok());
}

TEST_CASE("StorageParser returns error for missing top-level cache key", "[storage][parser][error]")
{
  auto result = parse_file("spans:\n  - name: x\n    path: /tmp\n");
  CHECK_FALSE(result.ok());
}

TEST_CASE("StorageParser returns error for invalid YAML syntax", "[storage][parser][error]")
{
  auto result = parse_file("cache: [not: valid: yaml");
  CHECK_FALSE(result.ok());
}

TEST_CASE("StorageParser warns on unknown keys", "[storage][parser][warning]")
{
  // Set FAILURE_SEVERITY to match ATS production (DL_Warning), so DL_Note
  // annotations do not make is_ok() return false.
  swoc::Errata::FAILURE_SEVERITY = swoc::Errata::Severity{static_cast<swoc::Errata::severity_type>(DL_Warning)};

  static constexpr char YAML[] = R"(cache:
  spans:
    - name: span-1
      path: /dev/sdb
      unknown_key: value
)";
  auto                  result = parse_file(YAML);
  // Parse succeeds (unknown key is a note not an error).
  CHECK(result.ok());
  CHECK(result.value.spans.size() == 1);
  // Errata should contain a note about the unknown key.
  CHECK_FALSE(result.errata.empty());

  // Restore default so other tests are unaffected.
  swoc::Errata::FAILURE_SEVERITY = swoc::Errata::Severity{2};
}

// ============================================================================
// parse_content (in-memory)
// ============================================================================

TEST_CASE("StorageParser::parse_content works without file I/O", "[storage][parser][content]")
{
  StorageParser parser;
  auto          result = parser.parse_content(SPANS_ONLY_YAML);

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.size() == 2);
}

// ============================================================================
// Marshaller
// ============================================================================

// ============================================================================
// Legacy storage.config parser
// ============================================================================

TEST_CASE("StorageParser parses legacy storage.config", "[storage][legacy][storage_config]")
{
  static constexpr char STORAGE_CONFIG[] = "# comment line\n"
                                           "/dev/sda\n"
                                           "/dev/sdb 10737418240\n" // 10 GB in bytes
                                           "/var/cache/disk3 id=myseed\n"
                                           "/dev/sdc 5368709120 volume=2\n" // 5 GB, assigned to volume 2
                                           "\n"
                                           "# another comment\n"
                                           "/dev/sdd volume=1\n";

  TempFile      file("storage.config", STORAGE_CONFIG);
  StorageParser parser;
  auto          result = parser.parse(file.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.size() == 5);

  SECTION("plain path uses path as name")
  {
    CHECK(result.value.spans[0].name == "/dev/sda");
    CHECK(result.value.spans[0].path == "/dev/sda");
    CHECK(result.value.spans[0].size == 0);
    CHECK(result.value.spans[0].hash_seed.empty());
  }

  SECTION("path with size")
  {
    CHECK(result.value.spans[1].path == "/dev/sdb");
    CHECK(result.value.spans[1].size == 10737418240LL);
  }

  SECTION("path with hash seed")
  {
    CHECK(result.value.spans[2].path == "/var/cache/disk3");
    CHECK(result.value.spans[2].hash_seed == "myseed");
  }

  SECTION("spans with volume annotations produce volume entries")
  {
    REQUIRE(result.value.volumes.size() == 2);

    // volumes are stored in map-order (id=1 before id=2)
    bool found_vol1 = false;
    bool found_vol2 = false;
    for (auto const &vol : result.value.volumes) {
      if (vol.id == 1) {
        found_vol1 = true;
        REQUIRE(vol.spans.size() == 1);
        CHECK(vol.spans[0].use == "/dev/sdd");
      } else if (vol.id == 2) {
        found_vol2 = true;
        REQUIRE(vol.spans.size() == 1);
        CHECK(vol.spans[0].use == "/dev/sdc");
      }
    }
    CHECK(found_vol1);
    CHECK(found_vol2);
  }
}

TEST_CASE("StorageParser::parse_legacy_storage_content parses basic lines", "[storage][legacy][storage_config][content]")
{
  static constexpr char CONTENT[] = "/path/to/disk1\n"
                                    "/path/to/disk2 1073741824\n"; // 1 GB

  StorageParser parser;
  auto          result = parser.parse_legacy_storage_content(CONTENT);

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.size() == 2);
  CHECK(result.value.spans[0].path == "/path/to/disk1");
  CHECK(result.value.spans[1].size == 1073741824LL);
}

TEST_CASE("StorageParser: single span exclusively assigned to one volume", "[storage][legacy][storage_config][volume]")
{
  static constexpr char CONTENT[] = "/dev/sda volume=1\n";

  StorageParser parser;
  auto          result = parser.parse_legacy_storage_content(CONTENT);

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.size() == 1);
  REQUIRE(result.value.volumes.size() == 1);
  CHECK(result.value.volumes[0].id == 1);
  REQUIRE(result.value.volumes[0].spans.size() == 1);
  CHECK(result.value.volumes[0].spans[0].use == "/dev/sda");
}

TEST_CASE("StorageParser: multiple spans exclusively assigned to different volumes", "[storage][legacy][storage_config][volume]")
{
  static constexpr char CONTENT[] = "/dev/sda volume=1\n"
                                    "/dev/sdb volume=2\n"
                                    "/dev/sdc volume=1\n"; // second span for volume 1

  StorageParser parser;
  auto          result = parser.parse_legacy_storage_content(CONTENT);

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.size() == 3);
  REQUIRE(result.value.volumes.size() == 2);

  // volumes are in map order (id=1 before id=2)
  auto const &vol1 = result.value.volumes[0];
  REQUIRE(vol1.id == 1);
  REQUIRE(vol1.spans.size() == 2);
  CHECK(vol1.spans[0].use == "/dev/sda");
  CHECK(vol1.spans[1].use == "/dev/sdc");

  auto const &vol2 = result.value.volumes[1];
  REQUIRE(vol2.id == 2);
  REQUIRE(vol2.spans.size() == 1);
  CHECK(vol2.spans[0].use == "/dev/sdb");
}

TEST_CASE("StorageParser: unassigned spans produce no volume entries", "[storage][legacy][storage_config][volume]")
{
  // No volume= annotations anywhere - no volumes should be created.
  static constexpr char CONTENT[] = "/dev/sda\n"
                                    "/dev/sdb 10737418240\n";

  StorageParser parser;
  auto          result = parser.parse_legacy_storage_content(CONTENT);

  REQUIRE(result.ok());
  CHECK(result.value.spans.size() == 2);
  CHECK(result.value.volumes.empty());
}

TEST_CASE("StorageParser: mix of assigned and unassigned spans", "[storage][legacy][storage_config][volume]")
{
  // /dev/sda and /dev/sdc are not assigned to any volume.
  // /dev/sdb is exclusively assigned to volume 3.
  static constexpr char CONTENT[] = "/dev/sda\n"
                                    "/dev/sdb volume=3\n"
                                    "/dev/sdc\n";

  StorageParser parser;
  auto          result = parser.parse_legacy_storage_content(CONTENT);

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.size() == 3);
  REQUIRE(result.value.volumes.size() == 1);
  CHECK(result.value.volumes[0].id == 3);
  REQUIRE(result.value.volumes[0].spans.size() == 1);
  CHECK(result.value.volumes[0].spans[0].use == "/dev/sdb");
}

TEST_CASE("StorageParser: volume=N with size and id= on the same line", "[storage][legacy][storage_config][volume]")
{
  static constexpr char CONTENT[] = "/dev/sda 5368709120 id=myseed volume=2\n";

  StorageParser parser;
  auto          result = parser.parse_legacy_storage_content(CONTENT);

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.size() == 1);
  CHECK(result.value.spans[0].size == 5368709120LL);
  CHECK(result.value.spans[0].hash_seed == "myseed");
  REQUIRE(result.value.volumes.size() == 1);
  CHECK(result.value.volumes[0].id == 2);
  CHECK(result.value.volumes[0].spans[0].use == "/dev/sda");
}

TEST_CASE("StorageParser: volume= id and volume= annotations in any order", "[storage][legacy][storage_config][volume]")
{
  // volume= before id= - order on the line should not matter.
  static constexpr char CONTENT[] = "/dev/sda volume=5 id=seed1\n";

  StorageParser parser;
  auto          result = parser.parse_legacy_storage_content(CONTENT);

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.size() == 1);
  CHECK(result.value.spans[0].hash_seed == "seed1");
  REQUIRE(result.value.volumes.size() == 1);
  CHECK(result.value.volumes[0].id == 5);
}

// ============================================================================
// merge_legacy_storage_configs
// ============================================================================

TEST_CASE("merge_legacy_storage_configs: volume=N spans get size/scheme from volume.config", "[storage][legacy][merge]")
{
  // storage.config: two spans, each exclusively assigned to a volume.
  static constexpr char STORAGE[] = "/dev/sda volume=1\n"
                                    "/dev/sdb volume=2\n";

  // volume.config: two volumes with explicit size and scheme.
  static constexpr char VOLUMES[] = "volume=1 scheme=http size=60%\n"
                                    "volume=2 scheme=http size=40%\n";

  StorageParser storage_parser;
  auto          storage_result = storage_parser.parse_legacy_storage_content(STORAGE);
  REQUIRE(storage_result.ok());

  VolumeParser volume_parser;
  auto         volume_result = volume_parser.parse_content(VOLUMES);
  REQUIRE(volume_result.ok());

  StorageConfig merged = merge_legacy_storage_configs(storage_result.value, volume_result.value);

  REQUIRE(merged.spans.size() == 2);
  REQUIRE(merged.volumes.size() == 2);

  // Volume 1: size from volume.config, span ref from storage.config.
  auto const &vol1 = merged.volumes[0];
  CHECK(vol1.id == 1);
  CHECK(vol1.size.in_percent);
  CHECK(vol1.size.percent == 60);
  REQUIRE(vol1.spans.size() == 1);
  CHECK(vol1.spans[0].use == "/dev/sda");

  // Volume 2: size from volume.config, span ref from storage.config.
  auto const &vol2 = merged.volumes[1];
  CHECK(vol2.id == 2);
  CHECK(vol2.size.in_percent);
  CHECK(vol2.size.percent == 40);
  REQUIRE(vol2.spans.size() == 1);
  CHECK(vol2.spans[0].use == "/dev/sdb");
}

TEST_CASE("merge_legacy_storage_configs: multiple spans assigned to the same volume", "[storage][legacy][merge]")
{
  // /dev/sda and /dev/sdc both go to volume 1.
  static constexpr char STORAGE[] = "/dev/sda volume=1\n"
                                    "/dev/sdb volume=2\n"
                                    "/dev/sdc volume=1\n";

  static constexpr char VOLUMES[] = "volume=1 scheme=http size=60%\n"
                                    "volume=2 scheme=http size=40%\n";

  StorageParser storage_parser;
  auto          storage_result = storage_parser.parse_legacy_storage_content(STORAGE);
  REQUIRE(storage_result.ok());

  VolumeParser volume_parser;
  auto         volume_result = volume_parser.parse_content(VOLUMES);
  REQUIRE(volume_result.ok());

  StorageConfig merged = merge_legacy_storage_configs(storage_result.value, volume_result.value);

  REQUIRE(merged.volumes.size() == 2);

  auto const &vol1 = merged.volumes[0];
  CHECK(vol1.id == 1);
  REQUIRE(vol1.spans.size() == 2);
  CHECK(vol1.spans[0].use == "/dev/sda");
  CHECK(vol1.spans[1].use == "/dev/sdc");

  auto const &vol2 = merged.volumes[1];
  CHECK(vol2.id == 2);
  REQUIRE(vol2.spans.size() == 1);
  CHECK(vol2.spans[0].use == "/dev/sdb");
}

TEST_CASE("merge_legacy_storage_configs: unassigned spans are not attached to any volume", "[storage][legacy][merge]")
{
  // /dev/sdb has no volume= annotation.
  static constexpr char STORAGE[] = "/dev/sda volume=1\n"
                                    "/dev/sdb\n";

  static constexpr char VOLUMES[] = "volume=1 scheme=http size=100%\n";

  StorageParser storage_parser;
  auto          storage_result = storage_parser.parse_legacy_storage_content(STORAGE);
  REQUIRE(storage_result.ok());

  VolumeParser volume_parser;
  auto         volume_result = volume_parser.parse_content(VOLUMES);
  REQUIRE(volume_result.ok());

  StorageConfig merged = merge_legacy_storage_configs(storage_result.value, volume_result.value);

  REQUIRE(merged.spans.size() == 2);
  REQUIRE(merged.volumes.size() == 1);

  auto const &vol1 = merged.volumes[0];
  CHECK(vol1.id == 1);
  REQUIRE(vol1.spans.size() == 1);
  CHECK(vol1.spans[0].use == "/dev/sda");
}

TEST_CASE("merge_legacy_storage_configs: volume.config volume without matching storage.config annotation has no span refs",
          "[storage][legacy][merge]")
{
  // storage.config has no volume= lines; volume.config defines a volume.
  // The merged volume should have the size/scheme but empty spans.
  static constexpr char STORAGE[] = "/dev/sda\n"
                                    "/dev/sdb\n";

  static constexpr char VOLUMES[] = "volume=1 scheme=http size=50%\n";

  StorageParser storage_parser;
  auto          storage_result = storage_parser.parse_legacy_storage_content(STORAGE);
  REQUIRE(storage_result.ok());

  VolumeParser volume_parser;
  auto         volume_result = volume_parser.parse_content(VOLUMES);
  REQUIRE(volume_result.ok());

  StorageConfig merged = merge_legacy_storage_configs(storage_result.value, volume_result.value);

  REQUIRE(merged.spans.size() == 2);
  REQUIRE(merged.volumes.size() == 1);
  CHECK(merged.volumes[0].id == 1);
  CHECK(merged.volumes[0].size.percent == 50);
  CHECK(merged.volumes[0].spans.empty());
}

TEST_CASE("merge_legacy_storage_configs: empty volume.config preserves storage.config partial volumes", "[storage][legacy][merge]")
{
  // When volume.config is absent (empty), keep the partial volumes from storage.config.
  static constexpr char STORAGE[] = "/dev/sda volume=1\n";

  StorageParser storage_parser;
  auto          storage_result = storage_parser.parse_legacy_storage_content(STORAGE);
  REQUIRE(storage_result.ok());

  StorageConfig empty_volumes;
  StorageConfig merged = merge_legacy_storage_configs(storage_result.value, empty_volumes);

  REQUIRE(merged.volumes.size() == 1);
  CHECK(merged.volumes[0].id == 1);
  REQUIRE(merged.volumes[0].spans.size() == 1);
  CHECK(merged.volumes[0].spans[0].use == "/dev/sda");
}

TEST_CASE("merge_legacy_storage_configs: volume attributes from volume.config are preserved", "[storage][legacy][merge]")
{
  static constexpr char STORAGE[] = "/dev/sda volume=3\n";

  static constexpr char VOLUMES[] = "volume=3 scheme=http size=512 "
                                    "avg_obj_size=8192 fragment_size=524288 ramcache=false "
                                    "ram_cache_size=1073741824 ram_cache_cutoff=262144\n";

  StorageParser storage_parser;
  auto          storage_result = storage_parser.parse_legacy_storage_content(STORAGE);
  REQUIRE(storage_result.ok());

  VolumeParser volume_parser;
  auto         volume_result = volume_parser.parse_content(VOLUMES);
  REQUIRE(volume_result.ok());

  StorageConfig merged = merge_legacy_storage_configs(storage_result.value, volume_result.value);

  REQUIRE(merged.volumes.size() == 1);
  auto const &vol = merged.volumes[0];
  CHECK(vol.id == 3);
  CHECK(vol.size.absolute_value == 512);
  CHECK_FALSE(vol.ram_cache);
  CHECK(vol.avg_obj_size == 8192);
  CHECK(vol.fragment_size == 524288);
  CHECK(vol.ram_cache_size == 1073741824LL);
  CHECK(vol.ram_cache_cutoff == 262144LL);
  REQUIRE(vol.spans.size() == 1);
  CHECK(vol.spans[0].use == "/dev/sda");
}

// ============================================================================
// Legacy volume.config parser
// ============================================================================

TEST_CASE("VolumeParser parses legacy volume.config", "[storage][legacy][volume_config]")
{
  static constexpr char VOLUME_CONFIG[] = "# comment\n"
                                          "volume=1 scheme=http size=60%\n"
                                          "volume=2 scheme=http size=40%\n";

  TempFile     file("volume.config", VOLUME_CONFIG);
  VolumeParser parser;
  auto         result = parser.parse(file.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.spans.empty());
  REQUIRE(result.value.volumes.size() == 2);

  SECTION("first volume")
  {
    auto const &vol = result.value.volumes[0];
    CHECK(vol.id == 1);
    CHECK(vol.scheme == "http");
    CHECK(vol.size.in_percent);
    CHECK(vol.size.percent == 60);
    CHECK(vol.ram_cache);
  }

  SECTION("second volume")
  {
    auto const &vol = result.value.volumes[1];
    CHECK(vol.id == 2);
    CHECK(vol.size.in_percent);
    CHECK(vol.size.percent == 40);
  }
}

TEST_CASE("VolumeParser::parse_content parses absolute size in MB", "[storage][legacy][volume_config][content]")
{
  static constexpr char CONTENT[] = "volume=1 scheme=http size=1024\n"; // 1024 MB

  VolumeParser parser;
  auto         result = parser.parse_content(CONTENT);

  REQUIRE(result.ok());
  REQUIRE(result.value.volumes.size() == 1);
  CHECK_FALSE(result.value.volumes[0].size.in_percent);
  CHECK(result.value.volumes[0].size.absolute_value == 1024);
}

TEST_CASE("VolumeParser::parse_content parses all fields", "[storage][legacy][volume_config][content]")
{
  static constexpr char CONTENT[] = "volume=3 scheme=http size=512 "
                                    "avg_obj_size=8192 fragment_size=524288 "
                                    "ramcache=false ram_cache_size=1073741824 ram_cache_cutoff=262144\n";

  VolumeParser parser;
  auto         result = parser.parse_content(CONTENT);

  REQUIRE(result.ok());
  REQUIRE(result.value.volumes.size() == 1);

  auto const &vol = result.value.volumes[0];
  CHECK(vol.id == 3);
  CHECK(vol.size.absolute_value == 512);
  CHECK_FALSE(vol.ram_cache);
  CHECK(vol.avg_obj_size == 8192);
  CHECK(vol.fragment_size == 524288);
  CHECK(vol.ram_cache_size == 1073741824LL);
  CHECK(vol.ram_cache_cutoff == 262144LL);
}

TEST_CASE("VolumeParser returns error for duplicate volume number", "[storage][legacy][volume_config][error]")
{
  static constexpr char CONTENT[] = "volume=1 scheme=http size=50%\n"
                                    "volume=1 scheme=http size=30%\n";

  VolumeParser parser;
  auto         result = parser.parse_content(CONTENT);

  // Second entry with duplicate id should be skipped; first parsed OK.
  // The errata records the duplicate.
  CHECK(result.value.volumes.size() == 1);
  CHECK_FALSE(result.errata.empty());
}

TEST_CASE("VolumeParser returns error when percent total exceeds 100", "[storage][legacy][volume_config][error]")
{
  static constexpr char CONTENT[] = "volume=1 scheme=http size=70%\n"
                                    "volume=2 scheme=http size=40%\n";

  VolumeParser parser;
  auto         result = parser.parse_content(CONTENT);

  CHECK_FALSE(result.errata.empty());
}

TEST_CASE("VolumeParser returns error for missing file", "[storage][legacy][volume_config][error]")
{
  VolumeParser parser;
  auto         result = parser.parse("/nonexistent/volume.config");
  CHECK_FALSE(result.ok());
}

TEST_CASE("StorageMarshaller produces valid YAML", "[storage][marshaller][yaml]")
{
  StorageConfig config;

  StorageSpanEntry span1;
  span1.name = "span-1";
  span1.path = "/var/cache/span1";
  span1.size = 10LL * 1024 * 1024 * 1024;
  config.spans.push_back(std::move(span1));

  StorageVolumeEntry vol1;
  vol1.id              = 1;
  vol1.scheme          = "http";
  vol1.size.in_percent = true;
  vol1.size.percent    = 100;
  config.volumes.push_back(std::move(vol1));

  StorageMarshaller marshaller;
  std::string       yaml = marshaller.to_yaml(config);

  SECTION("YAML contains expected fields")
  {
    CHECK(yaml.find("cache:") != std::string::npos);
    CHECK(yaml.find("span-1") != std::string::npos);
    CHECK(yaml.find("/var/cache/span1") != std::string::npos);
    CHECK(yaml.find("id: 1") != std::string::npos);
    CHECK(yaml.find("http") != std::string::npos);
  }

  SECTION("YAML can be re-parsed")
  {
    StorageParser parser;
    auto          result = parser.parse_content(yaml);
    REQUIRE(result.ok());
    REQUIRE(result.value.spans.size() == 1);
    REQUIRE(result.value.volumes.size() == 1);
    CHECK(result.value.spans[0].name == "span-1");
    CHECK(result.value.volumes[0].id == 1);
  }
}

TEST_CASE("StorageMarshaller produces valid JSON", "[storage][marshaller][json]")
{
  StorageConfig config;

  StorageSpanEntry span1;
  span1.name = "span-1";
  span1.path = "/var/cache/span1";
  config.spans.push_back(std::move(span1));

  StorageVolumeEntry vol1;
  vol1.id = 1;
  config.volumes.push_back(std::move(vol1));

  StorageMarshaller marshaller;
  std::string       json = marshaller.to_json(config);

  CHECK(json.find("\"cache\"") != std::string::npos);
  CHECK(json.find("\"span-1\"") != std::string::npos);
  CHECK(json.find("\"id\"") != std::string::npos);
  CHECK(json.find('{') != std::string::npos);
  CHECK(json.find('}') != std::string::npos);
}

TEST_CASE("Round-trip: parse -> marshal -> parse", "[storage][roundtrip]")
{
  StorageParser     parser;
  StorageMarshaller marshaller;

  auto initial = parser.parse_content(FULL_YAML);
  REQUIRE(initial.ok());

  std::string yaml       = marshaller.to_yaml(initial.value);
  auto        round_trip = parser.parse_content(yaml);
  REQUIRE(round_trip.ok());

  REQUIRE(initial.value.spans.size() == round_trip.value.spans.size());
  REQUIRE(initial.value.volumes.size() == round_trip.value.volumes.size());

  for (size_t i = 0; i < initial.value.spans.size(); ++i) {
    CHECK(initial.value.spans[i].name == round_trip.value.spans[i].name);
    CHECK(initial.value.spans[i].path == round_trip.value.spans[i].path);
    CHECK(initial.value.spans[i].hash_seed == round_trip.value.spans[i].hash_seed);
  }

  for (size_t i = 0; i < initial.value.volumes.size(); ++i) {
    CHECK(initial.value.volumes[i].id == round_trip.value.volumes[i].id);
    CHECK(initial.value.volumes[i].scheme == round_trip.value.volumes[i].scheme);
  }
}
