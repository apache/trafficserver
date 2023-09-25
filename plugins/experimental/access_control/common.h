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
 * @file common.h
 * @brief Common declarations and definitions (header file).
 */

#pragma once

#define PLUGIN_NAME "access_control"

#include <functional>
#include <string>
#include <set>
#include <list>
#include <vector>
#include <map>
#include <sstream>
#include <iostream>

using String       = std::string;
using StringView   = std::string_view;
using StringSet    = std::set<std::string>;
using StringList   = std::list<std::string>;
using StringVector = std::vector<std::string>;
using StringMap    = std::map<std::string, std::string>;

#ifdef ACCESS_CONTROL_UNIT_TEST
#include <stdio.h>
#include <stdarg.h>

#define AccessControlDebug(fmt, ...) \
  PrintToStdErr("(%s) %s:%d:%s() " fmt "\n", PLUGIN_NAME, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define AccessControlError(fmt, ...) \
  PrintToStdErr("(%s) %s:%d:%s() " fmt "\n", PLUGIN_NAME, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
void PrintToStdErr(const char *fmt, ...);

#else /* ACCESS_CONTROL_UNIT_TEST */
#include "ts/ts.h"

#define AccessControlDebug(fmt, ...)                                              \
  do {                                                                            \
    Dbg(dbg_ctl, "%s:%d:%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
  } while (0)

#define AccessControlError(fmt, ...)                                              \
  do {                                                                            \
    TSError("(%s) " fmt, PLUGIN_NAME, ##__VA_ARGS__);                             \
    Dbg(dbg_ctl, "%s:%d:%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
  } while (0)

namespace access_control_ns
{
extern DbgCtl dbg_ctl;
}
using namespace access_control_ns;

#endif /* ACCESS_CONTROL_UNIT_TEST */

int string2int(const StringView &s);
time_t string2time(const StringView &s);
