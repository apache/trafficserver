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
 *    (NT): BNullTransform.dll 
 *    (Solaris): bnull-transform.so 
 *
 *
 */

/* set tab stops to four. */

#include <stdio.h>
#include <ts/ts.h>

#define INK_NULL_MUTEX      NULL
#define STATE_BUFFER_DATA   0
#define STATE_OUTPUT_DATA   1

typedef struct
{
  int state;
  INKVIO output_vio;
  INKIOBuffer output_buffer;
  INKIOBufferReader output_reader;
} MyData;

static MyData *
my_data_alloc()
{
  MyData *data;

  data = (MyData *) INKmalloc(sizeof(MyData));
  data->state = STATE_BUFFER_DATA;
  data->output_vio = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;

  return data;
}

static void
my_data_destroy(MyData * data)
{
  if (data) {
    if (data->output_buffer) {
      INKIOBufferDestroy(data->output_buffer);
    }
    INKfree(data);
  }
}

static int
handle_buffering(INKCont contp, MyData * data)
{
  INKVIO write_vio;
  int towrite;
  int avail;

  /* Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = INKVConnWriteVIOGet(contp);

  /* Create the output buffer and its associated reader */
  if (!data->output_buffer) {
    data->output_buffer = INKIOBufferCreate();
    INKAssert(data->output_buffer);
    data->output_reader = INKIOBufferReaderAlloc(data->output_buffer);
    INKAssert(data->output_reader);
  }

  /* We also check to see if the write VIO's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this buffered
     transformation that means we're done buffering data. */

  if (!INKVIOBufferGet(write_vio)) {
    data->state = STATE_OUTPUT_DATA;
    return 0;
  }

  /* Determine how much data we have left to read. For this bnull
     transform plugin this is also the amount of data we have left
     to write to the output connection. */

  towrite = INKVIONTodoGet(write_vio);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */

    avail = INKIOBufferReaderAvail(INKVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the input buffer. */
      if (INKIOBufferCopy(data->output_buffer, INKVIOReaderGet(write_vio), towrite, 0) == INK_ERROR) {
        INKError("[bnull-transform] Unable to copy read buffer\n");
        goto Lerror;
      }

      /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
      if (INKIOBufferReaderConsume(INKVIOReaderGet(write_vio), towrite) == INK_ERROR) {
        INKError("[bnull-transform] Unable to copy read buffer\n");
        goto Lerror;
      }

      /* Modify the write VIO to reflect how much data we've
         completed. */
      if (INKVIONDoneSet(write_vio, INKVIONDoneGet(write_vio)
                         + towrite) == INK_ERROR) {
        INKError("[bnull-transform] Unable to copy read buffer\n");
        goto Lerror;
      }
    }
  }

  /* Now we check the write VIO to see if there is data left to read. */
  if (INKVIONTodoGet(write_vio) > 0) {
    if (towrite > 0) {
      /* Call back the write VIO continuation to let it know that we
         are ready for more data. */
      INKContCall(INKVIOContGet(write_vio), INK_EVENT_VCONN_WRITE_READY, write_vio);
    }
  } else {
    data->state = STATE_OUTPUT_DATA;

    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    INKContCall(INKVIOContGet(write_vio), INK_EVENT_VCONN_WRITE_COMPLETE, write_vio);
  }

  return 1;

Lerror:

  /* If we are in this code path then something is seriously wrong. */
  INKError("[bnull-transform] Fatal error in plugin");
  INKReleaseAssert(!"[bnull-transform] Fatal error in plugin\n");
  return 0;
}

static int
handle_output(INKCont contp, MyData * data)
{
  /* Check to see if we need to initiate the output operation. */
  if (!data->output_vio) {
    INKVConn output_conn;

    /* Get the output connection where we'll write data to. */
    output_conn = INKTransformOutputVConnGet(contp);

    data->output_vio =
      INKVConnWrite(output_conn, contp, data->output_reader, INKIOBufferReaderAvail(data->output_reader));

    INKAssert(data->output_vio);
  }
  return 1;
}

static void
handle_transform(INKCont contp)
{
  MyData *data;
  int done;

  /* Get our data structure for this operation. The private data
     structure contains the output VIO and output buffer. If the
     private data structure pointer is NULL, then we'll create it
     and initialize its internals. */

  data = INKContDataGet(contp);
  if (!data) {
    data = my_data_alloc();
    INKContDataSet(contp, data);
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
bnull_transform(INKCont contp, INKEvent event, void *edata)
{
  /* Check to see if the transformation has been closed by a 
     call to INKVConnClose. */

  if (INKVConnClosedGet(contp)) {
    my_data_destroy(INKContDataGet(contp));
    INKAssert(INKContDestroy(contp) == INK_SUCCESS);
  } else {
    switch (event) {
    case INK_EVENT_ERROR:{
        INKVIO write_vio;

        /* Get the write VIO for the write operation that was
           performed on ourself. This VIO contains the continuation of
           our parent transformation. */
        write_vio = INKVConnWriteVIOGet(contp);

        /* Call back the write VIO continuation to let it know that we
           have completed the write operation. */
        INKContCall(INKVIOContGet(write_vio), INK_EVENT_ERROR, write_vio);
        break;
      }

    case INK_EVENT_VCONN_WRITE_COMPLETE:
      /* When our output connection says that it has finished
         reading all the data we've written to it then we should
         shutdown the write portion of its connection to
         indicate that we don't want to hear about it anymore. */

      INKAssert(INKVConnShutdown(INKTransformOutputVConnGet(contp), 0, 1) != INK_ERROR);
      break;

    case INK_EVENT_VCONN_WRITE_READY:
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
transformable(INKHttpTxn txnp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKHttpStatus resp_status;
  int retv;

  /* We are only interested in transforming "200 OK" responses. */

  INKHttpTxnServerRespGet(txnp, &bufp, &hdr_loc);
  resp_status = INKHttpHdrStatusGet(bufp, hdr_loc);
  retv = (resp_status == INK_HTTP_STATUS_OK);

  if (INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc) == INK_ERROR) {
    INKError("[bnull-transform] Error releasing MLOC while checking " "header status\n");
  }

  return retv;
}

static void
transform_add(INKHttpTxn txnp)
{
  INKVConn connp;

  connp = INKTransformCreate(bnull_transform, txnp);
  if (INKHttpTxnHookAdd(txnp, INK_HTTP_RESPONSE_TRANSFORM_HOOK, connp)
      == INK_ERROR) {
    /* this should not happen */
    INKError("[bnull-transform] Error adding transform to transaction\n");
  }

  return;
}

static int
transform_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    if (transformable(txnp)) {
      transform_add(txnp);
    }
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    return 0;
  default:
    break;
  }

  return 0;
}

int
check_ts_version()
{
  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {

    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 5.2 */
    if (major_ts_version > 5) {
      result = 1;
    } else if (major_ts_version == 5) {
      if (minor_ts_version >= 2) {
        result = 1;
      }
    }

  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKPluginRegistrationInfo info;
  INKMutex mutex = INK_NULL_MUTEX;

  info.plugin_name = "buffered-null-transform";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_5_2, &info)) {
    INKError("[bnull-transform] Plugin registration failed.\n");
    goto Lerror;
  }

  if (!check_ts_version()) {
    INKError("[bnull-transform] Plugin requires Traffic Server 5.2.0" " or later\n");
    goto Lerror;
  }

  /* This is call we could use if we need to protect global data */
  /* INKReleaseAssert ((mutex = INKMutexCreate()) != INK_NULL_MUTEX); */

  if (INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, INKContCreate(transform_plugin, mutex)) == INK_ERROR) {
    INKError("[bnull-transform] Unable to add READ_RESPONSE_HDR_HOOK\n");
    goto Lerror;
  }

  return;

Lerror:
  INKError("[bnull-transform] Plugin disabled\n");
}
