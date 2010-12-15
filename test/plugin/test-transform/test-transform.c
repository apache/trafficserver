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
 * This plugin is used to test the following APIs:
 * -- TSHttpTxnUntransformedRespCache
 * -- TSHttpTxnTransformedRespCache
 * -- TSHttpTxnTransformRespGet
 * -- TSIOBufferBlockNext
 * -- TSIOBufferBlockReadAvail
 * -- TSIOBufferBlockWriteAvail
 * -- TSVConnReadVIOGet
 * -- TSVIOMutexGet
 * -- TSVIOVConnGet
 *
 * It is based on the null-transform plugin. The above function calls are inserted into the appropriate places.
 */

#include <stdio.h>
#include "ts.h"

#define DBG_TAG "test-transform-dbg"

#define PLUGIN_NAME "test-transform"
#define VALID_POINTER(X) ((X != NULL) && (X != TS_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_AND_RETURN(API_NAME) { \
    LOG_ERROR(API_NAME); \
    return -1; \
}
#define LOG_ERROR_AND_CLEANUP(API_NAME) { \
  LOG_ERROR(API_NAME); \
  goto Lcleanup; \
}
#define LOG_ERROR_AND_REENABLE(API_NAME) { \
  LOG_ERROR(API_NAME); \
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}


typedef struct
{
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;
  TSHttpTxn txn;
  int init_done;
} MyData;

static MyData *
my_data_alloc()
{
  MyData *data;

  data = (MyData *) TSmalloc(sizeof(MyData));
  data->output_vio = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;
  data->txn = NULL;
  data->init_done = 0;

  return data;
}

static void
my_data_destroy(MyData * data)
{
  if (data) {
    if (data->output_buffer) {
      TSIOBufferDestroy(data->output_buffer);
    }
    TSfree(data);
  }
}

/*
 * test the following VIO functions:
 * -- TSVIOMutexGet
 * -- TSVIOVConnGet
 */
static void
test_vio(TSCont contp)
{
  LOG_SET_FUNCTION_NAME("test_vio");
  TSVConn vio_vconn;
  TSVIO output_vio;
  TSVIO input_vio;
  TSMutex m1, m2;

  /* Get the read (output) VIO for the vconnection */
  if ((output_vio = TSVConnReadVIOGet(contp)) == TS_ERROR_PTR)
    LOG_ERROR("TSVConnReadVIOGet");
  if ((input_vio = TSVConnWriteVIOGet(contp)) == TS_ERROR_PTR)
    LOG_ERROR("TSVConnWriteVIOGet")

      /* get the mutex of the VIO */
      if ((m1 = TSVIOMutexGet(input_vio)) == TS_ERROR_PTR)
      LOG_ERROR("TSVIOMutexGet");

  /* get the vio mutex */
  if ((m2 = TSContMutexGet(contp)) == TS_ERROR_PTR)
    LOG_ERROR("TSContMutexGet");

  /* the VIO mutex should equal to the VConn mutex */
  if (m1 != m2)
    LOG_ERROR("TSVIOMutexGet");

  /* get the VConn of the VIO */
  if ((vio_vconn = TSVIOVConnGet(input_vio)) == TS_ERROR_PTR)
    LOG_ERROR("TSVIOVConnGet");

  /* the vconn should equal to the continuation */
  if (vio_vconn != contp)
    LOG_ERROR("vio_vconn");

  /* negative test */
#ifdef DEBUG
  if (TSVConnReadVIOGet(NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSVConnReadVIOGet");
  if (TSVConnWriteVIOGet(NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSVConnWriteVIOGet")
      if (TSVIOMutexGet(NULL) != TS_ERROR_PTR)
      LOG_ERROR_NEG("TSVIOMutexGet");
  if (TSContMutexGet(NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSContMutexGet");
#endif

}

/* test several IOBuffer functions */
static void
test_iobuffer()
{
  LOG_SET_FUNCTION_NAME("test_iobuffer");

  TSIOBuffer bufp = NULL;
  TSIOBufferBlock blockp = NULL;
  TSIOBufferReader readerp = NULL;
  int read_avail = 0, write_avail = 0, writestart = 0, avail = 0, towrite = 0;
  char *start;
  const char *STRING_CONSTANT = "constant string to be copied into an iobuffer";

  /* create an IOBuffer */
  if ((bufp = TSIOBufferCreate()) == TS_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("TSIOBufferCreate");


  /* Write STRING_CONSTANT at the beginning of the iobuffer */
  if ((blockp = TSIOBufferStart(bufp)) == TS_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("TSIOBufferStart");
  if ((start = TSIOBufferBlockWriteStart(blockp, &avail)) == TS_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("TSIOBufferBlockWriteStart");

  towrite = strlen(STRING_CONSTANT);
  while (start && towrite > 0) {
    if (towrite > avail) {
      memcpy(start, &STRING_CONSTANT[writestart], avail);
      writestart += avail;
      towrite -= avail;
      if ((TSIOBufferProduce(bufp, avail)) == TS_ERROR)
        LOG_ERROR_AND_CLEANUP("TSIOBufferProduce");
      if ((blockp = TSIOBufferStart(bufp)) == TS_ERROR_PTR)
        LOG_ERROR_AND_CLEANUP("TSIOBufferStart");
      if ((start = TSIOBufferBlockWriteStart(blockp, &avail)) == TS_ERROR_PTR)
        LOG_ERROR_AND_CLEANUP("TSIOBufferBlockWriteStart");
    } else {
      memcpy(start, &STRING_CONSTANT[writestart], towrite);
      writestart += towrite;
      if (TSIOBufferProduce(bufp, towrite) == TS_ERROR)
        LOG_ERROR_AND_CLEANUP("TSIOBufferProduce");
      towrite = 0;
    }
  }


  /* get the next block in the IOBuffer */
  if (TSIOBufferBlockNext(blockp) == TS_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("TSIOBufferBlockNext");
  }

  /* print the read avail and write avail of the iobuffer */
  if ((readerp = TSIOBufferReaderAlloc(bufp)) == TS_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("TSIOBufferReaderAlloc");
  if ((read_avail = TSIOBufferBlockReadAvail(blockp, readerp)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSIOBufferBlockReadAvail");
  if ((write_avail = TSIOBufferBlockWriteAvail(blockp)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSIOBufferBlockWriteAvail");

  TSDebug(DBG_TAG, "read_avail = %d", read_avail);
  TSDebug(DBG_TAG, "write_avail = %d", write_avail);

  /* negative test */
#ifdef DEBUG
  if (TSIOBufferStart(NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSIOBufferStart");
  if (TSIOBufferBlockWriteStart(NULL, &avail) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSIOBufferBlockWriteStart");
  if (TSIOBufferProduce(NULL, 0) != TS_ERROR)
    LOG_ERROR_NEG("TSIOBufferProduce");
  if (TSIOBufferBlockNext(NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSIOBufferBlockNext");

  if (TSIOBufferBlockReadAvail(NULL, readerp) != TS_ERROR)
    LOG_ERROR_NEG("TSIOBufferBlockReadAvail");
  if (TSIOBufferBlockReadAvail(blockp, NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSIOBufferBlockReadAvail");

  if (TSIOBufferBlockWriteAvail(NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSIOBufferBlockWriteAvail");

#endif

  /* cleanup */
Lcleanup:
  TSIOBufferDestroy(bufp);
}


static int
transform_init(TSCont contp, MyData * data)
{
  LOG_SET_FUNCTION_NAME("transform_init");

  TSVConn output_conn;
  TSVIO input_vio;

  TSMBuffer bufp;
  TSMLoc hdr_loc = NULL;
  TSMLoc ce_loc = NULL;        /* for the content encoding mime field */


  if ((output_conn = TSTransformOutputVConnGet(contp)) == TS_ERROR_PTR)
    LOG_ERROR_AND_RETURN("TSTransformOutputVConnGet");
  if ((input_vio = TSVConnWriteVIOGet(contp)) == TS_ERROR_PTR)
    LOG_ERROR_AND_RETURN("TSVConnWriteVIOGet");

  if ((data->output_buffer = TSIOBufferCreate()) == TS_ERROR_PTR)
    LOG_ERROR_AND_RETURN("TSIOBufferCreate");
  if ((data->output_reader = TSIOBufferReaderAlloc(data->output_buffer)) == TS_ERROR_PTR)
    LOG_ERROR_AND_RETURN("TSIOBufferReaderAlloc");
  if ((data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader,
                                        TSVIONBytesGet(input_vio))) == TS_ERROR_PTR)
    LOG_ERROR_AND_RETURN("TSVConnWrite");

  /*
   * Mark the output data as having null content encoding
   */
  if (TSHttpTxnTransformRespGet(data->txn, &bufp, &hdr_loc) != 1) {
    LOG_ERROR_AND_CLEANUP("TSHttpTxnTransformRespGet");
  }

  TSDebug(DBG_TAG, "Adding Content-Encoding mime field");
  if ((ce_loc = TSMimeHdrFieldCreate(bufp, hdr_loc)) == TS_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldCreate");
  if (TSMimeHdrFieldNameSet(bufp, hdr_loc, ce_loc, "Content-Encoding", -1) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldNameSet");
  if (TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, ce_loc, -1, "null", -1) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValueStringInsert");
  if (TSMimeHdrFieldAppend(bufp, hdr_loc, ce_loc) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldAppend");

  /* negative test */
#ifdef DEBUG
  if (TSTransformOutputVConnGet(NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSTransformOutputVConnGet");
#endif

Lcleanup:
  if (VALID_POINTER(ce_loc))
    TSHandleMLocRelease(bufp, hdr_loc, ce_loc);
  if (VALID_POINTER(hdr_loc))
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  data->init_done = 1;
  return 1;
}

static int
handle_transform(TSCont contp)
{
  LOG_SET_FUNCTION_NAME("handle_transform");

  TSVConn output_conn;
  TSVIO input_vio;
  TSIOBuffer input_buffer;
  MyData *data;
  int towrite, avail, ntodo;

  /* Get the output (downstream) vconnection where we'll write data to. */

  if ((output_conn = TSTransformOutputVConnGet(contp)) == TS_ERROR_PTR)
    LOG_ERROR_AND_RETURN("TSTransformOutputVConnGet");

  /* Get the write VIO for the write operation that was performed on
   * ourself. This VIO contains the buffer that we are to read from
   * as well as the continuation we are to call when the buffer is
   * empty. This is the input VIO (the write VIO for the upstream
   * vconnection).
   */
  if ((input_vio = TSVConnWriteVIOGet(contp)) == TS_ERROR_PTR)
    LOG_ERROR_AND_RETURN("TSVConnWriteVIOGet");

  /* test VIO */
  test_vio(contp);

  /* Get our data structure for this operation. The private data
   * structure contains the output VIO and output buffer. If the
   * private data structure pointer is NULL, then we'll create it
   * and initialize its internals.
   */
  if ((data = TSContDataGet(contp)) == TS_ERROR_PTR)
    LOG_ERROR_AND_RETURN("TSContDataGet");

  if (data->init_done == 0) {
    transform_init(contp, data);
  }

  /* We also check to see if the input VIO's buffer is non-NULL. A
   * NULL buffer indicates that the write operation has been
   * shutdown and that the upstream continuation does not want us to send any
   * more WRITE_READY or WRITE_COMPLETE events. For this simplistic
   * transformation that means we're done. In a more complex
   * transformation we might have to finish writing the transformed
   * data to our output connection.
   */
  if ((input_buffer = TSVIOBufferGet(input_vio)) == TS_ERROR_PTR)
    LOG_ERROR_AND_RETURN("TSVIOBufferGet");

  /* negative test */
#ifdef DEBUG
  if (TSVIOBufferGet(NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSVIOBufferGet");
  if (TSVIOReaderGet(NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSVIOReaderGet");
  if (TSVIONTodoGet(NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSVIONTodoGet");
  if (TSVIONDoneGet(NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSVIONDoneGet");

  if (TSVIONBytesSet(NULL, 1) != TS_ERROR)
    LOG_ERROR_NEG("TSVIONBytesSet");
  if (TSVIONBytesSet(data->output_vio, -1) != TS_ERROR)
    LOG_ERROR_NEG("TSVIONBytesSet");

  if (TSVIONDoneSet(NULL, 1) != TS_ERROR)
    LOG_ERROR_NEG("TSVIONDoneSet");
  if (TSVIONDoneSet(input_vio, -1) != TS_ERROR)
    LOG_ERROR_NEG("TSVIONDoneSet");
#endif

  if (!input_buffer) {
    if (TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio)) == TS_ERROR)
      LOG_ERROR_AND_RETURN("TSVIONBytesSet");
    if (TSVIOReenable(data->output_vio) == TS_ERROR)
      LOG_ERROR_AND_RETURN("TSVIOReenable");
    return;
  }

  /* Determine how much data we have left to read. For this null
   * transform plugin this is also the amount of data we have left
   * to write to the output connection.
   */
  if ((towrite = TSVIONTodoGet(input_vio)) == TS_ERROR)
    LOG_ERROR_AND_RETURN("TSVIONTodoGet");
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
     * the amount of data actually in the read buffer.
     */
    if ((avail = TSIOBufferReaderAvail(TSVIOReaderGet(input_vio))) == TS_ERROR)
      LOG_ERROR_AND_RETURN("TSIOBufferReaderAvail");

    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the output buffer. */
      if (TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(input_vio), towrite, 0) == TS_ERROR)
        LOG_ERROR_AND_RETURN("TSIOBufferCopy");

      /* negative test for TSIOBufferCopy */
#ifdef DEBUG
      if (TSIOBufferCopy(NULL, TSVIOReaderGet(input_vio), towrite, 0) != TS_ERROR)
        LOG_ERROR_NEG("TSIOBufferCopy");
      if (TSIOBufferCopy(TSVIOBufferGet(data->output_vio), NULL, towrite, 0) != TS_ERROR)
        LOG_ERROR_NEG("TSIOBufferCopy");
      if (TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(input_vio), -1, 0) != TS_ERROR)
        LOG_ERROR_NEG("TSIOBufferCopy");
      if (TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(input_vio), towrite, -1) != TS_ERROR)
        LOG_ERROR_NEG("TSIOBufferCopy");
#endif

      /* Tell the read buffer that we have read the data and are no
       * longer interested in it.
       */
      if (TSIOBufferReaderConsume(TSVIOReaderGet(input_vio), towrite) == TS_ERROR)
        LOG_ERROR_AND_RETURN("TSIOBufferReaderConsume");

      /* Modify the input VIO to reflect how much data we've
       * completed.
       */
      if (TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + towrite) == TS_ERROR)
        LOG_ERROR_AND_RETURN("TSVIONDoneSet");
    }
  }

  /* Now we check the input VIO to see if there is data left to
   * read.
   */
  if ((ntodo = TSVIONTodoGet(input_vio)) == TS_ERROR)
    LOG_ERROR_AND_RETURN("TSVIONTodoGet");

  if (ntodo > 0) {
    if (towrite > 0) {
      /* If there is data left to read, then we reenable the output
       * connection by reenabling the output VIO. This will wake up
       * the output connection and allow it to consume data from the
       * output buffer.
       */
      if (TSVIOReenable(data->output_vio) == TS_ERROR)
        LOG_ERROR_AND_RETURN("TSVIOReenable");

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
    if (TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio)) == TS_ERROR)
      LOG_ERROR_AND_RETURN("TSVIONBytesSet");
    if (TSVIOReenable(data->output_vio) == TS_ERROR)
      LOG_ERROR_AND_RETURN("TSVIOReenable");

    /* Call back the input VIO continuation to let it know that we
     * have completed the write operation.
     */
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }

}

static int
null_transform(TSCont contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("null_transform");

  /* Check to see if the transformation has been closed by a call to
   * TSVConnClose.
   */
  if (TSVConnClosedGet(contp)) {
    my_data_destroy(TSContDataGet(contp));
    if (TSContDestroy(contp) == TS_ERROR)
      LOG_ERROR("TSContDestroy");
    return 0;
  } else {
    switch (event) {
    case TS_EVENT_ERROR:
      {
        TSVIO input_vio;

        /* Get the write VIO for the write operation that was
         * performed on ourself. This VIO contains the continuation of
         * our parent transformation. This is the input VIO.
         */
        if ((input_vio = TSVConnWriteVIOGet(contp)) == TS_ERROR_PTR)
          LOG_ERROR_AND_RETURN("TSVConnWriteVIOGet");

        /* Call back the write VIO continuation to let it know that we
         * have completed the write operation.
         */
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
      }
      break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      /* When our output connection says that it has finished
       * reading all the data we've written to it then we should
       * shutdown the write portion of its connection to
       * indicate that we don't want to hear about it anymore.
       */
      if (TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1) == TS_ERROR)
        LOG_ERROR_AND_RETURN("TSVConnShutdown");
      break;
    case TS_EVENT_VCONN_WRITE_READY:
      handle_transform(contp);
      break;

    case TS_EVENT_IMMEDIATE:
      handle_transform(contp);
      break;

    default:
      handle_transform(contp);
      break;
    }
  }

  /* negative test */
#ifdef DEBUG
  if (TSVConnClosedGet(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSVConnClosedGet");
  }
  if (TSVIOContGet(NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSVIOContGet");
  }
#endif

  return 0;
}

static int
transformable(TSHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("transformable");

  /*
   *  We are only interested in transforming "200 OK" responses.
   */
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSHttpStatus resp_status;

  if (!TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc))
    LOG_ERROR_AND_RETURN("TSHttpTxnServerRespGet");

  if ((resp_status = TSHttpHdrStatusGet(bufp, hdr_loc)) == TS_ERROR)
    LOG_ERROR_AND_RETURN("TSHttpHdrStatusGet");

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  return ((resp_status == TS_HTTP_STATUS_OK) ? 1 : 0);
}

static int
transform_add(TSHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("transform_add");
  MyData *data;
  TSVConn connp;

  if ((connp = TSTransformCreate(null_transform, txnp)) == TS_ERROR_PTR)
    LOG_ERROR_AND_RETURN("TSTransformCreate");

  if (TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp) == TS_ERROR)
    LOG_ERROR_AND_RETURN("TSHttpTxnHookAdd");

  data = my_data_alloc();
  data->txn = txnp;
  if (TSContDataSet(connp, data) == TS_ERROR)
    LOG_ERROR_AND_RETURN("TSContDataSet");

  /* Cache the transformed content */
  if (TSHttpTxnUntransformedRespCache(txnp, 0) == TS_ERROR)
    LOG_ERROR_AND_RETURN("TSHttpTxnUntransformedRespCache");
  if (TSHttpTxnTransformedRespCache(txnp, 1) == TS_ERROR)
    LOG_ERROR_AND_RETURN("TSHttpTxnTransformedRespCache");

  /* negative test for TSHttpTxnTransformedRespCache */
#ifdef DEBUG
  if (TSHttpTxnUntransformedRespCache(NULL, 0) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpTxnUntransformedRespCache");

  if (TSHttpTxnTransformedRespCache(NULL, 1) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpTxnTransformedRespCache");

  if (TSTransformCreate(null_transform, NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSTransformCreate");
#endif
}

static int
transform_plugin(TSCont contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("transform_plugin");
  TSHttpTxn txnp = (TSHttpTxn) edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    if (transformable(txnp)) {
      transform_add(txnp);
    }
    if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR)
      LOG_ERROR_AND_RETURN("TSHttpTxnReenable");

    return 0;
  default:
    break;
  }

  return 0;
}

int
check_ts_version()
{

  const char *ts_version = TSTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Since this is an TS-SDK 2.0 plugin, we need at
       least Traffic Server 2.0 to run */
    if (major_ts_version >= 2) {
      result = 1;
    }
  }

  return result;
}

void
TSPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("TSPlugiInit");
  TSPluginRegistrationInfo info;

  info.plugin_name = "null-transform";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!TSPluginRegister(TS_SDK_VERSION_3_0, &info)) {
    TSError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 3.0 or later\n");
    return;
  }

  test_iobuffer();

  if (TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(transform_plugin, NULL)) == TS_ERROR)
    LOG_ERROR("TSHttpHookAdd");
}
