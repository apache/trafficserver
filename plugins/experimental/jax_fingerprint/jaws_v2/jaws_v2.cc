/** @file

  JAWS v2 encoder for parsed TLS ClientHello summaries.

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

#include "bitset_hex.h"
#include "jaws_v2.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
// clang-format off
constexpr std::array<std::uint16_t, 66> CIPHER_ANCHORS_V2 = {
  0x0000,
  0xC02F,
  0xC02B,
  0xC02C,
  0xC030,
  0xC013,
  0xC014,
  0x1302,
  0x1301,
  0xCCA8,
  0xCCA9,
  0x002F,
  0x009C,
  0x0035,
  0x009D,
  0x1303,
  0xC009,
  0xC00A,
  0xC027,
  0xC023,
  0xC028,
  0xC024,
  0x003C,
  0x003D,
  0x000A,
  0x009E,
  0x009F,
  0xC012,
  0xC008,
  0xCCAA,
  0x00FF,
  0x0033,
  0x0039,
  0x0067,
  0x006B,
  0x0032,
  0x0038,
  0x0040,
  0x006A,
  0x00A2,
  0x00A3,
  0xC0AD,
  0xC0AC,
  0xC09D,
  0xC09C,
  0xC09F,
  0xC09E,
  0xC0AF,
  0xC0AE,
  0xC0A1,
  0xC0A0,
  0xC0A3,
  0xC0A2,
  0xC004,
  0xC00E,
  0xC025,
  0xC029,
  0xC02D,
  0xC031,
  0xC005,
  0xC00F,
  0xC026,
  0xC02A,
  0xC02E,
  0xC032,
  0x1304,
};

constexpr std::array<std::uint16_t, 25> EXTENSION_ANCHORS_V2 = {
  0xFFFF,
  0x000A,
  0x000D,
  0x0000,
  0x0017,
  0x000B,
  0x002B,
  0x0033,
  0xFF01,
  0x002D,
  0x0005,
  0x0010,
  0x0023,
  0x0012,
  0x0015,
  0x001B,
  0x0029,
  0x0031,
  0x0032,
  0x0016,
  0x3374,
  0x000F,
  0x0011,
  0x0018,
  0x001C,
};

constexpr std::array<std::uint16_t, 31> CURVE_ANCHORS_V2 = {
  0x0000,
  0x0017,
  0x0018,
  0x001D,
  0x0019,
  0x11EC,
  0x001E,
  0x0100,
  0x0101,
  0x0102,
  0x0103,
  0x0104,
  0x6399,
  0x0016,
  0x0009,
  0x000B,
  0x000A,
  0x000C,
  0x000D,
  0x000E,
  0x001A,
  0x001B,
  0x001C,
  0x0015,
  0x0013,
  0x0001,
  0x0003,
  0xFE32,
  0x11EB,
  0x11ED,
  0x4138,
};

constexpr std::array<std::uint8_t, 4> POINT_FORMAT_ANCHORS_V2 = {
  0xFF,
  0x00,
  0x01,
  0x02,
};
// clang-format on

template <typename T, std::size_t N>
std::size_t
find_anchor_position(std::array<T, N> const &anchors, T value)
{
  if (auto const it{std::find(anchors.begin() + 1, anchors.end(), value)}; it != anchors.end()) {
    return static_cast<std::size_t>(std::distance(anchors.begin(), it));
  }
  return 0;
}

template <std::size_t N>
std::string
encode_point_format_section(std::vector<std::uint8_t> const &values, std::array<std::uint8_t, N> const &anchors)
{
  std::bitset<N> bits;
  std::size_t    count{0};

  if (values.empty()) {
    bits.set(0);
  } else {
    for (auto value : values) {
      ++count;
      bits.set(find_anchor_position(anchors, value));
    }
  }

  std::string result{std::to_string(count)};
  result.push_back('-');
  result.append(ja3::hex::hexify_bitset(bits));
  return result;
}

template <std::size_t N>
std::string
encode_u16_section(std::vector<std::uint16_t> const &values, std::array<std::uint16_t, N> const &anchors, bool saw_grease,
                   bool track_grease)
{
  std::bitset<N> bits;
  std::size_t    count{0};

  for (auto value : values) {
    if (ja3::is_GREASE(value)) {
      continue;
    }
    ++count;
    bits.set(find_anchor_position(anchors, value));
  }

  std::string result{std::to_string(count)};
  if (track_grease && saw_grease) {
    result.push_back('g');
  }
  result.push_back('-');
  result.append(ja3::hex::hexify_bitset(bits));
  return result;
}
} // namespace

std::string
ja3::jaws_v2::fingerprint(ClientHelloSummary const &summary, bool track_grease)
{
  std::string result{"j2:"};
  result.append(std::to_string(summary.effective_tls_version));
  result.push_back('|');
  result.append(encode_u16_section(summary.ciphers, CIPHER_ANCHORS_V2, summary.ciphers_have_grease, track_grease));
  result.push_back('|');
  result.append(encode_u16_section(summary.extensions, EXTENSION_ANCHORS_V2, summary.extensions_have_grease, track_grease));
  result.push_back('|');
  result.append(encode_u16_section(summary.curves, CURVE_ANCHORS_V2, summary.curves_have_grease, track_grease));
  result.push_back('|');
  result.append(encode_point_format_section(summary.point_formats, POINT_FORMAT_ANCHORS_V2));
  return result;
}
