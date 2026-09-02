/** @file

  Unit tests for ConfigVolumes

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

#include "P_CacheHosting.h"

#include "config/storage.h"

namespace
{

/**
 * Helper: parse a bare 'volumes:' YAML snippet into ConfigVolumes.
 *
 * The test YAML only contains a top-level 'volumes:' key. Wrap it in the
 * 'cache:' envelope that StorageParser expects, then convert the resulting
 * StorageVolumeEntry list into ConfigVol objects.
 */
bool
load_volumes(ConfigVolumes &v_config, std::string const &yaml)
{
  // Wrap the bare 'volumes:' snippet in a 'cache:' block.
  std::string wrapped = "cache:\n";

  // Indent every line by 2 spaces under 'cache:'.
  std::string            indented;
  std::string::size_type pos = 0;
  indented.reserve(yaml.size() + 64);
  while (pos < yaml.size()) {
    auto next  = yaml.find('\n', pos);
    indented  += "  ";
    if (next == std::string::npos) {
      indented += yaml.substr(pos);
      break;
    }
    indented += yaml.substr(pos, next - pos + 1);
    pos       = next + 1;
  }
  wrapped += indented;

  config::StorageParser parser;
  auto                  result = parser.parse_content(wrapped);
  if (!result.ok()) {
    return false;
  }

  for (auto const &vv : result.value.volumes) {
    auto *vol                = new ConfigVol();
    vol->number              = vv.id;
    vol->scheme              = (vv.scheme == "http") ? CacheType::HTTP : CacheType::NONE;
    vol->ramcache_enabled    = vv.ram_cache;
    vol->size.in_percent     = vv.size.in_percent;
    vol->size.percent        = vv.size.percent;
    vol->size.absolute_value = vv.size.absolute_value;
    vol->ram_cache_size      = vv.ram_cache_size;
    vol->ram_cache_cutoff    = vv.ram_cache_cutoff;
    vol->avg_obj_size        = vv.avg_obj_size;
    vol->fragment_size       = vv.fragment_size;
    for (auto const &sr : vv.spans) {
      ConfigVol::Span span;
      span.use                 = sr.use;
      span.size.in_percent     = sr.size.in_percent;
      span.size.percent        = sr.size.percent;
      span.size.absolute_value = sr.size.absolute_value;
      vol->spans.push_back(std::move(span));
    }
    v_config.cp_queue.enqueue(vol);
    v_config.num_volumes++;
  }
  v_config.complement();
  return true;
}

} // namespace

/**
  Unit Test to complement omitted size.
 */
TEST_CASE("ConfigVolumes::complement")
{
  SECTION("simple 2 volumes")
  {
    ConfigVolumes config;

    std::string in = R"EOF(
      volumes:
        - id: 1
        - id: 2
    )EOF";

    load_volumes(config, in);

    // Expected
    // volumes:
    //   - id: 1
    //     size: 50%
    //   - id: 2
    //     size: 50%
    for (ConfigVol *c = config.cp_queue.head; c != nullptr; c = config.cp_queue.next(c)) {
      CHECK(c->size.in_percent);
      CHECK(c->size.percent == 50);
    }
  }

  SECTION("one-third volume")
  {
    ConfigVolumes config;

    std::string in = R"EOF(
      volumes:
      - id: 1
        size: 66%
      - id: 2
    )EOF";

    load_volumes(config, in);

    // Expected
    // volumes:
    //   - id: 1
    //     size: 66%
    //   - id: 2
    //     size: 34%
    ConfigVol *v1 = config.cp_queue.head;
    REQUIRE(v1 != nullptr);
    CHECK(v1->size.percent == 66);

    ConfigVol *v2 = config.cp_queue.next(v1);
    REQUIRE(v2 != nullptr);
    CHECK(v2->size.percent == 34);
  }

  SECTION("simple exclusive span")
  {
    ConfigVolumes config;

    std::string in = R"EOF(
      volumes:
        - id: 1
          spans:
            - use: "span-1"
    )EOF";

    load_volumes(config, in);

    // Expected
    // volumes:
    //   - id: 1
    //     spans:
    //       - use: span-1
    //         size: 100%
    ConfigVol *v1 = config.cp_queue.head;

    REQUIRE(v1 != nullptr);
    CHECK(v1->size.is_empty());
    REQUIRE(v1->spans.size() == 1);
    CHECK(v1->spans[0].size.in_percent);
    CHECK(v1->spans[0].size.percent == 100);
  }

  SECTION("simple shared span")
  {
    ConfigVolumes config;

    std::string in = R"EOF(
      volumes:
        - id: 1
          spans:
            - use: span-1
        - id: 2
          spans:
            - use: span-1
    )EOF";

    load_volumes(config, in);

    // Expected
    // volumes:
    //   - id: 1
    //     spans:
    //       - use: span-1
    //         size: 50%
    //   - id: 2
    //     spans:
    //       - use: span-1
    //         size: 50%
    for (ConfigVol *c = config.cp_queue.head; c != nullptr; c = config.cp_queue.next(c)) {
      CHECK(c->size.is_empty());
      REQUIRE(c->spans.size() == 1);
      CHECK(c->spans[0].size.in_percent);
      CHECK(c->spans[0].size.percent == 50);
    }
  }

  SECTION("shared span")
  {
    ConfigVolumes config;

    std::string in = R"EOF(
      volumes:
        - id: 1
          spans:
            - use: span-1
              size: 10%
        - id: 2
          spans:
            - use: span-1
              size: 20%
        - id: 3
          spans:
            - use: span-1
    )EOF";

    load_volumes(config, in);

    // Expected
    // volumes:
    //   - id: 1
    //     spans:
    //       - use: span-1
    //         size: 10%
    //   - id: 2
    //     spans:
    //       - use: span-1
    //         size: 20%
    //   - id: 3
    //     spans:
    //       - use: span-1
    //         size: 70%
    ConfigVol *v1 = config.cp_queue.head;

    REQUIRE(v1 != nullptr);
    CHECK(v1->size.is_empty());
    CHECK(v1->spans[0].size.percent == 10);

    ConfigVol *v2 = config.cp_queue.next(v1);

    REQUIRE(v2 != nullptr);
    CHECK(v2->size.is_empty());
    CHECK(v2->spans[0].size.percent == 20);

    ConfigVol *v3 = config.cp_queue.next(v2);

    REQUIRE(v3 != nullptr);
    CHECK(v3->size.is_empty());
    CHECK(v3->spans[0].size.percent == 70);
  }

  SECTION("two shared spans")
  {
    ConfigVolumes config;

    std::string in = R"EOF(
      volumes:
        - id: 1
          spans:
            - use: span-1
              size: 66%
            - use: span-2
              size: 66%
        - id: 2
          spans:
            - use: span-1
            - use: span-2
        - id: 3
          spans:
            - use: span-1
            - use: span-2
    )EOF";

    load_volumes(config, in);

    // Expected
    // volumes:
    //   - id: 1
    //       - spans:
    //           - use: span-1
    //             size: 66%
    //           - use: ram.2
    //             size: 66%
    //   - id: 2
    //       - spans:
    //           - use: span-1
    //             size: 17%
    //           - use: ram.2
    //             size: 17%
    //   - id: 3
    //       - spans:
    //           - use: span-1
    //             size: 17%
    //           - use: ram.2
    //             size: 17%
    ConfigVol *v1 = config.cp_queue.head;

    REQUIRE(v1 != nullptr);
    CHECK(v1->size.is_empty());
    REQUIRE(v1->spans.size() == 2);

    CHECK(v1->spans[0].size.percent == 66);
    CHECK(v1->spans[1].size.percent == 66);

    ConfigVol *v2 = config.cp_queue.next(v1);

    CHECK(v2->spans[0].size.percent == 17);
    CHECK(v2->spans[1].size.percent == 17);

    ConfigVol *v3 = config.cp_queue.next(v2);

    CHECK(v3->spans[0].size.percent == 17);
    CHECK(v3->spans[1].size.percent == 17);
  }

  SECTION("mixed volumes")
  {
    ConfigVolumes config;

    std::string in = R"EOF(
      volumes:
        - id: 1
          spans:
            - use: span-1
        - id: 2
    )EOF";

    load_volumes(config, in);

    // Expected
    // volumes:
    //   - id: 1
    //     spans:
    //       - use: span-1
    //         size: 100%
    //   - id: 2
    //     size: 100%
    ConfigVol *v1 = config.cp_queue.head;

    REQUIRE(v1 != nullptr);
    CHECK(v1->size.is_empty());
    REQUIRE(v1->spans.size() == 1);
    CHECK(v1->spans[0].size.in_percent);
    CHECK(v1->spans[0].size.percent == 100);

    ConfigVol *v2 = config.cp_queue.next(v1);

    REQUIRE(v2 != nullptr);
    CHECK(v2->size.in_percent);
    CHECK(v2->size.percent == 100);
  }
}
