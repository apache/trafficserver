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
#include <string>
#include <cstring>

#define PLUGIN_NAME "http2_close_connection"

static DbgCtl dbg_ctl_tag{PLUGIN_NAME};

const char *FIELD_CONNECTION = "Connection";
const char *VALUE_CLOSE      = "close";

const int LEN_CONNECTION = 10;
const int LEN_CLOSE      = 5;

static int
txn_handler(TSCont /* contp */, TSEvent event, void *edata)
{
  Dbg(dbg_ctl_tag, "txn_handler event: %d", event);

  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  TSMBuffer resp_bufp    = nullptr;
  TSMLoc    resp_hdr_loc = nullptr;
  if (TSHttpTxnClientRespGet(txnp, &resp_bufp, &resp_hdr_loc) != TS_SUCCESS) {
    Dbg(dbg_ctl_tag, "TSHttpTxnClientRespGet failed");
  } else {
    Dbg(dbg_ctl_tag, "TSHttpTxnClientRespGet success");
    TSMLoc field_loc = TSMimeHdrFieldFind(resp_bufp, resp_hdr_loc, FIELD_CONNECTION, LEN_CONNECTION);
    if (field_loc) {
      Dbg(dbg_ctl_tag, "Found header %s", FIELD_CONNECTION);
      TSMimeHdrFieldValueStringSet(resp_bufp, resp_hdr_loc, field_loc, 0, VALUE_CLOSE, LEN_CLOSE);
      Dbg(dbg_ctl_tag, "Setting header %s:%s", FIELD_CONNECTION, VALUE_CLOSE);
    } else {
      Dbg(dbg_ctl_tag, "Header %s not found", FIELD_CONNECTION);
      if (TSMimeHdrFieldCreate(resp_bufp, resp_hdr_loc, &field_loc) == TS_SUCCESS) {
        TSMimeHdrFieldNameSet(resp_bufp, resp_hdr_loc, field_loc, FIELD_CONNECTION, LEN_CONNECTION);
        TSMimeHdrFieldAppend(resp_bufp, resp_hdr_loc, field_loc);
        TSMimeHdrFieldValueStringInsert(resp_bufp, resp_hdr_loc, field_loc, 0, VALUE_CLOSE, LEN_CLOSE);
        Dbg(dbg_ctl_tag, "Adding header %s:%s", FIELD_CONNECTION, VALUE_CLOSE);
      } else {
        Dbg(dbg_ctl_tag, "TSMimeHdrFieldCreate failed");
      }
    }
    TSHandleMLocRelease(resp_bufp, resp_hdr_loc, field_loc);
  }
  TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_hdr_loc);
  resp_bufp = nullptr;

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

void
TSPluginInit(int argc, const char **argv)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.support_email = const_cast<char *>("feid@yahooinc.com");
  info.vendor_name   = const_cast<char *>("Yahoo");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[" PLUGIN_NAME "] plugin registration failed\n");
    return;
  }

  Dbg(dbg_ctl_tag, "plugin registered");
  TSCont txn_cont = TSContCreate(txn_handler, nullptr);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, txn_cont);
}
