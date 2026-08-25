/** @file

  JA4 provider for Abuse Shield.

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

#include "ja4/fingerprint.h"
#include "ja4/ja4.h"

namespace
{
std::optional<std::string>
canonicalize(std::string_view value)
{
  if (value.size() != ja4::FINGERPRINT_LENGTH || value[ja4::DELIMITER_1_POSITION] != ja4::PORTION_DELIMITER ||
      value[ja4::DELIMITER_2_POSITION] != ja4::PORTION_DELIMITER ||
      !std::all_of(value.begin(), value.end(), [](char c) { return c >= '!' && c <= '~'; })) {
    return std::nullopt;
  }
  return std::string(value);
}
} // namespace

const abuse_shield::FingerprintMethod abuse_shield::fingerprints::ja4::method{
  "JA4",
  ::ja4::fingerprint,
  canonicalize,
};
