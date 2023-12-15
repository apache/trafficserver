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

#include <random>
#include <filesystem>

#include <ts/ts.h>
#include <ts/remap.h>
#include <cstring>
#include <string>
#include <string_view>
#include <charconv>
#include <type_traits>

#include <swoc/TextView.h>
#include <fmt/core.h>

// Silly typedef's for some PODs
using integer = int64_t;
using boolean = bool;

using namespace std::string_view_literals; // Gives ""sv for example, which can be convenient ...
using namespace std::literals;             // For e.g. 24h for times
namespace CFS = std::filesystem;           // Simplify the usage of std::filesystem

integer integer_helper(std::string_view sv);

namespace Cript
{
// Use Cript::string_view consistently, so that it's a one-stop shop for all string_view needs.
using string_view = swoc::TextView;

namespace details
{
  template <typename T> std::vector<T> splitter(T input, char delim);

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
  using mixin_type = Cript::string_view;

public:
  constexpr StringViewMixin() = default;
  constexpr StringViewMixin(const char *s) { _value = mixin_type(s, strlen(s)); }
  constexpr StringViewMixin(const char *s, mixin_type::size_type count) { _value = mixin_type(s, count); }
  constexpr StringViewMixin(Cript::string_view &str) { _value = str; }

  virtual self_type &operator=(const mixin_type str) = 0;

  // ToDo: We need the toFloat() and toBool() methods here
  operator integer() const { return integer_helper(_value); }

  [[nodiscard]] integer
  toInteger() const
  {
    return integer(*this);
  }

  [[nodiscard]] integer
  asInteger() const
  {
    return integer(*this);
  }

  std::vector<mixin_type>
  splitter(mixin_type input, char delim)
  {
    return details::splitter<mixin_type>(input, delim);
  }

  [[nodiscard]] std::vector<mixin_type>
  split(char delim)
  {
    return splitter(_value, delim);
  }

  [[nodiscard]] mixin_type
  getSV() const
  {
    return _value;
  }

  self_type &
  clear()
  {
    _value = Cript::string_view();
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

  // ToDo: There are other members of std::string_view /swoc::TextView that we may want to incorporate here,
  // to make the mixin class more complete.

  // ToDo: These are additions made by us, would be in C++20
  ChildT &
  ltrim(char c)
  {
    _value.remove_prefix(_value.find_first_not_of(c));
    return *(static_cast<ChildT *>(this));
  }

  ChildT &
  rtrim(char c)
  {
    auto n = _value.find_last_not_of(c);

    _value.remove_suffix(_value.size() - (n == mixin_type::npos ? 0 : n + 1));
    return *(static_cast<ChildT *>(this));
  }

  ChildT &
  trim(char c)
  {
    this->ltrim(c);
    this->rtrim(c);
    return *(static_cast<ChildT *>(this));
  }

  ChildT &
  ltrim(const char *chars = " \t\r\n")
  {
    _value.remove_prefix(_value.find_first_not_of(chars));
    return *(static_cast<ChildT *>(this));
  }

  ChildT &
  rtrim(const char *chars = " \t\r\n")
  {
    auto n = _value.find_last_not_of(chars);

    _value.remove_suffix(_value.size() - (n == mixin_type::npos ? 0 : n + 1));
    return *(static_cast<ChildT *>(this));
  }

  ChildT &
  trim(const char *chars = " \t")
  {
    this->ltrim(chars);
    this->rtrim(chars);

    return *(static_cast<ChildT *>(this));
  }

  [[nodiscard]] constexpr char const *
  data_end() const noexcept
  {
    return _value.data() + _value.size();
  }

  [[nodiscard]] bool
  ends_with(Cript::string_view const suffix) const
  {
    return _value.size() >= suffix.size() && 0 == ::memcmp(this->data_end() - suffix.size(), suffix.data(), suffix.size());
  }

  [[nodiscard]] bool
  starts_with(Cript::string_view const prefix) const
  {
    return _value.size() >= prefix.size() && 0 == ::memcmp(_value.data(), prefix.data(), prefix.size());
  }

protected:
  void
  _setSV(const mixin_type str)
  {
    _value = str;
  }

private:
  mixin_type _value;
}; // End class Cript::StringViewMixin

// ToDo: This is wonky right now, but once we're fully on ATS v10.0.0, we should just
// eliminate all this and use swoc::TextView directly and consistently.
class StringViewWrapper : public StringViewMixin<StringViewWrapper>
{
  using self_type  = StringViewWrapper;
  using super_type = StringViewMixin<self_type>;

public:
  using super_type::super_type;
  StringViewWrapper &
  operator=(const Cript::string_view str) override
  {
    _setSV(str);
    return *this;
  }
};

// ToDo: Should this be a mixin class too ?
class string : public std::string
{
  using super_type = std::string;
  using self_type  = string;

public:
  using std::string::string;

  // ToDo: This broke when switching to swoc::TextView
  // using std::string::operator Cript::string_view;
  using super_type::operator+=;
  using super_type::operator[];

  self_type &
  operator=(const self_type &str)
  {
    super_type::operator=(str);
    return *this;
  }

  self_type &
  operator=(const Cript::string_view &str)
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

  // This allows for a std::string to be moved to a Cript::string
  string(super_type &&that) : super_type(std::move(that)) {}
  string(self_type &&that) : super_type(std::move(that)) {}

  // ToDo: This seems to be ambiquous with STL implementation
  //  operator Cript::string_view() const { return {this->c_str(), this->size()}; }

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

  [[nodiscard]] std::vector<Cript::string_view> split(char delim) const &;

  // delete the rvalue ref overload to prevent dangling string_views
  // If you are getting an error here, you need to assign a variable to the Cript::string before calling split
  // and make sure its lifetime is longer than the returned vector
  [[nodiscard]] std::vector<Cript::string_view> split(char delim) const && = delete;

  operator integer() const;
  operator bool() const;
  operator float() const { return std::stod(*this); }

  [[nodiscard]] integer
  toInteger() const
  {
    return integer(*this);
  }

  [[nodiscard]] integer
  asInteger() const
  {
    return integer(*this);
  }

  [[nodiscard]] double
  toFloat() const
  {
    return float(*this);
  }

  [[nodiscard]] bool
  toBool() const
  {
    return bool(*this);
  }

}; // End class Cript::string

// Helpers for deducting if a type is a static string
template <typename T> struct isStatic : std::false_type {
};

template <std::size_t N> struct isStatic<const char[N]> : std::true_type {
};

template <std::size_t N> struct isStatic<char[N]> : std::true_type {
};

// Some helper functions, in the Cript:: generic namespace
int random(int max);
std::vector<Cript::string_view> splitter(Cript::string_view input, char delim);
Cript::string hex(const Cript::string &str);
Cript::string hex(Cript::string_view sv);
Cript::string unhex(const Cript::string &str);
Cript::string unhex(Cript::string_view sv);

} // namespace Cript

class Control
{
  class Base
  {
    using self_type = Base;

  public:
    Base()                       = delete;
    Base(const Base &)           = delete;
    void operator=(const Base &) = delete;

    explicit Base(TSHttpCntlType ctrl) : _ctrl(ctrl) {}
    bool _get(Cript::Context *context) const;
    void _set(Cript::Context *context, bool flag);

  protected:
    TSHttpCntlType _ctrl;
  };

  class Cache
  {
    using self_type = Cache;

  public:
    Cache()                       = default;
    Cache(const Cache &)          = delete;
    void operator=(const Cache &) = delete;

    Base response{TS_HTTP_CNTL_RESPONSE_CACHEABLE};
    Base request{TS_HTTP_CNTL_REQUEST_CACHEABLE};
    Base nostore{TS_HTTP_CNTL_SERVER_NO_STORE};
  };

public:
  Cache cache;
  Base logging{TS_HTTP_CNTL_LOGGING_MODE};
  Base intercept{TS_HTTP_CNTL_INTERCEPT_RETRY_MODE};
  Base debug{TS_HTTP_CNTL_TXN_DEBUG};
  Base remap{TS_HTTP_CNTL_SKIP_REMAPPING};

}; // End class Control

class Versions
{
  using self_type = Versions;

public:
  Versions()                       = default;
  Versions(const Versions &)       = delete;
  void operator=(const Versions &) = delete;

  Cript::string_view
  getSV()
  {
    if (_version.length() == 0) {
      const char *ver = TSTrafficServerVersionGet();

      _version = Cript::string_view(ver, strlen(ver)); // ToDo: Annoyingly, we have ambiquity on the operator=
    }

    return _version;
  }

  operator Cript::string_view() { return getSV(); }

  Cript::string_view::const_pointer
  data()
  {
    return getSV().data();
  }

  Cript::string_view::size_type
  size()
  {
    return getSV().size();
  }

  Cript::string_view::size_type
  length()
  {
    return getSV().length();
  }

private:
  class Major
  {
    using self_type = Major;

  public:
    Major()                       = default;
    Major(const Major &)          = delete;
    void operator=(const Major &) = delete;

    operator integer() const // This should not be explicit
    {
      return TSTrafficServerVersionGetMajor();
    }

  }; // End class Versions::Major

  class Minor
  {
    using self_type = Minor;

  public:
    Minor()                       = default;
    Minor(const Minor &)          = delete;
    void operator=(const Minor &) = delete;

    operator integer() const // This should not be explicit
    {
      return TSTrafficServerVersionGetMinor();
    }

  }; // End class Versions::Minor

  class Patch
  {
    using self_type = Minor;

  public:
    Patch()                       = default;
    Patch(const Patch &)          = delete;
    void operator=(const Patch &) = delete;

    operator integer() const // This should not be explicit
    {
      return TSTrafficServerVersionGetPatch();
    }

  }; // End class Versions::Patch

  friend struct fmt::formatter<Versions::Major>;
  friend struct fmt::formatter<Versions::Minor>;
  friend struct fmt::formatter<Versions::Patch>;

  Cript::string_view _version;

public:
  Major major;
  Minor minor;
  Patch patch;
}; // End class Versions

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<Versions> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Versions &version, FormatContext &ctx) -> decltype(ctx.out())
  {
    return format_to(ctx.out(), "{}", version.getSV());
  }
};

template <> struct formatter<Versions::Major> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Versions::Major &major, FormatContext &ctx) -> decltype(ctx.out())
  {
    return format_to(ctx.out(), "{}", integer(major));
  }
};

template <> struct formatter<Versions::Minor> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Versions::Minor &minor, FormatContext &ctx) -> decltype(ctx.out())
  {
    return format_to(ctx.out(), "{}", integer(minor));
  }
};

template <> struct formatter<Versions::Patch> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Versions::Patch &patch, FormatContext &ctx) -> decltype(ctx.out())
  {
    return format_to(ctx.out(), "{}", integer(patch));
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
    return format_to(ctx.out(), "{}", static_cast<int>(stat));
  }
};

template <> struct formatter<Cript::StringViewWrapper> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const Cript::StringViewWrapper &sv, FormatContext &ctx) -> decltype(ctx.out())
  {
    return format_to(ctx.out(), "{}", sv.getSV());
  }
};

} // namespace fmt
