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

#include "include/proxy-wasm/pairs_util.h"

#include <cstring>
#include <string_view>
#include <vector>

#include "include/proxy-wasm/exports.h"
#include "include/proxy-wasm/limits.h"
#include "include/proxy-wasm/word.h"

namespace proxy_wasm {

using Sizes = std::vector<std::pair<uint32_t, uint32_t>>;

size_t PairsUtil::pairsSize(const Pairs &pairs) {
  size_t size = sizeof(uint32_t); // number of headers
  for (const auto &p : pairs) {
    size += 2 * sizeof(uint32_t); // size of name, size of value
    size += p.first.size() + 1;   // NULL-terminated name
    size += p.second.size() + 1;  // NULL-terminated value
  }
  return size;
}

bool PairsUtil::marshalPairs(const Pairs &pairs, char *buffer, size_t size) {
  if (buffer == nullptr) {
    return false;
  }

  char *pos = buffer;
  const char *end = buffer + size;

  // Write number of pairs.
  uint32_t num_pairs =
      htowasm(pairs.size(), contextOrEffectiveContext() != nullptr
                                ? contextOrEffectiveContext()->wasmVm()->usesWasmByteOrder()
                                : false);
  if (pos + sizeof(uint32_t) > end) {
    return false;
  }
  ::memcpy(pos, &num_pairs, sizeof(uint32_t));
  pos += sizeof(uint32_t);

  for (const auto &p : pairs) {
    // Write name length.
    uint32_t name_len =
        htowasm(p.first.size(), contextOrEffectiveContext() != nullptr
                                    ? contextOrEffectiveContext()->wasmVm()->usesWasmByteOrder()
                                    : false);
    if (pos + sizeof(uint32_t) > end) {
      return false;
    }
    ::memcpy(pos, &name_len, sizeof(uint32_t));
    pos += sizeof(uint32_t);

    // Write value length.
    uint32_t value_len =
        htowasm(p.second.size(), contextOrEffectiveContext() != nullptr
                                     ? contextOrEffectiveContext()->wasmVm()->usesWasmByteOrder()
                                     : false);
    if (pos + sizeof(uint32_t) > end) {
      return false;
    }
    ::memcpy(pos, &value_len, sizeof(uint32_t));
    pos += sizeof(uint32_t);
  }

  for (const auto &p : pairs) {
    // Write name.
    if (pos + p.first.size() + 1 > end) {
      return false;
    }
    ::memcpy(pos, p.first.data(), p.first.size());
    pos += p.first.size();
    *pos++ = '\0'; // NULL-terminated string.

    // Write value.
    if (pos + p.second.size() + 1 > end) {
      return false;
    }
    ::memcpy(pos, p.second.data(), p.second.size());
    pos += p.second.size();
    *pos++ = '\0'; // NULL-terminated string.
  }

  return pos == end;
}

Pairs PairsUtil::toPairs(std::string_view buffer) {
  if (buffer.data() == nullptr || buffer.size() > PROXY_WASM_HOST_PAIRS_MAX_BYTES) {
    return {};
  }

  const char *pos = buffer.data();
  const char *end = buffer.data() + buffer.size();

  // Read number of pairs.
  if (pos + sizeof(uint32_t) > end) {
    return {};
  }
  uint32_t num_pairs = wasmtoh(*reinterpret_cast<const uint32_t *>(pos),
                               contextOrEffectiveContext() != nullptr
                                   ? contextOrEffectiveContext()->wasmVm()->usesWasmByteOrder()
                                   : false);
  pos += sizeof(uint32_t);

  // Check if we're not going to exceed the limit.
  if (num_pairs > PROXY_WASM_HOST_PAIRS_MAX_COUNT) {
    return {};
  }
  if (pos + num_pairs * 2 * sizeof(uint32_t) > end) {
    return {};
  }

  Sizes sizes;
  sizes.resize(num_pairs);

  for (auto &s : sizes) {
    // Read name length.
    if (pos + sizeof(uint32_t) > end) {
      return {};
    }
    s.first = wasmtoh(*reinterpret_cast<const uint32_t *>(pos),
                      contextOrEffectiveContext() != nullptr
                          ? contextOrEffectiveContext()->wasmVm()->usesWasmByteOrder()
                          : false);
    pos += sizeof(uint32_t);

    // Read value length.
    if (pos + sizeof(uint32_t) > end) {
      return {};
    }
    s.second = wasmtoh(*reinterpret_cast<const uint32_t *>(pos),
                       contextOrEffectiveContext() != nullptr
                           ? contextOrEffectiveContext()->wasmVm()->usesWasmByteOrder()
                           : false);
    pos += sizeof(uint32_t);
  }

  Pairs pairs;
  pairs.resize(num_pairs);

  for (uint32_t i = 0; i < num_pairs; i++) {
    auto &s = sizes[i];
    auto &p = pairs[i];

    // Don't overread.
    if (pos + s.first + 1 > end) {
      return {};
    }
    p.first = std::string_view(pos, s.first);
    pos += s.first;
    if (*pos++ != '\0') { // NULL-terminated string.
      return {};
    }

    // Don't overread.
    if (pos + s.second + 1 > end) {
      return {};
    }
    p.second = std::string_view(pos, s.second);
    pos += s.second;
    if (*pos++ != '\0') { // NULL-terminated string.
      return {};
    }
  }

  if (pos != end) {
    return {};
  }

  return pairs;
}

} // namespace proxy_wasm
