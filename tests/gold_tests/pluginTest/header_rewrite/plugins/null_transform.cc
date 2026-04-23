/** @file
 *
 *  Minimal null transform plugin that passes response body data through
 *  unchanged. Used to test that set-body takes precedence over transforms
 *  in the TRANSFORM_READ path of handle_api_return().
 *
 *  When both this plugin and set-body are active on the same transaction,
 *  the set-body replacement should be served and this transform should be
 *  bypassed.
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

#include <cstdint>

#define PLUGIN_NAME "null_transform"

static DbgCtl dbg_ctl{PLUGIN_NAME};

struct TransformData {
  TSIOBuffer       output_buffer;
  TSIOBufferReader output_reader;
  TSVIO            output_vio;
};

static TransformData *
transform_data_alloc()
{
  auto *data          = static_cast<TransformData *>(TSmalloc(sizeof(TransformData)));
  data->output_buffer = TSIOBufferCreate();
  data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
  data->output_vio    = nullptr;
  return data;
}

[[maybe_unused]] static void
transform_data_destroy(TransformData *data)
{
  if (data) {
    if (data->output_reader) {
      TSIOBufferReaderFree(data->output_reader);
    }
    if (data->output_buffer) {
      TSIOBufferDestroy(data->output_buffer);
    }
    TSfree(data);
  }
}

static int
null_transform_handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  TSVConn output_conn = TSTransformOutputVConnGet(contp);

  auto *data = static_cast<TransformData *>(TSContDataGet(contp));

  if (event == TS_EVENT_ERROR) {
    TSVIO input_vio = TSVConnWriteVIOGet(contp);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    return 0;
  }

  if (event == TS_EVENT_VCONN_WRITE_COMPLETE) {
    TSVConnShutdown(output_conn, 0, 1);
    return 0;
  }

  // TS_EVENT_IMMEDIATE or TS_EVENT_VCONN_WRITE_READY
  TSVIO input_vio = TSVConnWriteVIOGet(contp);
  if (!input_vio) {
    return 0;
  }

  if (!data->output_vio) {
    data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, TSVIONBytesGet(input_vio));
  }

  int64_t avail = TSIOBufferReaderAvail(TSVIOReaderGet(input_vio));
  if (avail > 0) {
    // Copy data straight through (null transform)
    TSIOBufferCopy(data->output_buffer, TSVIOReaderGet(input_vio), avail, 0);
    TSIOBufferReaderConsume(TSVIOReaderGet(input_vio), avail);
    TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + avail);
  }

  if (TSVIONTodoGet(input_vio) > 0) {
    TSVIOReenable(data->output_vio);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
  } else {
    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio));
    TSVIOReenable(data->output_vio);
  }

  return 0;
}

static int
transform_hook_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (event == TS_EVENT_HTTP_READ_RESPONSE_HDR) {
    Dbg(dbg_ctl, "Adding null transform to transaction");
    TSCont transform_contp = TSTransformCreate(null_transform_handler, txnp);
    auto  *data            = transform_data_alloc();
    TSContDataSet(transform_contp, data);
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, transform_contp);
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  Dbg(dbg_ctl, "Initialized");

  TSCont contp = TSContCreate(transform_hook_handler, nullptr);
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
}
