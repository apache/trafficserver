/** @file

  TLS ClientHello fingerprint provider lookup.

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
} // namespace

const abuse_shield::FingerprintMethod *
abuse_shield::find_fingerprint_method(std::string_view name)
{
  for (const auto *method : fingerprint_methods()) {
    if (names_equal(method->name, name)) {
      return method;
    }
  }
  return nullptr;
}
