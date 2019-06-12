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

/**
 * @file common.cc
 * @brief Common declarations and definitions.
 * @see common.h
 */

#include "common.h"

#ifdef ACCESS_CONTROL_UNIT_TEST

void
PrintToStdErr(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
}

#endif /* ACCESS_CONTROL_UNIT_TEST */

int
string2int(const StringView &s)
{
  time_t t = 0;
  try {
    t = (time_t)std::stoi(String(s));
  } catch (...) {
    /* Failed to convert return impossible value */
    return 0;
  }
  return t;
}
