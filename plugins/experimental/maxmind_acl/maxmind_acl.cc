/*
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

#include "mmdb.h"

static int
config_handler(TSCont cont, TSEvent event, void *edata)
{
  TSMutex mutex;

  mutex = TSContMutexGet(cont);
  TSMutexLock(mutex);

  TSDebug(PLUGIN_NAME, "In config Handler");
  Acl *a = static_cast<Acl *>(TSContDataGet(cont));

  // strdup for const string return
  char *config = strdup(a->get_state()->config_file.c_str());
  a->init(config);
  free(config);
  TSMutexUnlock(mutex);

  // Don't reschedule for TS_EVENT_MGMT_UPDATE
  if (event == TS_EVENT_TIMEOUT) {
    TSContScheduleOnPool(cont, CONFIG_TMOUT, TS_THREAD_POOL_TASK);
  }
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin as a remap plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[tsremap_init] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[tsremap_init] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "remap plugin is successfully initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf */, int /* errbuf_size */)
{
  TSCont config_cont;

  if (argc < 3) {
    TSError("[%s] Unable to create remap instance, missing configuration file", PLUGIN_NAME);
    return TS_ERROR;
  }

  Acl *a = new Acl();
  *ih    = static_cast<void *>(a);
  if (!a->init(argv[2])) {
    TSError("[%s] Failed to initialize maxmind with %s", PLUGIN_NAME, argv[2]);
    return TS_ERROR;
  }

  config_cont = TSContCreate(config_handler, TSMutexCreate());
  TSContDataSet(config_cont, static_cast<void *>(a));
  TSMgmtUpdateRegister(config_cont, PLUGIN_NAME);

  TSDebug(PLUGIN_NAME, "created remap instance with configuration %s", argv[2]);
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  if (nullptr != ih) {
    Acl *const a = static_cast<Acl *>(ih);
    delete a;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Main entry point when used as a remap plugin.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  if (nullptr == ih) {
    TSDebug(PLUGIN_NAME, "No ACLs configured");
  } else {
    Acl *a = static_cast<Acl *>(ih);
    if (!a->eval(rri, rh)) {
      TSDebug(PLUGIN_NAME, "denying request");
      TSHttpTxnStatusSet(rh, TS_HTTP_STATUS_FORBIDDEN);
    }
  }
  return TSREMAP_NO_REMAP;
}
