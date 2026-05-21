/** @file

  Storage configuration parsing and marshalling implementation.

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

#include "config/storage.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <exception>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include <yaml-cpp/yaml.h>

#include "swoc/swoc_file.h"
#include "tscore/ParseRules.h"
#include "tsutil/ts_diag_levels.h"

namespace
{

constexpr swoc::Errata::Severity ERRATA_NOTE_SEV{static_cast<swoc::Errata::severity_type>(DL_Note)};
constexpr swoc::Errata::Severity ERRATA_ERROR_SEV{static_cast<swoc::Errata::severity_type>(DL_Error)};

// YAML key names - top-level
constexpr char KEY_CACHE[]   = "cache";
constexpr char KEY_SPANS[]   = "spans";
constexpr char KEY_VOLUMES[] = "volumes";

// YAML key names - spans
constexpr char KEY_NAME[]      = "name";
constexpr char KEY_PATH[]      = "path";
constexpr char KEY_SIZE[]      = "size";
constexpr char KEY_HASH_SEED[] = "hash_seed";

// YAML key names - volumes
constexpr char KEY_ID[]               = "id";
constexpr char KEY_SCHEME[]           = "scheme";
constexpr char KEY_RAM_CACHE[]        = "ram_cache";
constexpr char KEY_RAM_CACHE_SIZE[]   = "ram_cache_size";
constexpr char KEY_RAM_CACHE_CUTOFF[] = "ram_cache_cutoff";
constexpr char KEY_AVG_OBJ_SIZE[]     = "avg_obj_size";
constexpr char KEY_FRAGMENT_SIZE[]    = "fragment_size";

// YAML key names - volume span refs
constexpr char KEY_USE[] = "use";

constexpr int MAX_VOLUME_IDX = 255;

std::set<std::string> const valid_cache_keys   = {KEY_SPANS, KEY_VOLUMES};
std::set<std::string> const valid_span_keys    = {KEY_NAME, KEY_PATH, KEY_SIZE, KEY_HASH_SEED};
std::set<std::string> const valid_volume_keys  = {KEY_ID,           KEY_SCHEME,         KEY_SIZE,
                                                  KEY_RAM_CACHE,    KEY_RAM_CACHE_SIZE, KEY_RAM_CACHE_CUTOFF,
                                                  KEY_AVG_OBJ_SIZE, KEY_FRAGMENT_SIZE,  KEY_SPANS};
std::set<std::string> const valid_spanref_keys = {KEY_USE, KEY_SIZE};

/**
 * Validate that all keys in @a node are members of @a valid_keys.
 * Returns warnings for unknown keys.
 */
swoc::Errata
validate_map(YAML::Node const &node, std::set<std::string> const &valid_keys)
{
  swoc::Errata          errata;
  std::set<std::string> unknown_keys;

  if (!node.IsMap()) {
    errata.note(ERRATA_ERROR_SEV, "expected a map node");
    return errata;
  }

  for (auto const &item : node) {
    std::string key = item.first.as<std::string>();
    if (valid_keys.find(key) == valid_keys.end() && unknown_keys.insert(key).second) {
      errata.note(ERRATA_NOTE_SEV, "ignoring unknown key '{}' at line {}", key, item.first.Mark().line + 1);
    }
  }

  return errata;
}

/**
 * Parse a human-readable size string into bytes (raw, no MB conversion).
 * Supports K/M/G/T suffixes. Returns -1 if the string does not start with a digit.
 */
int64_t
parse_byte_size(std::string const &s)
{
  if (s.empty() || !std::isdigit(static_cast<unsigned char>(s[0]))) {
    return -1;
  }
  return ink_atoi64(s.c_str());
}

/**
 * Parse a human-readable size string into bytes.
 * Supports K/M/G/T suffixes and percent values.
 * Returns the absolute byte value (not converted to MB — callers must do that).
 *
 * @return false if parsing failed.
 */
bool
parse_size_string(config::StorageVolumeEntry::Size &size, std::string const &s)
{
  if (s.empty() || !std::isdigit(static_cast<unsigned char>(s[0]))) {
    return false;
  }

  if (s.back() == '%') {
    int value = std::stoi(s.substr(0, s.size() - 1));
    if (value > 100) {
      return false;
    }
    size.in_percent = true;
    size.percent    = value;
  } else {
    size.in_percent = false;
    // Convert raw bytes to megabytes for block calculation (matching original logic).
    int64_t bytes = parse_byte_size(s);
    if (bytes < 0) {
      return false;
    }
    size.absolute_value = bytes / 1024 / 1024;
  }

  return true;
}

/**
 * Parse a single token from @a rest (whitespace-delimited).
 * Returns the token and advances @a rest past it and any trailing whitespace.
 */
std::string
next_token(std::string_view &rest)
{
  size_t start = rest.find_first_not_of(" \t");
  if (start == std::string_view::npos) {
    rest = {};
    return {};
  }
  rest       = rest.substr(start);
  size_t end = rest.find_first_of(" \t");
  if (end == std::string_view::npos) {
    std::string tok{rest};
    rest = {};
    return tok;
  }
  std::string tok{rest.substr(0, end)};
  rest = rest.substr(end);
  return tok;
}

/**
 * Parse legacy storage.config content.
 *
 * Format per line:  pathname [size_bytes] [id=hash_seed] [volume=N]
 */
config::ConfigResult<config::StorageConfig>
parse_legacy_storage_config(std::string_view content)
{
  config::StorageConfig result;
  swoc::Errata          errata;

  // volume_num -> list of span names (path used as name)
  std::map<int, std::vector<std::string>> vol_span_map;

  std::istringstream ss{std::string{content}};
  std::string        line;
  int                line_num = 0;

  while (std::getline(ss, line)) {
    ++line_num;

    // Normalize whitespace so simple whitespace splitting works.
    for (char &c : line) {
      if (std::isspace(static_cast<unsigned char>(c))) {
        c = ' ';
      }
    }

    std::string_view rest{line};
    std::string      path_tok = next_token(rest);

    if (path_tok.empty() || path_tok[0] == '#') {
      continue;
    }

    int64_t     size = 0;
    std::string hash_seed;
    int         volume_num = -1;
    bool        err        = false;

    while (true) {
      std::string tok = next_token(rest);
      if (tok.empty() || tok[0] == '#') {
        break;
      }

      if (tok.rfind("id=", 0) == 0) {
        hash_seed = tok.substr(3);
      } else if (tok.rfind("volume=", 0) == 0) {
        std::string num_str = tok.substr(7);
        if (num_str.empty() || !ParseRules::is_digit(num_str[0])) {
          errata.note(ERRATA_ERROR_SEV, "invalid volume number '{}' at line {}", num_str, line_num);
          err = true;
          break;
        }
        volume_num = std::stoi(num_str);
        if (volume_num < 1 || volume_num > MAX_VOLUME_IDX) {
          errata.note(ERRATA_ERROR_SEV, "volume number {} out of range at line {}", volume_num, line_num);
          err = true;
          break;
        }
      } else if (ParseRules::is_digit(tok[0])) {
        const char *end_ptr = nullptr;
        int64_t     v       = ink_atoi64(tok.c_str(), &end_ptr);
        if (v <= 0 || (end_ptr && *end_ptr != '\0')) {
          errata.note(ERRATA_ERROR_SEV, "invalid size '{}' at line {}", tok, line_num);
          err = true;
          break;
        }
        size = v;
      } else {
        errata.note(ERRATA_NOTE_SEV, "ignoring unknown token '{}' at line {}", tok, line_num);
      }
    }

    if (err) {
      continue;
    }

    config::StorageSpanEntry span;
    span.name      = path_tok;
    span.path      = path_tok;
    span.size      = size;
    span.hash_seed = hash_seed;
    result.spans.push_back(std::move(span));

    if (volume_num > 0) {
      vol_span_map[volume_num].push_back(path_tok);
    }
  }

  // Build volume entries from span assignments.
  for (auto const &[vol_id, span_names] : vol_span_map) {
    config::StorageVolumeEntry vol;
    vol.id     = vol_id;
    vol.scheme = "http";
    for (auto const &sname : span_names) {
      config::StorageVolumeEntry::SpanRef ref;
      ref.use = sname;
      vol.spans.push_back(std::move(ref));
    }
    result.volumes.push_back(std::move(vol));
  }

  return {result, std::move(errata)};
}

/**
 * Parse legacy volume.config content.
 *
 * Format per line:
 *   volume=N scheme=http size=V[%] [avg_obj_size=V] [fragment_size=V]
 *                                  [ramcache=true|false]
 *                                  [ram_cache_size=V] [ram_cache_cutoff=V]
 *
 * Absolute sizes are in megabytes.
 */
config::ConfigResult<config::StorageConfig>
parse_legacy_volume_config(std::string_view content)
{
  config::StorageConfig result;
  swoc::Errata          errata;

  std::istringstream ss{std::string{content}};
  std::string        line;
  int                line_num      = 0;
  int                total_percent = 0;
  std::set<int>      seen_ids;

  while (std::getline(ss, line)) {
    ++line_num;

    std::string_view rest{line};

    // Skip leading whitespace.
    size_t start = rest.find_first_not_of(" \t\r");
    if (start == std::string_view::npos || rest[start] == '#') {
      continue;
    }
    rest = rest.substr(start);

    int         volume_number = 0;
    std::string scheme;
    int         size             = 0;
    bool        in_percent       = false;
    bool        ramcache_enabled = true;
    int64_t     ram_cache_size   = -1;
    int64_t     ram_cache_cutoff = -1;
    int         avg_obj_size     = -1;
    int         fragment_size    = -1;
    bool        parse_error      = false;

    while (true) {
      std::string tok = next_token(rest);
      if (tok.empty() || tok[0] == '#') {
        break;
      }

      size_t eq = tok.find('=');
      if (eq == std::string::npos) {
        errata.note(ERRATA_ERROR_SEV, "unexpected token '{}' at line {} (expected key=value)", tok, line_num);
        parse_error = true;
        break;
      }

      std::string key{tok.substr(0, eq)};
      std::string val{tok.substr(eq + 1)};

      if (strcasecmp(key.c_str(), "volume") == 0) {
        if (val.empty() || !ParseRules::is_digit(val[0])) {
          errata.note(ERRATA_ERROR_SEV, "invalid volume number '{}' at line {}", val, line_num);
          parse_error = true;
          break;
        }
        volume_number = std::stoi(val);
        if (volume_number < 1 || volume_number > MAX_VOLUME_IDX) {
          errata.note(ERRATA_ERROR_SEV, "volume number {} out of range [1,255] at line {}", volume_number, line_num);
          parse_error = true;
          break;
        }
      } else if (strcasecmp(key.c_str(), "scheme") == 0) {
        if (strcasecmp(val.c_str(), "http") == 0) {
          scheme = "http";
        } else {
          errata.note(ERRATA_ERROR_SEV, "unsupported scheme '{}' at line {}", val, line_num);
          parse_error = true;
          break;
        }
      } else if (strcasecmp(key.c_str(), "size") == 0) {
        if (!val.empty() && val.back() == '%') {
          size = std::stoi(val.substr(0, val.size() - 1));
          if (size < 0 || size > 100) {
            errata.note(ERRATA_ERROR_SEV, "size percentage {} out of range at line {}", size, line_num);
            parse_error = true;
            break;
          }
          in_percent     = true;
          total_percent += size;
          if (total_percent > 100) {
            errata.note(ERRATA_ERROR_SEV, "total volume size exceeds 100%% at line {}", line_num);
            parse_error = true;
            break;
          }
        } else {
          if (val.empty() || !ParseRules::is_digit(val[0])) {
            errata.note(ERRATA_ERROR_SEV, "invalid size '{}' at line {}", val, line_num);
            parse_error = true;
            break;
          }
          size       = std::stoi(val);
          in_percent = false;
        }
      } else if (strcasecmp(key.c_str(), "avg_obj_size") == 0) {
        avg_obj_size = static_cast<int>(ink_atoi64(val.c_str()));
      } else if (strcasecmp(key.c_str(), "fragment_size") == 0) {
        fragment_size = static_cast<int>(ink_atoi64(val.c_str()));
      } else if (strcasecmp(key.c_str(), "ramcache") == 0) {
        if (strcasecmp(val.c_str(), "false") == 0) {
          ramcache_enabled = false;
        } else if (strcasecmp(val.c_str(), "true") == 0) {
          ramcache_enabled = true;
        } else {
          errata.note(ERRATA_ERROR_SEV, "invalid ramcache value '{}' at line {}", val, line_num);
          parse_error = true;
          break;
        }
      } else if (strcasecmp(key.c_str(), "ram_cache_size") == 0) {
        ram_cache_size = ink_atoi64(val.c_str());
      } else if (strcasecmp(key.c_str(), "ram_cache_cutoff") == 0) {
        ram_cache_cutoff = ink_atoi64(val.c_str());
      } else {
        errata.note(ERRATA_NOTE_SEV, "ignoring unknown key '{}' at line {}", key, line_num);
      }
    }

    if (parse_error || volume_number == 0) {
      continue;
    }

    if (seen_ids.count(volume_number)) {
      errata.note(ERRATA_ERROR_SEV, "duplicate volume number {} at line {}", volume_number, line_num);
      continue;
    }
    seen_ids.insert(volume_number);

    config::StorageVolumeEntry vol;
    vol.id               = volume_number;
    vol.scheme           = scheme.empty() ? "http" : scheme;
    vol.ram_cache        = ramcache_enabled;
    vol.ram_cache_size   = ram_cache_size;
    vol.ram_cache_cutoff = ram_cache_cutoff;
    vol.avg_obj_size     = avg_obj_size;
    vol.fragment_size    = fragment_size;
    if (in_percent) {
      vol.size.in_percent = true;
      vol.size.percent    = size;
    } else {
      vol.size.in_percent     = false;
      vol.size.absolute_value = size; // megabytes
    }
    result.volumes.push_back(std::move(vol));
  }

  return {result, std::move(errata)};
}

} // namespace

namespace YAML
{

template <> struct convert<config::StorageSpanEntry> {
  static bool
  decode(Node const &node, config::StorageSpanEntry &span)
  {
    if (!node[KEY_NAME]) {
      throw ParserException(node.Mark(), "missing 'name' argument in cache.spans[]");
    }
    span.name = node[KEY_NAME].as<std::string>();

    if (!node[KEY_PATH]) {
      throw ParserException(node.Mark(), "missing 'path' argument in cache.spans[]");
    }
    span.path = node[KEY_PATH].as<std::string>();

    if (node[KEY_SIZE]) {
      std::string s = node[KEY_SIZE].as<std::string>();
      int64_t     v = parse_byte_size(s);
      if (v < 0) {
        throw ParserException(node.Mark(), "invalid 'size' value in cache.spans[]: " + s);
      }
      span.size = v;
    }

    if (node[KEY_HASH_SEED]) {
      span.hash_seed = node[KEY_HASH_SEED].as<std::string>();
    }

    return true;
  }
};

template <> struct convert<config::StorageVolumeEntry::Size> {
  static bool
  decode(Node const &node, config::StorageVolumeEntry::Size &size)
  {
    std::string s = node.as<std::string>();
    if (!parse_size_string(size, s)) {
      throw YAML::Exception(node.Mark(), "invalid size value: " + s);
    }
    return true;
  }
};

template <> struct convert<config::StorageVolumeEntry::SpanRef> {
  static bool
  decode(Node const &node, config::StorageVolumeEntry::SpanRef &ref)
  {
    if (!node[KEY_USE]) {
      throw ParserException(node.Mark(), "missing 'use' argument in cache.volumes[].spans[]");
    }
    ref.use = node[KEY_USE].as<std::string>();

    if (node[KEY_SIZE]) {
      ref.size = node[KEY_SIZE].as<config::StorageVolumeEntry::Size>();
    }

    return true;
  }
};

template <> struct convert<config::StorageVolumeEntry> {
  static bool
  decode(Node const &node, config::StorageVolumeEntry &vol)
  {
    if (!node[KEY_ID]) {
      throw ParserException(node.Mark(), "missing 'id' argument in cache.volumes[]");
    }
    vol.id = node[KEY_ID].as<int>();
    if (vol.id < 1 || vol.id > MAX_VOLUME_IDX) {
      throw ParserException(node.Mark(), "volume id out of range [1, 255]: " + std::to_string(vol.id));
    }

    if (node[KEY_SCHEME]) {
      std::string s = node[KEY_SCHEME].as<std::string>();
      if (s != "http") {
        throw ParserException(node.Mark(), "unsupported scheme '" + s + "' in cache.volumes[]");
      }
      vol.scheme = s;
    }

    if (node[KEY_SIZE]) {
      vol.size = node[KEY_SIZE].as<config::StorageVolumeEntry::Size>();
    }

    if (node[KEY_RAM_CACHE]) {
      vol.ram_cache = node[KEY_RAM_CACHE].as<bool>();
    }

    if (node[KEY_RAM_CACHE_SIZE]) {
      std::string s = node[KEY_RAM_CACHE_SIZE].as<std::string>();
      int64_t     v = parse_byte_size(s);
      if (v < 0) {
        throw ParserException(node.Mark(), "invalid 'ram_cache_size' value: " + s);
      }
      vol.ram_cache_size = v;
    }

    if (node[KEY_RAM_CACHE_CUTOFF]) {
      std::string s = node[KEY_RAM_CACHE_CUTOFF].as<std::string>();
      int64_t     v = parse_byte_size(s);
      if (v < 0) {
        throw ParserException(node.Mark(), "invalid 'ram_cache_cutoff' value: " + s);
      }
      vol.ram_cache_cutoff = v;
    }

    if (node[KEY_AVG_OBJ_SIZE]) {
      std::string s = node[KEY_AVG_OBJ_SIZE].as<std::string>();
      int64_t     v = parse_byte_size(s);
      if (v < 0) {
        throw ParserException(node.Mark(), "invalid 'avg_obj_size' value: " + s);
      }
      vol.avg_obj_size = static_cast<int>(v);
    }

    if (node[KEY_FRAGMENT_SIZE]) {
      std::string s = node[KEY_FRAGMENT_SIZE].as<std::string>();
      int64_t     v = parse_byte_size(s);
      if (v < 0) {
        throw ParserException(node.Mark(), "invalid 'fragment_size' value: " + s);
      }
      vol.fragment_size = static_cast<int>(v);
    }

    if (node[KEY_SPANS]) {
      YAML::Node spans_node = node[KEY_SPANS];
      if (!spans_node.IsSequence()) {
        throw ParserException(node.Mark(), "expected sequence for 'spans' in cache.volumes[]");
      }
      for (auto const &s : spans_node) {
        vol.spans.push_back(s.as<config::StorageVolumeEntry::SpanRef>());
      }
    }

    return true;
  }
};

} // namespace YAML

namespace config
{

static void
emit_size(YAML::Emitter &out, StorageVolumeEntry::Size const &size)
{
  if (size.in_percent) {
    out << std::to_string(size.percent) + "%";
  } else {
    out << std::to_string(size.absolute_value * 1024 * 1024);
  }
}

ConfigResult<StorageConfig>
StorageParser::parse(std::string const &filename)
{
  std::error_code ec;
  std::string     content = swoc::file::load(filename, ec);
  if (ec) {
    if (ec.value() == ENOENT) {
      return {{}, swoc::Errata(ERRATA_ERROR_SEV, "Cannot open storage configuration \"{}\" - {}", filename, ec)};
    }
    return {{}, swoc::Errata(ERRATA_ERROR_SEV, "Failed to read storage configuration from \"{}\" - {}", filename, ec)};
  }

  bool use_yaml = filename.size() >= 5 && filename.substr(filename.size() - 5) == ".yaml";
  if (use_yaml) {
    return parse_content(content);
  }
  return parse_legacy_storage_content(content);
}

ConfigResult<StorageConfig>
StorageParser::parse_content(std::string_view content)
{
  StorageConfig result;
  swoc::Errata  errata;

  try {
    YAML::Node root = YAML::Load(std::string(content));
    if (root.IsNull()) {
      return {result, std::move(errata)};
    }

    if (!root[KEY_CACHE]) {
      return {result, swoc::Errata(ERRATA_ERROR_SEV, "expected a top-level 'cache' node in storage.yaml")};
    }

    YAML::Node cache = root[KEY_CACHE];

    // Warn about unknown keys under 'cache'.
    errata.note(validate_map(cache, valid_cache_keys));
    if (!errata.is_ok()) {
      return {result, std::move(errata)};
    }

    // Parse spans.
    if (cache[KEY_SPANS]) {
      YAML::Node spans_node = cache[KEY_SPANS];
      if (!spans_node.IsSequence()) {
        return {result, swoc::Errata(ERRATA_ERROR_SEV, "expected sequence for 'cache.spans'")};
      }
      for (auto const &s : spans_node) {
        // Warn about unknown keys in each span.
        errata.note(validate_map(s, valid_span_keys));
        result.spans.push_back(s.as<StorageSpanEntry>());
      }
    }

    // Parse volumes.
    if (cache[KEY_VOLUMES]) {
      YAML::Node vols_node = cache[KEY_VOLUMES];
      if (!vols_node.IsSequence()) {
        return {result, swoc::Errata(ERRATA_ERROR_SEV, "expected sequence for 'cache.volumes'")};
      }

      int           total_percent = 0;
      std::set<int> seen_ids;

      for (auto const &v : vols_node) {
        // Warn about unknown keys in each volume.
        errata.note(validate_map(v, valid_volume_keys));

        // Warn about unknown keys in each volume's span refs.
        if (v[KEY_SPANS] && v[KEY_SPANS].IsSequence()) {
          for (auto const &sr : v[KEY_SPANS]) {
            errata.note(validate_map(sr, valid_spanref_keys));
          }
        }

        StorageVolumeEntry vol = v.as<StorageVolumeEntry>();

        if (seen_ids.count(vol.id)) {
          return {result, swoc::Errata(ERRATA_ERROR_SEV, "duplicate volume id {} at line {}", vol.id, v.Mark().line + 1)};
        }
        seen_ids.insert(vol.id);

        if (vol.size.in_percent) {
          total_percent += vol.size.percent;
          if (total_percent > 100) {
            return {result, swoc::Errata(ERRATA_ERROR_SEV, "total volume size exceeds 100%%")};
          }
        }

        result.volumes.push_back(std::move(vol));
      }
    }

  } catch (std::exception const &ex) {
    return {result, swoc::Errata(ERRATA_ERROR_SEV, "YAML parse error: {}", ex.what())};
  }

  return {result, std::move(errata)};
}

ConfigResult<StorageConfig>
StorageParser::parse_legacy_storage_content(std::string_view content)
{
  return parse_legacy_storage_config(content);
}

ConfigResult<StorageConfig>
VolumeParser::parse(std::string const &filename)
{
  std::error_code ec;
  std::string     content = swoc::file::load(filename, ec);
  if (ec) {
    if (ec.value() == ENOENT) {
      ConfigResult<StorageConfig> r;
      r.file_not_found = true;
      r.errata.note(ERRATA_ERROR_SEV, "Cannot open volume configuration \"{}\" - {}", filename, ec);
      return r;
    }
    return {{}, swoc::Errata(ERRATA_ERROR_SEV, "Failed to read volume configuration from \"{}\" - {}", filename, ec)};
  }

  return parse_content(content);
}

ConfigResult<StorageConfig>
VolumeParser::parse_content(std::string_view content)
{
  return parse_legacy_volume_config(content);
}

StorageConfig
merge_legacy_storage_configs(StorageConfig const &storage, StorageConfig const &volumes)
{
  StorageConfig merged = storage;

  if (volumes.volumes.empty()) {
    return merged;
  }

  // Build a lookup of volume id -> span refs gathered from storage.config.
  std::map<int, std::vector<StorageVolumeEntry::SpanRef>> span_ref_map;
  for (auto const &vol : merged.volumes) {
    span_ref_map[vol.id] = vol.spans;
  }

  merged.volumes.clear();
  for (auto vol : volumes.volumes) {
    auto it = span_ref_map.find(vol.id);
    if (it != span_ref_map.end()) {
      vol.spans = it->second;
    }
    merged.volumes.push_back(std::move(vol));
  }

  return merged;
}

std::string
StorageMarshaller::to_yaml(StorageConfig const &config)
{
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << KEY_CACHE << YAML::Value << YAML::BeginMap;

  // Spans.
  out << YAML::Key << KEY_SPANS << YAML::Value << YAML::BeginSeq;
  for (auto const &span : config.spans) {
    out << YAML::BeginMap;
    out << YAML::Key << KEY_NAME << YAML::Value << span.name;
    out << YAML::Key << KEY_PATH << YAML::Value << span.path;
    if (span.size > 0) {
      out << YAML::Key << KEY_SIZE << YAML::Value << std::to_string(span.size);
    }
    if (!span.hash_seed.empty()) {
      out << YAML::Key << KEY_HASH_SEED << YAML::Value << span.hash_seed;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;

  // Volumes.
  if (!config.volumes.empty()) {
    out << YAML::Key << KEY_VOLUMES << YAML::Value << YAML::BeginSeq;
    for (auto const &vol : config.volumes) {
      out << YAML::BeginMap;
      out << YAML::Key << KEY_ID << YAML::Value << vol.id;
      out << YAML::Key << KEY_SCHEME << YAML::Value << vol.scheme;
      if (!vol.size.is_empty()) {
        out << YAML::Key << KEY_SIZE << YAML::Value;
        emit_size(out, vol.size);
      }
      out << YAML::Key << KEY_RAM_CACHE << YAML::Value << vol.ram_cache;
      if (vol.ram_cache_size >= 0) {
        out << YAML::Key << KEY_RAM_CACHE_SIZE << YAML::Value << std::to_string(vol.ram_cache_size);
      }
      if (vol.ram_cache_cutoff >= 0) {
        out << YAML::Key << KEY_RAM_CACHE_CUTOFF << YAML::Value << std::to_string(vol.ram_cache_cutoff);
      }
      if (vol.avg_obj_size >= 0) {
        out << YAML::Key << KEY_AVG_OBJ_SIZE << YAML::Value << std::to_string(vol.avg_obj_size);
      }
      if (vol.fragment_size >= 0) {
        out << YAML::Key << KEY_FRAGMENT_SIZE << YAML::Value << std::to_string(vol.fragment_size);
      }
      if (!vol.spans.empty()) {
        out << YAML::Key << KEY_SPANS << YAML::Value << YAML::BeginSeq;
        for (auto const &sr : vol.spans) {
          out << YAML::BeginMap;
          out << YAML::Key << KEY_USE << YAML::Value << sr.use;
          if (!sr.size.is_empty()) {
            out << YAML::Key << KEY_SIZE << YAML::Value;
            emit_size(out, sr.size);
          }
          out << YAML::EndMap;
        }
        out << YAML::EndSeq;
      }
      out << YAML::EndMap;
    }
    out << YAML::EndSeq;
  }

  out << YAML::EndMap << YAML::EndMap;
  return out.c_str();
}

std::string
StorageMarshaller::to_json(StorageConfig const &config)
{
  YAML::Emitter out;
  out << YAML::DoubleQuoted << YAML::Flow;
  out << YAML::BeginMap;
  out << YAML::Key << KEY_CACHE << YAML::Value << YAML::BeginMap;

  // Spans.
  out << YAML::Key << KEY_SPANS << YAML::Value << YAML::BeginSeq;
  for (auto const &span : config.spans) {
    out << YAML::BeginMap;
    out << YAML::Key << KEY_NAME << YAML::Value << span.name;
    out << YAML::Key << KEY_PATH << YAML::Value << span.path;
    if (span.size > 0) {
      out << YAML::Key << KEY_SIZE << YAML::Value << std::to_string(span.size);
    }
    if (!span.hash_seed.empty()) {
      out << YAML::Key << KEY_HASH_SEED << YAML::Value << span.hash_seed;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;

  // Volumes.
  if (!config.volumes.empty()) {
    out << YAML::Key << KEY_VOLUMES << YAML::Value << YAML::BeginSeq;
    for (auto const &vol : config.volumes) {
      out << YAML::BeginMap;
      out << YAML::Key << KEY_ID << YAML::Value << vol.id;
      out << YAML::Key << KEY_SCHEME << YAML::Value << vol.scheme;
      if (!vol.size.is_empty()) {
        out << YAML::Key << KEY_SIZE << YAML::Value;
        emit_size(out, vol.size);
      }
      out << YAML::Key << KEY_RAM_CACHE << YAML::Value << vol.ram_cache;
      if (vol.ram_cache_size >= 0) {
        out << YAML::Key << KEY_RAM_CACHE_SIZE << YAML::Value << std::to_string(vol.ram_cache_size);
      }
      if (vol.ram_cache_cutoff >= 0) {
        out << YAML::Key << KEY_RAM_CACHE_CUTOFF << YAML::Value << std::to_string(vol.ram_cache_cutoff);
      }
      if (vol.avg_obj_size >= 0) {
        out << YAML::Key << KEY_AVG_OBJ_SIZE << YAML::Value << std::to_string(vol.avg_obj_size);
      }
      if (vol.fragment_size >= 0) {
        out << YAML::Key << KEY_FRAGMENT_SIZE << YAML::Value << std::to_string(vol.fragment_size);
      }
      if (!vol.spans.empty()) {
        out << YAML::Key << KEY_SPANS << YAML::Value << YAML::BeginSeq;
        for (auto const &sr : vol.spans) {
          out << YAML::BeginMap;
          out << YAML::Key << KEY_USE << YAML::Value << sr.use;
          if (!sr.size.is_empty()) {
            out << YAML::Key << KEY_SIZE << YAML::Value;
            emit_size(out, sr.size);
          }
          out << YAML::EndMap;
        }
        out << YAML::EndSeq;
      }
      out << YAML::EndMap;
    }
    out << YAML::EndSeq;
  }

  out << YAML::EndMap << YAML::EndMap;
  return out.c_str();
}

} // namespace config
