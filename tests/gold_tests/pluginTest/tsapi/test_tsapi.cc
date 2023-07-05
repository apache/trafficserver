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

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
#include <fstream>
#include <string_view>
#include <string>
#include <bitset>

#include <tscpp/util/PostScript.h>

#include <ts/ts.h>
#include <ts/remap.h>

/*
Regression testing code for TS API.  Not comprehensive, hopefully will be built up over time.
*/

#define PINAME "test_tsapi"

namespace
{
char PIName[] = PINAME;

// NOTE:  It's important to flush this after writing so that a gold test using this plugin can examine the log before TS
// terminates.
//
std::fstream logFile;

TSCont tCont, gCont;

std::uintptr_t remap_count;
std::bitset<64> remap_mask;

void
testsForReqHdr(char const *desc, TSMBuffer hbuf, TSMLoc hloc)
{
  logFile << desc << ':' << std::endl;
  logFile << "TSHttpHdrEffectiveUrlBufGet():  ";
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
  logFile << "TSUrlSchemeGet():  ";
  TSMLoc url_loc;
  if (TSHttpHdrUrlGet(hbuf, hloc, &url_loc) != TS_SUCCESS) {
    logFile << "failed to get URL loc" << std::endl;

  } else {
    ts::PostScript ps([=]() -> void { TSHandleMLocRelease(hbuf, TS_NULL_MLOC, url_loc); });

    int scheme_len;
    char const *scheme_data = TSUrlSchemeGet(hbuf, url_loc, &scheme_len);
    if (!scheme_data || !scheme_len) {
      logFile << "failed to get URL scheme" << std::endl;
    } else {
      logFile << std::string_view(scheme_data, scheme_len) << std::endl;
    }
    logFile << "TSUrlRawSchemeGet():  ";
    scheme_data = TSUrlRawSchemeGet(hbuf, url_loc, &scheme_len);
    if (!scheme_data || !scheme_len) {
      logFile << "failed to get raw URL scheme" << std::endl;
    } else {
      logFile << std::string_view(scheme_data, scheme_len) << std::endl;
    }
    logFile << "TSUrlPortGet():  " << TSUrlPortGet(hbuf, url_loc) << std::endl;
    logFile << "TSUrlRawPortGet():  " << TSUrlRawPortGet(hbuf, url_loc) << std::endl;
  }
}

void
testsForEffectiveUrlStringGet(TSHttpTxn txn)
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
}

void
testsForReadReqHdrHook(TSHttpTxn txn)
{
  testsForEffectiveUrlStringGet(txn);

  {
    TSMBuffer hbuf;
    TSMLoc hloc;

    if (TSHttpTxnClientReqGet(txn, &hbuf, &hloc) != TS_SUCCESS) {
      logFile << "failed to get client request" << std::endl;

    } else {
      testsForReqHdr("Client Request", hbuf, hloc);
      TSHandleMLocRelease(hbuf, TS_NULL_MLOC, hloc);
    }
  }
}

void
testsForSendReqHdrHook(TSHttpTxn txn)
{
  testsForEffectiveUrlStringGet(txn);

  {
    TSMBuffer hbuf;
    TSMLoc hloc;

    if (TSHttpTxnServerReqGet(txn, &hbuf, &hloc) != TS_SUCCESS) {
      logFile << "failed to get server request" << std::endl;

    } else {
      testsForReqHdr("Request To Server", hbuf, hloc);
      TSHandleMLocRelease(hbuf, TS_NULL_MLOC, hloc);
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

  case TS_EVENT_HTTP_SEND_REQUEST_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    testsForSendReqHdrHook(txn);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  default: {
    TSReleaseAssert(false);
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
    TSHttpTxnHookAdd(txn, TS_HTTP_SEND_REQUEST_HDR_HOOK, tCont);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    testsForReadReqHdrHook(txn);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_SEND_REQUEST_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    testsForSendReqHdrHook(txn);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  default: {
    TSReleaseAssert(false);
  } break;

  } // end switch

  return 0;
}

static int
shutdown_handler(TSCont contp, TSEvent event, void *edata)
{
  if (event != TS_EVENT_LIFECYCLE_SHUTDOWN) {
    return 0;
  }
  TSDebug(PIName, "Cleaning up global continuations.");
  if (tCont) {
    TSContDestroy(tCont);
  }
  if (gCont) {
    TSContDestroy(gCont);
  }
  return 0;
}

} // end anonymous namespace

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSDebug(PIName, "TSRemapInit()");

  TSReleaseAssert(api_info && errbuf && errbuf_size);

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    std::snprintf(errbuf, errbuf_size, "Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
                  (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  const char *fileSpec = std::getenv("OUTPUT_FILE");

  if (nullptr == fileSpec) {
    TSError(PINAME ": Environment variable OUTPUT_FILE not found.");

    return TS_ERROR;
  }

  // Disable output buffering for logFile, so that explicit flushing is not necessary.
  logFile.rdbuf()->pubsetbuf(nullptr, 0);

  logFile.open(fileSpec, std::ios::out);
  if (!logFile.is_open()) {
    TSError(PINAME ": could not open log file \"%s\"", fileSpec);

    return TS_ERROR;
  }

  // Mutex to protect the logFile object.
  //
  TSMutex mtx = TSMutexCreate();

  gCont = TSContCreate(globalContFunc, mtx);

  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, gCont);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, gCont);
  TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, gCont);

  tCont = TSContCreate(transactionContFunc, mtx);

  TSLifecycleHookAdd(TS_LIFECYCLE_SHUTDOWN_HOOK, TSContCreate(shutdown_handler, nullptr));
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **instance, char *errbuf, int errbuf_size)
{
  TSReleaseAssert(errbuf && errbuf_size);
  TSReleaseAssert(remap_count < remap_mask.size());

  remap_mask[remap_count++] = true;
  *instance                 = reinterpret_cast<void *>(remap_count);

  logFile << "TSRemapNewInstance():" << std::endl;
  for (int i = 0; i < argc; ++i) {
    logFile << "argv[" << i << "]=" << argv[i] << std::endl;
  }

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *instance)
{
  // NOTE:  Currently this is never called.

  auto inum = reinterpret_cast<std::uintptr_t>(instance) - 1;
  logFile << "TSRemapNewInstance(): instance=" << inum << std::endl;
  TSReleaseAssert(inum < remap_mask.size());
  TSReleaseAssert(remap_mask[inum]);
  remap_mask[inum] = false;
}

TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  TSReleaseAssert(txnp && rri);
  auto inum = reinterpret_cast<std::uintptr_t>(instance) - 1;
  TSReleaseAssert(inum < remap_mask.size());

  logFile << "TSRemapDoRemap(): instance=" << inum << " redirect=" << rri->redirect << std::endl;

  testsForReqHdr("Remap Request", rri->requestBufp, rri->requestHdrp);

  return TSREMAP_NO_REMAP;
}
