/** @file

  Define the type used to store user-sensitive HTTP fields (such as cookies).

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

#pragma once

#include <algorithm>
#include <strings.h>
#include <string>
#include <string_view>
#include <unordered_set>

namespace traffic_dump
{
// A case-insensitive comparator used for comparing HTTP field names.
struct InsensitiveCompare {
  bool
  operator()(std::string_view a, std::string_view b) const
  {
    return strcasecmp(a.data(), b.data()) == 0;
  }
};

// A case-insensitive hash functor for HTTP field names.
struct StringHashByLower {
public:
  size_t
  operator()(std::string_view str) const
  {
    std::string lower;
    std::transform(str.begin(), str.end(), lower.begin(), [](unsigned char c) -> unsigned char { return std::tolower(c); });
    return std::hash<std::string>()(lower);
  }
};

/** The type used to store the set of user-sensitive HTTP fields, such as
 * "Cookie" and "Set-Cookie". */
using sensitive_fields_t = std::unordered_set<std::string, StringHashByLower, InsensitiveCompare>;

} // namespace traffic_dump
