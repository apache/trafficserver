/** @file

    ATS plugin to exercise the logging plugin APIs. This is not
    intended to be used for real traffic, but for tests.

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
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cinttypes>

#include "ts/ts.h"
#include "ts/remap.h"

static const char *PLUGIN_NAME = "test_logging";

///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "Plugin is successfully initialized");
  return TS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// We don't have any specific "instances" here, at least not yet.
//
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_sizeATS_UNUSED */)
{
  char *filename;
  TSTextLogObject log;

  if (argc != 3) {
    TSError("[%s] Unable to create remap instance, need exactly one parameter (the log filename)", PLUGIN_NAME);
    return TS_ERROR;
  }

  filename = argv[2]; // This just takes one argument, the name of the log file

  TSDebug(PLUGIN_NAME, "Created log object for %s", filename);
  if (TSTextLogObjectCreate(filename, TS_LOG_MODE_ADD_TIMESTAMP, &log) != TS_SUCCESS) {
    TSError("[%s] failed to create log file '%s'", PLUGIN_NAME, filename);
    return TS_ERROR;
  }

  TSTextLogObjectRollingEnabledSet(log, 1);
  TSTextLogObjectRollingIntervalSecSet(log, 300);
  TSTextLogObjectRollingSizeMbSet(log, 10);

  *ih = static_cast<void *>(log);

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  TSTextLogObject log = static_cast<TSTextLogObject>(ih);

  TSDebug(PLUGIN_NAME, "Destroyed log object");
  if (log) {
    TSTextLogObjectDestroy(log);
  }
}

///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  TSTextLogObject log = static_cast<TSTextLogObject>(ih);
  TSReturnCode ret    = TSTextLogObjectWrite(log, "Test logging code, SM id=%" PRId64, TSHttpTxnIdGet(txnp));

  if (TS_SUCCESS != ret) {
    TSError("[%s] failed to log", PLUGIN_NAME);
  }

  return TSREMAP_NO_REMAP;
}
