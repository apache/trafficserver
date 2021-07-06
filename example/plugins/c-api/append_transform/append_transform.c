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

/*
 * append_transform.c:  an example program that appends the text
 *                      contained in a file to all HTTP/text response
 *                      bodies.
 *
 *
 *
 *    Usage:
 *      append_transform.so <filename>
 *
 *              <filename> is the name of the file containing the
 *              text to be appended
 *
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "ts/ts.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "append_transform"

#define ASSERT_SUCCESS(_x) TSAssert((_x) == TS_SUCCESS)

typedef struct {
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;
  int append_needed;
} MyData;

static TSIOBuffer append_buffer;
static TSIOBufferReader append_buffer_reader;
static int append_buffer_length;

static MyData *
my_data_alloc()
{
  MyData *data;

  data = (MyData *)TSmalloc(sizeof(MyData));
  TSReleaseAssert(data);

  data->output_vio    = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;
  data->append_needed = 1;

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
  TSVIO write_vio;
  MyData *data;
  int64_t towrite;

  /* Get the output connection where we'll write data to. */
  output_conn = TSTransformOutputVConnGet(contp);

  /* Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = TSVConnWriteVIOGet(contp);

  /* Get our data structure for this operation. The private data
     structure contains the output VIO and output buffer. If the
     private data structure pointer is NULL, then we'll create it
     and initialize its internals. */
  data = TSContDataGet(contp);
  if (!data) {
    towrite = TSVIONBytesGet(write_vio);
    if (towrite != INT64_MAX) {
      towrite += append_buffer_length;
    }
    data                = my_data_alloc();
    data->output_buffer = TSIOBufferCreate();
    data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
    data->output_vio    = TSVConnWrite(output_conn, contp, data->output_reader, towrite);
    TSContDataSet(contp, data);
  }

  /* We also check to see if the write VIO's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this simplistic
     transformation that means we're done. In a more complex
     transformation we might have to finish writing the transformed
     data to our output connection. */
  if (!TSVIOBufferGet(write_vio)) {
    if (data->append_needed) {
      data->append_needed = 0;
      TSIOBufferCopy(TSVIOBufferGet(data->output_vio), append_buffer_reader, append_buffer_length, 0);
    }

    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(write_vio) + append_buffer_length);
    TSVIOReenable(data->output_vio);

    return;
  }

  /* Determine how much data we have left to read. For this append
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
      /* Copy the data from the read buffer to the output buffer. */
      TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(write_vio), towrite, 0);

      /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
      TSIOBufferReaderConsume(TSVIOReaderGet(write_vio), towrite);

      /* Modify the write VIO to reflect how much data we've
         completed. */
      TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
    }
  }

  /* Now we check the write VIO to see if there is data left to
     read. */
  if (TSVIONTodoGet(write_vio) > 0) {
    if (towrite > 0) {
      /* If there is data left to read, then we reenable the output
         connection by reenabling the output VIO. This will wakeup
         the output connection and allow it to consume data from the
         output buffer. */
      TSVIOReenable(data->output_vio);

      /* Call back the write VIO continuation to let it know that we
         are ready for more data. */
      TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
    }
  } else {
    if (data->append_needed) {
      data->append_needed = 0;
      TSIOBufferCopy(TSVIOBufferGet(data->output_vio), append_buffer_reader, append_buffer_length, 0);
    }

    /* If there is no data left to read, then we modify the output
       VIO to reflect how much data the output connection should
       expect. This allows the output connection to know when it
       is done reading. We then reenable the output connection so
       that it can consume the data we just gave it. */
    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(write_vio) + append_buffer_length);
    TSVIOReenable(data->output_vio);

    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
  }
}

static int
append_transform(TSCont contp, TSEvent event, void *edata ATS_UNUSED)
{
  /* Check to see if the transformation has been closed by a call to
     TSVConnClose. */
  if (TSVConnClosedGet(contp)) {
    my_data_destroy(TSContDataGet(contp));
    TSContDestroy(contp);
    return 0;
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
    } break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      /* When our output connection says that it has finished
         reading all the data we've written to it then we should
         shutdown the write portion of its connection to
         indicate that we don't want to hear about it anymore. */
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;
    case TS_EVENT_VCONN_WRITE_READY:
    default:
      /* If we get a WRITE_READY event or any other type of
         event (sent, perhaps, because we were reenabled) then
         we'll attempt to transform more data. */
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
  int val_length;

  if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc)) {
    /*
     *    We are only interested in "200 OK" responses.
     */

    if (TS_HTTP_STATUS_OK == TSHttpHdrStatusGet(bufp, hdr_loc)) {
      /* We only want to do the transformation on documents that have a
         content type of "text/html". */
      TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, "Content-Type", 12);
      if (!field_loc) {
        ASSERT_SUCCESS(TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc));
        return 0;
      }

      const char *value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &val_length);
      if (value && (strncasecmp(value, "text/html", sizeof("text/html") - 1) == 0)) {
        ASSERT_SUCCESS(TSHandleMLocRelease(bufp, hdr_loc, field_loc));
        ASSERT_SUCCESS(TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc));

        return 1;
      } else {
        ASSERT_SUCCESS(TSHandleMLocRelease(bufp, hdr_loc, field_loc));
        ASSERT_SUCCESS(TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc));
        return 0;
      }
    }
  }

  return 0; /* not a 200 */
}

static void
transform_add(TSHttpTxn txnp)
{
  TSVConn connp;

  connp = TSTransformCreate(append_transform, txnp);
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
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

static int
load(const char *filename)
{
  TSFile fp;
  int64_t avail;

  fp = TSfopen(filename, "r");
  if (!fp) {
    return 0;
  }

  append_buffer        = TSIOBufferCreate();
  append_buffer_reader = TSIOBufferReaderAlloc(append_buffer);

  for (;;) {
    TSIOBufferBlock blk = TSIOBufferStart(append_buffer);
    char *p             = TSIOBufferBlockWriteStart(blk, &avail);

    int err = TSfread(fp, p, avail);
    if (err > 0) {
      TSIOBufferProduce(append_buffer, err);
    } else {
      break;
    }
  }

  append_buffer_length = TSIOBufferReaderAvail(append_buffer_reader);

  TSfclose(fp);
  return 1;
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

  if (argc != 2) {
    TSError("[%s] Usage: %s <filename>", PLUGIN_NAME, argv[0]);
    goto Lerror;
  }

  if (!load(argv[1])) {
    TSError("[%s] Could not load %s", PLUGIN_NAME, argv[1]);
    goto Lerror;
  }

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(transform_plugin, NULL));
  return;

Lerror:

  TSError("[%s] Unable to initialize plugin", PLUGIN_NAME);
}
