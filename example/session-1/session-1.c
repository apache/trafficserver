/** @file

  A brief file description

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

/* session-1.c: a plugin that illustrates how to use
 *                session hooks
 *
 *
 *  Usage: session-1.so
 *
 */

#include <stdio.h>
#include "ts/ts.h"
#include "ts/ink_defs.h"

static INKStat transaction_count;
static INKStat session_count;
static INKStat av_transaction;

static void
txn_handler(TSHttpTxn txnp, TSCont contp)
{
  int64_t num_txns = 0;

  INKStatIncrement(transaction_count);
  num_txns = INKStatIntGet(transaction_count);
  TSDebug("tag_session", "The number of transactions is %" PRId64, num_txns);
}

static void
handle_session(TSHttpSsn ssnp, TSCont contp)
{
  int64_t num_ssn = 0;

  INKStatIncrement(session_count);
  num_ssn = INKStatIntGet(session_count);
  TSDebug("tag_session", "The number of sessions is %" PRId64, num_ssn);
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
    TSDebug("tag_session", "In the default case: event = %d\n", event);
    break;
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSCont contp;
  TSPluginRegistrationInfo info;

  info.plugin_name   = "session-1";
  info.vendor_name   = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[session-1] Plugin registration failed.\n");

    goto error;
  }

  transaction_count = INKStatCreate("transaction.count", INKSTAT_TYPE_INT64);
  session_count     = INKStatCreate("session.count", INKSTAT_TYPE_INT64);
  av_transaction    = INKStatCreate("avg.transactions", INKSTAT_TYPE_FLOAT);

  contp = TSContCreate(ssn_handler, NULL);
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, contp);

error:
  TSError("[session-1] Plugin not initialized");
}
