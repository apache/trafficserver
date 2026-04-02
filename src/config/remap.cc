/** @file

  Shared remap configuration parsing and marshalling implementation.

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

#include "config/remap.h"

#include <cerrno>
#include <cctype>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "swoc/swoc_file.h"
#include "tscore/MatcherUtils.h"
#include "tscore/Tokenizer.h"
#include "tsutil/ts_diag_levels.h"

namespace
{

constexpr swoc::Errata::Severity ERRATA_NOTE_SEV{static_cast<swoc::Errata::severity_type>(DL_Note)};
constexpr swoc::Errata::Severity ERRATA_ERROR_SEV{static_cast<swoc::Errata::severity_type>(DL_Error)};

struct ParsedOption {
  std::string key;
  std::string value;
  bool        has_value = true;
  bool        invert    = false;
};

YAML::Node
make_empty_remap_config()
{
  YAML::Node root{YAML::NodeType::Map};
  root["remap"] = YAML::Node{YAML::NodeType::Sequence};
  return root;
}

YAML::Node
ensure_sequence(YAML::Node node, char const *key)
{
  if (!node[key]) {
    node[key] = YAML::Node{YAML::NodeType::Sequence};
  }
  return node[key];
}

void
append_scalar_or_promote(YAML::Node &node, char const *key, std::string const &value)
{
  if (!node[key]) {
    node[key] = value;
    return;
  }

  if (node[key].IsSequence()) {
    node[key].push_back(value);
    return;
  }

  YAML::Node seq{YAML::NodeType::Sequence};
  seq.push_back(node[key].as<std::string>());
  seq.push_back(value);
  node[key] = seq;
}

std::string
trim_ascii(std::string_view text)
{
  size_t start = 0;
  size_t end   = text.size();

  while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) {
    ++start;
  }
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }

  return std::string{text.substr(start, end - start)};
}

bool
is_define_filter_directive(std::string_view directive)
{
  return directive == ".definefilter" || directive == ".deffilter" || directive == ".defflt";
}

bool
is_delete_filter_directive(std::string_view directive)
{
  return directive == ".deletefilter" || directive == ".delfilter" || directive == ".delflt";
}

bool
is_activate_filter_directive(std::string_view directive)
{
  return directive == ".usefilter" || directive == ".activefilter" || directive == ".activatefilter" || directive == ".useflt";
}

bool
is_deactivate_filter_directive(std::string_view directive)
{
  return directive == ".unusefilter" || directive == ".deactivatefilter" || directive == ".unactivefilter" ||
         directive == ".deuseflt" || directive == ".unuseflt";
}

std::string_view
remap_type_base(std::string_view type)
{
  return type.starts_with("regex_") ? type.substr(6) : type;
}

std::optional<ParsedOption>
parse_option(std::string_view raw)
{
  if (raw == "internal") {
    return ParsedOption{"internal", "", false, false};
  }

  size_t pos = raw.find('=');
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }

  ParsedOption option;
  option.key       = std::string{raw.substr(0, pos)};
  option.value     = std::string{raw.substr(pos + 1)};
  option.has_value = true;

  if ((option.key == "src_ip" || option.key == "src_ip_category" || option.key == "in_ip") && !option.value.empty() &&
      option.value.front() == '~') {
    option.invert = true;
    option.value.erase(option.value.begin());
  }

  return option;
}

swoc::Errata
apply_acl_option(YAML::Node &filter, ParsedOption const &option)
{
  swoc::Errata errata;

  auto require_value = [&](char const *label) -> bool {
    if (option.value.empty()) {
      errata.note(ERRATA_ERROR_SEV, "empty argument in @{}=", label);
      return false;
    }
    return true;
  };

  if (option.key == "method") {
    if (require_value("method")) {
      append_scalar_or_promote(filter, "method", option.value);
    }
    return errata;
  }

  if (option.key == "src_ip") {
    if (require_value("src_ip")) {
      append_scalar_or_promote(filter, option.invert ? "src_ip_invert" : "src_ip", option.value);
    }
    return errata;
  }

  if (option.key == "src_ip_category") {
    if (require_value("src_ip_category")) {
      append_scalar_or_promote(filter, option.invert ? "src_ip_category_invert" : "src_ip_category", option.value);
    }
    return errata;
  }

  if (option.key == "in_ip") {
    if (require_value("in_ip")) {
      append_scalar_or_promote(filter, option.invert ? "in_ip_invert" : "in_ip", option.value);
    }
    return errata;
  }

  if (option.key == "action") {
    if (!require_value("action")) {
      return errata;
    }
    if (filter["action"]) {
      errata.note(ERRATA_ERROR_SEV, "only one @action= is allowed per remap ACL");
    } else {
      filter["action"] = option.value;
    }
    return errata;
  }

  if (option.key == "internal") {
    filter["internal"] = true;
    return errata;
  }

  errata.note(ERRATA_ERROR_SEV, "unsupported ACL option @{}", option.key);
  return errata;
}

swoc::Errata
parse_volume_value(std::string_view raw, YAML::Node target)
{
  swoc::Errata errata;

  if (raw.empty()) {
    errata.note(ERRATA_ERROR_SEV, "empty @volume= directive");
    return errata;
  }

  YAML::Node seq{YAML::NodeType::Sequence};
  size_t     start = 0;

  while (start <= raw.size()) {
    size_t comma = raw.find(',', start);
    auto   token = raw.substr(start, comma == std::string_view::npos ? raw.size() - start : comma - start);

    std::string trimmed = trim_ascii(token);
    if (trimmed.empty()) {
      errata.note(ERRATA_ERROR_SEV, "invalid @volume={} (expected comma-separated numbers 1-255)", std::string{raw});
      return errata;
    }

    try {
      size_t              consumed = 0;
      unsigned long const value    = std::stoul(trimmed, &consumed, 10);
      if (consumed != trimmed.size() || value < 1 || value > 255) {
        errata.note(ERRATA_ERROR_SEV, "invalid @volume={} (expected comma-separated numbers 1-255)", std::string{raw});
        return errata;
      }
      seq.push_back(static_cast<int>(value));
    } catch (std::exception const &) {
      errata.note(ERRATA_ERROR_SEV, "invalid @volume={} (expected comma-separated numbers 1-255)", std::string{raw});
      return errata;
    }

    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
    if (start == raw.size()) {
      errata.note(ERRATA_ERROR_SEV, "invalid @volume={} (trailing comma)", std::string{raw});
      return errata;
    }
  }

  if (seq.size() == 1) {
    target = seq[0].as<int>();
  } else {
    target = seq;
  }

  return errata;
}

swoc::Errata
parse_rule_options(YAML::Node &entry, std::vector<std::string> const &options)
{
  swoc::Errata errata;
  YAML::Node   acl_filter{YAML::NodeType::Map};
  YAML::Node   plugins{YAML::NodeType::Sequence};
  int          current_plugin_idx = -1;

  for (auto const &raw_option : options) {
    auto parsed = parse_option(raw_option);
    if (!parsed.has_value()) {
      errata.note(ERRATA_NOTE_SEV, "ignoring invalid remap option '@{}'", raw_option);
      continue;
    }

    auto const &option = *parsed;

    if (option.key == "plugin") {
      if (option.value.empty()) {
        errata.note(ERRATA_ERROR_SEV, "empty argument in @plugin=");
        continue;
      }
      YAML::Node plugin{YAML::NodeType::Map};
      plugin["name"] = option.value;
      plugins.push_back(plugin);
      current_plugin_idx = static_cast<int>(plugins.size()) - 1;
      continue;
    }

    if (option.key == "pparam") {
      if (option.value.empty()) {
        errata.note(ERRATA_ERROR_SEV, "empty argument in @pparam=");
        continue;
      }
      if (current_plugin_idx < 0) {
        errata.note(ERRATA_NOTE_SEV, "ignoring orphan @pparam={} with no preceding @plugin=", option.value);
        continue;
      }
      ensure_sequence(plugins[current_plugin_idx], "params").push_back(option.value);
      continue;
    }

    if (option.key == "strategy") {
      if (option.value.empty()) {
        errata.note(ERRATA_ERROR_SEV, "empty argument in @strategy=");
      } else {
        entry["strategy"] = option.value;
      }
      continue;
    }

    if (option.key == "mapid") {
      if (option.value.empty()) {
        errata.note(ERRATA_ERROR_SEV, "empty argument in @mapid=");
        continue;
      }
      try {
        size_t              consumed = 0;
        unsigned long const value    = std::stoul(option.value, &consumed, 10);
        if (consumed != option.value.size() || value > std::numeric_limits<unsigned int>::max()) {
          throw std::out_of_range("invalid mapid");
        }
        entry["mapid"] = static_cast<unsigned int>(value);
      } catch (std::exception const &) {
        errata.note(ERRATA_ERROR_SEV, "invalid @mapid={}", option.value);
      }
      continue;
    }

    if (option.key == "volume") {
      auto volume_errata = parse_volume_value(option.value, entry["volume"]);
      errata.note(std::move(volume_errata));
      continue;
    }

    if (option.key == "method" || option.key == "src_ip" || option.key == "src_ip_category" || option.key == "in_ip" ||
        option.key == "action" || option.key == "internal") {
      auto acl_errata = apply_acl_option(acl_filter, option);
      errata.note(std::move(acl_errata));
      continue;
    }

    errata.note(ERRATA_NOTE_SEV, "ignoring invalid remap option '@{}'", raw_option);
  }

  if (acl_filter.size() > 0) {
    entry["acl_filter"] = acl_filter;
  }
  if (plugins.size() > 0) {
    entry["plugins"] = plugins;
  }

  return errata;
}

swoc::Errata
parse_filter_directive(YAML::Node remap_entries, std::string const &directive, std::vector<std::string> const &params,
                       std::vector<std::string> const &options)
{
  swoc::Errata errata;
  YAML::Node   entry{YAML::NodeType::Map};

  if (is_define_filter_directive(directive)) {
    if (params.size() < 2) {
      errata.note(ERRATA_ERROR_SEV, "directive \"{}\" must have name argument", directive);
      return errata;
    }
    if (options.empty()) {
      errata.note(ERRATA_ERROR_SEV, "directive \"{}\" must have filter parameter(s)", directive);
      return errata;
    }

    YAML::Node filter{YAML::NodeType::Map};
    for (auto const &raw_option : options) {
      auto parsed = parse_option(raw_option);
      if (!parsed.has_value()) {
        errata.note(ERRATA_ERROR_SEV, "unsupported ACL option @{}", raw_option);
        continue;
      }
      auto acl_errata = apply_acl_option(filter, *parsed);
      errata.note(std::move(acl_errata));
    }

    if (!errata.is_ok()) {
      return errata;
    }

    entry["define_filter"][params[1]] = filter;
    remap_entries.push_back(entry);
    return errata;
  }

  if (params.size() < 2) {
    errata.note(ERRATA_ERROR_SEV, "directive \"{}\" must have name argument", directive);
    return errata;
  }

  if (is_delete_filter_directive(directive)) {
    entry["delete_filter"] = params[1];
  } else if (is_activate_filter_directive(directive)) {
    entry["activate_filter"] = params[1];
  } else if (is_deactivate_filter_directive(directive)) {
    entry["deactivate_filter"] = params[1];
  } else if (directive == ".include") {
    entry["include"] = params[1];
  } else {
    errata.note(ERRATA_ERROR_SEV, "unknown directive \"{}\"", directive);
    return errata;
  }

  remap_entries.push_back(entry);
  return errata;
}

swoc::Errata
parse_legacy_line(YAML::Node remap_entries, std::vector<std::string> const &params, std::vector<std::string> const &options)
{
  swoc::Errata errata;

  if (params.empty()) {
    return errata;
  }

  if (params[0].starts_with('.')) {
    return parse_filter_directive(remap_entries, params[0], params, options);
  }

  if (params.size() < 3) {
    errata.note(ERRATA_ERROR_SEV, "malformed remap rule");
    return errata;
  }

  YAML::Node entry{YAML::NodeType::Map};
  entry["type"] = params[0];

  std::string from_url = params[1];
  if (from_url.size() >= 2 && from_url.ends_with("//")) {
    from_url.erase(from_url.size() - 2);
    entry["unique"] = true;
  }

  entry["from"]["url"] = from_url;
  entry["to"]["url"]   = params[2];

  if (remap_type_base(params[0]) == "map_with_referer") {
    if (params.size() < 4) {
      errata.note(ERRATA_ERROR_SEV, "map_with_referer requires a redirect URL");
      return errata;
    }
    entry["redirect"]["url"] = params[3];
    if (params.size() > 4) {
      auto regex_seq = ensure_sequence(entry["redirect"], "regex");
      for (size_t i = 4; i < params.size(); ++i) {
        regex_seq.push_back(params[i]);
      }
    }
  } else if (params.size() > 3) {
    entry["tag"] = params[3];
  }

  auto option_errata = parse_rule_options(entry, options);
  errata.note(std::move(option_errata));
  if (!errata.is_ok()) {
    return errata;
  }

  remap_entries.push_back(entry);
  return errata;
}

} // namespace

namespace config
{

ConfigResult<RemapConfig>
RemapParser::parse(std::string const &filename)
{
  ConfigResult<RemapConfig> result;

  std::error_code ec;
  std::string     content = swoc::file::load(filename, ec);
  if (ec) {
    result.value = make_empty_remap_config();
    if (ec.value() == ENOENT) {
      result.file_not_found = true;
      return result;
    }

    result.errata.note(ERRATA_ERROR_SEV, "failed to read remap configuration from \"{}\" - {}", filename, ec.message());
    return result;
  }

  return parse_content(content, filename);
}

ConfigResult<RemapConfig>
RemapParser::parse_content(std::string_view content, std::string const &filename)
{
  if (content.empty()) {
    return {make_empty_remap_config(), {}};
  }

  Format const format = detect_format(content, filename);
  if (format == Format::YAML) {
    return parse_yaml(content);
  }
  return parse_legacy(content);
}

RemapParser::Format
RemapParser::detect_format(std::string_view content, std::string const &filename) const
{
  if (filename.ends_with(".yaml") || filename.ends_with(".yml")) {
    return Format::YAML;
  }
  if (filename.ends_with(".config")) {
    return Format::Legacy;
  }
  if (content.find("remap:") != std::string_view::npos || content.find("acl_filters:") != std::string_view::npos) {
    return Format::YAML;
  }
  return Format::Legacy;
}

ConfigResult<RemapConfig>
RemapParser::parse_yaml(std::string_view content) const
{
  ConfigResult<RemapConfig> result;

  try {
    result.value = YAML::Load(std::string{content});
    if (!result.value || result.value.IsNull()) {
      result.value = make_empty_remap_config();
    } else if (!result.value.IsMap()) {
      result.errata.note(ERRATA_ERROR_SEV, "expected remap YAML configuration to be a map");
    }
  } catch (YAML::Exception const &ex) {
    result.value = make_empty_remap_config();
    result.errata.note(ERRATA_ERROR_SEV, "YAML parsing error: {}", ex.what());
  } catch (std::exception const &ex) {
    result.value = make_empty_remap_config();
    result.errata.note(ERRATA_ERROR_SEV, "exception parsing remap YAML: {}", ex.what());
  }

  return result;
}

ConfigResult<RemapConfig>
RemapParser::parse_legacy(std::string_view content) const
{
  ConfigResult<RemapConfig> result;
  result.value = make_empty_remap_config();

  Tokenizer   white_tok{" \t"};
  std::string buffer{content};
  char       *tok_state = nullptr;
  int         line_no   = 0;

  for (char *line = tokLine(buffer.data(), &tok_state, '\\'); line != nullptr; line = tokLine(nullptr, &tok_state, '\\')) {
    ++line_no;

    while (*line && std::isspace(static_cast<unsigned char>(*line))) {
      ++line;
    }

    if (*line == '\0' || *line == '#') {
      continue;
    }

    size_t size = std::strlen(line);
    while (size > 0 && std::isspace(static_cast<unsigned char>(line[size - 1]))) {
      line[--size] = '\0';
    }

    if (*line == '\0' || *line == '#') {
      continue;
    }

    std::vector<std::string> params;
    std::vector<std::string> options;

    int tok_count = white_tok.Initialize(line, SHARE_TOKS | ALLOW_SPACES);
    for (int idx = 0; idx < tok_count; ++idx) {
      auto *token = const_cast<char *>(white_tok[idx]);
      if (token[0] == '@' && token[1] != '\0') {
        options.emplace_back(token + 1);
      } else {
        params.emplace_back(token);
      }
    }

    auto line_errata = parse_legacy_line(result.value["remap"], params, options);
    if (!line_errata.is_ok()) {
      result.errata.note(ERRATA_ERROR_SEV, "error on line {} - {}", line_no, line_errata.front().text());
      return result;
    }

    result.errata.note(std::move(line_errata));
  }

  return result;
}

std::string
RemapMarshaller::to_yaml(RemapConfig const &config)
{
  YAML::Emitter emitter;
  emitter << config;
  return emitter.c_str();
}

} // namespace config
