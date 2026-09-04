/** @file

  Versioned inter-plugin registry for JAx fingerprint results.

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
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied.  See the License for the specific language governing
  permissions and limitations under the License.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace jax_fingerprint
{

inline constexpr uint32_t REGISTRY_MAGIC         = 0x4a415852; // "JAXR"
inline constexpr uint16_t REGISTRY_ABI_VERSION   = 1;
inline constexpr char     REGISTRY_DESCRIPTION[] = "JAx fingerprint registry ABI v1";

/** One immutable method/value pair exported for a connection or transaction. */
struct RegistryEntryV1 {
  const char *method;
  const char *value;
  uint32_t    method_length;
  uint32_t    value_length;
};

/** A read-only view of all JAx fingerprints associated with one ATS object.
 *
 * The producer owns the registry, its entry array, and all referenced strings.
 * Consumers must not retain pointers beyond the lifetime of the VConn or
 * transaction from which the registry was retrieved.
 */
struct RegistryV1 {
  uint32_t               magic;
  uint16_t               abi_version;
  uint16_t               struct_size;
  uint16_t               entry_size;
  uint16_t               reserved;
  uint32_t               entry_count;
  const RegistryEntryV1 *entries;
};

static_assert(std::is_standard_layout_v<RegistryEntryV1>);
static_assert(std::is_standard_layout_v<RegistryV1>);

inline bool
is_valid(const RegistryV1 *registry)
{
  return registry != nullptr && registry->magic == REGISTRY_MAGIC && registry->abi_version == REGISTRY_ABI_VERSION &&
         registry->struct_size >= sizeof(RegistryV1) && registry->entry_size >= sizeof(RegistryEntryV1) &&
         registry->entry_size % alignof(RegistryEntryV1) == 0 && (registry->entry_count == 0 || registry->entries != nullptr);
}

inline const RegistryEntryV1 *
entry_at(const RegistryV1 *registry, uint32_t index)
{
  if (!is_valid(registry) || index >= registry->entry_count) {
    return nullptr;
  }

  auto *entries = reinterpret_cast<const unsigned char *>(registry->entries);
  return reinterpret_cast<const RegistryEntryV1 *>(entries + static_cast<std::size_t>(index) * registry->entry_size);
}

inline std::string_view
find(const RegistryV1 *registry, std::string_view method)
{
  if (!is_valid(registry)) {
    return {};
  }

  for (uint32_t index = 0; index < registry->entry_count; ++index) {
    const auto *entry = entry_at(registry, index);
    if (entry->method != nullptr && entry->value != nullptr && std::string_view(entry->method, entry->method_length) == method) {
      return {entry->value, entry->value_length};
    }
  }
  return {};
}

} // namespace jax_fingerprint
