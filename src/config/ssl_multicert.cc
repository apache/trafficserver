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

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <set>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace
{

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

/// Trim whitespace from both ends of a string.
std::string
trim(std::string_view s)
{
  auto const start = s.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return {};
  }
  auto const end = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(start, end - start + 1));
}

/// Check if a string ends with a suffix.
bool
ends_with(std::string const &str, std::string_view suffix)
{
  if (suffix.size() > str.size()) {
    return false;
  }
  return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/**
 * Parse a line in legacy key=value format, handling quoted values.
 *
 * Tokenizes on whitespace but respects quoted strings. Each token should be
 * key=value format.
 */
std::vector<std::pair<std::string, std::string>>
parse_legacy_line(std::string_view line)
{
  std::vector<std::pair<std::string, std::string>> result;
  std::string                                      current_token;
  bool                                             in_quotes  = false;
  char                                             quote_char = 0;

  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];

    if (in_quotes) {
      if (c == quote_char) {
        in_quotes = false;
      } else {
        current_token += c;
      }
    } else if (c == '"' || c == '\'') {
      in_quotes  = true;
      quote_char = c;
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      if (!current_token.empty()) {
        auto const eq_pos = current_token.find('=');
        if (eq_pos != std::string::npos) {
          std::string key   = current_token.substr(0, eq_pos);
          std::string value = current_token.substr(eq_pos + 1);
          result.emplace_back(trim(key), trim(value));
        }
        current_token.clear();
      }
    } else {
      current_token += c;
    }
  }

  // Handle last token.
  if (!current_token.empty()) {
    auto const eq_pos = current_token.find('=');
    if (eq_pos != std::string::npos) {
      std::string key   = current_token.substr(0, eq_pos);
      std::string value = current_token.substr(eq_pos + 1);
      result.emplace_back(trim(key), trim(value));
    }
  }

  return result;
}

/// Escape a string for JSON output.
std::string
json_escape(std::string const &s)
{
  std::string result;
  result.reserve(s.size() + 2);
  result += '"';
  for (char c : s) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result += c;
    }
  }
  result += '"';
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
    for (auto const &elem : node) {
      std::string key = elem.first.as<std::string>();
      if (valid_keys.find(key) == valid_keys.end()) {
        // Unknown key - we could warn here, but for now we skip silently.
      }
    }

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
  std::ifstream file(filename);
  if (!file) {
    return {{}, swoc::Errata("failed to open file: {}", filename)};
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return parse_string(buffer.str(), filename);
}

ConfigResult<SSLMultiCertConfig>
SSLMultiCertParser::parse_string(std::string_view content, std::string const &filename)
{
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
  // Check file extension first.
  if (ends_with(filename, ".yaml") || ends_with(filename, ".yml")) {
    return Format::YAML;
  }
  if (ends_with(filename, ".config")) {
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
  SSLMultiCertConfig result;

  try {
    YAML::Node config = YAML::Load(std::string(content));
    if (config.IsNull()) {
      return {result, {}};
    }

    if (!config[KEY_SSL_MULTICERT]) {
      return {result, swoc::Errata("expected a toplevel 'ssl_multicert' node")};
    }

    YAML::Node entries = config[KEY_SSL_MULTICERT];
    if (!entries.IsSequence()) {
      return {result, swoc::Errata("expected 'ssl_multicert' to be a sequence")};
    }

    for (auto it = entries.begin(); it != entries.end(); ++it) {
      result.push_back(it->as<SSLMultiCertEntry>());
    }
  } catch (std::exception const &ex) {
    return {result, swoc::Errata("YAML parse error: {}", ex.what())};
  }

  return {result, {}};
}

ConfigResult<SSLMultiCertConfig>
SSLMultiCertParser::parse_legacy(std::string_view content)
{
  SSLMultiCertConfig result;
  std::istringstream stream{std::string(content)};
  std::string        line;
  int                line_number = 0;

  while (std::getline(stream, line)) {
    ++line_number;
    std::string trimmed = trim(line);

    // Skip empty lines and comments.
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }

    auto const pairs = parse_legacy_line(trimmed);
    if (pairs.empty()) {
      continue;
    }

    SSLMultiCertEntry entry;

    for (auto const &[key, value] : pairs) {
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
        try {
          entry.ssl_ticket_enabled = std::stoi(value);
        } catch (...) {
          // Ignore conversion errors.
        }
      } else if (key == KEY_SSL_TICKET_NUMBER) {
        try {
          entry.ssl_ticket_number = std::stoi(value);
        } catch (...) {
          // Ignore conversion errors.
        }
      }
    }

    result.push_back(std::move(entry));
  }

  return {result, {}};
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
  std::ostringstream out;
  out << "{\n  \"ssl_multicert\": [\n";

  bool first_entry = true;
  for (auto const &entry : config) {
    if (!first_entry) {
      out << ",\n";
    }
    first_entry = false;

    out << "    {";
    bool first_field = true;

    auto write_field = [&](char const *key, std::string const &value) {
      if (value.empty()) {
        return;
      }
      if (!first_field) {
        out << ", ";
      }
      first_field = false;
      out << json_escape(key) << ": " << json_escape(value);
    };

    auto write_int_field = [&](char const *key, std::optional<int> const &value) {
      if (!value.has_value()) {
        return;
      }
      if (!first_field) {
        out << ", ";
      }
      first_field = false;
      out << json_escape(key) << ": " << value.value();
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

    out << "}";
  }

  out << "\n  ]\n}\n";
  return out.str();
}

} // namespace config
