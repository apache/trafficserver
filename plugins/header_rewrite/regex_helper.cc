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

bool
regexHelper::setRegexMatch(const std::string &s)
{
  const char *errorComp  = nullptr;
  const char *errorStudy = nullptr;
  int erroffset;

  regexString = s;
  regex       = pcre_compile(regexString.c_str(), 0, &errorComp, &erroffset, nullptr);

  if (regex == nullptr) {
    return false;
  }
  regexExtra = pcre_study(regex, 0, &errorStudy);
  if ((regexExtra == nullptr) && (errorStudy != nullptr)) {
    return false;
  }
  if (pcre_fullinfo(regex, regexExtra, PCRE_INFO_CAPTURECOUNT, &regexCcount) != 0) {
    return false;
  }
  return true;
}

int
regexHelper::regexMatch(const char *str, int len, int ovector[]) const
{
  return pcre_exec(regex,      // the compiled pattern
                   regexExtra, // Extra data from study (maybe)
                   str,        // the subject std::string
                   len,        // the length of the subject
                   0,          // start at offset 0 in the subject
                   0,          // default options
                   ovector,    // output vector for substring information
                   OVECCOUNT); // number of elements in the output vector
};
