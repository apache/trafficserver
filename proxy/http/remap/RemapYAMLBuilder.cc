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

#include <array>
#include <tuple>

#include "RemapYAMLBuilder.h"
#include "UrlMapping.h"
#include "UrlRewrite.h"

using swoc::BufferWriter;
using swoc::TextView;
using std::string_view;

namespace YAML
{
// This enables efficient conversions between TextView and a YAML node.

template <> struct convert<swoc::TextView> {
  static Node
  encode(swoc::TextView const &tv)
  {
    Node zret;
    zret = std::string(tv.data(), tv.size());
    return zret;
  }
  static bool
  decode(const Node &node, swoc::TextView &tv)
  {
    if (!node.IsScalar()) {
      return false;
    }
    tv.assign(node.Scalar());
    return true;
  }
};

} // namespace YAML

namespace swoc
{
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, YAML::Mark const &mark)
{
  return w.print("Line {}", mark.line);
}

} // namespace swoc

namespace
{
// YAML parsing helpers.

/** Parse a node for an original URL to rewrite.
 *
 * @param erratum [out] Error reporting.
 * @param parent [in] Base node.
 * @param tag [in] Tag to look for in @a parent for the URL text.
 * @param url [out] A URL object to update from the text.
 * @param opts [out] Options set by the value.
 */
void
yaml_parse_target_url(swoc::Errata &erratum, YAML::Node const &parent, URL &url, RemapYAMLBuilder::URLOptions &opts)
{
  static constexpr swoc::TextView FROM_URL_KEY{"target"};

  if (parent[FROM_URL_KEY]) {
    auto node{parent[FROM_URL_KEY]};
    if (!node.IsScalar()) {
      erratum.error("'{}' FROM_URL_KEY is not a text value at {}", FROM_URL_KEY, node.Mark());
    } else {
      swoc::TextView text{node.Scalar()};
      url.create(nullptr);
      if (PARSE_RESULT_DONE != url.parse_no_path_component_breakdown(text.data(), text.size())) {
        erratum.error("Malformed URL '{}' in '{}' FROM_URL_KEY at {}", text, FROM_URL_KEY, node.Mark());
      }
      if (node.Tag() == "!rx") {
        opts[static_cast<unsigned>(RemapYAMLBuilder::URLOpt::REGEX)] = true;
      }
    }
  }
}

/** Parse a node for a URL as the new URL in a rewriting.
 *
 * @param erratum [out] Error reporting.
 * @param parent [in] Base node.
 * @param tag [in] Tag to look for in @a parent for the URL text.
 * @param url [out] A URL object to update from the text.
 * @param opts [out] Options set by the value.
 */
void
yaml_parse_replacement_url(swoc::Errata &erratum, YAML::Node const &parent, URL &url, RemapYAMLBuilder::URLOptions &opts)
{
  static constexpr swoc::TextView TO_URL_KEY{"replacement"};

  if (parent[TO_URL_KEY]) {
    auto node{parent[TO_URL_KEY]};
    if (!node.IsScalar()) {
      erratum.error("'{}' TO_URL_KEY is not a text value at {}", TO_URL_KEY, node.Mark());
    } else {
      swoc::TextView text{node.Scalar()};
      url.create(nullptr);
      if (PARSE_RESULT_DONE != url.parse_no_path_component_breakdown(text.data(), text.size())) {
        erratum.error("Malformed URL '{}' in '{}' TO_URL_KEY at {}", text, TO_URL_KEY, node.Mark());
      }
      if (node.Tag() == "!rx") {
        erratum.error("'{}' URL at {} is not allowed to be a regular expression", TO_URL_KEY, node.Mark());
      }
    }
  }
}

/** Parse an unsigned value.
 *
 * @param [out] erratum Error reporting.
 * @param parent Node which contains the value node.
 * @param key Member key for the value node.
 * @param n [out] Instance to update with value.
 */
void
yaml_parse_unsigned(swoc::Errata &erratum, YAML::Node const &parent, swoc::TextView key, unsigned &n)
{
  if (parent[key]) {
    auto node{parent[key]};
    if (!node.IsScalar()) {
      erratum.error("'{}' key is not a number at {}", key, node.Mark());
    } else {
      swoc::TextView text{swoc::TextView{node.Scalar()}.trim_if(&isspace)};
      if (!text.empty()) {
        swoc::TextView parsed;
        auto x = svtou(text, &parsed);
        if (parsed.size() == text.size()) {
          n = x;
        } else {
          erratum.error("'{}' key is not a number at {}", text, key, node.Mark());
        }
      } else {
        erratum.error("'{}' key is not a number at {}", text, key, node.Mark());
      }
    }
  }
}

} // namespace

swoc::Errata
RemapYAMLBuilder::parse_filter_definitions(YAML::Node const &filters)
{
  swoc::Errata zret;

  if (filters.IsMap()) {
  } else if (filters.IsSequence()) {
  } else {
    zret.error("Filters [{} {}] must be a filter definition or an array of filter definitions", FILTER_DEFINITIONS_TAG,
               filters.Mark());
  }
  return zret;
}

swoc::Errata &
RemapYAMLBuilder::apply_rule_option(swoc::Errata &erratum, YAML::Node const &node, RuleOptions &options)
{
  static const std::array<swoc::TextView, RuleOptions{}.size()> OPTIONS{{{"reverse"}, {"proxy_port"}}};

  bool unknown_tag_p = false;

  if (node.IsScalar()) {
    swoc::TextView opt{node.as<swoc::TextView>()};
    auto spot =
      std::find_if(OPTIONS.begin(), OPTIONS.end(), [=](swoc::TextView const &tag) -> bool { return strcasecmp(tag, opt); });
    if (spot == OPTIONS.end()) {
      erratum.error("Value '{}' for rule option at {} is not a valid value.", opt, node.Mark());
      unknown_tag_p = true;
    } else {
      options[spot - OPTIONS.begin()] = true;
    }
  } else {
    erratum.error("Rule option at {} is not a string", node.Mark());
  }

  if (unknown_tag_p) {
    swoc::LocalBufferWriter<256> w;
    w.write("Rule options must be one of [");
    auto pos = w.extent();
    for (auto const &tag : OPTIONS) {
      if (w.extent() != pos) {
        w.write(',');
      }
      w.write(tag);
    }
    w.write(']');
    erratum.error(w.view());
  }

  return erratum;
}

swoc::Rv<url_mapping *>
RemapYAMLBuilder::parse_rule_define(YAML::Node const &rule)
{
  static constexpr swoc::TextView RULE_ID_KEY{"id"};
  static constexpr swoc::TextView PROXY_PORT_KEY{"proxy_port"};
  static constexpr swoc::TextView OPTIONS_KEY{"options"};

  swoc::Errata zret;

  RuleOptions rule_options;
  std::unique_ptr<url_mapping> mapping{new url_mapping};

  URLOptions from_options;
  URLOptions to_options;

  yaml_parse_target_url(zret, rule, mapping->fromURL, from_options);
  yaml_parse_replacement_url(zret, rule, mapping->toUrl, to_options);
  yaml_parse_unsigned(zret, rule, RULE_ID_KEY, mapping->map_id);

  if (rule[OPTIONS_KEY]) {
    auto node{rule[OPTIONS_KEY]};
    if (node.IsScalar()) {
      this->apply_rule_option(zret, node, rule_options);
    } else if (node.IsSequence()) {
      for (auto const &n : node) {
        this->apply_rule_option(zret, n, rule_options);
      }
    } else {
      zret.error("'{}' tag at {} must be a string or an array of strings.", OPTIONS_KEY, node.Mark());
    }
  }

  if (zret.is_ok()) {
    UrlRewrite::RegexMapping *regex_mapping = nullptr;
    mapping_type map_type{FORWARD_MAP};

    if (from_options[URLOpt::REGEX]) {
      regex_mapping = new UrlRewrite::RegexMapping;
    }

    _rewriter->InsertMapping(map_type, mapping.release(), regex_mapping, nullptr, from_options[URLOpt::REGEX]);
  }

  zret.error("Failed to parse rule definition at {}", rule.Mark());
  return {nullptr, zret};
};

swoc::Errata
RemapYAMLBuilder::parse_rule_definitions(YAML::Node const &rules)
{
  swoc::Errata zret;

  if (rules.IsMap()) {
    auto rv{this->parse_rule_define(rules)};
    if (!rv.is_ok()) {
      zret.note(rv);
      zret.error("Rules [{} {}] was malformed.", RULE_DEFINITIONS_TAG, rules.Mark());
    }
  } else if (rules.IsSequence()) {
    for (auto const &rule : rules) {
      auto rv{this->parse_rule_define(rule)};
      if (!rv.is_ok()) {
        zret.note(rv);
        zret.error("Rules [{} {}] was malformed.", RULE_DEFINITIONS_TAG, rule.Mark());
      }
    }
  } else {
    zret.error("Rules [{} {}] must be a rule definition or an array of rule definitions", RULE_DEFINITIONS_TAG, rules.Mark());
  }
  return zret;
}

swoc::Errata
RemapYAMLBuilder::parse(UrlRewrite *rewriter, std::string const &content)
{
  swoc::Errata zret;
  YAML::Node top; // Top of the parsed YAML tree.
  try {
    top = YAML::Load(content);
  } catch (YAML::ParserException &ex) {
    return zret.warn("YAML parsing error: {}", ex.what());
  }

  if (!top[ROOT_TAG]) {
    return zret.warn("YAML parsing error: required root tag '{}' not found", ROOT_TAG);
  }
  auto root = top[ROOT_TAG];
  if (!root.IsMap()) {
    return zret.warn("YAML parsing error: required root tag '{}' was not an object", ROOT_TAG);
  }

  RemapYAMLBuilder builder{rewriter};

  YAML::Node filters{root[FILTER_DEFINITIONS_TAG]};
  if (filters) {
    if (!(zret = builder.parse_filter_definitions(filters)).is_ok()) {
      zret.error("YAML parsing error for '{}' tag", FILTER_DEFINITIONS_TAG);
      return zret;
    }
  }
  YAML::Node rules{root[RULE_DEFINITIONS_TAG]};
  if (rules) {
    if (!(zret = builder.parse_rule_definitions(rules)).is_ok()) {
      zret.error("YAML parsing error for '{}' tag", RULE_DEFINITIONS_TAG);
      return zret;
    }
  }
  return zret;
}
