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
  if (mods & COND_NOCASE) {
    _nocase = true;
  }

  if (_op == MATCH_REGULAR_EXPRESSION) {
    if (!_reHelper.setRegexMatch(_data, _nocase)) {
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

// Special case for strings, to allow for insensitive case comparisons for std::string matchers.
template <>
bool
Matchers<std::string>::test_eq(const std::string &t) const
{
  bool r = false;

  if (_data.length() == t.length()) {
    if (_nocase) {
      // ToDo: in C++20, this would be nicer with std::range, e.g.
      // r = std::ranges::equal(_data, t, [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); });
      r = std::equal(_data.begin(), _data.end(), t.begin(), [](char c1, char c2) {
        return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
      });
    } else {
      r = (t == _data);
    }
  }

  if (pi_dbg_ctl.on()) {
    debug_helper(t, " == ", r);
  }

  return r;
}
