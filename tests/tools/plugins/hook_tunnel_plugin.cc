/** @file

  Test adding continuation from same hook point

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
#include <string.h>

#define PLUGIN_NAME "test"

static int stat_tunnel_start = 0; // number of TS_HTTP_TUNNEL_START hooks caught
static int stat_http_req     = 0; // number of TS_HTTP_READ_REQUEST_HDR hooks caught
static int stat_error        = 0;
static int stat_test_done    = 0;

static int
tunnelStart(TSCont cont, TSEvent event, void *edata)
{
  TSHttpTxn txnp   = reinterpret_cast<TSHttpTxn>(edata);
  TSTxnType retval = TSHttpTxnTypeGet(txnp);
  TSDebug(PLUGIN_NAME, "tunnelStart event=%d type=%d", event, retval);

  TSStatIntIncrement(stat_tunnel_start, 1);
  if (retval != TS_TXN_TYPE_EXPLICIT_TUNNEL || event != TS_EVENT_HTTP_TUNNEL_START) {
    TSStatIntIncrement(stat_error, 1);
    TSDebug(PLUGIN_NAME, "tunnelStart unexpected type");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_SUCCESS;
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static int
transactionStart(TSCont cont, TSEvent event, void *edata)
{
  TSHttpTxn txnp   = reinterpret_cast<TSHttpTxn>(edata);
  TSTxnType retval = TSHttpTxnTypeGet(txnp);
  TSDebug(PLUGIN_NAME, "transactionStart event=%d type=%d", event, retval);

  TSStatIntIncrement(stat_http_req, 1);
  if (retval != TS_TXN_TYPE_HTTP || event != TS_EVENT_HTTP_READ_REQUEST_HDR) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_SUCCESS;
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static int
handleMsg(TSCont cont, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "handleMsg event=%d", event);
  TSStatIntIncrement(stat_test_done, 1);
  return TS_SUCCESS;
}

void
TSPluginInit(int argc, const char **argv)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.support_email = const_cast<char *>("shinrich@apache.org");
  info.vendor_name   = const_cast<char *>("Apache");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[" PLUGIN_NAME "] plugin registration failed\n");
    return;
  }
  stat_tunnel_start = TSStatCreate("txn_type_verify.tunnel.start", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  stat_http_req     = TSStatCreate("txn_type_verify.http.req", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  stat_error        = TSStatCreate("txn_type_verify.error", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  stat_test_done    = TSStatCreate("txn_type_verify.test.done", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);

  TSCont start_tunnel_contp = TSContCreate(tunnelStart, TSMutexCreate());
  TSCont start_txn_contp    = TSContCreate(transactionStart, TSMutexCreate());
  TSCont msg_contp          = TSContCreate(handleMsg, TSMutexCreate());

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, start_txn_contp);
  TSHttpHookAdd(TS_HTTP_TUNNEL_START_HOOK, start_tunnel_contp);
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, msg_contp);
  return;
}
