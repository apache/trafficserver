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

#include "prometheus_rules.h"

#include <algorithm>
#include <initializer_list>

#include <yaml-cpp/yaml.h>

namespace
{
struct ConfigError {
  std::string message;
};

[[noreturn]] void
fail(const YAML::Node &node, std::string_view message)
{
  std::string text;

  if (auto const mark = node.Mark(); !mark.is_null()) {
    text = "line " + std::to_string(mark.line + 1) + ": ";
  }
  text += message;
  throw ConfigError{std::move(text)};
}

// A missing node, or a key without a value.
bool
absent(const YAML::Node &node)
{
  return !node.IsDefined() || node.IsNull();
}

void
check_map(const YAML::Node &node, std::string_view where, std::initializer_list<std::string_view> keys)
{
  if (absent(node)) {
    return;
  }
  if (!node.IsMap()) {
    fail(node, std::string{where} + " must be a map");
  }
  for (auto const &item : node) {
    auto const key = item.first.as<std::string>();

    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
      fail(item.first, "unknown key " + std::string{where} + "." + key);
    }
  }
}

// A map whose keys the caller checks.
void
check_free_map(const YAML::Node &node, std::string_view where)
{
  if (!absent(node) && !node.IsMap()) {
    fail(node, std::string{where} + " must be a map");
  }
}

void
check_sequence(const YAML::Node &node, std::string_view where)
{
  if (!absent(node) && !node.IsSequence()) {
    fail(node, std::string{where} + " must be a list");
  }
}

std::string
scalar(const YAML::Node &node, std::string_view where)
{
  if (!node.IsScalar()) {
    fail(node, std::string{where} + " must be a string");
  }
  return node.Scalar();
}

Regex
compile(const YAML::Node &node, std::string_view where)
{
  std::string const pattern = scalar(node, where);
  Regex             regex;
  std::string       error;
  int               offset = 0;

  if (!regex.compile(pattern, error, offset)) {
    fail(node, std::string{where} + ": cannot compile '" + pattern + "' at offset " + std::to_string(offset) + ": " + error);
  }
  return regex;
}

bool
is_name_start(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool
is_name_char(char c)
{
  return is_name_start(c) || (c >= '0' && c <= '9');
}

bool
valid_label_name(std::string_view name)
{
  return !name.empty() && !name.starts_with("__") && is_name_start(name[0]) && std::all_of(name.begin(), name.end(), is_name_char);
}

bool
valid_metric_name(std::string_view name)
{
  auto const valid = [](char c) { return is_name_char(c) || c == ':'; };

  return !name.empty() && (is_name_start(name[0]) || name[0] == ':') && std::all_of(name.begin(), name.end(), valid);
}

std::string
label_name(const YAML::Node &node, std::string_view where)
{
  std::string name = scalar(node, where);

  if (!valid_label_name(name)) {
    fail(node, std::string{where} + ": '" + name + "' is not a valid label name");
  }
  return name;
}

PrometheusType
type_named(const YAML::Node &node, std::string_view where)
{
  std::string const name = scalar(node, where);

  for (auto type : {PrometheusType::COUNTER, PrometheusType::GAUGE, PrometheusType::UNTYPED}) {
    if (name == prometheus_type_name(type)) {
      return type;
    }
  }
  fail(node, std::string{where} + ": '" + name + "' is not counter, gauge or untyped");
}

void
replace_all(std::string &text, std::string_view from, std::string_view to)
{
  for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
    text.replace(at, from.size(), to);
  }
}
} // namespace

std::string
PrometheusRules::load(const YAML::Node &node)
{
  try {
    check_map(node, "prometheus", {"help", "const_labels", "name", "types", "exclude", "strings", "rules", "limits"});

    if (auto const help = node["help"]; help) {
      _help = help.as<bool>();
    }

    check_free_map(node["const_labels"], "prometheus.const_labels");
    for (auto const &item : node["const_labels"]) {
      std::string name = label_name(item.first, "prometheus.const_labels");

      if (std::any_of(_const_labels.begin(), _const_labels.end(), [&name](auto const &label) { return label.first == name; })) {
        fail(item.first, "prometheus.const_labels: '" + name + "' appears twice");
      }
      _const_labels.emplace_back(std::move(name), scalar(item.second, "prometheus.const_labels." + item.first.Scalar()));
    }
    std::sort(_const_labels.begin(), _const_labels.end());

    auto const has_const_label = [this](std::string_view name) {
      return std::any_of(_const_labels.begin(), _const_labels.end(), [name](auto const &label) { return label.first == name; });
    };

    if (auto const strings = node["strings"]; strings) {
      check_map(strings, "prometheus.strings", {"label", "names"});
      if (auto const label = strings["label"]; label) {
        _text_label = label_name(label, "prometheus.strings.label");
      }
      check_sequence(strings["names"], "prometheus.strings.names");
      for (auto const &name : strings["names"]) {
        _text_names.insert(scalar(name, "prometheus.strings.names"));
      }
    }
    if (has_const_label(_text_label)) {
      fail(node["const_labels"][_text_label], "prometheus.strings.label: '" + _text_label + "' is also a constant label");
    }
    _text_only.push_back(_text_label);

    for (int c = 1; c < static_cast<int>(_invalid_chars.size()); ++c) {
      _invalid_chars[c] = !is_name_char(static_cast<char>(c)) && c != ':';
    }
    if (auto const name = node["name"]; name) {
      check_map(name, "prometheus.name", {"replace", "invalid"});
      check_sequence(name["replace"], "prometheus.name.replace");
      for (auto const &item : name["replace"]) {
        check_map(item, "prometheus.name.replace", {"from", "to"});

        std::string from = item["from"] ? scalar(item["from"], "prometheus.name.replace.from") : std::string{};

        if (from.empty()) {
          fail(item, "prometheus.name.replace.from is missing or empty");
        }
        _replace.emplace_back(std::move(from), item["to"] ? scalar(item["to"], "prometheus.name.replace.to") : std::string{});
      }
      if (auto const invalid = name["invalid"]; invalid) {
        Regex const regex = compile(invalid, "prometheus.name.invalid");

        if (regex.exec(std::string_view{""})) {
          fail(invalid, "prometheus.name.invalid must not match an empty string");
        }
        for (int c = 1; c < static_cast<int>(_invalid_chars.size()); ++c) {
          char const ch = static_cast<char>(c);

          _invalid_chars[c] = regex.exec(std::string_view{&ch, 1});
        }
      }
    }

    if (auto const types = node["types"]; types) {
      check_map(types, "prometheus.types", {"source", "rules"});
      if (auto const source = types["source"]; source) {
        std::string const value = scalar(source, "prometheus.types.source");

        if (value == "rules") {
          _types_from_rules = true;
        } else if (value != "record") {
          fail(source, "prometheus.types.source: '" + value + "' is not record or rules");
        }
      }
      if (auto const rules = types["rules"]; rules) {
        if (!_types_from_rules) {
          fail(rules, "prometheus.types.rules needs prometheus.types.source set to rules");
        }
        check_sequence(rules, "prometheus.types.rules");
        for (auto const &rule : rules) {
          check_map(rule, "prometheus.types.rules", {"match", "type"});
          if (!rule["match"] || !rule["type"]) {
            fail(rule, "a rule in prometheus.types.rules needs match and type");
          }
          // Read the type before compiling the regex: GCC before 13 does not destroy a member that aggregate initialization
          // has already built when a later initializer throws.
          PrometheusType const type = type_named(rule["type"], "prometheus.types.rules.type");

          _type_rules.push_back({compile(rule["match"], "prometheus.types.rules.match"), type});
        }
      }
    }

    if (auto const exclude = node["exclude"]; exclude) {
      check_map(exclude, "prometheus.exclude", {"names", "match"});
      check_sequence(exclude["names"], "prometheus.exclude.names");
      for (auto const &name : exclude["names"]) {
        _exclude_names.insert(scalar(name, "prometheus.exclude.names"));
      }
      check_sequence(exclude["match"], "prometheus.exclude.match");
      for (auto const &match : exclude["match"]) {
        _exclude_match.push_back(compile(match, "prometheus.exclude.match"));
      }
    }

    check_sequence(node["rules"], "prometheus.rules");
    for (auto const &item : node["rules"]) {
      check_map(item, "prometheus.rules", {"match", "labels"});
      if (!item["match"]) {
        fail(item, "a rule in prometheus.rules needs match");
      }

      Rule &rule = _rules.emplace_back();

      rule.pattern  = scalar(item["match"], "prometheus.rules.match");
      rule.regex    = compile(item["match"], "prometheus.rules.match");
      rule.captures = rule.regex.get_capture_count();
      // The family name starts with the first capture group.
      if (rule.captures < 1) {
        fail(item["match"], "prometheus.rules.match: '" + rule.pattern + "' has no capture group");
      }
      check_free_map(item["labels"], "prometheus.rules.labels");
      for (auto const &label : item["labels"]) {
        std::string name     = label_name(label.first, "prometheus.rules.labels");
        int const   position = label.second.as<int>();

        if (position < 1 || position > rule.captures) {
          fail(label.second, "prometheus.rules.labels." + name + ": " + std::to_string(position) + " is not a capture group of '" +
                               rule.pattern + "', which has " + std::to_string(rule.captures) +
                               (rule.captures == 1 ? " group" : " groups"));
        }
        if (name == _text_label) {
          fail(label.first, "prometheus.rules.labels: '" + name + "' is also prometheus.strings.label");
        }
        if (has_const_label(name)) {
          fail(label.first, "prometheus.rules.labels: '" + name + "' is also a constant label");
        }
        if (std::find(rule.label_names.begin(), rule.label_names.end(), name) != rule.label_names.end()) {
          fail(label.first, "prometheus.rules.labels: '" + name + "' appears twice in the rule");
        }
        rule.labels.push_back({name, position});
        rule.label_names.push_back(std::move(name));
      }
      rule.label_names_with_text = rule.label_names;
      rule.label_names_with_text.push_back(_text_label);
      _max_captures = std::max(_max_captures, rule.captures);
    }

    if (auto const limits = node["limits"]; limits) {
      check_map(limits, "prometheus.limits", {"max_series"});
      if (auto const max_series = limits["max_series"]; max_series) {
        auto const value = max_series.as<int64_t>();

        if (value < 0) {
          fail(max_series, "prometheus.limits.max_series must not be negative");
        }
        _max_series = static_cast<uint64_t>(value);
      }
    }
  } catch (const ConfigError &e) {
    return e.message;
  } catch (const YAML::Exception &e) {
    return e.what();
  }
  return {};
}

bool
PrometheusRules::excluded(std::string_view name) const
{
  if (_exclude_names.count(std::string{name}) > 0) {
    return true;
  }
  return std::any_of(_exclude_match.begin(), _exclude_match.end(), [name](const Regex &regex) { return regex.exec(name); });
}

std::string
PrometheusRules::sanitize(std::string name) const
{
  for (auto const &[from, to] : _replace) {
    replace_all(name, from, to);
  }
  for (char &c : name) {
    if (_invalid_chars[static_cast<unsigned char>(c)]) {
      c = '_';
    }
  }
  return name;
}

PrometheusType
PrometheusRules::type_of(std::string_view name, TSRecordDataType data_type) const
{
  if (_types_from_rules) {
    for (auto const &rule : _type_rules) {
      if (rule.regex.exec(name)) {
        return rule.type;
      }
    }
  }
  return data_type == TS_RECORDDATATYPE_COUNTER ? PrometheusType::COUNTER : PrometheusType::GAUGE;
}

PrometheusRuleMatch
PrometheusRules::translate(std::string_view name, TSRecordDataType data_type) const
{
  PrometheusRuleMatch match;

  if ((data_type != TS_RECORDDATATYPE_COUNTER && data_type != TS_RECORDDATATYPE_INT && data_type != TS_RECORDDATATYPE_FLOAT &&
       data_type != TS_RECORDDATATYPE_STRING) ||
      excluded(name)) {
    return match;
  }

  match.text_label  = data_type == TS_RECORDDATATYPE_STRING && _text_names.count(std::string{name}) > 0;
  match.label_names = match.text_label ? &_text_only : &_no_labels;
  match.rule_index  = static_cast<uint32_t>(_rules.size());

  // The type rules match the name without its label values, so that a label value cannot change the type of its family.
  std::string  stripped{name};
  std::string  family{name};
  RegexMatches groups(_max_captures + 1);

  for (size_t i = 0; i < _rules.size(); ++i) {
    Rule const &rule = _rules[i];

    if (rule.regex.exec(name, groups) <= 0) {
      continue;
    }

    std::vector<std::string_view> parts;

    for (int g = 0; g <= rule.captures; ++g) {
      parts.push_back(groups[g]);
    }
    for (auto const &label : rule.labels) {
      std::string_view const value = parts[label.position];

      match.label_values.emplace_back(value);
      if (auto const at = stripped.find(value); !value.empty() && at != std::string::npos) {
        stripped.erase(at, value.size());
      }
    }
    for (auto const &label : rule.labels) {
      parts[label.position] = {};
    }
    // The family is the first group, then each later group that is neither a label nor empty, with a '_' before each.  A
    // label in the first group leaves a '_' at the start of the family.
    family = parts[1];
    for (int g = 2; g <= rule.captures; ++g) {
      if (!parts[g].empty()) {
        family.append("_").append(parts[g]);
      }
    }
    match.rule_index  = static_cast<uint32_t>(i);
    match.label_names = match.text_label ? &rule.label_names_with_text : &rule.label_names;
    break;
  }

  match.family = sanitize(std::move(family));
  if (!valid_metric_name(match.family)) {
    match.result = PrometheusRuleMatch::Result::INVALID_NAME;
    return match;
  }
  match.type   = type_of(stripped, data_type);
  match.result = PrometheusRuleMatch::Result::SAMPLE;
  return match;
}
