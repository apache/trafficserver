/** @ja3_utils.cc
  Plugin JA3 Fingerprint calculates JA3 signatures for incoming SSL traffic.
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

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace ja3
{

// GREASE table as in ja3
static std::unordered_set<std::uint16_t> const GREASE_table = {0x0a0a, 0x1a1a, 0x2a2a, 0x3a3a, 0x4a4a, 0x5a5a, 0x6a6a, 0x7a7a,
                                                               0x8a8a, 0x9a9a, 0xaaaa, 0xbaba, 0xcaca, 0xdada, 0xeaea, 0xfafa};

static constexpr std::uint16_t
from_big_endian(unsigned char lowbyte, unsigned char highbyte)
{
  return (static_cast<std::uint16_t>(lowbyte) << 8) + highbyte;
}

static bool
ja3_should_ignore(std::uint16_t n)
{
  return GREASE_table.find(n) != GREASE_table.end();
}

std::string
encode_word_buffer(unsigned char const *buf, int const len)
{
  std::string result;
  if (len > 0) {
    // Benchmarks show that reserving space in the string here would cause
    // a 40% increase in runtime for a buffer with 10 elements... so we
    // don't do it.
    result.append(std::to_string(buf[0]));
    std::for_each(buf + 1, buf + len, [&result](unsigned char i) {
      result.push_back('-');
      result.append(std::to_string(i));
    });
  }
  return result;
}

std::string
encode_dword_buffer(unsigned char const *buf, int const len)
{
  std::string result;
  auto        it{buf};
  while (it < buf + len && ja3_should_ignore(from_big_endian(it[0], it[1]))) {
    it += 2;
  }
  if (it < buf + len) {
    // Benchmarks show that reserving buf.size() - 1 space in the string here
    // would have no impact on performance. Since the string may not even need
    // that much due to GREASE values present in the buffer, we don't do it.
    result.append(std::to_string(from_big_endian(it[0], it[1])));
    it += 2;
    for (; it < buf + len; it += 2) {
      auto value{from_big_endian(it[0], it[1])};
      if (!ja3_should_ignore(value)) {
        result.push_back('-');
        result.append(std::to_string(value));
      }
    }
  }
  return result;
}

std::string
encode_integer_buffer(int const *buf, int const len)
{
  std::string result;
  auto        it{std::find_if(buf, buf + len, [](int i) { return !ja3_should_ignore(i); })};
  if (it < buf + len) {
    result.append(std::to_string(*it));
    std::for_each(it + 1, buf + len, [&result](int const i) {
      if (!ja3_should_ignore(i)) {
        result.push_back('-');
        result.append(std::to_string(i));
      }
    });
  }
  return result;
}

} // end namespace ja3
