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

namespace std
{
template <typename T>
ts::BufferWriter &
bwformat(ts::BufferWriter &w, ts::BWFSpec const &spec, std::atomic<T> const &v)
{
  return ts::bwformat(w, spec, v.load());
}
} // end namespace std

namespace ts
{
namespace bwf
{
  struct Errno {
    int _e;
    explicit Errno(int e) : _e(e) {}
  };

  struct Date {
    time_t _epoch;
    std::string_view _fmt;
    Date(time_t t, std::string_view fmt = "%Y %b %d %H:%M:%S"sv) : _epoch(t), _fmt(fmt) {}
    Date(std::string_view fmt = "%Y %b %d %H:%M:%S"sv);
  };
} // namespace bwf

BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, bwf::Errno const &e);
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, bwf::Date const &date);

} // namespace ts
