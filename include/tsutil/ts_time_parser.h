/** @file

  Parse strings with units.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
  See the NOTICE file distributed with this work for additional information regarding copyright
  ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance with the License.  You may obtain a
  copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
*/

#pragma once

#include "tsutil/ts_unit_parser.h"

namespace ts
{

namespace detail
{
  extern UnitParser const time_parser_ns;
}
/** Parse duration string.
 *
 * @param text Input text to parse.
 * @return A return value containing the time in nanoseconds or parsing errors.
 */
inline swoc::Rv<std::chrono::nanoseconds>
time_parser(swoc::TextView text)
{
  auto &&[duration, errata] = detail::time_parser_ns(text);
  return {std::chrono::nanoseconds(duration), std::move(errata)};
}
} // namespace ts
