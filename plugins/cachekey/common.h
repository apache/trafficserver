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

#ifndef PLUGINS_EXPERIMENTAL_CACHEKEY_COMMON_H_
#define PLUGINS_EXPERIMENTAL_CACHEKEY_COMMON_H_

#define PLUGIN_NAME "cachekey"

#include <string>
#include <set>
#include <list>
#include <vector>

typedef std::string String;
typedef std::set<std::string> StringSet;
typedef std::list<std::string> StringList;
typedef std::vector<std::string> StringVector;

#ifdef CACHEKEY_UNIT_TEST
#include <stdio.h>
#include <stdarg.h>

#define CacheKeyDebug(fmt, ...) PrintToStdErr("(%s) %s:%d:%s() " fmt "\n", PLUGIN_NAME, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define CacheKeyError(fmt, ...) PrintToStdErr("(%s) %s:%d:%s() " fmt "\n", PLUGIN_NAME, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
void PrintToStdErr(const char *fmt, ...);

#else /* CACHEKEY_UNIT_TEST */
#include "ts/ts.h"

#define CacheKeyDebug(fmt, ...)                                                           \
  do {                                                                                    \
    TSDebug(PLUGIN_NAME, "%s:%d:%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
  } while (0)

#define CacheKeyError(fmt, ...)                                                           \
  do {                                                                                    \
    TSError("(%s) " fmt, PLUGIN_NAME, ##__VA_ARGS__);                                     \
    TSDebug(PLUGIN_NAME, "%s:%d:%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
  } while (0)
#endif /* CACHEKEY_UNIT_TEST */

#endif /* PLUGINS_EXPERIMENTAL_CACHEKEY_COMMON_H_ */
