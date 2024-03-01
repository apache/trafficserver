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

#include "ts/remap.h"
#include "ts/ts.h"

// This is lame, but until C++20, we're missing important features from
// std::chrono :-/ Todo: Rewrite this with std::chrono when it has things like
// std::chrono::year_month_day

namespace detail
{
class BaseTime
{
  using self_type = detail::BaseTime;

public:
  BaseTime()                       = default;
  BaseTime(const BaseTime &)       = delete;
  void operator=(const BaseTime &) = delete;

  operator integer() const { return epoch(); }

  [[nodiscard]] integer
  epoch() const
  {
    return static_cast<integer>(_now);
  }

  [[nodiscard]] integer
  year() const
  {
    return static_cast<integer>(_result.tm_year) + 1900;
  }

  [[nodiscard]] integer
  month() const
  {
    return static_cast<integer>(1 + _result.tm_mon);
  }

  [[nodiscard]] integer
  day() const
  {
    return static_cast<integer>(_result.tm_mday);
  }

  [[nodiscard]] integer
  hour() const
  {
    return static_cast<integer>(_result.tm_hour);
  }

  [[nodiscard]] integer
  minute() const
  {
    return static_cast<integer>(_result.tm_min);
  }

  [[nodiscard]] integer
  second() const
  {
    return static_cast<integer>(_result.tm_sec);
  }

  [[nodiscard]] integer
  weekday() const
  {
    return static_cast<integer>(_result.tm_wday) + 1;
  }

  [[nodiscard]] integer
  yearday() const
  {
    return static_cast<integer>(_result.tm_yday) + 1;
  }

protected:
  std::time_t _now = std::time(nullptr);
  std::tm _result  = {};
};
} // namespace detail

namespace Time
{
// ToDo: Right now, we only have localtime, but we should support e.g. UTC and
// other time zone instances.
class Local : public detail::BaseTime
{
  using super_type = detail::BaseTime;
  using self_type  = Local;

public:
  Local() { localtime_r(&_now, static_cast<struct tm *>(&_result)); }
  Local(const Local &)          = delete;
  void operator=(const Local &) = delete;

  // Factory, for consistency with ::get()
  static Local
  now()
  {
    return {};
  }

}; // End class Time::Local

} // namespace Time

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<Time::Local> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Time::Local &time, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", time.epoch());
  }
};
} // namespace fmt
