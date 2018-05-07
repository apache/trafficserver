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

#include <string_view>
#include <unordered_map>

#include "ts/HashFNV.h"

/// Hash functor for @c string_view
inline size_t
TsLuaConfigSVHash(std::string_view const &sv)
{
  ATSHash64FNV1a h;
  h.update(sv.data(), sv.size());
  return h.get();
}

class TsEnumDescriptor
{
public:
  struct Pair {
    std::string_view key;
    int value;
  };
  TsEnumDescriptor(std::initializer_list<Pair> pairs) : values{pairs.size(), &TsLuaConfigSVHash}, keys{pairs.size()}
  {
    for (auto &p : pairs) {
      values[p.key] = p.value;
      keys[p.value] = p.key;
    }
  }
  std::unordered_map<std::string_view, int, size_t (*)(std::string_view const &)> values;
  std::unordered_map<int, std::string_view> keys;
  int
  get(std::string_view key)
  {
    auto it = values.find(key);
    if (it != values.end()) {
      return it->second;
    }
    return -1;
  }
};
