/** @file
 *
 *  Test plugin that calls TSHttpTxnServerRequestBodySet() to inject a
 *  request body to the origin server. This tests the api_server_request_body_set
 *  guard: when this plugin is active, the internal_msg_buffer is set but the
 *  set-body code path in handle_api_return() must NOT consume it as a response
 *  body replacement.
 *
 *  Usage: set_server_request_body.so <body_text>
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <ts/ts.h>

#include <cinttypes>
#include <cstring>
#include <cstdlib>

#define PLUGIN_NAME "set_server_request_body"

static DbgCtl dbg_ctl{PLUGIN_NAME};

static char   *body_text = nullptr;
static int64_t body_len  = 0;

static int
handle_send_request(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (event == TS_EVENT_HTTP_SEND_REQUEST_HDR) {
    // Allocate a copy of the body text for this transaction.
    // TSHttpTxnServerRequestBodySet takes ownership of the buffer
    // (it will be freed by ATS).
    // Allocate body_len + 1 for the null terminator. ATS's
    // setup_server_send_request() may print the buffer as a string.
    char *buf = static_cast<char *>(TSmalloc(body_len + 1));
    memcpy(buf, body_text, body_len);
    buf[body_len] = '\0';

    Dbg(dbg_ctl, "Setting server request body of %d bytes", static_cast<int>(body_len));
    TSHttpTxnServerRequestBodySet(txnp, buf, body_len);

    // Also set Content-Length and Content-Type on the server request
    TSMBuffer bufp;
    TSMLoc    hdr_loc;
    if (TSHttpTxnServerReqGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
      // Set method to POST since we're injecting a body
      TSHttpHdrMethodSet(bufp, hdr_loc, TS_HTTP_METHOD_POST, TS_HTTP_LEN_POST);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  if (argc >= 2) {
    body_text = TSstrdup(argv[1]);
    body_len  = strlen(body_text);
  } else {
    body_text = TSstrdup("injected-body");
    body_len  = strlen(body_text);
  }

  Dbg(dbg_ctl, "Initialized with body: '%s'", body_text);

  TSCont contp = TSContCreate(handle_send_request, nullptr);
  TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, contp);
}
