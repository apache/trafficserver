/** @file

  Transforms content using gzip, deflate or brotli

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

#include <ts/ts.h>

#define TAG "compress"

#define debug(fmt, args...)                                                             \
  do {                                                                                  \
    TSDebug(TAG, "DEBUG: [%s:%d] [%s] " fmt, __FILE__, __LINE__, __FUNCTION__, ##args); \
  } while (0)

#define info(fmt, args...)              \
  do {                                  \
    TSDebug(TAG, "INFO: " fmt, ##args); \
  } while (0)

#define warning(fmt, args...)              \
  do {                                     \
    TSDebug(TAG, "WARNING: " fmt, ##args); \
  } while (0)

#define error(fmt, args...)                                                             \
  do {                                                                                  \
    TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args);      \
    TSDebug(TAG, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args); \
  } while (0)

#define fatal(fmt, args...)                                                             \
  do {                                                                                  \
    TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args);      \
    TSDebug(TAG, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args); \
    exit(-1);                                                                           \
  } while (0)
