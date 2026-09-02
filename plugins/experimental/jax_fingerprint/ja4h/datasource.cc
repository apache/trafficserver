/** @file

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

#include "datasource.h"

#include <cctype>
#include <algorithm>

bool
Datasource::_should_include_field(std::string_view name)
{
  constexpr std::string_view COOKIE{"cookie"};
  constexpr std::string_view REFERER{"referer"};

  if (name.length() == COOKIE.length()) {
    if (std::equal(name.begin(), name.end(), COOKIE.begin(), [](char c1, char c2) { return std::tolower(c1) == c2; })) {
      return false;
    }
  } else if (name.length() == REFERER.length()) {
    if (std::equal(name.begin(), name.end(), REFERER.begin(), [](char c1, char c2) { return std::tolower(c1) == c2; })) {
      return false;
    }
  }

  return true;
}
