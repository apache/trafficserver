/** @file

  Implementation file for YamlCfg.h.

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

#include "YamlCfg.h"

#include <algorithm>
#include <string>

#include <tscore/ink_assert.h>

namespace ts
{
namespace Yaml
{
  Map::Map(const YAML::Node &map) : _map{map}
  {
    if (!_map.IsMap()) {
      throw YAML::ParserException(_map.Mark(), "map expected");
    }
  }

  YAML::Node
  Map::operator[](std::string_view key)
  {
    auto n = _map[std::string(key)];

    if (n) {
      // Add key to _used_key if not in it already.
      //
      if (std::find(_used_key.begin(), _used_key.end(), key) == _used_key.end()) {
        _used_key.push_back(key);
      }
    }

    return n;
  }

  void
  Map::done()
  {
    if (!_bad && (_used_key.size() != _map.size())) {
      ink_assert(_used_key.size() < _map.size());

      std::string msg{(_map.size() - _used_key.size()) > 1 ? "keys " : "key "};
      bool first{true};

      for (auto const &kv : _map) {
        auto key = kv.first.as<std::string>();

        if (std::find(_used_key.begin(), _used_key.end(), key) == _used_key.end()) {
          if (!first) {
            msg += ", ";
          }
          first = false;
          msg += key;
        }
      }
      throw YAML::ParserException(_map.Mark(), msg + " invalid in this map");
    }
  }

} // end namespace Yaml
} // end namespace ts
