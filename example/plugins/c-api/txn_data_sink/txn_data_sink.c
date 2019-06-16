/** @file

  Example plugin for using TS_HTTP_RESPONSE_CLIENT_HOOK.

  This example is used to maintain the transaction and thence the connection to the origin server for the full
  transaction, even if the user agent aborts. This is useful in cases where there are other reasons to complete
  the transaction besides providing data to the user agent. For example if the origin server data should always
  be cached (that is, force a background fill), or an expensive transform shouldn't be canceled part way through,
  or the origin server session is expensive to set up and it's cheaper to run this transaction to completion to
  be able to re-use the origin server connection than to set up a new connection.

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
#include <unistd.h>
#include <ts/ts.h>

// This gets the PRI*64 types
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#define PLUGIN_NAME "txn_data_sink"
#define PCP "[" PLUGIN_NAME "] "

// Activate the data sink if this field is present in the request.
static const char FLAG_MIME_FIELD[] = "TS-Agent";
static size_t const FLAG_MIME_LEN   = sizeof(FLAG_MIME_FIELD) - 1;

typedef struct {
  int64_t total;
} SinkData;

// This serves to consume all the data that arrives. If it's not consumed the tunnel gets stalled
// and the transaction doesn't complete. Other things could be done with the data, accessible via
// the IO buffer @a reader, such as writing it to disk to make an externally accessible copy.
static int
client_reader(TSCont contp, TSEvent event, void *edata)
{
  SinkData *data = TSContDataGet(contp);

  // If we got closed, we're done.
  if (TSVConnClosedGet(contp)) {
    TSfree(data);
    TSContDestroy(contp);
    return 0;
  }

  TSVIO input_vio = TSVConnWriteVIOGet(contp);

  if (!data) {
    data        = TSmalloc(sizeof(SinkData));
    data->total = 0;
    TSContDataSet(contp, data);
  }

  switch (event) {
  case TS_EVENT_ERROR:
    TSDebug(PLUGIN_NAME, "Error event");
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
    TSDebug(PLUGIN_NAME, "READ_COMPLETE");
    break;
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_IMMEDIATE:
    TSDebug(PLUGIN_NAME, "Data event - %s", event == TS_EVENT_IMMEDIATE ? "IMMEDIATE" : "READ_READY");
    // Look for data and if we find any, consume.
    if (TSVIOBufferGet(input_vio)) {
      TSIOBufferReader reader = TSVIOReaderGet(input_vio);
      int64_t n               = TSIOBufferReaderAvail(reader);
      if (n > 0) {
        TSIOBufferReaderConsume(reader, n);
        TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + n);
        data->total += n; // internal accounting so we can print the value at the end.
        TSDebug(PLUGIN_NAME, "Consumed %" PRId64 " bytes", n);
      }
      if (TSVIONTodoGet(input_vio) > 0) {
        // signal that we can accept more data.
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
      } else {
        TSDebug(PLUGIN_NAME, "send WRITE_COMPLETE");
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
      }
    } else { // buffer gone, we're done.
      TSDebug(PLUGIN_NAME, "upstream buffer disappeared - %" PRId64 " bytes", data->total);
    }
    break;
  default:
    TSDebug(PLUGIN_NAME, "unhandled event %d", event);
    break;
  }

  return 0;
}

static int
enable_agent_check(TSHttpTxn txnp)
{
  TSMBuffer req_buf;
  TSMLoc req_loc;
  int zret = 0;

  // Enable the sink agent if the header is present.
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &req_buf, &req_loc)) {
    TSMLoc range_field = TSMimeHdrFieldFind(req_buf, req_loc, FLAG_MIME_FIELD, FLAG_MIME_LEN);
    zret               = NULL == range_field ? 0 : 1;
  }

  return zret;
}

static void
client_add(TSHttpTxn txnp)
{
  // We use @c TSTransformCreate because ATS sees this the same as a transform, but with only
  // the input side hooked up and not the output side. Data flows from ATS in to the reader
  // but not back out to ATS. From the plugin point of view the input data is provided exactly
  // as it is with a transform.
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_CLIENT_HOOK, TSTransformCreate(client_reader, txnp));
}

static int
main_hook(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  TSDebug(PLUGIN_NAME, "Checking transaction");
  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    if (enable_agent_check(txnp)) {
      TSDebug(PLUGIN_NAME, "Adding data sink to transaction");
      client_add(txnp);
    }
    break;
  default:
    break;
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
    TSError(PCP "Plugin registration failed.\n");
    return;
  }

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(main_hook, NULL));
  return;
}
