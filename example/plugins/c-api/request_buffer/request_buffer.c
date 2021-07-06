/** @file

  A brief file description

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ts/ts.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "request_buffer"

#define TS_NULL_MUTEX NULL

static char *
request_body_get(TSHttpTxn txnp, int *len)
{
  char *ret                           = NULL;
  TSIOBufferReader post_buffer_reader = TSHttpTxnPostBufferReaderGet(txnp);
  int64_t read_avail                  = TSIOBufferReaderAvail(post_buffer_reader);
  if (read_avail == 0) {
    TSIOBufferReaderFree(post_buffer_reader);
    return NULL;
  }

  ret = (char *)TSmalloc(sizeof(char) * read_avail);

  int64_t consumed      = 0;
  int64_t data_len      = 0;
  const char *char_data = NULL;
  TSIOBufferBlock block = TSIOBufferReaderStart(post_buffer_reader);
  while (block != NULL) {
    char_data = TSIOBufferBlockReadStart(block, post_buffer_reader, &data_len);
    memcpy(ret + consumed, char_data, data_len);
    consumed += data_len;
    block = TSIOBufferBlockNext(block);
  }
  TSIOBufferReaderFree(post_buffer_reader);

  *len = (int)consumed;
  return ret;
}

static int
request_buffer_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "request_buffer_plugin starting, event[%d]", event);
  TSHttpTxn txnp = (TSHttpTxn)(edata);
  if (event == TS_EVENT_HTTP_REQUEST_BUFFER_COMPLETE) {
    int len    = 0;
    char *body = request_body_get(txnp, &len);
    TSDebug(PLUGIN_NAME, "request_buffer_plugin gets the request body with length[%d]", len);
    TSfree(body);
    TSContDestroy(contp);
  } else {
    ink_assert(0);
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

bool
is_post_request(TSHttpTxn txnp)
{
  TSMLoc req_loc;
  TSMBuffer req_bufp;
  if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_loc) == TS_ERROR) {
    TSError("Error while retrieving client request header\n");
    return false;
  }
  int method_len     = 0;
  const char *method = TSHttpHdrMethodGet(req_bufp, req_loc, &method_len);
  if (method_len != (int)strlen(TS_HTTP_METHOD_POST) || strncasecmp(method, TS_HTTP_METHOD_POST, method_len) != 0) {
    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
    return false;
  }
  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
  return true;
}

static int
global_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "transform_plugin starting");
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    if (is_post_request(txnp)) {
      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_REQUEST_BUFFER_ENABLED, 1);
      TSHttpTxnHookAdd(txnp, TS_HTTP_REQUEST_BUFFER_READ_COMPLETE_HOOK, TSContCreate(request_buffer_plugin, TSMutexCreate()));
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  default:
    break;
  }

  return 0;
}

void
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "[%s] Plugin registration failed", PLUGIN_NAME);

    goto Lerror;
  }

  /* This is call we could use if we need to protect global data */
  /* TSReleaseAssert ((mutex = TSMutexCreate()) != TS_NULL_MUTEX); */

  TSMutex mutex = TS_NULL_MUTEX;
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(global_plugin, mutex));
  TSDebug(PLUGIN_NAME, "[%s] Plugin registration succeeded", PLUGIN_NAME);
  return;

Lerror:
  TSDebug(PLUGIN_NAME, "[%s] Plugin disabled", PLUGIN_NAME);
}
