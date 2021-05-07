/** @file

  Wrapper to make PCRE handling easier.

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

#include <pcre.h>
#include <algorithm>
#include <string_view>

class Regex
{
public:
  enum Flag {
    CASE_INSENSITIVE = 0x0001, // default is case sensitive
    UNANCHORED       = 0x0002, // default (for DFA) is to anchor at the first matching position
    ANCHORED         = 0x0004, // default (for Regex) is unanchored
  };

  Regex()              = default;
  Regex(Regex const &) = delete;
  Regex &operator=(Regex const &) = delete;
  Regex(Regex &&that);
  Regex &operator=(Regex &&that);

  bool compile(const char *pattern, const unsigned flags = 0);

  // check if valid regex
  bool is_valid() const;

  // check for simple match
  bool matches(std::string_view const &src) const;

  // match returning substring positions.
  int exec(std::string_view const &src, int *ovector, int const ovecsize) const;

  ~Regex();

private:
  pcre *regex             = nullptr;
  pcre_extra *regex_extra = nullptr;
};

inline Regex::Regex(Regex &&that)
{
  std::swap(regex, that.regex);
  std::swap(regex_extra, that.regex_extra);
}

inline Regex &
Regex::operator=(Regex &&that)
{
  if (&that != this) {
    std::swap(regex, that.regex);
    std::swap(regex_extra, that.regex_extra);
  }
  return *this;
}

inline bool
Regex::is_valid() const
{
  return nullptr != regex;
}
