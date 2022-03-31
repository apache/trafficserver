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

#if !defined(UNITTEST)

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define DEBUG_LOG(fmt, ...) TSDebug(PLUGIN_NAME, "[%s:% 4d] %s(): " fmt, __FILENAME__, __LINE__, __func__, ##__VA_ARGS__)

#define ERROR_LOG(fmt, ...)                                                                         \
  TSError("[%s/%s:% 4d] %s(): " fmt, PLUGIN_NAME, __FILENAME__, __LINE__, __func__, ##__VA_ARGS__); \
  TSDebug(PLUGIN_NAME, "[%s:%04d] %s(): " fmt, __FILENAME__, __LINE__, __func__, ##__VA_ARGS__)

#else

#define DEBUG_LOG(fmt, ...)
#define ERROR_LOG(fmt, ...)

#endif
