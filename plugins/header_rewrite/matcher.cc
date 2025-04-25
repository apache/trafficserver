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

// Special case for strings, to make the distinction between regexes and string matching
template <>
void
Matchers<std::string>::set(const std::string &d, CondModifiers mods)
{
  _data = d;
  _mods = mods;

  if (_op == MATCH_REGULAR_EXPRESSION) {
    if (!_reHelper.setRegexMatch(_data, has_modifier(_mods, CondModifiers::MOD_NOCASE))) {
      std::stringstream ss;

      ss << _data;
      TSError("[%s] Invalid regex: failed to precompile: %s", PLUGIN_NAME, ss.str().c_str());
      Dbg(pi_dbg_ctl, "Invalid regex: failed to precompile: %s", ss.str().c_str());
      throw std::runtime_error("Malformed regex");
    } else {
      Dbg(pi_dbg_ctl, "Regex precompiled successfully");
    }
  }
}

template <>
bool
Matchers<std::string>::test_eq(const std::string &t) const
{
  std::string_view lhs    = _data;
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
