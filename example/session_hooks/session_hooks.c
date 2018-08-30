/** @file

  An example plugin that demonstrates session hook usage.

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

#include <stdio.h>
#include "ts/ts.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "session_hooks"

static int transaction_count_stat;
static int session_count_stat;

static void
txn_handler(TSHttpTxn txnp, TSCont contp)
{
  TSMgmtInt num_txns = 0;

  TSStatIntIncrement(transaction_count_stat, 1);
  num_txns = TSStatIntGet(transaction_count_stat);
  TSDebug(PLUGIN_NAME, "The number of transactions is %" PRId64, num_txns);
}

static void
handle_session(TSHttpSsn ssnp, TSCont contp)
{
  TSMgmtInt num_ssn = 0;

  TSStatIntIncrement(session_count_stat, 1);
  num_ssn = TSStatIntGet(session_count_stat);
  TSDebug(PLUGIN_NAME, "The number of sessions is %" PRId64, num_ssn);
  TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_START_HOOK, contp);
}

static int
ssn_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpSsn ssnp;
  TSHttpTxn txnp;

  switch (event) {
  case TS_EVENT_HTTP_SSN_START:

    ssnp = (TSHttpSsn)edata;
    handle_session(ssnp, contp);
    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
    return 0;

  case TS_EVENT_HTTP_TXN_START:
    txnp = (TSHttpTxn)edata;
    txn_handler(txnp, contp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;

  default:
    TSDebug(PLUGIN_NAME, "In the default case: event = %d", event);
    break;
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSCont contp;
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed.\n", PLUGIN_NAME);

    goto error;
  }

  transaction_count_stat = TSStatCreate("transaction.count", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  session_count_stat     = TSStatCreate("session.count", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);

  contp = TSContCreate(ssn_handler, NULL);
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, contp);

error:
  TSError("[%s] Plugin not initialized", PLUGIN_NAME);
}
