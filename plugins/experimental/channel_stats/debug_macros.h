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

#ifndef _DBG_MACROS_H
#define _DBG_MACROS_H

#include "ts/ink_defs.h"

#define TAG PLUGIN_NAME
#define API_TAG PLUGIN_NAME ".api"

#define debug_tag(tag, fmt, ...)          \
  do {                                    \
    if (unlikely(TSIsDebugTagSet(tag))) { \
      TSDebug(tag, fmt, ##__VA_ARGS__);   \
    }                                     \
  } while (0)

#define debug(fmt, ...) debug_tag(TAG, "DEBUG: [%s:%d] [%s] " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);

#define info(fmt, ...) debug_tag(TAG, "INFO: " fmt, ##__VA_ARGS__);

#define warning(fmt, ...) debug_tag(TAG, "WARNING: " fmt, ##__VA_ARGS__);

#define error(fmt, ...)                                                                          \
  do {                                                                                           \
    TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);        \
    debug_tag(TAG, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
  } while (0)

#define fatal(fmt, ...)                                                                          \
  do {                                                                                           \
    TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);        \
    debug_tag(TAG, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    exit(-1);                                                                                    \
  } while (0)

#define debug_api(fmt, ...) debug_tag(API_TAG, "DEBUG: [%s:%d] [%s] " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);

#define error_api(fmt, ...)                                                                          \
  do {                                                                                               \
    TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);            \
    debug_tag(API_TAG, "ERROR: [%s:%d] [%s] " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
  } while (0)

#define HRTIME_FOREVER (10 * HRTIME_DECADE)
#define HRTIME_DECADE (10 * HRTIME_YEAR)
#define HRTIME_YEAR (365 * HRTIME_DAY + HRTIME_DAY / 4)
#define HRTIME_WEEK (7 * HRTIME_DAY)
#define HRTIME_DAY (24 * HRTIME_HOUR)
#define HRTIME_HOUR (60 * HRTIME_MINUTE)
#define HRTIME_MINUTE (60 * HRTIME_SECOND)
#define HRTIME_SECOND (1000 * HRTIME_MSECOND)
#define HRTIME_MSECOND (1000 * HRTIME_USECOND)
#define HRTIME_USECOND (1000 * HRTIME_NSECOND)
#define HRTIME_NSECOND (1LL)

#endif //_DBG_MACROS_H
