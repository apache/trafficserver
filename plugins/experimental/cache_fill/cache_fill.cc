/** @file

    Plugin to perform background fetches of certain content that would
    otherwise not be cached. For example, Range: requests / responses.

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

#include <string>
#include <iostream>
#include <unordered_map>
#include <cinttypes>
#include <string_view>
#include <array>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ts/ts.h"
#include "ts/remap.h"
#include "background_fetch.h"

static const char *
getCacheLookupResultName(TSCacheLookupResult result)
{
  switch (result) {
  case TS_CACHE_LOOKUP_MISS:
    return "TS_CACHE_LOOKUP_MISS";
    break;
  case TS_CACHE_LOOKUP_HIT_STALE:
    return "TS_CACHE_LOOKUP_HIT_STALE";
    break;
  case TS_CACHE_LOOKUP_HIT_FRESH:
    return "TS_CACHE_LOOKUP_HIT_FRESH";
    break;
  case TS_CACHE_LOOKUP_SKIPPED:
    return "TS_CACHE_LOOKUP_SKIPPED";
    break;
  default:
    return "UNKNOWN_CACHE_LOOKUP_EVENT";
    break;
  }
  return "UNKNOWN_CACHE_LOOKUP_EVENT";
}

///////////////////////////////////////////////////////////////////////////
// create background fetch request if possible
//
static bool
cont_check_cacheable(TSHttpTxn txnp)
{
  if (TSHttpTxnIsInternal(txnp)) {
    return false;
  }
  int lookupStatus;
  TSHttpTxnCacheLookupStatusGet(txnp, &lookupStatus);
  TSDebug(PLUGIN_NAME, "lookup status: %s", getCacheLookupResultName(static_cast<TSCacheLookupResult>(lookupStatus)));
  bool ret = false;
  if (TS_CACHE_LOOKUP_MISS == lookupStatus || TS_CACHE_LOOKUP_HIT_STALE == lookupStatus) {
    bool const nostore = TSHttpTxnServerRespNoStoreGet(txnp);

    TSDebug(PLUGIN_NAME, "is nostore set %d", nostore);
    if (!nostore) {
      TSMBuffer request;
      TSMLoc req_hdr;
      if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &request, &req_hdr)) {
        BgFetchData *data = new BgFetchData();
        // Initialize the data structure (can fail) and acquire a privileged lock on the URL
        if (data->initialize(request, req_hdr, txnp) && data->acquireUrl()) {
          TSDebug(PLUGIN_NAME, "scheduling background fetch");
          data->schedule();
          ret = true;
        } else {
          delete data;
        }
      }
      TSHandleMLocRelease(request, TS_NULL_MLOC, req_hdr);
    }
  }
  return ret;
}

//////////////////////////////////////////////////////////////////////////////
// Main "plugin", which is a global TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE hook. Before
// initiating a background fetch, this checks
// if a background fetch is allowed for this request
//
static int
cont_handle_cache(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  if (TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE == event) {
    bool const requested = cont_check_cacheable(txnp);
    if (requested) // Made a background fetch request, do not cache the response
    {
      TSDebug(PLUGIN_NAME, "setting no store");
      TSHttpTxnServerRespNoStoreSet(txnp, 1);
      TSHttpTxnCacheLookupStatusSet(txnp, TS_CACHE_LOOKUP_MISS);
    }

  } else {
    TSError("[%s] Unknown event for this plugin %d", PLUGIN_NAME, event);
    TSDebug(PLUGIN_NAME, "unknown event for this plugin %d", event);
  }

  // Reenable and continue with the state machine.
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

///////////////////////////////////////////////////////////////////////////
// Setup Remap mode
///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSDebug(PLUGIN_NAME, "cache fill remap init");
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "cache fill remap is successfully initialized");
  return TS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// We don't have any specific "instances" here, at least not yet.
//
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf */, int /* errbuf_size */)
{
  TSCont cont = TSContCreate(cont_handle_cache, nullptr);
  *ih         = cont;
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  TSCont cont = static_cast<TSCont>(ih);
  TSContDestroy(cont);
}

///////////////////////////////////////////////////////////////////////////////
//// This is the main "entry" point for the plugin, called for every request.
////
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri */)
{
  if (nullptr == ih) {
    return TSREMAP_NO_REMAP;
  }
  TSCont const cont = static_cast<TSCont>(ih);
  TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);
  TSDebug(PLUGIN_NAME, "TSRemapDoRemap() added hook");

  return TSREMAP_NO_REMAP;
}
