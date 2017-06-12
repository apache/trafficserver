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
#include <map>
#include <set>
#include <sstream>
#include <stdlib.h> // for abort
#include <string.h> // for NULL macro
#include <ts/ts.h>  // for debug

// TODO Is LIFECYCLE_MSG enabled in 6.2.0, or 7.0.0, might require push
// with version rework

// debug messages viewable by setting 'proxy.config.diags.debug.tags'
// in 'records.config'

// debug messages during one-time initialization
static const char DEBUG_TAG_INIT[] = "ssntxnorder_verify.init";

// debug messages on every request serviced
static const char DEBUG_TAG_HOOK[] = "ssntxnorder_verify.hook";

// plugin registration info
static char plugin_name[]   = "ssntxnorder_verify";
static char vendor_name[]   = "Yahoo! Inc.";
static char support_email[] = "ats-devel@yahoo-inc.com";

static TSMutex order_mutex; // lock on global data

// List of started sessions, SSN_START seen, SSN_CLOSE not seen yet.
static std::set<TSHttpSsn> started_ssns;
static int ssn_balance = 0; // +1 on SSN_START, -1 on SSN_CLOSE

// Metadata for active transactions. Stored upon start to persist improper
// closing behavior.
typedef struct started_txn {
  uint64_t id;
  TSHttpTxn txnp;
  TSHttpSsn ssnp;                      // enclosing session
  started_txn(uint64_t id) : id(id) {} // used for lookup on id
  started_txn(uint64_t id, TSHttpTxn txnp, TSHttpSsn ssnp) : id(id), txnp(txnp), ssnp(ssnp) {}
} started_txn;

// Comparator functor for transactions. Compare by ID.
struct txn_compare {
  bool
  operator()(const started_txn &lhs, const started_txn &rhs) const
  {
    return lhs.id < rhs.id;
  }
};
// List of started transactions, TXN_START seen, TXN_CLOSE not seen yet.
static std::set<started_txn, txn_compare> started_txns;
static int txn_balance = 0; // +1 on TXN_START -1 on TXN_CLOSE

// Statistics provided by the plugin
static int stat_ssn_close = 0; // number of TS_HTTP_SSN_CLOSE hooks caught
static int stat_ssn_start = 0; // number of TS_HTTP_SSN_START hooks caught
static int stat_txn_close = 0; // number of TS_HTTP_TXN_CLOSE hooks caught
static int stat_txn_start = 0; // number of TS_HTTP_TXN_START hooks caught
static int stat_err       = 0; // number of inaccuracies encountered

// IPC information
static char *ctl_tag         = plugin_name; // name is a convenient identifier
static const char ctl_dump[] = "dump";      // output active ssn/txn tables cmd

/**
    This function is invoked upon TS_EVENT_LIFECYCLE_MSG. It outputs the
    active SSN and TXN tables (the items that have not yet been closed).
    Information displayed for transactions:
        - TXN ID
        - Enclosing SSN ID
        - HTTP Protocol Version - 1.0 / 1.1 / 2.0 etc...
    Information displayed for sessions:
        - SSN ID
*/
static void
dump_tables(void)
{
  TSDebug(DEBUG_TAG_HOOK, "Dumping active session and transaction tables.");
  std::stringstream dump("");

  dump << std::string(100, '+') << std::endl;

  if (started_ssns.empty()) {
    dump << "No active sessions could be found." << std::endl;
  } else {
    // Output for every active session
    for (std::set<TSHttpSsn>::iterator it = started_ssns.begin(); it != started_ssns.end(); ++it) {
      dump << "Session --> ID: " << *it << std::endl;
    }
  }

  if (started_txns.empty()) {
    dump << "No active transactions could be found." << std::endl;
  } else {
    // Output for every active transaction
    for (std::set<started_txn, txn_compare>::iterator it = started_txns.begin(); it != started_txns.end(); ++it) {
      dump << "Transaction --> ID: " << it->id << " ; Enclosing SSN ID: " << it->ssnp << " ;" << std::endl;
    }
  }
  dump << std::string(100, '+') << std::endl;
  std::cout << dump.str() << std::endl;
}

/**
    This function is called on every request and logs session and transaction
    start and close events. It is used upon initialization to install the hooks
    to the corresponding events. Return value is irrelevant.
*/
static int
handle_order(TSCont contp, TSEvent event, void *edata)
{
  TSHttpSsn ssnp;    // session data
  TSHttpTxn txnp;    // transaction data
  TSPluginMsg *msgp; // message data

  // Find the event that happened
  switch (event) {
  case TS_EVENT_HTTP_SSN_CLOSE: // End of session
  {
    ssnp = reinterpret_cast<TSHttpSsn>(edata);
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_SSN_CLOSE [ SSNID = %p ]", ssnp);
    TSStatIntIncrement(stat_ssn_close, 1);
    if (started_ssns.erase(ssnp) == 0) {
      // No record existsted for this session
      TSError("Session [ SSNID = %p ] closing was not previously started", ssnp);
      TSStatIntIncrement(stat_err, 1);
    }

    if (--ssn_balance < 0) {
      TSError("More sessions have been closed than started.");
      TSStatIntIncrement(stat_err, 1);
    }

    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
    break;
  }

  case TS_EVENT_HTTP_SSN_START: // Beginning of session
  {
    ssnp = reinterpret_cast<TSHttpSsn>(edata);
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_SSN_START [ SSNID = %p ]", ssnp);
    TSStatIntIncrement(stat_ssn_start, 1);

    if (!started_ssns.insert(ssnp).second) {
      // Insert failed. Session already existed in the record.
      TSError("Session [ SSNID = %p ] has previously started.", ssnp);
      TSStatIntIncrement(stat_err, 1);
    }
    ++ssn_balance;

    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
    break;
  }

  case TS_EVENT_HTTP_TXN_CLOSE: // End of transaction
  {
    txnp = reinterpret_cast<TSHttpTxn>(edata);
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_TXN_CLOSE [ TXNID = %" PRIu64 " ]", TSHttpTxnIdGet(txnp));
    TSStatIntIncrement(stat_txn_close, 1);

    std::set<started_txn>::iterator current_txn = started_txns.find(started_txn(TSHttpTxnIdGet(txnp)));

    if (current_txn != started_txns.end()) {
      // Transaction exists.

      ssnp = current_txn->ssnp;
      if (started_ssns.find(ssnp) == started_ssns.end()) {
        // The session of the transaction was either not started, or was
        // already closed.
        TSError("Transaction [ TXNID = %" PRIu64 " ] closing not in an "
                "active session [ SSNID = %p ].",
                current_txn->id, ssnp);
        TSStatIntIncrement(stat_err, 1);
      }
      started_txns.erase(current_txn); // Stop monitoring the transaction
    } else {
      // Transaction does not exists.
      TSError("Transaction [ TXNID = %" PRIu64 " ] closing not "
              "previously started.",
              current_txn->id);
      TSStatIntIncrement(stat_err, 1);
    }

    if (--txn_balance < 0) {
      TSError("More transactions have been closed than started.");
      TSStatIntIncrement(stat_err, 1);
    }

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  }

  case TS_EVENT_HTTP_TXN_START: // Beginning of transaction
  {
    txnp = reinterpret_cast<TSHttpTxn>(edata);
    ssnp = TSHttpTxnSsnGet(txnp);
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_TXN_START [ TXNID = %" PRIu64 " ]", TSHttpTxnIdGet(txnp));
    TSStatIntIncrement(stat_txn_start, 1);

    started_txn new_txn = started_txn(TSHttpTxnIdGet(txnp), txnp, ssnp);

    if (started_ssns.find(ssnp) == started_ssns.end()) {
      // Session of the transaction has not started.
      TSError("Transaction [ TXNID = %" PRIu64 " ] starting not in an "
              "active session [ SSNID = %p ].",
              new_txn.id, ssnp);
      TSStatIntIncrement(stat_err, 1);
    }

    if (!started_txns.insert(new_txn).second) {
      // Insertion failed. Transaction has previously started.
      TSError("Transaction [ TXNID = %" PRIu64 " ] has previously started.", new_txn.id);
      TSStatIntIncrement(stat_err, 1);
    }

    ++txn_balance;

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  }

#if ((TS_VERSION_MAJOR == 6 && TS_VERSION_MINOR >= 2) || TS_VERSION_MAJOR > 6)
  case TS_EVENT_LIFECYCLE_MSG: // External trigger, such as traffic_ctl
  {
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_LIFECYCLE_MSG");
    msgp = reinterpret_cast<TSPluginMsg *>(edata); // inconsistency

    // Verify message is with the appropriate tag
    if (!strcmp(ctl_tag, msgp->tag) && strncmp(ctl_dump, reinterpret_cast<const char *>(msgp->data), strlen(ctl_dump)) == 0) {
      dump_tables();
    }

    break;
  }
#endif

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

  order_mutex = TSMutexCreate();
  TSCont contp;

  contp = TSContCreate(handle_order, order_mutex);
  if (contp == NULL) {
    // Continuation initialization failed. Unrecoverable, report and exit.
    TSError("[%s] could not create continuation", plugin_name);
    abort();
  } else {
    // Continuation initialization succeeded.

    stat_ssn_start = TSStatCreate("ssntxnorder_verify.ssn.start", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    stat_ssn_close = TSStatCreate("ssntxnorder_verify.ssn.close", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    stat_txn_start = TSStatCreate("ssntxnorder_verify.txn.start", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    stat_txn_close = TSStatCreate("ssntxnorder_verify.txn.close", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    stat_err       = TSStatCreate("ssntxnorder_verify.err", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);

    // Add all hooks.
    TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, contp);
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, contp);

    TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, contp);
    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, contp);

#if ((TS_VERSION_MAJOR == 6 && TS_VERSION_MINOR >= 2) || TS_VERSION_MAJOR > 6)
    TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, contp);
#endif
  }
}
