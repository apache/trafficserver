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


//////////////////////////////////////////////////////////////////////////////////////////////
// 
// Main entry points for the plugin hooks etc.
//
#include "ts/ts.h"
#include "ts/remap.h"
#include "ink_config.h"

#include <stdio.h>

#include "rules.h"

using namespace ::HeaderFilter;

// Global plugin rules
Rules global;
int arg_idx;

// TODO: Maybe we should use wrappers for TSmalloc() for pcre_malloc (and _free),
// but since we only compile at config time, it's really not that important.


///////////////////////////////////////////////////////////////////////////////
// Continuation
//
static int
cont_header_filter(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn) edata;
  TSHttpHookID hook = TS_HTTP_LAST_HOOK;
  TSMBuffer reqp;
  TSMLoc hdr_loc;

  // Get the resources necessary to process this event
  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &reqp, &hdr_loc))
      hook = TS_HTTP_READ_REQUEST_HDR_HOOK;
    break;
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    if (TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &reqp, &hdr_loc))
      hook = TS_HTTP_SEND_REQUEST_HDR_HOOK;
    break;
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &reqp, &hdr_loc))
      hook = TS_HTTP_READ_RESPONSE_HDR_HOOK;
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &reqp, &hdr_loc))
      hook = TS_HTTP_SEND_RESPONSE_HDR_HOOK;
    break;
  default:
    TSError("header_filter: unknown event for this plugin");
    TSDebug(PLUGIN_NAME, "unknown event for this plugin");
    break;
  }

  if (hook != TS_HTTP_LAST_HOOK) {
    Rules* from_remap;

    global.execute(reqp, hdr_loc, hook);

    if (TS_HTTP_READ_REQUEST_HDR_HOOK != hook) { // Don't run the hook handled by remap plugin
      if ((from_remap = (Rules*)TSHttpTxnArgGet(txnp, arg_idx))) {
        from_remap->execute(reqp, hdr_loc, hook);
      }
    }
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Initialize the InkAPI plugin for the global hooks we support.
//
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name = const_cast<char*>(PLUGIN_NAME);
  info.vendor_name = const_cast<char*>("Apache Software Foundation");
  info.support_email = const_cast<char*>("dev@trafficserver.apache.org");

  if (TSPluginRegister(TS_SDK_VERSION_3_0 , &info) != TS_SUCCESS) {
    TSError("header_filter: plugin registration failed.\n"); 
  }

  // Parse the rules file
  if ((argc > 1)) {
    if (!global.parse_file(argv[1]))
      TSError("header_filter: failed to parse configuration file");
  }

  TSCont cont = TSContCreate(cont_header_filter, NULL);

  for (int i=TS_HTTP_READ_REQUEST_HDR_HOOK; i < TS_HTTP_LAST_HOOK; ++i) {
    if (global.supported_hook(static_cast<TSHttpHookID>(i))) {
      TSDebug(PLUGIN_NAME, "Registering hook %d", i);
      TSHttpHookAdd(static_cast<TSHttpHookID>(i), cont);
    }
  }
  if (TSHttpArgIndexReserve(PLUGIN_NAME, "Filter out headers in various hooks", &arg_idx) != TS_SUCCESS) {
    TSError("header_filter: failed to reserve private data slot");
  }
}


///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin as a remap plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface* api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[tsremap_init] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[tsremap_init] - Incorrect API version %ld.%ld",
             api_info->tsremap_version >> 16, (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "remap plugin is successfully initialized");
  return TS_SUCCESS;                     /* success */
}


TSReturnCode
TSRemapNewInstance(int argc, char* argv[], void** ih, char* /* errbuf ATS_UNUSED */, int /* errbuf_size */)
{
  if (argc < 3) {
    TSError("Unable to create remap instance, need rules file");
    return TS_ERROR;
  } else {
    Rules* conf = new(Rules);

    conf->parse_file(argv[2]);
    *ih = static_cast<void*>(conf);
  }

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void* ih)
{
  Rules* conf = static_cast<Rules*>(ih);

  delete conf;
}


///////////////////////////////////////////////////////////////////////////////
// Main entry point when used as a remap plugin.
//
TSRemapStatus
TSRemapDoRemap(void* ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  if (NULL == ih) {
    TSDebug(PLUGIN_NAME, "No Rules configured, falling back to default mapping rule");
  } else {
    Rules* confp = static_cast<Rules*>(ih);

    TSHttpTxnArgSet(rh, arg_idx, static_cast<void*>(ih)); // Save for later hooks
    confp->execute(rri->requestBufp, rri->requestHdrp, TS_HTTP_READ_REQUEST_HDR_HOOK);
  }

  return TSREMAP_NO_REMAP;
}


/*
  local variables:
  mode: C++
  indent-tabs-mode: nil
  c-basic-offset: 2
  c-comment-only-line-offset: 0
  c-file-offsets: ((statement-block-intro . +)
  (label . 0)
  (statement-cont . +)
  (innamespace . 0))
  end:

  Indent with: /usr/bin/indent -ncs -nut -npcs -l 120 logstats.cc
*/
