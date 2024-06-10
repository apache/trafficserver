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

#include <cstdint>

#include "swoc/Lexicon.h"
#include "swoc/Errata.h"

namespace ts
{

using swoc::Errata;
using swoc::Lexicon;
using swoc::Rv;
using swoc::TextView;

/** Parse a string that consists of counts and units.
 *
 * Give a set of units, each of which is a list of names and a multiplier, parse a string. The
 * string contents must consist of (optional whitespace) with alternating counts and units,
 * starting with a count. Each count is multiplied by the value of the subsequent unit. Optionally
 * the parser can be set to allow counts without units, which are not multiplied.
 *
 * For example, if the units were [ "X", 10 ] , [ "L", 50 ] , [ "C", 100 ] , [ "M", 1000 ]
 * then the following strings would be parsed as
 *
 * - "1X" : 10
 * - "1L3X" : 80
 * - "2C" : 200
 * - "1M 4C 4X" : 1,440
 * - "3M 5 C3 X" : 3,530
 */
class UnitParser
{
  using self_type = UnitParser; ///< Self reference type.
public:
  using value_type = uintmax_t;                 ///< Integral type returned.
  using Units      = swoc::Lexicon<value_type>; ///< Unit definition type.

  /// Symbolic name for setting whether units are required.
  static constexpr bool UNITS_REQUIRED = true;
  /// Symbolic name for setting whether units are required.
  static constexpr bool UNITS_NOT_REQUIRED = false;

  /** Constructor.
   *
   * @param units A @c Lexicon of unit definitions.
   * @param unit_required_p Whether valid input requires units on all values.
   */
  UnitParser(Units &&units, bool unit_required_p = true) noexcept;

  /** Set whether a unit is required.
   *
   * @param flag @c true if a unit is required, @c false if not.
   * @return @a this.
   */
  self_type &unit_required(bool flag);

  /** Parse a string.
   *
   * @param src Input string.
   * @return The computed value if the input is valid, or an error report.
   */
  Rv<value_type> operator()(swoc::TextView const &src) const noexcept;

protected:
  bool  _unit_required_p = true; ///< Whether unitless values are allowed.
  Units _units;                  ///< Unit definitions.
};

inline UnitParser::UnitParser(UnitParser::Units &&units, bool unit_required_p) noexcept
  : _unit_required_p(unit_required_p), _units(std::move(units))
{
  _units.set_default(value_type{0}); // Used to check for bad unit names.
}

inline UnitParser::self_type &
UnitParser::unit_required([[maybe_unused]] bool flag)
{
  _unit_required_p = false;
  return *this;
}

} // namespace ts
