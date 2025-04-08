/* @file

  Implementation for creating all values.

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

#include <string>
#include <algorithm>

#include "matcher.h"

// Special case for strings, to allow for insensitive case comparisons for std::string matchers.
template <>
bool
Matchers<std::string>::test_eq(const std::string &t) const
{
  std::string_view lhs    = std::get<std::string>(_data);
  std::string_view rhs    = t;
  bool             result = false;

  // ToDo: in C++20, we should be able to use std::ranges::equal, but this breaks on Ubuntu CI
  // return std::ranges::equal(a, b, [](char c1, char c2) {
  //   return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
  // });
  // Case-aware comparison
  auto compare = [&](const std::string_view a, const std::string_view b) -> bool {
    if (has_modifier(_mods, CondModifiers::MOD_NOCASE)) {
      return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char c1, char c2) {
               return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
             });
    }
    return a == b;
  };

  // Case-aware substring match
  auto contains = [&](const std::string_view haystack, const std::string_view &needle) -> bool {
    if (!has_modifier(_mods, CondModifiers::MOD_NOCASE)) {
      return haystack.find(needle) != std::string_view::npos;
    }
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char c1, char c2) {
      return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
    });
    return it != haystack.end();
  };

  if (has_modifier(_mods, CondModifiers::MOD_EXT)) {
    auto dot = rhs.rfind('.');
    if (dot != std::string_view::npos && dot + 1 < rhs.size()) {
      result = compare(rhs.substr(dot + 1), lhs);
    }
  } else if (has_modifier(_mods, CondModifiers::MOD_SUF)) {
    if (rhs.size() >= lhs.size()) {
      result = compare(rhs.substr(rhs.size() - lhs.size()), lhs);
    }
  } else if (has_modifier(_mods, CondModifiers::MOD_PRE)) {
    if (rhs.size() >= lhs.size()) {
      result = compare(rhs.substr(0, lhs.size()), lhs);
    }
  } else if (has_modifier(_mods, CondModifiers::MOD_MID)) {
    result = contains(rhs, lhs);
  } else {
    if (rhs.size() == lhs.size()) {
      result = compare(rhs, lhs);
    }
  }

  if (pi_dbg_ctl.on()) {
    debug_helper(t, " == ", result);
  }

  return result;
}

template <>
bool
Matchers<const sockaddr *>::test(const sockaddr *const &addr, const Resources & /* Not used */) const
{
  TSAssert(std::holds_alternative<swoc::IPRangeSet>(_data));
  const auto &ranges = std::get<swoc::IPRangeSet>(_data);

  if (ranges.contains(swoc::IPAddr(addr))) {
    if (pi_dbg_ctl.on()) {
      char text[INET6_ADDRSTRLEN];
      Dbg(pi_dbg_ctl, "Successfully found IP-range match on %s", getIP(addr, text));
    }
    return true;
  }

  return false;
}
