/**
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
 * @file logging_internal.h
 *
 *
 * @brief logging used inside the atscppapi library.
 */

#pragma once

#include "tscpp/api/Logger.h"

// Because we have the helper in Logger.h with the same name.
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif

#ifdef LOG_ERROR
#undef LOG_ERROR
#endif

#define LOG_DEBUG(fmt, ...) TS_DEBUG("atscppapi", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) TS_ERROR("atscppapi", fmt, ##__VA_ARGS__)
