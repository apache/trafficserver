/** @file

  JA3 provider for Abuse Shield.

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

#include "method.h"

#include <algorithm>
#include <cctype>

#include "ja3/fingerprint.h"

namespace
{
std::optional<std::string>
canonicalize(std::string_view value)
{
  if (value.size() != 32 ||
      !std::all_of(value.begin(), value.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
    return std::nullopt;
  }

  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
  return result;
}
} // namespace

const abuse_shield::FingerprintMethod abuse_shield::fingerprints::ja3::method{
  "JA3",
  ::ja3::fingerprint,
  canonicalize,
};
