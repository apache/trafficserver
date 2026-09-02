/** @file

  Plugin configuration parsing and marshalling.

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

#include "config/plugin_config.h"

#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace
{

constexpr char                   KEY_PLUGINS[] = "plugins";
constexpr swoc::Errata::Severity INFO_SEVERITY{0};

std::vector<std::string>
tokenize_plugin_line(std::string_view line)
{
  std::vector<std::string> tokens;
  std::size_t              i = 0;

  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
      ++i;
    }
    if (i >= line.size() || line[i] == '#') {
      break;
    }

    if (line[i] == '"') {
      ++i;
      std::size_t start = i;
      while (i < line.size() && line[i] != '"') {
        ++i;
      }
      tokens.emplace_back(line.substr(start, i - start));
      if (i < line.size()) {
        ++i;
      }
    } else {
      std::size_t start = i;
      while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '#') {
        ++i;
      }
      tokens.emplace_back(line.substr(start, i - start));
    }
  }
  return tokens;
}

void
emit_entry(YAML::Emitter &yaml, config::PluginConfigEntry const &entry)
{
  yaml << YAML::BeginMap;
  yaml << YAML::Key << "path" << YAML::Value << entry.path;

  if (!entry.enabled) {
    yaml << YAML::Key << "enabled" << YAML::Value << false;
  }

  if (!entry.args.empty()) {
    yaml << YAML::Key << "params" << YAML::Value << YAML::BeginSeq;
    for (auto const &arg : entry.args) {
      yaml << arg;
    }
    yaml << YAML::EndSeq;
  }

  yaml << YAML::EndMap;
}

} // namespace

namespace config
{

ConfigResult<PluginConfigData>
PluginConfigParser::parse(std::string const &filename)
{
  std::ifstream file(filename);

  if (!file.is_open()) {
    ConfigResult<PluginConfigData> result;
    result.file_not_found = true;
    result.errata.note("unable to open '{}'", filename);
    return result;
  }

  std::ostringstream ss;
  ss << file.rdbuf();
  return parse_legacy(ss.str());
}

ConfigResult<PluginConfigData>
PluginConfigParser::parse_legacy(std::string_view content)
{
  ConfigResult<PluginConfigData> result;
  std::istringstream             stream{std::string{content}};
  std::string                    line;
  int                            line_no = 0;

  while (std::getline(stream, line)) {
    ++line_no;
    std::string_view sv{line};

    std::size_t start = 0;
    while (start < sv.size() && (sv[start] == ' ' || sv[start] == '\t')) {
      ++start;
    }
    sv = sv.substr(start);

    if (sv.empty()) {
      continue;
    }

    bool commented = false;
    if (sv[0] == '#') {
      commented = true;
      sv        = sv.substr(1);
      start     = 0;
      while (start < sv.size() && (sv[start] == ' ' || sv[start] == '\t')) {
        ++start;
      }
      sv = sv.substr(start);
    }

    if (sv.empty()) {
      continue;
    }

    auto tokens = tokenize_plugin_line(sv);
    if (tokens.empty()) {
      continue;
    }

    if (tokens[0].find(".so") == std::string::npos) {
      result.errata.note(INFO_SEVERITY, "skipping line {}: '{}' does not look like a plugin path (no .so)", line_no, tokens[0]);
      continue;
    }

    PluginConfigEntry entry;
    entry.path    = std::move(tokens[0]);
    entry.enabled = !commented;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
      entry.args.emplace_back(std::move(tokens[i]));
    }
    result.value.emplace_back(std::move(entry));
  }

  return result;
}

std::string
PluginConfigMarshaller::to_yaml(PluginConfigData const &config)
{
  YAML::Emitter yaml;

  yaml << YAML::BeginMap;
  yaml << YAML::Key << KEY_PLUGINS << YAML::Value << YAML::BeginSeq;

  for (auto const &entry : config) {
    emit_entry(yaml, entry);
  }

  yaml << YAML::EndSeq << YAML::EndMap;
  return yaml.c_str();
}

} // namespace config
