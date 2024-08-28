/*
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

#include <filesystem>

#include <cstring>
#include <string>
#include <string_view>
#include <charconv>
#include <type_traits>

#include <fmt/core.h>

#include "swoc/TextView.h"
#include "ts/ts.h"
#include "ts/remap.h"

// Silly typedef's for some PODs
using integer = int64_t;
using boolean = bool;

using namespace std::string_view_literals; // Gives ""sv for example, which can be convenient ...
using namespace std::literals;             // For e.g. 24h for times
namespace CFS = std::filesystem;           // Simplify the usage of std::filesystem

integer integer_helper(std::string_view sv);

// Some convenience macros
#define borrow         auto &
#define CAssert(...)   TSReleaseAssert(__VA_ARGS__)
#define CFatal(...)    TSFatal(__VA_ARGS__)
#define AsBoolean(arg) std::get<boolean>(arg)
#define AsString(arg)  std::get<cripts::string>(arg)
#define AsInteger(arg) std::get<integer>(arg)
#define AsFloat(arg)   std::get<double>(arg)
#define AsPointer(arg) std::get<void *>(arg)

namespace cripts
{
// Use cripts::string_view consistently, so that it's a one-stop shop for all string_view needs.
using string_view = swoc::TextView;

namespace details
{
  template <typename T> std::vector<T> Splitter(T input, char delim);

} // namespace details

namespace Pacing
{
  static constexpr uint32_t Off = std::numeric_limits<uint32_t>::max();
} // namespace Pacing

class Context;

// This is a mixin template class
template <typename ChildT> class StringViewMixin
{
  using self_type  = StringViewMixin;
  using mixin_type = cripts::string_view;

public:
  constexpr StringViewMixin() = default;
  constexpr StringViewMixin(const char *s) { _value = mixin_type(s, strlen(s)); }
  constexpr StringViewMixin(const char *s, mixin_type::size_type count) { _value = mixin_type(s, count); }
  constexpr StringViewMixin(const cripts::string_view &str) { _value = str; }

  virtual self_type &operator=(const mixin_type str) = 0;

  operator integer() const { return integer_helper(_value); }

  [[nodiscard]] integer
  ToInteger() const
  {
    return integer(*this);
  }

  [[nodiscard]] double
  ToFloat() const
  {
    return float(*this);
  }

  [[nodiscard]] bool
  ToBool() const
  {
    return bool(*this);
  }

  std::vector<mixin_type>
  Splitter(mixin_type input, char delim)
  {
    return details::Splitter<mixin_type>(input, delim);
  }

  [[nodiscard]] std::vector<mixin_type>
  split(char delim)
  {
    return Splitter(_value, delim);
  }

  [[nodiscard]] mixin_type
  GetSV() const
  {
    return _value;
  }

  self_type &
  clear()
  {
    _value = cripts::string_view();
    return *this;
  }

  [[nodiscard]] constexpr bool
  empty() const
  {
    return _value.empty();
  }

  [[nodiscard]] constexpr mixin_type::const_pointer
  data() const
  {
    return _value.data();
  }

  [[nodiscard]] constexpr mixin_type::size_type
  size() const
  {
    return _value.size();
  }

  [[nodiscard]] constexpr mixin_type::size_type
  length() const
  {
    return _value.size();
  }

  operator mixin_type() { return _value; }

  [[nodiscard]] constexpr mixin_type
  value() const
  {
    return _value;
  }

  bool
  operator==(const mixin_type rhs) const
  {
    return _value == rhs;
  }

  bool
  operator!=(const mixin_type rhs) const
  {
    return _value != rhs;
  }

  [[nodiscard]] constexpr mixin_type
  substr(mixin_type::size_type pos = 0, mixin_type::size_type count = mixin_type::npos) const
  {
    return _value.substr(pos, count);
  }

  constexpr void
  remove_prefix(mixin_type::size_type n)
  {
    _value.remove_prefix(n);
  }

  constexpr void
  remove_suffix(mixin_type::size_type n)
  {
    _value.remove_suffix(n);
  }

  ChildT &
  ltrim(char c)
  {
    _value.ltrim(c);
    return *(static_cast<ChildT *>(this));
  }

  ChildT &
  rtrim(char c)
  {
    _value.rtrim(c);
    return *(static_cast<ChildT *>(this));
  }

  ChildT &
  trim(char c)
  {
    _value.trim(c);
    return *(static_cast<ChildT *>(this));
  }

  ChildT &
  ltrim(const char *chars = " \t\r\n")
  {
    _value.ltrim(chars);
    return *(static_cast<ChildT *>(this));
  }

  ChildT &
  rtrim(const char *chars = " \t\r\n")
  {
    _value.rtrim(chars);
    return *(static_cast<ChildT *>(this));
  }

  ChildT &
  trim(const char *chars = " \t")
  {
    _value.trim(chars);
    return *(static_cast<ChildT *>(this));
  }

  [[nodiscard]] constexpr char const *
  data_end() const noexcept
  {
    return _value.data_end();
  }

  [[nodiscard]] constexpr bool
  ends_with(cripts::string_view const suffix) const
  {
    return _value.ends_with(suffix);
  }

  [[nodiscard]] constexpr bool
  starts_with(cripts::string_view const prefix) const
  {
    return _value.starts_with(prefix);
  }

  [[nodiscard]] constexpr mixin_type::size_type
  find(cripts::string_view const substr, mixin_type::size_type pos = 0) const
  {
    return _value.find(substr, pos);
  }

  [[nodiscard]] constexpr mixin_type::size_type
  rfind(cripts::string_view const substr, mixin_type::size_type pos = 0) const
  {
    return _value.rfind(substr, pos);
  }

  [[nodiscard]] constexpr bool
  contains(cripts::string_view const substr) const
  {
    return (_value.find(substr) != _value.npos);
  }

protected:
  void
  _setSV(const mixin_type str)
  {
    _value = str;
  }

private:
  mixin_type _value;
}; // End class cripts::StringViewMixin

class string : public std::string
{
  using super_type = std::string;
  using self_type  = string;

public:
  using std::string::string;

  // ToDo: This broke when switching to swoc::TextView
  // using std::string::operator cripts::string_view;
  using super_type::operator+=;
  using super_type::operator[];

  self_type &
  operator=(const self_type &str)
  {
    super_type::operator=(str);
    return *this;
  }

  self_type &
  operator=(const cripts::string_view &str)
  {
    super_type::operator=(str);
    return *this;
  }

  self_type &
  operator=(const char *str)
  {
    super_type::operator=(str);
    return *this;
  }

  string(const self_type &that) : super_type(that) {}

  // This allows for a std::string to be moved to a cripts::string
  string(super_type &&that) : super_type(std::move(that)) {}
  string(self_type &&that) noexcept : super_type(that) {}

  operator cripts::string_view() const { return {this->c_str(), this->size()}; }

  // Make sure to keep these updated with C++20 ...
  self_type &
  ltrim(char c = ' ')
  {
    this->erase(0, this->find_first_not_of(c));
    return *this;
  }

  self_type &
  rtrim(char c = ' ')
  {
    this->erase(this->find_last_not_of(c) + 1);
    return *this;
  }

  self_type &
  trim(char c = ' ')
  {
    return this->ltrim(c).rtrim(c);
  }

  self_type &
  ltrim(const char *chars = " \t\r\n")
  {
    this->erase(0, this->find_first_not_of(chars));
    return *this;
  }

  self_type &
  rtrim(const char *chars = " \t\r\n")
  {
    this->erase(this->find_last_not_of(chars) + 1);
    return *this;
  }

  self_type &
  trim(const char *chars)
  {
    return this->ltrim(chars).rtrim(chars);
  }

  [[nodiscard]] std::vector<cripts::string_view> split(char delim) const &;

  // delete the rvalue ref overload to prevent dangling string_views
  // If you are getting an error here, you need to assign a variable to the cripts::string before calling split
  // and make sure its lifetime is longer than the returned vector
  [[nodiscard]] std::vector<cripts::string_view> split(char delim) const && = delete;

  operator integer() const;
  operator bool() const;
  operator float() const { return std::stod(*this); }

  [[nodiscard]] integer
  ToInteger() const
  {
    return integer(*this);
  }

  [[nodiscard]] double
  ToFloat() const
  {
    return float(*this);
  }

  [[nodiscard]] bool
  ToBool() const
  {
    return bool(*this);
  }

}; // End class cripts::string

// Some helper functions, in the cripts:: generic namespace
int                              Random(int max);
std::vector<cripts::string_view> Splitter(cripts::string_view input, char delim);
cripts::string                   Hex(const cripts::string &str);
cripts::string                   Hex(cripts::string_view sv);
cripts::string                   UnHex(const cripts::string &str);
cripts::string                   UnHex(cripts::string_view sv);

class Control
{
  class Base
  {
    using self_type = Base;

  public:
    Base()                            = delete;
    Base(const self_type &)           = delete;
    void operator=(const self_type &) = delete;

    explicit Base(TSHttpCntlType ctrl) : _ctrl(ctrl) {}
    bool _get(cripts::Context *context) const;
    void _set(cripts::Context *context, bool flag);

  protected:
    TSHttpCntlType _ctrl;
  };

  class Cache
  {
    using self_type = Cache;

  public:
    Cache()                           = default;
    Cache(const self_type &)          = delete;
    void operator=(const self_type &) = delete;

    Base response{TS_HTTP_CNTL_RESPONSE_CACHEABLE};
    Base request{TS_HTTP_CNTL_REQUEST_CACHEABLE};
    Base nostore{TS_HTTP_CNTL_SERVER_NO_STORE};
  };

public:
  Cache cache;
  Base  logging{TS_HTTP_CNTL_LOGGING_MODE};
  Base  intercept{TS_HTTP_CNTL_INTERCEPT_RETRY_MODE};
  Base  debug{TS_HTTP_CNTL_TXN_DEBUG};
  Base  remap{TS_HTTP_CNTL_SKIP_REMAPPING};

}; // End class Control

class Versions
{
  using self_type = Versions;

public:
  Versions()                        = default;
  Versions(const self_type &)       = delete;
  void operator=(const self_type &) = delete;

  cripts::string_view GetSV();

  operator cripts::string_view() { return GetSV(); }

  cripts::string_view::const_pointer
  Data()
  {
    return GetSV().data();
  }

  cripts::string_view::size_type
  Size()
  {
    return GetSV().size();
  }

  cripts::string_view::size_type
  Length()
  {
    return GetSV().length();
  }

private:
  class Major
  {
    using self_type = Major;

  public:
    Major()                           = default;
    Major(const self_type &)          = delete;
    void operator=(const self_type &) = delete;

    operator integer() const // This should not be explicit
    {
      return TSTrafficServerVersionGetMajor();
    }

  }; // End class Versions::Major

  class Minor
  {
    using self_type = Minor;

  public:
    Minor()                           = default;
    Minor(const self_type &)          = delete;
    void operator=(const self_type &) = delete;

    operator integer() const // This should not be explicit
    {
      return TSTrafficServerVersionGetMinor();
    }

  }; // End class Versions::Minor

  class Patch
  {
    using self_type = Minor;

  public:
    Patch()                           = default;
    Patch(const self_type &)          = delete;
    void operator=(const self_type &) = delete;

    operator integer() const // This should not be explicit
    {
      return TSTrafficServerVersionGetPatch();
    }

  }; // End class Versions::Patch

  friend struct fmt::formatter<Versions::Major>;
  friend struct fmt::formatter<Versions::Minor>;
  friend struct fmt::formatter<Versions::Patch>;

  cripts::string_view _version;

public:
  Major major;
  Minor minor;
  Patch patch;
}; // End class Versions

} // namespace cripts

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<cripts::Versions> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(cripts::Versions &version, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", version.GetSV());
  }
};

template <> struct formatter<cripts::Versions::Major> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(cripts::Versions::Major &major, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", integer(major));
  }
};

template <> struct formatter<cripts::Versions::Minor> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(cripts::Versions::Minor &minor, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", integer(minor));
  }
};

template <> struct formatter<cripts::Versions::Patch> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(cripts::Versions::Patch &patch, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", integer(patch));
  }
};

template <> struct formatter<TSHttpStatus> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const TSHttpStatus &stat, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", static_cast<int>(stat));
  }
};

} // namespace fmt
