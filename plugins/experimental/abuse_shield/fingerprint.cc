/** @file

  TLS ClientHello fingerprint configuration helpers.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements. See the NOTICE file distributed with this work for additional information regarding
  copyright ownership. Licensed under the Apache License, Version 2.0 (the "License"); you may not
  use this file except in compliance with the License. You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

#include "fingerprint.h"

#include <algorithm>
#include <cctype>

namespace
{
bool
names_equal(std::string_view lhs, std::string_view rhs)
{
  return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char lhs_char, char rhs_char) {
           return std::tolower(static_cast<unsigned char>(lhs_char)) == std::tolower(static_cast<unsigned char>(rhs_char));
         });
}

bool
is_method_name(std::string_view value)
{
  constexpr size_t MAX_METHOD_NAME_LENGTH = 64;

  return !value.empty() && value.size() <= MAX_METHOD_NAME_LENGTH && std::all_of(value.begin(), value.end(), [](char c) {
    auto uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_' || c == '-' || c == '.';
  });
}

bool
is_opaque_value(std::string_view value)
{
  constexpr size_t MAX_FINGERPRINT_LENGTH = 4096;

  return !value.empty() && value.size() <= MAX_FINGERPRINT_LENGTH && std::all_of(value.begin(), value.end(), [](char c) {
    auto uc = static_cast<unsigned char>(c);
    return uc >= static_cast<unsigned char>(' ') && uc <= static_cast<unsigned char>('~');
  });
}
} // namespace

std::optional<abuse_shield::ConfiguredFingerprint>
abuse_shield::canonicalize_fingerprint(std::string_view method, std::string_view value)
{
  if (!is_method_name(method)) {
    return std::nullopt;
  }

  if (names_equal(method, "JA3")) {
    if (value.size() != 32 ||
        !std::all_of(value.begin(), value.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
      return std::nullopt;
    }

    ConfiguredFingerprint result{"JA3", std::string(value)};
    std::transform(result.value.begin(), result.value.end(), result.value.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return result;
  }

  if (names_equal(method, "JA4")) {
    constexpr size_t JA4_LENGTH = 36;
    if (value.size() != JA4_LENGTH || value[10] != '_' || value[23] != '_' || !is_opaque_value(value)) {
      return std::nullopt;
    }
    return ConfiguredFingerprint{"JA4", std::string(value)};
  }

  if (!is_opaque_value(value)) {
    return std::nullopt;
  }
  return ConfiguredFingerprint{std::string(method), std::string(value)};
}
