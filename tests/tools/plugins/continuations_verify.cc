/**
  @file
  @brief Plugin to verify the ordering of session and transaction start and
  close hooks is correct. Keeps track of statistics about the number of
  hooks tracked that are caught and of the number of errors encountered.

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

#include <cstdlib> // for abort
#include <ts/ts.h> // for debug

// debug messages viewable by setting 'proxy.config.diags.debug.tags'
// in 'records.config'

// debug messages during one-time initialization
static const char DEBUG_TAG_INIT[] = "continuations_verify.init";
static const char DEBUG_TAG_MSG[]  = "continuations_verify.msg";
static const char DEBUG_TAG_HOOK[] = "continuations_verify.hook";

// plugin registration info
static char plugin_name[]   = "continuations_verify";
static char vendor_name[]   = "apache";
static char support_email[] = "shinrich@apache.org";

// Statistics provided by the plugin
static int stat_ssn_close_1 = 0; // number of TS_HTTP_SSN_CLOSE hooks caught by the first continuation
static int stat_ssn_close_2 = 0; // number of TS_HTTP_SSN_CLOSE hooks caught by the second continuation
static int stat_txn_close_1 = 0; // number of TS_HTTP_TXN_CLOSE hooks caught by the first continuation
static int stat_txn_close_2 = 0; // number of TS_HTTP_TXN_CLOSE hooks caught by the second continuation
static int stat_test_done   = 0; // Incremented when receiving a traffic_ctl message

static int
handle_msg(TSCont contp, TSEvent event, void *edata)
{
  if (event == TS_EVENT_LIFECYCLE_MSG) { // External trigger, such as traffic_ctl
    TSDebug(DEBUG_TAG_MSG, "event TS_EVENT_LIFECYCLE_MSG");
    // Send to a ET net thread just to be sure.
    // Turns out the msg is sent to a task thread, but task
    // threads do not get their thread local copy of the stats
    // merged in.  So externally, test.done was stuck at 0 without
    // the Schedule to a NET thread
    TSContScheduleOnPool(contp, 0, TS_THREAD_POOL_NET);
  } else {
    TSDebug(DEBUG_TAG_MSG, "event %d", event);
    TSStatIntIncrement(stat_test_done, 1);
  }
  return 0;
}

/**
    This function is called on every request and logs session and transaction
    start and close events. It is used upon initialization to install the hooks
    to the corresponding events. Return value is irrelevant.
*/
static int
handle_order_1(TSCont contp, TSEvent event, void *edata)
{
  TSHttpSsn ssnp; // session data
  TSHttpTxn txnp; // transaction data

  TSDebug(DEBUG_TAG_HOOK, "order_1 event %d", event);

  // Find the event that happened
  switch (event) {
  case TS_EVENT_HTTP_TXN_CLOSE: // End of transaction
    txnp = reinterpret_cast<TSHttpTxn>(edata);

    TSStatIntIncrement(stat_txn_close_1, 1);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_SSN_CLOSE: // End of session
    ssnp = reinterpret_cast<TSHttpSsn>(edata);

    TSStatIntIncrement(stat_ssn_close_1, 1);
    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
    break;
  // Just release the lock for all other states and do nothing
  default:
    break;
  }

  return 0;
}

static int
handle_order_2(TSCont contp, TSEvent event, void *edata)
{
  TSHttpSsn ssnp; // session data
  TSHttpTxn txnp; // transaction data

  TSDebug(DEBUG_TAG_HOOK, "order_2 event %d", event);

  // Find the event that happened
  switch (event) {
  case TS_EVENT_HTTP_TXN_CLOSE: // End of transaction
    txnp = reinterpret_cast<TSHttpTxn>(edata);

    TSStatIntIncrement(stat_txn_close_2, 1);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_SSN_CLOSE: // End of session
    ssnp = reinterpret_cast<TSHttpSsn>(edata);

    TSStatIntIncrement(stat_ssn_close_2, 1);
    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
    break;
  // Just release the lock for all other states and do nothing
  default:
    break;
  }

  return 0;
}

/**
    Entry point for the plugin.
        - Attaches global hooks for session start and close.
        - Attaches global hooks for transaction start and close.
        - Attaches lifecycle hook for communication through traffic_ctl
        - Initializes all statistics as described in the README
*/
void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(DEBUG_TAG_INIT, "initializing plugin");

  TSPluginRegistrationInfo info;

  info.plugin_name   = plugin_name;
  info.vendor_name   = vendor_name;
  info.support_email = support_email;

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed. \n", plugin_name);
  }

  TSCont contp_1 = TSContCreate(handle_order_1, TSMutexCreate());
  TSCont contp_2 = TSContCreate(handle_order_2, TSMutexCreate());
  TSCont contp   = TSContCreate(handle_msg, TSMutexCreate());

  if (contp_1 == nullptr || contp_2 == nullptr || contp == nullptr) {
    // Continuation initialization failed. Unrecoverable, report and exit.
    TSError("[%s] could not create continuation", plugin_name);
    abort();
  } else {
    // Continuation initialization succeeded.

    stat_txn_close_1 =
      TSStatCreate("continuations_verify.txn.close.1", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    stat_ssn_close_1 =
      TSStatCreate("continuations_verify.ssn.close.1", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    stat_txn_close_2 =
      TSStatCreate("continuations_verify.txn.close.2", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    stat_ssn_close_2 =
      TSStatCreate("continuations_verify.ssn.close.2", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    stat_test_done =
      TSStatCreate("continuations_verify.test.done", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);

    // Add all hooks.
    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, contp_1);
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, contp_1);

    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, contp_2);
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, contp_2);

    // Respond to a traffic_ctl message
    TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, contp);
  }
}
