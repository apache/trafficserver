/** @file

  An example program that does a null transform of response body content.

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
#include <unistd.h>
#include <cinttypes>

#include "ts/ts.h"

#define PLUGIN_NAME "null_transform"

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};
}

struct MyData {
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;
};

static MyData *
my_data_alloc()
{
  MyData *data;

  data                = static_cast<MyData *>(TSmalloc(sizeof(MyData)));
  data->output_vio    = nullptr;
  data->output_buffer = nullptr;
  data->output_reader = nullptr;

  return data;
}

static void
my_data_destroy(MyData *data)
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
  TSVConn output_conn;
  TSIOBuffer buf_test;
  TSVIO input_vio;
  MyData *data;
  int64_t towrite;

  Dbg(dbg_ctl, "Entering handle_transform()");
  /* Get the output (downstream) vconnection where we'll write data to. */

  output_conn = TSTransformOutputVConnGet(contp);

  /* Get the write VIO for the write operation that was performed on
   * ourself. This VIO contains the buffer that we are to read from
   * as well as the continuation we are to call when the buffer is
   * empty. This is the input VIO (the write VIO for the upstream
   * vconnection).
   */
  input_vio = TSVConnWriteVIOGet(contp);

  /* Get our data structure for this operation. The private data
   * structure contains the output VIO and output buffer. If the
   * private data structure pointer is null, then we'll create it
   * and initialize its internals.
   */
  data = static_cast<decltype(data)>(TSContDataGet(contp));
  if (!data) {
    data                = my_data_alloc();
    data->output_buffer = TSIOBufferCreate();
    data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
    Dbg(dbg_ctl, "\tWriting %" PRId64 " bytes on VConn", TSVIONBytesGet(input_vio));
    data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, INT64_MAX);
    TSContDataSet(contp, data);
  }

  /* We also check to see if the input VIO's buffer is non-null. A
   * null buffer indicates that the write operation has been
   * shutdown and that the upstream continuation does not want us to send any
   * more WRITE_READY or WRITE_COMPLETE events. For this simplistic
   * transformation that means we're done. In a more complex
   * transformation we might have to finish writing the transformed
   * data to our output connection.
   */
  buf_test = TSVIOBufferGet(input_vio);

  if (!buf_test) {
    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio));
    TSVIOReenable(data->output_vio);
    return;
  }

  /* Determine how much data we have left to read. For this null
   * transform plugin this is also the amount of data we have left
   * to write to the output connection.
   */
  towrite = TSVIONTodoGet(input_vio);
  Dbg(dbg_ctl, "\ttoWrite is %" PRId64 "", towrite);

  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
     * the amount of data actually in the read buffer.
     */
    int64_t avail = TSIOBufferReaderAvail(TSVIOReaderGet(input_vio));
    Dbg(dbg_ctl, "\tavail is %" PRId64 "", avail);
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the output buffer. */
      TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(input_vio), towrite, 0);

      /* Tell the read buffer that we have read the data and are no
       * longer interested in it.
       */
      TSIOBufferReaderConsume(TSVIOReaderGet(input_vio), towrite);

      /* Modify the input VIO to reflect how much data we've
       * completed.
       */
      TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + towrite);
    }
  }

  /* Now we check the input VIO to see if there is data left to
   * read.
   */
  if (TSVIONTodoGet(input_vio) > 0) {
    if (towrite > 0) {
      /* If there is data left to read, then we reenable the output
       * connection by reenabling the output VIO. This will wake up
       * the output connection and allow it to consume data from the
       * output buffer.
       */
      TSVIOReenable(data->output_vio);

      /* Call back the input VIO continuation to let it know that we
       * are ready for more data.
       */
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
    }
  } else {
    /* If there is no data left to read, then we modify the output
     * VIO to reflect how much data the output connection should
     * expect. This allows the output connection to know when it
     * is done reading. We then reenable the output connection so
     * that it can consume the data we just gave it.
     */
    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio));
    TSVIOReenable(data->output_vio);

    /* Call back the input VIO continuation to let it know that we
     * have completed the write operation.
     */
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }
}

static int
null_transform(TSCont contp, TSEvent event, void *edata)
{
  /* Check to see if the transformation has been closed by a call to
   * TSVConnClose.
   */
  Dbg(dbg_ctl, "Entering null_transform()");

  if (TSVConnClosedGet(contp)) {
    Dbg(dbg_ctl, "\tVConn is closed");
    my_data_destroy(static_cast<MyData *>(TSContDataGet(contp)));
    TSContDestroy(contp);
    return 0;
  } else {
    switch (event) {
    case TS_EVENT_ERROR: {
      TSVIO input_vio;

      Dbg(dbg_ctl, "\tEvent is TS_EVENT_ERROR");
      /* Get the write VIO for the write operation that was
       * performed on ourself. This VIO contains the continuation of
       * our parent transformation. This is the input VIO.
       */
      input_vio = TSVConnWriteVIOGet(contp);

      /* Call back the write VIO continuation to let it know that we
       * have completed the write operation.
       */
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    } break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      Dbg(dbg_ctl, "\tEvent is TS_EVENT_VCONN_WRITE_COMPLETE");
      /* When our output connection says that it has finished
       * reading all the data we've written to it then we should
       * shutdown the write portion of its connection to
       * indicate that we don't want to hear about it anymore.
       */
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;

    /* If we get a WRITE_READY event or any other type of
     * event (sent, perhaps, because we were re-enabled) then
     * we'll attempt to transform more data.
     */
    case TS_EVENT_VCONN_WRITE_READY:
      Dbg(dbg_ctl, "\tEvent is TS_EVENT_VCONN_WRITE_READY");
      handle_transform(contp);
      break;
    default:
      Dbg(dbg_ctl, "\t(event is %d)", event);
      handle_transform(contp);
      break;
    }
  }

  return 0;
}

static int
transformable(TSHttpTxn txnp)
{
  /*
   *  We are only interested in transforming "200 OK" responses.
   */

  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSHttpStatus resp_status;
  int retv = 0;

  Dbg(dbg_ctl, "Entering transformable()");

  if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc)) {
    resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
    retv        = (resp_status == TS_HTTP_STATUS_OK);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  }

  Dbg(dbg_ctl, "Exiting transformable with return %d", retv);
  return retv;
}

static void
transform_add(TSHttpTxn txnp)
{
  TSVConn connp;

  Dbg(dbg_ctl, "Entering transform_add()");
  connp = TSTransformCreate(null_transform, txnp);
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
}

static int
transform_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  Dbg(dbg_ctl, "Entering transform_plugin()");
  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    Dbg(dbg_ctl, "\tEvent is TS_EVENT_HTTP_READ_RESPONSE_HDR");
    if (transformable(txnp)) {
      transform_add(txnp);
    }

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  default:
    break;
  }

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

    goto Lerror;
  }

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(transform_plugin, nullptr));
  return;

Lerror:
  TSError("[%s] Unable to initialize plugin (disabled)", PLUGIN_NAME);
}
