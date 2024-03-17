/** @file

  A brief file description

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

#define CATCH_CONFIG_MAIN

#include "catch.hpp"

#include "P_Cache.h"
#include "P_CacheHosting.h"

#include "iocore/cache/YamlStorageConfig.h"

#include <yaml-cpp/yaml.h>

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

    YamlStorageConfig::load_volumes(config, in);

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

    YamlStorageConfig::load_volumes(config, in);

    // Expected
    // volumes:
    //   - id: 1
    //     size: 66%
    //   - id: 2
    //     size: 34%
    ConfigVol *v1 = config.cp_queue.head;
    CHECK(v1->size.percent == 66);

    ConfigVol *v2 = config.cp_queue.next(v1);
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

    YamlStorageConfig::load_volumes(config, in);

    // Expected
    // volumes:
    //   - id: 1
    //     spans:
    //       - use: span-1
    //         size: 100%
    ConfigVol *v1 = config.cp_queue.head;

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

    YamlStorageConfig::load_volumes(config, in);

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

    YamlStorageConfig::load_volumes(config, in);

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

    CHECK(v1->size.is_empty());
    CHECK(v1->spans[0].size.percent == 10);

    ConfigVol *v2 = config.cp_queue.next(v1);

    CHECK(v2->size.is_empty());
    CHECK(v2->spans[0].size.percent == 20);

    ConfigVol *v3 = config.cp_queue.next(v2);

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

    YamlStorageConfig::load_volumes(config, in);

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

    CHECK(v1->size.is_empty());
    REQUIRE(v1->spans.size() == 2);

    CHECK(v1->spans[0].size.percent == 66);
    CHECK(v1->spans[1].size.percent == 66);

    ConfigVol *v2 = config.cp_queue.next(v1);

    CHECK(v2->spans[0].size.percent == 17);
    CHECK(v2->spans[1].size.percent == 17);

    ConfigVol *v3 = config.cp_queue.next(v1);

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

    YamlStorageConfig::load_volumes(config, in);

    // Expected
    // volumes:
    //   - id: 1
    //     spans:
    //       - use: span-1
    //         size: 100%
    //   - id: 2
    //     size: 100%
    ConfigVol *v1 = config.cp_queue.head;

    CHECK(v1->size.is_empty());
    REQUIRE(v1->spans.size() == 1);
    CHECK(v1->spans[0].size.in_percent);
    CHECK(v1->spans[0].size.percent == 100);

    ConfigVol *v2 = config.cp_queue.next(v1);

    CHECK(v2->size.in_percent);
    CHECK(v2->size.percent == 100);
  }
}
