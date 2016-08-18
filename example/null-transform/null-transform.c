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

/* null-transform.c:  an example program that does a null transform
 *                    of response body content
 *
 *
 *
 *	Usage:
 *	  null-transform.so
 *
 *
 */

#include <stdio.h>
#include <unistd.h>

#include "ts/ts.h"
#include "ts/ink_defs.h"

typedef struct {
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;
} MyData;

static MyData *
my_data_alloc()
{
  MyData *data;

  data                = (MyData *)TSmalloc(sizeof(MyData));
  data->output_vio    = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;

  return data;
}

static void
my_data_destroy(MyData *data)
{
  if (data) {
    if (data->output_buffer)
      TSIOBufferDestroy(data->output_buffer);
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
  int64_t avail;

  TSDebug("null-transform", "Entering handle_transform()");
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
   * private data structure pointer is NULL, then we'll create it
   * and initialize its internals.
   */
  data = TSContDataGet(contp);
  if (!data) {
    data                = my_data_alloc();
    data->output_buffer = TSIOBufferCreate();
    data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
    TSDebug("null-transform", "\tWriting %" PRId64 " bytes on VConn", TSVIONBytesGet(input_vio));
    // data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, INT32_MAX);
    data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, INT64_MAX);
    // data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, TSVIONBytesGet(input_vio));
    TSContDataSet(contp, data);
  }

  /* We also check to see if the input VIO's buffer is non-NULL. A
   * NULL buffer indicates that the write operation has been
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
  TSDebug("null-transform", "\ttoWrite is %" PRId64 "", towrite);

  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
     * the amount of data actually in the read buffer.
     */
    avail = TSIOBufferReaderAvail(TSVIOReaderGet(input_vio));
    TSDebug("null-transform", "\tavail is %" PRId64 "", avail);
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
null_transform(TSCont contp, TSEvent event, void *edata ATS_UNUSED)
{
  /* Check to see if the transformation has been closed by a call to
   * TSVConnClose.
   */
  TSDebug("null-transform", "Entering null_transform()");

  if (TSVConnClosedGet(contp)) {
    TSDebug("null-transform", "\tVConn is closed");
    my_data_destroy(TSContDataGet(contp));
    TSContDestroy(contp);
    return 0;
  } else {
    switch (event) {
    case TS_EVENT_ERROR: {
      TSVIO input_vio;

      TSDebug("null-transform", "\tEvent is TS_EVENT_ERROR");
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
      TSDebug("null-transform", "\tEvent is TS_EVENT_VCONN_WRITE_COMPLETE");
      /* When our output connection says that it has finished
       * reading all the data we've written to it then we should
       * shutdown the write portion of its connection to
       * indicate that we don't want to hear about it anymore.
       */
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;
    case TS_EVENT_VCONN_WRITE_READY:
      TSDebug("null-transform", "\tEvent is TS_EVENT_VCONN_WRITE_READY");
    default:
      TSDebug("null-transform", "\t(event is %d)", event);
      /* If we get a WRITE_READY event or any other type of
       * event (sent, perhaps, because we were reenabled) then
       * we'll attempt to transform more data.
       */
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

  TSDebug("null-transform", "Entering transformable()");

  if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc)) {
    resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
    retv        = (resp_status == TS_HTTP_STATUS_OK);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  }

  TSDebug("null-transform", "Exiting transformable with return %d", retv);
  return retv;
}

static void
transform_add(TSHttpTxn txnp)
{
  TSVConn connp;

  TSDebug("null-transform", "Entering transform_add()");
  connp = TSTransformCreate(null_transform, txnp);
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
}

static int
transform_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  TSDebug("null-transform", "Entering transform_plugin()");
  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    TSDebug("null-transform", "\tEvent is TS_EVENT_HTTP_READ_RESPONSE_HDR");
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
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = "null-transform";
  info.vendor_name   = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[null-transform] Plugin registration failed.");

    goto Lerror;
  }

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(transform_plugin, NULL));
  return;

Lerror:
  TSError("[null-transform] Unable to initialize plugin (disabled).");
}
