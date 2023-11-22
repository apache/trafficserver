// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    BufferWriter formatters for types in the std namespace.
 */

#pragma once

#include <array>
#include <string_view>

#include "swoc/swoc_version.h"
#include "swoc/bwf_base.h"
#include "swoc/swoc_meta.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
namespace bwf {
using namespace swoc::literals; // enable ""sv

/** Output @a text @a n times.
 *
 */
struct Pattern {
  int _n;                 ///< # of instances of @a pattern.
  std::string_view _text; ///< output text.
};

/** Format wrapper for @c errno.
 * This stores a copy of the argument or @c errno if an argument isn't provided. The output
 * is then formatted with the short, long, and numeric value of @c errno. If the format specifier
 * is type 'd' then just the numeric value is printed.
 */
struct Errno {
  int _e; ///< Errno value.

  /// Construct wrapper, default to current @c errno
  explicit Errno(int e = errno) : _e(e) {}
};

/** Format wrapper for time stamps.
 * If the time isn't provided, the current epoch time is used. If the format string isn't
 * provided a format like "2017 Jun 29 14:11:29" is used.
 */
struct Date {
  /// Default format
  static constexpr std::string_view DEFAULT_FORMAT{"%Y %b %d %H:%M:%S"_sv};
  time_t _epoch;         ///< The time.
  std::string_view _fmt; ///< Data format.

  /** Constructor.
   *
   * @param t The timestamp.
   * @param fmt Timestamp format.
   */
  Date(time_t t, std::string_view fmt = DEFAULT_FORMAT) : _epoch(t), _fmt(fmt) {}

  /// Default construct using current time with optional format.
  Date(std::string_view fmt = DEFAULT_FORMAT);
};

namespace detail {
// Special case conversions - these handle nullptr because the @c std::string_view spec is stupid.
inline std::string_view
FirstOfConverter(std::nullptr_t) {
  return std::string_view{};
}

inline std::string_view
FirstOfConverter(char const *s) {
  return std::string_view{s ? s : ""};
}

// Otherwise do any compliant conversion.
template <typename T>
std::string_view
FirstOfConverter(T &&t) {
  return t;
}
} // namespace detail

/// Print the first of a list of strings that is not an empty string.
/// All arguments must be convertible to @c std::string_view.
template <typename... Args>
std::string_view
FirstOf(Args &&...args) {
  std::array<std::string_view, sizeof...(args)> strings{{detail::FirstOfConverter(args)...}};
  for (auto &s : strings) {
    if (!s.empty())
      return s;
  }
  return std::string_view{};
}

/** Wrapper for a sub-text, where the @a args are output according to @a fmt.
 *
 * @tparam Args Argument types.
 */
template <typename... Args> struct SubText {
  using arg_pack = std::tuple<Args...>; ///< The pack of arguments for format string.
  TextView _fmt;                        ///< Format string. If empty, do not generate output.
  arg_pack _args;                       ///< Arguments to format string.

  /// Construct with a specific @a fmt and @a args.
  SubText(TextView const &fmt, arg_pack const &args) : _fmt(fmt), _args(args){};

  /// Check for output not enabled.
  bool operator!() const;

  /// Check for output enabled.
  explicit operator bool() const;
};

template <typename... Args> SubText<Args...>::operator bool() const {
  return !_fmt.empty();
}

template <typename... Args>
bool
SubText<Args...>::operator!() const {
  return _fmt.empty();
}

/** Optional printing wrapper.
 *
 * @tparam Args Arguments for output.
 * @param flag Generate output flag.
 * @param fmt Format for output and args.
 * @param args The arguments.
 * @return A wrapper for the optional text.
 *
 * This function is passed a @a flag, a printing format @a fmt, and a set of arguments @a args to
 * be used by the format. Output is generated if @a flag is @c true, otherwise the empty string
 * (no output) is generated. For example, if in a function there was a flag to determine if an
 * extra tag with delimiters, e.g. "[tag]", was to be generated, this could be done with
 *
 * @code
 *   w.print("Some other text{}.", bwf::If(flag, " [{}]", tag));
 * @endcode
 *
 * @internal To disambiguate overloads, this is enabled only if there is at least one argument
 * to be passed to the format string.
 */
template <typename... Args>
SubText<Args...>
If(bool flag, TextView const &fmt, Args &&...args) {
  return SubText<Args...>(flag ? fmt : TextView{}, std::forward_as_tuple(args...));
}

namespace detail {
// @a T has the @c empty() method.
template <typename T>
auto
Optional(meta::CaseTag<2>, TextView fmt, T &&t) -> decltype(void(t.empty()), meta::TypeFunc<SubText<T>>()) {
  return SubText<T>(t.empty() ? TextView{} : fmt, std::forward_as_tuple(t));
}

// @a T is convertible to @c bool.
template <typename T>
auto
Optional(meta::CaseTag<1>, TextView fmt, T &&t) -> decltype(bool(t), meta::TypeFunc<SubText<T>>()) {
  return SubText<T>(bool(t) ? fmt : TextView{}, std::forward_as_tuple(t));
}

// @a T is not optional, always print.
template <typename T>
auto
Optional(meta::CaseTag<0>, TextView fmt, T &&t) -> SubText<T> {
  return SubText<T>(fmt, std::forward_as_tuple(t));
}
} // namespace detail

/** Simplified optional text wrapper.
 *
 * @tparam ARG the type of the (single) argument.
 * @param fmt Format string.
 * @param arg The single argument to the format string and predicate.
 * @return An optional text wrapper.
 *
 * This generates output iff @a arg is not empty. @a fmt is required to take only a single
 * argument, which will be @a arg. This is a convenience overload, to handle the common case
 * where the argument and the conditional are the same. The argument must have one of the
 * following properties in order to serve as the conditional. These are checked in order.
 *
 * - The @c empty() method which returns @c true if the argument is empty and should not be printed.
 *   This handles the case of C++ string types.
 *
 * - Conversion to @c bool which is @c false if the argument should not be printed. This covers the
 *   case of pointers.
 *
 * As an example, if an output function had three strings @a alpha, @a bravo, and
 * @a charlie, each of which could be null, which should be output with space separators,
 * this would be
 * @code
 * w.print("Leading text{}{}{}.", Optional(" {}", alpha)
 *                              , Optional(" {}", bravo)
 *                              , Optional(" {}", charlie));
 * @endcode
 *
 * Because of the property handling, these strings can be C styles strings ( @c char* ) or C++
 * string types (such as @c std::string_view ).
 *
 */
template <typename ARG>
SubText<ARG>
Optional(TextView fmt, ARG &&arg) {
  return detail::Optional(meta::CaseArg, fmt, std::forward<ARG>(arg));
}

/** Convert from ASCII hexadecimal to raw bytes.
 *
 * E.g. if the source span contains "4576696c20446176652052756c7a" then "Evil Dave Rulz" is the output.
 * For format specifier support, on lhe max width is used. Any @c MemSpan compatible class can be used
 * as the target, including @c std::string and @c std::string_view.
 *
 * @code
 *   void f(std::string const& str) {
 *     w.print("{}", bwf::UnHex(str));
 *     // ...
 * @endcode
 */
struct UnHex {
  UnHex(MemSpan<void const> const &span) : _span(span) {}
  MemSpan<void const> _span; ///< Source span.
};
} // namespace bwf

/** Repeatedly output a pattern.
 *
 * @param w Output.
 * @param spec Format specifier.
 * @param pattern Output patterning.
 * @return @a w
 *
 * The @a pattern contains the count and text to output.
 */
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, bwf::Pattern const &pattern);

/** Format an integer as an @c errno value.
 *
 * @param w Output.
 * @param spec Format specifier.
 * @param e Error code.
 * @return @a w
 */
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, bwf::Errno const &e);

/** Format a timestamp wrapped in a @c Date.
 *
 * @param w Output.
 * @param spec Format specifier.
 * @param date Timestamp.
 * @return @a w
 */
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, bwf::Date const &date);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, bwf::UnHex const &obj);

/** Output a nested formatted string.
 *
 * @tparam Args Argument pack for @a subtext.
 * @param w Output
 * @param subtext Format string and arguments.
 * @return @a w
 *
 * This supports a nested format string and arguments inside another format string. This is most often useful
 * if one of the formats is fixed or pre-compiled.
 *
 * @code
 * bwformat(w, "Line {} offset {} with data {}.", line_no, line_off, bwf::SubText("alpha {} bravo {}", alpha, bravo"));
 * @endcode
 *
 * @see bwf::Subtext
 */
template <typename... Args>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &, bwf::SubText<Args...> const &subtext) {
  if (!subtext._fmt.empty()) {
    w.print_v(subtext._fmt, subtext._args);
  }
  return w;
}

}} // namespace swoc::SWOC_VERSION_NS
