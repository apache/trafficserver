// Copyright 2016-2019 Envoy Project Authors
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace proxy_wasm {

using Pairs = std::vector<std::pair<std::string_view, std::string_view>>;
using StringPairs = std::vector<std::pair<std::string, std::string>>;

class PairsUtil {
public:
  /**
   * pairsSize returns the buffer size required to serialize Pairs.
   * @param pairs Pairs to serialize.
   * @return size of the output buffer.
   */
  static size_t pairsSize(const Pairs &pairs);

  static size_t pairsSize(const StringPairs &stringpairs) {
    Pairs views(stringpairs.begin(), stringpairs.end());
    return pairsSize(views);
  }

  /**
   * marshalPairs serializes Pairs to output buffer.
   * @param pairs Pairs to serialize.
   * @param buffer output buffer.
   * @param size size of the output buffer.
   * @return indicates whether serialization succeeded or not.
   */
  static bool marshalPairs(const Pairs &pairs, char *buffer, size_t size);

  static bool marshalPairs(const StringPairs &stringpairs, char *buffer, size_t size) {
    Pairs views(stringpairs.begin(), stringpairs.end());
    return marshalPairs(views, buffer, size);
  }

  /**
   * toPairs deserializes input buffer to Pairs.
   * @param buffer serialized input buffer.
   * @return deserialized Pairs or an empty instance in case of deserialization failure.
   */
  static Pairs toPairs(std::string_view buffer);
};

} // namespace proxy_wasm
