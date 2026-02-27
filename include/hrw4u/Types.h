/*
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

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <set>

#include "hrw4u/ObjTypes.h"
#include <cstdint>

namespace hrw4u
{

enum class SectionType : uint8_t {
  UNKNOWN       = 0,
  READ_REQUEST  = 1,
  SEND_REQUEST  = 2,
  READ_RESPONSE = 3,
  SEND_RESPONSE = 4,
  PRE_REMAP     = 5,
  POST_REMAP    = 6,
  REMAP         = 7,
  TXN_START     = 8,
  TXN_CLOSE     = 9,
};

[[nodiscard]] std::string_view section_type_to_string(SectionType type);
[[nodiscard]] SectionType      section_type_from_string(std::string_view name);

enum class VarType : uint8_t {
  BOOL  = 0,
  INT8  = 1,
  INT16 = 2,
};

struct VarTypeInfo {
  std::string_view  name;
  std::string_view  cond_tag;
  std::string_view  op_tag;
  hrw::OperatorType op_type;
  int               limit;
};

[[nodiscard]] const VarTypeInfo     &var_type_info(VarType type);
[[nodiscard]] std::string_view       var_type_to_string(VarType type);
[[nodiscard]] std::optional<VarType> var_type_from_string(std::string_view name);

enum class SuffixGroup : uint8_t {
  URL_FIELDS,
  HTTP_CNTL_FIELDS,
  CONN_FIELDS,
  GEO_FIELDS,
  ID_FIELDS,
  DATE_FIELDS,
  CERT_FIELDS,
  SAN_FIELDS,
  BOOL_FIELDS,
  PLUGIN_CNTL_FIELDS,
};

[[nodiscard]] bool                                 validate_suffix(SuffixGroup group, std::string_view suffix);
[[nodiscard]] const std::vector<std::string_view> &get_valid_suffixes(SuffixGroup group);

// Utility functions for case conversion
inline std::string
to_lower(std::string_view s)
{
  std::string result;

  result.reserve(s.size());
  for (char c : s) {
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }

  return result;
}

inline std::string
to_upper(std::string_view s)
{
  std::string result;

  result.reserve(s.size());
  for (char c : s) {
    result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }

  return result;
}

struct Variable {
  std::string name;
  VarType     type = VarType::BOOL;
  int         slot = -1;

  [[nodiscard]] bool
  has_explicit_slot() const
  {
    return slot >= 0;
  }
};

using SectionSet = std::set<SectionType>;

} // namespace hrw4u
