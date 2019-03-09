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
#include "RemapBuilder.h"

class url_mapping;
class UrlRewrite;

/** Parse YAML based url rewriting configuration.
 *
 */
class RemapYAMLBuilder : public RemapBuilder
{
  using self_type  = RemapYAMLBuilder; ///< Self reference type.
  using super_type = RemapBuilder;     ///< Parent type.

public:
  /// The root key for data in the YAML tree.
  static constexpr swoc::TextView ROOT_KEY{"url_rewrite"};
  /// Key for filter definitions.
  static constexpr swoc::TextView FILTER_DEFINITIONS_KEY{"filters"};
  /// Key for rule definitions.
  static constexpr swoc::TextView RULE_DEFINITIONS_KEY{"rules"};
  /// Key for redirect.
  static constexpr swoc::TextView REDIRECT_KEY{"redirect"};
  /// Value for redirect.
  /// @{
  static constexpr swoc::TextView REDIRECT_VALUE_PERMANENT{"permanent"};
  static constexpr swoc::TextView REDIRECT_VALUE_TEMPORARY{"temporary"};
  /// @}

  /// Value type for negating.
  static constexpr swoc::TextView YAML_NOT_TYPE{"!not"};

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

  /** Constructor.
   *
   * @param url_rewriter The persistent store of remap information.
   */
  explicit RemapYAMLBuilder(UrlRewrite *url_rewriter) : super_type{url_rewriter} {}

  /** Parse @a content as YAML configuration.
   *
   * @param rewriter Configuration object to update.
   * @param content The file content to parse.
   * @return The result of parsing.
   *
   * This is the entry point for going from the text file contents to parsed data.
   */
  static swoc::Errata parse(UrlRewrite *rewriter, std::string const &content);

  /** Parse the named filters.
   *
   * @param filters Node containing the named filter definitions.
   * @return Any errors.
   */
  swoc::Errata parse_filter_definitions(YAML::Node const &filters);

  /** Parse direct filters for a rule @a mp.
   *
   * @param node Value node containing the filters.
   * @param mp The rule.
   * @return Any processing errors.
   */
  swoc::Errata parse_rule_filter(YAML::Node const &node, url_mapping *mp);

  /** Parse a rule directive.
   *
   * @param node Value node with the directive.
   * @return Errors, if any.
   */
  swoc::Errata parse_directive(YAML::Node const &node);

  /** Make a named filter active.
   *
   * @param name Name of the filter.
   * @return Errors, if any.
   *
   * This adds the filter to end of the active list. It is error if there is no named filter with
   * @a name. It is not an error for the filter to already be active.
   */
  swoc::Errata enable_filter(swoc::TextView name);

  /** Parse a directive to enable a filter.
   *
   * @param node Value node with the directive.
   * @return Errors, if any.
   */
  swoc::Errata parse_enable_directive(YAML::Node const &node);

  /** Make a named filter inactive.
   *
   * @param name Name of the filter.
   * @return Errors, if any.
   *
   * This removes the most recent instance of the filter from the active list, reporting an error if
   * the filter is not found.
   */
  swoc::Errata disable_filter(swoc::TextView name);

  /** Parse a disable filter directive.
   *
   * @param node Value node with the directive.
   * @return Errors, if any.
   */
  swoc::Errata parse_disable_directive(YAML::Node const &node);

  /** Parse a plugin definition for a rule.
   *
   * @param node Value node with the plugin definition.
   * @param mp The rule.
   * @return Errors, if any.
   *
   * This handles the plugin path and all arguments. This does not load the plugin.
   */
  swoc::Errata parse_plugin_define(YAML::Node const &node, url_mapping *mp);

  /** Handle the top rules tag.
   *
   * @param filters Value of the top rules tag.
   * @return Any errors.
   */
  swoc::Errata parse_directives(YAML::Node const &rules);

  /** Handle a single rewrite rule.
   *
   * @param rule The rule node.
   * @return Any errors.
   */
  swoc::Errata parse_rule_define(YAML::Node const &rule);

  /** Parse a node for an original URL to rewrite.
   *
   * @param erratum [out] Error reporting.
   * @param parent [in] Base node.
   * @param url [out] A URL object to update from the text.
   * @param opts [out] Options set by the value.
   */
  void parse_target_url(swoc::Errata &erratum, YAML::Node const &parent, URL &url, URLOptions &opts);

  /** Parse a node for a URL as the new URL in a rewriting.
   *
   * @param erratum [out] Error reporting.
   * @param parent [in] Base node.
   * @param url [out] A URL object to update from the text.
   * @param opts [out] Options set by the value.
   */
  void parse_replacement_url(swoc::Errata &erratum, YAML::Node const &parent, URL &url, URLOptions &opts);

  /** Parse a filter definition.
   *
   * @param node Node of the definition.
   * @return A new @c RemapFilter, or error.
   */
  swoc::Rv<RemapFilter *> parse_filter_define(YAML::Node const &node);

  /** Parse an IP address range.
   *
   * @param node [in] Value node with the range string.
   * @param min [out] Minimum address in the range.
   * @param max [out] Maximum address in the range.
   * @return Errors, if any.
   */
  swoc::Errata parse_ip_addr(YAML::Node const &node, IpAddr &min, IpAddr &max);

  /** Parse an IP address range for remote inbound addresses.
   *
   * @param node Value node with the range string.
   * @param filter Instance to update with the range.
   * @return Errors, if any.
   */
  swoc::Errata parse_filter_src_ip_range(YAML::Node const &node, RemapFilter *filter);

  /** Parse an IP address range for local inbound addresses.
   *
   * @param node Value node with the range string.
   * @param filter Instance to update with the range.
   * @return Errors, if any.
   */
  swoc::Errata parse_filter_proxy_ip_range(YAML::Node const &node, RemapFilter *filter);

  /** Parse a regular expression match for a referer rule.
   *
   * @param node [in] Value node with the regular expression string.
   * @param mp [out] Rule to update.
   * @return Errors, if any.
   */
  swoc::Errata parse_referer_match(YAML::Node const &node, url_mapping *mp);

  /** Parse referer data for a rule.
   *
   * @param node [in] Value node with the referer definition.
   * @param mp [out] Rule to update.
   * @return Errors, if any.
   */
  swoc::Errata parse_referer(YAML::Node const &node, url_mapping *mp);

  /** Parse and apply options for a rule.
   *
   * @param erratum [out] Error accumulator.
   * @param node [in] Value node with the option data.
   * @param options [out] Option flags to update.
   * @return @a erratum
   */
  swoc::Errata &apply_rule_option(swoc::Errata &erratum, YAML::Node const &node, RuleOptions &options);
};
