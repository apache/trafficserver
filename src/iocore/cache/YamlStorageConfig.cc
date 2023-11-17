/** @file

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

#include "P_Cache.h"
#include "P_CacheHosting.h"

#include "iocore/cache/YamlStorageConfig.h"

#include "tscore/Diags.h"

#include <yaml-cpp/yaml.h>

#include <set>
#include <string>

namespace
{
/**
   cache:           # file level key
     spans:         #
       - id:        # identifier / name of the span
         path:      # path to storage
         size:      # size in bytes, required for file system storage, optional for raw device
         hash_seed: # optional, used to isolate lookup from path changes
     volumes:       # optional
       - id:        # identifier [1-255]
         size:      # optional, size in percentage
         scheme:    # optional, default to "http"
         ram_cache: # optional, default to "true"
         spans:     # optional
           - use:   # Span identifier
             size:  # size allocated to this volume
 */
std::set<std::string> valid_cache_config_keys         = {"spans", "volumes"};
std::set<std::string> valid_spans_config_keys         = {"id", "path", "size", "hash_seed"};
std::set<std::string> valid_volumes_config_keys       = {"id", "size", "scheme", "ram_cache", "spans"};
std::set<std::string> valid_volumes_spans_config_keys = {"use", "size"};

bool
validate_map(const YAML::Node &node, const std::set<std::string> &keys)
{
  if (!node.IsMap()) {
    throw YAML::ParserException(node.Mark(), "malformed entry");
  }

  for (const auto &item : node) {
    if (std::none_of(keys.begin(), keys.end(), [&item](const std::string &s) { return s == item.first.as<std::string>(); })) {
      throw YAML::ParserException(item.first.Mark(), "format: unsupported key '" + item.first.as<std::string>() + "'");
    }
  }

  return true;
}

bool
parse_volume_scheme(ConfigVol &volume, const std::string &scheme)
{
  if (scheme.compare("http") == 0) {
    volume.scheme = CACHE_HTTP_TYPE;
    return true;
  } else {
    volume.scheme = CACHE_NONE_TYPE;
    return false;
  }
}

/**
  Convert given @s into ConfigVol::Size.
  @s can be parcent (%), human readable unit (K/M/G/T) or absolute number.
 */
bool
parse_size(ConfigVol::Size &size, const std::string &s)
{
  // s.ends_with('%')
  if (s[s.length() - 1] == '%') {
    int value = std::stoi(s.substr(0, s.length() - 1));
    if (value > 100) {
      return false;
    }
    size.in_percent = true;
    size.percent    = value;
  } else {
    size.in_percent     = false;
    size.absolute_value = ink_atoi64(s.c_str());
  }

  return true;
}
} // namespace

namespace YAML
{
////
// spans
//
template <> struct convert<SpanConfig> {
  static bool
  decode(const Node &node, SpanConfig &spans)
  {
    if (!node.IsSequence()) {
      Error("malformed data; expected sequence in cache.spans");
      return false;
    }

    for (const auto &it : node) {
      spans.push_back(it.as<SpanConfigParams>());
    }

    return true;
  }
};

template <> struct convert<SpanConfigParams> {
  static bool
  decode(const Node &node, SpanConfigParams &span)
  {
    validate_map(node, valid_spans_config_keys);

    if (!node["id"]) {
      throw ParserException(node.Mark(), "missing 'id' argument in cache.spans[]");
    }
    span.id = node["id"].as<std::string>();

    if (!node["path"]) {
      throw ParserException(node.Mark(), "missing 'path' argument in cache.spans[]");
    }
    span.path = node["path"].as<std::string>();

    // optional configs
    if (node["size"]) {
      std::string size = node["size"].as<std::string>();
      span.size        = ink_atoi64(size.c_str());
    }

    if (node["hash_seed"]) {
      span.hash_seed = node["hash_seed"].as<std::string>();
    };

    return true;
  }
};

////
// volumes
//
template <> struct convert<ConfigVolumes> {
  static bool
  decode(const Node &node, ConfigVolumes &volumes)
  {
    if (!node.IsSequence()) {
      Error("malformed data; expected sequence in cache.volumes");
      return false;
    }

    int total_percent = 0;
    for (const auto &it : node) {
      ConfigVol volume = it.as<ConfigVol>();

      if (volume.size.in_percent) {
        total_percent += volume.size.percent;
      }

      if (total_percent > 100) {
        Error("Total volume size added up to more than 100 percent");
        return false;
      }

      volumes.cp_queue.enqueue(new ConfigVol(volume));
      volumes.num_volumes++;
      volumes.num_http_volumes++;
    }

    return true;
  }
};

template <> struct convert<ConfigVol> {
  static bool
  decode(const Node &node, ConfigVol &volume)
  {
    validate_map(node, valid_volumes_config_keys);

    if (!node["id"]) {
      throw ParserException(node.Mark(), "missing 'id' argument in cache.volumes[]");
    }
    volume.number = node["id"].as<int>();

    // optional configs
    if (node["scheme"]) {
      std::string scheme = node["scheme"].as<std::string>();
      if (!parse_volume_scheme(volume, scheme)) {
        throw ParserException(node.Mark(), "error on parsing 'scheme: " + scheme + "' in cache.volumes[]");
      }
    } else {
      // default scheme is http
      volume.scheme = CACHE_HTTP_TYPE;
    }

    if (node["size"]) {
      std::string size = node["size"].as<std::string>();
      if (!parse_size(volume.size, size)) {
        throw ParserException(node.Mark(), "error on parsing 'size: " + size + "' in cache.volumes[]");
      }
    }

    if (node["ram_cache"]) {
      volume.ramcache_enabled = node["ram_cache"].as<bool>();
    }

    if (node["spans"]) {
      volume.spans = node["spans"].as<ConfigVol::Spans>();
    }

    return true;
  }
};

template <> struct convert<ConfigVol::Spans> {
  static bool
  decode(const Node &node, ConfigVol::Spans &spans)
  {
    if (!node.IsSequence()) {
      Error("malformed data; expected sequence in cache.volumes[].spans");
      return false;
    }

    for (const auto &it : node) {
      spans.push_back(it.as<ConfigVol::Span>());
    }

    return true;
  }
};

template <> struct convert<ConfigVol::Span> {
  static bool
  decode(const Node &node, ConfigVol::Span &span)
  {
    validate_map(node, valid_volumes_spans_config_keys);

    if (!node["use"]) {
      throw ParserException(node.Mark(), "missing 'use' argument in cache.volumes[].spans[]");
    }
    span.use = node["use"].as<std::string>();

    if (node["size"]) {
      std::string size = node["size"].as<std::string>();
      if (!parse_size(span.size, size)) {
        throw ParserException(node.Mark(), "error on parsing 'size: " + size + "' in cache.volumes[].spans[]");
      }
    }

    return true;
  }
};
} // namespace YAML

/**
  ConfigVolume unit test helper
 */
bool
YamlStorageConfig::load_volumes(ConfigVolumes &v_config, std::string &yaml)
{
  try {
    YAML::Node node = YAML::Load(yaml);
    v_config        = node["volumes"].as<ConfigVolumes>();
    v_config.complement();
  } catch (std::exception &ex) {
    Error("%s", ex.what());
    return false;
  }

  return true;
}
/**
  Load storage.yaml as SpanConfig and ConfigVolumes
 */
bool
YamlStorageConfig::load(SpanConfig &s_config, ConfigVolumes &v_config, std::string_view filename)
{
  try {
    YAML::Node config = YAML::LoadFile(filename.data());

    if (config.IsNull()) {
      return false;
    }

    YAML::Node cache_config = config["cache"];

    validate_map(cache_config, valid_cache_config_keys);

    s_config = cache_config["spans"].as<SpanConfig>();

    if (cache_config["volumes"]) {
      v_config = cache_config["volumes"].as<ConfigVolumes>();
      v_config.complement();
    }
  } catch (std::exception &ex) {
    Error("%s", ex.what());
    return false;
  }

  return true;
}
