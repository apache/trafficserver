/** @file

  Null request transform plugin hooked at TS_HTTP_READ_REQUEST_HDR_HOOK.

  Used by post_early_response_transform.test.py to reproduce a use-after-free
  in HttpSM::state_read_server_response_header() when abort_tunnel() is called
  while a request transform is active. The transform passes request body data
  through unmodified.

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

#include <cstdio>
#include <cinttypes>

#include "ts/ts.h"

#define PLUGIN_NAME "null_transform_request"

typedef struct {
  TSVIO            output_vio;
  TSIOBuffer       output_buffer;
  TSIOBufferReader output_reader;
} TransformData;

static TransformData *
transform_data_alloc()
{
  auto *data          = static_cast<TransformData *>(TSmalloc(sizeof(TransformData)));
  data->output_vio    = nullptr;
  data->output_buffer = nullptr;
  data->output_reader = nullptr;
  return data;
}

static void
transform_data_destroy(TransformData *data)
{
  if (data) {
    if (data->output_buffer) {
      TSIOBufferDestroy(data->output_buffer);
    }
    TSfree(data);
  }
}

static void
handle_transform(TSCont contp)
{
  TSVConn        output_conn = TSTransformOutputVConnGet(contp);
  TSVIO          input_vio   = TSVConnWriteVIOGet(contp);
  TransformData *data        = static_cast<TransformData *>(TSContDataGet(contp));

  if (!data) {
    data                = transform_data_alloc();
    data->output_buffer = TSIOBufferCreate();
    data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
    data->output_vio    = TSVConnWrite(output_conn, contp, data->output_reader, TSVIONBytesGet(input_vio));
    TSContDataSet(contp, data);
  }

  if (!TSVIOBufferGet(input_vio)) {
    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio));
    TSVIOReenable(data->output_vio);
    return;
  }

  int64_t towrite = TSVIONTodoGet(input_vio);
  if (towrite > 0) {
    int64_t avail = TSIOBufferReaderAvail(TSVIOReaderGet(input_vio));
    if (towrite > avail) {
      towrite = avail;
    }
    if (towrite > 0) {
      TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(input_vio), towrite, 0);
      TSIOBufferReaderConsume(TSVIOReaderGet(input_vio), towrite);
      TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + towrite);
    }
  }

  if (TSVIONTodoGet(input_vio) > 0) {
    if (towrite > 0) {
      TSVIOReenable(data->output_vio);
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
    }
  } else {
    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio));
    TSVIOReenable(data->output_vio);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }
}

static int
null_transform(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  if (TSVConnClosedGet(contp)) {
    transform_data_destroy(static_cast<TransformData *>(TSContDataGet(contp)));
    TSContDestroy(contp);
    return 0;
  }

  switch (event) {
  case TS_EVENT_ERROR: {
    TSVIO input_vio = TSVConnWriteVIOGet(contp);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    break;
  }
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;
  default:
    handle_transform(contp);
    break;
  }

  return 0;
}

static int
transform_plugin(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  if (event == TS_EVENT_HTTP_READ_REQUEST_HDR) {
    TSHttpTxn txnp  = static_cast<TSHttpTxn>(edata);
    TSVConn   connp = TSTransformCreate(null_transform, txnp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_REQUEST_TRANSFORM_HOOK, connp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }
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
    TSError("[%s] Unable to initialize plugin (disabled)", PLUGIN_NAME);
    return;
  }

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(transform_plugin, nullptr));
}
