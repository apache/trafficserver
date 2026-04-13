/** @file

  Shared remap configuration parsing and marshalling.

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

#include <string>
#include <string_view>

#include <yaml-cpp/yaml.h>

#include "config/config_result.h"

namespace config
{

using RemapConfig = YAML::Node;

/**
 * Parser for remap.yaml / remap.config configuration files.
 *
 * The parser normalizes the legacy remap.config format into the YAML tree shape
 * consumed by the remap.yaml loader so both traffic_server and traffic_ctl can
 * share one conversion path.
 */
class RemapParser
{
public:
  /**
   * Parse a remap configuration file.
   *
   * The format is auto-detected based on the filename extension and content.
   *
   * @param[in] filename Path to the configuration file.
   * @return ConfigResult containing the parsed configuration tree or errors.
   */
  ConfigResult<RemapConfig> parse(std::string const &filename);

  /**
   * Parse remap configuration content from a string.
   *
   * The format is auto-detected from @a filename and @a content.
   *
   * @param[in] content The configuration content to parse.
   * @param[in] filename Synthetic filename used for format detection.
   * @return ConfigResult containing the parsed configuration tree or errors.
   */
  ConfigResult<RemapConfig> parse_content(std::string_view content, std::string const &filename = "remap.yaml");

private:
  enum class Format { YAML, Legacy };

  Format                    detect_format(std::string_view content, std::string const &filename) const;
  ConfigResult<RemapConfig> parse_yaml(std::string_view content) const;
  ConfigResult<RemapConfig> parse_legacy(std::string_view content) const;
};

/**
 * Marshaller for remap configuration.
 */
class RemapMarshaller
{
public:
  /**
   * Serialize configuration to YAML format.
   *
   * @param[in] config The configuration tree to serialize.
   * @return YAML string representation.
   */
  std::string to_yaml(RemapConfig const &config);
};

} // namespace config
