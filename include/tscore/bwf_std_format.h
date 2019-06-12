/** @file

    BufferWriter formatters for types in the std namespace.

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

#pragma once

#include <atomic>
#include <array>
#include <string_view>
#include "tscpp/util/TextView.h"
#include "tscore/BufferWriterForward.h"

namespace std
{
template <typename T>
ts::BufferWriter &
bwformat(ts::BufferWriter &w, ts::BWFSpec const &spec, atomic<T> const &v)
{
  return ts::bwformat(w, spec, v.load());
}
} // end namespace std

namespace ts
{
namespace bwf
{
  using namespace std::literals; // enable ""sv

  /** Format wrapper for @c errno.
   * This stores a copy of the argument or @c errno if an argument isn't provided. The output
   * is then formatted with the short, long, and numeric value of @c errno. If the format specifier
   * is type 'd' then just the numeric value is printed.
   */
  struct Errno {
    int _e;
    explicit Errno(int e = errno) : _e(e) {}
  };

  /** Format wrapper for time stamps.
   * If the time isn't provided, the current epoch time is used. If the format string isn't
   * provided a format like "2017 Jun 29 14:11:29" is used.
   */
  struct Date {
    static constexpr std::string_view DEFAULT_FORMAT{"%Y %b %d %H:%M:%S"_sv};
    time_t _epoch;
    std::string_view _fmt;
    Date(time_t t, std::string_view fmt = DEFAULT_FORMAT) : _epoch(t), _fmt(fmt) {}
    Date(std::string_view fmt = DEFAULT_FORMAT);
  };

  namespace detail
  {
    // Special case conversions - these handle nullptr because the @c std::string_view spec is stupid.
    inline std::string_view FirstOfConverter(std::nullptr_t) { return std::string_view{}; }
    inline std::string_view
    FirstOfConverter(char const *s)
    {
      return std::string_view{s ? s : ""};
    }
    // Otherwise do any compliant conversion.
    template <typename T>
    std::string_view
    FirstOfConverter(T &&t)
    {
      return t;
    }
  } // namespace detail
  /// Print the first of a list of strings that is not an empty string.
  /// All arguments must be convertible to @c std::string.
  template <typename... Args>
  std::string_view
  FirstOf(Args &&... args)
  {
    std::array<std::string_view, sizeof...(args)> strings{{detail::FirstOfConverter(args)...}};
    for (auto &s : strings) {
      if (!s.empty())
        return s;
    }
    return std::string_view{};
  };
  /** For optional printing strings along with suffixes and prefixes.
   *  If the wrapped string is null or empty, nothing is printed. Otherwise the prefix, string,
   *  and suffix are printed. The default are a single space for suffix and nothing for the prefix.
   */
  struct OptionalAffix {
    std::string_view _text;
    std::string_view _suffix;
    std::string_view _prefix;

    OptionalAffix(const char *text, std::string_view suffix = " "sv, std::string_view prefix = ""sv)
      : OptionalAffix(std::string_view(text ? text : ""), suffix, prefix)
    {
    }

    OptionalAffix(std::string_view text, std::string_view suffix = " "sv, std::string_view prefix = ""sv)
    {
      // If text is null or empty, leave the members empty too.
      if (!text.empty()) {
        _text   = text;
        _prefix = prefix;
        _suffix = suffix;
      }
    }
  };

}; // namespace bwf

BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, bwf::Errno const &e);
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, bwf::Date const &date);
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, bwf::OptionalAffix const &opts);

} // namespace ts
