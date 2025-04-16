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
  bool r = false;
  auto d = std::get<std::string>(_data);

  if (d.length() == t.length()) {
    if (_nocase) {
      // ToDo: in C++20, this would be nicer with std::range, e.g.
      // r = std::ranges::equal(d, t, [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); });
      r = std::equal(d.begin(), d.end(), t.begin(), [](char c1, char c2) {
        return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
      });
    } else {
      r = (t == d);
    }
  }

  if (pi_dbg_ctl.on()) {
    debug_helper(t, " == ", r);
  }

  return r;
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
