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

// This is used to contrast how the code would look if the proposed C++ wrapper
// for TS C API calls to access components of HTTP Messages.
//
#define USE_HTTP_MSG_COMP 0

#include <string>

#include <ts/ts.h>
#include <tscpp/util/TextView.h>
#if USE_HTTP_MSG_COMP
#include <tscpp/api/HttpMsgComp.h>
#endif
#include <tscpp/api/Cleanup.h>

#define PINAME "pi_set_err_status"

namespace
{
char const PIName[] = PINAME;

struct AuxData {
  TSEvent error_event{TS_EVENT_NONE};
  int http_status_code{-1};
  std::string resp_body;

  TSEvent last_event{TS_EVENT_NONE};

  ~AuxData()
  {
    TSReleaseAssert(TS_EVENT_NONE != error_event);
    TSReleaseAssert(TS_EVENT_HTTP_SEND_RESPONSE_HDR == last_event);
  }
};

atscppapi::TxnAuxMgrData md;
using AuxDataMgr = atscppapi::TxnAuxDataMgr<AuxData, md>;

int
contFunc(TSCont, TSEvent event, void *edata)
{
  TSDebug(PIName, "event=%u", unsigned(event));

  TSEvent reenable_event{TS_EVENT_HTTP_CONTINUE};
  auto txn{static_cast<TSHttpTxn>(edata)};
  {
    AuxData &d{AuxDataMgr::data(txn)};

    switch (event) {
    case TS_EVENT_HTTP_READ_REQUEST_HDR: {
      TSReleaseAssert(TS_EVENT_NONE == d.last_event);

#if USE_HTTP_MSG_COMP
      atscppapi::TxnClientReq req{txn};
      TSReleaseAssert(req.hasMsg());
      atscppapi::MimeField fld{req, "X-Test-Data"};
      TSReleaseAssert(fld.valid());
      ts::TextView test_data{fld.valuesGet()};
#else
      TSMBuffer msgBuffer;
      TSMLoc bufLoc;
      TSReleaseAssert(TS_SUCCESS == TSHttpTxnClientReqGet(txn, &msgBuffer, &bufLoc));
      char const fldName[] = "X-Test-Data";
      TSMLoc fldLoc        = TSMimeHdrFieldFind(msgBuffer, bufLoc, fldName, sizeof(fldName) - 1);
      TSReleaseAssert(TS_NULL_MLOC != fldLoc);
      int fldValLen;
      const char *fldValArr = TSMimeHdrFieldValueStringGet(msgBuffer, bufLoc, fldLoc, -1, &fldValLen);
      TSReleaseAssert(fldValArr && (fldValLen > 0));
      ts::TextView test_data(fldValArr, fldValLen);
#endif

      ts::TextView hook_name{test_data.take_prefix_at('/')};
      TSDebug(PIName, "hook_name=%.*s", int(hook_name.size()), hook_name.data());
      if ("READ_REQUEST_HDR" == hook_name) {
        d.error_event = TS_EVENT_HTTP_READ_REQUEST_HDR;

      } else if ("PRE_REMAP" == hook_name) {
        d.error_event = TS_EVENT_HTTP_PRE_REMAP;

      } else if ("POST_REMAP" == hook_name) {
        d.error_event = TS_EVENT_HTTP_POST_REMAP;

      } else if ("CACHE_LOOKUP_COMPLETE" == hook_name) {
        d.error_event = TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE;

      } else if ("SEND_RESPONSE_HDR" == hook_name) {
        d.error_event = TS_EVENT_HTTP_SEND_RESPONSE_HDR;

      } else {
        TSReleaseAssert(false);
      }

      d.http_status_code = int(svtoi(test_data.take_prefix_at('/')));
      d.resp_body        = test_data;

      TSDebug(PIName, "hook_name=%.*s status=%d body=%s", int(hook_name.size()), hook_name.data(), d.http_status_code,
              d.resp_body.c_str());

#if !USE_HTTP_MSG_COMP
      // Only releasing mloc for the field because the release for the message mloc does nothing.
      //
      TSReleaseAssert(TSHandleMLocRelease(msgBuffer, bufLoc, fldLoc) == TS_SUCCESS);
#endif
    } break;

    case TS_EVENT_HTTP_PRE_REMAP: {
      TSReleaseAssert(TS_EVENT_HTTP_READ_REQUEST_HDR == d.last_event);
    } break;

    case TS_EVENT_HTTP_POST_REMAP: {
      TSReleaseAssert(TS_EVENT_HTTP_PRE_REMAP == d.last_event);
    } break;

    case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE: {
      TSReleaseAssert(TS_EVENT_HTTP_POST_REMAP == d.last_event);
    } break;

    case TS_EVENT_HTTP_SEND_RESPONSE_HDR: {
      if (TS_EVENT_HTTP_SEND_RESPONSE_HDR == d.error_event) {
        TSEvent expected_last =
          TS_EVENT_HTTP_SEND_RESPONSE_HDR == d.error_event ? TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE : d.error_event;
        TSReleaseAssert(expected_last == d.last_event);
      }
    } break;

    default:
      TSReleaseAssert(false);
    }

    if (event == d.error_event) {
      reenable_event = TS_EVENT_HTTP_ERROR;
      TSHttpTxnStatusSet(txn, TSHttpStatus(d.http_status_code));
      if (!d.resp_body.empty()) {
        TSHttpTxnErrorBodySet(txn, TSstrdup(d.resp_body.c_str()), d.resp_body.size(), nullptr);
      }
    }

    d.last_event = event;
  }

  TSHttpTxnReenable(txn, reenable_event);

  return 0;
}

} // end anonymous namespace

void
TSPluginInit(int n_arg, char const *arg[])
{
  TSDebug(PIName, "initializing plugin");

  TSPluginRegistrationInfo info;

  info.plugin_name   = const_cast<char *>(PIName);
  info.vendor_name   = const_cast<char *>("Apache");
  info.support_email = const_cast<char *>("dev-subscribe@trafficserver.apache.com");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(PINAME ": Plugin registration failed.");
    return;
  } else {
    TSDebug(PIName, "Plugin registration succeeded.");
  }

  AuxDataMgr::init(PIName);

  if (n_arg != 1) {
    TSError(PINAME ": global initialization failed, no plugin arguments allowed");
    return;
  }

  TSCont contp = TSContCreate(contFunc, nullptr);
  TSReleaseAssert(contp != nullptr);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_PRE_REMAP_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
}
