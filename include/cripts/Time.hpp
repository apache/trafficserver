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

#include <ctime>

#include "ts/ts.h"
#include "ts/remap.h"

#include "cripts/Lulu.hpp"

// This is lame, but until C++20, we're missing important features from
// std::chrono :-/ Todo: Rewrite this with std::chrono when it has things like
// std::chrono::year_month_day

namespace cripts::Time
{
using Clock = std::chrono::system_clock;
using Point = Clock::time_point;
} // namespace cripts::Time

namespace detail
{
class BaseTime
{
  using self_type = detail::BaseTime;

public:
  BaseTime()                        = default;
  BaseTime(const self_type &)       = delete;
  void operator=(const self_type &) = delete;

  operator integer() const { return Epoch(); }

  [[nodiscard]] integer
  Epoch() const
  {
    return static_cast<integer>(_now);
  }

  [[nodiscard]] integer
  Year() const
  {
    return static_cast<integer>(_result.tm_year) + 1900;
  }

  [[nodiscard]] integer
  Month() const
  {
    return static_cast<integer>(1 + _result.tm_mon);
  }

  [[nodiscard]] integer
  Day() const
  {
    return static_cast<integer>(_result.tm_mday);
  }

  [[nodiscard]] integer
  Hour() const
  {
    return static_cast<integer>(_result.tm_hour);
  }

  [[nodiscard]] integer
  Minute() const
  {
    return static_cast<integer>(_result.tm_min);
  }

  [[nodiscard]] integer
  Second() const
  {
    return static_cast<integer>(_result.tm_sec);
  }

  [[nodiscard]] integer
  WeekDay() const
  {
    return static_cast<integer>(_result.tm_wday) + 1;
  }

  [[nodiscard]] integer
  YearDay() const
  {
    return static_cast<integer>(_result.tm_yday) + 1;
  }

  [[nodiscard]] const cripts::string_view
  ToDate()
  {
    int len = sizeof(_buffer);

    TSMimeFormatDate(_now, _buffer, &len);
    return {_buffer, len};
  }

protected:
  char        _buffer[64] = {};
  std::time_t _now        = std::time(nullptr);
  std::tm     _result     = {};
};
} // namespace detail

namespace cripts::Time
{
class Local : public detail::BaseTime
{
  using super_type = detail::BaseTime;
  using self_type  = Local;

public:
  Local(const self_type &)          = delete;
  void operator=(const self_type &) = delete;

  Local() { localtime_r(&_now, static_cast<struct tm *>(&_result)); }

  // Factory, for consistency with ::get()
  static Local
  Now()
  {
    return {};
  }

  explicit Local(Time::Point tp)
  {
    _now = Time::Clock::to_time_t(tp);
    localtime_r(&_now, static_cast<struct tm *>(&_result));
  }

}; // End class Time::Local

class UTC : public detail::BaseTime
{
  using super_type = detail::BaseTime;
  using self_type  = UTC;

public:
  UTC(const self_type &)            = delete;
  void operator=(const self_type &) = delete;

  UTC() { gmtime_r(&_now, static_cast<struct tm *>(&_result)); }

  explicit UTC(Time::Point tp)
  {
    _now = Time::Clock::to_time_t(tp);
    gmtime_r(&_now, static_cast<struct tm *>(&_result));
  }

  static UTC
  Now()
  {
    return {};
  }
};

} // namespace cripts::Time

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<cripts::Time::Local> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const cripts::Time::Local &time, FormatContext &ctx) const -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", time.Epoch());
  }
};

template <> struct formatter<cripts::Time::UTC> {
  constexpr auto
  parse(const format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const cripts::Time::UTC &time, FormatContext &ctx) const -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", time.Epoch());
  }
};

} // namespace fmt
