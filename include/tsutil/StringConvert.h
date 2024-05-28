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
