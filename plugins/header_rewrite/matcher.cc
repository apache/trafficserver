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

static bool
match_with_modifiers(std::string_view rhs, std::string_view lhs, CondModifiers mods)
{
  // Case-aware equality
  static auto equals = [](std::string_view a, std::string_view b, CondModifiers mods) -> bool {
    if (has_modifier(mods, CondModifiers::MOD_NOCASE)) {
      return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char c1, char c2) {
               return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
             });
    }
    return a == b;
  };

  // Case-aware substring search
  static auto contains = [](std::string_view haystack, std::string_view needle, CondModifiers mods) -> bool {
    if (!has_modifier(mods, CondModifiers::MOD_NOCASE)) {
      return haystack.find(needle) != std::string_view::npos;
    }
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char c1, char c2) {
      return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
    });
    return it != haystack.end();
  };

  if (has_modifier(mods, CondModifiers::MOD_EXT)) {
    auto dot = rhs.rfind('.');
    return dot != std::string_view::npos && dot + 1 < rhs.size() && equals(rhs.substr(dot + 1), lhs, mods);
  }

  if (has_modifier(mods, CondModifiers::MOD_SUF)) {
    return rhs.size() >= lhs.size() && equals(rhs.substr(rhs.size() - lhs.size()), lhs, mods);
  }

  if (has_modifier(mods, CondModifiers::MOD_PRE)) {
    return rhs.size() >= lhs.size() && equals(rhs.substr(0, lhs.size()), lhs, mods);
  }

  if (has_modifier(mods, CondModifiers::MOD_MID)) {
    return contains(rhs, lhs, mods);
  }

  return equals(rhs, lhs, mods);
}

// Special case for strings, to allow for insensitive case comparisons for std::string matchers.
template <>
bool
Matchers<std::string>::test_eq(const std::string &t) const
{
  std::string_view lhs    = std::get<std::string>(_data);
  std::string_view rhs    = t;
  bool             result = match_with_modifiers(rhs, lhs, _mods);

  if (pi_dbg_ctl.on()) {
    debug_helper(t, " == ", result);
  }

  return result;
}

template <>
bool
Matchers<std::string>::test_set(const std::string &t) const
{
  TSAssert(std::holds_alternative<std::set<std::string>>(_data));
  std::string_view rhs = t;

  for (const auto &entry : std::get<std::set<std::string>>(_data)) {
    if (match_with_modifiers(rhs, entry, _mods)) {
      if (pi_dbg_ctl.on()) {
        debug_helper(t, " ∈ ", true);
        return true;
      }
    }
  }

  debug_helper(t, " ∈ ", false);
  return false;
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
