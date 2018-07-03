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
#include <string_view>
#include <ts/TextView.h>
#include <ts/BufferWriterForward.h>

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

} // namespace bwf

BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, bwf::Errno const &e);
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, bwf::Date const &date);

} // namespace ts
