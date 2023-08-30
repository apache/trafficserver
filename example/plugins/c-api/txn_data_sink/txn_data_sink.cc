/** @file

  Example plugin for using the TS_HTTP_REQUEST_CLIENT_HOOK and TS_HTTP_RESPONSE_CLIENT_HOOK hooks.

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

DbgCtl dbg_ctl{PLUGIN_NAME};

/** The flag for activating response body data sink for a transaction. */
std::string_view FLAG_DUMP_RESPONSE_BODY = "X-Dump-Response";

/** The flag for activating request body data sink for a transaction. */
std::string_view FLAG_DUMP_REQUEST_BODY = "X-Dump-Request";

/** The sink data for a transaction. */
struct SinkData {
  /** The bytes for the response body streamed in from the sink.
   *
   * @note This example plugin buffers the body which is useful for the
   * associated Autest. In most production scenarios the user will want to
   * interact with the body as a stream rather than buffering the entire body
   * for each transaction.
   */
  std::string response_body_bytes;

  /** The bytes for the request body streamed in from the sink.
   *
   * @note This example plugin buffers the body which is useful for the
   * associated Autest. In most production scenarios the user will want to
   * interact with the body as a stream rather than buffering the entire body
   * for each transaction.
   */
  std::string request_body_bytes;
};

/** A flag to request that response body bytes be sinked. */
constexpr bool SINK_RESPONSE_BODY = true;

/** A flag to request that request body bytes be sinked. */
constexpr bool SINK_REQUEST_BODY = false;

/** This serves to consume all the data that arrives in the VIO.
 *
 * Note that if any data is not consumed then the tunnel gets stalled and the
 * transaction doesn't complete. Various things can be done with the data,
 * accessible via the IO buffer @a reader, such as writing it to disk in order
 * to make an externally accessible copy.
 *
 * @param[in] sync_response_body: Indicates whether response body bytes should
 * be consumed.
 */
int
body_reader_helper(TSCont contp, TSEvent event, bool sync_response_body)
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

  std::string &body_bytes = sync_response_body ? data->response_body_bytes : data->request_body_bytes;

  switch (event) {
  case TS_EVENT_ERROR:
    Dbg(dbg_ctl, "Error event");
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
    Dbg(dbg_ctl, "READ_COMPLETE");
    break;
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_IMMEDIATE:
    Dbg(dbg_ctl, "Data event - %s", event == TS_EVENT_IMMEDIATE ? "IMMEDIATE" : "READ_READY");
    // Look for data and if we find any, consume it.
    if (TSVIOBufferGet(input_vio)) {
      TSIOBufferReader reader = TSVIOReaderGet(input_vio);
      size_t const n          = TSIOBufferReaderAvail(reader);
      if (n > 0) {
        auto const offset = body_bytes.size();
        body_bytes.resize(offset + n);
        TSIOBufferReaderCopy(reader, body_bytes.data() + offset, n);

        TSIOBufferReaderConsume(reader, n);
        TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + n);
        Dbg(dbg_ctl, "Consumed %zd bytes", n);
      }
      if (TSVIONTodoGet(input_vio) > 0) {
        // Signal that we can accept more data.
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
      } else {
        Dbg(dbg_ctl, "Consumed the following body: \"%.*s\"", static_cast<int>(body_bytes.size()), body_bytes.data());
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
      }
    } else { // The buffer is gone so we're done.
      Dbg(dbg_ctl, "upstream buffer disappeared - %zd bytes", body_bytes.size());
    }
    break;
  default:
    Dbg(dbg_ctl, "unhandled event %d", event);
    break;
  }

  return 0;
}

/** The handler for transaction data sink for response bodies. */
int
response_body_reader(TSCont contp, TSEvent event, void *edata)
{
  return body_reader_helper(contp, event, SINK_RESPONSE_BODY);
}

/** The handler for transaction data sink for request bodies. */
int
request_body_reader(TSCont contp, TSEvent event, void *edata)
{
  return body_reader_helper(contp, event, SINK_REQUEST_BODY);
}

/** A helper function for common logic between request_sink_requested and
 * response_sink_requested. */
bool
sink_requested_helper(TSHttpTxn txnp, std::string_view header)
{
  TSMLoc field      = nullptr;
  TSMBuffer req_buf = nullptr;
  TSMLoc req_loc    = nullptr;
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &req_buf, &req_loc)) {
    field = TSMimeHdrFieldFind(req_buf, req_loc, header.data(), header.length());
  }

  return field != nullptr;
}

/** Determine whether the headers enable request body sink.
 *
 * Inspect the given request headers for the flag that enables request body
 * sink.
 *
 * @param[in] txnp The transaction with the request headers to search for the
 * header that enables request body sink.
 *
 * @return True if the headers enable request body sink, false otherwise.
 */
bool
request_sink_requested(TSHttpTxn txnp)
{
  return sink_requested_helper(txnp, FLAG_DUMP_REQUEST_BODY);
}

/** Determine whether the headers enable response body sink.
 *
 * Inspect the given response headers for the flag that enables response body
 * sink.
 *
 * @param[in] txnp The transaction with the response headers to search for the
 * header that enables response body sink.
 *
 * @return True if the headers enable response body sink, false otherwise.
 */
bool
response_sink_requested(TSHttpTxn txnp)
{
  return sink_requested_helper(txnp, FLAG_DUMP_RESPONSE_BODY);
}

/** Implements the handler for inspecting the request header bytes and enabling
 * transaction data sink if X-Dump-Request or X-Dump-Response flags are used.
 */
int
main_hook(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  Dbg(dbg_ctl, "Checking transaction for any flags to enable transaction data sink.");
  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    /// We use @c TSTransformCreate because ATS sees this the same as a
    /// transform, but with only the input side hooked up and not the output
    /// side. Data flows from ATS in to the reader but not back out to ATS.
    /// From the plugin point of view the input data is provided exactly as it
    /// is with a transform.
    if (response_sink_requested(txnp)) {
      TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_CLIENT_HOOK, TSTransformCreate(response_body_reader, txnp));
      Dbg(dbg_ctl, "Adding response data sink to transaction");
    }
    if (request_sink_requested(txnp)) {
      TSHttpTxnHookAdd(txnp, TS_HTTP_REQUEST_CLIENT_HOOK, TSTransformCreate(request_body_reader, txnp));
      Dbg(dbg_ctl, "Adding request data sink to transaction");
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

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(main_hook, nullptr));
  return;
}
