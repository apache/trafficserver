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

#define PLUGIN_TAG "test"

// Number of seconds to reschedule to a task thread and delay
int DelayStart = 0;

int
transactionHandler(TSCont continuation, TSEvent event, void *d)
{
  if (!(event == TS_EVENT_HTTP_PRE_REMAP || event == TS_EVENT_HTTP_TXN_CLOSE)) {
    TSError("[" PLUGIN_TAG "] unexpected event on transactionHandler: %i\n", event);
    return 0;
  }

  TSHttpTxn transaction = static_cast<TSHttpTxn>(d);

  switch (event) {
  case TS_EVENT_HTTP_PRE_REMAP: {
    TSDebug(PLUGIN_TAG, " -- transactionHandler :: TS_EVENT_HTTP_PRE_REMAP");
  } break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    TSDebug(PLUGIN_TAG, " -- transactionHandler :: TS_EVENT_HTTP_TXN_CLOSE");
    TSContDataSet(continuation, nullptr);
    TSContDestroy(continuation);
    break;

  default:
    break;
  }

  TSHttpTxnReenable(transaction, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

int
sessionHandler(TSCont continuation, TSEvent event, void *d)
{
  TSHttpTxn txnp = (TSHttpTxn)d;
  TSCont txn_contp;

  switch (event) {
  case TS_EVENT_HTTP_PRE_REMAP: {
    TSDebug(PLUGIN_TAG, " -- sessionHandler :: TS_EVENT_HTTP_PRE_REMAP");
    txn_contp = TSContCreate(transactionHandler, nullptr);

    /* Registers locally to hook PRE_REMAP_HOOK and TXN_CLOSE */
    TSHttpTxnHookAdd(txnp, TS_HTTP_PRE_REMAP_HOOK, txn_contp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);
  } break;

  case TS_EVENT_HTTP_SSN_CLOSE: {
    TSDebug(PLUGIN_TAG, " -- sessionHandler :: TS_EVENT_HTTP_SSN_CLOSE");
    const TSHttpSsn session = static_cast<TSHttpSsn>(d);

    TSHttpSsnReenable(session, TS_EVENT_HTTP_CONTINUE);
    TSContDestroy(continuation);
    return 0;
  } break;

  case TS_EVENT_TIMEOUT: { // The schedule case, reenable the session continuation
    TSDebug(PLUGIN_TAG, " -- sessionHandler :: TS_EVENT_TIMEOUT");
    TSHttpSsn session = static_cast<TSHttpSsn>(TSContDataGet(continuation));
    TSHttpSsnReenable(session, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }
  default:
    TSAssert(!"Unexpected event");
    break;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

int
globalHandler(TSCont continuation, TSEvent event, void *data)
{
  if (event == TS_EVENT_HTTP_SSN_START) {
    TSHttpSsn session = static_cast<TSHttpSsn>(data);
    TSDebug(PLUGIN_TAG, " -- globalHandler :: TS_EVENT_HTTP_SSN_START");
    TSCont cont = TSContCreate(sessionHandler, TSMutexCreate());

    TSHttpSsnHookAdd(session, TS_HTTP_PRE_REMAP_HOOK, cont);
    TSHttpSsnHookAdd(session, TS_HTTP_SSN_CLOSE_HOOK, cont);

    TSDebug(PLUGIN_TAG, "New session, cont is %p", cont);

    if (DelayStart == 0) {
      TSHttpSsnReenable(session, TS_EVENT_HTTP_CONTINUE);
    } else {
      TSContDataSet(cont, session);
      TSContScheduleOnPool(cont, 500, TS_THREAD_POOL_TASK);
    }
  }

  return 0;
}

void
TSPluginInit(int argc, const char **argv)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = const_cast<char *>(PLUGIN_TAG);
  info.support_email = const_cast<char *>("shinrich@verizonmedia.com");
  info.vendor_name   = const_cast<char *>("Verizon Media");

  TSReturnCode ret;
#if (TS_VERSION_MAJOR >= 7)
  ret = TSPluginRegister(&info);
#else
  ret = TSPluginRegister(TS_SDK_VERSION_3_0, &info);
#endif

  if (TS_ERROR == ret) {
    TSError("[" PLUGIN_TAG "] plugin registration failed\n");
    return;
  }

  if (argc >= 2) {
    TSDebug(PLUGIN_TAG, "Argument %s", argv[1]);
    if (strcmp(argv[1], "-delay") == 0) {
      DelayStart = 1;
    }
  }
  TSCont continuation = TSContCreate(globalHandler, TSMutexCreate());

  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, continuation);
}
