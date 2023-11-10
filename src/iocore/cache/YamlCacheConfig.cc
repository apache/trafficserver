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

#include "iocore/cache/YamlCacheConfig.h"

#include "P_Cache.h"

#include "tscore/Diags.h"

#include <yaml-cpp/yaml.h>

#include <set>
#include <string>

namespace
{
std::set<std::string> valid_volume_config_keys = {"volume", "scheme", "size", "ramcache"};

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

bool
parse_volume_size(ConfigVol &volume, const std::string &s)
{
  // s.ends_with('%')
  if (s[s.length() - 1] == '%') {
    volume.in_percent = true;
    int value         = std::stoi(s.substr(0, s.length() - 1));
    if (value > 100) {
      return false;
    }
    volume.percent = value;
  } else {
    volume.in_percent = false;
    volume.size       = std::stoi(s);
  }

  return true;
}
} // namespace

namespace YAML
{
template <> struct convert<ConfigVol> {
  static bool
  decode(const Node &node, ConfigVol &volume)
  {
    if (!node.IsMap()) {
      throw ParserException(node.Mark(), "malformed entry");
    }

    for (const auto &item : node) {
      if (std::none_of(valid_volume_config_keys.begin(), valid_volume_config_keys.end(),
                       [&item](const std::string &s) { return s == item.first.as<std::string>(); })) {
        throw ParserException(item.first.Mark(), "format: unsupported key '" + item.first.as<std::string>() + "'");
      }
    }

    if (!node["volume"]) {
      throw ParserException(node.Mark(), "missing 'volume' argument");
    }
    volume.number = node["volume"].as<int>();

    if (!node["scheme"]) {
      throw ParserException(node.Mark(), "missing 'scheme' argument");
    }
    std::string scheme = node["scheme"].as<std::string>();
    if (!parse_volume_scheme(volume, scheme)) {
      throw ParserException(node.Mark(), "error on parsing 'scheme: " + scheme + "'");
    }

    if (!node["size"]) {
      throw ParserException(node.Mark(), "missing 'size' argument");
    }
    std::string size = node["size"].as<std::string>();
    if (!parse_volume_size(volume, size)) {
      throw ParserException(node.Mark(), "error on parsing 'size: " + size + "'");
    }

    // optional configs
    if (node["ramcache"]) {
      volume.ramcache_enabled = node["ramcache"].as<bool>();
    }

    return true;
  };
};
} // namespace YAML

bool
YamlVolumeConfig::load(ConfigVolumes &config_v, std::string_view filename)
{
  try {
    YAML::Node config = YAML::LoadFile(filename.data());

    if (config.IsNull()) {
      return false;
    }

    YAML::Node volumes = config["volumes"];
    if (!volumes) {
      Error("malformed %s file; expected a toplevel 'volumes' node", filename.data());
      return false;
    }

    if (!volumes.IsSequence()) {
      Error("malformed %s file; expected sequence", filename.data());
      return false;
    }

    int total_percent = 0;
    for (const auto &it : volumes) {
      ConfigVol volume = it.as<ConfigVol>();

      if (volume.in_percent) {
        total_percent += volume.percent;
      }

      if (total_percent > 100) {
        Error("Total volume size added up to more than 100 percent");
        return false;
      }

      config_v.cp_queue.enqueue(new ConfigVol(volume));
      config_v.num_volumes++;
      config_v.num_http_volumes++;
    }
  } catch (std::exception &ex) {
    Error("%s", ex.what());
    return false;
  }

  return true;
}
