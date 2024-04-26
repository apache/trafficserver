/**
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

  Copyright 2019, Oath Inc.
*/

#include <string>
#include <chrono>

#include <swoc/bwf_std.h>
#include <swoc/bwf_ip.h>

#include "txn_box/common.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

using swoc::TextView;
using namespace swoc::literals;
using swoc::BufferWriter;
using swoc::Errata;
using swoc::Rv;

// ----
namespace
{
struct join_visitor {
  swoc::BufferWriter &_w;
  TextView            _glue;
  unsigned            _recurse = 0;

  swoc::BufferWriter &
  glue() const
  {
    if (_w.size()) {
      _w.write(_glue);
    }
    return _w;
  }

  void
  operator()(feature_type_for<NIL>) const
  {
  }
  void
  operator()(feature_type_for<STRING> const &s) const
  {
    this->glue().write(s);
  }
  void
  operator()(feature_type_for<INTEGER> n) const
  {
    this->glue().print("{}", n);
  }
  void
  operator()(feature_type_for<BOOLEAN> flag) const
  {
    this->glue().print("{}", flag);
  }
  void
  operator()(feature_type_for<DURATION> d) const
  {
    this->glue().print("{}", d);
  }
  void
  operator()(feature_type_for<TUPLE> t) const
  {
    this->glue();
    if (_recurse) {
      _w.write("[ "_tv);
    }
    auto lw = swoc::FixedBufferWriter{_w.aux_span()};
    for (auto const &item : t) {
      std::visit(join_visitor{lw, _glue, _recurse + 1}, item);
    }
    _w.commit(lw.size());
    if (_recurse) {
      _w.write(" ]"_tv);
    }
  }

  template <typename T>
  auto
  operator()(T const &) const -> EnableForFeatureTypes<T, void>
  {
  }
};

} // namespace

Feature
Feature::join(Context &ctx, const swoc::TextView &glue) const
{
  return {ctx.render_transient([=](BufferWriter &w) { std::visit(join_visitor{w, glue}, *this); })};
}
// ----
/** Parse a string that consists of counts and units.
 *
 * Given a set of units, each of which is a list of names and a multiplier, parse a string. The
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
template <typename E> class UnitParser
{
  using self_type = UnitParser; ///< Self reference type.
public:
  using value_type = E;                         ///< Integral type returned.
  using unit_type  = swoc::Lexicon<value_type>; ///< Unit definition type.
  static constexpr value_type NIL{0};

  /** Constructor.
   *
   * @param units A @c Lexicon of unit definitions.
   */
  explicit UnitParser(unit_type &&units) noexcept;

  /** Constructor.
   *
   * @param units A @c Lexicon of unit definitions.
   * @param unit_required_p Specify if every number must have an attached unit.
   */
  UnitParser(unit_type &&units, bool unit_required_p) noexcept;

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

  unit_type const &
  units()
  {
    return _units;
  }

protected:
  bool            _unit_required_p = true; ///< Whether unitless values are allowed.
  const unit_type _units;                  ///< Unit definitions.
};

template <typename E> UnitParser<E>::UnitParser(UnitParser::unit_type &&units) noexcept : UnitParser(std::move(units), true) {}

template <typename E>
UnitParser<E>::UnitParser(UnitParser::unit_type &&units, bool unit_required_p) noexcept
  : _unit_required_p(unit_required_p), _units(std::move(units.set_default(NIL)))
{
}

template <typename E>
auto
UnitParser<E>::unit_required(bool flag) -> self_type &
{
  _unit_required_p = flag;
  return *this;
}

template <typename E>
auto
UnitParser<E>::operator()(swoc::TextView const &src) const noexcept -> Rv<value_type>
{
  value_type zret{0};
  TextView   text = src; // Keep @a src around to report error offsets.

  while (text.ltrim_if(&isspace)) {
    // Get a count first.
    auto ptr   = text.data(); // save for error reporting.
    auto count = text.clip_prefix_of(&isdigit);
    if (count.empty()) {
      return {NIL, Errata(S_ERROR, "Required count not found at offset {}", ptr - src.data())};
    }
    // Should always parse correctly as @a count is a non-empty sequence of digits.
    auto n = svtou(count);

    // Next, the unit.
    ptr = text.ltrim_if(&isspace).data(); // save for error reporting.
    // Everything up to the next digit or whitespace.
    auto unit = text.clip_prefix_of([](char c) { return !(isspace(c) || isdigit(c)); });
    if (unit.empty()) {
      if (_unit_required_p) {
        return {NIL, Errata(S_ERROR, "Required unit not found at offset {}", ptr - src.data())};
      }
      zret += value_type{n}; // no metric -> unit metric.
    } else {
      auto mult = _units[unit]; // What's the multiplier?
      if (mult == NIL) {
        return {NIL, Errata(S_ERROR, "Unknown unit \"{}\" at offset {}", unit, ptr - src.data())};
      }
      zret += mult * n;
    }
  }
  return zret;
}

// ----
namespace
{
struct bool_visitor {
  template <typename F>
  auto
  operator()(F const &) const -> EnableForFeatureTypes<F, bool>
  {
    TS_DBG("should not be here!");
    return false;
  }

  bool
  operator()(feature_type_for<BOOLEAN> const &flag) const
  {
    return flag;
  }

  bool
  operator()(feature_type_for<NIL>) const
  {
    return false;
  }

  bool
  operator()(feature_type_for<STRING> const &s) const
  {
    return BoolNames[s] == True;
  }

  bool
  operator()(feature_type_for<INTEGER> const &n) const
  {
    return n != 0;
  }

  bool
  operator()(feature_type_for<FLOAT> const &f) const
  {
    return f != 0;
  }

  bool
  operator()(feature_type_for<IP_ADDR> const &addr) const
  {
    return addr.is_valid();
  }

  bool
  operator()(feature_type_for<TUPLE> const &t) const
  {
    return t.size() > 0;
  }

  bool
  operator()(feature_type_for<DURATION> const &d) const
  {
    return d.count() != 0;
  }
};

} // namespace

auto
Feature::as_bool() const -> type_for<BOOLEAN>
{
  static bool_visitor visitor;
  return std::visit(visitor, *this);
}

// ----
namespace
{
struct integer_visitor {
  /// Target feature type.
  using ftype = feature_type_for<INTEGER>;
  /// Return type.
  using ret_type = Rv<ftype>;

  ftype _invalid;

  explicit integer_visitor(ftype invalid) : _invalid(invalid) {}

  ret_type
  operator()(feature_type_for<STRING> const &s)
  {
    TextView parsed;
    ftype    zret = swoc::svtoi(s, &parsed);
    if (parsed.size() != s.size()) {
      return {_invalid, Errata(S_ERROR, "Invalid format for integer at offset {}", parsed.size() + 1)};
    }
    return zret;
  }

  ret_type
  operator()(feature_type_for<INTEGER> n)
  {
    return n;
  }

  ret_type
  operator()(feature_type_for<FLOAT> f)
  {
    return ftype(f);
  }

  ret_type
  operator()(feature_type_for<BOOLEAN> flag)
  {
    return ftype(flag);
  }

  ret_type
  operator()(feature_type_for<TUPLE> t)
  {
    return t.size();
  }

  template <typename F>
  auto
  operator()(F const &) -> EnableForFeatureTypes<F, ret_type>
  {
    return {_invalid, Errata(S_ERROR, "Feature of type {} cannot be coerced to type {}.", value_type_of<F>, INTEGER)};
  }
};

} // namespace
auto
Feature::as_integer(type_for<INTEGER> invalid) const -> Rv<type_for<INTEGER>>
{
  return std::visit(integer_visitor{invalid}, *this);
}
// ----
// Duration conversion support.
using namespace std::chrono;
namespace swoc
{
template <>
uintmax_t
Lexicon_Hash(system_clock::duration d)
{
  return static_cast<uintmax_t>(d.count());
}
} // namespace swoc

UnitParser<system_clock::duration> DurationParser{
  UnitParser<system_clock::duration>::unit_type{UnitParser<system_clock::duration>::unit_type::with_multi{
    {duration_cast<std::chrono::system_clock::duration>(nanoseconds(1)), {"ns", "nanoseconds"}},
    {duration_cast<std::chrono::system_clock::duration>(microseconds(1)), {"us", "microseconds"}},
    {duration_cast<std::chrono::system_clock::duration>(milliseconds(1)), {"ms", "milliseconds"}},
    {duration_cast<std::chrono::system_clock::duration>(seconds(1)), {"s", "sec", "second", "seconds"}},
    {duration_cast<std::chrono::system_clock::duration>(minutes(1)), {"m", "min", "minute", "minutes"}},
    {duration_cast<std::chrono::system_clock::duration>(hours(1)), {"h", "hour", "hours"}},
    {duration_cast<std::chrono::system_clock::duration>(days(1)), {"d", "day", "days"}},
    {duration_cast<std::chrono::system_clock::duration>(days(7)), {"w", "week", "weeks"}}}}};

// Generate a list, ordered largest to smallest, of the duration name units.
// The lambda constructs such a vector, which is then used to move construct @c DurationOrder.
std::vector<decltype(DurationParser)::unit_type::iterator> DurationOrder{[]() {
  using I             = decltype(DurationParser)::unit_type::iterator;
  auto const &lexicon = DurationParser.units();
  auto        n       = lexicon.count();

  std::vector<I> zret;
  zret.reserve(n);
  // Load the iterators into the vector.
  for (auto spot = lexicon.begin(); spot != lexicon.end(); ++spot) {
    zret.push_back(spot);
  }

  // Sort the iterators by scale, largest first.
  std::sort(zret.begin(), zret.end(), [](I const &lhs, I const &rhs) { return std::get<0>(*lhs) > std::get<0>(*rhs); });

  return zret;
}()};

namespace
{
/// Handlers for coercing feature types into duration.
struct duration_visitor {
  /// Target feature type.
  using ftype = feature_type_for<DURATION>;
  /// Return type.
  using ret_type = Rv<ftype>;

  ftype _invalid; ///< Value to return if an error occurs.

  explicit duration_visitor(ftype invalid) : _invalid(invalid) {}

  ret_type operator()(feature_type_for<DURATION> const &d);

  ret_type operator()(feature_type_for<STRING> const &s);

  ret_type operator()(feature_type_for<TUPLE> t);

  template <typename F>
  auto
  operator()(F const &) -> EnableForFeatureTypes<F, ret_type>
  {
    return {_invalid, Errata(S_ERROR, "Feature of type {} cannot be coerced to type {}.", value_type_of<F>, INTEGER)};
  }
};

// It's already the correct type, pass it back.
auto
duration_visitor::operator()(feature_type_for<DURATION> const &d) -> ret_type
{
  return d;
}

duration_visitor::ret_type
duration_visitor::operator()(feature_type_for<STRING> const &s)
{
  auto &&[n, errata] = DurationParser(s);
  if (!errata.is_ok()) {
    errata.note("Duration string was not a valid format.");
    return {_invalid, std::move(errata)};
  }
  return {feature_type_for<DURATION>(n)};
}

// Sum the durations in the tuple, coercing as it goes.
duration_visitor::ret_type
duration_visitor::operator()(feature_type_for<TUPLE> t)
{
  ftype    zret{0};
  unsigned idx = 0; // Just for error reporting.
  for (auto const &item : t) {
    auto &&[value, errata]{std::visit(duration_visitor{_invalid}, item)};
    if (!errata.is_ok()) {
      errata.note("The tuple element at index {} was not a valid duration.", idx);
      return {_invalid, std::move(errata)};
    }
    ++idx;
    zret += value;
  }
  return zret;
}

} // namespace

auto
Feature::as_duration(type_for<DURATION> invalid) const -> Rv<type_for<DURATION>>
{
  return std::visit(duration_visitor{invalid}, *this);
}

/* ------------------------------------------------------------------------------------ */
bool
operator==(Feature const &lhs, Feature const &rhs)
{
  auto lidx = lhs.index();
  if (lidx != rhs.index()) {
    return false;
  }
  switch (lidx) {
  case IndexFor(NO_VALUE):
  case IndexFor(NIL):
    return true;
  case IndexFor(BOOLEAN):
    return std::get<IndexFor(BOOLEAN)>(lhs) == std::get<IndexFor(BOOLEAN)>(rhs);
  case IndexFor(INTEGER):
    return std::get<IndexFor(INTEGER)>(lhs) == std::get<IndexFor(INTEGER)>(rhs);
  case IndexFor(IP_ADDR):
    return std::get<IndexFor(IP_ADDR)>(lhs) == std::get<IndexFor(IP_ADDR)>(rhs);
  case IndexFor(DURATION):
    return std::get<IndexFor(DURATION)>(lhs) == std::get<IndexFor(DURATION)>(rhs);
  default:
    break;
  }
  return false;
}

bool
operator<(Feature const &lhs, Feature const &rhs)
{
  auto lidx = lhs.index();
  if (lidx != rhs.index()) {
    return false;
  }
  switch (lidx) {
  case IndexFor(BOOLEAN):
    return std::get<IndexFor(BOOLEAN)>(lhs) < std::get<IndexFor(BOOLEAN)>(rhs);
  case IndexFor(INTEGER):
    return std::get<IndexFor(INTEGER)>(lhs) < std::get<IndexFor(INTEGER)>(rhs);
  case IndexFor(IP_ADDR):
    return std::get<IndexFor(IP_ADDR)>(lhs) < std::get<IndexFor(IP_ADDR)>(rhs);
  case IndexFor(DURATION):
    return std::get<IndexFor(DURATION)>(lhs) < std::get<IndexFor(DURATION)>(rhs);
  default:
    break;
  }
  return false;
}

bool
operator<=(Feature const &lhs, Feature const &rhs)
{
  auto lidx = lhs.index();
  if (lidx != rhs.index()) {
    return false;
  }
  switch (lidx) {
  case IndexFor(NIL):
    return true;
  case IndexFor(BOOLEAN):
    return std::get<IndexFor(BOOLEAN)>(lhs) <= std::get<IndexFor(BOOLEAN)>(rhs);
  case IndexFor(INTEGER):
    return std::get<IndexFor(INTEGER)>(lhs) <= std::get<IndexFor(INTEGER)>(rhs);
  case IndexFor(IP_ADDR):
    return std::get<IndexFor(IP_ADDR)>(lhs) <= std::get<IndexFor(IP_ADDR)>(rhs);
  case IndexFor(DURATION):
    return std::get<IndexFor(DURATION)>(lhs) <= std::get<IndexFor(DURATION)>(rhs);
  default:
    break;
  }
  return false;
}
/* ------------------------------------------------------------------------------------ */
namespace swoc
{
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &, feature_type_for<NO_VALUE>)
{
  return w.write("!NO_VALUE");
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &, feature_type_for<NIL>)
{
  return w.write("NULL");
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, ValueType type)
{
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, static_cast<unsigned>(type));
  }
  return bwformat(w, spec, ValueTypeNames[type]);
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, ValueMask const &mask)
{
  auto span{w.aux_span()};
  if (span.size() > spec._max) {
    span = span.prefix(spec._max);
  }
  swoc::FixedBufferWriter lw{span};
  if (mask.any()) {
    for (auto const &[e, v] : ValueTypeNames) {
      if (!mask[e]) {
        continue;
      }
      if (lw.extent()) {
        lw.write(", ");
      }
      bwformat(lw, spec, v);
    }
  } else {
    bwformat(lw, spec, "*no value"_tv);
  }
  w.commit(lw.extent());
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, FeatureTuple const &t)
{
  if (t.count() > 0) {
    bwformat(w, spec, t[0]);
    for (auto &&f : t.subspan(1, t.count() - 1)) {
      w.write(", ");
      bwformat(w, spec, f);
    }
  }
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, Feature const &feature)
{
  if (is_nil(feature)) {
    return bwformat(w, spec, "NULL"_tv);
  } else {
    auto visitor = [&](auto &&arg) -> BufferWriter & { return bwformat(w, spec, arg); };
    return std::visit(visitor, feature);
  }
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, feature_type_for<DURATION> const &d)
{
  bool sep_p = false;
  auto n     = d.count();
  for (auto item : DurationOrder) {
    auto scale = std::get<0>(*item).count();
    auto c     = n / scale;
    if (c > 0) {
      if (sep_p) {
        w.write(' ');
      }
      sep_p = true;
      bwformat(w, spec, c);
      w.write(' ');
      w.write(std::get<1>(*item));
      n -= c * scale;
    }
  }
  return w;
}
} // namespace swoc

BufferWriter &
bwformat(BufferWriter &w, swoc::bwf::Spec const &spec, ActiveType const &type)
{
  bwformat(w, spec, type._base_type);
  if (type._tuple_type.any()) {
    w.write(", Tuples of [");
    bwformat(w, spec, type._tuple_type);
    w.write(']');
  }
  return w;
}
