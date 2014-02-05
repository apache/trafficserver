/**
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

#include <iostream>
#include <cstdlib>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/ClientRequest.h>
#include <atscppapi/utils.h>

#include <ts/ts.h>

using namespace atscppapi;
using namespace std;

namespace {

const string SPECIAL_HEADER("Special-Header");

}

class GlobalHookPlugin : public GlobalPlugin {
public:
  GlobalHookPlugin() {
    registerHook(HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
    registerHook(HOOK_SEND_RESPONSE_HEADERS);
  }

  virtual void handleReadRequestHeadersPreRemap(Transaction &transaction) {
    cout << "Hello from handleReadRequesHeadersPreRemap!" << endl;
    checkForSpecialHeader(transaction.getClientRequest().getHeaders());
    transaction.resume();
  }

  virtual void handleSendResponseHeaders(Transaction &transaction) {
    cout << "Hello from handleSendResponseHeaders!" << endl;
    checkForSpecialHeader(transaction.getClientRequest().getHeaders());
    transaction.resume();
  }

private:
  void checkForSpecialHeader(Headers &headers) {
    Headers::iterator iter = headers.find(SPECIAL_HEADER);
    if (iter == headers.end()) {
      cout << "Special header is absent" << endl;
    } else {
      cout << "Special header is present with value " << (*iter).str() << endl;
    }
  }
};

namespace {

int handlePostRemap(TSCont cont ATSCPPAPI_UNUSED, TSEvent event ATSCPPAPI_UNUSED, void *edata) {
  TSHttpTxn txn = static_cast<TSHttpTxn>(edata);
  TSMBuffer hdr_buf;
  TSMLoc hdr_loc, field_loc;
  TSHttpTxnClientReqGet(txn, &hdr_buf, &hdr_loc);
  int nullTerminatedStringLength = -1;
  TSMimeHdrFieldCreateNamed(hdr_buf, hdr_loc, SPECIAL_HEADER.c_str(), nullTerminatedStringLength, &field_loc);
  const char *value = "foo";
  int insertAtBeginningIndex = 0;
  TSMimeHdrFieldValueStringInsert(hdr_buf, hdr_loc, field_loc, insertAtBeginningIndex, value,
                                  nullTerminatedStringLength);
  TSMimeHdrFieldAppend(hdr_buf, hdr_loc, field_loc);
  TSHandleMLocRelease(hdr_buf, hdr_loc, field_loc);
  TSMLoc hdr_loc_null_parent = NULL;
  TSHandleMLocRelease(hdr_buf, hdr_loc_null_parent, hdr_loc);
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

}

void TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED) {
  int do_overwrite = 1;
  setenv(utils::DISABLE_DATA_CACHING_ENV_FLAG.c_str(), "true", do_overwrite);

  new GlobalHookPlugin();

  TSMutex nullMutex = NULL;
  TSCont globalCont = TSContCreate(handlePostRemap, nullMutex);
  TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, globalCont);
}
