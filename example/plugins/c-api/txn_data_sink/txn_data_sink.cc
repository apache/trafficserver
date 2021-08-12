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

#include <ts/ts.h>

#include <string>
#include <string_view>

namespace
{
constexpr char const *PLUGIN_NAME = "txn_data_sink";

/** Activate the data sink if this header field is present in the request. */
std::string_view FLAG_HEADER_FIELD = "TS-Agent";

/** The sink data for a transaction. */
struct SinkData {
  /** The bytes for the response body streamed in from the sink.
   *
   * @note This example plugin buffers the body which is useful for the
   * associated Autest. In most production scenarios the user will want to
   * interact with the body as a stream rather than buffering the entire body
   * for each transaction.
   */
  std::string body_bytes;
};

// This serves to consume all the data that arrives. If it's not consumed the tunnel gets stalled
// and the transaction doesn't complete. Other things could be done with the data, accessible via
// the IO buffer @a reader, such as writing it to disk to make an externally accessible copy.
int
client_reader(TSCont contp, TSEvent event, void *edata)
{
  SinkData *data = static_cast<SinkData *>(TSContDataGet(contp));

  // If we got closed, we're done.
  if (TSVConnClosedGet(contp)) {
    delete data;
    TSContDestroy(contp);
    return 0;
  }

  TSVIO input_vio = TSVConnWriteVIOGet(contp);

  if (data == nullptr) {
    data = new SinkData;
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
    // Look for data and if we find any, consume it.
    if (TSVIOBufferGet(input_vio)) {
      TSIOBufferReader reader = TSVIOReaderGet(input_vio);
      size_t const n          = TSIOBufferReaderAvail(reader);
      if (n > 0) {
        auto const offset = data->body_bytes.size();
        data->body_bytes.resize(offset + n);
        TSIOBufferReaderCopy(reader, data->body_bytes.data() + offset, n);

        TSIOBufferReaderConsume(reader, n);
        TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + n);
        TSDebug(PLUGIN_NAME, "Consumed %zd bytes", n);
      }
      if (TSVIONTodoGet(input_vio) > 0) {
        // Signal that we can accept more data.
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
      } else {
        TSDebug(PLUGIN_NAME, "Consumed the following body: \"%.*s\"", (int)data->body_bytes.size(), data->body_bytes.data());
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
      }
    } else { // The buffer is gone so we're done.
      TSDebug(PLUGIN_NAME, "upstream buffer disappeared - %zd bytes", data->body_bytes.size());
    }
    break;
  default:
    TSDebug(PLUGIN_NAME, "unhandled event %d", event);
    break;
  }

  return 0;
}

int
enable_agent_check(TSHttpTxn txnp)
{
  TSMBuffer req_buf;
  TSMLoc req_loc;
  int zret = 0;

  // Enable the sink agent if the header is present.
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &req_buf, &req_loc)) {
    TSMLoc agent_field = TSMimeHdrFieldFind(req_buf, req_loc, FLAG_HEADER_FIELD.data(), FLAG_HEADER_FIELD.length());
    zret               = nullptr == agent_field ? 0 : 1;
  }

  return zret;
}

void
client_add(TSHttpTxn txnp)
{
  // We use @c TSTransformCreate because ATS sees this the same as a transform, but with only
  // the input side hooked up and not the output side. Data flows from ATS in to the reader
  // but not back out to ATS. From the plugin point of view the input data is provided exactly
  // as it is with a transform.
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_CLIENT_HOOK, TSTransformCreate(client_reader, txnp));
}

int
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
} // anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed.", PLUGIN_NAME);
    return;
  }

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(main_hook, nullptr));
  return;
}
