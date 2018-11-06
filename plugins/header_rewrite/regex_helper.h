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

#include "tscore/ink_defs.h"

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#include <string>

const int OVECCOUNT = 30; // We support $1 - $9 only, and this needs to be 3x that

class regexHelper
{
public:
  ~regexHelper()
  {
    pcre_free(regex);
    pcre_free(regexExtra);
  }

  bool setRegexMatch(const std::string &s);
  int regexMatch(const char *, int, int ovector[]) const;

private:
  std::string regexString;
  pcre *regex            = nullptr;
  pcre_extra *regexExtra = nullptr;
  int regexCcount        = 0;
};
