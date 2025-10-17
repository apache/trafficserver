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
#include "regex_helper.h"
#include "lulu.h"
#include "ts/ts.h"
#include "tsutil/Regex.h"

bool
regexHelper::setRegexMatch(const std::string &s, bool nocase)
{
  std::string error;
  int         errorOffset;

  regexString = s;

  if (!regex.compile(regexString, error, errorOffset, nocase ? RE_CASE_INSENSITIVE : 0)) {
    TSError("[%s] Invalid regex: failed to precompile: %s (%s at %d)", PLUGIN_NAME, s.c_str(), error.c_str(), errorOffset);
    return false;
  }
  return true;
}

int
regexHelper::regexMatch(std::string_view subject, RegexMatches &matches) const
{
  return regex.exec(subject, matches);
};
