/** @file

  Rules that name records in the Prometheus text format: exclusions, labels from regular expressions, and types.

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

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <ts/apidefs.h>

#include "prometheus_render.h"
#include "tsutil/Regex.h"

namespace YAML
{
class Node;
}

/// The translation of one record name by the rules.
struct PrometheusRuleMatch {
  enum class Result : uint8_t {
    SAMPLE,       ///< The record is a sample of @a family.
    EXCLUDED,     ///< The rules leave the record out.
    INVALID_NAME, ///< @a family is not a valid metric name.
  };

  Result      result = Result::EXCLUDED;
  std::string family;
  /// The label names of the sample, in the order of its rule.  The text label is last when @a text_label is set.
  const std::vector<std::string> *label_names = nullptr;
  /// The values of the rule labels, in the order of @a label_names.  The text label has no value here.
  std::vector<std::string> label_values;
  PrometheusType           type = PrometheusType::UNTYPED;
  /// The index of the rule that matched, or the number of rules when none matched.
  uint32_t rule_index = 0;
  /// Whether the sample is 1, with the string value of the record in the text label.
  bool text_label = false;
};

/** Rules that turn record names into Prometheus families, labels and types.
 *
 * Each regular expression is a search for a match anywhere in the name.
 */
class PrometheusRules
{
public:
  /** Load the rules from the @c prometheus node of a configuration.
   *
   * @return An empty string, or a description of the first error.
   */
  std::string load(const YAML::Node &node);

  /// Translate the name of a record.  This is thread safe.
  PrometheusRuleMatch translate(std::string_view name, TSRecordDataType data_type) const;

  /// Whether to write a HELP line for each family.
  bool
  help() const
  {
    return _help;
  }

  /// The maximum number of series that a render writes, or 0 for no limit.
  uint64_t
  max_series() const
  {
    return _max_series;
  }

  /// The labels of every sample, sorted by name.
  const std::vector<std::pair<std::string, std::string>> &
  const_labels() const
  {
    return _const_labels;
  }

private:
  struct Label {
    std::string name;
    int         position;
  };

  struct Rule {
    std::string              pattern;
    Regex                    regex;
    int                      captures = 0;
    std::vector<Label>       labels;
    std::vector<std::string> label_names;
    std::vector<std::string> label_names_with_text;
  };

  struct TypeRule {
    Regex          regex;
    PrometheusType type;
  };

  std::string    sanitize(std::string name) const;
  PrometheusType type_of(std::string_view name, TSRecordDataType data_type) const;
  bool           excluded(std::string_view name) const;

  bool                                             _help       = false;
  uint64_t                                         _max_series = 0;
  std::vector<std::pair<std::string, std::string>> _const_labels;
  std::vector<std::pair<std::string, std::string>> _replace;
  std::array<bool, 256>                            _invalid_chars{};
  bool                                             _types_from_rules = false;
  std::vector<TypeRule>                            _type_rules;
  std::unordered_set<std::string>                  _exclude_names;
  std::vector<Regex>                               _exclude_match;
  std::string                                      _text_label = "value";
  std::unordered_set<std::string>                  _text_names;
  std::vector<Rule>                                _rules;
  int                                              _max_captures = 0;
  std::vector<std::string>                         _no_labels;
  std::vector<std::string>                         _text_only;
};
