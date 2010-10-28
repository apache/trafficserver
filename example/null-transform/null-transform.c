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
 * 	(NT): NullTransform.dll
 *	(Solaris): null-transform.so
 *
 *
 */

#include <stdio.h>
#include <ts/ts.h>

typedef struct
{
  INKVIO output_vio;
  INKIOBuffer output_buffer;
  INKIOBufferReader output_reader;
} MyData;

static MyData *
my_data_alloc()
{
  MyData *data;

  data = (MyData *) INKmalloc(sizeof(MyData));
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
      INKAssert(INKIOBufferDestroy(data->output_buffer) == INK_SUCCESS);
    }
    INKfree(data);
  }
}

static void
handle_transform(INKCont contp)
{
  INKVConn output_conn;
  INKIOBuffer buf_test;
  INKVIO input_vio;
  MyData *data;
  int towrite;
  int avail;

  INKDebug("null-transform", "Entering handle_transform()");
  /* Get the output (downstream) vconnection where we'll write data to. */

  output_conn = INKTransformOutputVConnGet(contp);

  /* Get the write VIO for the write operation that was performed on
   * ourself. This VIO contains the buffer that we are to read from
   * as well as the continuation we are to call when the buffer is
   * empty. This is the input VIO (the write VIO for the upstream
   * vconnection).
   */
  input_vio = INKVConnWriteVIOGet(contp);
  if (input_vio == INK_ERROR_PTR) {
    INKError("[null-transform] Unable to fetching input VIO\n");
    goto Lerror;
  }

  /* Get our data structure for this operation. The private data
   * structure contains the output VIO and output buffer. If the
   * private data structure pointer is NULL, then we'll create it
   * and initialize its internals.
   */
  data = INKContDataGet(contp);
  if (!data) {
    data = my_data_alloc();
    data->output_buffer = INKIOBufferCreate();
    data->output_reader = INKIOBufferReaderAlloc(data->output_buffer);
    INKDebug("null-transform", "\tWriting %d bytes on VConn", INKVIONBytesGet(input_vio));
    data->output_vio = INKVConnWrite(output_conn, contp, data->output_reader, INKVIONBytesGet(input_vio));
    if (INKContDataSet(contp, data) == INK_ERROR) {
      INKError("[null-transform] unable to set continuation " "data!\n");
      goto Lerror;
    }
  }

  /* We also check to see if the input VIO's buffer is non-NULL. A
   * NULL buffer indicates that the write operation has been
   * shutdown and that the upstream continuation does not want us to send any
   * more WRITE_READY or WRITE_COMPLETE events. For this simplistic
   * transformation that means we're done. In a more complex
   * transformation we might have to finish writing the transformed
   * data to our output connection.
   */
  buf_test = INKVIOBufferGet(input_vio);

  if (buf_test) {
    if (buf_test == INK_ERROR_PTR) {
      INKError("[null-transform] error fetching buffer\n");
      goto Lerror;
    }
  } else {
    if (INKVIONBytesSet(data->output_vio, INKVIONDoneGet(input_vio)) == INK_ERROR) {
      INKError("[null-transform] error seting output VIO nbytes\n");
      goto Lerror;
    }

    if (INKVIOReenable(data->output_vio) == INK_ERROR) {
      INKError("[null-transform] error reenabling output VIO\n");
      goto Lerror;
    }

    return;
  }

  /* Determine how much data we have left to read. For this null
   * transform plugin this is also the amount of data we have left
   * to write to the output connection.
   */
  towrite = INKVIONTodoGet(input_vio);
  INKDebug("null-transform", "\ttoWrite is %d", towrite);

  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
     * the amount of data actually in the read buffer.
     */
    avail = INKIOBufferReaderAvail(INKVIOReaderGet(input_vio));
    INKDebug("null-transform", "\tavail is %d", avail);
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the output buffer. */
      if (INKIOBufferCopy(INKVIOBufferGet(data->output_vio), INKVIOReaderGet(input_vio), towrite, 0) == INK_ERROR) {
        INKError("[null-plugin] unable to copy IO buffers\n");
        goto Lerror;
      }

      /* Tell the read buffer that we have read the data and are no
       * longer interested in it.
       */
      if (INKIOBufferReaderConsume(INKVIOReaderGet(input_vio), towrite) == INK_ERROR) {
        INKError("[null-plugin] unable to update VIO reader\n");
        goto Lerror;
      }

      /* Modify the input VIO to reflect how much data we've
       * completed.
       */
      if (INKVIONDoneSet(input_vio, INKVIONDoneGet(input_vio) + towrite) == INK_ERROR) {
        INKError("[null-plugin] unable to update VIO\n");
        goto Lerror;
      }
    }
  } else if (towrite == INK_ERROR) {
    INKError("[null-plugin] error fetching VIO to-do amount\n");
    goto Lerror;
  }

  /* Now we check the input VIO to see if there is data left to
   * read.
   */
  if (INKVIONTodoGet(input_vio) > 0) {
    if (towrite > 0) {
      /* If there is data left to read, then we reenable the output
       * connection by reenabling the output VIO. This will wake up
       * the output connection and allow it to consume data from the
       * output buffer.
       */
      if (INKVIOReenable(data->output_vio) == INK_ERROR) {
        INKError("[null-plugin] error reenabling transaction\n");
        goto Lerror;
      }
      /* Call back the input VIO continuation to let it know that we
       * are ready for more data.
       */
      INKContCall(INKVIOContGet(input_vio), INK_EVENT_VCONN_WRITE_READY, input_vio);
    }
  } else {
    /* If there is no data left to read, then we modify the output
     * VIO to reflect how much data the output connection should
     * expect. This allows the output connection to know when it
     * is done reading. We then reenable the output connection so
     * that it can consume the data we just gave it.
     */
    INKVIONBytesSet(data->output_vio, INKVIONDoneGet(input_vio));
    if (INKVIOReenable(data->output_vio) == INK_ERROR) {
      INKError("[null-plugin] error reenabling transaction\n");
      goto Lerror;
    }

    /* Call back the input VIO continuation to let it know that we
     * have completed the write operation.
     */
    INKContCall(INKVIOContGet(input_vio), INK_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }


Lerror:
  return;
}

static int
null_transform(INKCont contp, INKEvent event, void *edata)
{
  /* Check to see if the transformation has been closed by a call to
   * INKVConnClose.
   */
  INKDebug("null-transform", "Entering null_transform()");

  if (INKVConnClosedGet(contp)) {
    INKDebug("null-transform", "\tVConn is closed");
    my_data_destroy(INKContDataGet(contp));
    INKAssert(INKContDestroy(contp) == INK_SUCCESS);
    return 0;
  } else {
    switch (event) {
    case INK_EVENT_ERROR:
      {
        INKVIO input_vio;

        INKDebug("null-transform", "\tEvent is INK_EVENT_ERROR");
        /* Get the write VIO for the write operation that was
         * performed on ourself. This VIO contains the continuation of
         * our parent transformation. This is the input VIO.
         */
        input_vio = INKVConnWriteVIOGet(contp);

        /* Call back the write VIO continuation to let it know that we
         * have completed the write operation.
         */
        INKContCall(INKVIOContGet(input_vio), INK_EVENT_ERROR, input_vio);
      }
      break;
    case INK_EVENT_VCONN_WRITE_COMPLETE:
      INKDebug("null-transform", "\tEvent is INK_EVENT_VCONN_WRITE_COMPLETE");
      /* When our output connection says that it has finished
       * reading all the data we've written to it then we should
       * shutdown the write portion of its connection to
       * indicate that we don't want to hear about it anymore.
       */
      INKAssert(INKVConnShutdown(INKTransformOutputVConnGet(contp), 0, 1) != INK_ERROR);
      break;
    case INK_EVENT_VCONN_WRITE_READY:
      INKDebug("null-transform", "\tEvent is INK_EVENT_VCONN_WRITE_READY");
    default:
      INKDebug("null-transform", "\t(event is %d)", event);
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
transformable(INKHttpTxn txnp)
{
  /*
   *  We are only interested in transforming "200 OK" responses.
   */

  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKHttpStatus resp_status;
  int retv;

  INKDebug("null-transform", "Entering transformable()");

  INKHttpTxnServerRespGet(txnp, &bufp, &hdr_loc);
  resp_status = INKHttpHdrStatusGet(bufp, hdr_loc);
  retv = (resp_status == INK_HTTP_STATUS_OK);

  if (INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc) == INK_ERROR) {
    INKError("[null-transform] Error releasing MLOC while checking " "header status\n");
  }

  INKDebug("null-transform", "Exiting transformable with return %d", retv);
  return retv;
}

static void
transform_add(INKHttpTxn txnp)
{
  INKVConn connp;

  INKDebug("null-transform", "Entering transform_add()");
  connp = INKTransformCreate(null_transform, txnp);
  if (INKHttpTxnHookAdd(txnp, INK_HTTP_RESPONSE_TRANSFORM_HOOK, connp) == INK_ERROR) {
    INKError("[null-plugin] Unable to attach plugin to transaction\n");
  }
}

static int
transform_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  INKDebug("null-transform", "Entering transform_plugin()");
  switch (event) {
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    INKDebug("null-transform", "\tEvent is INK_EVENT_HTTP_READ_RESPONSE_HDR");
    if (transformable(txnp)) {
      transform_add(txnp);
    }

    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
      INKError("[null-plugin] Alert! unable to continue " "the HTTP transaction\n");
      return -1;
    }
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

    /* Need at least TS 2.0 */
    if (major_ts_version >= 2) {
      result = 1;
    }

  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKPluginRegistrationInfo info;

  info.plugin_name = "null-transform";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("[null-transform] Plugin registration failed.\n");
    goto Lerror;
  }

  if (!check_ts_version()) {
    INKError("[null-transform] Plugin requires Traffic Server 2.0 " "or later\n");
    goto Lerror;
  }

  if (INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, INKContCreate(transform_plugin, NULL)) == INK_ERROR) {
    INKError("[null-transform] Unable to set READ_RESPONSE_HDR_HOOK\n");
    goto Lerror;
  }

  return;

Lerror:
  INKError("[null-tranform] Unable to initialize plugin (disabled).\n");
}
