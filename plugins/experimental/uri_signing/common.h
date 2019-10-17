/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#define PLUGIN_NAME "uri_signing"

#ifdef URI_SIGNING_UNIT_TEST
#include <stdio.h>
#include <stdarg.h>

#define PluginDebug(fmt, ...) PrintToStdErr("(%s) %s:%d:%s() " fmt "\n", PLUGIN_NAME, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define PluginError(fmt, ...) PrintToStdErr("(%s) %s:%d:%s() " fmt "\n", PLUGIN_NAME, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define TSmalloc(x) malloc(x)
#define TSfree(p) free(p)
void PrintToStdErr(const char *fmt, ...);

#else

#include "ts/ts.h"
#define PluginDebug(...) TSDebug("uri_signing", PLUGIN_NAME " " __VA_ARGS__)
#define PluginError(...) PluginDebug(__VA_ARGS__), TSError(PLUGIN_NAME " " __VA_ARGS__)

#endif
