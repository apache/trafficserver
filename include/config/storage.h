/** @file

  Storage configuration parsing and marshalling.

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

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "config/config_result.h"

namespace config
{

/**
 * Represents a single storage span entry in storage.yaml.
 */
struct StorageSpanEntry {
  std::string name;
  std::string path;
  std::string hash_seed; ///< optional
  int64_t     size = 0;  ///< optional for raw device
};

/**
 * Represents a single volume entry in storage.yaml.
 */
struct StorageVolumeEntry {
  struct Size {
    int64_t absolute_value = 0;
    bool    in_percent     = false;
    int     percent        = 0;

    bool
    is_empty() const
    {
      return !in_percent && absolute_value == 0;
    }
  };

  struct SpanRef {
    std::string use;
    Size        size;
  };

  int                  id     = 0;
  std::string          scheme = "http";
  Size                 size;
  bool                 ram_cache        = true;
  int64_t              ram_cache_size   = -1;
  int64_t              ram_cache_cutoff = -1;
  int                  avg_obj_size     = -1;
  int                  fragment_size    = -1;
  std::vector<SpanRef> spans;
};

/**
 * Top-level storage configuration.
 */
struct StorageConfig {
  std::vector<StorageSpanEntry>   spans;
  std::vector<StorageVolumeEntry> volumes;
};

/**
 * Parser for storage.yaml / storage.config configuration files.
 *
 * Supports both the current YAML format (storage.yaml) and the legacy
 * line-based format (storage.config).  The format is auto-detected by
 * file extension when parse() is used: files ending in ".yaml" are
 * parsed as YAML; all others are parsed as legacy storage.config.
 */
class StorageParser
{
public:
  /**
   * Parse a storage configuration file.
   *
   * Files ending in ".yaml" are parsed as YAML (storage.yaml); all other
   * files are parsed using the legacy storage.config line-based format.
   *
   * @param[in] filename Path to the configuration file.
   * @return ConfigResult containing the parsed configuration or errors.
   */
  ConfigResult<StorageConfig> parse(std::string const &filename);

  /**
   * Parse storage.yaml content from a string (useful for unit tests).
   *
   * @param[in] content The YAML content to parse.
   * @return ConfigResult containing the parsed configuration or errors.
   */
  ConfigResult<StorageConfig> parse_content(std::string_view content);

  /**
   * Parse legacy storage.config content from a string.
   *
   * Each non-blank, non-comment line has the form:
   *   pathname [size_bytes] [id=hash_seed] [volume=N]
   *
   * Spans whose lines carry a "volume=N" annotation produce a
   * StorageVolumeEntry with id=N and a span-ref to the span.
   *
   * @param[in] content The legacy storage.config content to parse.
   * @return ConfigResult containing the parsed configuration or errors.
   */
  ConfigResult<StorageConfig> parse_legacy_storage_content(std::string_view content);
};

/**
 * Parser for the legacy volume.config configuration file.
 *
 * Each non-blank, non-comment line has the form:
 *   volume=N scheme=http size=V[%] [avg_obj_size=V] [fragment_size=V]
 *                                  [ramcache=true|false]
 *                                  [ram_cache_size=V] [ram_cache_cutoff=V]
 *
 * Absolute sizes are in megabytes; percent sizes are written as "N%".
 */
class VolumeParser
{
public:
  /**
   * Parse a volume.config file.
   *
   * @param[in] filename Path to the configuration file.
   * @return ConfigResult whose value has only the volumes field populated.
   */
  ConfigResult<StorageConfig> parse(std::string const &filename);

  /**
   * Parse legacy volume.config content from a string (useful for unit tests).
   *
   * @param[in] content The legacy volume.config content to parse.
   * @return ConfigResult whose value has only the volumes field populated.
   */
  ConfigResult<StorageConfig> parse_content(std::string_view content);
};

/**
 * Marshaller for storage configuration.
 *
 * Serializes configuration to YAML or JSON format.
 */
class StorageMarshaller
{
public:
  /**
   * Serialize configuration to YAML format.
   *
   * @param[in] config The configuration to serialize.
   * @return YAML string representation.
   */
  std::string to_yaml(StorageConfig const &config);

  /**
   * Serialize configuration to JSON format.
   *
   * @param[in] config The configuration to serialize.
   * @return JSON string representation.
   */
  std::string to_json(StorageConfig const &config);
};

} // namespace config
