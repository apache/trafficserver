/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/* slicer.cc:  plugin to break GET requests into blocks.
 *
 */

#pragma once

#include "ts/ts.h"

#include <cstring>

#ifndef SLICER_EXPORT
#define SLICER_EXPORT extern "C" tsapi
#endif

#define PLUGIN_NAME "slicer"

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define DEBUG_LOG(fmt, ...) TSDebug(PLUGIN_NAME, "[%s:%05d] %s(): " fmt, __FILENAME__, __LINE__, __func__, ##__VA_ARGS__)

#define ERROR_LOG(fmt, ...) TSError("[%s:%05d] %s(): " fmt, __FILENAME__, __LINE__, __func__, ##__VA_ARGS__)

#define ALLOC_DEBUG_LOG(fmt, ...)
