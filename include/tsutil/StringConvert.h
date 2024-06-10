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

#include <charconv>
#include <stdexcept>
#include <string>
#include <system_error>

namespace ts
{

/**
 * Convert input string into a hex string
 *
 * Each character in input is interpreted as an unsigned char and represented as a two-digit hex value [0-255] in the returned
 * string.
 *
 * @param input Input string view
 * @return The hex representation of the input string
 *
 * @note this is a specialization of boost::algorithms::hex
 */
inline std::string
hex(const std::string_view input)
{
  std::string result;

  result.resize(input.size() * 2);

  char *p = result.data();
  for (auto x : input) {
    if (auto [ptr, err] = std::to_chars(p, result.data() + result.size(), x, 16); err == std::errc()) {
      p = ptr;
    } else {
      throw std::runtime_error(std::make_error_code(err).message().c_str());
    }
  }

  return result;
}

/**
 * Convert input hex string into a string
 *
 * The input string must have even size and be comprised of hex digits [0-f]. Each two-digit pair is converted from hex to a
 * character in the resulting output string.
 *
 * @param input Input string view
 * @return The unhexified result string
 *
 * @note this is a specialization of boost::algorithms::unhex
 */
inline std::string
unhex(const std::string_view input)
{
  std::string result;

  if (input.size() % 2 != 0) {
    throw std::invalid_argument("input to unhex needs to be an even size");
  }

  result.resize(input.size() / 2);

  const char *p = input.data();
  for (auto &x : result) {
    if (auto [ptr, err] = std::from_chars(p, p + 2, x, 16); err == std::errc()) {
      p = ptr;
    } else {
      throw std::runtime_error(std::make_error_code(err).message().c_str());
    }
  }

  return result;
}

} // namespace ts
