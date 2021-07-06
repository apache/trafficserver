/** @file

  A test plugin for testing Plugin's Dynamic Shared Objects (DSO)

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

  @section details Details

  Implements code necessary for Reverse Proxy which mostly consists of
  general purpose hostname substitution in URLs.

 */

#include "plugin_testing_common.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "ts/ts.h"
#include "ts/remap.h"

PluginDebugObject debugObject;

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  debugObject.contextInit = pluginThreadContext;
  return TS_SUCCESS;
}

void
TSRemapDone(void)
{
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  return TSREMAP_NO_REMAP;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  debugObject.contextInitInstance = pluginThreadContext;

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *)
{
}

void
TSRemapOSResponse(void *ih, TSHttpTxn rh, int os_response_type)
{
}

void
TSPluginInit(int argc, const char *argv[])
{
}

void
TSRemapConfigReload(void)
{
}

/* This is meant for test with plugins of different versions */
int
pluginDsoVersionTest()
{
#ifdef PLUGINDSOVER
  return PLUGINDSOVER;
#else
  return -1;
#endif
}

void *
getPluginDebugObjectTest()
{
  return (void *)&debugObject;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
