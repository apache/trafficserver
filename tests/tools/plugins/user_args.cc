/** @file

  Test and verify all the user args APIs.

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

#include <ts/ts.h>
#include <ts/remap.h>
#include <cstring>
#include <cstdio>

using ArgIndexes = struct {
  int    TXN, SSN, VCONN, GLB;
  TSCont contp;
};

const char *PLUGIN_NAME = "user_args_test";
ArgIndexes  gIX;

bool
set_header(TSMBuffer bufp, TSMLoc hdr_loc, const char *header, const char *val)
{
  if (!bufp || !hdr_loc || !header || !val) {
    return false;
  }

  bool   ret       = false;
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, header, strlen(header));

  TSReleaseAssert(!field_loc); // The headers are for testing, should never exist
  if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(bufp, hdr_loc, header, strlen(header), &field_loc)) {
    if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, val, strlen(val))) {
      TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
      ret = true;
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }

  return ret;
}

static int
cont_global(TSCont /* contp ATS_UNUSED */, TSEvent /* event ATS_UNUSED */, void *edata)
{
  TSHttpTxn txnp   = static_cast<TSHttpTxn>(edata);
  TSHttpSsn ssnp   = TSHttpTxnSsnGet(txnp);
  TSVConn   vconnp = TSHttpSsnClientVConnGet(ssnp);

  TSUserArgSet(txnp, gIX.TXN, (void *)"Transaction Data");
  TSUserArgSet(ssnp, gIX.SSN, (void *)"Session Data");
  TSUserArgSet(vconnp, gIX.VCONN, (void *)"VConn Data");

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

static int
cont_remap(TSCont contp, TSEvent /* event ATS_UNUSED */, void *edata)
{
  TSMBuffer   bufp;
  TSMLoc      hdrs;
  ArgIndexes *ix     = static_cast<ArgIndexes *>(TSContDataGet(contp));
  TSHttpTxn   txnp   = static_cast<TSHttpTxn>(edata);
  TSHttpSsn   ssnp   = TSHttpTxnSsnGet(txnp);
  TSVConn     vconnp = TSHttpSsnClientVConnGet(ssnp);

  if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &bufp, &hdrs)) {
    set_header(bufp, hdrs, "X-Arg-GLB", static_cast<const char *>(TSUserArgGet(nullptr, ix->GLB)));
    set_header(bufp, hdrs, "X-Arg-TXN", static_cast<const char *>(TSUserArgGet(txnp, ix->TXN)));
    set_header(bufp, hdrs, "X-Arg-SSN", static_cast<const char *>(TSUserArgGet(ssnp, ix->SSN)));
    set_header(bufp, hdrs, "X-Arg-VCONN", static_cast<const char *>(TSUserArgGet(vconnp, ix->VCONN)));
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

// Called by ATS as our initialization point
void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>("user_args");
  info.vendor_name   = const_cast<char *>("apache");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  if (TS_SUCCESS != TSUserArgIndexReserve(TS_USER_ARGS_TXN, PLUGIN_NAME, "User args tests TXN", &gIX.TXN)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to reserve TXN arg.", PLUGIN_NAME);
    return;
  } else if (TS_SUCCESS != TSUserArgIndexReserve(TS_USER_ARGS_SSN, PLUGIN_NAME, "User args tests SSN", &gIX.SSN)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to reserve SSN arg.", PLUGIN_NAME);
    return;
  } else if (TS_SUCCESS != TSUserArgIndexReserve(TS_USER_ARGS_VCONN, PLUGIN_NAME, "User args tests VCONN", &gIX.VCONN)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to reserve VCONN arg.", PLUGIN_NAME);
    return;
  } else if (TS_SUCCESS != TSUserArgIndexReserve(TS_USER_ARGS_GLB, PLUGIN_NAME, "User args tests GLB", &gIX.GLB)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to reserve GLB arg.", PLUGIN_NAME);
    return;
  }

  // Setup the global slot value
  TSUserArgSet(nullptr, gIX.GLB, (void *)"Global Data");

  gIX.contp = TSContCreate(cont_global, nullptr);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, gIX.contp);
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  TSReleaseAssert(api_info != nullptr);
  TSReleaseAssert(api_info->size == sizeof(TSRemapInterface));
  TSReleaseAssert(api_info->tsremap_version == TSREMAP_VERSION);

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int /* argc ATS_UNUSED */, char ** /* argv ATS_UNUSED */, void **ih, char * /* errbuf ATS_UNUSED */,
                   int /* errbuf_size ATS_UNUSED */)
{
  ArgIndexes *ix = new ArgIndexes;

  ix->contp = TSContCreate(cont_remap, nullptr);
  TSContDataSet(ix->contp, static_cast<void *>(ix));

  if (TS_SUCCESS != TSUserArgIndexNameLookup(TS_USER_ARGS_TXN, PLUGIN_NAME, &ix->TXN, nullptr)) {
    TSError("[%s] Failed to lookup TXN arg.", PLUGIN_NAME);
    return TS_ERROR;
  } else if (TS_SUCCESS != TSUserArgIndexNameLookup(TS_USER_ARGS_SSN, PLUGIN_NAME, &ix->SSN, nullptr)) {
    TSError("[%s] Failed to lookup SSN arg.", PLUGIN_NAME);
    return TS_ERROR;
  } else if (TS_SUCCESS != TSUserArgIndexNameLookup(TS_USER_ARGS_VCONN, PLUGIN_NAME, &ix->VCONN, nullptr)) {
    TSError("[%s] Failed to lookup VCONN arg.", PLUGIN_NAME);
    return TS_ERROR;
  } else if (TS_SUCCESS != TSUserArgIndexNameLookup(TS_USER_ARGS_GLB, PLUGIN_NAME, &ix->GLB, nullptr)) {
    TSError("[%s] Failed to lookup GLB arg.", PLUGIN_NAME);
    return TS_ERROR;
  }

  *ih = static_cast<void *>(ix);
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  ArgIndexes *ix = static_cast<ArgIndexes *>(ih);

  TSContDestroy(ix->contp);
  delete ix;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri ATS_UNUSED */)
{
  ArgIndexes *ix = static_cast<ArgIndexes *>(ih);

  TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, ix->contp);
  TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_MOVED_TEMPORARILY);

  return TSREMAP_DID_REMAP;
}
