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

/* bnull-transform.c:  an example program that illustrates a buffered
 *                     null transform.
 *
 *
 *
 *    Usage:
 *      bnull-transform.so
 *
 *
 */

/* set tab stops to four. */

#include <stdio.h>

#include "ts/ts.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "bnull_transform"

#define TS_NULL_MUTEX NULL
#define STATE_BUFFER_DATA 0
#define STATE_OUTPUT_DATA 1

typedef struct {
  int state;
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;
} MyData;

static MyData *
my_data_alloc()
{
  MyData *data;

  data                = (MyData *)TSmalloc(sizeof(MyData));
  data->state         = STATE_BUFFER_DATA;
  data->output_vio    = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;

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

static int
handle_buffering(TSCont contp, MyData *data)
{
  TSVIO write_vio;
  int64_t towrite;

  /* Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = TSVConnWriteVIOGet(contp);

  /* Create the output buffer and its associated reader */
  if (!data->output_buffer) {
    data->output_buffer = TSIOBufferCreate();
    TSAssert(data->output_buffer);
    data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
    TSAssert(data->output_reader);
  }

  /* We also check to see if the write VIO's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this buffered
     transformation that means we're done buffering data. */

  if (!TSVIOBufferGet(write_vio)) {
    data->state = STATE_OUTPUT_DATA;
    return 0;
  }

  /* Determine how much data we have left to read. For this bnull
     transform plugin this is also the amount of data we have left
     to write to the output connection. */

  towrite = TSVIONTodoGet(write_vio);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */

    int64_t avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the input buffer. */
      TSIOBufferCopy(data->output_buffer, TSVIOReaderGet(write_vio), towrite, 0);

      /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
      TSIOBufferReaderConsume(TSVIOReaderGet(write_vio), towrite);

      /* Modify the write VIO to reflect how much data we've
         completed. */
      TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
    }
  }

  /* Now we check the write VIO to see if there is data left to read. */
  if (TSVIONTodoGet(write_vio) > 0) {
    if (towrite > 0) {
      /* Call back the write VIO continuation to let it know that we
         are ready for more data. */
      TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
    }
  } else {
    data->state = STATE_OUTPUT_DATA;

    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
  }

  return 1;
}

static int
handle_output(TSCont contp, MyData *data)
{
  /* Check to see if we need to initiate the output operation. */
  if (!data->output_vio) {
    TSVConn output_conn;

    /* Get the output connection where we'll write data to. */
    output_conn = TSTransformOutputVConnGet(contp);

    data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, TSIOBufferReaderAvail(data->output_reader));

    TSAssert(data->output_vio);
  }
  return 1;
}

static void
handle_transform(TSCont contp)
{
  MyData *data;
  int done;

  /* Get our data structure for this operation. The private data
     structure contains the output VIO and output buffer. If the
     private data structure pointer is NULL, then we'll create it
     and initialize its internals. */

  data = (MyData *)TSContDataGet(contp);
  if (!data) {
    data = my_data_alloc();
    TSContDataSet(contp, (void *)data);
  }

  do {
    switch (data->state) {
    case STATE_BUFFER_DATA:
      done = handle_buffering(contp, data);
      break;
    case STATE_OUTPUT_DATA:
      done = handle_output(contp, data);
      break;
    default:
      done = 1;
      break;
    }
  } while (!done);
}

static int
bnull_transform(TSCont contp, TSEvent event, void *edata ATS_UNUSED)
{
  /* Check to see if the transformation has been closed by a
     call to TSVConnClose. */

  if (TSVConnClosedGet(contp)) {
    my_data_destroy((MyData *)TSContDataGet(contp));
    TSContDestroy(contp);
  } else {
    switch (event) {
    case TS_EVENT_ERROR: {
      TSVIO write_vio;

      /* Get the write VIO for the write operation that was
         performed on ourself. This VIO contains the continuation of
         our parent transformation. */
      write_vio = TSVConnWriteVIOGet(contp);

      /* Call back the write VIO continuation to let it know that we
         have completed the write operation. */
      TSContCall(TSVIOContGet(write_vio), TS_EVENT_ERROR, write_vio);
      break;
    }

    case TS_EVENT_VCONN_WRITE_COMPLETE:
      /* When our output connection says that it has finished
         reading all the data we've written to it then we should
         shutdown the write portion of its connection to
         indicate that we don't want to hear about it anymore. */

      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;

    case TS_EVENT_VCONN_WRITE_READY:
    default:
      /* If we get a WRITE_READY event or any other type of event
         (sent, perhaps, because we were reenabled) then we'll attempt
         to transform more data. */
      handle_transform(contp);
      break;
    }
  }

  return 0;
}

static int
transformable(TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSHttpStatus resp_status;
  int retv = 0;

  /* We are only interested in transforming "200 OK" responses. */

  if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc)) {
    resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
    retv        = (resp_status == TS_HTTP_STATUS_OK);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  }

  return retv;
}

static void
transform_add(TSHttpTxn txnp)
{
  TSVConn connp;

  connp = TSTransformCreate(bnull_transform, txnp);
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
  return;
}

static int
transform_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
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
  TSMutex mutex = TS_NULL_MUTEX;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);

    goto Lerror;
  }

  /* This is call we could use if we need to protect global data */
  /* TSReleaseAssert ((mutex = TSMutexCreate()) != TS_NULL_MUTEX); */

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(transform_plugin, mutex));
  return;

Lerror:
  TSError("[%s] Plugin disabled", PLUGIN_NAME);
}
