/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
Regression test code for TS API HTTP hooks.  The code assumes there will only be one active transaction at a time.  It
verifies the event data parameter to the continuations triggered by the hooks is correct.
*/

#include <fstream>
#include <cstdlib>

#include <ts/ts.h>

// TSReleaseAssert() doesn't seem to produce any logging output for a debug build, so do both kinds of assert.
//
#define ALWAYS_ASSERT(EXPR) \
  {                         \
    TSAssert(EXPR);         \
    TSReleaseAssert(EXPR);  \
  }

namespace
{
#define PINAME "test_hooks"
char PIName[] = PINAME;

// NOTE:  It's important to flush this after writing so that a gold test using this plugin can examine the log before TS
// terminates.
//
std::fstream logFile;

TSVConn activeVConn;

TSHttpSsn activeSsn;

TSHttpTxn activeTxn;

int
transactionContFunc(TSCont, TSEvent event, void *eventData)
{
  logFile << "Transaction: event=" << TSHttpEventNameLookup(event) << std::endl;

  TSDebug(PIName, "Transaction: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_HTTP_TXN_CLOSE: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    TSDebug(PIName, "Transaction: ssn=%p", TSHttpTxnSsnGet(txn));

    // Don't assume any order of continuation execution on the same hook.
    ALWAYS_ASSERT((txn == activeTxn) or !activeTxn)

    ALWAYS_ASSERT(TSHttpTxnSsnGet(txn) == activeSsn)

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    TSDebug(PIName, "Transaction: ssn=%p", TSHttpTxnSsnGet(txn));

    ALWAYS_ASSERT(txn == activeTxn)
    ALWAYS_ASSERT(TSHttpTxnSsnGet(txn) == activeSsn)

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  default: {
    ALWAYS_ASSERT(false)
  } break;

  } // end switch

  return 0;
}

TSCont tCont;

int
sessionContFunc(TSCont, TSEvent event, void *eventData)
{
  logFile << "Session: event=" << TSHttpEventNameLookup(event) << std::endl;

  TSDebug(PIName, "Session: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_HTTP_SSN_CLOSE: {
    auto ssn = static_cast<TSHttpSsn>(eventData);

    // Don't assume any order of continuation execution on the same hook.
    ALWAYS_ASSERT((ssn == activeSsn) or !activeSsn)

    TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_TXN_START: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    // Don't assume any order of continuation execution on the same hook.
    ALWAYS_ASSERT((txn == activeTxn) or !activeTxn)

    TSDebug(PIName, "Session: ssn=%p", TSHttpTxnSsnGet(txn));

    ALWAYS_ASSERT(TSHttpTxnSsnGet(txn) == activeSsn)

    TSHttpTxnHookAdd(txn, TS_HTTP_READ_REQUEST_HDR_HOOK, tCont);
    TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, tCont);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_TXN_CLOSE: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    TSDebug(PIName, "Session: ssn=%p", TSHttpTxnSsnGet(txn));

    // Don't assume any order of continuation execution on the same hook.
    ALWAYS_ASSERT((txn == activeTxn) or !activeTxn)

    ALWAYS_ASSERT(TSHttpTxnSsnGet(txn) == activeSsn)

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    TSDebug(PIName, "Session: ssn=%p", TSHttpTxnSsnGet(txn));

    ALWAYS_ASSERT(txn == activeTxn)
    ALWAYS_ASSERT(TSHttpTxnSsnGet(txn) == activeSsn)

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  default: {
    ALWAYS_ASSERT(false)
  } break;

  } // end switch

  return 0;
}

TSCont sCont;

int
globalContFunc(TSCont, TSEvent event, void *eventData)
{
  logFile << "Global: event=" << TSHttpEventNameLookup(event) << std::endl;

  TSDebug(PIName, "Global: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_VCONN_START: {
    ALWAYS_ASSERT(!activeVConn)

    auto vConn = static_cast<TSVConn>(eventData);

    activeVConn = vConn;

    logFile << "Global: ssl flag=" << TSVConnIsSsl(vConn) << std::endl;

    TSVConnReenable(vConn);
  } break;

  case TS_EVENT_SSL_CERT:
  case TS_EVENT_SSL_SERVERNAME: {
    auto vConn = static_cast<TSVConn>(eventData);

    ALWAYS_ASSERT(vConn == activeVConn)

    logFile << "Global: ssl flag=" << TSVConnIsSsl(vConn) << std::endl;

    TSVConnReenable(vConn);
  } break;

  case TS_EVENT_VCONN_CLOSE: {
    auto vConn = static_cast<TSVConn>(eventData);

    ALWAYS_ASSERT(vConn == activeVConn)

    logFile << "Global: ssl flag=" << TSVConnIsSsl(vConn) << std::endl;

    TSVConnReenable(vConn);

    activeVConn = nullptr;
  } break;

  case TS_EVENT_HTTP_SSN_START: {
    ALWAYS_ASSERT(!activeSsn)

    auto ssn = static_cast<TSHttpSsn>(eventData);

    activeSsn = ssn;

    TSHttpSsnHookAdd(ssn, TS_HTTP_READ_REQUEST_HDR_HOOK, sCont);
    TSHttpSsnHookAdd(ssn, TS_HTTP_SSN_CLOSE_HOOK, sCont);
    TSHttpSsnHookAdd(ssn, TS_HTTP_TXN_START_HOOK, sCont);
    TSHttpSsnHookAdd(ssn, TS_HTTP_TXN_CLOSE_HOOK, sCont);

    TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_SSN_CLOSE: {
    auto ssn = static_cast<TSHttpSsn>(eventData);

    ALWAYS_ASSERT(ssn == activeSsn)

    activeSsn = nullptr;

    TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_TXN_START: {
    ALWAYS_ASSERT(!activeTxn)

    auto txn = static_cast<TSHttpTxn>(eventData);

    TSDebug(PIName, "Global: ssn=%p", TSHttpTxnSsnGet(txn));

    activeTxn = txn;

    ALWAYS_ASSERT(TSHttpTxnSsnGet(txn) == activeSsn)

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_TXN_CLOSE: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    TSDebug(PIName, "Global: ssn=%p", TSHttpTxnSsnGet(txn));

    ALWAYS_ASSERT(txn == activeTxn)
    ALWAYS_ASSERT(TSHttpTxnSsnGet(txn) == activeSsn)

    activeTxn = nullptr;

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    TSDebug(PIName, "Global: ssn=%p", TSHttpTxnSsnGet(txn));

    ALWAYS_ASSERT(txn == activeTxn)
    ALWAYS_ASSERT(TSHttpTxnSsnGet(txn) == activeSsn)

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  default: {
    ALWAYS_ASSERT(false)
  } break;

  } // end switch

  return 0;
}

TSCont gCont;

} // end anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PIName;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(PINAME ": Plugin registration failed");

    return;
  }

  const char *fileSpec = std::getenv("OUTPUT_FILE");

  if (nullptr == fileSpec) {
    TSError(PINAME ": Environment variable OUTPUT_FILE not found.");

    return;
  }

  // Disable output buffering for logFile, so that explicit flushing is not necessary.
  logFile.rdbuf()->pubsetbuf(nullptr, 0);

  logFile.open(fileSpec, std::ios::out);
  if (!logFile.is_open()) {
    TSError(PINAME ": could not open log file \"%s\"", fileSpec);

    return;
  }

  // Mutex to protext the logFile object.
  //
  TSMutex mtx = TSMutexCreate();

  gCont = TSContCreate(globalContFunc, mtx);

  // Setup the global hook
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, gCont);
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, gCont);
  TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, gCont);
  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, gCont);
  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, gCont);
  TSHttpHookAdd(TS_SSL_CERT_HOOK, gCont);
  TSHttpHookAdd(TS_SSL_SERVERNAME_HOOK, gCont);

  // NOTE: as of January 2019 these two hooks are only triggered for TLS connections.  It seems that, at trafficserver
  // startup, spurious data on the TLS TCP port may cause trafficserver to attempt (and fail) to create a TLS
  // connection.  If this happens, it will result in TS_VCONN_START_HOOK being triggered, and then TS_VCONN_CLOSE_HOOK
  // will be triggered when the connection closes due to failure.
  //
  TSHttpHookAdd(TS_VCONN_START_HOOK, gCont);
  TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, gCont);

  // TSHttpHookAdd(TS_SSL_SESSION_HOOK, gCont); -- Event is TS_EVENT_SSL_SESSION_NEW -- Event data is TSHttpSsn
  // TSHttpHookAdd(TS_SSL_SERVER_VERIFY_HOOK, gCont);
  // TSHttpHookAdd(TS_SSL_VERIFY_CLIENT_HOOK, gCont);
  // TSHttpHookAdd(TS_VCONN_OUTBOUND_START_HOOK, gCont);
  // TSHttpHookAdd(TS_VCONN_OUTBOUND_CLOSE_HOOK, gCont);

  sCont = TSContCreate(sessionContFunc, mtx);

  tCont = TSContCreate(transactionContFunc, mtx);
}

namespace
{
class Cleanup
{
public:
  ~Cleanup()
  {
    // In practice it is not strictly necessary to destroy remaining continuations on program exit.

    if (tCont) {
      TSContDestroy(tCont);
    }
    if (sCont) {
      TSContDestroy(sCont);
    }
    if (gCont) {
      TSContDestroy(gCont);
    }
  }
};

// Do any needed cleanup for this source file at program termination time.
//
Cleanup cleanup;

} // namespace
