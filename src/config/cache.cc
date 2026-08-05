/** @file

  Cache rule configuration parsing and marshalling implementation.

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

#include "config/cache.h"

#include <charconv>
#include <cctype>
#include <cerrno>
#include <set>
#include <string>
#include <system_error>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "swoc/swoc_file.h"
#include "tscore/MatcherUtils.h"
#include "tsutil/ts_diag_levels.h"

namespace
{

constexpr swoc::Errata::Severity ERRATA_WARN_SEV{static_cast<swoc::Errata::severity_type>(DL_Warning)};
constexpr swoc::Errata::Severity ERRATA_ERROR_SEV{static_cast<swoc::Errata::severity_type>(DL_Error)};

constexpr char KEY_CACHE[]                      = "cache";
constexpr char KEY_MATCH[]                      = "match";
constexpr char KEY_ACTION[]                     = "action";
constexpr char KEY_DEST_HOST[]                  = "dest_host";
constexpr char KEY_DEST_DOMAIN[]                = "dest_domain";
constexpr char KEY_DEST_IP[]                    = "dest_ip";
constexpr char KEY_URL_REGEX[]                  = "url_regex";
constexpr char KEY_HOST_REGEX[]                 = "host_regex";
constexpr char KEY_PORT[]                       = "port";
constexpr char KEY_SCHEME[]                     = "scheme";
constexpr char KEY_PREFIX[]                     = "prefix";
constexpr char KEY_SUFFIX[]                     = "suffix";
constexpr char KEY_METHOD[]                     = "method";
constexpr char KEY_TIME[]                       = "time";
constexpr char KEY_SRC_IP[]                     = "src_ip";
constexpr char KEY_INCOMING_PORT[]              = "incoming_port";
constexpr char KEY_TAG[]                        = "tag";
constexpr char KEY_INTERNAL[]                   = "internal";
constexpr char KEY_CACHE_MODE[]                 = "cache";
constexpr char KEY_REVALIDATE[]                 = "revalidate";
constexpr char KEY_PIN_IN_CACHE[]               = "pin_in_cache";
constexpr char KEY_TTL_IN_CACHE[]               = "ttl_in_cache";
constexpr char KEY_IGNORE_NO_CACHE[]            = "ignore_no_cache";
constexpr char KEY_IGNORE_CLIENT_NO_CACHE[]     = "ignore_client_no_cache";
constexpr char KEY_IGNORE_SERVER_NO_CACHE[]     = "ignore_server_no_cache";
constexpr char KEY_CACHE_RESPONSES_TO_COOKIES[] = "cache_responses_to_cookies";

std::set<std::string> const rule_keys{KEY_MATCH, KEY_ACTION};
std::set<std::string> const match_keys{
  KEY_DEST_HOST, KEY_DEST_DOMAIN, KEY_DEST_IP, KEY_URL_REGEX, KEY_HOST_REGEX,    KEY_PORT, KEY_SCHEME,   KEY_PREFIX,
  KEY_SUFFIX,    KEY_METHOD,      KEY_TIME,    KEY_SRC_IP,    KEY_INCOMING_PORT, KEY_TAG,  KEY_INTERNAL,
};
std::set<std::string> const action_keys{
  KEY_CACHE_MODE,
  KEY_REVALIDATE,
  KEY_PIN_IN_CACHE,
  KEY_TTL_IN_CACHE,
  KEY_IGNORE_NO_CACHE,
  KEY_IGNORE_CLIENT_NO_CACHE,
  KEY_IGNORE_SERVER_NO_CACHE,
  KEY_CACHE_RESPONSES_TO_COOKIES,
};

struct LegacyToken {
  std::string key;
  std::string value;
};

bool
has_only_keys(YAML::Node const &node, std::set<std::string> const &valid_keys, swoc::Errata &errata, std::string_view context)
{
  bool                  is_valid = true;
  std::set<std::string> seen_keys;

  for (auto const &item : node) {
    if (!item.first.IsScalar()) {
      errata.note(ERRATA_ERROR_SEV, "{} at line {} has a non-scalar key", context, item.first.Mark().line + 1);
      is_valid = false;
      continue;
    }

    std::string const key{item.first.Scalar()};
    if (!valid_keys.contains(key)) {
      errata.note(ERRATA_ERROR_SEV, "{} at line {} has unknown key '{}'", context, item.first.Mark().line + 1, key);
      is_valid = false;
    } else if (!seen_keys.insert(key).second) {
      errata.note(ERRATA_ERROR_SEV, "{} at line {} repeats key '{}'", context, item.first.Mark().line + 1, key);
      is_valid = false;
    }
  }

  return is_valid;
}

bool
read_scalar(YAML::Node const &node, std::string &value, swoc::Errata &errata, std::string_view key)
{
  if (!node.IsScalar()) {
    errata.note(ERRATA_ERROR_SEV, "'{}' at line {} must be a scalar", key, node.Mark().line + 1);
    return false;
  }

  value = node.Scalar();
  if (value.find_first_of("\r\n") != std::string::npos) {
    errata.note(ERRATA_ERROR_SEV, "'{}' at line {} cannot contain a newline", key, node.Mark().line + 1);
    return false;
  }
  return true;
}

bool
read_bool(YAML::Node const &node, bool &value, swoc::Errata &errata, std::string_view key)
{
  try {
    value = node.as<bool>();
    return true;
  } catch (YAML::Exception const &) {
    errata.note(ERRATA_ERROR_SEV, "'{}' at line {} must be true or false", key, node.Mark().line + 1);
    return false;
  }
}

bool
validate_duration(std::string const &value, swoc::Errata &errata, std::string_view key, int line)
{
  std::string mutable_value{value};
  int         seconds = 0;

  if (char const *error = processDurationString(mutable_value.data(), &seconds); error != nullptr) {
    errata.note(ERRATA_ERROR_SEV, "'{}' at line {} has invalid duration '{}': {}", key, line, value, error);
    return false;
  }
  return true;
}

std::string
lowercase(std::string_view text)
{
  std::string result{text};
  for (char &c : result) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return result;
}

bool
parse_bool(std::string_view text, bool &value)
{
  std::string const normalized = lowercase(text);

  if (normalized == "true") {
    value = true;
    return true;
  }
  if (normalized == "false") {
    value = false;
    return true;
  }
  return false;
}

bool
parse_cookie_mode(std::string_view text, int &value)
{
  auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value >= 0 && value <= 4;
}

bool
tokenize_legacy_line(std::string_view line, std::vector<LegacyToken> &tokens, std::string &error)
{
  std::size_t pos = 0;

  while (pos < line.size()) {
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
      ++pos;
    }
    if (pos == line.size() || line[pos] == '#') {
      return true;
    }

    std::size_t const key_start = pos;
    while (pos < line.size() && line[pos] != '=' && !std::isspace(static_cast<unsigned char>(line[pos]))) {
      ++pos;
    }
    if (pos == key_start || pos == line.size() || line[pos] != '=') {
      error = "expected key=value";
      return false;
    }

    LegacyToken token;
    token.key.assign(line.substr(key_start, pos - key_start));
    ++pos;

    if (pos < line.size() && (line[pos] == '"' || line[pos] == '\'')) {
      char const quote  = line[pos++];
      bool       closed = false;

      while (pos < line.size()) {
        char const c = line[pos++];
        if (c == quote) {
          closed = true;
          break;
        }
        if (c == '\\' && pos < line.size()) {
          token.value.push_back(line[pos++]);
        } else {
          token.value.push_back(c);
        }
      }

      if (!closed) {
        error = "unterminated quoted value";
        return false;
      }
      if (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos])) && line[pos] != '#') {
        error = "unexpected text after quoted value";
        return false;
      }
    } else {
      std::size_t const value_start = pos;
      while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
      }
      token.value.assign(line.substr(value_start, pos - value_start));
    }

    if (token.value.empty()) {
      error = "empty value for '" + token.key + "'";
      return false;
    }
    tokens.push_back(std::move(token));
  }

  return true;
}

void
emit_string(YAML::Emitter &yaml, char const *key, std::optional<std::string> const &value)
{
  if (value) {
    yaml << YAML::Key << key << YAML::Value << *value;
  }
}

void
emit_bool(YAML::Emitter &yaml, char const *key, std::optional<bool> const &value)
{
  if (value) {
    yaml << YAML::Key << key << YAML::Value << *value;
  }
}

} // namespace

namespace config
{

int
CacheMatch::primary_count() const
{
  return static_cast<int>(dest_host.has_value()) + static_cast<int>(dest_domain.has_value()) +
         static_cast<int>(dest_ip.has_value()) + static_cast<int>(url_regex.has_value()) + static_cast<int>(host_regex.has_value());
}

bool
CacheMatch::empty() const
{
  return primary_count() == 0 && !port && !scheme && !prefix && !suffix && !method && !time && !src_ip && !incoming_port && !tag &&
         !internal;
}

bool
CacheAction::effective() const
{
  return cache.has_value() || revalidate.has_value() || pin_in_cache.has_value() || ttl_in_cache.has_value() ||
         ignore_no_cache.value_or(false) || ignore_client_no_cache.value_or(false) || ignore_server_no_cache.value_or(false) ||
         cache_responses_to_cookies.has_value();
}

ConfigResult<CacheConfig>
CacheConfigParser::parse(std::string const &filename) const
{
  std::error_code ec;
  std::string     content{swoc::file::load(filename, ec)};

  if (ec) {
    ConfigResult<CacheConfig> result;
    result.file_not_found = ec.value() == ENOENT;
    result.errata.note(result.file_not_found ? ERRATA_WARN_SEV : ERRATA_ERROR_SEV, "Failed to read cache configuration '{}': {}",
                       filename, ec);
    return result;
  }

  return parse_content(content, filename);
}

ConfigResult<CacheConfig>
CacheConfigParser::parse_content(std::string_view content, std::string_view filename) const
{
  if (content.find_first_not_of(" \t\r\n") == std::string_view::npos) {
    return {};
  }

  return detect_format(content, filename) == Format::YAML ? parse_yaml(content) : parse_legacy(content);
}

CacheConfigParser::Format
CacheConfigParser::detect_format(std::string_view content, std::string_view filename) const
{
  if (filename.ends_with(".yaml") || filename.ends_with(".yml")) {
    return Format::YAML;
  }
  if (filename.ends_with(".config")) {
    return Format::Legacy;
  }
  if (content.find("cache:") != std::string_view::npos) {
    return Format::YAML;
  }
  if (content.find('=') != std::string_view::npos) {
    return Format::Legacy;
  }
  return Format::YAML;
}

ConfigResult<CacheConfig>
CacheConfigParser::parse_yaml(std::string_view content) const
{
  ConfigResult<CacheConfig> result;

  try {
    YAML::Node root{YAML::Load(std::string{content})};
    if (root.IsNull()) {
      return result;
    }
    if (!root.IsMap()) {
      result.errata.note(ERRATA_ERROR_SEV, "cache.yaml must contain a top-level map");
      return result;
    }
    if (!has_only_keys(root, {KEY_CACHE}, result.errata, "cache.yaml")) {
      return result;
    }

    YAML::Node rules{root[KEY_CACHE]};
    if (!rules) {
      result.errata.note(ERRATA_ERROR_SEV, "cache.yaml is missing the top-level 'cache' key");
      return result;
    }
    if (!rules.IsSequence()) {
      result.errata.note(ERRATA_ERROR_SEV, "the top-level 'cache' key must contain a sequence");
      return result;
    }

    for (auto const &rule_node : rules) {
      if (!rule_node.IsMap()) {
        result.errata.note(ERRATA_ERROR_SEV, "cache rule at line {} must be a map", rule_node.Mark().line + 1);
        continue;
      }
      if (!has_only_keys(rule_node, rule_keys, result.errata, "cache rule")) {
        continue;
      }

      CacheRule rule;
      bool      rule_is_valid = true;

      if (YAML::Node match = rule_node[KEY_MATCH]; match) {
        if (!match.IsMap()) {
          result.errata.note(ERRATA_ERROR_SEV, "'match' at line {} must be a map", match.Mark().line + 1);
          continue;
        }
        if (!has_only_keys(match, match_keys, result.errata, "cache match")) {
          continue;
        }

        for (auto const &item : match) {
          std::string const key{item.first.Scalar()};

          if (key == KEY_INTERNAL) {
            bool value = false;
            if (read_bool(item.second, value, result.errata, key)) {
              rule.match.internal = value;
            } else {
              rule_is_valid = false;
            }
            continue;
          }

          std::string value;
          if (!read_scalar(item.second, value, result.errata, key)) {
            rule_is_valid = false;
            continue;
          }

          if (key == KEY_DEST_HOST) {
            rule.match.dest_host = std::move(value);
          } else if (key == KEY_DEST_DOMAIN) {
            rule.match.dest_domain = std::move(value);
          } else if (key == KEY_DEST_IP) {
            rule.match.dest_ip = std::move(value);
          } else if (key == KEY_URL_REGEX) {
            rule.match.url_regex = std::move(value);
          } else if (key == KEY_HOST_REGEX) {
            rule.match.host_regex = std::move(value);
          } else if (key == KEY_PORT) {
            rule.match.port = std::move(value);
          } else if (key == KEY_SCHEME) {
            rule.match.scheme = std::move(value);
          } else if (key == KEY_PREFIX) {
            rule.match.prefix = std::move(value);
          } else if (key == KEY_SUFFIX) {
            rule.match.suffix = std::move(value);
          } else if (key == KEY_METHOD) {
            rule.match.method = std::move(value);
          } else if (key == KEY_TIME) {
            rule.match.time = std::move(value);
          } else if (key == KEY_SRC_IP) {
            rule.match.src_ip = std::move(value);
          } else if (key == KEY_INCOMING_PORT) {
            rule.match.incoming_port = std::move(value);
          } else if (key == KEY_TAG) {
            rule.match.tag = std::move(value);
          }
        }
      }

      if (rule.match.primary_count() > 1) {
        result.errata.note(ERRATA_ERROR_SEV, "cache rule at line {} has multiple primary match keys", rule_node.Mark().line + 1);
        rule_is_valid = false;
      }

      YAML::Node action{rule_node[KEY_ACTION]};
      if (!action || !action.IsMap()) {
        result.errata.note(ERRATA_ERROR_SEV, "cache rule at line {} must contain an 'action' map", rule_node.Mark().line + 1);
        continue;
      }
      if (!has_only_keys(action, action_keys, result.errata, "cache action")) {
        continue;
      }

      for (auto const &item : action) {
        std::string const key{item.first.Scalar()};

        if (key == KEY_CACHE_MODE) {
          std::string value;
          if (!read_scalar(item.second, value, result.errata, key)) {
            rule_is_valid = false;
          } else if (value == "never") {
            rule.action.cache = CacheMode::NEVER;
          } else if (value == "standard") {
            rule.action.cache = CacheMode::STANDARD;
          } else {
            result.errata.note(ERRATA_ERROR_SEV, "'cache' at line {} must be 'never' or 'standard'", item.second.Mark().line + 1);
            rule_is_valid = false;
          }
        } else if (key == KEY_REVALIDATE || key == KEY_PIN_IN_CACHE || key == KEY_TTL_IN_CACHE) {
          std::string value;
          if (!read_scalar(item.second, value, result.errata, key) ||
              !validate_duration(value, result.errata, key, item.second.Mark().line + 1)) {
            rule_is_valid = false;
          } else if (key == KEY_REVALIDATE) {
            rule.action.revalidate = std::move(value);
          } else if (key == KEY_PIN_IN_CACHE) {
            rule.action.pin_in_cache = std::move(value);
          } else {
            rule.action.ttl_in_cache = std::move(value);
          }
        } else if (key == KEY_CACHE_RESPONSES_TO_COOKIES) {
          try {
            int const value = item.second.as<int>();
            if (value < 0 || value > 4) {
              throw YAML::BadConversion(item.second.Mark());
            }
            rule.action.cache_responses_to_cookies = value;
          } catch (YAML::Exception const &) {
            result.errata.note(ERRATA_ERROR_SEV, "'{}' at line {} must be an integer from 0 through 4", key,
                               item.second.Mark().line + 1);
            rule_is_valid = false;
          }
        } else {
          bool value = false;
          if (!read_bool(item.second, value, result.errata, key)) {
            rule_is_valid = false;
          } else if (key == KEY_IGNORE_NO_CACHE) {
            rule.action.ignore_no_cache = value;
          } else if (key == KEY_IGNORE_CLIENT_NO_CACHE) {
            rule.action.ignore_client_no_cache = value;
          } else if (key == KEY_IGNORE_SERVER_NO_CACHE) {
            rule.action.ignore_server_no_cache = value;
          }
        }
      }

      if (rule.action.cache == CacheMode::NEVER && rule.action.ttl_in_cache) {
        result.errata.note(ERRATA_ERROR_SEV, "cache rule at line {} cannot combine 'cache: never' with 'ttl_in_cache'",
                           rule_node.Mark().line + 1);
        rule_is_valid = false;
      }
      if (!rule.action.effective()) {
        result.errata.note(ERRATA_ERROR_SEV, "cache rule at line {} does not specify an effective action",
                           rule_node.Mark().line + 1);
        rule_is_valid = false;
      }
      if (rule_is_valid) {
        result.value.push_back(std::move(rule));
      }
    }
  } catch (YAML::Exception const &e) {
    result.errata.note(ERRATA_ERROR_SEV, "failed to parse cache.yaml: {}", e.what());
  }

  return result;
}

ConfigResult<CacheConfig>
CacheConfigParser::parse_legacy(std::string_view content) const
{
  ConfigResult<CacheConfig> result;
  std::size_t               offset  = 0;
  int                       line_no = 0;

  while (offset <= content.size()) {
    std::size_t const end  = content.find('\n', offset);
    std::string_view  line = end == std::string_view::npos ? content.substr(offset) : content.substr(offset, end - offset);
    offset                 = end == std::string_view::npos ? content.size() + 1 : end + 1;
    ++line_no;

    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
      line.remove_prefix(1);
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
      line.remove_suffix(1);
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }

    std::vector<LegacyToken> tokens;
    std::string              tokenize_error;
    if (!tokenize_legacy_line(line, tokens, tokenize_error)) {
      result.errata.note(ERRATA_ERROR_SEV, "cache.config line {} is malformed: {}", line_no, tokenize_error);
      continue;
    }

    CacheRule             rule;
    bool                  rule_is_valid   = true;
    int                   directive_count = 0;
    std::set<std::string> seen_keys;

    for (auto const &[raw_key, value] : tokens) {
      std::string key = lowercase(raw_key);
      if (key == KEY_INCOMING_PORT) {
        key = "iport";
      }

      if (!seen_keys.insert(key).second) {
        result.errata.note(ERRATA_ERROR_SEV, "cache.config line {} repeats '{}'", line_no, raw_key);
        rule_is_valid = false;
        continue;
      }

      if (key == KEY_DEST_HOST) {
        rule.match.dest_host = value;
      } else if (key == KEY_DEST_DOMAIN) {
        rule.match.dest_domain = value;
      } else if (key == KEY_DEST_IP) {
        rule.match.dest_ip = value;
      } else if (key == KEY_URL_REGEX) {
        rule.match.url_regex = value;
      } else if (key == KEY_HOST_REGEX) {
        rule.match.host_regex = value;
      } else if (key == KEY_PORT) {
        rule.match.port = value;
      } else if (key == KEY_SCHEME) {
        rule.match.scheme = value;
      } else if (key == KEY_PREFIX) {
        rule.match.prefix = value;
      } else if (key == KEY_SUFFIX) {
        rule.match.suffix = value;
      } else if (key == KEY_METHOD) {
        rule.match.method = value;
      } else if (key == KEY_TIME) {
        rule.match.time = value;
      } else if (key == KEY_SRC_IP) {
        rule.match.src_ip = value;
      } else if (key == "iport") {
        rule.match.incoming_port = value;
      } else if (key == KEY_TAG) {
        rule.match.tag = value;
      } else if (key == KEY_INTERNAL) {
        bool parsed_value = false;
        if (parse_bool(value, parsed_value)) {
          rule.match.internal = parsed_value;
        } else {
          result.errata.note(ERRATA_ERROR_SEV, "cache.config line {} has invalid internal value '{}'", line_no, value);
          rule_is_valid = false;
        }
      } else if (key == KEY_ACTION) {
        std::string const action = lowercase(value);

        ++directive_count;
        if (action == "never-cache") {
          rule.action.cache = CacheMode::NEVER;
        } else if (action == "standard-cache") {
          rule.action.cache = CacheMode::STANDARD;
        } else if (action == "ignore-no-cache") {
          rule.action.ignore_no_cache = true;
        } else if (action == "ignore-client-no-cache") {
          rule.action.ignore_client_no_cache = true;
        } else if (action == "ignore-server-no-cache") {
          rule.action.ignore_server_no_cache = true;
        } else {
          result.errata.note(ERRATA_ERROR_SEV, "cache.config line {} has invalid action '{}'", line_no, value);
          rule_is_valid = false;
        }
      } else if (key == KEY_REVALIDATE || key == "pin-in-cache" || key == "ttl-in-cache") {
        ++directive_count;
        if (!validate_duration(value, result.errata, key, line_no)) {
          rule_is_valid = false;
        } else if (key == KEY_REVALIDATE) {
          rule.action.revalidate = value;
        } else if (key == "pin-in-cache") {
          rule.action.pin_in_cache = value;
        } else {
          rule.action.ttl_in_cache = value;
        }
      } else if (key == "cache-responses-to-cookies") {
        int mode = -1;
        if (parse_cookie_mode(value, mode)) {
          rule.action.cache_responses_to_cookies = mode;
        } else {
          result.errata.note(ERRATA_ERROR_SEV, "cache.config line {} has invalid cache-responses-to-cookies value '{}'", line_no,
                             value);
          rule_is_valid = false;
        }
      } else {
        result.errata.note(ERRATA_ERROR_SEV, "cache.config line {} has unknown key '{}'", line_no, raw_key);
        rule_is_valid = false;
      }
    }

    if (rule.match.primary_count() != 1) {
      result.errata.note(ERRATA_ERROR_SEV, "cache.config line {} must have exactly one primary destination", line_no);
      rule_is_valid = false;
    }
    if (directive_count != 1) {
      result.errata.note(ERRATA_ERROR_SEV, "cache.config line {} must have exactly one cache directive", line_no);
      rule_is_valid = false;
    }
    if (rule_is_valid) {
      result.value.push_back(std::move(rule));
    }
  }

  return result;
}

std::string
CacheConfigMarshaller::to_yaml(CacheConfig const &config) const
{
  YAML::Emitter yaml;

  yaml << YAML::BeginMap << YAML::Key << KEY_CACHE << YAML::Value << YAML::BeginSeq;
  for (auto const &rule : config) {
    yaml << YAML::BeginMap;

    if (!rule.match.empty()) {
      yaml << YAML::Key << KEY_MATCH << YAML::Value << YAML::BeginMap;
      emit_string(yaml, KEY_DEST_HOST, rule.match.dest_host);
      emit_string(yaml, KEY_DEST_DOMAIN, rule.match.dest_domain);
      emit_string(yaml, KEY_DEST_IP, rule.match.dest_ip);
      emit_string(yaml, KEY_URL_REGEX, rule.match.url_regex);
      emit_string(yaml, KEY_HOST_REGEX, rule.match.host_regex);
      emit_string(yaml, KEY_PORT, rule.match.port);
      emit_string(yaml, KEY_SCHEME, rule.match.scheme);
      emit_string(yaml, KEY_PREFIX, rule.match.prefix);
      emit_string(yaml, KEY_SUFFIX, rule.match.suffix);
      emit_string(yaml, KEY_METHOD, rule.match.method);
      emit_string(yaml, KEY_TIME, rule.match.time);
      emit_string(yaml, KEY_SRC_IP, rule.match.src_ip);
      emit_string(yaml, KEY_INCOMING_PORT, rule.match.incoming_port);
      emit_string(yaml, KEY_TAG, rule.match.tag);
      emit_bool(yaml, KEY_INTERNAL, rule.match.internal);
      yaml << YAML::EndMap;
    }

    yaml << YAML::Key << KEY_ACTION << YAML::Value << YAML::BeginMap;
    if (rule.action.cache) {
      yaml << YAML::Key << KEY_CACHE_MODE << YAML::Value << (*rule.action.cache == CacheMode::NEVER ? "never" : "standard");
    }
    emit_string(yaml, KEY_REVALIDATE, rule.action.revalidate);
    emit_string(yaml, KEY_PIN_IN_CACHE, rule.action.pin_in_cache);
    emit_string(yaml, KEY_TTL_IN_CACHE, rule.action.ttl_in_cache);
    emit_bool(yaml, KEY_IGNORE_NO_CACHE, rule.action.ignore_no_cache);
    emit_bool(yaml, KEY_IGNORE_CLIENT_NO_CACHE, rule.action.ignore_client_no_cache);
    emit_bool(yaml, KEY_IGNORE_SERVER_NO_CACHE, rule.action.ignore_server_no_cache);
    if (rule.action.cache_responses_to_cookies) {
      yaml << YAML::Key << KEY_CACHE_RESPONSES_TO_COOKIES << YAML::Value << *rule.action.cache_responses_to_cookies;
    }
    yaml << YAML::EndMap << YAML::EndMap;
  }
  yaml << YAML::EndSeq << YAML::EndMap;

  return yaml.c_str();
}

} // namespace config
