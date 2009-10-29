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
#include "InkAPI.h"

static INKStat transaction_count;
static INKStat session_count;
static INKStat av_transaction;


static void
txn_handler(INKHttpTxn txnp, INKCont contp)
{
  int num_txns;
  INKStatIncrement(transaction_count);
  num_txns = INKStatIntRead(transaction_count);
  INKDebug("tag_session", "The number of transactions is %d\n", num_txns);

}


static void
handle_session(INKHttpSsn ssnp, INKCont contp)
{
  int num_ssn;

  INKStatIncrement(session_count);
  num_ssn = INKStatIntRead(session_count);
  INKDebug("tag_session", "The number of sessions is %d\n", num_ssn);
  INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_START_HOOK, contp);
}

static int
ssn_handler(INKCont contp, INKEvent event, void *edata)
{
  INKHttpSsn ssnp;
  INKHttpTxn txnp;

  switch (event) {
  case INK_EVENT_HTTP_SSN_START:

    ssnp = (INKHttpSsn) edata;
    handle_session(ssnp, contp);
    INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);
    return 0;

  case INK_EVENT_HTTP_TXN_START:
    txnp = (INKHttpTxn) edata;
    txn_handler(txnp, contp);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    return 0;

  default:
    INKDebug("tag_session", "In the default case: event = %d\n", event);
    break;
  }
  return 0;
}

int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 5.2 */
    if (major_ts_version > 5) {
      result = 1;
    } else if (major_ts_version == 5) {
      if (minor_ts_version >= 2) {
        result = 1;
      }
    }
  }

  return result;
}


void
INKPluginInit(int argc, const char *argv[])
{
  INKCont contp;
  INKPluginRegistrationInfo info;

  info.plugin_name = "session-1";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_5_2, &info)) {
    INKError("[PluginInit] Plugin registration failed.\n");
    goto error;
  }

  if (!check_ts_version()) {
    INKError("[PluginInit] Plugin requires Traffic Server 5.2.0 or later\n");
    goto error;
  }

  transaction_count = INKStatCreate("transaction.count", INKSTAT_TYPE_INT64);
  session_count = INKStatCreate("session.count", INKSTAT_TYPE_INT64);
  av_transaction = INKStatCreate("avg.transactions", INKSTAT_TYPE_FLOAT);

  contp = INKContCreate(ssn_handler, NULL);
  INKHttpHookAdd(INK_HTTP_SSN_START_HOOK, contp);

error:
  INKError("[PluginInit] Plugin not initialized");
}
