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
  using self_type = Regex;

public:
  enum Flag {
    CASE_INSENSITIVE = 0x0001, // default is case sensitive
    UNANCHORED       = 0x0002, // default (for DFA) is to anchor at the first matching position
    ANCHORED         = 0x0004, // default (for Regex) is unanchored
  };

  Regex() = default;
  Regex(self_type &&that);

  bool compile(const char *pattern, const unsigned flags = 0);
  // It is safe to call exec() concurrently on the same object instance
  bool exec(std::string_view src) const;
  bool exec(std::string_view src, int *ovector, int ovecsize) const;
  int get_capture_count();
  ~Regex();

private:
  pcre *regex             = nullptr;
  pcre_extra *regex_extra = nullptr;
};

inline Regex::Regex(self_type &&that)
{
  std::swap(regex, that.regex);
  std::swap(regex_extra, that.regex_extra);
}
