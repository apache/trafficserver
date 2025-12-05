/** @file

  Map of transaction overridable configuration variables and names.

  This file uses the X-macro definitions from OverridableConfigDefs.h to
  auto-generate the mapping. To add a new overridable config, add an entry
  to the OVERRIDABLE_CONFIGS macro in that header file.

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

#include "shared/overridable_txn_vars.h"

#include <cstddef>

#include "iocore/net/ConnectionTracker.h"
#include "proxy/http/OverridableConfigDefs.h"

// ============================================================================
// Compile-time validation that OVERRIDABLE_CONFIGS order matches the
// TSOverridableConfigKey enum order in apidefs.h.in.
// ============================================================================

// clang-format off

// Generate an array of enum values from the X-macro in definition order.
static constexpr TSOverridableConfigKey xmacro_enum_order[] = {
#define X_ENUM_ORDER(CONFIG_KEY, MEMBER, RECORD_NAME, DATA_TYPE, CONV) TS_CONFIG_##CONFIG_KEY,
  OVERRIDABLE_CONFIGS(X_ENUM_ORDER)
#undef X_ENUM_ORDER
};

// clang-format on

// Recursive constexpr function to verify each X-macro entry's enum value
// equals its position (i.e., the X-macro order matches enum order).
template <std::size_t N>
constexpr bool
check_xmacro_order(const TSOverridableConfigKey (&arr)[N], std::size_t i = 0)
{
  return i >= N || (arr[i] == static_cast<TSOverridableConfigKey>(i) && check_xmacro_order(arr, i + 1));
}

// Verify the count matches.
static_assert(sizeof(xmacro_enum_order) / sizeof(xmacro_enum_order[0]) == TS_CONFIG_LAST_ENTRY,
              "OVERRIDABLE_CONFIGS entry count must match TS_CONFIG_LAST_ENTRY. "
              "Did you forget to add the enum to apidefs.h.in or the X-macro entry?");

// Verify the order matches.
static_assert(check_xmacro_order(xmacro_enum_order),
              "OVERRIDABLE_CONFIGS order must match TSOverridableConfigKey enum order in apidefs.h.in. "
              "Ensure entries are in the same order in both files.");
// ============================================================================
// End of compile-time validation.
// ============================================================================

// ============================================================================
// Configuration string name to enum and type mapping.
// ============================================================================

// clang-format off
const std::unordered_map<std::string_view, std::tuple<const TSOverridableConfigKey, const TSRecordDataType>>
  ts::Overridable_Txn_Vars({

/** Use OVERRIDABLE_CONFIGS to populate the map with entries like:
 * ...
 * "proxy.config.http.chunking_enabled", {TS_CONFIG_HTTP_CHUNKING_ENABLED, TS_RECORDDATATYPE_INT}
 * "proxy.config.http.negative_caching_list", {TS_CONFIG_HTTP_NEGATIVE_CACHING_LIST, TS_RECORDDATATYPE_STRING}
 * ...
 */
#define X_TXN_VAR(CONFIG_KEY, MEMBER, RECORD_NAME, DATA_TYPE, CONV) \
    {RECORD_NAME, {TS_CONFIG_##CONFIG_KEY, TS_RECORDDATATYPE_##DATA_TYPE}},
    OVERRIDABLE_CONFIGS(X_TXN_VAR)
#undef X_TXN_VAR
});
// clang-format on
