/** @file

  Pure encoders for JA3 raw and JA3 hash fingerprints.

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

#include "ja3_fingerprints.h"

#include <openssl/md5.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr int ja3_hash_included_byte_count{16};
static_assert(ja3_hash_included_byte_count <= MD5_DIGEST_LENGTH);
constexpr int ja3_hash_hex_string_with_null_terminator_length{2 * ja3_hash_included_byte_count + 1};

template <typename T>
std::string
join_values(std::vector<T> const &values)
{
  std::string result;
  bool        first{true};
  for (auto value : values) {
    if (!first) {
      result.push_back('-');
    }
    first = false;
    result.append(std::to_string(value));
  }
  return result;
}

std::string
join_u16_values(std::vector<std::uint16_t> const &values, bool preserve_grease)
{
  std::string result;
  bool        first{true};
  for (auto value : values) {
    if (!preserve_grease && ja3::is_GREASE(value)) {
      continue;
    }
    if (!first) {
      result.push_back('-');
    }
    first = false;
    result.append(std::to_string(value));
  }
  return result;
}

std::string
md5_hex(std::string_view input)
{
  char          fingerprint[ja3_hash_hex_string_with_null_terminator_length]{};
  unsigned char digest[MD5_DIGEST_LENGTH];
  MD5(reinterpret_cast<unsigned char const *>(input.data()), input.size(), digest);
  for (int i{0}; i < ja3_hash_included_byte_count; ++i) {
    std::snprintf(&(fingerprint[i * 2]), sizeof(fingerprint) - (i * 2), "%02x", static_cast<unsigned int>(digest[i]));
  }
  return {fingerprint};
}

} // end anonymous namespace

std::string
ja3::make_ja3_raw(ClientHelloSummary const &summary, bool preserve_grease)
{
  std::string result;
  result.append(std::to_string(summary.legacy_version));
  result.push_back(',');
  result.append(join_u16_values(summary.ciphers, preserve_grease));
  result.push_back(',');
  result.append(join_u16_values(summary.extensions, preserve_grease));
  result.push_back(',');
  result.append(join_u16_values(summary.curves, preserve_grease));
  result.push_back(',');
  result.append(join_values(summary.point_formats));
  return result;
}

std::string
ja3::make_ja3_hash(ClientHelloSummary const &summary)
{
  return md5_hex(make_ja3_raw(summary, false));
}
