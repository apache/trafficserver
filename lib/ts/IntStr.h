/** @file

    Class for efficient conversion from signed and unsigned 64-bit integers to strings.

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

#include <cstdint>

#include <ts/string_view.h>

namespace ts
{
class IntStr
{
public:
  static const std::size_t MaxSize = 20;

  IntStr(std::uint64_t v) { _gen(v); }

  IntStr(std::int64_t v)
  {
    if (v >= 0) {
      _gen(v);

    } else {
      _gen(static_cast<std::uint64_t>(-v));

      _buf[MaxSize - (++_size)] = '-';
    }
  }

  const char *
  data() const
  {
    return _buf + MaxSize - _size;
  }

  std::size_t
  size() const
  {
    return _size;
  }

  operator ts::string_view() const { return ts::string_view(data(), _size); }

private:
  char _buf[MaxSize];

  std::size_t _size;

  void
  _gen(std::uint64_t v)
  {
    if (v) {
      int i = MaxSize - 1;

      for (;;) {
        _buf[i] = (v % 10) + '0';
        v /= 10;
        if (!v) {
          break;
        }
        --i;
      }

      _size = MaxSize - i;

    } else {
      _buf[MaxSize - 1] = '0';
      _size             = 1;
    }
  }
};

} // end namespace ts
