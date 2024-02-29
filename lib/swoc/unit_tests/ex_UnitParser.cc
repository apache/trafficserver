// SPDX-License-Identifier: Apache-2.0
// Copyright Verizon Media 2020
/** @file

    Example parser for parsing strings that are counts with attached unit tokens.
*/

#include <ctype.h>
#include <chrono>

#include "swoc/Lexicon.h"
#include "swoc/Errata.h"
#include "catch.hpp"

using swoc::TextView;
using swoc::Lexicon;
using swoc::Errata;
using swoc::Rv;

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
class UnitParser {
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
   * @return The computed value if the input it valid, or an error report.
   */
  Rv<value_type> operator()(swoc::TextView const &src) const noexcept;

protected:
  bool _unit_required_p = true; ///< Whether unitless values are allowed.
  Units _units;                 ///< Unit definitions.
};

UnitParser::UnitParser(UnitParser::Units &&units, bool unit_required_p) noexcept
  : _unit_required_p(unit_required_p), _units(std::move(units)) {
  _units.set_default(value_type{0}); // Used to check for bad unit names.
}

UnitParser::self_type &
UnitParser::unit_required(bool flag) {
  _unit_required_p = false;
  return *this;
}

auto
UnitParser::operator()(swoc::TextView const &src) const noexcept -> Rv<value_type> {
  value_type zret = 0;
  TextView text   = src; // Keep @a src around to report error offsets.

  while (text.ltrim_if(&isspace)) {
    TextView parsed;
    auto n = swoc::svtou(text, &parsed);
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

// --- Tests ---

TEST_CASE("UnitParser Bytes", "[Lexicon][UnitParser]") {
  UnitParser bytes{UnitParser::Units{{{1, {"B", "bytes"}},
                                      {1024, {"K", "KB", "kilo", "kilobyte", "kilobytes"}},
                                      {1048576, {"M", "MB", "mega", "megabyte", "megabytes"}},
                                      {1 << 30, {"G", "GB", "giga", "gigabyte", "gigabytes"}}}},
                   UnitParser::UNITS_NOT_REQUIRED};

  REQUIRE(bytes("56 bytes") == 56);
  REQUIRE(bytes("3 kb") == 3 * (1 << 10));
  REQUIRE(bytes("6k128bytes") == 6 * (1 << 10) + 128);
  REQUIRE(bytes("6 k128bytes") == 6 * (1 << 10) + 128);
  REQUIRE(bytes("6 K128 bytes") == 6 * (1 << 10) + 128);
  REQUIRE(bytes("6 kilo 0x80 bytes") == 6 * (1 << 10) + 128);
  REQUIRE(bytes("6kilo 0x8b bytes") == 6 * (1 << 10) + 0x8b);
  REQUIRE(bytes("111") == 111);
  REQUIRE(bytes("4MB") == 4 * (uintmax_t(1) << 20));
  REQUIRE(bytes("4 giga") == 4 * (uintmax_t(1) << 30));
  REQUIRE(bytes("10M 256K 512") == 10 * (1 << 20) + 256 * (1 << 10) + 512);
  REQUIRE(bytes("512 256 kilobytes 10 megabytes") == 10 * (1 << 20) + 256 * (1 << 10) + 512);
  REQUIRE(bytes("0x100000000") == 0x100000000);
  auto result = bytes("56delain");
  REQUIRE(result.is_ok() == false);
  REQUIRE(result.errata().front().text() == "Unknown unit \"delain\" at offset 2");
  result = bytes("12K delain");
  REQUIRE(result.is_ok() == false);
  REQUIRE(result.errata().front().text() == "Required count not found at offset 4");
  result = bytes("99999999999999999999");
  REQUIRE(result.is_ok() == false);
  REQUIRE(result.errata().front().text() == "Count at offset 0 was out of bounds");
}

TEST_CASE("UnitParser Time", "[Lexicon][UnitParser]") {
  using namespace std::chrono;
  UnitParser time{UnitParser::Units{{{nanoseconds{1}.count(), {"ns", "nanosec", "nanoseconds"}},
                                     {nanoseconds{microseconds{1}}.count(), {"us", "microsec", "microseconds"}},
                                     {nanoseconds{milliseconds{1}}.count(), {"ms", "millisec", "milliseconds"}},
                                     {nanoseconds{seconds{1}}.count(), {"s", "sec", "seconds"}},
                                     {nanoseconds{minutes{1}}.count(), {"m", "min", "minutes"}},
                                     {nanoseconds{hours{1}}.count(), {"h", "hour", "hours"}},
                                     {nanoseconds{hours{24}}.count(), {"d", "day", "days"}},
                                     {nanoseconds{hours{168}}.count(), {"w", "week", "weeks"}}}}};

  REQUIRE(nanoseconds{time("2s")} == seconds{2});
  REQUIRE(nanoseconds{time("1w 2days 12 hours")} == hours{168} + hours{2 * 24} + hours{12});
  REQUIRE(nanoseconds{time("300ms")} == milliseconds{300});
  REQUIRE(nanoseconds{time("1h30m")} == hours{1} + minutes{30});

  auto result = time("1h30m10");
  REQUIRE(result.is_ok() == false);
  REQUIRE(result.errata().front().text() == "Required unit not found at offset 7");

  auto duration = nanoseconds(time("30 minutes 12h"));
  REQUIRE(minutes(750) == duration);
}

TEST_CASE("UnitParser Eggs", "[Lexicon][UnitParser]") {
  const UnitParser eggs{
    UnitParser::Units{UnitParser::Units::with_multi{{1, {"egg", "eggs"}}, {12, {"dozen"}}, {12 * 12, {"gross"}}}},
    UnitParser::UNITS_NOT_REQUIRED};

  REQUIRE(eggs("1") == 1);
  REQUIRE(eggs("6") == 6);
  REQUIRE(eggs("1 dozen") == 12);
  REQUIRE(eggs("2 gross 6 dozen 10 eggs") == 370);
}
