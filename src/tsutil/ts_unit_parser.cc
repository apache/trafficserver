/** @file Support for common diagnostics between core, plugins, and libswoc.

  This enables specifying the set of methods usable by a user agent based on the remove IP address
  for a user agent connection.

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

#include <chrono>

#include "tsutil/ts_unit_parser.h"
#include "tsutil/ts_time_parser.h"

namespace ts
{

auto
UnitParser::operator()(swoc::TextView const &src) const noexcept -> Rv<value_type>
{
  value_type zret = 0;
  TextView   text = src; // Keep @a src around to report error offsets.

  while (text.ltrim_if(&isspace)) {
    TextView parsed;
    auto     n = swoc::svtou(text, &parsed);
    if (parsed.empty()) {
      return Errata("Required count not found at offset {}", text.data() - src.data());
    } else if (n == std::numeric_limits<decltype(n)>::max()) {
      return Errata("Count at offset {} was out of bounds", text.data() - src.data());
    }
    text.remove_prefix(parsed.size());
    auto ptr = text.ltrim_if(&isspace).data(); // save for error reporting.
    // Everything up to the next digit or whitespace.
    auto unit = text.clip_prefix_of([](char c) { return !(isspace(c) || isdigit(c)); });
    if (unit.empty()) {
      if (_unit_required_p) {
        return Errata("Required unit not found at offset {}", ptr - src.data());
      }
    } else {
      auto mult = _units[unit]; // What's the multiplier?
      if (mult == 0) {
        return Errata("Unknown unit \"{}\" at offset {}", unit, ptr - src.data());
      }
      n *= mult;
    }
    zret += n;
  }
  return zret;
}

// Concrete unit parsers.

using namespace std::chrono;

namespace detail
{
  /** Functor to parse duration strings.
   * @c time_parser is a functor and is used like a function.
   * @code
   *   TextView text = "12 hours 30 minutes";
   *   auto duration = std::chrono::nanoseconds(ts::time_parser_ns(text));
   * @endcode
   */
  UnitParser const time_parser_ns{UnitParser::Units{{{nanoseconds{1}.count(), {"ns", "nanosec", "nanoseconds"}},
                                                     {nanoseconds{microseconds{1}}.count(), {"us", "microsec", "microseconds"}},
                                                     {nanoseconds{milliseconds{1}}.count(), {"ms", "millisec", "milliseconds"}},
                                                     {nanoseconds{seconds{1}}.count(), {"s", "sec", "seconds"}},
                                                     {nanoseconds{minutes{1}}.count(), {"m", "min", "minutes"}},
                                                     {nanoseconds{hours{1}}.count(), {"h", "hour", "hours"}},
                                                     {nanoseconds{hours{24}}.count(), {"d", "day", "days"}},
                                                     {nanoseconds{hours{168}}.count(), {"w", "week", "weeks"}}}}};

} // namespace detail

} // namespace ts
