/** @file
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

#include "ts/ts.h"
#include "ts/experimental.h"

#include <cstring>
#include <string_view>

#ifndef SLICE_EXPORT
#define SLICE_EXPORT extern "C" tsapi
#endif

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "slice"
#endif

#ifndef COLLECT_STATS
#define COLLECT_STATS
#endif

constexpr std::string_view X_CRR_IMS_HEADER = {"X-Crr-Ims"};

#if !defined(UNITTEST)

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define DEBUG_LOG(fmt, ...)                                                      \
  TSDebug(PLUGIN_NAME, "[%s:% 4d] %s(): " fmt, __FILENAME__, __LINE__, __func__, \
          ##__VA_ARGS__) /*                                                      \
                                 ; fprintf(stderr, "[%s:%04d]: " fmt "\n"        \
                                         , __FILENAME__                          \
                                         , __LINE__                              \
                                         , ##__VA_ARGS__)                        \
                         */

#define ERROR_LOG(fmt, ...)                                                         \
  TSError("[%s:% 4d] %s(): " fmt, __FILENAME__, __LINE__, __func__, ##__VA_ARGS__); \
  TSDebug(PLUGIN_NAME, "[%s:%04d] %s(): " fmt, __FILENAME__, __LINE__, __func__, ##__VA_ARGS__)

#else

#define DEBUG_LOG(fmt, ...)
#define ERROR_LOG(fmt, ...)

#endif

#if defined(COLLECT_STATS)
namespace stats
{
extern int DataCreate;
extern int DataDestroy;
extern int Reader;
extern int Server;
extern int Client;
extern int RequestTime;
extern int FirstHeaderTime;
extern int NextHeaderTime;
extern int ServerTime;
extern int ClientTime;

struct StatsRAI {
  int m_statid;
  TSHRTime m_timebeg;

  StatsRAI(int statid) : m_statid(statid), m_timebeg(TShrtime()) {}

  ~StatsRAI()
  {
    TSHRTime const timeend = TShrtime();
    TSStatIntIncrement(m_statid, timeend - m_timebeg);
  }
};

} // namespace stats
#endif // COLLECT_STATS
