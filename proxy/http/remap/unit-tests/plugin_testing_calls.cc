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

#include "ts/ts.h"
#include "ts/remap.h"
#include "plugin_testing_common.h"
#include <iostream>

#include "../RemapPluginInfo.h"

PluginDebugObject debugObject;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

TSReturnCode
handleInitRun(char *errbuf, int errbuf_size, int &counter)
{
  TSReturnCode result = TS_SUCCESS;

  if (debugObject.fail) {
    result = TS_ERROR;
    snprintf(errbuf, errbuf_size, "%s", "Init failed");
  }

  counter++;

  return result;
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSReturnCode result = handleInitRun(errbuf, errbuf_size, debugObject.initCalled);
  return result;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  TSReturnCode result = handleInitRun(errbuf, errbuf_size, debugObject.initInstanceCalled);

  if (TS_SUCCESS == result) {
    *ih = debugObject.input_ih;
  }

  debugObject.argc = argc;
  debugObject.argv = argv;

  return result;
}

void
TSRemapDone(void)
{
  debugObject.doneCalled++;
}

void
TSRemapDeleteInstance(void *ih)
{
  debugObject.deleteInstanceCalled++;
  debugObject.ih = ih;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  debugObject.doRemapCalled++;
  return TSREMAP_NO_REMAP;
}

void
TSRemapOSResponse(void *ih, TSHttpTxn rh, int os_response_type)
{
}

void
TSRemapPreConfigReload(void)
{
  debugObject.preReloadConfigCalled++;
}

void
TSRemapPostConfigReload(TSRemapReloadStatus reloadStatus)
{
  debugObject.postReloadConfigCalled++;
  debugObject.postReloadConfigStatus = reloadStatus;
}

/* The folowing functions are meant for unit testing */
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
