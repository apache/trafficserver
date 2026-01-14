/** @file

  SSL Multi-Certificate configuration parsing and marshalling implementation.

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

#include "config/ssl_multicert.h"

#include <cerrno>
#include <cctype>
#include <exception>
#include <set>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include "swoc/swoc_file.h"
#include "swoc/TextView.h"
#include "tsutil/ts_diag_levels.h"

namespace
{

constexpr swoc::Errata::Severity ERRATA_NOTE_SEV{static_cast<swoc::Errata::severity_type>(DL_Note)};
constexpr swoc::Errata::Severity ERRATA_WARN_SEV{static_cast<swoc::Errata::severity_type>(DL_Warning)};
constexpr swoc::Errata::Severity ERRATA_ERROR_SEV{static_cast<swoc::Errata::severity_type>(DL_Error)};

// YAML key names.
constexpr char KEY_SSL_CERT_NAME[]      = "ssl_cert_name";
constexpr char KEY_DEST_IP[]            = "dest_ip";
constexpr char KEY_SSL_KEY_NAME[]       = "ssl_key_name";
constexpr char KEY_SSL_CA_NAME[]        = "ssl_ca_name";
constexpr char KEY_SSL_OCSP_NAME[]      = "ssl_ocsp_name";
constexpr char KEY_SSL_KEY_DIALOG[]     = "ssl_key_dialog";
constexpr char KEY_DEST_FQDN[]          = "dest_fqdn";
constexpr char KEY_SSL_TICKET_ENABLED[] = "ssl_ticket_enabled";
constexpr char KEY_SSL_TICKET_NUMBER[]  = "ssl_ticket_number";
constexpr char KEY_ACTION[]             = "action";
constexpr char KEY_SSL_MULTICERT[]      = "ssl_multicert";

std::set<std::string> const valid_keys = {
  KEY_SSL_CERT_NAME,  KEY_DEST_IP,   KEY_SSL_KEY_NAME,       KEY_SSL_CA_NAME,       KEY_SSL_OCSP_NAME,
  KEY_SSL_KEY_DIALOG, KEY_DEST_FQDN, KEY_SSL_TICKET_ENABLED, KEY_SSL_TICKET_NUMBER, KEY_ACTION,
};

/**
 * Parse a line in legacy key=value format, handling quoted values.
 *
 * Tokenizes on whitespace but respects quoted strings. Each token should be
 * key=value format.
 */
std::vector<std::pair<std::string, std::string>>
parse_legacy_line(swoc::TextView line)
{
  std::vector<std::pair<std::string, std::string>> result;

  while (!line.ltrim_if(isspace).empty()) {
    swoc::TextView key = line.split_prefix_at('=');
    if (key.empty()) {
      // No '=' found, skip this malformed token by consuming to next whitespace.
      line.take_prefix_if(isspace);
      continue;
    }
    key.trim_if(isspace);

    swoc::TextView value;
    if (!line.empty() && (line.front() == '"' || line.front() == '\'')) {
      // Quoted value: extract until closing quote.
      char const quote = line.front();
      line.remove_prefix(1);
      value = line.take_prefix_at(quote);
    } else {
      // Unquoted value: extract until whitespace.
      value = line.take_prefix_if(isspace);
    }
    value.trim_if(isspace);

    if (!key.empty()) {
      result.emplace_back(std::string{key}, std::string{value});
    }
  }

  return result;
}

/// Escape a string for YAML output if needed.
std::string
yaml_escape(std::string const &value)
{
  if (value.empty()) {
    return "\"\"";
  }

  bool needs_quotes = false;
  if (value[0] == '*' || value[0] == '!' || value[0] == '&' || value[0] == '{' || value[0] == '}' || value[0] == '[' ||
      value[0] == ']' || value[0] == ',' || value[0] == '#' || value[0] == '?' || value[0] == '-' || value[0] == ':' ||
      value[0] == '>' || value[0] == '|' || value[0] == '@' || value[0] == '`' || value[0] == '"' || value[0] == '\'') {
    needs_quotes = true;
  } else if (value.find(':') != std::string::npos || value.find('#') != std::string::npos) {
    needs_quotes = true;
  } else if (value == "true" || value == "false" || value == "yes" || value == "no" || value == "null" || value == "True" ||
             value == "False" || value == "Yes" || value == "No" || value == "Null") {
    needs_quotes = true;
  }

  if (needs_quotes) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped += '"';
    for (char c : value) {
      if (c == '\\') {
        escaped += "\\\\";
      } else if (c == '"') {
        escaped += "\\\"";
      } else {
        escaped += c;
      }
    }
    escaped += '"';
    return escaped;
  }
  return value;
}

} // namespace

namespace YAML
{
template <> struct convert<config::SSLMultiCertEntry> {
  static bool
  decode(Node const &node, config::SSLMultiCertEntry &entry)
  {
    if (node[KEY_SSL_CERT_NAME]) {
      entry.ssl_cert_name = node[KEY_SSL_CERT_NAME].as<std::string>();
    }

    if (node[KEY_DEST_IP]) {
      entry.dest_ip = node[KEY_DEST_IP].as<std::string>();
    }

    if (node[KEY_SSL_KEY_NAME]) {
      entry.ssl_key_name = node[KEY_SSL_KEY_NAME].as<std::string>();
    }

    if (node[KEY_SSL_CA_NAME]) {
      entry.ssl_ca_name = node[KEY_SSL_CA_NAME].as<std::string>();
    }

    if (node[KEY_SSL_OCSP_NAME]) {
      entry.ssl_ocsp_name = node[KEY_SSL_OCSP_NAME].as<std::string>();
    }

    if (node[KEY_SSL_KEY_DIALOG]) {
      entry.ssl_key_dialog = node[KEY_SSL_KEY_DIALOG].as<std::string>();
    }

    if (node[KEY_DEST_FQDN]) {
      entry.dest_fqdn = node[KEY_DEST_FQDN].as<std::string>();
    }

    if (node[KEY_SSL_TICKET_ENABLED]) {
      entry.ssl_ticket_enabled = node[KEY_SSL_TICKET_ENABLED].as<int>();
    }

    if (node[KEY_SSL_TICKET_NUMBER]) {
      entry.ssl_ticket_number = node[KEY_SSL_TICKET_NUMBER].as<int>();
    }

    if (node[KEY_ACTION]) {
      entry.action = node[KEY_ACTION].as<std::string>();
    }

    return true;
  }
};
} // namespace YAML

namespace config
{

ConfigResult<SSLMultiCertConfig>
SSLMultiCertParser::parse(std::string const &filename)
{
  std::error_code ec;
  std::string     content = swoc::file::load(filename, ec);
  if (ec) {
    // Missing ssl_multicert.* is an acceptable runtime state.
    if (ec.value() == ENOENT) {
      return {{}, swoc::Errata(ERRATA_WARN_SEV, "Cannot open SSL certificate configuration \"{}\" - {}", filename, ec)};
    }
    return {{}, swoc::Errata(ERRATA_ERROR_SEV, "Failed to read SSL certificate configuration from \"{}\" - {}", filename, ec)};
  }

  if (content.empty()) {
    return {{}, {}};
  }

  Format const format = detect_format(content, filename);
  if (format == Format::YAML) {
    return parse_yaml(content);
  }
  return parse_legacy(content);
}

SSLMultiCertParser::Format
SSLMultiCertParser::detect_format(std::string_view content, std::string const &filename)
{
  swoc::TextView const fn{filename};

  // Check file extension first.
  if (fn.ends_with(".yaml") || fn.ends_with(".yml")) {
    return Format::YAML;
  }
  if (fn.ends_with(".config")) {
    return Format::Legacy;
  }

  // Fall back to content inspection.
  if (content.find("ssl_multicert:") != std::string_view::npos) {
    return Format::YAML;
  }

  // Legacy format uses key=value.
  if (content.find('=') != std::string_view::npos) {
    return Format::Legacy;
  }

  // Default to YAML as that's the preferred format.
  return Format::YAML;
}

ConfigResult<SSLMultiCertConfig>
SSLMultiCertParser::parse_yaml(std::string_view content)
{
  SSLMultiCertConfig    result;
  swoc::Errata          errata;
  std::set<std::string> unknown_keys;

  try {
    YAML::Node config = YAML::Load(std::string(content));
    if (config.IsNull()) {
      return {result, std::move(errata)};
    }

    if (!config[KEY_SSL_MULTICERT]) {
      return {result, swoc::Errata("expected a toplevel 'ssl_multicert' node")};
    }

    YAML::Node entries = config[KEY_SSL_MULTICERT];
    if (!entries.IsSequence()) {
      return {result, swoc::Errata("expected 'ssl_multicert' to be a sequence")};
    }

    for (auto it = entries.begin(); it != entries.end(); ++it) {
      YAML::Node entry_node = *it;
      auto const mark       = entry_node.Mark();
      if (!entry_node.IsMap()) {
        return {result, swoc::Errata(ERRATA_ERROR_SEV, "Expected ssl_multicert entries to be maps at line {}, column {}", mark.line,
                                     mark.column)};
      }

      for (auto const &field : entry_node) {
        std::string key = field.first.as<std::string>();
        if (valid_keys.find(key) == valid_keys.end() && unknown_keys.insert(key).second) {
          errata.note(ERRATA_NOTE_SEV, "Ignoring unknown ssl_multicert key '{}' at line {}, column {}", key, mark.line,
                      mark.column);
        }
      }

      result.push_back(entry_node.as<SSLMultiCertEntry>());
    }
  } catch (std::exception const &ex) {
    return {result, swoc::Errata("YAML parse error: {}", ex.what())};
  }

  return {result, std::move(errata)};
}

ConfigResult<SSLMultiCertConfig>
SSLMultiCertParser::parse_legacy(std::string_view content)
{
  SSLMultiCertConfig    result;
  swoc::Errata          errata;
  std::set<std::string> unknown_keys;
  swoc::TextView        src{content};

  while (!src.empty()) {
    swoc::TextView line = src.take_prefix_at('\n');
    line.trim_if(isspace);

    // Skip empty lines and comments.
    if (line.empty() || line.front() == '#') {
      continue;
    }

    auto const kv_pairs = parse_legacy_line(line);
    if (kv_pairs.empty()) {
      continue;
    }

    SSLMultiCertEntry entry;

    for (auto const &[key, value] : kv_pairs) {
      if (key == KEY_SSL_CERT_NAME) {
        entry.ssl_cert_name = value;
      } else if (key == KEY_DEST_IP) {
        entry.dest_ip = value;
      } else if (key == KEY_SSL_KEY_NAME) {
        entry.ssl_key_name = value;
      } else if (key == KEY_SSL_CA_NAME) {
        entry.ssl_ca_name = value;
      } else if (key == KEY_SSL_OCSP_NAME) {
        entry.ssl_ocsp_name = value;
      } else if (key == KEY_SSL_KEY_DIALOG) {
        entry.ssl_key_dialog = value;
      } else if (key == KEY_DEST_FQDN) {
        entry.dest_fqdn = value;
      } else if (key == KEY_ACTION) {
        entry.action = value;
      } else if (key == KEY_SSL_TICKET_ENABLED) {
        entry.ssl_ticket_enabled = swoc::svtoi(value);
      } else if (key == KEY_SSL_TICKET_NUMBER) {
        entry.ssl_ticket_number = swoc::svtoi(value);
      } else if (unknown_keys.insert(key).second) {
        errata.note(ERRATA_NOTE_SEV, "Ignoring unknown ssl_multicert key '{}' in legacy format", key);
      }
    }

    result.push_back(std::move(entry));
  }

  return {result, std::move(errata)};
}

std::string
SSLMultiCertMarshaller::to_yaml(SSLMultiCertConfig const &config)
{
  std::ostringstream out;
  out << "ssl_multicert:\n";

  for (auto const &entry : config) {
    bool first = true;

    auto write_field = [&](char const *key, std::string const &value) {
      if (value.empty()) {
        return;
      }
      out << (first ? "  - " : "    ") << key << ": " << yaml_escape(value) << "\n";
      first = false;
    };

    auto write_int_field = [&](char const *key, std::optional<int> const &value) {
      if (!value.has_value()) {
        return;
      }
      out << (first ? "  - " : "    ") << key << ": " << value.value() << "\n";
      first = false;
    };

    write_field(KEY_SSL_CERT_NAME, entry.ssl_cert_name);
    write_field(KEY_DEST_IP, entry.dest_ip);
    write_field(KEY_SSL_KEY_NAME, entry.ssl_key_name);
    write_field(KEY_SSL_CA_NAME, entry.ssl_ca_name);
    write_field(KEY_SSL_OCSP_NAME, entry.ssl_ocsp_name);
    write_field(KEY_SSL_KEY_DIALOG, entry.ssl_key_dialog);
    write_field(KEY_DEST_FQDN, entry.dest_fqdn);
    write_field(KEY_ACTION, entry.action);
    write_int_field(KEY_SSL_TICKET_ENABLED, entry.ssl_ticket_enabled);
    write_int_field(KEY_SSL_TICKET_NUMBER, entry.ssl_ticket_number);
  }

  return out.str();
}

std::string
SSLMultiCertMarshaller::to_json(SSLMultiCertConfig const &config)
{
  YAML::Emitter json;
  json << YAML::DoubleQuoted << YAML::Flow;
  json << YAML::BeginMap;
  json << YAML::Key << KEY_SSL_MULTICERT << YAML::Value << YAML::BeginSeq;

  for (auto const &entry : config) {
    json << YAML::BeginMap;

    auto write_field = [&](char const *key, std::string const &value) {
      if (!value.empty()) {
        json << YAML::Key << key << YAML::Value << value;
      }
    };

    auto write_int_field = [&](char const *key, std::optional<int> const &value) {
      if (value.has_value()) {
        json << YAML::Key << key << YAML::Value << value.value();
      }
    };

    write_field(KEY_SSL_CERT_NAME, entry.ssl_cert_name);
    write_field(KEY_DEST_IP, entry.dest_ip);
    write_field(KEY_SSL_KEY_NAME, entry.ssl_key_name);
    write_field(KEY_SSL_CA_NAME, entry.ssl_ca_name);
    write_field(KEY_SSL_OCSP_NAME, entry.ssl_ocsp_name);
    write_field(KEY_SSL_KEY_DIALOG, entry.ssl_key_dialog);
    write_field(KEY_DEST_FQDN, entry.dest_fqdn);
    write_field(KEY_ACTION, entry.action);
    write_int_field(KEY_SSL_TICKET_ENABLED, entry.ssl_ticket_enabled);
    write_int_field(KEY_SSL_TICKET_NUMBER, entry.ssl_ticket_number);

    json << YAML::EndMap;
  }

  json << YAML::EndSeq << YAML::EndMap;
  return json.c_str();
}

} // namespace config
