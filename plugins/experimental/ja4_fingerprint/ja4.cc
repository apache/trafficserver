/** @file ja3_fingerprint.cc
 *
  JA4 fingerprint calculation.

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

#include "ja4.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>

static char        convert_protocol_to_char(JA4::Protocol protocol);
static std::string convert_TLS_version_to_string(std::uint16_t TLS_version);
static char        convert_SNI_to_char(JA4::SNI SNI_type);
static std::string convert_count_to_two_digit_string(std::size_t count);
static std::string convert_ALPN_to_two_char_string(std::string_view ALPN);
static void        remove_trailing_character(std::string &s);
static std::string hexify(std::uint16_t n);

namespace
{
constexpr std::size_t U16_HEX_BUF_SIZE{4};
} // end anonymous namespace

std::string
JA4::make_JA4_a_raw(TLSClientHelloSummary const &TLS_summary)
{
  std::string result;
  result.reserve(9);
  result.push_back(convert_protocol_to_char(TLS_summary.protocol));
  result.append(convert_TLS_version_to_string(TLS_summary.TLS_version));
  result.push_back(convert_SNI_to_char(TLS_summary.SNI_type));
  result.append(convert_count_to_two_digit_string(TLS_summary.get_cipher_count()));
  result.append(convert_count_to_two_digit_string(TLS_summary.get_extension_count()));
  result.append(convert_ALPN_to_two_char_string(TLS_summary.ALPN));
  return result;
}

static char
convert_protocol_to_char(JA4::Protocol protocol)
{
  return static_cast<char>(protocol);
}

static std::string
convert_TLS_version_to_string(std::uint16_t TLS_version)
{
  switch (TLS_version) {
  case 0x304:
    return "13";
  case 0x303:
    return "12";
  case 0x302:
    return "11";
  case 0x301:
    return "10";
  case 0x300:
    return "s3";
  case 0x200:
    return "s2";
  case 0x100:
    return "s1";
  case 0xfeff:
    return "d1";
  case 0xfefd:
    return "d2";
  case 0xfefc:
    return "d3";
  default:
    return "00";
  }
}

static char
convert_SNI_to_char(JA4::SNI SNI_type)
{
  return static_cast<char>(SNI_type);
}

static std::string
convert_count_to_two_digit_string(std::size_t count)
{
  std::string result;
  if (count <= 9) {
    result.push_back('0');
  }
  // We could also clamp the lower bound to 1 since there must be at least 1
  // cipher, but 0 is more helpful for debugging if the cipher list is empty.
  result.append(std::to_string(std::clamp(count, std::size_t{0}, std::size_t{99})));
  return result;
}

std::string
convert_ALPN_to_two_char_string(std::string_view ALPN)
{
  std::string result;
  if (ALPN.empty()) {
    result = "00";
  } else {
    result.push_back(ALPN.front());
    result.push_back(ALPN.back());
  }
  return result;
}

std::string
JA4::make_JA4_b_raw(TLSClientHelloSummary const &TLS_summary)
{
  std::string result;
  result.reserve(12);
  std::vector temp = TLS_summary.get_ciphers();
  std::sort(temp.begin(), temp.end());

  for (auto cipher : temp) {
    result.append(hexify(cipher));
    result.push_back(',');
  }
  remove_trailing_character(result);
  return result;
}

std::string
JA4::make_JA4_c_raw(TLSClientHelloSummary const &TLS_summary)
{
  std::string result;
  result.reserve(12);
  std::vector temp = TLS_summary.get_extensions();
  std::sort(temp.begin(), temp.end());

  for (auto extension : temp) {
    result.append(hexify(extension));
    result.push_back(',');
  }
  remove_trailing_character(result);
  return result;
}

void
remove_trailing_character(std::string &s)
{
  if (!s.empty()) {
    s.pop_back();
  }
}

std::string
hexify(std::uint16_t n)
{
  char result[U16_HEX_BUF_SIZE + 1]{};
  std::snprintf(result, sizeof(result), "%.4x", n);
  return result;
}
