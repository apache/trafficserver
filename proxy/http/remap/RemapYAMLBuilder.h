/** @file
 *
 *  YAML configuration for URL rewriting.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
 *  agreements.  See the NOTICE file distributed with this work for additional information regarding
 *  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with the License.  You may
 *  obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software distributed under the
 *  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied. See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <string_view>
#include <bitset>

#include "swoc/TextView.h"
#include "swoc/Errata.h"
#include "yaml-cpp/yaml.h"

class url_mapping;
class UrlRewrite;

/** Parse YAML based url rewriting configuration.
 *
 */
class RemapYAMLBuilder
{
  using self_type = RemapYAMLBuilder;

public:
  /// The root tag for data in the YAML tree.
  static constexpr swoc::TextView ROOT_TAG{"url_rewrite"};
  /// Tag for filter definitions.
  static constexpr swoc::TextView FILTER_DEFINITIONS_TAG{"filters"};
  /// Tag for rule definitions.
  static constexpr swoc::TextView RULE_DEFINITIONS_TAG{"rules"};

  /// Rule options.
  enum class RuleOpt {
    REVERSE,   ///< Reverse rewrite.
    PROXY_PORT ///< Use local inbound port to determine mapping.
  };
  /// A set of options.
  /// @internal This must have an element for each value in @c RuleOpt.
  using RuleOptions = std::bitset<2>;

  /// URL options.
  enum URLOpt {
    REGEX ///< Regular expression
  };
  /// A set of URL options.
  /// @internal This must have an element for each value in @c URLOpt.
  using URLOptions = std::bitset<1>;

  /** Parse @a content as YAML configuration.
   *
   * @param rewriter Configuration object to update.
   * @param content The file content to parse.
   * @return The result of parsing.
   *
   * This is the entry point for going from the text file contents to parsed data.
   */
  static swoc::Errata parse(UrlRewrite *rewriter, std::string const &content);

  /** Handle the top filter tag.
   *
   * @param filters Value of the top filter tag.
   * @return Any errors.
   */
  swoc::Errata parse_filter_definitions(YAML::Node const &filters);

  /** Handle the top rules tag.
   *
   * @param filters Value of the top rules tag.
   * @return Any errors.
   */
  swoc::Errata parse_rule_definitions(YAML::Node const &rules);

  /** Handle a single rewrite rule.
   *
   * @param rule The rule node.
   * @return A new rule instance on success, @c nullptr and errors on failure.
   */
  swoc::Rv<url_mapping *> parse_rule_define(YAML::Node const &rule);

  swoc::Errata &apply_rule_option(swoc::Errata &erratum, YAML::Node const &node, RuleOptions &options);

protected:
  /// Default constructor.
  RemapYAMLBuilder(UrlRewrite *rewriter) : _rewriter(rewriter) {}

  /// Configuration object to update.
  UrlRewrite *_rewriter = nullptr;
};
