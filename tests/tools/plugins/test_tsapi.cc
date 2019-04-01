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
Regression testing code for TS API.  Not comprehensive, hopefully will be built up over time.
*/

#include <fstream>
#include <cstdlib>
#include <string_view>
#include <string>

#include <ts/ts.h>
#include <tscpp/util/PostScript.h>

// TSReleaseAssert() doesn't seem to produce any logging output for a debug build, so do both kinds of assert.
//
#define ALWAYS_ASSERT(EXPR) \
  {                         \
    TSAssert(EXPR);         \
    TSReleaseAssert(EXPR);  \
  }

namespace
{
#define PINAME "test_tsapi"
char PIName[] = PINAME;

// NOTE:  It's important to flush this after writing so that a gold test using this plugin can examine the log before TS
// terminates.
//
std::fstream logFile;

TSCont tCont, gCont;

void
testsForReadReqHdrHook(TSHttpTxn txn)
{
  logFile << "TSHttpTxnEffectiveUrlStringGet():  ";
  int urlLength;
  char *urlStr = TSHttpTxnEffectiveUrlStringGet(txn, &urlLength);
  if (!urlStr) {
    logFile << "URL null" << std::endl;
  } else if (0 == urlLength) {
    logFile << "URL length zero" << std::endl;
  } else if (0 > urlLength) {
    logFile << "URL length negative" << std::endl;
  } else {
    logFile << std::string_view(urlStr, urlLength) << std::endl;

    TSfree(urlStr);
  }

  logFile << "TSHttpHdrEffectiveUrlBufGet():  ";
  {
    TSMBuffer hbuf;
    TSMLoc hloc;

    if (TSHttpTxnClientReqGet(txn, &hbuf, &hloc) != TS_SUCCESS) {
      logFile << "failed to get client request" << std::endl;

    } else {
      ts::PostScript ps([=]() -> void { TSHandleMLocRelease(hbuf, TS_NULL_MLOC, hloc); });

      int64_t url_length;

      if (TSHttpHdrEffectiveUrlBufGet(hbuf, hloc, nullptr, 0, &url_length) != TS_SUCCESS) {
        logFile << "sizing call failed " << std::endl;

      } else if (0 == url_length) {
        logFile << "zero URL length returned" << std::endl;

      } else {
        std::string s(url_length, '?');

        s += "yada";

        int64_t url_length2;

        if (TSHttpHdrEffectiveUrlBufGet(hbuf, hloc, s.data(), url_length + 4, &url_length2) != TS_SUCCESS) {
          logFile << "data-obtaining call failed" << std::endl;

        } else if (url_length2 != url_length) {
          logFile << "second size does not match first" << std::endl;

        } else if (s.substr(url_length, 4) != "yada") {
          logFile << "overwrite" << std::endl;

        } else {
          logFile << s.substr(0, url_length) << std::endl;
        }
      }
    }
  }
}

int
transactionContFunc(TSCont, TSEvent event, void *eventData)
{
  logFile << "Transaction: event=" << TSHttpEventNameLookup(event) << std::endl;

  TSDebug(PIName, "Transaction: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    testsForReadReqHdrHook(txn);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  default: {
    ALWAYS_ASSERT(false)
  } break;

  } // end switch

  return 0;
}

int
globalContFunc(TSCont, TSEvent event, void *eventData)
{
  logFile << "Global: event=" << TSHttpEventNameLookup(event) << std::endl;

  TSDebug(PIName, "Global: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_HTTP_TXN_START: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    TSHttpTxnHookAdd(txn, TS_HTTP_READ_REQUEST_HDR_HOOK, tCont);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    testsForReadReqHdrHook(txn);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  default: {
    ALWAYS_ASSERT(false)
  } break;

  } // end switch

  return 0;
}

} // end anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(PIName, "TSPluginInit()");

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

  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, gCont);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, gCont);

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
    if (gCont) {
      TSContDestroy(gCont);
    }
  }
};

// Do any needed cleanup for this source file at program termination time.
//
Cleanup cleanup;

} // end anonymous namespace
