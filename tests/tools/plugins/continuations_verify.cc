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

#define __STDC_FORMAT_MACROS 1 // for inttypes.h
#include <inttypes.h>          // for PRIu64
#include <iostream>
#include <stdlib.h> // for abort
#include <string.h> // for NULL macro
#include <ts/ts.h>  // for debug

// TODO Is LIFECYCLE_MSG enabled in 6.2.0, or 7.0.0, might require push
// with version rework

// debug messages viewable by setting 'proxy.config.diags.debug.tags'
// in 'records.config'

// debug messages during one-time initialization
static const char DEBUG_TAG_INIT[] = "continuations_verify.init";

// plugin registration info
static char plugin_name[]   = "continuations_verify";
static char vendor_name[]   = "Yahoo! Inc.";
static char support_email[] = "ats-devel@yahoo-inc.com";

static TSMutex order_mutex_1; // lock on global data
static TSMutex order_mutex_2; // lock on global data

// Statistics provided by the plugin
static int stat_ssn_close_1 = 0; // number of TS_HTTP_SSN_CLOSE hooks caught by the first continuation
static int stat_ssn_close_2 = 0; // number of TS_HTTP_SSN_CLOSE hooks caught by the second continuation
static int stat_txn_close_1 = 0; // number of TS_HTTP_TXN_CLOSE hooks caught by the first continuation
static int stat_txn_close_2 = 0; // number of TS_HTTP_TXN_CLOSE hooks caught by the second continuation

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

#if (TS_VERSION_MAJOR < 3)
  if (TSPluginRegister(TS_SDK_VERSION_2_0, &info) != TS_SUCCESS) {
#elif (TS_VERSION_MAJOR < 6)
  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
#else
  if (TSPluginRegister(&info) != TS_SUCCESS) {
#endif
    TSError("[%s] Plugin registration failed. \n", plugin_name);
  }

  order_mutex_1 = TSMutexCreate();
  TSCont contp_1;
  order_mutex_2 = TSMutexCreate();
  TSCont contp_2;

  contp_1 = TSContCreate(handle_order_1, order_mutex_1);
  contp_2 = TSContCreate(handle_order_2, order_mutex_2);
  if (contp_1 == NULL || contp_2 == NULL) {
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

    // Add all hooks.
    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, contp_1);
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, contp_1);

    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, contp_2);
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, contp_2);
  }
}
