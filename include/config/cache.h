/** @file

  Cache rule configuration parsing and marshalling.

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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config_result.h"

namespace config
{

/// Cache behavior selected by a rule.
enum class CacheMode {
  NEVER,
  STANDARD,
};

/// Request attributes used to select a cache rule.
struct CacheMatch {
  std::optional<std::string> dest_host;
  std::optional<std::string> dest_domain;
  std::optional<std::string> dest_ip;
  std::optional<std::string> url_regex;
  std::optional<std::string> host_regex;
  std::optional<std::string> port;
  std::optional<std::string> scheme;
  std::optional<std::string> prefix;
  std::optional<std::string> suffix;
  std::optional<std::string> method;
  std::optional<std::string> time;
  std::optional<std::string> src_ip;
  std::optional<std::string> incoming_port;
  std::optional<std::string> tag;
  std::optional<bool>        internal;

  /// @return The number of primary destination selectors.
  int primary_count() const;

  /// @return @c true if the rule has no match restrictions.
  bool empty() const;
};

/// Cache policy applied by a matching rule.
struct CacheAction {
  std::optional<CacheMode>   cache;
  std::optional<std::string> revalidate;
  std::optional<std::string> pin_in_cache;
  std::optional<std::string> ttl_in_cache;
  std::optional<bool>        ignore_no_cache;
  std::optional<bool>        ignore_client_no_cache;
  std::optional<bool>        ignore_server_no_cache;
  std::optional<int>         cache_responses_to_cookies;

  /// @return @c true if the action changes cache policy.
  bool effective() const;
};

/// One ordered cache selection and policy rule.
struct CacheRule {
  CacheMatch  match;
  CacheAction action;
};

/// The ordered rules in a cache configuration.
using CacheConfig = std::vector<CacheRule>;

/**
 * Parser for cache rule configuration files.
 *
 * Both cache.yaml and the legacy cache.config syntax are supported. The file
 * extension is used first for format detection, with content inspection as a
 * fallback.
 */
class CacheConfigParser
{
public:
  /**
   * Parse a cache configuration file.
   *
   * @param[in] filename Path to the configuration file.
   * @return The parsed rules and any diagnostics.
   */
  ConfigResult<CacheConfig> parse(std::string const &filename) const;

  /**
   * Parse cache configuration content.
   *
   * @param[in] content Configuration content.
   * @param[in] filename Name used for format detection.
   * @return The parsed rules and any diagnostics.
   */
  ConfigResult<CacheConfig> parse_content(std::string_view content, std::string_view filename) const;

private:
  enum class Format { YAML, Legacy };

  Format                    detect_format(std::string_view content, std::string_view filename) const;
  ConfigResult<CacheConfig> parse_yaml(std::string_view content) const;
  ConfigResult<CacheConfig> parse_legacy(std::string_view content) const;
};

/// Serializes cache rules in cache.yaml format.
class CacheConfigMarshaller
{
public:
  /**
   * Serialize ordered cache rules.
   *
   * @param[in] config Cache rules to serialize.
   * @return A cache.yaml document.
   */
  std::string to_yaml(CacheConfig const &config) const;
};

} // namespace config
