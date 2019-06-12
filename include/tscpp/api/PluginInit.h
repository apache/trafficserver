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
 * @file PluginInit.h
 * @brief Provides hooks that plugins have to implement. ATS will invoke these when loading the plugin .so files.
 */

#pragma once
#include <ts/apidefs.h>
#include "tscpp/api/utils.h"
extern "C" {

/**
 * Invoked for "general" plugins - listed in plugin.config. The arguments in the
 * plugin.config line are provided in this invocation.
 *
 * @param argc Count of arguments
 * @param argv Array of pointers pointing to arguments
 */
void TSPluginInit(int argc, const char *argv[]);
/**
 * Invoked for remap plugins - listed in remap.config. The arguments provided as @pparam
 * in the remap.config line are provided in this invocation.
 *
 * @param argc Count of arguments
 * @param argv Array of pointers pointing to arguments
 * @param instance_handle Should be passed to the RemapPlugin constructor
 * @param errbuf Not used
 * @param errbuf_size Not used
 */
TSReturnCode TSRemapNewInstance(int argc, char *argv[], void **instance_handle, char *errbuf, int errbuf_size);
}
