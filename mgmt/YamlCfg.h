/** @file

  Utilities to help with parsing YAML files with good error reporting.

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

#pragma once

#include <vector>
#include <string_view>

#include <yaml-cpp/yaml.h>

namespace ts
{
namespace Yaml
{
  // A class that is a wrapper for a YAML::Node that corresponds to a map in a YAML input file.
  // It's purpose is to make sure all keys in the map are processed.
  //
  class Map
  {
  public:
    // A YAML::ParserException will be thrown if 'map' isn't actually a map.
    //
    explicit Map(const YAML::Node &map);

    // Get the node for a key.  Throw a YAML::Exception if 'key' is not in the map.  The node for each key in the
    // map must be gotten at least once.  The lifetime of the char array referenced by passed key must be as long
    // as this instance.
    //
    YAML::Node operator[](std::string_view key);

    // Call this after the last call to the [] operator.  Will throw a YAML::ParserException if instance not
    // already marked bad, and all keys in the map were not accessed at least once with the [] operator.  The
    // 'what' of the exception will list the keys that were not accessed as invalid for the map.
    //
    void done();

    // Mark instance as bad.
    //
    void
    bad()
    {
      _bad = true;
    }

    // No copy/move.
    //
    Map(Map const &) = delete;
    Map &operator=(Map const &) = delete;

  private:
    YAML::Node _map;
    std::vector<std::string_view> _used_key;
    bool _bad{false};
  };

} // end namespace Yaml
} // end namespace ts
